//========================================================================================
//
//  KohakuExtendScriptChangeMarker (KESCM)
//  ---------------------------------------------------------------------------------------
//  2 つのドキュメント(版)の違いを、変化箇所を囲む「半透明の枠(リング)」で示す
//  非破壊オーバーレイ。内容は下の実ページがそのまま透けて見えるので隠れない。
//  リング色は通常は赤。ただし枠の下の実ページが赤っぽい画素では、赤枠が背景に埋もれるのを
//  避けるため、その画素だけ青へ切り替える(画素単位の色適応)。
//
//  ★複数ページ対応(2026-06-21): 対象を「1ページ」から「ページUID集合(std::map)」へ拡張。
//     kEndSpreadMessage は描画されるスプレッドごとに発火するので、低ズームで複数スプレッドが
//     同時に見えても、各スプレッドの描画イベントで各々のページが描かれ、自動的に全部に出る。
//     変化が無いページはエントリを作らない(マスクも持たない=描画でも即スキップ)。
//
//  API:
//   Document.kescmMarkChangesDoc(sourceDoc):
//     このドキュメントの全ページを sourceDoc の同じ番号のページと CMYK 比較し、変化ページ全部に
//     リングを付ける(総入れ替え)。ページ数が違う場合は重なる範囲のみ比較。
//   Page/Document.kescmClearMarks():
//     全エントリを破棄して再描画し、オーバーレイを消す。
//   (旧版べた載せはミドルボタン peek = kescmArmMousePeek/kescmDisarmMousePeek に統合。
//    トースト=kescmToast、印刷=kescmSetPrintMarks。単ページ版 kescmMarkChanges /
//    手動 kescmShowOriginal/HideOriginal/ShowOriginalUnderMouse は 2026-06-24 に撤去。)
//
//  装飾(IPageItemAdornmentList)ではなく DrawEventHandler で描くので、書類モデルに一切
//  触れない=.indd に保存されない(missing-plugin 警告も出ない)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// スクリプトプロバイダ基盤:
#include "IScript.h"
#include "IScriptRequestData.h"			// ScriptData / ExtractRequestData(ScriptData.h を取り込む)
#include "CScriptProvider.h"			// 実装基底
#include "CPMUnknown.h"					// CREATE_PMINTERFACE

// オブジェクトモデル:
#include "PersistUtils.h"				// ::GetUIDRef
#include "IDataBase.h"

// プロジェクト内インクルード:
//   実処理は各分割ファイル(KESCMCore / 描画エンジン / トースト / 色サンプラ / peek)に移譲済み。
//   ここはスクリプト引数の取り出しと委譲だけなので、必要なヘッダだけを持つ。
#include "KESCMScriptingDefs.h"			// e_KESCM* / p_KESCM* の ID
#include "KESCMID.h"					// boss / impl の ID
#include "KESCMConstants.h"				// kKESCMToastDefaultMs
#include "KESCMCore.h"					// 共有操作(KESCMDo*)。実処理はこちら
#include "KESCMToast.h"					// KESCMShowToast









//========================================================================================
// KESCMScriptProvider
//   Page / Document オブジェクトに kescmMarkChangesDoc / kescmClearMarks / kescmArmMousePeek /
//   kescmDisarmMousePeek / kescmToast / kescmSetPrintMarks を生やす。
//========================================================================================



class KESCMScriptProvider : public CScriptProvider
{
public:
	KESCMScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	~KESCMScriptProvider() {}

	virtual ErrorCode HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent);

private:
	// kescmMarkChangesDoc(sourceDoc): このドキュメント全ページを sourceDoc と突き合わせて総入れ替え。
	ErrorCode MarkChangesDoc(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmClearMarks(): オーバーレイを全消去。
	ErrorCode ClearMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmArmMousePeek(sourceDoc): ミドルボタン peek を arm(比較相手の旧ドキュメントを登録)。Document 対応。
	ErrorCode ArmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmDisarmMousePeek(): ミドルボタン peek を解除し、旧版べた載せを隠してキャッシュも解放。Document 対応。
	ErrorCode DisarmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmToast(message): 画面中央に message を少し表示して自動で消す。Page/Document 対応。
	ErrorCode Toast(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmSetPrintMarks([flag]): 変更マーク(リング＋変更数)を印刷/PDF に出すか。ON の間は画面も常時表示。Page/Document 対応。
	ErrorCode SetPrintMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent);
};

CREATE_PMINTERFACE(KESCMScriptProvider, kKESCMScriptProviderImpl)


ErrorCode KESCMScriptProvider::HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	ErrorCode status = kFailure;
	switch (methodID.Get())
	{
	case e_KESCMMarkChangesDoc:
		status = MarkChangesDoc(methodID, data, parent);
		break;
	case e_KESCMClearMarks:
		status = ClearMarks(methodID, data, parent);
		break;
	case e_KESCMArmMousePeek:
		status = ArmMousePeek(methodID, data, parent);
		break;
	case e_KESCMDisarmMousePeek:
		status = DisarmMousePeek(methodID, data, parent);
		break;
	case e_KESCMToast:
		status = Toast(methodID, data, parent);
		break;
	case e_KESCMSetPrintMarks:
		status = SetPrintMarks(methodID, data, parent);
		break;
	default:
		status = CScriptProvider::HandleMethod(methodID, data, parent);
	}
	return status;
}


