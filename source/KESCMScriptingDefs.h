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
	e_KESCMClearMarks     = 'eKGc',	// e=method K=Kohaku G=KESCM c=clear : Page.kescmClearMarks()
	e_KESCMMarkChangesDoc = 'eKGD',	// e=method K=Kohaku G=KESCM D=Doc   : Document.kescmMarkChangesDoc(sourceDoc)
	// 'eKGm' (e_KESCMMarkChanges) is now free (per-page mark removed; use kescmMarkChangesDoc)
	// 'eKGx' (e_KESCMShowPageX) is now free (the diagonal page X was removed)
	// 'eKGo' (e_KESCMShowOverset) is now free (kescmShowOverset removed; overset always rides the press-reveal)
	// 'eKGr' (e_KESCMShowOriginal) is now free (manual opaque-OLD overlay removed; folded into the middle-button peek)
	// 'eKGh' (e_KESCMHideOriginal) is now free (paired with kescmShowOriginal, removed)
	// 'eKGu' (e_KESCMShowOriginalUnderMouse) is now free (manual under-mouse overlay removed; use the middle-button peek)
	e_KESCMArmMousePeek    = 'eKGa',	// e=method K=Kohaku G=KESCM a=arm    : Document.kescmArmMousePeek(sourceDoc) - arm the middle-button peek (arg reuses p_KESCMSourceDoc)
	e_KESCMDisarmMousePeek = 'eKGd',	// e=method K=Kohaku G=KESCM d=disarm : Document.kescmDisarmMousePeek() - disarm the middle-button peek and free the cache
	e_KESCMToast           = 'eKGt',	// e=method K=Kohaku G=KESCM t=toast  : Page/Document.kescmToast(message) - brief auto-dismissing on-canvas message at screen center
	e_KESCMSetPrintMarks   = 'eKGp'	// e=method K=Kohaku G=KESCM p=print  : Page/Document.kescmSetPrintMarks(flag) - make the change marks appear in print/PDF (and stay on screen while ON)
};

// Property / Parameter IDs
enum KESCMScriptProperties
{
	p_KESCMSourceDoc    = 'pKGD',	// p=param K=Kohaku G=KESCM D=source Doc  : compare-against (old) document
	// 'pKGs' (p_KESCMSourcePage) is now free (per-page kescmMarkChanges/kescmShowOriginal removed)
	// 'pKGx' (p_KESCMShowPageXFlag) is now free (kescmShowPageX removed)
	// 'pKGo' (p_KESCMShowOversetFlag) is now free (kescmShowOverset removed)
	p_KESCMToastMsg      = 'pKGt',	// p=param K=Kohaku G=KESCM t=toast text  : the message string shown briefly at screen center
	p_KESCMPrintMarksFlag = 'pKGp',	// p=param K=Kohaku G=KESCM p=print flag  : kTrue to print the marks (default), kFalse to keep them screen-only
	p_KESCMPrintFaintFlag = 'pKGf'	// p=param K=Kohaku G=KESCM f=faint flag  : kTrue to print the marks at ~30% opacity, kFalse(default) for normal opacity
	// 'pKGn' (p_KESCMSensitivity) is now free — sensitivity selector removed; detection is always CMYK 4ch diff>0
};


#endif // __KESCMScriptingDefs_h__
