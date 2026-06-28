//========================================================================================
//
//  KESCMDrawEventHandler.h
//
//  差分オーバーレイの描画エンジン。ページUID→オーバーレイ(KESCMOverlayEntry)を保持し、
//  スプレッド描画イベント時にリング/変更数/旧版べた載せ/トーストを描く。共有状態は public static
//  メンバとして公開し、他モジュール(peek/トースト/コア処理)から参照させる。
//
//========================================================================================
#ifndef __KESCMDrawEventHandler_h__
#define __KESCMDrawEventHandler_h__

#include <map>
#include "KESCMConstants.h"
#include "CPMUnknown.h"
#include "IDrwEvtHandler.h"
#include "GraphicsExternal.h"   // AGMImageRecord (構造体メンバ)
#include "UIDRef.h"             // UID / UIDRef
#include "PMString.h"
#include "PMReal.h"

class IDataBase;
class IDrwEvtDispatcher;
class IControlView;
class IPanorama;

struct KESCMOverlayEntry
{
	uint8*         buf;			// 自前の ARGB バッファ(リング画像)。所有
	AGMImageRecord rec;			// buf を指す自前の画像レコード(blit 用)
	uint8*         dist;		// 差分マスクのチェスボード距離変換(w*h, uint8, 0=変化画素, clamp255)。所有。
								//   リング = 0<dist<=radius。BuildRing が膨張なしの1パスで塗れる(mask は dist 生成後は破棄)。
	uint8*         bgRed;		// 対象ページが「赤っぽい」画素=1 のマップ(w*h)。リングの青切替に使う。所有(nil可)
	int32          w, h;
	int32          rowBytes;	// buf の行バイト数(= rec.byteWidth)
	int32          bpp;			// バイト/ピクセル
	int32          lastRadius;	// 最後に描いたリング半径(px)。-1=未描画
	int32          changeCount;	// このページの変更(枠)の数=マスクの連結成分数。テキスト表示用

	KESCMOverlayEntry() : buf(nil), dist(nil), bgRed(nil), w(0), h(0), rowBytes(0), bpp(0), lastRadius(-1), changeCount(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KESCMOverlayEntry()
	{
		if (buf)   delete[] buf;
		if (dist)  delete[] dist;
		if (bgRed) delete[] bgRed;
	}
};


//========================================================================================
// KESCMOrigImage
//   1ページ分の「旧版(比較相手)」の不透明画像。kescmShowOriginal 実行時にその場で生成して保持し、
//   トグルが ON の間、対応するページの矩形いっぱいに不透明 blit する(べた載せ)。
//   SnapshotUtilsEx/accessor は保持せず、画素を buf へコピーして自前 rec を組む(切り離し=破棄時クラッシュ回避)。
//========================================================================================
struct KESCMOrigImage
{
	uint8*         buf;			// 自前の画像バッファ(不透明)。所有
	AGMImageRecord rec;			// buf を指す自前の画像レコード(blit 用)
	int32          w, h;
	int32          rowBytes;
	int32          bpp;

	KESCMOrigImage() : buf(nil), w(0), h(0), rowBytes(0), bpp(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KESCMOrigImage()
	{
		if (buf) delete[] buf;
	}
};


//========================================================================================
// KESCMDrawEventHandler
//   ページUID→オーバーレイの集合を保持し、スプレッド描画時に、そのスプレッドに属する
//   各ページのリングを blit する。リング太さは描画時のズームに追従。非永続=.indd に残らない。
//========================================================================================
class KESCMDrawEventHandler : public CPMUnknown<IDrwEvtHandler>
{
public:
	KESCMDrawEventHandler(IPMUnknown* boss) : CPMUnknown<IDrwEvtHandler>(boss) {}
	~KESCMDrawEventHandler() {}

	virtual void Register(IDrwEvtDispatcher* d);
	virtual void UnRegister(IDrwEvtDispatcher* d);
	virtual bool16 HandleDrawEvent(ClassID eventID, void* eventData);

