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

// 4文字 ScriptID コードは KES の採番規約に従う: [種別][K=Kohaku][G=KESCM/ChangeMarker][メンバ]。
// docs/ai-notes/kes-scriptid-registry.md に登録(本体/他の KES プラグインとの衝突をチェック済み)。

// メソッド ID
enum KESCMScriptEvents
{
	e_KESCMClearMarks     = 'eKGc',	// e=method K=Kohaku G=KESCM c=clear : Page.kescmClearMarks()
	e_KESCMMarkChangesDoc = 'eKGD',	// e=method K=Kohaku G=KESCM D=Doc   : Document.kescmMarkChangesDoc(sourceDoc)
	// 'eKGm' (e_KESCMMarkChanges) は現在空き(ページ単位マークは廃止; kescmMarkChangesDoc を使う)
	// 'eKGx' (e_KESCMShowPageX) は現在空き(対角線のページ × は廃止)
	// 'eKGo' (e_KESCMShowOverset) は現在空き(kescmShowOverset 廃止; overset は常に押下表示に乗る)
	// 'eKGr' (e_KESCMShowOriginal) は現在空き(手動の不透明旧版べた載せは廃止; ミドルボタン peek に統合)
	// 'eKGh' (e_KESCMHideOriginal) は現在空き(kescmShowOriginal と対で廃止)
	// 'eKGu' (e_KESCMShowOriginalUnderMouse) は現在空き(手動のマウス下べた載せは廃止; ミドルボタン peek を使う)
	e_KESCMArmMousePeek    = 'eKGa',	// e=method K=Kohaku G=KESCM a=arm    : Document.kescmArmMousePeek(sourceDoc) - ミドルボタン peek を arm(引数は p_KESCMSourceDoc を再利用)
	e_KESCMDisarmMousePeek = 'eKGd',	// e=method K=Kohaku G=KESCM d=disarm : Document.kescmDisarmMousePeek() - ミドルボタン peek を disarm してキャッシュを解放
	e_KESCMToast           = 'eKGt',	// e=method K=Kohaku G=KESCM t=toast  : Page/Document.kescmToast(message) - 画面中央に少し出て自動で消えるメッセージ
	e_KESCMSetPrintMarks   = 'eKGp'	// e=method K=Kohaku G=KESCM p=print  : Page/Document.kescmSetPrintMarks(flag) - 変更マークを印刷/PDF に出す(ON の間は画面にも残る)
};

// プロパティ / パラメータ ID
enum KESCMScriptProperties
{
	p_KESCMSourceDoc    = 'pKGD',	// p=param K=Kohaku G=KESCM D=source Doc  : 比較相手(旧)ドキュメント
	// 'pKGs' (p_KESCMSourcePage) は現在空き(ページ単位の kescmMarkChanges/kescmShowOriginal は廃止)
	// 'pKGx' (p_KESCMShowPageXFlag) は現在空き(kescmShowPageX は廃止)
	// 'pKGo' (p_KESCMShowOversetFlag) は現在空き(kescmShowOverset は廃止)
	p_KESCMToastMsg      = 'pKGt',	// p=param K=Kohaku G=KESCM t=toast text  : 画面中央に少し表示する文字列
	p_KESCMPrintMarksFlag = 'pKGp',	// p=param K=Kohaku G=KESCM p=print flag  : kTrue でマークを印刷(既定)、kFalse で画面のみ
	p_KESCMPrintFaintFlag = 'pKGf'	// p=param K=Kohaku G=KESCM f=faint flag  : kTrue でマークを約25%不透明度で印刷、kFalse(既定)で通常不透明度
	// 'pKGn' (p_KESCMSensitivity) は現在空き — 感度セレクタは廃止; 検出は常に CMYK 4ch の差>0
};


#endif // __KESCMScriptingDefs_h__
