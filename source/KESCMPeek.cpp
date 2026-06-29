//========================================================================================
//
//  KESCMPeek.cpp
//
//  ミドルボタン peek の実装(KESCMScriptProvider.cpp から分離)。peek 状態、ミドルボタン＋修飾キーを
//  スヌープする IEventWatcher、起動サービス、KESCMCore.h で宣言した arm/disarm/状態アクセサの入口を持つ。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル:
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IGeometry.h"
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "ILayoutUIUtils.h"
#include "IEventUtils.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IShape.h"
#include "ISession.h"

// イベント監視 / ツール / 起動:
#include "IEventWatcher.h"
#include "IEvent.h"
#include "IEventDispatcher.h"
#include "IStartupShutdownService.h"
#include "CreateObject.h"
#include "CPMUnknown.h"
#include "IToolBoxUtils.h"
#include "ITool.h"
#include "LayoutUIID.h"
#include "DocumentContextID.h"

// ジオメトリ / ビュー:
#include "IControlView.h"
#include "IPanorama.h"
#include "IWidgetParent.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMReal.h"
#include "TransformUtils.h"

#include <vector>
#include <map>

// プロジェクト内インクルード:
#include "KESCMID.h"
#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // エンジンの共有 static ＋ KESCMQueryPanorama
#include "KESCMToast.h"              // KESCMShowToast / ShowHoldToast / HideHoldToast
#include "KESCMColorSampler.h"       // KESCMSampleCmykUnderMouse
#include "KESCMCore.h"               // KESCMCollectPageUIDs ＋ arm/disarm/状態 宣言
#include "KESCMPeek.h"

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
static bool16 sSingleShowing     = kFalse;	// 修飾なしミドル押下中(=全マークを約25%で一時表示中)か。離すと隠す＋基準opacityへ
static bool16 sFaintShowing      = kFalse;	// Shift+Alt+ミドル押下中(=全マークを通常=不透明で一時表示中)か。離すと隠す＋基準opacityへ
static bool16 sColorHoldShowing  = kFalse;	// Shift＋Ctrl＋Alt＋ミドル押下中(=色サンプルのトースト表示中)か。離すと消す
// ミドル押下中だけハンドツール(掴んで移動)に一時切替。離すと元のツールへ戻す。
static ITool*  sSavedTool  = nil;	// 切替前のツール(ref を保持。Restore で Release)
static bool16  sHandActive = kFalse;	// ハンドツールに一時切替中か

// 画面マークの「基準」不透明度(=ミドル/Shift+Alt のどちらも押していない常時表示時の値)。
//   印刷マークON＋25%(faint)選択中は 0.25(画面も印刷と同じ薄さ)、それ以外は 1.0(不透明)。
//   ミドル/Shift+Alt を離したら sMarkScreenOpacity をこの値へ戻す。
PMReal KESCMBaseScreenOpacity()
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

	PMReal mx = 0.0, my = 0.0;
	if (!KESCMQueryMouseContentPoint(view, mx, my))
		return kKESCMPeekNoView;

	// マウス下のスプレッド/ページを特定(平坦通し番号も取得)。共有ヘルパ KESCMFindPageUnderMouse に集約。
	KESCMPageHit hit;
	if (!KESCMFindPageUnderMouse(targetDB, mx, my, hit))
		return kKESCMPeekNoSpread;

	// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。新→旧の通し番号対応に使う。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);

	const int32 s           = hit.spreadIndex;
	const int32 np          = hit.numPages;
	const int32 globalIndex = hit.globalPageBase;
	InterfacePtr<ISpread> spread(targetDB, hit.spreadUID, UseDefaultIID());
	if (spread == nil)
		return kKESCMPeekNoSpread;

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
	KESCMDrawEventHandler::sPeekOpacity = opacity;	// 旧版の不透明度(描画時に旧版べた載せの描画ブロックが参照)
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 覗き中は枠等を出さない(旧版だけ)
	KESCMEnterHandTool();	// 旧状態で掴んで移動
	KESCMPeekShowUnderMouse(sPeekTargetDB, sPeekSourceDB, nil, nil);
}




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
	PMReal mx = 0.0, my = 0.0;
	if (!KESCMQueryMouseContentPoint(view, mx, my))
		return kFalse;

	// マウス下のスプレッド/ページを特定(平坦通し番号も取得)。共有ヘルパ KESCMFindPageUnderMouse に集約。
	KESCMPageHit hit;
	if (!KESCMFindPageUnderMouse(targetDB, mx, my, hit))
		return kFalse;

	// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);

	// マークの所属ドキュメントを合わせる(別 doc にマークがあった場合のみ総入れ替え=通常は一致で何もしない)。
	if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sDB != targetDB)
		KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	InterfacePtr<ISpread> spread(targetDB, hit.spreadUID, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	const int32 np = hit.numPages;

	// このスプレッドの各ページを再比較して枠を更新。新→旧は globalPageBase で対応。
	int32 changedCount = 0;
	for (int32 p = 0; p < np; ++p)
	{
		const int32 gi = hit.globalPageBase + p;
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

	if (outSpread)  *outSpread = hit.spreadIndex;
	if (outChanged) *outChanged = changedCount;
	return kTrue;
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
			KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();	// 不透明度も基準値(印刷設定に応じる)へ戻す。他の解除箇所と一貫
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
				KESCMDrawEventHandler::sMarkScreenOpacity = 1.0;				// 通常=不透明(印刷25%中でも不透明で確認できる)
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
			// シングル動作(修飾キーなしミドル押下中): 全マーク(リング＋変更数)を「約25%で薄表示」にして、
			// ハンドツールに切替えて「枠を見ながら掴んで移動」できるようにする。離す(kMButtonUp)と非表示＋不透明度を戻す。
			// マークが何も無い(エントリ無し)時は反応しない=素のミドルクリックを邪魔しない。
			const bool16 haveContent = !KESCMDrawEventHandler::sEntries.empty();
			if (haveContent)
			{
				sSingleShowing = kTrue;
				KESCMDrawEventHandler::sMarkScreenOpacity = kKESCMFaintOpacity;	// ミドルのみ=25%薄表示
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
			// ミドルのみの押下を離した → 25%表示を解除し、不透明度を基準値(印刷設定に応じた値)へ戻す＋非表示へ。
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
	// 保持していたマーク/旧版画像バッファを解放(終了時もきれいに片付ける)。
	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::DropAllOrig();
	KESCMShutdownToast();	// トーストタイマ本体も解放(セッション中1個を保持していた)
}

//========================================================================================
// arm / disarm / 状態アクセサ(KESCMCore.h で宣言)。上の file-local な peek 状態を共有させるため、
// ここに置いている。
//========================================================================================

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

// パネルの状態アクセサ(arm 済み peek =「開始済み」状態を反映する)。
bool16     KESCMIsArmed()        { return sPeekArmed; }
IDataBase* KESCMArmedTargetDB()  { return sPeekTargetDB; }
IDataBase* KESCMArmedSourceDB()  { return sPeekSourceDB; }
