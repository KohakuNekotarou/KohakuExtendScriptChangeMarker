//========================================================================================
//
//  KohakuExtendScriptChangeMarker (KESCM)
//  ---------------------------------------------------------------------------------------
//  2 つのドキュメント(版)の違いを、変化箇所を囲む「半透明の枠(リング)」で示す
//  非破壊オーバーレイ。内容は下の実ページがそのまま透けて見えるので隠れない。
//  リング色は通常は赤。ただし枠の下の実ページが赤っぽい画素では、赤枠が背景に埋もれるのを
//  避けるため、その画素だけ青へ切り替える(画素単位の色適応)。
//
//  ★複数ページ対応(2026-06-21): 対象を「1ページ」から「ページUID集合(std::map)」へ拡張。
//     kEndSpreadMessage は描画されるスプレッドごとに発火するので、低ズームで複数スプレッドが
//     同時に見えても、各スプレッドの描画イベントで各々のページが描かれ、自動的に全部に出る。
//     変化が無いページはエントリを作らない(マスクも持たない=描画でも即スキップ)。
//
//  API:
//   Document.kescmMarkChangesDoc(sourceDoc):
//     このドキュメントの全ページを sourceDoc の同じ番号のページと CMYK 比較し、変化ページ全部に
//     リングを付ける(総入れ替え)。ページ数が違う場合は重なる範囲のみ比較。
//   Page/Document.kescmClearMarks():
//     全エントリを破棄して再描画し、オーバーレイを消す。
//   (旧版べた載せはミドルボタン peek = kescmArmMousePeek/kescmDisarmMousePeek に統合。
//    トースト=kescmToast、印刷=kescmSetPrintMarks。単ページ版 kescmMarkChanges /
//    手動 kescmShowOriginal/HideOriginal/ShowOriginalUnderMouse は 2026-06-24 に撤去。)
//
//  装飾(IPageItemAdornmentList)ではなく DrawEventHandler で描くので、書類モデルに一切
//  触れない=.indd に保存されない(missing-plugin 警告も出ない)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IScript.h"
#include "IScriptRequestData.h"

// General includes:
#include "CScriptProvider.h"
#include "CPMUnknown.h"					// 実装基底
#include "CServiceProvider.h"			// IK2ServiceProvider 実装基底

// Object model:
#include "PersistUtils.h"				// ::GetUIDRef
#include "IDataBase.h"
#include "IGeometry.h"					// ページ bbox
#include "IDocument.h"					// InvalidateViews に渡す IDocument
#include "ILayoutUtils.h"				// InvalidateViews(即時再描画)
#include "ILayoutUIUtils.h"				// QueryFrontView(マウスが乗っている前面レイアウトビュー)
#include "IEventUtils.h"				// GetGlobalMouseLocation(現在のマウス位置)
#include "IApplication.h"				// QueryDocumentList / app boss = IEventDispatcher
#include "IDocumentList.h"				// FindDocByDataBase(arm 済み DB の生存確認=ダングリング回避)
#include "ISpread.h"					// changedBy(スプレッド)→ページ列
#include "ISpreadList.h"				// ドキュメント→全スプレッド列挙(doc単位の一括mark)
#include "IShape.h"						// kPreviewMode / kPrinting フラグ

// 描画 / Draw Event:
#include "IDrwEvtHandler.h"				// IDrwEvtHandler / DrawEventData
#include "IDrwEvtDispatcher.h"			// RegisterHandler / kDEHLowestPriority

// ミドルボタン peek(イベント監視):
#include "IEventWatcher.h"				// 非消費スヌープ(kMButtonDn/Up)
#include "IEvent.h"						// IEvent::kMButtonDn / kMButtonUp / GetType
#include "IEventDispatcher.h"			// AddEventWatcher / EventTypeList
#include "IStartupShutdownService.h"	// 起動時に watcher を開始
#include "CreateObject.h"				// CreateObject2<IEventWatcher> / CreateObject2<IIdleTask>
#include "IIdleTask.h"					// 一時トーストの自動消去タイマ(RunTask で消す)
#include "IIdleTaskMgr.h"				// AddTask / RemoveTask(タイマ登録)
#include "IToolBoxUtils.h"				// ツール切替(ミドル押下中だけハンドツール)
#include "ITool.h"						// ITool
#include "LayoutUIID.h"					// kGrabberHandToolBoss / kPointerToolBoss
#include "DocumentContextID.h"			// kEndSpreadMessage
#include "GraphicsID.h"					// kDrawEventService
#include "GraphicsData.h"				// GraphicsData::GetGraphicsPort / GetView / GetViewPortAttributes
#include "IViewPortAttributes.h"		// GetAttr(kSepPrvOPPEnabledVPAttr) でオーバープリントプレビュー検出
#include "OutPrvID.h"					// kSepPrvOPPEnabledVPAttr(オーバープリントプレビュー有効フラグ)
#include "IGraphicsPort.h"				// image() / translate / scale
#include "AutoGSave.h"					// 描画状態の save/restore
#include "IControlView.h"				// GetContentToWindowMatrix(現ズーム)
#include "IPanorama.h"					// 可視範囲の上端を content 座標で取る(拡大時の数値の縦位置追従)
#include "IWidgetParent.h"				// QueryParentFor(子ウィジェットから親 LayoutWidget の panorama を辿る)
#include "ISession.h"					// GetExecutionContextSession(既定フォント取得)
#include "IFontMgr.h"					// 既定フォント取得(framelabel/TEST と同じ)
#include "IPMFont.h"					// IPMFont(selectfont に渡す)
#include "PMMatrix.h"
#include "PMPoint.h"					// 回転オブジェクトの角の点を spread 座標へ変換
#include "PMReal.h"						// ::ToInt32 / ::Round
#include "TransformUtils.h"				// ::InnerToSpreadMatrix / ::GetDataBase

// ラスタ化:
#include "SnapshotUtilsEx.h"			// ページをオフスクリーン(ビットマップ)化
#include "AGMImageAccessor.h"			// GetBounds() / GetBaseAddr() / GetAGMColorFamily()
#include "GraphicsExternal.h"			// AGMImageRecord(自前で組んで blit する)
#include "IXPUtils.h"					// 印刷用: CreateImagePaintServer / ReleasePaintServer(透明合成経由のリング描画)

// STL:
#include <map>
#include <vector>
#include <string.h>						// memcpy

// Project includes:
#include "KESCMScriptingDefs.h"
#include "KESCMID.h"
#include "KESCMCore.h"				// shared ops exposed to the panel UI (KESCMPanelObserver.cpp)


//========================================================================================
// チューニング定数
//========================================================================================
static const PMReal kKESCMRingTargetPx = 8.0;	// リングの目標太さ(画面px)。ズームに依らず一定に見せる
static const uint8 kKESCMRingAlpha = 255;	// リングの基本アルファ(0..255)。「通常」=不透明(255)。薄表示は setopacity 側で行う(30%→255×0.3=実30%)
static const PMReal kKESCMFaintOpacity = 0.25;	// 薄表示(ミドルのみ/印刷30%系)の不透明度。小さいほど薄い。★現在25%
// 変化判定: 常に CMYK でラスタ化して比較し、CMYK 4ch のどれかがこのしきい値を超えて違えば「変化」とする。
// しきい値=0 は「どんな差も拾う」(=CMYK 1単位でも検出)。CMYK の微差は RGB へ変換すると丸めで消えるため、
// CMYK のまま比較するのが要点(ユーザーは CMYK 数値で考える)。画像/効果の再描画ゆらぎでノイズが出るなら 1〜2 に上げる。
static const int   kKESCMCmykThr = 0;
static const int32 kKESCMBaseRadius = 4;	// リング初期半径(画像px)。描画時にズームから再算出するための初期値
static const PMReal kKESCMResolution = 72.0;	// 保存・表示のラスタ解像度(dpi)。リング画像/マスクはこの解像度で持つ(軽い)
// 【取りこぼし防止】比較だけ高解像度で行い、結果を低解像度に圧縮(マックスプーリング)して記憶する。
// 比較解像度 = kKESCMResolution × kKESCMHiResMul。低解像度では平均化で消える細線/微小ズレを満額で拾う。
static const PMReal kKESCMHiResMul    = 2.0;	// 比較解像度の倍率(2=144dpi)。上げるほど検出力↑/一時メモリ↑。300dpi 相当なら≒4.17
static const int32  kKESCMPoolMinCount = 1;	// プーリング: 低解像度1セル内の「高解像度の変化画素数」がこの値以上で変化と判定。
											// 1=最高感度(縁ノイズも拾う)/大きいほどノイズ耐性↑(取りこぼしのリスクも僅かに増)

// リング色: 通常は赤。ただし枠の下の実ページが「赤っぽい」画素の上では、半透明の赤枠が背景に埋もれて
// 見えなくなるため、視認性確保のためにシアンへ切り替える(画素単位)。シアン=赤の補色(色相180°反対)で、
// 明るさも高い(輝度≒0.79)ため赤上で明暗・色相とも最大コントラスト。純青は暗く細線で沈むため不採用。
static const uint8 kKESCMRingR = 255, kKESCMRingG = 0,   kKESCMRingB = 0;		// 通常(赤)
static const uint8 kKESCMRingAltR = 0,   kKESCMRingAltG = 255, kKESCMRingAltB = 255;	// 赤背景の上(シアン=赤の補色)
static const int   kKESCMRedBgDom = 25;	// 背景を「赤っぽい」と判定する R 優位の閾値(R が G,B の双方より これ以上大きい)。小さいほどピンク/薄い赤も拾う

// 変更数テキストの色。数字=赤、後ろの語=シアン。setrgbcolor(0..1)。リング(赤/シアン)とは別系統。
static const PMReal kKESCMCountNumR  = 1.0, kKESCMCountNumG  = 0.0, kKESCMCountNumB  = 0.0;	// 数字 赤
static const PMReal kKESCMMarkR = 0.0, kKESCMMarkG = 1.0, kKESCMMarkB = 1.0;					// 語 シアン

// 変更数テキスト(各ページの変更=枠の数を「N chg」で表示)。常時表示(トグル無し。X廃止)。ズームで大きさ不変。
// 位置=各ページ上端から少し下、ページ「内側」に横中央で置く。数字は赤fill＋細い赤ストロークでやや太字、白い縁(ハロー)付き。
// 白縁=本体の前に白を太めのストロークで描き(線幅=文字サイズ×kKESCMCountHaloFrac)。ストロークは輪郭中心なので
// 見える白縁は線幅の約半分。本体の赤fill＋赤ストロークが内側半分を覆い、外側に白リムが残る。
static const PMReal kKESCMCountTextPx   = 20.0;	// 数字の文字サイズ(画面px)
static const PMReal kKESCMCountInsetPx  = 6.0;	// ページ上端からの内側余白(画面px)
static const PMReal kKESCMCountHaloFrac = 0.16;	// 白縁の太さ(文字サイズに対する比)。見える縁はこの約半分。大きいほど太い縁
static const PMReal kKESCMCountPrintHaloFrac = 0.08;	// 印刷時のみの白縁太さ(文字サイズ比)。画面の半分=白縁を少なく
static const PMReal kKESCMCountBodyFrac = 0.05;	// 赤本体を太らせるストローク幅(文字サイズ比)。大きいほど太い赤文字。0でfillのみ(最も細い)
static const PMReal kKESCMCountPrintThinFrac = 0.03;	// 印刷時のみ: 赤fillの外縁を白で削って細く見せるストローク幅(文字サイズ比)。大きいほど細い。0.1は削りすぎでステムが消えるため0.03(削る総幅=numPt×値)
static const PMReal kKESCMCountWordThinFrac  = 0.04;	// 印刷時のみ: 語("chg")の外縁を白で削って細く見せるストローク幅(語サイズ比)。半分くらい細く。大きいほど細い(削りすぎると消える)
// 数字の後ろに続く語(" chg")。小さめ・細め(fill のみ=ストローク無し)で添える。
static const PMReal kKESCMCountWordPx   = 11.0;	// 語の文字サイズ(画面px)。数字より小さめ
static const PMReal kKESCMCountDropPx   = 5.0;	// 数字ベースラインをページ上端基準からさらに下げる量(画面px)
// 縦ドリフト抑制: ページ内に置く落とし込み量(画面px固定=inset+numPt)は縮小すると spread 座標で増大し
// ページ内を下へ流れる。ページ高さのこの割合を上限にクランプし、上部に留める。
static const PMReal kKESCMCountTopFracMax = 0.30;	// ベースラインの上端からのオフセット上限(ページ高さ比)

// 【文字サイズのズーム連動】画面表示サイズを「拡大率(UIズーム値)」で倍変させる。倍率 M を上の各 px に掛ける。
// 2点 (zoomLo, mulLo) (zoomHi, mulHi) を通る反比例 M = a - b/zoom(割り算1回で軽い)。範囲外は [mulLo, mulHi] にクランプ。
// 低ズームで小さめ・高ズームで大きめ。比例(直線)より低ズーム側で急に立ち上がり高ズーム側で頭打ちになる曲線。
static const PMReal kKESCMCountZoomLo = 0.05;	// 基準ズーム(下)= 5%
static const PMReal kKESCMCountMulLo  = 0.80;	// その時の倍率(現状の0.8倍)
static const PMReal kKESCMCountZoomHi = 1.00;	// 基準ズーム(上)= 100%
static const PMReal kKESCMCountMulHi  = 3.00;	// その時の倍率(現状の3倍)

// 変更数カウント用の併合半径(画像px, 72dpi基準)。数える前にマスクをこの半径で膨張し、近接した変化
// (文字グリフの破片や1段落内の行など)を1つの塊にまとめてから連結成分を数える。これで「文字が変わった
// だけで数が膨大」を防ぎ、見た目の赤いリング(塊)の数に近づける。大きいほど大きくまとめる(数が減る)。
static const int32 kKESCMCountMergeRadius = 8;

// 旧版べた載せ(kescmShowOriginal)で重ねる画像の解像度(dpi)。スクリプト実行時に、対象ページの旧版を
// この解像度で1枚だけラスタ化(オフスクリーン1枚=即破棄)し、不透明でページ矩形いっぱいに重ねる。
// 高いほど鮮明・メモリ大(A4・300dpi で約26〜35MB/ページ)。覗いたページの分だけ保持する。
// 72dpi = 標準(非HiDPI)100%拡大でドキュメント1inch=72px と1:1一致する値。メモリ最軽量(A4で約2MB/頁)・
// 押下時のラスタ化も最速。2x HiDPIで拡大して粗ければ 144 へ戻す。
static const PMReal kKESCMOrigResolution = 72.0;

// 一時トーストメッセージ(画面=可視領域の中央に少し出て自動で消える「ChangeMarker ON」等)。ズーム不変サイズ。
static const PMReal kKESCMToastTextPx    = 28.8;	// 文字サイズ(画面px)。従来36.0の80%
static const PMReal kKESCMToastPadPx     = 12.8;	// 文字周りの内側余白(画面px)。大きいほど背景ボックスが広い。従来16.0の80%
static const uint32 kKESCMToastDefaultMs = 2500;	// 既定の表示時間(ms)。表示後この時間で自動的に消える

// クリック点 CMYK サンプリング(Shift＋Ctrl＋Alt＋ミドル)。クリック周りの極小領域だけを高dpi・CMYK で
// ラスタ化し、中心1画素の生値(0..255)を新・旧で読む。AA は OFF(ベクター縁の中間色を避ける)。
static const PMReal kKESCMSampleDpi    = 300.0;	// サンプリングのラスタ解像度(dpi)
static const PMReal kKESCMSampleHalfPt = 1.0;	// サンプル領域の半幅(pt)。300dpi で約2pt四方≒8px→中心1画素を読む


//========================================================================================
// KESCMOverlayEntry
//   1ページ分のオーバーレイ。★SnapshotUtilsEx / AGMImageAccessor は保持しない。
//   生成時に画素を自前バッファ(buf)へコピーし、buf を指す自前の AGMImageRecord(rec)を組む。
//   こうしてオフスクリーンを完全に切り離す(非推奨 GetAGMImageRecord も不使用)ことで、
//   破棄時クラッシュ(保持した accessor/snapshot の delete 時の二重解放)を回避する。
//   dist は 72dpi 差分マスクの「チェスボード距離変換」(★案A)。変化のあったページにだけ作られる。
//========================================================================================
struct KESCMOverlayEntry
{
	uint8*         buf;			// 自前の ARGB バッファ(リング画像)。所有
	AGMImageRecord rec;			// buf を指す自前の画像レコード(blit 用)
	uint8*         dist;		// ★案A: 差分マスクのチェスボード距離変換(w*h, uint8, 0=変化画素, clamp255)。所有。
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
														// Ctrl＋ミドル=0.5(半透明)。(A2)描画ブロックが参照する

