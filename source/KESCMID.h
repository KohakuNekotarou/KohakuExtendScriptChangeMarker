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


#ifndef __KESCMID_h__
#define __KESCMID_h__

#include "SDKDef.h"

// Company:
#define kKESCMCompanyKey	kSDKDefPlugInCompanyKey		// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_".
#define kKESCMCompanyValue	kSDKDefPlugInCompanyValue	// Company name displayed externally.

// Plug-in:
#define kKESCMPluginName	"KohakuExtendScriptChangeMarker"			// Name of this plug-in.
#define kKESCMPrefixNumber	0x205515 		// Unique prefix number for this plug-in(*Must* be obtained from Adobe Developer Support).
#define kKESCMVersion		kSDKDefPluginVersionString						// Version of this plug-in (for the About Box).
#define kKESCMAuthor		""					// Author of this plug-in (for the About Box).

// Plug-in Prefix: (please change kKESCMPrefixNumber above to modify the prefix.)
#define kKESCMPrefix		RezLong(kKESCMPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKESCMStringPrefix	SDK_DEF_STRINGIZE(kKESCMPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKESCMMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKESCMMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKESCMPluginID, kKESCMPrefix + 0)

// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKESCMScriptProviderBoss, kKESCMPrefix + 3)
DECLARE_PMID(kClassIDSpace, kKESCMDrawEventServiceBoss, kKESCMPrefix + 4)
DECLARE_PMID(kClassIDSpace, kKESCMPeekWatcherBoss, kKESCMPrefix + 5)	// IEventWatcher: middle-button peek (snoop kMButtonDn/Up)
DECLARE_PMID(kClassIDSpace, kKESCMPeekStartupBoss, kKESCMPrefix + 6)	// IStartupShutdown: starts the peek watcher at app launch
DECLARE_PMID(kClassIDSpace, kKESCMToastIdleTaskBoss, kKESCMPrefix + 7)	// IIdleTask: auto-dismiss the on-canvas toast message
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 6)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 8)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 9)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 10)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 11)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 12)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 13)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 14)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 15)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 16)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 17)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 18)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 19)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 20)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 21)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 22)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 23)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 24)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 25)


// InterfaceIDs:
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 0)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 1)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 2)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 3)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 4)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 5)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 6)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 7)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 8)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 9)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 10)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 11)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 12)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 13)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 14)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 15)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 16)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 17)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 18)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 19)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 20)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 21)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 22)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 23)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 24)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 25)


// ImplementationIDs:
DECLARE_PMID(kImplementationIDSpace, kKESCMScriptProviderImpl, kKESCMPrefix + 0)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventSrvcImpl, kKESCMPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventHandlerImpl, kKESCMPrefix + 2)
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekWatcherImpl, kKESCMPrefix + 3)	// IEventWatcher impl (middle-button peek)
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekStartupImpl, kKESCMPrefix + 4)	// IStartupShutdown impl (starts the peek watcher)
DECLARE_PMID(kImplementationIDSpace, kKESCMToastIdleTaskImpl, kKESCMPrefix + 5)	// IIdleTask impl (auto-dismiss toast)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 6)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 7)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 8)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 9)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 10)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 11)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 12)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 13)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 14)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 15)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 16)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 17)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 18)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 19)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 20)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 21)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 22)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 23)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 24)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 25)


// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKESCMAboutActionID, kKESCMPrefix + 0)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 5)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 6)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 7)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 8)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 9)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 10)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 11)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 12)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 13)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 14)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 15)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 16)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 17)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 18)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 19)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 20)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 21)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 22)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 23)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 24)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 25)


// WidgetIDs:
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 2)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 3)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 4)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 5)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 6)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 7)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 8)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 9)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 10)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 11)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 12)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 13)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 14)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 15)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 16)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 17)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 18)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 19)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 20)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 21)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 22)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 24)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 25)

//Script Element IDs
DECLARE_PMID(kScriptInfoIDSpace, kKESCMMarkChangesMethodScriptElement, kKESCMPrefix + 1)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMClearMarksMethodScriptElement, kKESCMPrefix + 2)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMMarkChangesDocMethodScriptElement, kKESCMPrefix + 3)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMShowPageXMethodScriptElement, kKESCMPrefix + 4)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMShowOversetMethodScriptElement, kKESCMPrefix + 5)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMShowOriginalMethodScriptElement, kKESCMPrefix + 6)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMHideOriginalMethodScriptElement, kKESCMPrefix + 7)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMShowOriginalUnderMouseMethodScriptElement, kKESCMPrefix + 8)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMArmMousePeekMethodScriptElement, kKESCMPrefix + 9)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMDisarmMousePeekMethodScriptElement, kKESCMPrefix + 10)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMToastMethodScriptElement, kKESCMPrefix + 11)

// "About Plug-ins" sub-menu:
#define kKESCMAboutMenuKey			kKESCMStringPrefix "kKESCMAboutMenuKey"
#define kKESCMAboutMenuPath		kSDKDefStandardAboutMenuPath kKESCMCompanyKey

// "Plug-ins" sub-menu:
#define kKESCMPluginsMenuKey 		kKESCMStringPrefix "kKESCMPluginsMenuKey"
#define kKESCMPluginsMenuPath		kSDKDefPlugInsStandardMenuPath kKESCMCompanyKey kSDKDefDelimitMenuPath kKESCMPluginsMenuKey

// Menu item keys:

// Other StringKeys:
#define kKESCMAboutBoxStringKey	kKESCMStringPrefix "kKESCMAboutBoxStringKey"
#define kKESCMTargetMenuPath kKESCMPluginsMenuPath

// Menu item positions:


// Initial data format version numbers
#define kKESCMFirstMajorFormatNumber  RezLong(1)
#define kKESCMFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKESCMCurrentMajorFormatNumber kKESCMFirstMajorFormatNumber
#define kKESCMCurrentMinorFormatNumber kKESCMFirstMinorFormatNumber

#endif // __KESCMID_h__
