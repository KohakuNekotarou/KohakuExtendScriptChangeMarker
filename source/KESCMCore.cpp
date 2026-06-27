//========================================================================================
//
//  KESCMCore.cpp
//
//  Shared ChangeMarker operations (declared in KESCMCore.h), split out of
//  KESCMScriptProvider.cpp. These are plain functions so both the scripting methods and the
//  panel widget observer drive the exact same behavior. They delegate to the overlay engine
//  (KESCMDrawEventHandler), the toast helper, and the peek module.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "ILayoutUIUtils.h"
#include "IControlView.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "PMString.h"

#include <vector>

#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // overlay engine + shared statics
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
