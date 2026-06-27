//========================================================================================
//
//  KESCMPanelObserver.cpp
//
//  IObserver for the ChangeMarker control panel. It mirrors work/changemarker-panel.jsx:
//    - Start button : resolve Target (= active doc) + Source (= the other open doc),
//                     mark all changed pages, and arm the middle-button peek.
//    - Clear button : clear the overlay and disarm the peek.
//    - Print check  : toggle whether the marks print (and stay on screen) via SetPrintMarks.
//    - 25% / Normal : print opacity, only meaningful while Print is on.
//  The Target:/Source: labels and the ON/OFF icon reflect the armed ("started") state, which is
//  shared application-wide (KESCMIsArmed/…), so reopening the panel keeps showing the right state.
//
//  Modeled on the SnippetRunner panel observer (SnipRunPanelWidgetObserver.cpp).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "ISubject.h"
#include "ITextControlData.h"
#include "ITriStateControlData.h"
#include "IBooleanControlData.h"
#include "IApplication.h"			// GetExecutionContextSession, QueryApplication
#include "IActiveContext.h"
#include "IDocument.h"
#include "IDocumentList.h"

// General includes:
#include "CObserver.h"
#include "widgetid.h"				// kTrueStateMessage / kFalseStateMessage
#include "PersistUtils.h"			// ::GetUIDRef

// Project includes:
#include "KESCMID.h"
#include "KESCMCore.h"

/** Observes the ChangeMarker panel's widgets and drives the shared overlay operations. */
class KESCMPanelObserver : public CObserver
{
public:
	KESCMPanelObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KESCMPanelObserver() {}

	virtual void AutoAttach();
	virtual void AutoDetach();
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);

private:
	void AttachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid);
	void DetachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid);
	IControlView* FindW(const WidgetID& wid);

	void DoStart();
	void DoClear();
	void ApplyPrintMarks();
	void UpdateOpacityEnabled();
	void UpdateInfoDisplay();
	void SetStatus(const PMString& s);

	bool16 IsSelected(const WidgetID& wid);
	void   SetSelected(const WidgetID& wid, bool16 sel);	// select/deselect a checkbox or radio (no notify)
};

CREATE_PMINTERFACE(KESCMPanelObserver, kKESCMPanelObserverImpl)

//----------------------------------------------------------------------------------------
// Local helpers
//----------------------------------------------------------------------------------------

// The active (front) document = the comparison Target.
static IDocument* KESCMActiveDoc()
{
	IActiveContext* ac = GetExecutionContextSession() ? GetExecutionContextSession()->GetActiveContext() : nil;
	return ac ? ac->GetContextDocument() : nil;
}

// The first open document that is not 'target' = the comparison Source (the old version).
static IDocument* KESCMFirstOtherDoc(IDocument* target)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return nil;
	const int32 n = docList->GetDocCount();
	for (int32 i = 0; i < n; ++i)
	{
		IDocument* d = docList->GetNthDoc(i);
		if (d != nil && d != target)
			return d;
	}
	return nil;
}

// Display name for the document that owns 'db' (shortened to fit the label like the JSX panel).
static PMString KESCMDocNameFromDB(IDataBase* db)
{
	PMString name;
	name.SetTranslatable(kFalse);
	if (db == nil)
		return name;

	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return name;

	IDocument* d = docList->FindDocByDataBase(db);
	if (d != nil)
		d->GetName(name);

	// 長すぎる名前は末尾を残して切り詰め(JSX の shortName 相当)。
	if (name.CharCount() > 26)
	{
		PMString* tail = name.Substring(name.CharCount() - 23, 23);
		PMString shortened("...");
		shortened.SetTranslatable(kFalse);
		if (tail != nil)
		{
			shortened.Append(*tail);
			delete tail;
		}
		name = shortened;
	}
	return name;
}

//----------------------------------------------------------------------------------------
// Attach / detach
//----------------------------------------------------------------------------------------

void KESCMPanelObserver::AutoAttach()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->AttachWidget(pcd, kKESCMStartButtonWidgetID,        IBooleanControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMClearButtonWidgetID,        IBooleanControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMPrintCheckWidgetID,         ITriStateControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMOpacity25RadioWidgetID,     ITriStateControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMOpacityNormalRadioWidgetID, ITriStateControlData::kDefaultIID);

	// 初期状態を明示する。RadioButtonWidget は .fr で初期選択状態を持たず、放置すると両方が
	// 選択状態で表示される。印刷チェックは OFF、ラジオは 25% のみ選択(通常は解除)にそろえる。
	this->SetSelected(kKESCMPrintCheckWidgetID,         kFalse);
	this->SetSelected(kKESCMOpacityNormalRadioWidgetID, kFalse);
	this->SetSelected(kKESCMOpacity25RadioWidgetID,     kTrue);

	this->UpdateOpacityEnabled();	// 初期=印刷OFF なのでラジオは無効
	this->UpdateInfoDisplay();		// 開始済みなら Target/Source 名と ON アイコン、未開始なら名前なし+OFF
}