	// 一時トースト(画面=可視領域の中央に少し出て自動で消えるメッセージ)。マーク等とは完全に独立。
	// sToastDB のドキュメントの前面ビューにだけ描く。自動消去は IIdleTask(KESCMToastIdleTask)が担う。
	static PMString   sToastMsg;		// 表示文字列
	static bool16     sToastVisible;	// 表示中か(タイマで kFalse に戻す)
	static IDataBase* sToastDB;			// トーストを描くドキュメント(前面)

	// 距離変換 dist を使い、buf(ARGB)へリング(0<dist<=radius)を1パスで描く(★案A=膨張不要)。
	// 各リング画素の色は、その位置の背景が赤っぽい(bgRed[idx])なら青、そうでなければ赤。
	// リング以外の画素は透明(alpha=0)。dist は KESCMDistTransform で事前生成(0=変化画素)。
	static void BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
		const uint8* dist, const uint8* bgRed, int32 radius);

	// target/source を 72dpi ARGB でラスタ化→差分マスク作成。変化px数>0 のときだけ
	// sEntries[target.UID] にエントリ登録(既存は置換)。changed に「変化したか」を返す。
	// target/source を CMYK ラスタ化して4ch比較(しきい値 kKESCMCmykThr)。変化px数>0 のときだけエントリ登録。
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

CREATE_PMINTERFACE(KESCMDrawEventHandler, kKESCMDrawEventHandlerImpl)

std::map<UID, KESCMOverlayEntry*> KESCMDrawEventHandler::sEntries;
IDataBase* KESCMDrawEventHandler::sDB = nil;
bool16 KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定=非表示。枠等はシングルミドル押下中だけ表示(master トグル)
PMReal KESCMDrawEventHandler::sMarkScreenOpacity = 1.0;	// 既定=不透明。ミドルのみ=30%/Shift+Alt=不透明/印刷30%中の常時表示=30%
bool16 KESCMDrawEventHandler::sPrintMarks = kFalse;	// 既定=画面のみ(印刷/PDF には出さない)
bool16 KESCMDrawEventHandler::sPrintFaint = kFalse;	// 既定=印刷時は通常不透明度(約70%)
bool16 KESCMDrawEventHandler::sRasterizing = kFalse;	// 自前ラスタ化中だけ kTrue(自己参照防止)
std::map<UID, KESCMOrigImage*> KESCMDrawEventHandler::sOrigImages;
IDataBase* KESCMDrawEventHandler::sOrigDB = nil;
bool16 KESCMDrawEventHandler::sShowOriginal = kFalse;	// 既定=非表示(kescmShowOriginal で ON)
PMReal KESCMDrawEventHandler::sOrigScale = 0.0;	// ラスタ化時のズームスケール(0=未設定)
PMReal KESCMDrawEventHandler::sPeekOpacity = 1.0;	// 既定=不透明(Shift peek)。Ctrl peek で 0.5 にする
PMString   KESCMDrawEventHandler::sToastMsg;
bool16     KESCMDrawEventHandler::sToastVisible = kFalse;	// 既定=非表示
IDataBase* KESCMDrawEventHandler::sToastDB = nil;


void KESCMDrawEventHandler::BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
	const uint8* dist, const uint8* bgRed, int32 radius)
{
	if (buf == nil || dist == nil || wt <= 0 || ht <= 0 || bpp < 3)
		return;
	if (radius < 1) radius = 1;
	const int32 colorOff = bpp - 3;
	const uint8 rad = (radius > 255) ? 255 : (uint8)radius;	// dist は uint8 clamp255。半径上限は200<255。
	// ★端クリップ対策: バッファ(=ページ矩形)端から radius 以内に変化があると、外側の帯がページ端を
	//   越える分はバッファ外=描かれず、その辺の枠が痩せて欠ける。対策は「端から radius 以内の変化画素を
	//   内側帯として塗る」だけでよい。ある変化画素が x<radius にあれば、領域は左端から radius 以内に
	//   到達済み=左の外側帯は必ずクリップされるので、接触判定(旧 drow[0] 等)は不要。4辺とも対称に扱う。

	// ★案A: 距離変換の1パス塗り。リング = 0<dist<=radius(=「半径内に変化画素があり、かつ自身は変化画素でない」)。
	// 旧版の横膨張+縦膨張(各 O(W*H) のスライディングウィンドウ)が消え、ズーム段ごとの仕事が約1/3。
	// チェスボード距離ゆえ角型リングで形状は従来と同一。
	for (int32 y = 0; y < ht; ++y)
	{
		uint8* rowB = buf + (size_t)y * rb;
		const uint8* drow = dist + (size_t)y * wt;
		const uint8* brow = (bgRed != nil) ? (bgRed + (size_t)y * wt) : nil;
		for (int32 x = 0; x < wt; ++x)
		{
			uint8* pixT = rowB + (size_t)x * bpp;	// ARGB 先頭=alpha
			uint8* px = pixT + colorOff;
			const uint8 d = drow[x];
			bool16 ring = (d != 0 && d <= rad);		// 外側の帯(従来)
			if (!ring && d == 0)
			{
				// 変化画素が端から radius 以内にあれば、その端の外側帯はクリップ済み=内側に補填する。
				if (x < radius            || (wt - 1 - x) < radius ||
				    y < radius            || (ht - 1 - y) < radius)
					ring = kTrue;
			}
			if (ring)
			{
				// リング画素。下の実ページが赤っぽければ青、そうでなければ赤(画素単位)。
				const bool useAlt = (brow != nil && brow[x]);
				px[0] = useAlt ? kKESCMRingAltR : kKESCMRingR;
				px[1] = useAlt ? kKESCMRingAltG : kKESCMRingG;
				px[2] = useAlt ? kKESCMRingAltB : kKESCMRingB;
				if (bpp >= 4) pixT[0] = kKESCMRingAlpha;	// リング画素の基本アルファ(=255 不透明)。薄表示は setopacity 側
			}
			else { px[0] = 255; px[1] = 255; px[2] = 255; if (bpp >= 4) pixT[0] = 0; }	// 透明
		}
	}
}


//========================================================================================
// ヘルパ: マスク(0/1)を半径 radius で膨張して out(0/1)へ。BuildRing と同じ分離スライディング
//   ウィンドウ(O(W*H))。カウント用に近接変化を併合する目的。out は呼び出し側が確保(w*h)。
//========================================================================================
static void KESCMDilateMask(const uint8* mask, int32 wt, int32 ht, int32 radius, uint8* out)
{
	if (mask == nil || out == nil || wt <= 0 || ht <= 0)
		return;
	const size_t N = (size_t)wt * ht;
	if (radius < 1) { memcpy(out, mask, N); return; }
	uint8* H = new uint8[N];		// 横方向膨張の中間
	if (H == nil) { memcpy(out, mask, N); return; }
	// 横方向膨張
	for (int32 y = 0; y < ht; ++y)
	{
		const uint8* mrow = mask + (size_t)y * wt;
		uint8* hrow = H + (size_t)y * wt;
		int32 cnt = 0, lo = 0, hi = -1;
		for (int32 x = 0; x < wt; ++x)
		{
			const int32 wantHi = (x + radius >= wt) ? wt - 1 : x + radius;
			const int32 wantLo = (x - radius < 0) ? 0 : x - radius;
			while (hi < wantHi) { ++hi; cnt += mrow[hi]; }
			while (lo < wantLo) { cnt -= mrow[lo]; ++lo; }
			hrow[x] = (cnt > 0) ? 1 : 0;
		}
	}
	// 縦方向膨張(H → out)
	for (int32 x = 0; x < wt; ++x)
	{
		int32 cnt = 0, lo = 0, hi = -1;
		for (int32 y = 0; y < ht; ++y)
		{
			const int32 wantHi = (y + radius >= ht) ? ht - 1 : y + radius;
			const int32 wantLo = (y - radius < 0) ? 0 : y - radius;
			while (hi < wantHi) { ++hi; cnt += H[(size_t)hi * wt + x]; }
			while (lo < wantLo) { cnt -= H[(size_t)lo * wt + x]; ++lo; }
			out[(size_t)y * wt + x] = (cnt > 0) ? 1 : 0;
		}
	}
	delete[] H;
}


//========================================================================================
// ヘルパ: 差分マスク(0/1)の連結成分数を数える(=この頁の「変更=枠」の数)。
//   4近傍のフラッドフィル。固定半径で膨張したマスクに対して数えるのでズームに依らず一定。
//========================================================================================
static int32 KESCMCountComponents(const uint8* mask, int32 wt, int32 ht)
{
	if (mask == nil || wt <= 0 || ht <= 0)
		return 0;
	const size_t N = (size_t)wt * ht;
	std::vector<uint8> seen(N, 0);
	std::vector<int32> stack;
	int32 count = 0;
	for (int32 y0 = 0; y0 < ht; ++y0)
	{
		for (int32 x0 = 0; x0 < wt; ++x0)
		{
			const size_t s0 = (size_t)y0 * wt + x0;
			if (mask[s0] == 0 || seen[s0])
				continue;
			++count;
			stack.push_back((int32)s0);
			seen[s0] = 1;
			while (!stack.empty())
			{
				const int32 idx = stack.back(); stack.pop_back();
				const int32 x = idx % wt, y = idx / wt;
				// 4近傍。マスク=1 かつ未訪問なら同一成分。
				if (x > 0)      { const size_t n = idx - 1;  if (mask[n] && !seen[n]) { seen[n] = 1; stack.push_back((int32)n); } }
				if (x < wt - 1) { const size_t n = idx + 1;  if (mask[n] && !seen[n]) { seen[n] = 1; stack.push_back((int32)n); } }
				if (y > 0)      { const size_t n = idx - wt; if (mask[n] && !seen[n]) { seen[n] = 1; stack.push_back((int32)n); } }
				if (y < ht - 1) { const size_t n = idx + wt; if (mask[n] && !seen[n]) { seen[n] = 1; stack.push_back((int32)n); } }
			}
		}
	}
	return count;
}


//========================================================================================
// ヘルパ: 差分マスク(0/1)のチェスボード距離変換 → out(uint8, 0=変化画素, clamp255)。★案A。
//   各画素に「最も近い変化画素までのチェスボード距離(=max(|dx|,|dy|))」を入れる。リング描画は
//   out の閾値処理(0<out<=radius)だけで済む。8近傍・全コスト1の二パス chamfer(前進+後退)。
//   out は呼び出し側が確保(w*h)。
//========================================================================================
static void KESCMDistTransform(const uint8* mask, int32 wt, int32 ht, uint8* out)
{
	if (mask == nil || out == nil || wt <= 0 || ht <= 0)
		return;
	const size_t N = (size_t)wt * ht;
	for (size_t i = 0; i < N; ++i)
		out[i] = mask[i] ? 0 : (uint8)255;

	// 前進パス(左上→右下): 既処理の (左, 上, 左上, 右上) から +1。
	for (int32 y = 0; y < ht; ++y)
	{
		for (int32 x = 0; x < wt; ++x)
		{
			const size_t idx = (size_t)y * wt + x;
			if (out[idx] == 0) continue;
			int32 best = out[idx];
			if (x > 0)                    { int32 v = (int32)out[idx - 1]      + 1; if (v < best) best = v; }
			if (y > 0)                    { int32 v = (int32)out[idx - wt]     + 1; if (v < best) best = v; }
			if (y > 0 && x > 0)           { int32 v = (int32)out[idx - wt - 1] + 1; if (v < best) best = v; }
			if (y > 0 && x < wt - 1)      { int32 v = (int32)out[idx - wt + 1] + 1; if (v < best) best = v; }
			if (best > 255) best = 255;
			out[idx] = (uint8)best;
		}
	}
	// 後退パス(右下→左上): 既処理の (右, 下, 右下, 左下) から +1。
	for (int32 y = ht - 1; y >= 0; --y)
	{
		for (int32 x = wt - 1; x >= 0; --x)
		{
			const size_t idx = (size_t)y * wt + x;
			if (out[idx] == 0) continue;
			int32 best = out[idx];
			if (x < wt - 1)               { int32 v = (int32)out[idx + 1]      + 1; if (v < best) best = v; }
			if (y < ht - 1)               { int32 v = (int32)out[idx + wt]     + 1; if (v < best) best = v; }
			if (y < ht - 1 && x > 0)      { int32 v = (int32)out[idx + wt - 1] + 1; if (v < best) best = v; }
			if (y < ht - 1 && x < wt - 1) { int32 v = (int32)out[idx + wt + 1] + 1; if (v < best) best = v; }
			if (best > 255) best = 255;
			out[idx] = (uint8)best;
		}
	}
}