/* MarkChangesDoc
   このドキュメント(parent=Document)の全ページを、sourceDoc の同じ番号のページと突き合わせ、
   変化したページ全部にリングを付ける(既存マークは総入れ替え)。ページ数が違う場合は重なる範囲のみ。
*/
ErrorCode KESCMScriptProvider::MarkChangesDoc(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef targetDocRef = ::GetUIDRef(parent);
	IDataBase* targetDB = targetDocRef.GetDataBase();
	if (targetDB == nil)
		return kFailure;

	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMSourceDoc, arg) != kSuccess)
		return kFailure;
	InterfacePtr<IScript> srcScript(arg.QueryObject());
	if (srcScript == nil)
		return kFailure;
	IDataBase* sourceDB = ::GetUIDRef(srcScript).GetDataBase();
	if (sourceDB == nil)
		return kFailure;

	PMString report;
	if (KESCMDoMarkChangesDoc(targetDB, sourceDB, report) != kSuccess)
		return kFailure;

	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ClearMarks
   全エントリを破棄し、対象ドキュメントを再描画してオーバーレイを消す。Page でも Document でも可。
*/
ErrorCode KESCMScriptProvider::ClearMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();

	KESCMDoClearMarks(db);

	PMString report("marks cleared");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* SetPrintMarks
   変更マーク(リング＋変更数)を印刷/PDF にも出すかを切り替える(既定=画面のみ)。ON の間は、ミドル押下に
   関係なく画面でも常時表示(WYSIWYG)。マークのデータ(sEntries)は触らず、表示/印刷の挙動だけ変える。
   引数 flag は省略可(省略時=ON=印刷する)。Page でも Document でも呼べる。
*/
ErrorCode KESCMScriptProvider::SetPrintMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	bool16 flag = kTrue;	// 省略時は印刷ON
	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMPrintMarksFlag, arg) == kSuccess)
		arg.GetBoolean(&flag);

	bool16 faint = kFalse;	// 省略時は通常不透明度(約70%)
	ScriptData arg2;
	if (data->ExtractRequestData(p_KESCMPrintFaintFlag, arg2) == kSuccess)
		arg2.GetBoolean(&faint);

	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	KESCMDoSetPrintMarks(flag, faint, db);

	PMString report;
	report.SetTranslatable(kFalse);
	if (flag)
		report.Append(faint ? "kescm: marks will print at ~25% (and stay visible on screen)"
		                    : "kescm: marks will print at normal opacity (and stay visible on screen)");
	else
		report.Append("kescm: marks are screen-only (won't print)");
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ArmMousePeek
   ミドルボタン peek を arm する。parent=新ドキュメント(targetDB)、引数 sourceDoc=比較相手の旧ドキュメント。
   以後、ミドルボタンを押している間だけマウス下スプレッドの旧版が不透明べた載せされ、離すと隠れる。
   キャッシュは直近に覗いた1スプレッド分だけ保持(同じスプレッドの再 peek は即時)。実表示は watcher が行う。
*/
ErrorCode KESCMScriptProvider::ArmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef targetDocRef = ::GetUIDRef(parent);
	IDataBase* targetDB = targetDocRef.GetDataBase();
	if (targetDB == nil)
		return kFailure;

	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMSourceDoc, arg) != kSuccess)
		return kFailure;
	InterfacePtr<IScript> srcScript(arg.QueryObject());
	if (srcScript == nil)
		return kFailure;
	IDataBase* sourceDB = ::GetUIDRef(srcScript).GetDataBase();
	if (sourceDB == nil)
		return kFailure;

	KESCMDoArmMousePeek(targetDB, sourceDB);

	PMString report("kescm: mouse peek armed (Shift+middle-click and hold over a spread)");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* DisarmMousePeek
   ミドルボタン peek を解除する。表示を OFF にし、旧版キャッシュも解放(メモリ開放)。Page/Document 可。
*/
ErrorCode KESCMScriptProvider::DisarmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	KESCMDoDisarmMousePeek(db);

	PMString report("kescm: mouse peek disarmed");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* Toast
   画面(=可視領域)の中央に message を少し表示し、約 2.5 秒後に自動で消す。Page でも Document でも可。
   画面表示のみ・非永続。実描画は HandleDrawEvent、自動消去は KESCMToastIdleTask が担う。
*/
ErrorCode KESCMScriptProvider::Toast(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();

	ScriptData arg;
	PMString msg;
	if (data->ExtractRequestData(p_KESCMToastMsg, arg) == kSuccess)
		arg.GetPMString(msg);
	msg.SetTranslatable(kFalse);

	KESCMShowToast(db, msg, kKESCMToastDefaultMs);

	PMString report("kescm: toast shown");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}