void KESCMPanelObserver::AutoDetach()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->DetachWidget(pcd, kKESCMStartButtonWidgetID,        IBooleanControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMClearButtonWidgetID,        IBooleanControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMPrintCheckWidgetID,         ITriStateControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMOpacity25RadioWidgetID,     ITriStateControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMOpacityNormalRadioWidgetID, ITriStateControlData::kDefaultIID);
}

void KESCMPanelObserver::AttachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid)
{
	IControlView* cv = pcd->FindWidget(wid);
	if (cv == nil)
		return;
	InterfacePtr<ISubject> subject(cv, UseDefaultIID());
	if (subject != nil)
		subject->AttachObserver(this, iid);
}

void KESCMPanelObserver::DetachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid)
{
	IControlView* cv = pcd->FindWidget(wid);
	if (cv == nil)
		return;
	InterfacePtr<ISubject> subject(cv, UseDefaultIID());
	if (subject != nil)
		subject->DetachObserver(this, iid);
}

IControlView* KESCMPanelObserver::FindW(const WidgetID& wid)
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	return (pcd != nil) ? pcd->FindWidget(wid) : nil;
}

//----------------------------------------------------------------------------------------
// Update dispatch
//----------------------------------------------------------------------------------------

void KESCMPanelObserver::Update(const ClassID& theChange, ISubject* theSubject, const PMIID& /*protocol*/, void* /*changedBy*/)
{
	InterfacePtr<IControlView> cv(theSubject, UseDefaultIID());
	if (cv == nil)
		return;

	const WidgetID wid = cv->GetWidgetID();

	if (theChange == kTrueStateMessage)
	{
		switch (wid.Get())
		{
			case kKESCMStartButtonWidgetID:        this->DoStart(); break;
			case kKESCMClearButtonWidgetID:        this->DoClear(); break;
			case kKESCMPrintCheckWidgetID:         this->ApplyPrintMarks(); this->UpdateOpacityEnabled(); break;
			// 通常/25% の切替: 印刷ON中のみ反映(OFF中は次に印刷ONにしたとき反映される)。
			case kKESCMOpacity25RadioWidgetID:
				this->SetSelected(kKESCMOpacityNormalRadioWidgetID, kFalse);	// 相互排他(手動)
				if (this->IsSelected(kKESCMPrintCheckWidgetID))
					this->ApplyPrintMarks();
				break;
			case kKESCMOpacityNormalRadioWidgetID:
				this->SetSelected(kKESCMOpacity25RadioWidgetID, kFalse);		// 相互排他(手動)
				if (this->IsSelected(kKESCMPrintCheckWidgetID))
					this->ApplyPrintMarks();
				break;
			default: break;
		}
	}
	else if (theChange == kFalseStateMessage)
	{
		if (wid == kKESCMPrintCheckWidgetID)
		{
			this->ApplyPrintMarks();
			this->UpdateOpacityEnabled();
		}
	}
}

//----------------------------------------------------------------------------------------
// Actions
//----------------------------------------------------------------------------------------

void KESCMPanelObserver::DoStart()
{
	IDocument* target = KESCMActiveDoc();
	if (target == nil)
	{
		PMString s("Target and source documents not found."); s.SetTranslatable(kFalse);
		this->SetStatus(s);
		return;
	}
	IDocument* source = KESCMFirstOtherDoc(target);
	if (source == nil)
	{
		PMString s("Target or source documents not found."); s.SetTranslatable(kFalse);
		this->SetStatus(s);
		return;
	}

	IDataBase* targetDB = ::GetUIDRef(target).GetDataBase();
	IDataBase* sourceDB = ::GetUIDRef(source).GetDataBase();

	PMString report;
	KESCMDoMarkChangesDoc(targetDB, sourceDB, report);
	KESCMDoArmMousePeek(targetDB, sourceDB);
	this->SetStatus(report);
	this->UpdateInfoDisplay();
}

