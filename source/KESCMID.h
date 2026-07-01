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
#define kKESCMCompanyKey	"KohakuNekotarou"	// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_".
#define kKESCMCompanyValue	"KohakuNekotarou"	// Company name displayed externally.

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
DECLARE_PMID(kClassIDSpace, kKESCMPeekWatcherBoss, kKESCMPrefix + 5)	// IEventWatcher: ミドルボタン peek(kMButtonDn/Up をスヌープ)
DECLARE_PMID(kClassIDSpace, kKESCMPeekStartupBoss, kKESCMPrefix + 6)	// IStartupShutdown: アプリ起動時に peek ウォッチャを開始
DECLARE_PMID(kClassIDSpace, kKESCMToastIdleTaskBoss, kKESCMPrefix + 7)	// IIdleTask: カンバス上のトーストを自動で消す
DECLARE_PMID(kClassIDSpace, kKESCMPanelWidgetBoss, kKESCMPrefix + 8)	// ChangeMarker 操作パネル(パレット)
DECLARE_PMID(kClassIDSpace, kKESCMActionComponentBoss, kKESCMPrefix + 9)	// About メニューのアクションコンポーネント
DECLARE_PMID(kClassIDSpace, kKESCMDocResponderServiceBoss, kKESCMPrefix + 10)	// IK2ServiceProvider+IResponder: ドキュメントクローズ監視(閉じた文書の追跡状態を確定クリーンアップ)
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
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekWatcherImpl, kKESCMPrefix + 3)	// IEventWatcher 実装(ミドルボタン peek)
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekStartupImpl, kKESCMPrefix + 4)	// IStartupShutdown 実装(peek ウォッチャを開始)
DECLARE_PMID(kImplementationIDSpace, kKESCMToastIdleTaskImpl, kKESCMPrefix + 5)	// IIdleTask 実装(トースト自動消去)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelObserverImpl, kKESCMPrefix + 6)	// IObserver 実装(パネルのウィジェットオブザーバ)
DECLARE_PMID(kImplementationIDSpace, kKESCMActionComponentImpl, kKESCMPrefix + 7)	// IActionComponent 実装(About)
DECLARE_PMID(kImplementationIDSpace, kKESCMDocServiceProviderImpl, kKESCMPrefix + 8)	// IK2ServiceProvider 実装(クローズ監視のサービス登録)
DECLARE_PMID(kImplementationIDSpace, kKESCMDocResponderImpl, kKESCMPrefix + 9)	// IResponder 実装(クローズ確定時の追跡状態クリーンアップ)
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
DECLARE_PMID(kActionIDSpace, kKESCMPanelWidgetActionID, kKESCMPrefix + 1)	// パネルの表示/非表示(ウィンドウメニュー)
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutThisActionID, kKESCMPrefix + 2)	// パネルのフライアウトの「このプラグインについて」
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutScriptActionID, kKESCMPrefix + 3)	// パネルのフライアウトの「スクリプトについて」
DECLARE_PMID(kActionIDSpace, kKESCMPopupUsageActionID, kKESCMPrefix + 4)	// パネルのフライアウトの「使い方」
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
DECLARE_PMID(kWidgetIDSpace, kKESCMPanelWidgetID, kKESCMPrefix + 0)
DECLARE_PMID(kWidgetIDSpace, kKESCMTargetTextWidgetID, kKESCMPrefix + 1)
DECLARE_PMID(kWidgetIDSpace, kKESCMSourceTextWidgetID, kKESCMPrefix + 26)
// kWidgetIDSpace +27 は現在空き(旧 kKESCMStartButtonWidgetID; 開始/解除を kKESCMToggleButtonWidgetID に統合)
// kWidgetIDSpace +28 は現在空き(旧 kKESCMClearButtonWidgetID; 同上)
DECLARE_PMID(kWidgetIDSpace, kKESCMPrintCheckWidgetID, kKESCMPrefix + 29)
DECLARE_PMID(kWidgetIDSpace, kKESCMOpacityClusterWidgetID, kKESCMPrefix + 30)
DECLARE_PMID(kWidgetIDSpace, kKESCMOpacity25RadioWidgetID, kKESCMPrefix + 31)
DECLARE_PMID(kWidgetIDSpace, kKESCMOpacityNormalRadioWidgetID, kKESCMPrefix + 32)
// kWidgetIDSpace +33 は現在空き(旧 kKESCMHintTextWidgetID; 説明文はパネルから撤去しフライアウト「使い方」へ移動)
DECLARE_PMID(kWidgetIDSpace, kKESCMIconOnWidgetID, kKESCMPrefix + 34)
DECLARE_PMID(kWidgetIDSpace, kKESCMIconOffWidgetID, kKESCMPrefix + 35)
DECLARE_PMID(kWidgetIDSpace, kKESCMStatusTextWidgetID, kKESCMPrefix + 36)
DECLARE_PMID(kWidgetIDSpace, kKESCMToggleButtonWidgetID, kKESCMPrefix + 37)	// 開始/解除を兼ねるトグルボタン
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