ErrorCode KESCMDrawEventHandler::MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed)
{
	changed = kFalse;
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// ★案1(ラスタ化 3→2): 旧版は別途 72dpi の target(snapL)もラスタ化していたが、その画素は
	//   BuildRing が buf を全上書きするため一切使われていなかった。低解像度の寸法は高解像度から割り戻し、
	//   背景の「赤っぽい」判定(bgRed)も高解像度 target をプーリングして作るので、snapL は不要=削除。
	// 【高解像度】差分検出用。target / source を高dpi(kKESCMResolution×kKESCMHiResMul)でラスタ化。
	// 低解像度では平均化で消える細線/微小ズレを満額の差分画素として拾い、取りこぼしを防ぐ。
	const PMReal hiRes = kKESCMResolution * kKESCMHiResMul;
	// 比較は常に CMYK 4ch を不透明ラスタ化して行う(CMYK の微差が RGB 変換で消えるのを回避)。
	// 表示リングは別途 ARGB で合成するので、比較ラスタは不透明(addTransparencyAlpha=kFalse)でよい。
	SnapshotUtilsEx* snapTH = new SnapshotUtilsEx(targetRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	sRasterizing = kTrue;	// この Draw 中に再入する HandleDrawEvent はマークを描かない(自己参照防止)
	ErrorCode drewTH = snapTH->Draw(IShape::kPreviewMode);
	sRasterizing = kFalse;
	AGMImageAccessor* accTH = (drewTH == kSuccess) ? snapTH->CreateAGMImageAccessor() : nil;

	SnapshotUtilsEx* snapSH = new SnapshotUtilsEx(sourceRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	sRasterizing = kTrue;
	ErrorCode drewSH = snapSH->Draw(IShape::kPreviewMode);
	sRasterizing = kFalse;
	AGMImageAccessor* accSH = (drewSH == kSuccess) ? snapSH->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (accTH != nil && accSH != nil)
	{
		// 高解像度(比較)の寸法・バッファ
		Int32Rect bth = accTH->GetBounds();
		Int32Rect bsh = accSH->GetBounds();
		const int32 wth = bth.right - bth.left, hth = bth.bottom - bth.top;
		const int32 wsh = bsh.right - bsh.left, hsh = bsh.bottom - bsh.top;
		const int32 rbTH = (int32)accTH->GetRowBytes();
		const int32 rbSH = (int32)accSH->GetRowBytes();
		const int32 bppH = (int32)accTH->GetBitsPerPixel() / 8;
		const uint8* ptH = accTH->GetBaseAddr();
		const uint8* psH = accSH->GetBaseAddr();

		// 低解像度(保存・表示)の寸法は高解像度から割り戻す。buf は ARGB の自前バッファ(行パディング無し)。
		int32 wl = ::ToInt32(::Round(PMReal(wth) / kKESCMHiResMul));
		int32 hl = ::ToInt32(::Round(PMReal(hth) / kKESCMHiResMul));
		if (wl < 1) wl = 1;
		if (hl < 1) hl = 1;
		const int32 bppL = 4;				// 表示リングは常に自前 ARGB(=4)合成。比較ラスタの ch 数(RGB=4/CMYK=4)とは独立
		const int32 rbL = wl * bppL;		// 自前バッファ=行パディング無し

		if (ptH != nil && psH != nil &&
			wth == wsh && hth == hsh && rbTH == rbSH && rbTH > 0 &&
			bppH >= 4 && wl > 0 && hl > 0)
		{
			const size_t N = (size_t)wl * hl;
			uint8*  M     = new uint8[N];	// 低解像度マスク(保存): プーリング結果
			uint16* cntHi = new uint16[N];	// 低解像度セルごとの「高解像度の変化画素数」(プーリング用一時)
			if (M != nil && cntHi != nil)
			{
				memset(cntHi, 0, N * sizeof(uint16));

				// 【高解像度で比較 → 低解像度セルへ散らす(scatter)】
				// 高解像度の各画素を差分判定(生の各チャンネル最大差>しきい値)し、変化していたら
				// 対応する低解像度セルのカウンタを増やす。セル写像は寸法比(高/低が整数倍でなくてもよい)。
				// RGB: 先頭アルファを飛ばして3ch(offset=bppH-3)。CMYK: 先頭から4ch(offset=0, しきい値=kKESCMCmykThr)。
				const int  nch       = 4;
				const int32 colorOffH = 0;
				const int  thr        = kKESCMCmykThr;
				for (int32 y = 0; y < hth; ++y)
				{
					const uint8* rowT = ptH + (size_t)y * rbTH;
					const uint8* rowS = psH + (size_t)y * rbTH;
					int32 yl = (int32)((int64)y * hl / hth);
					if (yl >= hl) yl = hl - 1;
					uint16* cntRow = cntHi + (size_t)yl * wl;
					for (int32 x = 0; x < wth; ++x)
					{
						const uint8* px = rowT + (size_t)x * bppH + colorOffH;
						const uint8* sx = rowS + (size_t)x * bppH + colorOffH;
						int cm = 0;
						for (int c = 0; c < nch; ++c)
						{
							const int d = (px[c] > sx[c]) ? px[c] - sx[c] : sx[c] - px[c];
							if (d > cm) cm = d;
						}
						if (cm > thr)
						{
							int32 xl = (int32)((int64)x * wl / wth);
							if (xl >= wl) xl = wl - 1;
							if (cntRow[xl] < 0xFFFF) ++cntRow[xl];
						}
					}
				}

				// 【マックスプーリング】セル内の高解像度変化画素が min-count 以上なら低解像度マスク=1。
				// 1個でも(min=1)立てれば取りこぼしゼロ。min を上げると縁ノイズ耐性が増す。
				size_t diffCount = 0;
				for (size_t i = 0; i < N; ++i)
				{
					uint8 m = (cntHi[i] >= (uint16)kKESCMPoolMinCount) ? 1 : 0;
					M[i] = m;
					if (m) ++diffCount;
				}
				delete[] cntHi; cntHi = nil;

				if (diffCount == 0)
				{
					// 変化なし: エントリを作らない。
					delete[] M;
					status = kSuccess;	// 成功・ただし changed=false
				}
				else
				{
					// 背景(対象ページ)の「赤っぽい」画素マップを、高解像度 target をプーリングして作る
					// (案1: 低解像度 snapL を廃止。低解像度セル中心の高解像度画素1点を代表サンプルに)。
					// CMYK 経路は RGB が無いので、サンプル CMYK を近似 RGB に変換してから同じ R 優位判定を使う。
					const int32 colorOffT = 0;
					uint8* BG = new uint8[N];
					if (BG != nil)
					{
						for (int32 y = 0; y < hl; ++y)
						{
							int32 yh = (int32)(((int64)y * hth + hth / 2) / hl);
							if (yh >= hth) yh = hth - 1;
							const uint8* rowT = ptH + (size_t)yh * rbTH;
							for (int32 x = 0; x < wl; ++x)
							{
								int32 xh = (int32)(((int64)x * wth + wth / 2) / wl);
								if (xh >= wth) xh = wth - 1;
								const uint8* px = rowT + (size_t)xh * bppH + colorOffT;
								// CMYK(0..255) → 近似 RGB: ch=(255-ink)*(255-K)/255 の簡易式
								const int C = px[0], Mk = px[1], Yk = px[2], K = px[3];
								const int r = (255 - C)  * (255 - K) / 255;
								const int g = (255 - Mk) * (255 - K) / 255;
								const int b = (255 - Yk) * (255 - K) / 255;
								BG[(size_t)y * wl + x] = (r - g > kKESCMRedBgDom && r - b > kKESCMRedBgDom) ? 1 : 0;
							}
						}
					}

					// ★buf を指す自前 AGMImageRecord を組んで切り離す(buf は下で BuildRing が全画素を書くので
					//   ラスタ画素のコピーは不要)。SnapshotUtilsEx / accessor は保持しない(下で即破棄)。
					//   GetAGMImageRecord も呼ばない=破棄時クラッシュ(保持 accessor の delete)を根本回避。
					KESCMOverlayEntry* e = new KESCMOverlayEntry();
					e->w = wl;  e->h = hl;  e->rowBytes = rbL;  e->bpp = bppL;
					e->bgRed = BG;  e->lastRadius = kKESCMBaseRadius;
					// この頁の変更(枠)の数。生 M をそのまま数えると、文字変更で各グリフ片が
					// 別成分になり数が膨大になる。固定半径で膨張して近接変化を併合してから数え、
					// 見た目の赤い塊(リング)の数に近づける。
					{
						uint8* Dn = new uint8[N];	// 併合用の一時マスク(1byte/px)
						if (Dn != nil)
						{
							KESCMDilateMask(M, wl, hl, kKESCMCountMergeRadius, Dn);
							e->changeCount = KESCMCountComponents(Dn, wl, hl);
							delete[] Dn;
						}
						else
						{
							e->changeCount = KESCMCountComponents(M, wl, hl);	// 確保失敗時は生 M
						}
					}
					// ★案A: mask M から距離変換 dist を1回だけ作って保持(以後の BuildRing はこれ1つで描ける)。
					//   dist 生成後、mask M はもう不要なので解放(常駐メモリは dist が mask を置換=純増ゼロ)。
					e->dist = new uint8[N];
					if (e->dist != nil)
						KESCMDistTransform(M, wl, hl, e->dist);
					delete[] M;

					// 初回リング(基準半径)を buf へ直接描く(dist 確保失敗時のみ透明クリアで安全に)。
					// buf 確保失敗(nil)時はここでは触らない。描画側(HandleDrawEvent)が e->buf==nil で skip する。
					e->buf = new uint8[(size_t)rbL * hl];
					if (e->buf != nil)
					{
						if (e->dist != nil)
							BuildRing(e->buf, rbL, bppL, wl, hl, e->dist, BG, kKESCMBaseRadius);
						else
							memset(e->buf, 0, (size_t)rbL * hl);
					}
					e->rec.bounds.xMin = 0;             e->rec.bounds.yMin = 0;
					e->rec.bounds.xMax = (int16)wl;     e->rec.bounds.yMax = (int16)hl;
					e->rec.baseAddr     = e->buf;
					e->rec.byteWidth    = rbL;
					// ARGB(alpha 先頭)。HasAlpha フラグを立てないと透明画素が不透明白で描かれる。
					// 既定が ARGB 順なので SwapAlpha は不要(RGBA なら | kColorSpaceSwapAlpha)。
					e->rec.colorSpace   = (int16)(kRGBColorSpace | kColorSpaceHasAlpha);
					e->rec.bitsPerPixel = (int16)(bppL * 8);
					e->rec.decodeArray  = nil;
					e->rec.colorTab.numColors = 0;  e->rec.colorTab.theColors = nil;

					// 既存エントリがあれば置換。
					UID key = targetRef.GetUID();
					std::map<UID, KESCMOverlayEntry*>::iterator old = sEntries.find(key);
					if (old != sEntries.end()) { delete old->second; sEntries.erase(old); }
					sEntries[key] = e;

					// dist / bgRed / buf は entry が所有(mask M は dist 生成後に解放済み)。スナップショットは下の後始末で即破棄。
					changed = kTrue;
					status = kSuccess;
				}
			}
			else
			{
				if (M)     delete[] M;
				if (cntHi) delete[] cntHi;
			}
		}
	}

	// 後始末: 2つのスナップショット/アクセサを破棄(案1でラスタ化は2回=低解像度 snapL は廃止)。
	if (accSH)  delete accSH;
	if (snapSH) delete snapSH;
	if (accTH)  delete accTH;
	if (snapTH) delete snapTH;
	return status;
}


ErrorCode KESCMDrawEventHandler::MakeOrigImage(const UIDRef& targetRef, const UIDRef& sourceRef, const PMReal& resolution)
{
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// source(旧)を resolution(dpi)で不透明ラスタ化。addTransparencyAlpha=kFalse=ページを不透明に描く(べた載せ用)。
	// オフスクリーンは1枚だけ。画素を自前 buf へコピーしたら即破棄(下)＝同時に複数生存しない=安全。
	SnapshotUtilsEx* snap = new SnapshotUtilsEx(sourceRef, 1.0, 1.0, resolution, resolution, 0.0, SnapshotUtilsEx::kCsRGB, kFalse);
	sRasterizing = kTrue;	// この Draw 中に再入する HandleDrawEvent はマークを描かない(自己参照防止)
	ErrorCode drew = snap->Draw(IShape::kPreviewMode);
	sRasterizing = kFalse;
	AGMImageAccessor* acc = (drew == kSuccess) ? snap->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (acc != nil)
	{
		Int32Rect b = acc->GetBounds();
		const int32 w = b.right - b.left, h = b.bottom - b.top;
		const int32 rb = (int32)acc->GetRowBytes();
		const int32 bpp = (int32)acc->GetBitsPerPixel() / 8;
		const uint8* p = acc->GetBaseAddr();
		// AGMImageRecord.bounds は int16。300dpi で超大型ページ(幅/高さ>32767px≒109inch)だと破綻するので弾く。
		if (p != nil && w > 0 && h > 0 && rb > 0 && bpp >= 3 && b.right <= 32767 && b.bottom <= 32767)
		{
			KESCMOrigImage* o = new KESCMOrigImage();
			o->w = w;  o->h = h;  o->rowBytes = rb;  o->bpp = bpp;
			o->buf = new uint8[(size_t)rb * h];
			memcpy(o->buf, p, (size_t)rb * h);
			// 不透明保証: ARGB(alpha 先頭)なら alpha を 255 に揃える(べた載せ=下が透けない)。
			// ★案D: まず格子状(約8×8点)にサンプリングし、全サンプルが既に 255(不透明)なら O(W*H) の
			//   全画素ループを丸ごと省く。ラスタが既に不透明(addTransparencyAlpha=kFalse)なら書き込みを回避。
			//   サンプルに非255が1つでもあれば従来どおり全画素を 255 に揃える(自己補正=どちらでも正しい)。
			if (bpp >= 4)
			{
				bool16 alreadyOpaque = kTrue;
				const int32 sy = (h > 8) ? h / 8 : 1;
				const int32 sx = (w > 8) ? w / 8 : 1;
				for (int32 y = 0; y < h && alreadyOpaque; y += sy)
				{
					const uint8* row = o->buf + (size_t)y * rb;
					for (int32 x = 0; x < w; x += sx)
						if (row[(size_t)x * bpp] != 255) { alreadyOpaque = kFalse; break; }
				}
				if (!alreadyOpaque)
				{
					for (int32 y = 0; y < h; ++y)
					{
						uint8* row = o->buf + (size_t)y * rb;
						for (int32 x = 0; x < w; ++x)
							row[(size_t)x * bpp] = 255;
					}
				}
			}
			o->rec.bounds.xMin = (int16)b.left;   o->rec.bounds.yMin = (int16)b.top;
			o->rec.bounds.xMax = (int16)b.right;  o->rec.bounds.yMax = (int16)b.bottom;
			o->rec.baseAddr     = o->buf;
			o->rec.byteWidth    = rb;
			o->rec.colorSpace   = (int16)((bpp >= 4) ? (kRGBColorSpace | kColorSpaceHasAlpha) : kRGBColorSpace);
			o->rec.bitsPerPixel = (int16)acc->GetBitsPerPixel();
			o->rec.decodeArray  = nil;
			o->rec.colorTab.numColors = 0;  o->rec.colorTab.theColors = nil;

			// 既存があれば置換。
			UID key = targetRef.GetUID();
			std::map<UID, KESCMOrigImage*>::iterator old = sOrigImages.find(key);
			if (old != sOrigImages.end()) { delete old->second; sOrigImages.erase(old); }
			sOrigImages[key] = o;
			status = kSuccess;
		}
	}

	if (acc)  delete acc;
	if (snap) delete snap;
	return status;
}


void KESCMDrawEventHandler::Register(IDrwEvtDispatcher* d)
{
	// スプレッド単位で配られる描画イベント。ポートは spread 座標。枠/変更数はこちらで描く。
	// トーストもこちらで描く=スプレッド/ペーストボード帯の「前面」分(帯外はクリップされる)。
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
	// ウィンドウ単位(全スプレッド描画後に1回)。ポートは CTM=pasteboard。スプレッド/ペーストボードの
	// 背面に来るため、トーストの「帯外=カンバス背景」分だけをこちらで描く(2系統併用で全域カバー)。
	d->RegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this, kDEHLowestPriority);
}

void KESCMDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
	d->UnRegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this);
}


// ビューから IPanorama を取る。ページアイテム系の子ウィジェットは panorama を持たないため、
// CTracker::QueryPanorama と同じく自身→親(LayoutWidget)の順で辿る。呼び出し側で Release すること。
static IPanorama* KESCMQueryPanorama(IControlView* view)
{
	if (view == nil)
		return nil;
	IPanorama* pano = (IPanorama*)view->QueryInterface(IID_IPANORAMA);
	if (pano != nil)
		return pano;
	InterfacePtr<IWidgetParent> parent(view, IID_IWIDGETPARENT);
	if (parent == nil)
		return nil;
	return (IPanorama*)parent->QueryParentFor(IID_IPANORAMA);
}

// 変更数テキストのサイズ倍率 M(uiZoom)。2点 (zoomLo,mulLo)(zoomHi,mulHi) を通す反比例 M = a - b/zoom。
// b = (mulHi-mulLo)/(1/zoomLo - 1/zoomHi)、a = mulHi + b/zoomHi。範囲外は [mulLo, mulHi] にクランプ。
// uiZoom<=0(ビュー/パノラマ不明)なら 1.0(=現状サイズ)を返す。
static PMReal KESCMCountSizeMul(PMReal uiZoom)
{
	if (uiZoom <= 0.0)
		return PMReal(1.0);
	const PMReal invLo = PMReal(1.0) / kKESCMCountZoomLo;
	const PMReal invHi = PMReal(1.0) / kKESCMCountZoomHi;
	const PMReal b = (kKESCMCountMulHi - kKESCMCountMulLo) / (invLo - invHi);
	const PMReal a = kKESCMCountMulHi + b * invHi;
	PMReal m = a - b / uiZoom;
	if (m < kKESCMCountMulLo) m = kKESCMCountMulLo;	// 5%未満でも 0.8倍で下げ止め
	if (m > kKESCMCountMulHi) m = kKESCMCountMulHi;	// 100%超でも 3倍で頭打ち(青天井にしない)
	return m;
}

// トースト用の文字列幅の概算(em 単位の合計)。SDK に「選択フォントで任意文字列を実測する」軽量 API が
// 無いため、文字種別の代表値を合計して近似する(プロポーショナルフォントで一律 0.5em より実幅に近い)。
// 大文字・記号は広め、小文字は中、スペースは狭め、非 ASCII(全角=CJK 等)は約 1em とみなす。
static PMReal KESCMEstimateTextEm(const UTF16TextChar* buf, int32 n)
{
	PMReal sum = 0.0;
	for (int32 i = 0; i < n; ++i)
	{
		const UTF16TextChar c = buf[i];
		PMReal w;
		if (c == 0x20)                      w = PMReal(0.30);	// 半角スペース
		else if (c >= 0x41 && c <= 0x5A)    w = PMReal(0.80);	// 大文字 A-Z(広め。右端の詰まり対策で少し大きめ)
		else if (c >= 0x61 && c <= 0x7A)    w = PMReal(0.50);	// 小文字 a-z
		else if (c >= 0x30 && c <= 0x39)    w = PMReal(0.55);	// 数字 0-9
		else if (c < 0x80)                  w = PMReal(0.50);	// その他 ASCII(記号等)
		else                                w = PMReal(1.00);	// 非 ASCII(全角=CJK 等)
		sum = sum + w;
	}
	return sum;
}

//========================================================================================
// pasteboard 座標 → このスプレッドの spread 座標 への変換オフセット(= pasteboard - spread)。
//   pasteboard 座標はドキュメント全体で1つ。スプレッドは pasteboard 上で(主に縦に)積まれ、各々が
//   オフセットを持つ(spread[0] だけ偶然 0)。同一の inner 原点(0,0)を InnerToSpreadMatrix と
//   InnerToPasteboardMatrix の両方で写し、その差を取ればこのスプレッドのオフセットになる。
//   pasteboard 中心からこれを引けば、そのスプレッドの spread 座標における中心が得られる。
//========================================================================================
static PMPoint KESCMSpreadOffsetFromPasteboard(IDataBase* db, ISpread* spread)
{
	PMPoint off(0.0, 0.0);
	if (db == nil || spread == nil || spread->GetNumPages() < 1)
		return off;
	InterfacePtr<IGeometry> pg(db, spread->GetNthPageUID(0), UseDefaultIID());
	if (pg == nil)
		return off;
	PMMatrix mS = ::InnerToSpreadMatrix(pg);
	PMMatrix mP = ::InnerToPasteboardMatrix(pg);
	PMPoint ps(0.0, 0.0), pp(0.0, 0.0);
	mS.Transform(&ps);
	mP.Transform(&pp);
	return PMPoint(pp.X() - ps.X(), pp.Y() - ps.Y());
}

