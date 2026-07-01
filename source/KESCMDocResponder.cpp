//========================================================================================
//
//  KESCMDocResponder.cpp
//
//  Document-close responder. Registered (via KESCMDocServiceProvider) for the
//  kAfterCloseDocSignalResponderService signal, which fires only when a document close
//  has actually completed (a close cancelled from the save-changes dialog does NOT fire
//  it). On that signal we hand off to KESCMHandleDocsClosed(), which sweeps every tracked
//  database (marks / original images / toast / peek arm) against the live document list
//  and cleans up whichever ones vanished, then refreshes the panel ON->OFF.
//
//  We deliberately use AfterClose rather than BeforeClose: BeforeClose can be followed by
//  a user cancel (which would wrongly drop the marks), and AfterClose gives no usable
//  document UIDRef anyway, so identifying "which db closed" is done by a liveness sweep
//  (KESCMHandleDocsClosed) rather than from the signal data.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "isignalmgr.h"

// Implementation includes:
#include "CResponder.h"
#include "KESCMCore.h"		// KESCMHandleDocsClosed
#include "KESCMID.h"

/** KESCMDocResponder
	Responds to the "after close document" signal by cleaning up any of KESCM's
	tracked, now-closed documents. Implements IResponder via the CResponder partial
	implementation.
*/
class KESCMDocResponder : public CResponder
{
public:
	KESCMDocResponder(IPMUnknown* boss) : CResponder(boss) {}

	virtual void Respond(ISignalMgr* signalMgr);
};

CREATE_PMINTERFACE(KESCMDocResponder, kKESCMDocResponderImpl)

void KESCMDocResponder::Respond(ISignalMgr* /*signalMgr*/)
{
	// The service provider only registers kAfterCloseDocSignalResponderService, so any
	// call here means a document just finished closing. We do not need the signal's
	// document UIDRef (it is invalid after close); KESCMHandleDocsClosed() figures out
	// which tracked databases are gone by checking them against the live document list.
	KESCMHandleDocsClosed();
}

// End, KESCMDocResponder.cpp.
