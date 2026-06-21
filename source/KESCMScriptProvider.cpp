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
//   Page.kescmMarkChanges(sourcePage):
//     呼び出し元ページ(新)と sourcePage(旧, 別ドキュメント可)を 72dpi ARGB でラスタ化し、
//     画素差分マスク→赤リングを作って、このページにエントリ登録(複数回呼ぶと貯まる)。
//   Document.kescmMarkChangesDoc(sourceDoc):
//     このドキュメントの全ページを sourceDoc の同じ番号のページと突き合わせ、変化ページ全部に
//     リングを付ける(総入れ替え)。ページ数が違う場合は重なる範囲のみ比較。
//   Page/Document.kescmClearMarks():
//     全エントリを破棄して再描画し、オーバーレイを消す。
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
#include "ISpread.h"					// changedBy(スプレッド)→ページ列
#include "ISpreadList.h"				// ドキュメント→全スプレッド列挙(doc単位の一括mark)
#include "IShape.h"						// kPreviewMode / kPrinting フラグ

// 描画 / Draw Event:
#include "IDrwEvtHandler.h"				// IDrwEvtHandler / DrawEventData
#include "IDrwEvtDispatcher.h"			// RegisterHandler / kDEHLowestPriority
#include "DocumentContextID.h"			// kEndSpreadMessage
#include "GraphicsID.h"					// kDrawEventService
#include "GraphicsData.h"				// GraphicsData::GetGraphicsPort / GetView
#include "IGraphicsPort.h"				// image() / translate / scale
#include "AutoGSave.h"					// 描画状態の save/restore
#include "IControlView.h"				// GetContentToWindowMatrix(現ズーム)
#include "ISession.h"					// GetExecutionContextSession(既定フォント取得)
#include "IFontMgr.h"					// 既定フォント取得(framelabel/TEST と同じ)
#include "IPMFont.h"					// IPMFont(selectfont に渡す)
#include "PMMatrix.h"
#include "PMReal.h"						// ::ToInt32 / ::Round
#include "TransformUtils.h"				// ::InnerToSpreadMatrix / ::GetDataBase

// ラスタ化:
#include "SnapshotUtilsEx.h"			// ページをオフスクリーン(ビットマップ)化
#include "AGMImageAccessor.h"			// GetBounds() / GetBaseAddr() / GetAGMColorFamily()
#include "GraphicsExternal.h"			// AGMImageRecord(自前で組んで blit する)

// STL:
#include <map>
#include <vector>
#include <string.h>						// memcpy

// Project includes:
#include "KESCMScriptingDefs.h"
#include "KESCMID.h"


//========================================================================================
// チューニング定数
//========================================================================================
static const PMReal kKESCMRingTargetPx = 7.0;	// リングの目標太さ(画面px)。ズームに依らず一定に見せる
static const uint8 kKESCMRingAlpha = 180;	// リングのアルファ(0..255)。約71%不透明=半透明で下の実ページが透ける(小さいほど透明)
static const int   kKESCMDiffThr = 32;	// 変化判定: 2版の生RGB最大チャンネル差がこれ超で「変化」(AA/微差を除く)
static const int32 kKESCMBaseRadius = 4;	// リング初期半径(画像px)。描画時にズームから再算出するための初期値
static const PMReal kKESCMResolution = 72.0;	// 保存・表示のラスタ解像度(dpi)。リング画像/マスクはこの解像度で持つ(軽い)
// 【取りこぼし防止】比較だけ高解像度で行い、結果を低解像度に圧縮(マックスプーリング)して記憶する。
// 比較解像度 = kKESCMResolution × kKESCMHiResMul。低解像度では平均化で消える細線/微小ズレを満額で拾う。
static const PMReal kKESCMHiResMul    = 2.0;	// 比較解像度の倍率(2=144dpi)。上げるほど検出力↑/一時メモリ↑。300dpi 相当なら≒4.17
static const int32  kKESCMPoolMinCount = 2;	// プーリング: 低解像度1セル内の「高解像度の変化画素数」がこの値以上で変化と判定。
											// 1=最高感度(縁ノイズも拾う)/大きいほどノイズ耐性↑(取りこぼしのリスクも僅かに増)