//========================================================================================
// 一時トースト描画: 指定中心(centerPort)に、半透明の暗いボックス＋白文字でメッセージを描く。
//   中心は「呼び出すポートの座標系」で渡す:
//     - kEndSpreadMessage(per-spread)        → spread 座標(スプレッド/ペーストボード帯の前面)
//     - kAfterLastSpreadDrawMessage(ウィンドウ)→ pasteboard 座標(帯外=カンバス背景)
//   この2系統を併用すると、各画素はどちらか一方が担当=二重描き無しで全域(スプレッド+ペースト
//   ボード+カンバス)をカバーできる。可視/db/中心の有効性チェックは呼び出し側で済ませる。
//   サイズはズーム不変(画面px / sxr)。
//========================================================================================
static void KESCMDrawToast(IGraphicsPort* gPort, PMReal sxr, const PMPoint& centerPort)
{
	if (gPort == nil || sxr <= 0)
		return;

	const PMReal sX = centerPort.X();
	const PMReal sY = centerPort.Y();

	PMString msg = KESCMDrawEventHandler::sToastMsg;
	msg.SetTranslatable(kFalse);
	const int32 nch = msg.NumUTF16TextChars();
	if (nch <= 0)
		return;
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IPMFont> theFont(fontMgr ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil);
	if (theFont == nil)
		return;

	const PMReal fpt   = kKESCMToastTextPx / sxr;			// 文字pt(ズーム不変)
	const PMReal pad   = kKESCMToastPadPx  / sxr;
	const UTF16TextChar* mbuf = msg.GrabUTF16Buffer(nil);	// 文字バッファ(幅見積もりと描画で共用)

	// 改行(LF=0x0A)で行に分割(最大 kMaxLines 行)。改行が無ければ 1 行=従来どおり。各行は中央寄せで縦に積む。
	const int32 kMaxLines = 8;
	int32 lineStart[kMaxLines], lineLen[kMaxLines], nLines = 0;
	int32 st = 0;
	for (int32 i = 0; i <= nch; ++i)
	{
		if (i == nch || mbuf[i] == 0x0A)
		{
			if (nLines < kMaxLines) { lineStart[nLines] = st; lineLen[nLines] = i - st; ++nLines; }
			st = i + 1;
		}
	}
	if (nLines <= 0)
		return;

	// 各行を任意の TAB(0x09)で「ラベル列(seg0)」と「値列(seg1)」に分ける。値列を全行同じ X(固定列)から
	// 描くと、ラベル(Target/Source)の実幅が違っても値(C000 …)の桁がぴったり揃う。TAB が無ければその行は
	// seg0 のみ＝中央寄せ(単一行トースト等は従来どおり)。
	int32 seg0Len[kMaxLines], seg1Off[kMaxLines], seg1Len[kMaxLines];
	bool16 anyTab = kFalse;
	for (int32 L = 0; L < nLines; ++L)
	{
		int32 tabRel = -1;
		for (int32 k = 0; k < lineLen[L]; ++k)
			if (mbuf[lineStart[L] + k] == 0x09) { tabRel = k; break; }
		if (tabRel >= 0)
		{
			seg0Len[L] = tabRel;
			seg1Off[L] = tabRel + 1;
			seg1Len[L] = lineLen[L] - (tabRel + 1);
			anyTab = kTrue;
		}
		else { seg0Len[L] = lineLen[L]; seg1Off[L] = lineLen[L]; seg1Len[L] = 0; }
	}

	// 列幅(em)とコンテンツ幅(em)を見積もる。TAB あり=ラベル列幅+間隔+値列幅、無し=行全体の最大幅。
	PMReal col0Em = 0.0, col1Em = 0.0, maxLineEm = 0.0;
	for (int32 L = 0; L < nLines; ++L)
	{
		const PMReal e0 = KESCMEstimateTextEm(&mbuf[lineStart[L]], seg0Len[L]);
		const PMReal e1 = (seg1Len[L] > 0) ? KESCMEstimateTextEm(&mbuf[lineStart[L] + seg1Off[L]], seg1Len[L]) : PMReal(0.0);
		if (e0 > col0Em) col0Em = e0;
		if (e1 > col1Em) col1Em = e1;
		const PMReal whole = KESCMEstimateTextEm(&mbuf[lineStart[L]], lineLen[L]);
		if (whole > maxLineEm) maxLineEm = whole;
	}
	const PMReal gapEm     = PMReal(0.6);					// ラベル列と値列の間隔(em)
	const PMReal contentEm = anyTab ? (col0Em + gapEm + col1Em) : maxLineEm;

	const PMReal textW   = fpt * contentEm;
	const PMReal lineGap = fpt * PMReal(1.20);				// 行送り(2 行目以降の縦間隔)
	const PMReal boxW    = textW + pad * PMReal(2.0);
	const PMReal boxH    = fpt + PMReal(nLines - 1) * lineGap + pad * PMReal(2.0);
	const PMReal x0      = sX - boxW / PMReal(2.0);
	const PMReal y0      = sY - boxH / PMReal(2.0);

	AutoGSave ag(gPort);
	// 背景: ほぼ不透明の暗いボックス(下の青線/ガイドが透けてまだらにならないよう不透明寄りに)。
	gPort->setopacity(PMReal(0.92), kFalse);
	gPort->setrgbcolor(PMReal(0.10), PMReal(0.10), PMReal(0.10));
	gPort->rectfill(x0, y0, boxW, boxH);
	// 細い白枠。
	gPort->setopacity(PMReal(1.0), kFalse);
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->setlinewidth(PMReal(1.0) / sxr);
	gPort->rectpath(x0, y0, boxW, boxH);
	gPort->stroke();
	// 白文字。show は baseline 左端基準。1 行目の縦中央 ≒ y0 + pad + fpt*0.78。
	gPort->selectfont(theFont, fpt);
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	const PMReal contentLeft = x0 + pad;					// テキストブロックの左端(ボックス内で中央寄せ)
	const PMReal col1X       = contentLeft + fpt * (col0Em + gapEm);	// 値列(seg1)の共通左端=固定列
	for (int32 L = 0; L < nLines; ++L)
	{
		const PMReal ty = y0 + pad + fpt * PMReal(0.78) + PMReal(L) * lineGap;
		if (anyTab)
		{
			// 列レイアウト: ラベルは左端、値は固定列(col1X)から。両行で値の桁が揃う。
			if (seg0Len[L] > 0)
				gPort->show(contentLeft, ty, seg0Len[L], &mbuf[lineStart[L]], (IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
			if (seg1Len[L] > 0)
				gPort->show(col1X, ty, seg1Len[L], &mbuf[lineStart[L] + seg1Off[L]], (IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
		}
		else
		{
			// 中央寄せ(従来=単一行トースト等)。
			if (lineLen[L] <= 0)
				continue;
			const PMReal lw = fpt * KESCMEstimateTextEm(&mbuf[lineStart[L]], lineLen[L]);
			const PMReal tx = sX - lw / PMReal(2.0);
			gPort->show(tx, ty, lineLen[L], &mbuf[lineStart[L]], (IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
		}
	}
}


//========================================================================================
// 印刷/PDF 用のリング描画。画面は image() blit でよいが(画素 alpha を honor する)、印刷のフラットナ
// 経路は blit 画像の部分 alpha を honor せず枠が不透明になる。そこで transparencyeffect サンプルと
// 同じ作法=リング形状を「グレーのアルファサーバ」にして純色のベクター fill を setopacity で半透明に
// 描く(透明合成エンジンが honor する)。赤と青(背景適応)を保つため、赤画素・青画素それぞれのグレー
// マスクで2回 fill する。呼び出し側で translate/scale 済み(user 空間 = 画像px)であること。
//   e->buf は ARGB(先頭=alpha, 続いて R,G,B)。
//========================================================================================
static void KESCMDrawRingForPrint(IGraphicsPort* gPort, KESCMOverlayEntry* e)
{
	if (gPort == nil || e == nil || e->buf == nil || e->w <= 0 || e->h <= 0 || e->bpp < 4)
		return;
	// 透明合成ユーティリティ(アルファサーバ生成/解放に使う)。実行中アプリでは常在するが、
	// transparencyeffect サンプル流に、取得できなければ何もしない(クラッシュ回避)。以後この1個を使い回す。
	Utils<IXPUtils> xpUtils;
	if (!xpUtils)
		return;
	const int32 w = e->w, h = e->h, rb = e->rowBytes, bpp = e->bpp;
	const size_t N = (size_t)w * h;

	// e->buf(ARGB)から、赤リング画素=255 / 青リング画素=255 の2枚のグレーマスクを作る。
	uint8* maskR = new uint8[N];
	uint8* maskB = new uint8[N];
	if (maskR == nil || maskB == nil) { if (maskR) delete[] maskR; if (maskB) delete[] maskB; return; }
	for (int32 y = 0; y < h; ++y)
	{
		const uint8* row = e->buf + (size_t)y * rb;
		for (int32 x = 0; x < w; ++x)
		{
			const uint8* px  = row + (size_t)x * bpp;	// [alpha, R, G, B]
			const size_t idx = (size_t)y * w + x;
			if (px[0] != 0)								// リング画素(alpha!=0)
			{
				const bool16 blue = (px[3] > px[1]);	// B>R = 青(背景適応で青に切り替わった画素)
				maskR[idx] = blue ? 0 : 255;
				maskB[idx] = blue ? 255 : 0;
			}
			else { maskR[idx] = 0; maskB[idx] = 0; }
		}
	}

	// リングの不透明度。通常=画面と同じ(kKESCMRingAlpha/255=1.0 不透明) / faint=約30%(kKESCMFaintOpacity)。
	const PMReal op = KESCMDrawEventHandler::sPrintFaint ? kKESCMFaintOpacity : (kKESCMRingAlpha / PMReal(255.0));
	struct PassDef { uint8* buf; uint8 r, g, b; };
	PassDef passes[2] = { { maskR, 255, 0, 0 }, { maskB, 0, 0, 255 } };	// 赤 / 青

	for (int p = 0; p < 2; ++p)
	{
		// マスクを指すグレー(8bpp, alpha無し)の AGMImageRecord。アルファサーバは gray colorspace 必須。
		AGMImageRecord mrec;
		mrec.bounds.xMin = 0;            mrec.bounds.yMin = 0;
		mrec.bounds.xMax = (int16)w;     mrec.bounds.yMax = (int16)h;
		mrec.baseAddr     = passes[p].buf;
		mrec.byteWidth    = w;								// 1byte/px, 行パディング無し
		mrec.colorSpace   = (int16)kGrayColorSpace;
		mrec.bitsPerPixel = 8;
		mrec.decodeArray  = nil;
		mrec.colorTab.numColors = 0;     mrec.colorTab.theColors = nil;

		PMMatrix idm;										// 恒等。user 空間=画像px なので画素(x,y)→user(x,y)
		AGMPaint* alphaPaint = xpUtils->CreateImagePaintServer(&mrec, &idm, 0, nil);
		if (alphaPaint != nil)
		{
			AutoGSave ag(gPort);
			gPort->SetAlphaServer(alphaPaint, kTrue, PMMatrix());	// 形状=リング画素(per-pixel)
			gPort->setopacity(op, kFalse);							// 半透明(透明合成が honor)
			gPort->setrgbcolor(passes[p].r / PMReal(255.0), passes[p].g / PMReal(255.0), passes[p].b / PMReal(255.0));
			gPort->newpath();
			gPort->rectpath(PMReal(0.0), PMReal(0.0), PMReal(w), PMReal(h));	// user 空間=画像px(呼び出し側で translate/scale 済)
			gPort->fill();
			xpUtils->ReleasePaintServer(alphaPaint);
		}
	}

	delete[] maskR;
	delete[] maskB;
}


bool16 KESCMDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;
	// 自前のラスタ化(MakeEntry の比較スナップショット / MakeOrigImage の旧版スナップショット)中の再入は
	// 描かない(自己参照=マークがスナップショットに写り込む feedback を防ぐ)。以前は kPreviewMode ビットで
	// 弾いていたが、それは PDF 書き出しの kPDFExportMode と同一ビット(4096)で export を巻き込んでいたため、
	// 明示的な再入フラグ sRasterizing に置き換えた。
	if (sRasterizing)
		return kFalse;
	// 印刷文脈か(kPrinting=512)。印刷時はマークの ON/OFF を sPrintMarks で決める。通常の画面描画では立たない。
	// ※PDF 書き出し(File>Export)はこのスプレッド描画イベントを発火しないため対象外(print-to-PDF を使う)。
	// 自己参照(自前スナップショット)は上の sRasterizing で防ぐので、ここで kPreviewMode は見ない。
	const bool16 printing = (ded->flags & IShape::kPrinting) != 0;
	// オーバープリントプレビュー(OPP)中か。OPP は「印刷の見え方」を画面でシミュレートするモードなので、
	// 枠は基本非印刷=OPP でも「枠の印刷」OFF なら隠す(以前 kPreviewMode で弾いていた挙動の復帰)。ただし
	// kPreviewMode(4096) は PDF 書き出しの kPDFExportMode と同一ビットで衝突するため使わず、OPP 専用の
	// ビューポート属性 kSepPrvOPPEnabledVPAttr で正確に判定する(PDF export とは衝突しない)。
	bool16 overprintPreview = kFalse;
	IViewPortAttributes* vpa = ded->gd->GetViewPortAttributes();	// ded->gd は冒頭で非nil確認済み
	if (vpa != nil)
		overprintPreview = (vpa->GetAttr(kSepPrvOPPEnabledVPAttr, 0) != 0);
	// 印刷 or オーバープリントプレビューで「枠の印刷」が OFF のときは描かない(枠は基本非印刷)。
	if ((printing || overprintPreview) && !sPrintMarks)
		return kFalse;
	// マークも 旧版べた載せ も トースト も無ければ何もしない。
	if (sEntries.empty() && !(sShowOriginal && !sOrigImages.empty()) && !sToastVisible)
		return kFalse;

	GraphicsData* gd = ded->gd;
	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;

	// changedBy = 今描いているスプレッド。
	InterfacePtr<ISpread> spread(ded->changedBy, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	IDataBase* db = ::GetDataBase(ded->changedBy);
	if (db == nil)
		return kFalse;

	// 画面スケール(ズーム)を一度だけ取得。画面描画時のみ非nil。
	PMReal sxr = 0.0;
	PMReal countMul = 1.0;	// 変更数テキストのサイズ倍率(拡大率連動)。ビュー/パノラマ不明時は 1.0(=現状サイズ)
	IControlView* zview = gd->GetView();
	InterfacePtr<IPanorama> pano;	// 可視上端(縦位置)と UIズーム(文字倍率)の両方に使う。画面描画時のみ非nil
	PMPoint centerPb(0.0, 0.0);		// 可視領域の中心(pasteboard 座標)。トーストの基準位置に使う
	bool16  hasCenter = kFalse;		// 上記が有効か(panorama を辿れた=画面描画時のみ)
	if (zview != nil)
	{
		PMMatrix toWin = zview->GetContentToWindowMatrix();	// content→window(画面px), 現ズーム
		sxr = toWin.GetXScale(); if (sxr < 0) sxr = -sxr;
		pano.reset(KESCMQueryPanorama(zview));	// 自身→親(LayoutWidget)で IPanorama を辿る(attach=addref済みを所有)
		if (pano != nil)
		{
			// UIズーム値=モニタ倍率を含まない「ユーザーに見える拡大率」(5%→0.05, 100%→1.0)。
			// sxr(=ズーム×デバイス倍率)ではなくこちらでサイズ倍率を決める(ユーザー指定の 5%/100% に合わせる)。
			const PMReal uiZoom = pano->GetXScaleFactor(kFalse);
			countMul = KESCMCountSizeMul(uiZoom);
			centerPb = pano->GetContentLocationAtFrameCenter();	// 可視中心(content=pasteboard 座標)
			hasCenter = kTrue;
		}
	}

	// ★印刷/PDF 時は「100% 表示の見た目」に固定する(ズーム連動を切る)。印刷ポートには view が無く
	// sxr=0 / pano=nil になるので、実効 sxr=1.0(=100%・deviceScale 1 相当)と 100% の文字倍率を与える。
	// これでリング太さ・数値サイズ・数値位置の各式が、画面 100% 表示時とちょうど同じ値になる(下流の
	// ズーム適応式・フォールバック式をそのまま使い回せる)。画面描画(printing=false)は従来どおりズーム連動。
	if (printing)
	{
		sxr = 1.0;
		countMul = KESCMCountSizeMul(1.0);
	}

	// ★ウィンドウ単位イベント(全スプレッド描画後, CTM=pasteboard)= トーストの「帯外(カンバス背景)」担当。
	// このポートはスプレッド/ペーストボードの背面に来るため、何も被さらないカンバス部分にだけ見える。
	// スプレッド/ペーストボード帯の前面分は per-spread(kEndSpreadMessage)側で描く(下)。
	// 枠/旧版/変更数もスプレッド単位側で描く。トーストは一時メッセージなので印刷/PDF には出さない。
	if (eventID == ClassID(kAfterLastSpreadDrawMessage))
	{
		if (!printing && sToastVisible && db == sToastDB && hasCenter && sxr > 0)
			KESCMDrawToast(gPort, sxr, centerPb);	// pasteboard 座標の中心へ直接
		return kFalse;
	}

	// 今描いている「このスプレッド」を覗いている(旧版べた載せ中)か。覗きで旧版が乗るのはマウス下の1スプレッド
	// だけ(そのページが sOrigImages にある)。覗き中のスプレッドだけ旧版をきれいに見せたいので、マーク
	// (枠/変更数)を描かない。それ以外のスプレッドは通常どおりマークを描く。
	bool16 peekingThisSpread = kFalse;
	if (!printing && sShowOriginal && !sOrigImages.empty() && sOrigDB != nil && db == sOrigDB)
	{
		const int32 npChk = spread->GetNumPages();
		for (int32 i = 0; i < npChk; ++i)
			if (sOrigImages.find(spread->GetNthPageUID(i)) != sOrigImages.end())
			{ peekingThisSpread = kTrue; break; }
	}

	// (A2) 旧版べた載せ — マーク(sEntries)とは独立。覗き中のスプレッドの各ページに旧版画像を不透明で
	// ページ矩形いっぱいに blit する。
	if (peekingThisSpread)
	{
		const int32 npo = spread->GetNumPages();
		for (int32 i = 0; i < npo; ++i)
		{
			const UID pageUID = spread->GetNthPageUID(i);
			std::map<UID, KESCMOrigImage*>::iterator it = sOrigImages.find(pageUID);
			if (it == sOrigImages.end())
				continue;
			KESCMOrigImage* o = it->second;
			if (o == nil || o->buf == nil || o->w <= 0 || o->h <= 0)
				continue;
			InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
			if (pageGeo == nil)
				continue;
			PMRect pr = pageGeo->GetPathBoundingBox();		// ページ inner
			PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
			m.Transform(&pr);								// → spread(=描画ポート)座標
			AutoGSave ag(gPort);
			gPort->setopacity(sPeekOpacity, kFalse);		// Shift peek=1.0(不透明) / Ctrl peek=0.5(半透明)
			gPort->translate(pr.Left(), pr.Top());
			gPort->scale(pr.Width() / o->w, pr.Height() / o->h);	// 旧版画像をページ矩形にフィット
			gPort->image(&o->rec, PMMatrix(), 0);			// 旧版を sPeekOpacity で重ねる
		}
	}

	// (A3) トースト — スプレッド/ペーストボード帯の「前面」に出すため per-spread(spread座標)ポートでも描く。
	// このポートは帯にクリップされる(帯外は欠ける)ので、帯外=カンバスは kAfterLastSpreadDrawMessage 側が担う。
	// window 中心(centerPb=pasteboard座標)をこのスプレッドのオフセット分だけ引いて spread 座標の中心へ変換する。
	// per-spread は可視スプレッドごとに発火するので、箱が複数スプレッドにまたがっても各帯で前面に出る。
	if (!printing && sToastVisible && db == sToastDB && hasCenter && sxr > 0)
	{
		PMPoint off = KESCMSpreadOffsetFromPasteboard(db, spread);
		PMPoint cS(centerPb.X() - off.X(), centerPb.Y() - off.Y());
		KESCMDrawToast(gPort, sxr, cS);
	}

	// (B) 変更オーバーレイ(リング＋変更数) — マーク済みドキュメントが現スプレッドの db と一致する時だけ。
	// master 表示トグル(sMarksVisible)が OFF の間、またはこのスプレッドを覗き中(旧版べた載せ中)は描かない
	// (データは保持=再表示で即復帰)。覗いていない他のスプレッドのマークは通常どおり残る。
	// ★印刷マーク(sPrintMarks)が ON の間は、ミドル押下に関係なく常に描く(画面=WYSIWYG / 印刷・PDF にも出る)。
	if (peekingThisSpread || !(sPrintMarks || sMarksVisible) || sEntries.empty() || sDB == nil || db != sDB)
		return kFalse;

	// 画面マークの実効不透明度。sMarkScreenOpacity は常に実効値を保持する(下の各ソースが設定):
	//   ・既定/印刷通常 = 1.0(不透明)  ・印刷30%選択中(常時表示) = 0.3
	//   ・ミドルのみ押下中 = 0.3        ・Shift+Alt 押下中 = 1.0(不透明=印刷30%中でも不透明で確認できる)
	// 離すと印刷設定に応じた基準値(KESCMBaseScreenOpacity)へ戻る。printing 経路はここを使わない。
	const PMReal screenMarkOp = sMarkScreenOpacity;

	// 変更数テキスト用の既定フォントは、このスプレッドの全ページで共通。ページループ内で毎回
	// 取得すると変更ページ数ぶん無駄に問い合わせるので、ループ外で1回だけ取得して使い回す。
	InterfacePtr<IFontMgr> countFontMgr(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IPMFont> countFont(countFontMgr ? countFontMgr->QueryFont(countFontMgr->GetDefaultFontName()) : nil);

	// このスプレッドの各ページについて、エントリがあれば描く。
	const int32 np = spread->GetNumPages();
	for (int32 i = 0; i < np; ++i)
	{
		const UID pageUID = spread->GetNthPageUID(i);
		std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.find(pageUID);
		if (it == sEntries.end())
			continue;
		KESCMOverlayEntry* e = it->second;
		if (e == nil || e->buf == nil)
			continue;

		const int32 iw = e->w, ih = e->h;
		InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
		if (iw <= 0 || ih <= 0 || pageGeo == nil)
			continue;

		// 【座標の肝】kEndSpreadMessage の描画ポートは spread 座標。ページ inner bbox を
		// InnerToSpreadMatrix で spread 座標へ変換してフィットさせる。
		PMRect pr = pageGeo->GetPathBoundingBox();			// ページ inner
		PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
		m.Transform(&pr);									// → spread(=描画ポート)座標

		// 【リング太さのズーム適応】このページの実寸と現ズームから「画面 kKESCMRingTargetPx 相当」の
		// 膨張半径(画像px)を逆算。前回と違えば描き直し。拡大時は下限(2)に張り付くので再計算が止まる。
		if (e->dist != nil && sxr > 0)
		{
			PMReal denom = (pr.Width() / PMReal(iw)) * sxr;		// 画面px / 画像px
			if (denom > PMReal(0.0001))
			{
				int32 R = ::ToInt32(::Round(kKESCMRingTargetPx / denom));
				if (R < 2) R = 2;									// 最小2px(量子化後は最小4px)
				if (R > 200) R = 200;								// 過大膨張の上限
				// ★案B: 量子化を 2px→4px 刻みに。ズーム中に R が変わる回数(=BuildRing 再計算)がほぼ半減。
				// 代償=太さの段階がやや粗い。最小は 4、200 は 200 に丸まる。
				R = ((R + 2) / 4) * 4;								// 4px 量子化
				if (R != e->lastRadius)
				{
					BuildRing(e->buf, e->rowBytes, e->bpp, e->w, e->h, e->dist, e->bgRed, R);
					e->lastRadius = R;
				}
			}
		}

		// 【描画順】まず枠の画像(リング)を blit し、その上に × と枠の数を重ねる。
		// translate/scale はこの gsave 内だけ。閉じれば spread 座標に戻るので後続の ×/数 はそのまま描ける。
		{
			AutoGSave ag(gPort);
			gPort->translate(pr.Left(), pr.Top());				// ページ左上へ
			gPort->scale(pr.Width() / iw, pr.Height() / ih);	// 画像px → ページ矩形にフィット
			// ★印刷/PDF 時は image() blit だと枠が不透明になる(フラットナが画像の部分 alpha を honor しない)。
			// アルファサーバ＋純色ベクター fill＋setopacity で半透明に描く(透明合成エンジンが honor)。
			// 画面描画(printing=false)は従来の ARGB blit のまま(画素 alpha を honor=検証済みの見た目を維持)。
			if (printing)
				KESCMDrawRingForPrint(gPort, e);
			else
			{
				// 画面 blit は image() の画素 alpha に加えてポート opacity も honor する。薄表示(Shift+Alt+ミドル)
				// 中や 30%設定中は screenMarkOp(≒0.3)、通常は 1.0。AutoGSave 内なので閉じれば元へ戻る。
				gPort->setopacity(screenMarkOp, kFalse);
				gPort->image(&e->rec, PMMatrix(), 0);			// 自前レコード(buf を指す)を blit
			}
		}

		// この頁の変更数「N chg」を常時表示(常時・トグル無し)。リング画像の上に重ねる。
		// 文字サイズは拡大率連動(画面px = 既定px × countMul、px→pt は /sxr)。framelabel 流: session→IFontMgr→
		// 既定フォントを selectfont し show(数字=白ストローク縁＋赤fill＋赤ストローク(やや太字)、語=青fill のみ)。
		{
			AutoGSave agx(gPort);
			if (e->changeCount > 0)
			{
				IPMFont* theFont = countFont;	// ループ外で取得済みの既定フォントを使い回す
				if (theFont != nil)
				{
					// 画面pxサイズに拡大率連動の倍率 countMul を掛けてから px→pt(/sxr)へ。
					const PMReal numPt  = (sxr > 0) ? (kKESCMCountTextPx * countMul / sxr) : (pr.Width() / PMReal(24));
					const PMReal wordPt = (sxr > 0) ? (kKESCMCountWordPx * countMul / sxr) : (pr.Width() / PMReal(48));

					// 数字と、その後ろの語("chg")を別サイズで描く。
					PMString numStr;  numStr.SetTranslatable(kFalse);  numStr.AppendNumber(e->changeCount);
					PMString wordStr; wordStr.SetTranslatable(kFalse);
					wordStr.Append(" chg");	// 先頭空白で数字と間隔
					const int32 numCh  = numStr.NumUTF16TextChars();
					const int32 wordCh = wordStr.NumUTF16TextChars();

					// 概算幅(≒0.5em/字)。数字+語をひとまとまりとして横中央に置く。show は baseline 左端基準。
					const PMReal numW  = numPt  * PMReal(0.5) * PMReal(numCh);
					const PMReal wordW = wordPt * PMReal(0.5) * PMReal(wordCh);
					const PMReal startX = (pr.Left() + pr.Right()) / PMReal(2.0) - (numW + wordW) / PMReal(2.0);

					// 【縦位置=ページ上端に固定】数字の上端をページ上端にフラッシュで揃える(スクロール追従なし)。
					// show は baseline 基準ゆえ、概ね文字高さぶん(≒0.78em)下げて文字上端をページ上端に合わせ、
					// さらに kKESCMCountDropPx(画面px)ぶん下へ。drop は px→spread 換算(/sxr)で一定px見えにする。
					// 横位置は従来どおりページ横中央(startX)。数字・語の共通ベースライン。
					const PMReal drop = (sxr > 0) ? (kKESCMCountDropPx / sxr) : (pr.Width() / PMReal(240));
					const PMReal ty = pr.Top() + numPt * PMReal(0.78) + drop;

					// 変更数テキストの不透明度: 印刷時は通常=1.0 / faint=約30%(リングと同率)。画面時は screenMarkOp
					// (通常=1.0 / Shift+Alt薄表示中・30%設定中=約0.3)。リングと同じ実効値で揃える。
					const PMReal textOp = printing ? (sPrintFaint ? kKESCMFaintOpacity : PMReal(1.0)) : screenMarkOp;
					// 薄表示中(<1.0)は数字の重ね描き(赤fill＋赤太らせ)が半透明で足し合わさり濃い赤が残るため、太字化を省く。
					const bool16 faintText = (textOp < PMReal(0.999));

					// テキストの不透明度。画面=screenMarkOp / 印刷=通常1.0・faint0.25。なお印刷フラットナは
					// text show() の setopacity を無視するため、印刷の薄表示は別途「白縁を半分＋外縁を白で削って
					// 細く見せる」で表現している(透明化は断念済み)。ここでは画面の薄表示にだけ効く。
					gPort->setopacity(textOp, kFalse);
					gPort->selectfont(theFont, numPt);
					const UTF16TextChar* numBuf = numStr.GrabUTF16Buffer(nil);

					// 数字の白い縁: 先に白を太めのストロークで描いてハローを作る。印刷時は白縁を半分に細く。
					gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));		// 白
					gPort->setlinewidth(numPt * (printing ? kKESCMCountPrintHaloFrac : kKESCMCountHaloFrac));
					gPort->show(startX, ty, numCh, numBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));

					// 数字の本体: まず塗り(kFillText 単独)で中までベタ赤に。合成フラグ(kFillText|kStrokeText)だと
					// このポートでは塗りが効かず輪郭だけ赤になるため、塗りと縁取りを2回の show に分ける。
					gPort->setrgbcolor(kKESCMCountNumR, kKESCMCountNumG, kKESCMCountNumB);
					gPort->show(startX, ty, numCh, numBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
					// 同色の細い赤ストロークで少し太らせる。輪郭中心ゆえ半分が外へ膨らみ、外側の白ハローを
					// 僅かに侵食して赤を太く見せる(白ハローは更に外側に残る=「赤文字に白縁」)。
					// 印刷時は太らせない(細く出したいので fill のみに留める)。
					if (!faintText && !printing)
					{
						gPort->setlinewidth(numPt * kKESCMCountBodyFrac);
						gPort->show(startX, ty, numCh, numBuf,
							(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));
					}
					// 印刷時のみ: 紙白(白)を細いストロークで上描きして赤fillの外縁を削り、数字を細く見せる
					// (ストロークは輪郭中心ゆえ内側半分が赤を侵食=erode。白ハローは更に外側に残る)。白紙前提。
					if (printing)
					{
						gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));	// 紙白
						gPort->setlinewidth(numPt * kKESCMCountPrintThinFrac);
						gPort->show(startX, ty, numCh, numBuf,
							(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));
					}

					// 語: 小さめ・細め(fill のみ=ストローク無し)・青。数字の直後・同じベースライン。
					gPort->setrgbcolor(kKESCMMarkR, kKESCMMarkG, kKESCMMarkB);
					gPort->selectfont(theFont, wordPt);
					const UTF16TextChar* wordBuf = wordStr.GrabUTF16Buffer(nil);
					gPort->show(startX + numW, ty, wordCh, wordBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
					// 印刷時のみ: 数字と同様に紙白を細いストロークで上描きして語の外縁を削り、半分くらい細く見せる。
					if (printing)
					{
						gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));	// 紙白
						gPort->setlinewidth(wordPt * kKESCMCountWordThinFrac);
						gPort->show(startX + numW, ty, wordCh, wordBuf,
							(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));
					}
				}
			}
		}
	}

	return kFalse;	// 他のハンドラ・描画を続行させる
}


//========================================================================================
// KESCMDrawEventSrvc
//   kDrawEventService サービスとして自身を登録する。アプリ起動時にこのサービスが見つかり、
//   同じ boss 上の IDrwEvtHandler が描画イベントディスパッチャに登録される。
//========================================================================================
class KESCMDrawEventSrvc : public CServiceProvider
{
public:
	KESCMDrawEventSrvc(IPMUnknown* boss) : CServiceProvider(boss) {}
	~KESCMDrawEventSrvc() {}

	virtual ServiceID GetServiceID() { return kDrawEventService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	virtual void GetName(PMString* pName) { pName->SetKey("KESCMDrawEventSrvc\0"); }
	virtual IPlugIn::ThreadingPolicy GetThreadingPolicy() const { return IPlugIn::kMainThreadOnly; }
};

CREATE_PMINTERFACE(KESCMDrawEventSrvc, kKESCMDrawEventSrvcImpl)


//========================================================================================
// ヘルパ: ドキュメント内の全ページUIDを、スプレッド順・ページ順で平坦に集める。
//========================================================================================
static void KESCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out)
{
	if (db == nil)
		return;
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return;
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			out.push_back(spread->GetNthPageUID(p));
	}
}


//========================================================================================
// ミドルボタン peek — 共有状態とヘルパ。
//   ミドルボタンを押している間だけ、マウス下スプレッドの旧版を不透明べた載せし、離すと隠す。
//   IEventWatcher はグローバル(スクリプト引数を持てない)ので、比較相手の旧ドキュメントは先に
//   Document.kescmArmMousePeek(sourceDoc) で登録しておく。watcher(KESCMPeekWatcher)とスクリプト
//   メソッドが下の arm 状態を共有する。全部を1つの翻訳単位に置くことで、watcher が MakeOrigImage /
//   マウス下スプレッド判定 / sOrigImages を直接再利用できる。
//========================================================================================
static IDataBase* sPeekTargetDB = nil;	// 表示中(新)ドキュメント。使用前に「まだ開いているか」を検証する。
static IDataBase* sPeekSourceDB = nil;	// peek 中に重ねる旧ドキュメント。
static bool16     sPeekArmed    = kFalse;

// Shift＋ミドル=旧版を不透明(100%)で / Ctrl(=Win, IEvent::CmdKeyDown)＋ミドル=旧版を 50% で重ねて peek。
// 押下中だけ表示し、ミドルを離すと消す(修飾キーは離してもよい)。判定はミドル押下時に1回見るだけ。
static const PMReal kKESCMPeekSemiOpacity = 0.5;	// Ctrl＋ミドル時の旧版の不透明度(0..1)
static bool16 sPeekActive        = kFalse;	// Shift/Ctrl+ミドルを押し込み中(=覗き表示中)か
static bool16 sSingleShowing     = kFalse;	// 修飾なしミドル押下中(=全マークを約30%で一時表示中)か。離すと隠す＋基準opacityへ
static bool16 sFaintShowing      = kFalse;	// Shift+Alt+ミドル押下中(=全マークを通常=不透明で一時表示中)か。離すと隠す＋基準opacityへ
static bool16 sColorHoldShowing  = kFalse;	// Shift＋Ctrl＋Alt＋ミドル押下中(=色サンプルのトースト表示中)か。離すと消す
// ミドル押下中だけハンドツール(掴んで移動)に一時切替。離すと元のツールへ戻す。
static ITool*  sSavedTool  = nil;	// 切替前のツール(ref を保持。Restore で Release)
static bool16  sHandActive = kFalse;	// ハンドツールに一時切替中か

// 画面マークの「基準」不透明度(=ミドル/Shift+Alt のどちらも押していない常時表示時の値)。
//   印刷マークON＋30%(faint)選択中は 0.3(画面も印刷と同じ薄さ)、それ以外は 1.0(不透明)。
//   ミドル/Shift+Alt を離したら sMarkScreenOpacity をこの値へ戻す。
static PMReal KESCMBaseScreenOpacity()
{
	return (KESCMDrawEventHandler::sPrintMarks && KESCMDrawEventHandler::sPrintFaint)
	       ? kKESCMFaintOpacity : PMReal(1.0);
}

// peek 試行の結果(スクリプトの状態文字列用。watcher は無視する)。
enum KESCMPeekResult { kKESCMPeekNoView = 0, kKESCMPeekNoSpread = 1, kKESCMPeekShown = 2, kKESCMPeekNoChange = 3 };

// 前面レイアウトビューで「マウス下スプレッド」の旧版べた載せを表示する。
//   targetDB=表示中(新)ドキュメント, sourceDB=重ねる旧ドキュメント。
//   そのスプレッドが既にキャッシュ済みなら再利用(即時)。未キャッシュなら旧キャッシュを捨てて、その
//   スプレッドだけをその場でラスタ化(保持は常に1スプレッド)。成功時に sShowOriginal を立てて再描画。
//   outSpread/outPages は任意(nil 可)。
static KESCMPeekResult KESCMPeekShowUnderMouse(IDataBase* targetDB, IDataBase* sourceDB,
	int32* outSpread, int32* outPages)
{
	if (outSpread) *outSpread = -1;
	if (outPages)  *outPages = 0;
	if (targetDB == nil || sourceDB == nil)
		return kKESCMPeekNoView;

	// 前面レイアウトビュー(マウスが乗っているビュー)。前面が layout でなければ nil。
	InterfacePtr<IControlView> view(Utils<ILayoutUIUtils>()->QueryFrontView());
	if (view == nil)
		return kKESCMPeekNoView;

	// 現在のズーム(content→window スケール=ズーム×デバイス倍率)から、画面と 1:1 になる解像度を決める。
	// dpi = 72 × スケール。1:1 のとき最も綺麗(画像px=画面px)。
	PMReal curScale = view->GetContentToWindowMatrix().GetXScale();
	if (curScale < 0) curScale = -curScale;
	if (curScale <= 0) curScale = 1.0;

	// 【低ズームの下限=UI 50%】UIズーム(ユーザーに見える拡大率, デバイス倍率を含まない)が 50% を下回る時は
	// 「50% 相当の解像度」で頭打ちにする。50%以上は画面と 1:1 のままくっきり。50%未満は画像が画面より高精細に
	// なり、縮小blit(点サンプリング)で多少粗くなる(=10% などは汚くてよい、という方針)。下限を UI% で決めるので
	// デバイス倍率に依らず、画面に見える 50% がそのまま境界になる。パノラマ不明時は 1:1(従来=全ズーム綺麗)。
	PMReal effScale = curScale;
	InterfacePtr<IPanorama> peekPano(KESCMQueryPanorama(view));
	if (peekPano != nil)
	{
		const PMReal uiZoom = peekPano->GetXScaleFactor(kFalse);	// UIズーム(例: 0.5=50%)
		if (uiZoom > 0)
		{
			const PMReal deviceScale = curScale / uiZoom;			// 画面デバイス倍率(=curScale/uiZoom)
			const PMReal flooredZoom = (uiZoom < PMReal(0.5)) ? PMReal(0.5) : uiZoom;	// UI 50% で頭打ち
			effScale = flooredZoom * deviceScale;
		}
	}

	PMReal peekDpi = PMReal(72.0) * effScale;
	if (peekDpi < 16.0)  peekDpi = 16.0;	// 安全下限(degenerate 回避。通常は効かない)
	if (peekDpi > 300.0) peekDpi = 300.0;	// 過大メモリ防止(300dpi A4 ≒ 35MB/頁)

	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	const PMReal mx = pt.X(), my = pt.Y();

	// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。新→旧の通し番号対応に使う。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);

	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kKESCMPeekNoSpread;

	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	for (int32 s = 0; s < ns; ++s)
	{
		InterfacePtr<ISpread> spread(targetDB, spreadList->GetNthSpreadUID(s), UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();

		// マウスがこのスプレッドのいずれかのページ上にあるか?
		bool16 inThis = kFalse;
		for (int32 p = 0; p < np; ++p)
		{
			InterfacePtr<IGeometry> geo(targetDB, spread->GetNthPageUID(p), UseDefaultIID());
			if (geo == nil)
				continue;
			PMRect bb = geo->GetPathBoundingBox();
			PMMatrix m = ::InnerToPasteboardMatrix(geo);
			m.Transform(&bb);
			PMReal L = bb.Left(), R = bb.Right(), T = bb.Top(), B = bb.Bottom();
			if (L > R) { PMReal t = L; L = R; R = t; }
			if (T > B) { PMReal t = T; T = B; B = t; }
			if (mx >= L && mx <= R && my >= T && my <= B) { inThis = kTrue; break; }
		}

		if (inThis)
		{
			// 【未更新スプレッドの早期スキップ】このドキュメントで比較が実行済み(sDB==targetDB)で、かつ
			// このスプレッドのどのページも変化エントリ(sEntries)に無いなら、旧版は現行と同一=重ねる意味が
			// 無い。重いラスタ化を丸ごと省いて即 return する(旧版を出さない)。比較が未実行(sDB!=targetDB)
			// なら変化の有無を判定できないので、従来どおりラスタ化する(全スキップしない)。
			if (KESCMDrawEventHandler::sDB == targetDB)
			{
				bool16 anyChanged = kFalse;
				for (int32 p = 0; p < np; ++p)
					if (KESCMDrawEventHandler::sEntries.find(spread->GetNthPageUID(p)) !=
					    KESCMDrawEventHandler::sEntries.end())
					{ anyChanged = kTrue; break; }
				if (!anyChanged)
				{
					if (outSpread) *outSpread = s;
					if (outPages)  *outPages = 0;
					return kKESCMPeekNoChange;
				}
			}

			// このスプレッドは既に丸ごとキャッシュ済みか?(同じ db かつ 全ページが sOrigImages にある) → 再利用(即時)。
			bool16 cached = (KESCMDrawEventHandler::sOrigDB == targetDB);
			for (int32 p = 0; p < np && cached; ++p)
				if (KESCMDrawEventHandler::sOrigImages.find(spread->GetNthPageUID(p)) ==
				    KESCMDrawEventHandler::sOrigImages.end())
					cached = kFalse;
			// ズームが変わっていたら(キャッシュ時と解像度が合わない)作り直す。差が2%以内なら再利用。
			if (cached && KESCMDrawEventHandler::sOrigScale > 0)
			{
				PMReal d = effScale - KESCMDrawEventHandler::sOrigScale;
				if (d < 0) d = -d;
				if (d > KESCMDrawEventHandler::sOrigScale * PMReal(0.02))
					cached = kFalse;
			}

			int32 captured = 0;
			if (cached)
			{
				captured = np;	// ラスタ化不要=キャッシュがこのスプレッドを覆っている
			}
			else
			{
				KESCMDrawEventHandler::DropAllOrig();		// 覗くのは1スプレッドだけ=他は破棄
				KESCMDrawEventHandler::sOrigDB = targetDB;
				KESCMDrawEventHandler::sOrigScale = effScale;	// このラスタ化解像度を記録(再 peek の作り直し判定用)
				for (int32 p = 0; p < np; ++p)
				{
					const int32 gi = globalIndex + p;
					if (gi < (int32)sPages.size())
					{
						UIDRef tRef(targetDB, spread->GetNthPageUID(p));
						UIDRef sRef(sourceDB, sPages[gi]);
						if (KESCMDrawEventHandler::MakeOrigImage(tRef, sRef, peekDpi) == kSuccess)
							++captured;
					}
				}
			}
			KESCMDrawEventHandler::sShowOriginal = kTrue;

			InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
			if (doc != nil)
				Utils<ILayoutUtils>()->InvalidateViews(doc);

			if (outSpread) *outSpread = s;
			if (outPages)  *outPages = captured;
			return kKESCMPeekShown;
		}
		globalIndex += np;
	}
	return kKESCMPeekNoSpread;
}


// ミドル押下中だけ一時的にハンドツール(掴んで移動)へ切り替える。元のツールを覚えておく(離すと戻す)。
// 既に切替中なら何もしない(ハンド自身を「元のツール」として覚えてしまわないため)。
static void KESCMEnterHandTool()
{
	if (sHandActive)
		return;
	ITool* cur  = Utils<IToolBoxUtils>()->QueryActiveTool(kPointerToolBoss);	// +1 ref
	ITool* hand = Utils<IToolBoxUtils>()->QueryTool(kGrabberHandToolBoss);	// +1 ref
	if (hand != nil)
	{
		sSavedTool = cur;	// ref を保持(下の Restore で Release)。cur が nil でも可
		Utils<IToolBoxUtils>()->SetActiveTool(hand, kPointerToolBoss);
		hand->Release();
		sHandActive = kTrue;
	}
	else if (cur != nil)
	{
		cur->Release();
	}
}

// 覚えていた元のツールへ戻す(ハンドに切替えていた場合のみ)。
static void KESCMRestoreTool()
{
	if (!sHandActive)
		return;
	if (sSavedTool != nil)
	{
		Utils<IToolBoxUtils>()->SetActiveTool(sSavedTool, kPointerToolBoss);
		sSavedTool->Release();
		sSavedTool = nil;
	}
	sHandActive = kFalse;
}

// Shift／Ctrl＋ミドル押下を検出したときの共通処理: 「保持中だけ覗く」状態に入り、マウス下スプレッドの旧版を
// opacity(Shift=1.0 不透明 / Ctrl=0.5 半透明)で表示。覗き中もハンドツールにして「旧状態で掴んで移動」できるように。
// 覗き中は枠等(マーク)は不要なので sMarksVisible=kFalse のまま(既定が非表示)＝旧版だけが乗る。覗いている
// スプレッドは旧版が覆い、他スプレッドも非表示なので、画面全体が枠なしの「旧版/現行のみ」になる。
static void KESCMBeginPeekHold(PMReal opacity)
{
	sPeekActive = kTrue;
	KESCMDrawEventHandler::sPeekOpacity = opacity;	// 旧版の不透明度(描画時に (A2) ブロックが参照)
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 覗き中は枠等を出さない(旧版だけ)
	KESCMEnterHandTool();	// 旧状態で掴んで移動
	KESCMPeekShowUnderMouse(sPeekTargetDB, sPeekSourceDB, nil, nil);
}


// トースト表示(後方で定義)。WatchEvent から呼ぶための前方宣言。
static void KESCMShowToast(IDataBase* db, const PMString& msg, uint32 ms);

// 色サンプル(Shift＋Ctrl＋Alt＋ミドル)用。後方で定義。WatchEvent から呼ぶための前方宣言。
static bool16 KESCMSampleCmykUnderMouse(IDataBase* targetDB, IDataBase* sourceDB, PMString& outMsg);
static void   KESCMShowHoldToast(IDataBase* db, const PMString& msg);
static void   KESCMHideHoldToast();

// Alt＋ミドルクリック: マウス下スプレッドだけを再比較して枠(リング)を更新する(部分更新)。
//   targetDB=新(arm 済み表示中) / sourceDB=旧(arm 済み比較相手)。新→旧ページは平坦通し番号で対応。
//   ・各ページを MakeEntry で取り直し(編集後の差分に更新)。変化が無くなったページは古い枠を消す。
//   ・旧版画像キャッシュ(sOrigImages)は古いので破棄(次の peek で作り直し)。
//   見つかったスプレッドの index(0始まり)を outSpread に、変化ページ数を outChanged に返す。戻り=見つかったか。
static bool16 KESCMRefreshSpreadUnderMouse(IDataBase* targetDB, IDataBase* sourceDB, int32* outSpread, int32* outChanged)
{
	if (outSpread)  *outSpread = -1;
	if (outChanged) *outChanged = 0;
	if (targetDB == nil || sourceDB == nil)
		return kFalse;

	InterfacePtr<IControlView> view(Utils<ILayoutUIUtils>()->QueryFrontView());
	if (view == nil)
		return kFalse;

	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	const PMReal mx = pt.X(), my = pt.Y();

	// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);

	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kFalse;

	// マークの所属ドキュメントを合わせる(別 doc にマークがあった場合のみ総入れ替え=通常は一致で何もしない)。
	if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sDB != targetDB)
		KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	for (int32 s = 0; s < ns; ++s)
	{
		InterfacePtr<ISpread> spread(targetDB, spreadList->GetNthSpreadUID(s), UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();

		// マウスがこのスプレッドのいずれかのページ上にあるか?
		bool16 inThis = kFalse;
		for (int32 p = 0; p < np; ++p)
		{
			InterfacePtr<IGeometry> geo(targetDB, spread->GetNthPageUID(p), UseDefaultIID());
			if (geo == nil)
				continue;
			PMRect bb = geo->GetPathBoundingBox();
			PMMatrix m = ::InnerToPasteboardMatrix(geo);
			m.Transform(&bb);
			PMReal L = bb.Left(), R = bb.Right(), T = bb.Top(), B = bb.Bottom();
			if (L > R) { PMReal t = L; L = R; R = t; }
			if (T > B) { PMReal t = T; T = B; B = t; }
			if (mx >= L && mx <= R && my >= T && my <= B) { inThis = kTrue; break; }
		}

		if (inThis)
		{
			// このスプレッドの各ページを再比較して枠を更新。新→旧は globalIndex で対応。
			int32 changedCount = 0;
			for (int32 p = 0; p < np; ++p)
			{
				const int32 gi = globalIndex + p;
				if (gi >= (int32)sPages.size())
					continue;
				const UID tUID = spread->GetNthPageUID(p);
				bool16 changed = kFalse;
				KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tUID), UIDRef(sourceDB, sPages[gi]), changed);
				if (changed)
					++changedCount;
				else
				{
					// 変化が無くなったページ → 古い枠が残っていれば消す(更新で消えるべき)。
					std::map<UID, KESCMOverlayEntry*>::iterator old = KESCMDrawEventHandler::sEntries.find(tUID);
					if (old != KESCMDrawEventHandler::sEntries.end())
					{ delete old->second; KESCMDrawEventHandler::sEntries.erase(old); }
				}
			}

			// 旧版画像キャッシュは古いので破棄(次の peek で現ズームで作り直し)。
			KESCMDrawEventHandler::DropAllOrig();

			InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
			if (doc != nil)
				Utils<ILayoutUtils>()->InvalidateViews(doc);

			if (outSpread)  *outSpread = s;
			if (outChanged) *outChanged = changedCount;
			return kTrue;
		}
		globalIndex += np;
	}
	return kFalse;
}