// スクリプト要素 ID
// kScriptInfoIDSpace +1 は現在空き(ページ単位 kescmMarkChanges は廃止; kescmMarkChangesDoc を使う)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMClearMarksMethodScriptElement, kKESCMPrefix + 2)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMMarkChangesDocMethodScriptElement, kKESCMPrefix + 3)
// kScriptInfoIDSpace +4 は現在空き(kescmShowPageX 廃止; 対角線のページ × は撤去)
// kScriptInfoIDSpace +5 は現在空き(kescmShowOverset 廃止); 再利用時は衝突に注意
// kScriptInfoIDSpace +6 は現在空き(kescmShowOriginal 廃止; ミドルボタン peek に統合)
// kScriptInfoIDSpace +7 は現在空き(kescmHideOriginal 廃止; kescmShowOriginal と対)
// kScriptInfoIDSpace +8 は現在空き(kescmShowOriginalUnderMouse 廃止; ミドルボタン peek を使う)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMArmMousePeekMethodScriptElement, kKESCMPrefix + 9)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMDisarmMousePeekMethodScriptElement, kKESCMPrefix + 10)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMToastMethodScriptElement, kKESCMPrefix + 11)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMSetPrintMarksMethodScriptElement, kKESCMPrefix + 12)

// "About Plug-ins" sub-menu:
#define kKESCMAboutMenuKey			kKESCMStringPrefix "kKESCMAboutMenuKey"
#define kKESCMAboutMenuPath		kSDKDefStandardAboutMenuPath kKESCMCompanyKey

// "Plug-ins" sub-menu:
#define kKESCMPluginsMenuKey 		kKESCMStringPrefix "kKESCMPluginsMenuKey"
#define kKESCMPluginsMenuPath		kSDKDefPlugInsStandardMenuPath kKESCMCompanyKey kSDKDefDelimitMenuPath kKESCMPluginsMenuKey

// パネルを Plug-Ins メニューへ出すためのパスと位置。
// Plug-Ins ▸ KohakuNekotarou ▸ KohakuChangeMarker（リーフはパネル名キー）。
#define kKESCMPanelPluginsMenuPath		kSDKDefPlugInsStandardMenuPath kKESCMCompanyKey kSDKDefDelimitMenuPath kKESCMPanelTitleKey
#define kKESCMPanelPluginsMenuPosition	100.0	// 大きいほど下に並ぶ。

// Menu item keys:

// Other StringKeys:
#define kKESCMAboutBoxStringKey	kKESCMStringPrefix "kKESCMAboutBoxStringKey"
#define kKESCMAboutScriptMenuKey	kKESCMStringPrefix "kKESCMAboutScriptMenuKey"	// パネルのフライアウト「スクリプトについて」のメニュー名
#define kKESCMScriptHelpStringKey	kKESCMStringPrefix "kKESCMScriptHelpStringKey"	// その本文(kescmToast の使い方)
#define kKESCMUsageMenuKey		kKESCMStringPrefix "kKESCMUsageMenuKey"	// パネルのフライアウト「使い方」のメニュー名(本文は kKESCMHintKey を再利用)
#define kKESCMTargetMenuPath kKESCMPluginsMenuPath

// パネル: 内部フライアウト(ポップアップ)メニュー名＋そのメニューパス。
#define kKESCMInternalPopupMenuNameKey	kKESCMStringPrefix "kKESCMInternalPopupMenuNameKey"
#define kKESCMPopupMenuPath				kKESCMInternalPopupMenuNameKey

// パネルの文字列キー(KESCM_enUS.fr / KESCM_jaJP.fr でローカライズ)。
#define kKESCMPanelTitleKey		kKESCMStringPrefix "kKESCMPanelTitleKey"
#define kKESCMTargetLabelKey	kKESCMStringPrefix "kKESCMTargetLabelKey"	// パネルの "Target:" ラベル。リテラルだとシステム訳と衝突するため自前キーで持つ
#define kKESCMSourceLabelKey	kKESCMStringPrefix "kKESCMSourceLabelKey"	// パネルの "Source:" ラベル。リテラル "Source:" は日本語ロケールで「スタイルソース :」に化けるため自前キーで持つ
#define kKESCMStartButtonKey	kKESCMStringPrefix "kKESCMStartButtonKey"	// トグルボタンの .fr 初期キャプション(未開始=Start)。Stop ラベルは Observer が英語リテラルで設定
#define kKESCMPrintCheckKey		kKESCMStringPrefix "kKESCMPrintCheckKey"
#define kKESCMOpacity25Key		kKESCMStringPrefix "kKESCMOpacity25Key"
#define kKESCMOpacityNormalKey	kKESCMStringPrefix "kKESCMOpacityNormalKey"
#define kKESCMHintKey			kKESCMStringPrefix "kKESCMHintKey"

// PNG アイコンリソース(プラグインに埋め込み; .pln とは別ファイルでは出荷しない)。
#define kKESCMIconOnResID	1001
#define kKESCMIconOffResID	1002
#define kKESCMPaletteIconResID	1003	// パネルが折りたたまれた時に出る小さいドックタブアイコン

// Menu item positions (flyout order): 使い方(10) → スクリプトについて(11) → このプラグインについて(12)
#define kKESCMUsageMenuItemPosition			10.0	// 「使い方」を先頭に
#define kKESCMAboutScriptMenuItemPosition	11.0	// その下に「スクリプトについて」
#define kKESCMAboutThisMenuItemPosition		12.0	// 末尾に「このプラグインについて」


// Initial data format version numbers
#define kKESCMFirstMajorFormatNumber  RezLong(1)
#define kKESCMFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKESCMCurrentMajorFormatNumber kKESCMFirstMajorFormatNumber
#define kKESCMCurrentMinorFormatNumber kKESCMFirstMinorFormatNumber

#endif // __KESCMID_h__
