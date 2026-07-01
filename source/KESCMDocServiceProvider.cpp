//========================================================================================
//
//  KESCMDocServiceProvider.cpp
//
//  Registers KESCMDocResponder as a responder for the "after close document" signal.
//  One instance per session. See KESCMDocResponder.cpp for what the responder does.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:

// Implementation includes:
#include "CServiceProvider.h"
#include "K2Vector.h"
#include "DocumentID.h"		// kAfterCloseDocSignalResponderService
#include "KESCMID.h"

/** KESCMDocServiceProvider
	Advertises the single document signal that KESCMDocResponder handles. Implements
	IK2ServiceProvider via the CServiceProvider partial implementation.
*/
class KESCMDocServiceProvider : public CServiceProvider
{
public:
	KESCMDocServiceProvider(IPMUnknown* boss) : CServiceProvider(boss) {}
	virtual ~KESCMDocServiceProvider() {}

	virtual void GetName(PMString* pName) { pName->SetKey("KESCM Doc Close Responder Service"); }
	virtual ServiceID GetServiceID() { return kAfterCloseDocSignalResponderService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual IK2ServiceProvider::InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	virtual bool16 HasMultipleIDs() const { return kFalse; }
	virtual void GetServiceIDs(K2Vector<ServiceID>& serviceIDs) { serviceIDs.push_back(kAfterCloseDocSignalResponderService); }
};

CREATE_PMINTERFACE(KESCMDocServiceProvider, kKESCMDocServiceProviderImpl)

// End, KESCMDocServiceProvider.cpp.
