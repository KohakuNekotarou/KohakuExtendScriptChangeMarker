//========================================================================================
//
//  KESCMToast.cpp
//
//  一時トーストのタイマと表示/消去の実装(旧 KESCMScriptProvider.cpp から分離)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CPMUnknown.h"
#include "IIdleTask.h"
#include "IIdleTaskMgr.h"
#include "CreateObject.h"
#include "ISession.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "IDocument.h"
#include "IDataBase.h"
#include "ILayoutUtils.h"
#include "PMString.h"

#include "KESCMID.h"
#include "KESCMDrawEventHandler.h"   // sToastMsg / sToastVisible / sToastDB
#include "KESCMToast.h"

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
void KESCMShowToast(IDataBase* db, const PMString& msg, uint32 ms)
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
void KESCMShowHoldToast(IDataBase* db, const PMString& msg)
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
void KESCMHideHoldToast()
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

// セッション終了時にトーストタイマ本体(sToastTask)を解放する。キューに残っていれば外してから Release。
void KESCMShutdownToast()
{
	if (sToastTask == nil)
		return;
	if (sToastQueued)
	{
		InterfacePtr<IIdleTaskMgr> mgr(GetExecutionContextSession(), UseDefaultIID());
		if (mgr != nil)
			mgr->RemoveTask(sToastTask);
		sToastQueued = kFalse;
	}
	sToastTask->Release();
	sToastTask = nil;
}
