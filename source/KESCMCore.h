//========================================================================================
//
//  KESCMCore.h
//
//  ChangeMarker (KESCM) の共有操作。スクリプトプロバイダとパネル UI の両方から呼べる。
//  描画エンジン本体とその file-local 状態は KESCMDrawEventHandler.cpp にあり、ここはその薄い
//  入口として、パネルのウィジェットオブザーバがスクリプトメソッドと完全に同じ挙動を駆動できるように
//  する(Start = 変更マーク＋peek arm、Clear = マーク消去＋peek disarm、など)。
//
//========================================================================================

#ifndef __KESCMCore_h__
#define __KESCMCore_h__

#include "BaseType.h"		// ErrorCode, bool16
#include "PMString.h"
#include "PMReal.h"			// PMReal(ヒットテストヘルパのマウス座標)
#include "OMTypes.h"			// UID (typedef IDType<UID_tag>)
#include <vector>

class IDataBase;
class IControlView;

// ドキュメント内の全ページUIDを、スプレッド順・ページ順で平坦に集める。比較(KESCMDoMarkChangesDoc)と
// 色サンプラが共有するヘルパ。実体は KESCMCore.cpp。
void		KESCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out);

// 現在のマウス位置を、このビューの content(ペーストボード)座標で読む(画面→窓→content)。view が nil なら
// kFalse。peek と色サンプラが同じ流儀でカーソル位置を求めるための共有ヘルパ。
bool16		KESCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY);

// マウス下のページを特定した結果(KESCMFindPageUnderMouse 参照)。平坦ページ番号は KESCMCollectPageUIDs と
// 一致するので、globalPageBase + hitPageIndex が旧ドキュメントの平坦ページ列にそのまま対応する。
struct KESCMPageHit
{
	int32 spreadIndex;		// 当たったスプレッドのスプレッドリスト内インデックス
	UID   spreadUID;		// そのスプレッドのUID(必要に応じて ISpread を引き直す)
	int32 numPages;			// そのスプレッドのページ数
	int32 globalPageBase;	// このスプレッド先頭の平坦ページ番号
	int32 hitPageIndex;		// スプレッド内でカーソル下にあるページの 0 始まりインデックス
	UID   hitPageUID;		// そのページのUID
};

// マウス(content/ペーストボード座標)を targetDB の全ページにスプレッド順・ページ順でヒットテストする。
// 最初に (mx,my) を含むページで 'out' を埋めて kTrue を返す。無ヒットなら kFalse。
bool16		KESCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KESCMPageHit& out);

// targetDB の各ページを sourceDB の同番号ページと比較し、変更マークのオーバーレイを(再)構築する。
// outReport にはスクリプトメソッドが返すのと同じ状態文字列が入る。
ErrorCode	KESCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport);

// オーバーレイ全体(と旧版画像のキャッシュ)を破棄し、db を再描画する。
void		KESCMDoClearMarks(IDataBase* db);

// マークを印刷に出すか(かつ画面に常時表示するか)を切り替える。faintFlag = 約25%で印刷。
void		KESCMDoSetPrintMarks(bool16 printFlag, bool16 faintFlag, IDataBase* db);

// 旧版のミドルボタン peek を arm / disarm する(パネルの ON/OFF 状態も駆動する)。
void		KESCMDoArmMousePeek(IDataBase* targetDB, IDataBase* sourceDB);
void		KESCMDoDisarmMousePeek(IDataBase* db);

// パネルの状態アクセサ。"Armed" == Start ボタンが実行済みで Clear がまだ、の状態。arm 中はパネルが
// Target/Source 名と ON アイコンを表示し、それ以外では名前を隠して OFF を表示する。
bool16		KESCMIsArmed();
IDataBase*	KESCMArmedTargetDB();
IDataBase*	KESCMArmedSourceDB();

// 現在の印刷マーク設定。パネルを開き直したときにチェック/ラジオを実状態へ復元するために使う。
bool16		KESCMGetPrintMarks();	// 印刷マーク ON/OFF
bool16		KESCMGetPrintFaint();	// 印刷不透明度: kTrue=約25% / kFalse=通常

// ドキュメントがクローズされた直後(kAfterCloseDoc レスポンダ)に呼ぶ。追跡中の全DB(マーク/旧版画像/
// トースト/peek arm)を IDocumentList で生存確認し、閉じていたものだけ確定的にクリーンアップする
// (DropAll/DropAllOrig/トースト消去/無音 disarm)。片付けが起きたらパネルも ON→OFF 更新する。
// どの db が閉じたかは信号から取れない(AfterClose では UIDRef 無効)ため、生存スイープで判定する。
// 実体は KESCMPeek.cpp(peek の file-local 状態にアクセスできる唯一の場所)。
void		KESCMHandleDocsClosed();

// 現在表示中のパネルがあれば、その ON/OFF 表示(Target/Source 名・アイコン・トグルラベル)を
// 現在の arm 状態(KESCMIsArmed 等)に合わせて更新する。パネルが隠れていれば何もしない
// (再表示時に AutoAttach が反映する)。実体は KESCMPanelObserver.cpp。
void		KESCMRefreshPanel();

#endif // __KESCMCore_h__