void KESCMPanelObserver::DoClear()
{
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;
	if (db == nil)
	{
		PMString s("Target document not found."); s.SetTranslatable(kFalse);
		this->SetStatus(s);
	}
	else
	{
		KESCMDoClearMarks(db);
		KESCMDoDisarmMousePeek(db);
		PMString s("marks cleared"); s.SetTranslatable(kFalse);
		this->SetStatus(s);
	}
	this->UpdateInfoDisplay();
}

void KESCMPanelObserver::ApplyPrintMarks()
{
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;
	if (db == nil)
	{
		PMString s("Target document not found."); s.SetTranslatable(kFalse);
		this->SetStatus(s);
		return;
	}

	const bool16 flag  = this->IsSelected(kKESCMPrintCheckWidgetID);
	const bool16 faint = this->IsSelected(kKESCMOpacity25RadioWidgetID);
	KESCMDoSetPrintMarks(flag, faint, db);

	PMString report;
	report.SetTranslatable(kFalse);
	if (flag)
		report.Append(faint ? "kescm: marks will print at ~25% (and stay visible on screen)"
		                    : "kescm: marks will print at normal opacity (and stay visible on screen)");
	else
		report.Append("kescm: marks are screen-only (won't print)");
	this->SetStatus(report);
}

//----------------------------------------------------------------------------------------
// Display helpers
//----------------------------------------------------------------------------------------

void KESCMPanelObserver::UpdateOpacityEnabled()
{
	const bool16 enable = this->IsSelected(kKESCMPrintCheckWidgetID);
	IControlView* r25 = this->FindW(kKESCMOpacity25RadioWidgetID);
	IControlView* rN  = this->FindW(kKESCMOpacityNormalRadioWidgetID);
	if (r25 != nil) r25->Enable(enable);
	if (rN  != nil) rN->Enable(enable);
}

void KESCMPanelObserver::UpdateInfoDisplay()
{
	const bool16 started = KESCMIsArmed() && (KESCMArmedTargetDB() != nil);

	// Target:/Source: ラベルは常時。名前は開始中のみ表示(英語固定: 現状英語のまま)。
	PMString target("Target:"); target.SetTranslatable(kFalse);
	if (started)
	{
		target.Append(" ");
		target.Append(KESCMDocNameFromDB(KESCMArmedTargetDB()));
	}
	PMString source("Source:"); source.SetTranslatable(kFalse);
	if (started && KESCMArmedSourceDB() != nil)
	{
		source.Append(" ");
		source.Append(KESCMDocNameFromDB(KESCMArmedSourceDB()));
	}

	IControlView* tView = this->FindW(kKESCMTargetTextWidgetID);
	if (tView != nil)
	{
		InterfacePtr<ITextControlData> tcd(tView, UseDefaultIID());
		if (tcd != nil) tcd->SetString(target);
	}
	IControlView* sView = this->FindW(kKESCMSourceTextWidgetID);
	if (sView != nil)
	{
		InterfacePtr<ITextControlData> tcd(sView, UseDefaultIID());
		if (tcd != nil) tcd->SetString(source);
	}

	// アイコン: 開始中=ON / 未開始=OFF を出し分ける(2枚を重ねて可視を切替)。
	IControlView* onView  = this->FindW(kKESCMIconOnWidgetID);
	IControlView* offView = this->FindW(kKESCMIconOffWidgetID);
	if (onView  != nil) onView->ShowView(started ? kTrue : kFalse);
	if (offView != nil) offView->ShowView(started ? kFalse : kTrue);
}

void KESCMPanelObserver::SetStatus(const PMString& s)
{
	IControlView* cv = this->FindW(kKESCMStatusTextWidgetID);
	if (cv == nil)
		return;
	InterfacePtr<ITextControlData> tcd(cv, UseDefaultIID());
	if (tcd != nil)
		tcd->SetString(s);
}

bool16 KESCMPanelObserver::IsSelected(const WidgetID& wid)
{
	IControlView* cv = this->FindW(wid);
	if (cv == nil)
		return kFalse;
	InterfacePtr<ITriStateControlData> ts(cv, UseDefaultIID());
	return (ts != nil) ? ts->IsSelected() : kFalse;
}

void KESCMPanelObserver::SetSelected(const WidgetID& wid, bool16 sel)
{
	IControlView* cv = this->FindW(wid);
	if (cv == nil)
		return;
	InterfacePtr<ITriStateControlData> ts(cv, UseDefaultIID());
	if (ts == nil)
		return;
	if (sel)
		ts->Select(kTrue /*invalidate*/, kFalse /*don't notify*/);
	else
		ts->Deselect(kTrue /*invalidate*/, kFalse /*don't notify*/);
}

// End, KESCMPanelObserver.cpp.
