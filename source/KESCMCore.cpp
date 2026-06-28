//========================================================================================
//
//  KESCMCore.cpp
//
//  ChangeMarker の共有操作(KESCMCore.h で宣言)。KESCMScriptProvider.cpp から分離したもの。
//  スクリプトメソッドとパネルのウィジェットオブザーバが完全に同じ挙動を駆動できるよう、ただの関数に
//  してある。描画エンジン(KESCMDrawEventHandler)・トーストヘルパ・peek モジュールへ委譲する。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "ILayoutUIUtils.h"
#include "IControlView.h"
#include "IEventUtils.h"
#include "IGeometry.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "PMString.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"
#include "TransformUtils.h"

#include <vector>

#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // 描画エンジン＋共有 static
#include "KESCMToast.h"              // KESCMShowToast
#include "KESCMPeek.h"               // KESCMBaseScreenOpacity
#include "KESCMCore.h"

//========================================================================================
// ヘルパ: ドキュメント内の全ページUIDを、スプレッド順・ページ順で平坦に集める。
//========================================================================================
void KESCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out)
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
// マウス位置・ヒットテストの共有ヘルパ(peek と色サンプラが同じ流儀でカーソル位置を求める)。
//========================================================================================
bool16 KESCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY)
{
	outX = 0.0; outY = 0.0;
	if (view == nil)
		return kFalse;
	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	outX = pt.X();
	outY = pt.Y();
	return kTrue;
}

bool16 KESCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KESCMPageHit& out)
{
	out.spreadIndex = -1; out.spreadUID = kInvalidUID; out.numPages = 0;
	out.globalPageBase = 0; out.hitPageIndex = -1; out.hitPageUID = kInvalidUID;
	if (targetDB == nil)
		return kFalse;
	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kFalse;
	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(targetDB, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();
		// マウスがこのスプレッドのいずれかのページ上にあるか?(最初に当たったページを採用)
		for (int32 p = 0; p < np; ++p)
		{
			const UID pageUID = spread->GetNthPageUID(p);
			InterfacePtr<IGeometry> geo(targetDB, pageUID, UseDefaultIID());
			if (geo == nil)
				continue;
			PMRect bb = geo->GetPathBoundingBox();
			PMMatrix m = ::InnerToPasteboardMatrix(geo);
			m.Transform(&bb);
			PMReal L = bb.Left(), R = bb.Right(), T = bb.Top(), B = bb.Bottom();
			if (L > R) { PMReal t = L; L = R; R = t; }
			if (T > B) { PMReal t = T; T = B; B = t; }
			if (mx >= L && mx <= R && my >= T && my <= B)
			{
				out.spreadIndex    = s;
				out.spreadUID      = spreadUID;
				out.numPages       = np;
				out.globalPageBase = globalIndex;
				out.hitPageIndex   = p;
				out.hitPageUID     = pageUID;
				return kTrue;
			}
		}
		globalIndex += np;
	}
	return kFalse;
}

//========================================================================================
// 共有コア操作(KESCMCore.h で宣言)。
//
// 以前はスクリプトメソッド内にインラインで書かれていた本体。今はパネルのウィジェットオブザーバ
// (KESCMPanelObserver.cpp)が完全に同じ挙動を駆動できるよう、ただの(非 static)関数にしてある。
// この翻訳単位に置くのは意図的で、描画エンジン(KESCMDrawEventHandler)と file-local な peek 状態
// (sPeek*)へ直接アクセスできるようにするため。
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