// マーク(枠/変更数)の表示を切り替えた後、マークが属するドキュメント(sDB)を再描画して
// 即反映する。arm の有無に依らず使えるよう、peek 用の sPeekTargetDB ではなく sDB を使う(arm 不要)。
static void KESCMInvalidateMarksDoc()
{
	IDataBase* db = KESCMDrawEventHandler::sDB;
	if (db == nil)
		return;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);
}


//========================================================================================
// KESCMPeekWatcher
//   非消費のイベントウォッチャ。peek が arm 済み(kescmArmMousePeek)の間、Shift＋ミドルボタンを押すと
//   マウス下スプレッドの旧版べた載せを表示し、ミドルを離すと隠す。非消費=ミドルボタン本来の動作も走る。
//========================================================================================
class KESCMPeekWatcher : public CPMUnknown<IEventWatcher>
{
public:
	KESCMPeekWatcher(IPMUnknown* boss) : CPMUnknown<IEventWatcher>(boss), fWatching(kFalse) {}
	~KESCMPeekWatcher() {}

	IEventDispatcher::EventTypeList WatchEvent(IEvent* e);
	void StartWatching();
	void StopWatching();

private:
	bool16 fWatching;
};

CREATE_PMINTERFACE(KESCMPeekWatcher, kKESCMPeekWatcherImpl)

