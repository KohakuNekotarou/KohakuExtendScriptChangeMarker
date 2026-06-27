//========================================================================================
//
//  KESCMActionComponent.cpp
//
//  Handles the plug-in's menu actions: the "About Plug-ins" entry and the panel flyout's
//  "About this plug-in" entry. Modeled on the BasicPanel sample (BscPnlActionComponent.cpp).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "CActionComponent.h"
#include "CAlert.h"

// Project includes:
#include "KESCMID.h"

/** Implements IActionComponent for the ChangeMarker plug-in's menu items.
*/
class KESCMActionComponent : public CActionComponent
{
public:
	KESCMActionComponent(IPMUnknown* boss) : CActionComponent(boss) {}

	/** Execute the requested menu action. */
	void DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

private:
	void DoAbout();
};

/* Binds the C++ implementation class onto its ImplementationID. */
CREATE_PMINTERFACE(KESCMActionComponent, kKESCMActionComponentImpl)

/* DoAction */
void KESCMActionComponent::DoAction(IActiveContext* /*ac*/, ActionID actionID, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	switch (actionID.Get())
	{
		case kKESCMAboutActionID:
		case kKESCMPopupAboutThisActionID:
			this->DoAbout();
			break;

		default:
			break;
	}
}

/* DoAbout */
void KESCMActionComponent::DoAbout()
{
	CAlert::ModalAlert
	(
		kKESCMAboutBoxStringKey,	// Alert string
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

// End, KESCMActionComponent.cpp.