// リング色: 通常は赤。ただし枠の下の実ページが「赤っぽい」画素の上では、半透明の赤枠が背景に埋もれて
// 見えなくなるため、視認性確保のために青へ切り替える(画素単位)。
static const uint8 kKESCMRingR = 255, kKESCMRingG = 0,   kKESCMRingB = 0;		// 通常(赤)
static const uint8 kKESCMRingAltR = 0,   kKESCMRingAltG = 0,   kKESCMRingAltB = 255;	// 赤背景の上(純粋な青)
static const int   kKESCMRedBgDom = 25;	// 背景を「赤っぽい」と判定する R 優位の閾値(R が G,B の双方より これ以上大きい)。小さいほどピンク/薄い赤も拾う

// 変更数テキストの色。数字=赤、後ろの語=青。setrgbcolor(0..1)。リング(赤/青)とは別系統。
static const PMReal kKESCMCountNumR  = 1.0, kKESCMCountNumG  = 0.0, kKESCMCountNumB  = 0.0;	// 数字 赤
static const PMReal kKESCMMarkR = 0.0, kKESCMMarkG = 0.0, kKESCMMarkB = 1.0;					// 語 青

// ページ全体の対角線×(どのページが変わったか一目で分かる印)。任意で表示/非表示(kescmShowPageX)。
// 2本の線は色を分ける: "/"(右上→左下)=赤、"\"(左上→右下)=青。
static const PMReal kKESCMPageXPx      = 5.0;	// ×線の太さ(画面px)。ズームに依らず一定に見せる
static const PMReal kKESCMPageXOpacity = 0.7;	// ×線の不透明度(0..1)。下のページが透ける半透明
static const PMReal kKESCMSlashR = 1.0, kKESCMSlashG = 0.0, kKESCMSlashB = 0.0;	// "/" 赤
static const PMReal kKESCMBackR  = 0.0, kKESCMBackG  = 0.0, kKESCMBackB  = 1.0;	// "\" 青

// 変更数テキスト(各ページの変更=枠の数を「N chg」で表示)。×トグルと同じ sShowPageX で表示。ズームで大きさ不変。
// 位置=各ページ上端から少し下、ページ「内側」に横中央で置く。数字は細く(fill のみ)、白い縁(ハロー)付き。
// 白縁=本体の前に白を太めのストロークで2度描き(線幅=文字サイズ×kKESCMCountHaloFrac)。ストロークは輪郭中心なので
// 見える白縁は線幅の約半分。本体の赤fillが内側半分を覆い、外側に白リムが残る。
static const PMReal kKESCMCountTextPx   = 20.0;	// 数字の文字サイズ(画面px)
static const PMReal kKESCMCountInsetPx  = 6.0;	// ページ上端からの内側余白(画面px)
static const PMReal kKESCMCountHaloFrac = 0.16;	// 白縁の太さ(文字サイズに対する比)。見える縁はこの約半分。大きいほど太い縁
static const PMReal kKESCMCountBodyFrac = 0.03;	// 数字本体の太さ(赤ストローク幅, 文字サイズ比)。白縁より細くすると白リムが残る。0で最も細い(fillのみ)
// 数字の後ろに続く語(" chg")。小さめ・細め(fill のみ=ストローク無し)で添える。
static const PMReal kKESCMCountWordPx   = 11.0;	// 語の文字サイズ(画面px)。数字より小さめ
// 縦ドリフト抑制: ページ内に置く落とし込み量(画面px固定=inset+numPt)は縮小すると spread 座標で増大し
// ページ内を下へ流れる。ページ高さのこの割合を上限にクランプし、上部に留める。
static const PMReal kKESCMCountTopFracMax = 0.30;	// ベースラインの上端からのオフセット上限(ページ高さ比)
// 変更数カウント用の併合半径(画像px, 72dpi基準)。数える前にマスクをこの半径で膨張し、近接した変化
// (文字グリフの破片や1段落内の行など)を1つの塊にまとめてから連結成分を数える。これで「文字が変わった
// だけで数が膨大」を防ぎ、見た目の赤いリング(塊)の数に近づける。大きいほど大きくまとめる(数が減る)。
static const int32 kKESCMCountMergeRadius = 8;