IEventDispatcher::EventTypeList KESCMPeekWatcher::WatchEvent(IEvent* e)
{
	// 興味=ミドル押下/解放のみ。毎回返す(空を返すと監視解除される)。Shift 判定は押下イベントで見る。
	IEventDispatcher::EventTypeList interest(IEvent::kMButtonDn, IEvent::kMButtonUp);

	if (e == nil)
		return interest;

	const IEvent::EventType type = e->GetType();
	if (type != IEvent::kMButtonDn && type != IEvent::kMButtonUp)
		return interest;

	// 旧版べた載せ(peek)の検証は arm 済みの時だけ。シングルの枠表示は arm 不要なので、ここで素通りさせる。
	if (sPeekArmed)
	{
		// arm 済みドキュメントがまだ開いているか検証(片方を閉じた後のダングリング参照を防ぐ)。
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList == nil ||
		    docList->FindDocByDataBase(sPeekTargetDB) == nil ||
		    docList->FindDocByDataBase(sPeekSourceDB) == nil)
		{
			// ドキュメントが消えた → disarm して全部破棄。
			KESCMRestoreTool();	// ハンドに切替え中なら元へ戻す
			sPeekArmed = kFalse;
			sPeekTargetDB = nil;
			sPeekSourceDB = nil;
			sPeekActive = kFalse;
			sSingleShowing = kFalse;
			sFaintShowing = kFalse;
			sColorHoldShowing = kFalse;
			KESCMDrawEventHandler::sToastVisible = kFalse;	// 色サンプルのトーストが出ていれば消す(db が消えたため)
			KESCMDrawEventHandler::sToastDB = nil;
			KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)へ
			KESCMDrawEventHandler::sMarkScreenOpacity = 1.0;	// 不透明度も既定へ戻す
			KESCMDrawEventHandler::DropAllOrig();
			return interest;
		}
	}

	if (type == IEvent::kMButtonDn)
	{
		if (sPeekArmed && e->ShiftKeyDown() && e->CmdKeyDown() && e->OptionAltKeyDown())
		{
			// Shift＋Ctrl＋Alt＋ミドル押下: クリック点の CMYK 生値(0..255)を新・旧でサンプリングし、押下中だけ
			// トーストで "Target C.. M.. Y.. K..  Source C.. M.. Y.. K.." を表示。離す(kMButtonUp)と消す。3キー同時は
			// この先頭分岐で捕まえる(後続の Shift/Ctrl/Alt 単独 peek より前に置く=単独分岐に吸われないため)。
			PMString colorMsg;
			if (KESCMSampleCmykUnderMouse(sPeekTargetDB, sPeekSourceDB, colorMsg))
			{
				sColorHoldShowing = kTrue;
				KESCMShowHoldToast(sPeekTargetDB, colorMsg);
			}
		}
		else if (e->ShiftKeyDown() && e->OptionAltKeyDown() && !e->CmdKeyDown())
		{
			// Shift＋Alt＋ミドル押下: 既存マーク(リング＋変更数)を「通常(不透明)」で表示。peek(旧版べた載せ)とは別物で
			// arm 不要(見せるだけ)。ハンドツールに切替えて枠を見ながら掴んで移動できる。離すと非表示へ戻す。
			// マークが無ければ無反応(素のミドルを邪魔しない)。Shift 単独/ Alt 単独の peek 分岐より前に置く=吸われない。
			const bool16 haveContent = !KESCMDrawEventHandler::sEntries.empty();
			if (haveContent)
			{
				sFaintShowing = kTrue;
				KESCMDrawEventHandler::sMarkScreenOpacity = 1.0;				// 通常=不透明(印刷30%中でも不透明で確認できる)
				KESCMDrawEventHandler::sMarksVisible = kTrue;					// 押下中だけ表示
				KESCMEnterHandTool();											// 枠を見ながら掴んで移動
				KESCMInvalidateMarksDoc();
			}
		}
		else if (sPeekArmed && e->ShiftKeyDown())
		{
			// Shift＋ミドル押下: マウス下スプレッドの旧版べた載せ(peek)を不透明(100%)で開始。押下中だけ表示。
			// 判定はこの押下時の修飾キー状態のみ。以後キーを離しても変わらず、ミドルを離すと消える。
			KESCMBeginPeekHold(PMReal(1.0));
		}
		else if (sPeekArmed && e->OptionAltKeyDown())
		{
			// Alt(=Win, OptionAltKeyDown)＋ミドル押下: 同じ peek を 50% 透明で重ねる(現行ページと半々のゴースト比較)。
			KESCMBeginPeekHold(kKESCMPeekSemiOpacity);
		}
		else if (sPeekArmed && e->CmdKeyDown())
		{
			// Ctrl(=Win, CmdKeyDown)＋ミドル押下(momentary): マウス下スプレッドだけ枠を再検出して更新。
			// 旧版画像キャッシュは破棄(次 peek で作り直し)。完了したら「spread N updated」をトースト表示。
			int32 sp = -1;
			if (KESCMRefreshSpreadUnderMouse(sPeekTargetDB, sPeekSourceDB, &sp, nil))
			{
				PMString msg("spread ");
				msg.SetTranslatable(kFalse);
				msg.AppendNumber(sp + 1);	// スプレッド番号(1始まり)
				msg.Append(" markers refreshed");
				KESCMShowToast(sPeekTargetDB, msg, kKESCMToastDefaultMs);
			}
		}
		else if (!e->ShiftKeyDown() && !e->CmdKeyDown() && !e->OptionAltKeyDown())
		{
			// シングル動作(修飾キーなしミドル押下中): 全マーク(リング＋変更数)を「約30%で薄表示」にして、
			// ハンドツールに切替えて「枠を見ながら掴んで移動」できるようにする。離す(kMButtonUp)と非表示＋不透明度を戻す。
			// マークが何も無い(エントリ無し)時は反応しない=素のミドルクリックを邪魔しない。
			const bool16 haveContent = !KESCMDrawEventHandler::sEntries.empty();
			if (haveContent)
			{
				sSingleShowing = kTrue;
				KESCMDrawEventHandler::sMarkScreenOpacity = kKESCMFaintOpacity;	// ミドルのみ=30%薄表示
				KESCMDrawEventHandler::sMarksVisible = kTrue;	// 押下中だけ枠等を表示
				KESCMEnterHandTool();	// 枠を見ながら掴んで移動
				KESCMInvalidateMarksDoc();
			}
		}
		// (Shift/Ctrl/Alt を押していて arm 未済 → 何もしない。これらは peek 系専用に予約)
	}
	else // kMButtonUp
	{
		// ミドルを離したら、ハンドに切替えていた場合は元のツールへ戻す(シングル/ダブル共通)。
		KESCMRestoreTool();

		if (sColorHoldShowing)
		{
			// Shift＋Ctrl＋Alt＋ミドルを離した → 色サンプルのトーストを消す(他の状態には触らない)。
			sColorHoldShowing = kFalse;
			KESCMHideHoldToast();
		}

		if (sPeekActive)
		{
			// Shift／Alt＋ミドルを離した(ミドル解放) → 旧版を隠す(マークは触らない)。キャッシュは保持(再 peek は即時)。
			sPeekActive = kFalse;
			if (KESCMDrawEventHandler::sShowOriginal)
			{
				KESCMDrawEventHandler::sShowOriginal = kFalse;
				InterfacePtr<IDocument> doc(sPeekTargetDB, sPeekTargetDB->GetRootUID(), UseDefaultIID());
				if (doc != nil)
					Utils<ILayoutUtils>()->InvalidateViews(doc);
			}
		}
		else if (sSingleShowing)
		{
			// ミドルのみの押下を離した → 30%表示を解除し、不透明度を基準値(印刷設定に応じた値)へ戻す＋非表示へ。
			sSingleShowing = kFalse;
			KESCMDrawEventHandler::sMarksVisible = kFalse;
			KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();
			KESCMInvalidateMarksDoc();
		}
		else if (sFaintShowing)
		{
			// Shift＋Alt＋ミドルを離した → 通常(不透明)表示を解除し、不透明度を基準値へ戻す＋非表示へ。
			sFaintShowing = kFalse;
			KESCMDrawEventHandler::sMarksVisible = kFalse;
			KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();
			KESCMInvalidateMarksDoc();
		}
	}
	return interest;
}