	// ページUID → オーバーレイ。変化のあったページだけ登録される。
	static std::map<UID, KESCMOverlayEntry*> sEntries;
	// 全エントリが属する単一ドキュメント。別dbをmarkしたら作り直す(UIDはdb内のみ一意なため)。
	static IDataBase* sDB;
	// 上書き表示(変更リング)の master 表示トグル。データ(sEntries)は消さず
	// 表示だけ切り替える。★既定=kFalse(非表示)。シングルミドルボタンを押している間だけ kTrue にして枠等を
	// 表示し、離すと kFalse に戻す。kFalse の間はこれら全部を描かない。旧版べた載せ(sShowOriginal)は
	// このトグルの影響を受けない(ダブルクリックで別管理)。
	static bool16 sMarksVisible;
	// 画面マーク(リング＋変更数)に掛ける「実効」不透明度。★既定=1.0(不透明)。リング blit と数字 show の双方に同率。
	//   ・ミドルのみ押下中 = kKESCMFaintOpacity(≒0.3)   ・Shift+Alt 押下中 = 1.0(不透明)
	//   ・押していない常時表示時 = 基準値 KESCMBaseScreenOpacity()(印刷ON＋30%なら0.3 / それ以外1.0)
	static PMReal sMarkScreenOpacity;
	// 変更マーク(リング＋変更数)を印刷/PDF にも出すか(kescmSetPrintMarks)。★既定=kFalse(画面のみ)。
	// ON の間は、ミドル押下に関係なく画面でも常時表示(WYSIWYG)＋印刷/PDF にも描く。マークデータとは独立に保持。
	static bool16 sPrintMarks;
	// 印刷/PDF 時のマーク不透明度を薄く(約30%)するか(kescmSetPrintMarks の第2引数 faint)。★既定=kFalse(通常=不透明)。
	// 印刷経路(リング=KESCMDrawRingForPrint / 変更数テキスト)に効くほか、印刷ON＋30%中は画面の常時表示の
	// 基準不透明度(KESCMBaseScreenOpacity)も0.3に下げる(画面と印刷の見た目を一致)。
	static bool16 sPrintFaint;
	// 自前のラスタ化(MakeEntry/MakeOrigImage の SnapshotUtilsEx::Draw)中だけ kTrue。HandleDrawEvent が
	// 再入したらマークを描かない(自己参照防止)。kPreviewMode ビットに頼ると PDF 書き出し(同ビット)を巻き込むため。
	static bool16 sRasterizing;

	// 旧版べた載せ(kescmShowOriginal / kescmHideOriginal)。マーク(sEntries)とは完全に独立。
	// 実行時に覗いたページの旧版画像を sOrigImages に保持し、sShowOriginal が ON の間その db のページに不透明 blit する。
	static std::map<UID, KESCMOrigImage*> sOrigImages;	// ページUID(新) → 旧版画像
	static IDataBase* sOrigDB;							// 旧版画像が属する単一ドキュメント(別dbに切替えたら作り直す)
	static bool16 sShowOriginal;						// べた載せ表示 ON/OFF(既定 OFF)
	static PMReal sOrigScale;							// 旧版画像をラスタ化した時の content→window スケール(ズーム×デバイス倍率)。
														// 再 peek 時にズームが変わっていたら作り直す基準。0=未設定
	static PMReal sPeekOpacity;							// 覗き中(peek)の旧版べた載せの不透明度。Shift＋ミドル=1.0(不透明)/
														// Ctrl＋ミドル=0.5(半透明)。描画ブロックが参照する

	// 一時トースト(画面=可視領域の中央に少し出て自動で消えるメッセージ)。マーク等とは完全に独立。
	// sToastDB のドキュメントの前面ビューにだけ描く。自動消去は IIdleTask(KESCMToastIdleTask)が担う。
	static PMString   sToastMsg;		// 表示文字列
	static bool16     sToastVisible;	// 表示中か(タイマで kFalse に戻す)
	static IDataBase* sToastDB;			// トーストを描くドキュメント(前面)

	// 距離変換 dist を使い、buf(ARGB)へリング(0<dist<=radius)を1パスで描く(膨張不要)。
	// 各リング画素の色は、その位置の背景が赤っぽい(bgRed[idx])なら青、そうでなければ赤。
	// リング以外の画素は透明(alpha=0)。dist は KESCMDistTransform で事前生成(0=変化画素)。
	static void BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
		const uint8* dist, const uint8* bgRed, int32 radius);

	// target/source を高解像度(kKESCMResolution×kKESCMHiResMul)で CMYK ラスタ化し、4ch を比較
	// (しきい値 kKESCMCmykThr)。変化px数>0 のときだけ sEntries[target.UID] にエントリ登録(既存は置換)。
	// changed に「変化したか」を返す。
	static ErrorCode MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed);

	// sourceRef(旧)を resolution(dpi)で1枚だけラスタ化し、不透明画像を sOrigImages[target.UID] に
	// 保持(既存は置換)。オフスクリーンは即破棄=同時に1枚しか生存しない(安全)。
	// resolution 既定=kKESCMOrigResolution。peek 経路では現在のズームから dpi=72×スケールを渡して常にくっきり。
	static ErrorCode MakeOrigImage(const UIDRef& targetRef, const UIDRef& sourceRef, const PMReal& resolution = kKESCMOrigResolution);

	// 全エントリ破棄(kescmClearMarks / 別ドキュメント切替時)。
	static void DropAll()
	{
		for (std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.begin(); it != sEntries.end(); ++it)
			delete it->second;
		sEntries.clear();
		sDB = nil;
	}

	// 旧版画像を全破棄(kescmClearMarks / 別ドキュメント切替時)。表示トグルもOFFへ。
	static void DropAllOrig()
	{
		for (std::map<UID, KESCMOrigImage*>::iterator it = sOrigImages.begin(); it != sOrigImages.end(); ++it)
			delete it->second;
		sOrigImages.clear();
		sOrigDB = nil;
		sShowOriginal = kFalse;
		sOrigScale = 0.0;
	}
};

// IControlView から可視範囲(IPanorama)を辿る小ヘルパ。エンジンと peek の双方で使うため公開する。
IPanorama* KESCMQueryPanorama(IControlView* view);

#endif // __KESCMDrawEventHandler_h__