//========================================================================================
// KESCMOverlayEntry
//   1ページ分のオーバーレイ。★SnapshotUtilsEx / AGMImageAccessor は保持しない。
//   生成時に画素を自前バッファ(buf)へコピーし、buf を指す自前の AGMImageRecord(rec)を組む。
//   こうしてオフスクリーンを完全に切り離す(非推奨 GetAGMImageRecord も不使用)ことで、
//   破棄時クラッシュ(保持した accessor/snapshot の delete 時の二重解放)を回避する。
//   mask は 72dpi の差分マスク。変化のあったページにだけ作られる。
//========================================================================================
struct KESCMOverlayEntry
{
	uint8*         buf;			// 自前の ARGB バッファ(リング画像)。所有
	AGMImageRecord rec;			// buf を指す自前の画像レコード(blit 用)
	uint8*         mask;		// 差分マスク(w*h, 0/1)。所有
	uint8*         bgRed;		// 対象ページが「赤っぽい」画素=1 のマップ(w*h)。リングの青切替に使う。所有(nil可)
	int32          w, h;
	int32          rowBytes;	// buf の行バイト数(= rec.byteWidth)
	int32          bpp;			// バイト/ピクセル
	int32          lastRadius;	// 最後に描いたリング半径(px)。-1=未描画
	int32          changeCount;	// このページの変更(枠)の数=マスクの連結成分数。テキスト表示用

	KESCMOverlayEntry() : buf(nil), mask(nil), bgRed(nil), w(0), h(0), rowBytes(0), bpp(0), lastRadius(-1), changeCount(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KESCMOverlayEntry()
	{
		if (buf)   delete[] buf;
		if (mask)  delete[] mask;
		if (bgRed) delete[] bgRed;
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
	// 変更ページに対角線×を出すか(任意トグル kescmShowPageX)。マークとは独立に保持(DropAllでは変えない)。
	static bool16 sShowPageX;

	// mask を半径 radius で膨張し、buf(ARGB)へリング(膨張 かつ !mask)を描く。
	// 各リング画素の色は、その位置の背景が赤っぽい(bgRed[idx])なら青、そうでなければ赤。
	// 膨張は半径非依存のスライディングウィンドウ(O(W*H))。リング以外の画素は透明(alpha=0)。
	static void BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
		const uint8* mask, const uint8* bgRed, int32 radius);

	// target/source を 72dpi ARGB でラスタ化→差分マスク作成。変化px数>0 のときだけ
	// sEntries[target.UID] にエントリ登録(既存は置換)。changed に「変化したか」を返す。
	static ErrorCode MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed);

	// 全エントリ破棄(kescmClearMarks / 別ドキュメント切替時)。
	static void DropAll()
	{
		for (std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.begin(); it != sEntries.end(); ++it)
			delete it->second;
		sEntries.clear();
		sDB = nil;
	}
};

CREATE_PMINTERFACE(KESCMDrawEventHandler, kKESCMDrawEventHandlerImpl)

std::map<UID, KESCMOverlayEntry*> KESCMDrawEventHandler::sEntries;
IDataBase* KESCMDrawEventHandler::sDB = nil;
bool16 KESCMDrawEventHandler::sShowPageX = kTrue;	// 既定=表示