void KESCMPeekWatcher::StartWatching()
{
	if (fWatching) return;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IEventDispatcher> dispatcher(app, UseDefaultIID());
	if (dispatcher)
	{
		dispatcher->AddEventWatcher(this, IEventDispatcher::EventTypeList(IEvent::kMButtonDn, IEvent::kMButtonUp));
		fWatching = kTrue;
	}
}

void KESCMPeekWatcher::StopWatching()
{
	if (!fWatching) return;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IEventDispatcher> dispatcher(app, UseDefaultIID());
	if (dispatcher)
		dispatcher->RemoveEventWatcher(this, IEventDispatcher::EventTypeList());	// 空=全種の監視を解除
	fWatching = kFalse;
}


//========================================================================================
// KESCMPeekStartup
//   アプリ起動時に peek ウォッチャを生成して監視を開始する。
//========================================================================================
class KESCMPeekStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KESCMPeekStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss), fWatcher(nil) {}
	~KESCMPeekStartup() {}

	virtual void Startup();
	virtual void Shutdown();

private:
	IEventWatcher* fWatcher;
};

CREATE_PMINTERFACE(KESCMPeekStartup, kKESCMPeekStartupImpl)

void KESCMPeekStartup::Startup()
{
	fWatcher = ::CreateObject2<IEventWatcher>(kKESCMPeekWatcherBoss);
	if (fWatcher)
		fWatcher->StartWatching();
}

void KESCMPeekStartup::Shutdown()
{
	if (fWatcher)
	{
		fWatcher->StopWatching();
		fWatcher->Release();
		fWatcher = nil;
	}
}


//========================================================================================
// KESCMToastIdleTask — 一時トーストの自動消去タイマ。
//   KESCMShowToast() が AddTask(this, ms) で登録 → ms 後に RunTask が呼ばれてトーストを非表示にし
//   再描画する。kEndOfTime を返して自分をキューから外す(タイマ本体オブジェクトはセッション中
//   sToastTask に保持して再利用)。
//========================================================================================
static IIdleTask* sToastTask   = nil;	// タイマ本体(起動中に1度だけ生成して再利用)
static bool16     sToastQueued = kFalse;	// タイマが現在キューに入っているか(二重 AddTask 防止)

class KESCMToastIdleTask : public CPMUnknown<IIdleTask>
{
public:
	KESCMToastIdleTask(IPMUnknown* boss) : CPMUnknown<IIdleTask>(boss) {}
	~KESCMToastIdleTask() {}

	virtual uint32 RunTask(uint32 appFlags, IdleTimer* timeCheck);
	virtual void InstallTask(uint32 millisecsBeforeFirstRun);
	virtual void UninstallTask();
	virtual const char* TaskName() { return "KESCMToastIdleTask"; }
};

CREATE_PMINTERFACE(KESCMToastIdleTask, kKESCMToastIdleTaskImpl)

uint32 KESCMToastIdleTask::RunTask(uint32 /*appFlags*/, IdleTimer* /*timeCheck*/)
{
	// トーストを消して再描画。対象ドキュメントが既に閉じていれば再描画はスキップ(ダングリング回避)。
	KESCMDrawEventHandler::sToastVisible = kFalse;
	IDataBase* db = KESCMDrawEventHandler::sToastDB;
	KESCMDrawEventHandler::sToastDB = nil;
	if (db != nil)
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList != nil && docList->FindDocByDataBase(db) != nil)
		{
			InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
			if (doc != nil)
				Utils<ILayoutUtils>()->InvalidateViews(doc);
		}
	}
	sToastQueued = kFalse;
	return IIdleTask::kEndOfTime;	// 自分をキューから除去(オブジェクトは sToastTask に保持=次の表示で再利用)
}

void KESCMToastIdleTask::InstallTask(uint32 millisecsBeforeFirstRun)
{
	InterfacePtr<IIdleTaskMgr> mgr(GetExecutionContextSession(), UseDefaultIID());
	if (mgr != nil)
		mgr->AddTask(this, millisecsBeforeFirstRun);
}

void KESCMToastIdleTask::UninstallTask()
{
	InterfacePtr<IIdleTaskMgr> mgr(GetExecutionContextSession(), UseDefaultIID());
	if (mgr != nil)
		mgr->RemoveTask(this);
}

// 画面中央に msg を ms ミリ秒だけ表示し、その後自動で消す。db=描画するドキュメント(前面)。
// 直近の表示タイマが生きていれば取り消して入れ直す(=最後の表示から ms 後に消える)。
static void KESCMShowToast(IDataBase* db, const PMString& msg, uint32 ms)
{
	KESCMDrawEventHandler::sToastMsg = msg;
	KESCMDrawEventHandler::sToastMsg.SetTranslatable(kFalse);
	KESCMDrawEventHandler::sToastVisible = kTrue;
	KESCMDrawEventHandler::sToastDB = db;

	// 即時に1回描く。
	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	// 自動消去タイマ(IIdleTask)。タイマ本体はセッション中1個を生成して再利用。
	InterfacePtr<IIdleTaskMgr> mgr(GetExecutionContextSession(), UseDefaultIID());
	if (mgr == nil)
		return;
	if (sToastTask == nil)
		sToastTask = ::CreateObject2<IIdleTask>(kKESCMToastIdleTaskBoss);	// +1 ref, セッション保持
	if (sToastTask == nil)
		return;
	if (sToastQueued)			// 直近のタイマを取り消して入れ直す(同一タスクの二重 AddTask は不可のため)
		mgr->RemoveTask(sToastTask);
	mgr->AddTask(sToastTask, ms);
	sToastQueued = kTrue;
}


// 押下中だけ表示するトースト(自動消去タイマを使わない)。色サンプル(Shift＋Ctrl＋Alt＋ミドル)用。
//   直近の自動消去タイマが残っていると押下中に消えてしまうので、あれば取り消してから表示する。
static void KESCMShowHoldToast(IDataBase* db, const PMString& msg)
{
	KESCMDrawEventHandler::sToastMsg = msg;
	KESCMDrawEventHandler::sToastMsg.SetTranslatable(kFalse);
	KESCMDrawEventHandler::sToastVisible = kTrue;
	KESCMDrawEventHandler::sToastDB = db;

	// 自動消去タイマが生きていれば取り消す(押下中は離すまで残す)。
	if (sToastQueued && sToastTask != nil)
	{
		InterfacePtr<IIdleTaskMgr> mgr(GetExecutionContextSession(), UseDefaultIID());
		if (mgr != nil)
			mgr->RemoveTask(sToastTask);
		sToastQueued = kFalse;
	}

	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}
}

// 押下中トーストを消す(ミドルを離したとき)。db がまだ開いていれば再描画して即反映する。
static void KESCMHideHoldToast()
{
	if (!KESCMDrawEventHandler::sToastVisible)
		return;
	IDataBase* db = KESCMDrawEventHandler::sToastDB;
	KESCMDrawEventHandler::sToastVisible = kFalse;
	KESCMDrawEventHandler::sToastDB = nil;
	if (db == nil)
		return;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList != nil && docList->FindDocByDataBase(db) != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}
}

