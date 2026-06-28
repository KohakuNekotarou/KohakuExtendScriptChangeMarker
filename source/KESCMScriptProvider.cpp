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
//  スクリプト API:
//   Page/Document.kescmToast(message):
//     画面中央に message を少し表示して自動で消す(画面のみ・非永続)。
//   ※ 比較/消去/ミドル peek/印刷マークの各操作は操作パネルが KESCMCore の共有関数を直接呼ぶ。
//     以前あったスクリプトメソッド(kescmMarkChangesDoc / kescmClearMarks / kescmArmMousePeek /
//     kescmDisarmMousePeek / kescmSetPrintMarks)は 2026-06-28 に撤去(パネル経由に一本化)。
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
#include "KESCMScriptingDefs.h"			// e_KESCMToast / p_KESCMToastMsg の ID
#include "KESCMID.h"					// boss / impl の ID
#include "KESCMConstants.h"				// kKESCMToastDefaultMs
#include "KESCMToast.h"					// KESCMShowToast









//========================================================================================
// KESCMScriptProvider
//   Page / Document オブジェクトに kescmToast(message) を生やす。
//   (比較/消去/peek/印刷は操作パネルが KESCMCore の共有関数を直接呼ぶ=スクリプト API は非公開。)
//========================================================================================



class KESCMScriptProvider : public CScriptProvider
{
public:
	KESCMScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	~KESCMScriptProvider() {}

	virtual ErrorCode HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent);

private:
	// kescmToast(message): 画面中央に message を少し表示して自動で消す。Page/Document 対応。
	ErrorCode Toast(ScriptID methodID, IScriptRequestData* data, IScript* parent);
};

CREATE_PMINTERFACE(KESCMScriptProvider, kKESCMScriptProviderImpl)


ErrorCode KESCMScriptProvider::HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	ErrorCode status = kFailure;
	switch (methodID.Get())
	{
	case e_KESCMToast:
		status = Toast(methodID, data, parent);
		break;
	default:
		status = CScriptProvider::HandleMethod(methodID, data, parent);
	}
	return status;
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
