//========================================================================================
//
//  $File: $
//
//  Owner:
//
//  $Author: $
//
//  $DateTime: $
//
//  $Revision: $
//
//  $Change: $
//
//  Copyright 1997-2012 Adobe Systems Incorporated. All rights reserved.
//
//  NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance
//  with the terms of the Adobe license agreement accompanying it.  If you have received
//  this file from a source other than Adobe, then your use, modification, or
//  distribution of it requires the prior written permission of Adobe.
//
//========================================================================================


#ifndef __KESCMScriptingDefs_h__
#define __KESCMScriptingDefs_h__

#define kCPrefs_CLSID { 0x8d448fe0, 0x8194, 0x11d3, { 0xa6, 0x53, 0x0, 0xe0, 0x98, 0x71, 0xa, 0x6f } }
DECLARE_GUID(CPrefs_CLSID, kCPrefs_CLSID);

// 4-char ScriptID codes follow the KES numbering rule: [type][K=Kohaku][G=KESCM/ChangeMarker][member].
// Registered in docs/ai-notes/kes-scriptid-registry.md (checked for conflicts vs core / other KES plug-ins).

// Method IDs
enum KESCMScriptEvents
{
	e_KESCMMarkChanges    = 'eKGm',	// e=method K=Kohaku G=KESCM m=mark  : Page.kescmMarkChanges(sourcePage)
	e_KESCMClearMarks     = 'eKGc',	// e=method K=Kohaku G=KESCM c=clear : Page.kescmClearMarks()
	e_KESCMMarkChangesDoc = 'eKGD',	// e=method K=Kohaku G=KESCM D=Doc   : Document.kescmMarkChangesDoc(sourceDoc)
	e_KESCMShowPageX      = 'eKGx'	// e=method K=Kohaku G=KESCM x=X-mark: Page/Document.kescmShowPageX(flag)
};

// Property / Parameter IDs
enum KESCMScriptProperties
{
	p_KESCMSourcePage   = 'pKGs',	// p=param K=Kohaku G=KESCM s=source page : compare-against (old) page, may be in another document
	p_KESCMSourceDoc    = 'pKGD',	// p=param K=Kohaku G=KESCM D=source Doc  : compare-against (old) document
	p_KESCMShowPageXFlag = 'pKGx'	// p=param K=Kohaku G=KESCM x=X-mark flag : kTrue to show the page X, kFalse to hide
};


#endif // __KESCMScriptingDefs_h__