// pageRef のページを、spreadPt(そのページの spread 座標)まわりの極小領域だけ CMYK・高dpi でラスタ化し、
// 中心1画素の C,M,Y,K 生値(0..255)を out[4] に読む。アクセサ/スナップショットは即破棄(保持ゼロで
// 破棄時クラッシュを回避)。成功で kTrue。
static bool16 KESCMReadCmykPixel(const UIDRef& pageRef, const PMPoint& spreadPt, uint8 out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0;
	if (pageRef.GetDataBase() == nil || pageRef.GetUID() == kInvalidUID)
		return kFalse;

	// クリック点まわりの極小矩形(spread 座標)。boundsToSpreadMatrix=identity(=既に spread 座標)。
	const PMReal hp = kKESCMSampleHalfPt;
	PMRect clip(spreadPt.X() - hp, spreadPt.Y() - hp, spreadPt.X() + hp, spreadPt.Y() + hp);

	SnapshotUtilsEx* snap = new SnapshotUtilsEx(clip, PMMatrix(), pageRef, 1.0, 1.0,
		kKESCMSampleDpi, 72.0, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	KESCMDrawEventHandler::sRasterizing = kTrue;	// この Draw 中の再入でマークを描かせない
	ErrorCode drew = snap->Draw(IShape::kPreviewMode, kTrue /*fullRes*/, 7.0, kFalse /*AA off*/);
	KESCMDrawEventHandler::sRasterizing = kFalse;
	AGMImageAccessor* acc = (drew == kSuccess) ? snap->CreateAGMImageAccessor() : nil;

	bool16 ok = kFalse;
	if (acc != nil)
	{
		Int32Rect b = acc->GetBounds();
		const int32 w = b.right - b.left, h = b.bottom - b.top;
		const int32 rb = (int32)acc->GetRowBytes();
		const int32 bpp = (int32)acc->GetBitsPerPixel() / 8;
		const uint8* base = acc->GetBaseAddr();
		if (base != nil && w > 0 && h > 0 && rb > 0 && bpp >= 4)
		{
			const int32 cx = w / 2, cy = h / 2;	// 中心画素=クリック点
			const uint8* px = base + (size_t)cy * rb + (size_t)cx * bpp;
			out[0] = px[0]; out[1] = px[1]; out[2] = px[2]; out[3] = px[3];	// C,M,Y,K(offset 0)
			ok = kTrue;
		}
		delete acc;
	}
	delete snap;
	return ok;
}

// 0..255 の値を必ず3桁(ゼロ埋め)で追記する。Target/Source の C/M/Y/K の桁を縦に揃えて見やすくするため
// (AppendNumber はゼロ埋めしないので桁ごとに分けて出す)。
static void KESCMAppend3(PMString& s, int32 v)
{
	if (v < 0)   v = 0;
	if (v > 999) v = 999;
	s.AppendNumber(v / 100);
	s.AppendNumber((v / 10) % 10);
	s.AppendNumber(v % 10);
}

// CMYK ラスタの 8bit 値(0..255) を、本来の CMYK 数値である 0..100% に四捨五入で換算する。
// 例: 255→100 / 0→0 / 128→50。(v*100+127)/255 で round。
static int32 KESCMByteToPct(uint8 v)
{
	return ((int32)v * 100 + 127) / 255;
}

// Shift＋Ctrl＋Alt＋ミドルクリック: マウス下ページのクリック点 CMYK 生値を新(target)・旧(source)で
// サンプリングし、"Target C000 …(改行)Source C000 …"(各値3桁ゼロ埋め)を outMsg に組む。成功で kTrue。
//   新→旧ページは平坦通し番号で対応。クリック点を inner(ページ内)座標へ戻し、新/旧それぞれの spread
//   座標へ写してから各ページを極小ラスタ化する(新旧の幾何一致が前提)。
static bool16 KESCMSampleCmykUnderMouse(IDataBase* targetDB, IDataBase* sourceDB, PMString& outMsg)
{
	if (targetDB == nil || sourceDB == nil)
		return kFalse;

	InterfacePtr<IControlView> view(Utils<ILayoutUIUtils>()->QueryFrontView());
	if (view == nil)
		return kFalse;

	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	const PMReal mx = pt.X(), my = pt.Y();

	// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。新→旧の通し番号対応に使う。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);

	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kFalse;

	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	for (int32 s = 0; s < ns; ++s)
	{
		InterfacePtr<ISpread> tSpread(targetDB, spreadList->GetNthSpreadUID(s), UseDefaultIID());
		if (tSpread == nil)
			continue;
		const int32 np = tSpread->GetNumPages();

		for (int32 p = 0; p < np; ++p)
		{
			const UID tPageUID = tSpread->GetNthPageUID(p);
			InterfacePtr<IGeometry> tGeo(targetDB, tPageUID, UseDefaultIID());
			if (tGeo == nil)
				continue;
			PMRect bbPB = tGeo->GetPathBoundingBox();
			PMMatrix mPB = ::InnerToPasteboardMatrix(tGeo);
			mPB.Transform(&bbPB);
			PMReal L = bbPB.Left(), R = bbPB.Right(), T = bbPB.Top(), B = bbPB.Bottom();
			if (L > R) { PMReal t = L; L = R; R = t; }
			if (T > B) { PMReal t = T; T = B; B = t; }
			if (!(mx >= L && mx <= R && my >= T && my <= B))
				continue;

			// ヒット。新→旧ページ対応(平坦通し番号)。
			const int32 gi = globalIndex + p;
			if (gi >= (int32)sPages.size())
				return kFalse;
			const UID sPageUID = sPages[gi];
			InterfacePtr<IGeometry> sGeo(sourceDB, sPageUID, UseDefaultIID());
			if (sGeo == nil)
				return kFalse;

			// クリック点(pasteboard) → ページ内(inner)座標 → 新/旧それぞれの spread 座標。
			if (mPB.IsSingular())
				return kFalse;
			PMPoint inner(mx, my);
			PMMatrix fromPB = mPB.Inverse();
			fromPB.Transform(&inner);

			PMPoint tSpreadPt(inner.X(), inner.Y());
			::InnerToSpreadMatrix(tGeo).Transform(&tSpreadPt);
			PMPoint sSpreadPt(inner.X(), inner.Y());
			::InnerToSpreadMatrix(sGeo).Transform(&sSpreadPt);

			uint8 cN[4], cO[4];
			const bool16 okN = KESCMReadCmykPixel(UIDRef(targetDB, tPageUID), tSpreadPt, cN);
			const bool16 okO = KESCMReadCmykPixel(UIDRef(sourceDB, sPageUID), sSpreadPt, cO);
			if (!okN || !okO)
				return kFalse;

			// ラベルは ASCII。1行目=Target(新/cN)、改行(LF)、2行目=Source(旧/cO)。各値はラスタ8bit(0..255)を
			// 本来の CMYK 数値 0..100% に換算し、3桁ゼロ埋めで桁を縦に揃える。
			outMsg.SetTranslatable(kFalse);
			outMsg.Append("Target\tC"); KESCMAppend3(outMsg, KESCMByteToPct(cN[0]));	// TAB=ラベル列/値列の区切り(値の桁を固定列で縦揃え)
			outMsg.Append(" M");       KESCMAppend3(outMsg, KESCMByteToPct(cN[1]));
			outMsg.Append(" Y");       KESCMAppend3(outMsg, KESCMByteToPct(cN[2]));
			outMsg.Append(" K");       KESCMAppend3(outMsg, KESCMByteToPct(cN[3]));
			outMsg.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
			outMsg.Append("Source\tC"); KESCMAppend3(outMsg, KESCMByteToPct(cO[0]));	// TAB=ラベル列/値列の区切り(値の桁を固定列で縦揃え)
			outMsg.Append(" M");       KESCMAppend3(outMsg, KESCMByteToPct(cO[1]));
			outMsg.Append(" Y");       KESCMAppend3(outMsg, KESCMByteToPct(cO[2]));
			outMsg.Append(" K");       KESCMAppend3(outMsg, KESCMByteToPct(cO[3]));
			return kTrue;
		}
		globalIndex += np;
	}
	return kFalse;
}


//========================================================================================
// KESCMScriptProvider
//   Page / Document オブジェクトに kescmMarkChangesDoc / kescmClearMarks / kescmArmMousePeek /
//   kescmDisarmMousePeek / kescmToast / kescmSetPrintMarks を生やす。
//========================================================================================
//========================================================================================
// Shared core operations (declared in KESCMCore.h).
//
// These are the bodies that used to live inline in the scripting methods below. They are now
// plain (non-static) functions so the panel widget observer (KESCMPanelObserver.cpp) can drive
// the exact same behavior. They run in this translation unit on purpose: that gives them direct
// access to the overlay engine (KESCMDrawEventHandler) and the file-local peek state (sPeek*).
//========================================================================================

ErrorCode KESCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport)
{
	if (targetDB == nil || sourceDB == nil)
		return kFailure;

	// ドキュメント単位の総入れ替え。
	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	// 両ドキュメントのページUIDをドキュメント順に平坦列挙。
	std::vector<UID> tPages, sPages;
	KESCMCollectPageUIDs(targetDB, tPages);
	KESCMCollectPageUIDs(sourceDB, sPages);

	// 比較は同期実行で全ページをラスタ化するため時間がかかる。ループ前に「Comparing changes...」を出し、
	// ForceRedraw で即時に1回描いてからループに入る(ブロック中も表示が見えるようにする)。
	{
		PMString busyMsg("Comparing changes...");
		busyMsg.SetTranslatable(kFalse);
		KESCMShowToast(targetDB, busyMsg, kKESCMToastDefaultMs);
		InterfacePtr<IControlView> fv(Utils<ILayoutUIUtils>()->QueryFrontView());
		if (fv != nil)
			fv->ForceRedraw(nil, kTrue);	// ブロックする比較ループの前に同期描画
	}

	const size_t n = (tPages.size() < sPages.size()) ? tPages.size() : sPages.size();
	int32 changedCount = 0;
	for (size_t i = 0; i < n; ++i)
	{
		bool16 changed = kFalse;
		KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
		if (changed) ++changedCount;
	}

	InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append("kescm: pages compared="); report.AppendNumber((int32)n);
	report.Append(" changed="); report.AppendNumber(changedCount);
	outReport = report;
	return kSuccess;
}

void KESCMDoClearMarks(IDataBase* db)
{
	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::DropAllOrig();	// 旧版べた載せのキャッシュも解放(メモリ開放)

	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}
}

void KESCMDoSetPrintMarks(bool16 printFlag, bool16 faintFlag, IDataBase* db)
{
	KESCMDrawEventHandler::sPrintMarks = printFlag;
	KESCMDrawEventHandler::sPrintFaint = faintFlag;
	// 常時表示(画面)の不透明度を印刷設定に合わせて即反映。
	KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();

	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}
}

void KESCMDoArmMousePeek(IDataBase* targetDB, IDataBase* sourceDB)
{
	// arm 対象が変わったら古い peek キャッシュは捨てる。
	if (sPeekSourceDB != sourceDB || sPeekTargetDB != targetDB)
		KESCMDrawEventHandler::DropAllOrig();

	sPeekTargetDB = targetDB;
	sPeekSourceDB = sourceDB;
	sPeekArmed = kTrue;
	sPeekActive = kFalse;			// 覗き状態を初期化
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)へ。arm 中も枠は押下中だけ表示

	PMString onMsg("ChangeMarker ON");
	onMsg.SetTranslatable(kFalse);
	KESCMShowToast(targetDB, onMsg, kKESCMToastDefaultMs);
}

void KESCMDoDisarmMousePeek(IDataBase* db)
{
	KESCMRestoreTool();	// ハンドに切替え中なら元のツールへ戻す
	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;
	sPeekActive = kFalse;
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)のまま
	KESCMDrawEventHandler::DropAllOrig();	// sShowOriginal も OFF にし、キャッシュを解放

	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	PMString offMsg("ChangeMarker OFF");
	offMsg.SetTranslatable(kFalse);
	KESCMShowToast(db, offMsg, kKESCMToastDefaultMs);
}

// Panel state accessors (reflect the armed peek = "started" state).
bool16     KESCMIsArmed()        { return sPeekArmed; }
IDataBase* KESCMArmedTargetDB()  { return sPeekTargetDB; }
IDataBase* KESCMArmedSourceDB()  { return sPeekSourceDB; }


class KESCMScriptProvider : public CScriptProvider
{
public:
	KESCMScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	~KESCMScriptProvider() {}

	virtual ErrorCode HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent);

private:
	// kescmMarkChangesDoc(sourceDoc): このドキュメント全ページを sourceDoc と突き合わせて総入れ替え。
	ErrorCode MarkChangesDoc(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmClearMarks(): オーバーレイを全消去。
	ErrorCode ClearMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmArmMousePeek(sourceDoc): ミドルボタン peek を arm(比較相手の旧ドキュメントを登録)。Document 対応。
	ErrorCode ArmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmDisarmMousePeek(): ミドルボタン peek を解除し、旧版べた載せを隠してキャッシュも解放。Document 対応。
	ErrorCode DisarmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmToast(message): 画面中央に message を少し表示して自動で消す。Page/Document 対応。
	ErrorCode Toast(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmSetPrintMarks([flag]): 変更マーク(リング＋変更数)を印刷/PDF に出すか。ON の間は画面も常時表示。Page/Document 対応。
	ErrorCode SetPrintMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent);
};

CREATE_PMINTERFACE(KESCMScriptProvider, kKESCMScriptProviderImpl)


ErrorCode KESCMScriptProvider::HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	ErrorCode status = kFailure;
	switch (methodID.Get())
	{
	case e_KESCMMarkChangesDoc:
		status = MarkChangesDoc(methodID, data, parent);
		break;
	case e_KESCMClearMarks:
		status = ClearMarks(methodID, data, parent);
		break;
	case e_KESCMArmMousePeek:
		status = ArmMousePeek(methodID, data, parent);
		break;
	case e_KESCMDisarmMousePeek:
		status = DisarmMousePeek(methodID, data, parent);
		break;
	case e_KESCMToast:
		status = Toast(methodID, data, parent);
		break;
	case e_KESCMSetPrintMarks:
		status = SetPrintMarks(methodID, data, parent);
		break;
	default:
		status = CScriptProvider::HandleMethod(methodID, data, parent);
	}
	return status;
}


/* MarkChangesDoc
   このドキュメント(parent=Document)の全ページを、sourceDoc の同じ番号のページと突き合わせ、
   変化したページ全部にリングを付ける(既存マークは総入れ替え)。ページ数が違う場合は重なる範囲のみ。
*/
ErrorCode KESCMScriptProvider::MarkChangesDoc(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef targetDocRef = ::GetUIDRef(parent);
	IDataBase* targetDB = targetDocRef.GetDataBase();
	if (targetDB == nil)
		return kFailure;

	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMSourceDoc, arg) != kSuccess)
		return kFailure;
	InterfacePtr<IScript> srcScript(arg.QueryObject());
	if (srcScript == nil)
		return kFailure;
	IDataBase* sourceDB = ::GetUIDRef(srcScript).GetDataBase();
	if (sourceDB == nil)
		return kFailure;

	PMString report;
	if (KESCMDoMarkChangesDoc(targetDB, sourceDB, report) != kSuccess)
		return kFailure;

	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ClearMarks
   全エントリを破棄し、対象ドキュメントを再描画してオーバーレイを消す。Page でも Document でも可。
*/
ErrorCode KESCMScriptProvider::ClearMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();

	KESCMDoClearMarks(db);

	PMString report("marks cleared");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* SetPrintMarks
   変更マーク(リング＋変更数)を印刷/PDF にも出すかを切り替える(既定=画面のみ)。ON の間は、ミドル押下に
   関係なく画面でも常時表示(WYSIWYG)。マークのデータ(sEntries)は触らず、表示/印刷の挙動だけ変える。
   引数 flag は省略可(省略時=ON=印刷する)。Page でも Document でも呼べる。
*/
ErrorCode KESCMScriptProvider::SetPrintMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	bool16 flag = kTrue;	// 省略時は印刷ON
	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMPrintMarksFlag, arg) == kSuccess)
		arg.GetBoolean(&flag);

	bool16 faint = kFalse;	// 省略時は通常不透明度(約70%)
	ScriptData arg2;
	if (data->ExtractRequestData(p_KESCMPrintFaintFlag, arg2) == kSuccess)
		arg2.GetBoolean(&faint);

	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	KESCMDoSetPrintMarks(flag, faint, db);

	PMString report;
	report.SetTranslatable(kFalse);
	if (flag)
		report.Append(faint ? "kescm: marks will print at ~25% (and stay visible on screen)"
		                    : "kescm: marks will print at normal opacity (and stay visible on screen)");
	else
		report.Append("kescm: marks are screen-only (won't print)");
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ArmMousePeek
   ミドルボタン peek を arm する。parent=新ドキュメント(targetDB)、引数 sourceDoc=比較相手の旧ドキュメント。
   以後、ミドルボタンを押している間だけマウス下スプレッドの旧版が不透明べた載せされ、離すと隠れる。
   キャッシュは直近に覗いた1スプレッド分だけ保持(同じスプレッドの再 peek は即時)。実表示は watcher が行う。
*/
ErrorCode KESCMScriptProvider::ArmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef targetDocRef = ::GetUIDRef(parent);
	IDataBase* targetDB = targetDocRef.GetDataBase();
	if (targetDB == nil)
		return kFailure;

	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMSourceDoc, arg) != kSuccess)
		return kFailure;
	InterfacePtr<IScript> srcScript(arg.QueryObject());
	if (srcScript == nil)
		return kFailure;
	IDataBase* sourceDB = ::GetUIDRef(srcScript).GetDataBase();
	if (sourceDB == nil)
		return kFailure;

	KESCMDoArmMousePeek(targetDB, sourceDB);

	PMString report("kescm: mouse peek armed (Shift+middle-click and hold over a spread)");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* DisarmMousePeek
   ミドルボタン peek を解除する。表示を OFF にし、旧版キャッシュも解放(メモリ開放)。Page/Document 可。
*/
ErrorCode KESCMScriptProvider::DisarmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	KESCMDoDisarmMousePeek(db);

	PMString report("kescm: mouse peek disarmed");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* Toast
   画面(=可視領域)の中央に message を少し表示し、約 2.5 秒後に自動で消す。Page でも Document でも可。
   画面表示のみ・非永続。実描画は HandleDrawEvent、自動消去は KESCMToastIdleTask が担う。
*/
ErrorCode KESCMScriptProvider::Toast(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();

	ScriptData arg;
	PMString msg;
	if (data->ExtractRequestData(p_KESCMToastMsg, arg) == kSuccess)
		arg.GetPMString(msg);
	msg.SetTranslatable(kFalse);

	KESCMShowToast(db, msg, kKESCMToastDefaultMs);

	PMString report("kescm: toast shown");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}
