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
	e_KESCMShowPageX      = 'eKGx',	// e=method K=Kohaku G=KESCM x=X-mark: Page/Document.kescmShowPageX(flag)
	e_KESCMShowOverset    = 'eKGo',	// e=method K=Kohaku G=KESCM o=overset: Page/Document.kescmShowOverset(flag) [TEST]
	e_KESCMShowOriginal   = 'eKGr',	// e=method K=Kohaku G=KESCM r=reveal : Page.kescmShowOriginal(sourcePage) - opaque OLD overlay (arg reuses p_KESCMSourcePage)
	e_KESCMHideOriginal   = 'eKGh',	// e=method K=Kohaku G=KESCM h=hide   : Page/Document.kescmHideOriginal() - hide the opaque OLD overlay
	e_KESCMShowOriginalUnderMouse = 'eKGu',	// e=method K=Kohaku G=KESCM u=under-mouse: Document.kescmShowOriginalUnderMouse(sourceDoc) - overlay the spread UNDER THE MOUSE (arg reuses p_KESCMSourceDoc)
	e_KESCMArmMousePeek    = 'eKGa',	// e=method K=Kohaku G=KESCM a=arm    : Document.kescmArmMousePeek(sourceDoc) - arm the middle-button peek (arg reuses p_KESCMSourceDoc)
	e_KESCMDisarmMousePeek = 'eKGd',	// e=method K=Kohaku G=KESCM d=disarm : Document.kescmDisarmMousePeek() - disarm the middle-button peek and free the cache
	e_KESCMToast           = 'eKGt'	// e=method K=Kohaku G=KESCM t=toast  : Page/Document.kescmToast(message) - brief auto-dismissing on-canvas message at screen center
};

// Property / Parameter IDs
enum KESCMScriptProperties
{
	p_KESCMSourcePage   = 'pKGs',	// p=param K=Kohaku G=KESCM s=source page : compare-against (old) page, may be in another document
	p_KESCMSourceDoc    = 'pKGD',	// p=param K=Kohaku G=KESCM D=source Doc  : compare-against (old) document
	p_KESCMShowPageXFlag = 'pKGx',	// p=param K=Kohaku G=KESCM x=X-mark flag : kTrue to show the page X, kFalse to hide
	p_KESCMShowOversetFlag = 'pKGo',	// p=param K=Kohaku G=KESCM o=overset flag: kTrue to show the overset marks, kFalse to hide
	p_KESCMToastMsg      = 'pKGt'	// p=param K=Kohaku G=KESCM t=toast text  : the message string shown briefly at screen center
};


#endif // __KESCMScriptingDefs_h__