void KESCMDrawEventHandler::BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
	const uint8* mask, const uint8* bgRed, int32 radius)
{
	if (buf == nil || mask == nil || wt <= 0 || ht <= 0 || bpp < 3)
		return;
	if (radius < 1) radius = 1;
	const int32 colorOff = bpp - 3;
	const size_t N = (size_t)wt * ht;
	uint8* H = new uint8[N];		// 横方向膨張
	uint8* D = new uint8[N];		// 縦方向膨張(=最終 dilate)
	if (H == nil || D == nil) { if (H) delete[] H; if (D) delete[] D; return; }

	// 横方向膨張: 各行をスライディングウィンドウで「窓内に1があるか」(running count)。
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
	// 縦方向膨張: 各列をスライディングウィンドウで。
	for (int32 x = 0; x < wt; ++x)
	{
		int32 cnt = 0, lo = 0, hi = -1;
		for (int32 y = 0; y < ht; ++y)
		{
			const int32 wantHi = (y + radius >= ht) ? ht - 1 : y + radius;
			const int32 wantLo = (y - radius < 0) ? 0 : y - radius;
			while (hi < wantHi) { ++hi; cnt += H[(size_t)hi * wt + x]; }
			while (lo < wantLo) { cnt -= H[(size_t)lo * wt + x]; ++lo; }
			D[(size_t)y * wt + x] = (cnt > 0) ? 1 : 0;
		}
	}
	// リング描画(膨張 かつ 元マスクでない)。それ以外=透明。
	for (int32 y = 0; y < ht; ++y)
	{
		uint8* rowB = buf + (size_t)y * rb;
		for (int32 x = 0; x < wt; ++x)
		{
			uint8* pixT = rowB + (size_t)x * bpp;	// ARGB 先頭=alpha
			uint8* px = pixT + colorOff;
			const size_t idx = (size_t)y * wt + x;
			if (D[idx] && !mask[idx])
			{
				// リング画素。下の実ページが赤っぽければ青、そうでなければ赤(画素単位)。
				const bool useAlt = (bgRed != nil && bgRed[idx]);
				px[0] = useAlt ? kKESCMRingAltR : kKESCMRingR;
				px[1] = useAlt ? kKESCMRingAltG : kKESCMRingG;
				px[2] = useAlt ? kKESCMRingAltB : kKESCMRingB;
				if (bpp >= 4) pixT[0] = kKESCMRingAlpha;	// 半透明
			}
			else { px[0] = 255; px[1] = 255; px[2] = 255; if (bpp >= 4) pixT[0] = 0; }	// 透明
		}
	}
	delete[] H; delete[] D;
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


ErrorCode KESCMDrawEventHandler::MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed)
{
	changed = kFalse;
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// 【低解像度】保存・表示用(リング画像＋赤背景判定＋マスク寸法の基準)。target を kKESCMResolution でラスタ化。
	// addTransparencyAlpha=kTrue で変化なし画素を透明にでき、下の実ページが透ける。
	SnapshotUtilsEx* snapL = new SnapshotUtilsEx(targetRef, 1.0, 1.0, kKESCMResolution, kKESCMResolution, 0.0, SnapshotUtilsEx::kCsRGB, kTrue);
	ErrorCode drewL = snapL->Draw(IShape::kPreviewMode);
	AGMImageAccessor* accL = (drewL == kSuccess) ? snapL->CreateAGMImageAccessor() : nil;

	// 【高解像度】差分検出用。target / source を高dpi(kKESCMResolution×kKESCMHiResMul)でラスタ化。
	// 低解像度では平均化で消える細線/微小ズレを満額の差分画素として拾い、取りこぼしを防ぐ。
	const PMReal hiRes = kKESCMResolution * kKESCMHiResMul;
	SnapshotUtilsEx* snapTH = new SnapshotUtilsEx(targetRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsRGB, kTrue);
	ErrorCode drewTH = snapTH->Draw(IShape::kPreviewMode);
	AGMImageAccessor* accTH = (drewTH == kSuccess) ? snapTH->CreateAGMImageAccessor() : nil;

	SnapshotUtilsEx* snapSH = new SnapshotUtilsEx(sourceRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsRGB, kTrue);
	ErrorCode drewSH = snapSH->Draw(IShape::kPreviewMode);
	AGMImageAccessor* accSH = (drewSH == kSuccess) ? snapSH->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (accL != nil && accTH != nil && accSH != nil)
	{
		// 低解像度(保存・表示)の寸法・バッファ
		Int32Rect bl = accL->GetBounds();
		const int32 wl = bl.right - bl.left, hl = bl.bottom - bl.top;
		const int32 rbL = (int32)accL->GetRowBytes();
		const int32 bppL = (int32)accL->GetBitsPerPixel() / 8;
		uint8* ptL = const_cast<uint8*>(accL->GetBaseAddr());

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

		if (ptL != nil && ptH != nil && psH != nil &&
			wth == wsh && hth == hsh && rbTH == rbSH && rbTH > 0 &&
			bppH >= 3 && bppL >= 3 && wl > 0 && hl > 0)
		{
			const size_t N = (size_t)wl * hl;
			uint8*  M     = new uint8[N];	// 低解像度マスク(保存): プーリング結果
			uint16* cntHi = new uint16[N];	// 低解像度セルごとの「高解像度の変化画素数」(プーリング用一時)
			if (M != nil && cntHi != nil)
			{
				memset(cntHi, 0, N * sizeof(uint16));

				// 【高解像度で比較 → 低解像度セルへ散らす(scatter)】
				// 高解像度の各画素を差分判定(生RGB最大チャンネル差>しきい値)し、変化していたら
				// 対応する低解像度セルのカウンタを増やす。セル写像は寸法比(高/低が整数倍でなくてもよい)。
				const int32 colorOffH = bppH - 3;
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
						const int dR = (px[0] > sx[0]) ? px[0] - sx[0] : sx[0] - px[0];
						const int dG = (px[1] > sx[1]) ? px[1] - sx[1] : sx[1] - px[1];
						const int dB = (px[2] > sx[2]) ? px[2] - sx[2] : sx[2] - px[2];
						int cm = dR; if (dG > cm) cm = dG; if (dB > cm) cm = dB;
						if (cm > kKESCMDiffThr)
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
					// 以降は低解像度(ptL, wl, hl, rbL, bppL)で従来通り。
					// 背景(対象ページ)の「赤っぽい」画素マップを、BuildRing が ptL を上書きする前に作る。
					const int32 colorOffL = bppL - 3;
					uint8* BG = new uint8[N];
					if (BG != nil)
					{
						for (int32 y = 0; y < hl; ++y)
						{
							const uint8* rowT = ptL + (size_t)y * rbL;
							for (int32 x = 0; x < wl; ++x)
							{
								const uint8* px = rowT + (size_t)x * bppL + colorOffL;
								const int r = px[0], g = px[1], b = px[2];
								BG[(size_t)y * wl + x] = (r - g > kKESCMRedBgDom && r - b > kKESCMRedBgDom) ? 1 : 0;
							}
						}
					}

					// 初回リング(基準半径)を低解像度 target バッファへ描く。
					BuildRing(ptL, rbL, bppL, wl, hl, M, BG, kKESCMBaseRadius);

					// ★画素を自前バッファへコピーし、buf を指す自前 AGMImageRecord を組んで切り離す。
					//   SnapshotUtilsEx / accessor は保持しない(下で即破棄)。GetAGMImageRecord も呼ばない
					//   =破棄時クラッシュ(保持 accessor の delete)を根本回避。
					KESCMOverlayEntry* e = new KESCMOverlayEntry();
					e->w = wl;  e->h = hl;  e->rowBytes = rbL;  e->bpp = bppL;
					e->mask = M;  e->bgRed = BG;  e->lastRadius = kKESCMBaseRadius;
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
					e->buf = new uint8[(size_t)rbL * hl];
					memcpy(e->buf, ptL, (size_t)rbL * hl);
					e->rec.bounds.xMin = (int16)bl.left;   e->rec.bounds.yMin = (int16)bl.top;
					e->rec.bounds.xMax = (int16)bl.right;  e->rec.bounds.yMax = (int16)bl.bottom;
					e->rec.baseAddr     = e->buf;
					e->rec.byteWidth    = rbL;
					// ARGB(alpha 先頭)。HasAlpha フラグを立てないと透明画素が不透明白で描かれる。
					// 既定が ARGB 順なので SwapAlpha は不要(RGBA なら | kColorSpaceSwapAlpha)。
					e->rec.colorSpace   = (int16)(kRGBColorSpace | kColorSpaceHasAlpha);
					e->rec.bitsPerPixel = (int16)accL->GetBitsPerPixel();
					e->rec.decodeArray  = nil;
					e->rec.colorTab.numColors = 0;  e->rec.colorTab.theColors = nil;

					// 既存エントリがあれば置換。
					UID key = targetRef.GetUID();
					std::map<UID, KESCMOverlayEntry*>::iterator old = sEntries.find(key);
					if (old != sEntries.end()) { delete old->second; sEntries.erase(old); }
					sEntries[key] = e;

					// 画素コピー済み・mask は entry が所有。スナップショットは下の後始末で即破棄(保持しない)。
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

	// 後始末: 3つのスナップショット/アクセサを破棄(高解像度バッファもここで解放=常駐メモリは低解像度のみ)。
	if (accSH)  delete accSH;
	if (snapSH) delete snapSH;
	if (accTH)  delete accTH;
	if (snapTH) delete snapTH;
	if (accL)   delete accL;
	if (snapL)  delete snapL;
	return status;
}


void KESCMDrawEventHandler::Register(IDrwEvtDispatcher* d)
{
	// スプレッド単位で配られる描画イベント。ポートは spread 座標。
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
}

void KESCMDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
}

bool16 KESCMDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;
	if (ded->flags & IShape::kPrinting)		// 画面のみ(印刷/PDF には描かない)
		return kFalse;
	if (ded->flags & IShape::kPreviewMode)	// スナップショット描画には乗らない(自己参照防止)
		return kFalse;
	if (sEntries.empty() || sDB == nil)
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
	if (db == nil || db != sDB)
		return kFalse;

	// 画面スケール(ズーム)を一度だけ取得。画面描画時のみ非nil。
	PMReal sxr = 0.0;
	IControlView* zview = gd->GetView();
	if (zview != nil)
	{
		PMMatrix toWin = zview->GetContentToWindowMatrix();	// content→window(画面px), 現ズーム
		sxr = toWin.GetXScale(); if (sxr < 0) sxr = -sxr;
	}

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
		if (e->mask != nil && sxr > 0)
		{
			PMReal denom = (pr.Width() / PMReal(iw)) * sxr;		// 画面px / 画像px
			if (denom > PMReal(0.0001))
			{
				int32 R = ::ToInt32(::Round(kKESCMRingTargetPx / denom));
				if (R < 2) R = 2;									// 最小2px
				if (R > 200) R = 200;								// 過大膨張の上限
				R = ((R + 1) / 2) * 2;								// 2px 量子化
				if (R != e->lastRadius)
				{
					BuildRing(e->buf, e->rowBytes, e->bpp, e->w, e->h, e->mask, e->bgRed, R);
					e->lastRadius = R;
				}
			}
		}

		// 【描画順】まず枠の画像(リング)を blit し、その上に × と枠の数を重ねる。
		// translate/scale はこの gsave 内だけ。閉じれば spread 座標に戻るので後続の ×/数 はそのまま描ける。
		{
			AutoGSave ag(gPort);
			gPort->translate(pr.Left(), pr.Top());				// ページ左上へ
			gPort->scale(pr.Width() / iw, pr.Height() / ih);	// リング画像をページ矩形にフィット
			gPort->image(&e->rec, PMMatrix(), 0);				// 自前レコード(buf を指す)を blit
		}

		// 変更ページを一目で示す対角線の×(任意トグル)。ページ矩形の対角に2本、画面で一定の細さ。
		// spread 座標のまま(translate/scale 前)に独自の gsave で描く。リング画像の「上」に乗る。
		if (sShowPageX)
		{
			const PMReal lw = (sxr > 0) ? (kKESCMPageXPx / sxr) : (pr.Width() / PMReal(300));
			AutoGSave agx(gPort);
			gPort->setopacity(kKESCMPageXOpacity, kFalse);
			gPort->setlinewidth(lw);
			// "\"(左上→右下)=青
			gPort->setrgbcolor(kKESCMBackR, kKESCMBackG, kKESCMBackB);
			gPort->newpath();
			gPort->moveto(pr.Left(),  pr.Top());    gPort->lineto(pr.Right(), pr.Bottom());
			gPort->stroke();
			// "/"(右上→左下)=赤
			gPort->setrgbcolor(kKESCMSlashR, kKESCMSlashG, kKESCMSlashB);
			gPort->newpath();
			gPort->moveto(pr.Right(), pr.Top());    gPort->lineto(pr.Left(),  pr.Bottom());
			gPort->stroke();

			// この頁の変更(枠)の数を「N chg」でページ内側・上部・横中央に表示。文字サイズはズーム不変(px/sxr で逆算)。
			// TEST(framelabel)流: session→IFontMgr→既定フォントを selectfont し show で描く(数字=赤fill+stroke=太字、語=青fill のみ)。
			if (e->changeCount > 0)
			{
				InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
				InterfacePtr<IPMFont> theFont(fontMgr ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil);
				if (theFont != nil)
				{
					const PMReal numPt  = (sxr > 0) ? (kKESCMCountTextPx  / sxr) : (pr.Width() / PMReal(24));
					const PMReal wordPt = (sxr > 0) ? (kKESCMCountWordPx  / sxr) : (pr.Width() / PMReal(48));
					const PMReal inset  = (sxr > 0) ? (kKESCMCountInsetPx / sxr) : (pr.Width() / PMReal(80));

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

					// 【縦位置】ページ「内側」上部に置く。上端から inset+numPt(画面px固定)下げた所をベースラインに。
					// 縮小すると画面px固定オフセットは spread 座標で増大しページ内を下へ流れるので、ページ高さ比で
					// クランプして上部に留める。
					PMReal off = inset + numPt;
					const PMReal cap = pr.Height() * kKESCMCountTopFracMax;
					if (off > cap) off = cap;
					const PMReal ty = pr.Top() + off;	// 数字・語の共通ベースライン(下端揃え)

					gPort->setopacity(PMReal(1.0), kFalse);							// 読みやすく不透明
					gPort->selectfont(theFont, numPt);
					const UTF16TextChar* numBuf = numStr.GrabUTF16Buffer(nil);

					// 数字の白い縁: 先に白を太めのストロークで描いてハローを作る。
					gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));		// 白
					gPort->setlinewidth(numPt * kKESCMCountHaloFrac);
					gPort->show(startX, ty, numCh, numBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));

					// 数字の本体: 赤。fill + 細い赤ストロークで程よい太さに(白縁より細い幅なので白リムは残る)。
					gPort->setrgbcolor(kKESCMCountNumR, kKESCMCountNumG, kKESCMCountNumB);
					gPort->setlinewidth(numPt * kKESCMCountBodyFrac);
					gPort->show(startX, ty, numCh, numBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText | IGraphicsPort::kStrokeText));

					// 語: 小さめ・細め(fill のみ=ストローク無し)・青。数字の直後・同じベースライン。
					gPort->setrgbcolor(kKESCMMarkR, kKESCMMarkG, kKESCMMarkB);
					gPort->selectfont(theFont, wordPt);
					gPort->show(startX + numW, ty, wordCh, wordStr.GrabUTF16Buffer(nil),
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
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
// KESCMScriptProvider
//   Page / Document オブジェクトに kescmMarkChanges / kescmMarkChangesDoc / kescmClearMarks を生やす。
//========================================================================================
class KESCMScriptProvider : public CScriptProvider
{
public:
	KESCMScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	~KESCMScriptProvider() {}

	virtual ErrorCode HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent);

private:
	// kescmMarkChanges(sourcePage): このページ1枚にエントリを追加(複数回呼ぶと貯まる)。
	ErrorCode MarkChanges(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmMarkChangesDoc(sourceDoc): このドキュメント全ページを sourceDoc と突き合わせて総入れ替え。
	ErrorCode MarkChangesDoc(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmClearMarks(): オーバーレイを全消去。
	ErrorCode ClearMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmShowPageX(flag): 変更ページの対角線×の表示/非表示を切り替え(任意, 既定=表示)。
	ErrorCode ShowPageX(ScriptID methodID, IScriptRequestData* data, IScript* parent);
};

CREATE_PMINTERFACE(KESCMScriptProvider, kKESCMScriptProviderImpl)


ErrorCode KESCMScriptProvider::HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	ErrorCode status = kFailure;
	switch (methodID.Get())
	{
	case e_KESCMMarkChanges:
		status = MarkChanges(methodID, data, parent);
		break;
	case e_KESCMMarkChangesDoc:
		status = MarkChangesDoc(methodID, data, parent);
		break;
	case e_KESCMClearMarks:
		status = ClearMarks(methodID, data, parent);
		break;
	case e_KESCMShowPageX:
		status = ShowPageX(methodID, data, parent);
		break;
	default:
		status = CScriptProvider::HandleMethod(methodID, data, parent);
	}
	return status;
}


/* MarkChanges
   呼び出し元ページ(parent=新)と引数 sourcePage(旧, 別ドキュメント可)を比較し、変化があれば
   このページ用のエントリを追加する(置換)。複数回呼べば複数ページに貯まる。
*/
ErrorCode KESCMScriptProvider::MarkChanges(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef targetRef = ::GetUIDRef(parent);
	IDataBase* targetDB = targetRef.GetDataBase();
	if (targetDB == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;

	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMSourcePage, arg) != kSuccess)
		return kFailure;
	InterfacePtr<IScript> srcScript(arg.QueryObject());
	if (srcScript == nil)
		return kFailure;
	UIDRef sourceRef = ::GetUIDRef(srcScript);
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// 別ドキュメントを対象にしたら作り直す(UIDはdb内のみ一意)。
	if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sDB != targetDB)
		KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	bool16 changed = kFalse;
	ErrorCode ec = KESCMDrawEventHandler::MakeEntry(targetRef, sourceRef, changed);

	// 即時再描画(ズームを跨がないで反映)。何も付けない=書類が dirty にならない。
	InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);

	PMString report;
	report.SetTranslatable(kFalse);
	if (ec == kSuccess)
		report.Append(changed ? "kescm: marked (changed)" : "kescm: no change on this page");
	else
		report.Append("kescm: failed to rasterize");

	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
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
	UIDRef srcDocRef = ::GetUIDRef(srcScript);
	IDataBase* sourceDB = srcDocRef.GetDataBase();
	if (sourceDB == nil)
		return kFailure;

	// ドキュメント単位の総入れ替え。
	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	// 両ドキュメントのページUIDをドキュメント順に平坦列挙。
	std::vector<UID> tPages, sPages;
	KESCMCollectPageUIDs(targetDB, tPages);
	KESCMCollectPageUIDs(sourceDB, sPages);

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

	KESCMDrawEventHandler::DropAll();

	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	PMString report("marks cleared");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ShowPageX
   変更ページに重ねる対角線×の表示/非表示を切り替える。引数 flag は任意(省略時は表示=kTrue)。
   マーク自体(エントリ)は触らず、トグルだけ変えて再描画する。Page でも Document でも可。
*/
ErrorCode KESCMScriptProvider::ShowPageX(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	bool16 flag = kTrue;	// 省略時は表示
	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMShowPageXFlag, arg) == kSuccess)
		arg.GetBoolean(&flag);

	KESCMDrawEventHandler::sShowPageX = flag;

	// 再描画(エントリはそのまま、×の有無だけ反映)。
	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(flag ? "kescm: page X shown" : "kescm: page X hidden");
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}
