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
//   Page.kescmMarkChanges(sourcePage):
//     呼び出し元ページ(新)と sourcePage(旧, 別ドキュメント可)を 72dpi ARGB でラスタ化し、
//     画素差分マスク→赤リングを作って、このページにエントリ登録(複数回呼ぶと貯まる)。
//   Document.kescmMarkChangesDoc(sourceDoc):
//     このドキュメントの全ページを sourceDoc の同じ番号のページと突き合わせ、変化ページ全部に
//     リングを付ける(総入れ替え)。ページ数が違う場合は重なる範囲のみ比較。
//   Page/Document.kescmClearMarks():
//     全エントリを破棄して再描画し、オーバーレイを消す。
//
//  装飾(IPageItemAdornmentList)ではなく DrawEventHandler で描くので、書類モデルに一切
//  触れない=.indd に保存されない(missing-plugin 警告も出ない)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IScript.h"
#include "IScriptRequestData.h"

// General includes:
#include "CScriptProvider.h"
#include "CPMUnknown.h"					// 実装基底
#include "CServiceProvider.h"			// IK2ServiceProvider 実装基底

// Object model:
#include "PersistUtils.h"				// ::GetUIDRef
#include "IDataBase.h"
#include "IGeometry.h"					// ページ bbox
#include "IDocument.h"					// InvalidateViews に渡す IDocument
#include "ILayoutUtils.h"				// InvalidateViews(即時再描画)
#include "ILayoutUIUtils.h"				// QueryFrontView(マウスが乗っている前面レイアウトビュー)
#include "IEventUtils.h"				// GetGlobalMouseLocation(現在のマウス位置)
#include "IApplication.h"				// QueryDocumentList / app boss = IEventDispatcher
#include "IDocumentList.h"				// FindDocByDataBase(arm 済み DB の生存確認=ダングリング回避)
#include "ISpread.h"					// changedBy(スプレッド)→ページ列 / GetItemsOnPage
#include "ISpreadList.h"				// ドキュメント→全スプレッド列挙(doc単位の一括mark)
#include "IFrameUtils.h"				// IsTextFrame/GetTextFrameUID + textflags(kTF_Overset)
#include "UIDList.h"					// GetItemsOnPage の結果受け
#include "IMultiColumnTextFrame.h"		// テキストフレーム→ストーリー(text model)UID
#include "IStoryOptions.h"				// IsVertical()でそのストーリーが縦書きかを per-story 判定
#include "IShape.h"						// kPreviewMode / kPrinting フラグ

// 描画 / Draw Event:
#include "IDrwEvtHandler.h"				// IDrwEvtHandler / DrawEventData
#include "IDrwEvtDispatcher.h"			// RegisterHandler / kDEHLowestPriority

// ミドルボタン peek(イベント監視):
#include "IEventWatcher.h"				// 非消費スヌープ(kMButtonDn/Up)
#include "IEvent.h"						// IEvent::kMButtonDn / kMButtonUp / GetType
#include "IEventDispatcher.h"			// AddEventWatcher / EventTypeList
#include "IStartupShutdownService.h"	// 起動時に watcher を開始
#include "CreateObject.h"				// CreateObject2<IEventWatcher> / CreateObject2<IIdleTask>
#include "IIdleTask.h"					// 一時トーストの自動消去タイマ(RunTask で消す)
#include "IIdleTaskMgr.h"				// AddTask / RemoveTask(タイマ登録)
#include "IToolBoxUtils.h"				// ツール切替(ミドル押下中だけハンドツール)
#include "ITool.h"						// ITool
#include "LayoutUIID.h"					// kGrabberHandToolBoss / kPointerToolBoss
#include "DocumentContextID.h"			// kEndSpreadMessage
#include "GraphicsID.h"					// kDrawEventService
#include "GraphicsData.h"				// GraphicsData::GetGraphicsPort / GetView
#include "IGraphicsPort.h"				// image() / translate / scale
#include "AutoGSave.h"					// 描画状態の save/restore
#include "IControlView.h"				// GetContentToWindowMatrix(現ズーム)
#include "IPanorama.h"					// 可視範囲の上端を content 座標で取る(拡大時の数値の縦位置追従)
#include "IWidgetParent.h"				// QueryParentFor(子ウィジェットから親 LayoutWidget の panorama を辿る)
#include "ISession.h"					// GetExecutionContextSession(既定フォント取得)
#include "IFontMgr.h"					// 既定フォント取得(framelabel/TEST と同じ)
#include "IPMFont.h"					// IPMFont(selectfont に渡す)
#include "PMMatrix.h"
#include "PMPoint.h"					// 回転オブジェクトの角の点を spread 座標へ変換
#include "PMReal.h"						// ::ToInt32 / ::Round
#include "TransformUtils.h"				// ::InnerToSpreadMatrix / ::GetDataBase

// ラスタ化:
#include "SnapshotUtilsEx.h"			// ページをオフスクリーン(ビットマップ)化
#include "AGMImageAccessor.h"			// GetBounds() / GetBaseAddr() / GetAGMColorFamily()
#include "GraphicsExternal.h"			// AGMImageRecord(自前で組んで blit する)

// STL:
#include <map>
#include <vector>
#include <string.h>						// memcpy

// Project includes:
#include "KESCMScriptingDefs.h"
#include "KESCMID.h"


//========================================================================================
// チューニング定数
//========================================================================================
static const PMReal kKESCMRingTargetPx = 7.0;	// リングの目標太さ(画面px)。ズームに依らず一定に見せる
static const uint8 kKESCMRingAlpha = 180;	// リングのアルファ(0..255)。約71%不透明=半透明で下の実ページが透ける(小さいほど透明)
static const int   kKESCMDiffThr = 32;	// 変化判定: 2版の生RGB最大チャンネル差がこれ超で「変化」(AA/微差を除く)
static const int32 kKESCMBaseRadius = 4;	// リング初期半径(画像px)。描画時にズームから再算出するための初期値
static const PMReal kKESCMResolution = 72.0;	// 保存・表示のラスタ解像度(dpi)。リング画像/マスクはこの解像度で持つ(軽い)
// 【取りこぼし防止】比較だけ高解像度で行い、結果を低解像度に圧縮(マックスプーリング)して記憶する。
// 比較解像度 = kKESCMResolution × kKESCMHiResMul。低解像度では平均化で消える細線/微小ズレを満額で拾う。
static const PMReal kKESCMHiResMul    = 2.0;	// 比較解像度の倍率(2=144dpi)。上げるほど検出力↑/一時メモリ↑。300dpi 相当なら≒4.17
static const int32  kKESCMPoolMinCount = 1;	// プーリング: 低解像度1セル内の「高解像度の変化画素数」がこの値以上で変化と判定。
											// 1=最高感度(縁ノイズも拾う)/大きいほどノイズ耐性↑(取りこぼしのリスクも僅かに増)

// リング色: 通常は赤。ただし枠の下の実ページが「赤っぽい」画素の上では、半透明の赤枠が背景に埋もれて
// 見えなくなるため、視認性確保のために青へ切り替える(画素単位)。
static const uint8 kKESCMRingR = 255, kKESCMRingG = 0,   kKESCMRingB = 0;		// 通常(赤)
static const uint8 kKESCMRingAltR = 0,   kKESCMRingAltG = 0,   kKESCMRingAltB = 255;	// 赤背景の上(純粋な青)
static const int   kKESCMRedBgDom = 25;	// 背景を「赤っぽい」と判定する R 優位の閾値(R が G,B の双方より これ以上大きい)。小さいほどピンク/薄い赤も拾う

// 変更数テキストの色。数字=赤、後ろの語=青。setrgbcolor(0..1)。リング(赤/青)とは別系統。
static const PMReal kKESCMCountNumR  = 1.0, kKESCMCountNumG  = 0.0, kKESCMCountNumB  = 0.0;	// 数字 赤
static const PMReal kKESCMMarkR = 0.0, kKESCMMarkG = 0.0, kKESCMMarkB = 1.0;					// 語 青

// ページ全体の対角線×(どのページが変わったか一目で分かる印)。任意で表示/非表示(kescmShowPageX)。
// 2本の線は色を分ける: "/"(右上→左下)=赤、"\"(左上→右下)=青。
static const PMReal kKESCMPageXPx      = 5.0;	// ×線の太さ(画面px)。ズームに依らず一定に見せる
static const PMReal kKESCMPageXOpacity = 0.7;	// ×線の不透明度(0..1)。下のページが透ける半透明
static const PMReal kKESCMSlashR = 1.0, kKESCMSlashG = 0.0, kKESCMSlashB = 0.0;	// "/" 赤
static const PMReal kKESCMBackR  = 0.0, kKESCMBackG  = 0.0, kKESCMBackB  = 1.0;	// "\" 青

// 変更数テキスト(各ページの変更=枠の数を「N chg」で表示)。常時表示(トグル無し。X廃止)。ズームで大きさ不変。
// 位置=各ページ上端から少し下、ページ「内側」に横中央で置く。数字は赤fill＋細い赤ストロークでやや太字、白い縁(ハロー)付き。
// 白縁=本体の前に白を太めのストロークで描き(線幅=文字サイズ×kKESCMCountHaloFrac)。ストロークは輪郭中心なので
// 見える白縁は線幅の約半分。本体の赤fill＋赤ストロークが内側半分を覆い、外側に白リムが残る。
static const PMReal kKESCMCountTextPx   = 20.0;	// 数字の文字サイズ(画面px)
static const PMReal kKESCMCountInsetPx  = 6.0;	// ページ上端からの内側余白(画面px)
static const PMReal kKESCMCountHaloFrac = 0.16;	// 白縁の太さ(文字サイズに対する比)。見える縁はこの約半分。大きいほど太い縁
static const PMReal kKESCMCountBodyFrac = 0.06;	// 赤本体を太らせるストローク幅(文字サイズ比)。大きいほど太い赤文字。0でfillのみ(最も細い)
// 数字の後ろに続く語(" chg")。小さめ・細め(fill のみ=ストローク無し)で添える。
static const PMReal kKESCMCountWordPx   = 11.0;	// 語の文字サイズ(画面px)。数字より小さめ
// 縦ドリフト抑制: ページ内に置く落とし込み量(画面px固定=inset+numPt)は縮小すると spread 座標で増大し
// ページ内を下へ流れる。ページ高さのこの割合を上限にクランプし、上部に留める。
static const PMReal kKESCMCountTopFracMax = 0.30;	// ベースラインの上端からのオフセット上限(ページ高さ比)

// 【文字サイズのズーム連動】画面表示サイズを「拡大率(UIズーム値)」で倍変させる。倍率 M を上の各 px に掛ける。
// 2点 (zoomLo, mulLo) (zoomHi, mulHi) を通る反比例 M = a - b/zoom(割り算1回で軽い)。範囲外は [mulLo, mulHi] にクランプ。
// 低ズームで小さめ・高ズームで大きめ。比例(直線)より低ズーム側で急に立ち上がり高ズーム側で頭打ちになる曲線。
static const PMReal kKESCMCountZoomLo = 0.05;	// 基準ズーム(下)= 5%
static const PMReal kKESCMCountMulLo  = 0.80;	// その時の倍率(現状の0.8倍)
static const PMReal kKESCMCountZoomHi = 1.00;	// 基準ズーム(上)= 100%
static const PMReal kKESCMCountMulHi  = 3.00;	// その時の倍率(現状の3倍)

// 【テスト機能】オーバーセット(あふれ)テキストフレームの目印。内蔵の小さな赤「＋」は拡大できないので、
// 自前で「白塗りの四角＋赤い十字」をフレーム右下(アウトポート相当)に、ズーム不変で重ねて描く(kescmShowOverset)。
// 回転フレームでも、マーク自身は回さず(軸そろえ)、位置だけ回転後の角へ置く。
// 横書きフレームは右下角(アウトポート)、縦書きフレームは左下角に出す。
static const PMReal kKESCMOversetPx     = 28.0;	// 四角の一辺(画面px)
static const PMReal kKESCMOversetLinePx = 2.5;	// 線(四角の枠と十字)の太さ(画面px)。赤
static const PMReal kKESCMOversetHaloPx = 1.5;	// 白塗りを枠の外へどれだけはみ出すか(画面px)。背景に埋もれない外周の白リム
static const PMReal kKESCMOversetPlusInsetFrac = 0.22;	// 十字の腕を四角の辺からどれだけ内側に入れるか(辺長比)
// 変更数カウント用の併合半径(画像px, 72dpi基準)。数える前にマスクをこの半径で膨張し、近接した変化
// (文字グリフの破片や1段落内の行など)を1つの塊にまとめてから連結成分を数える。これで「文字が変わった
// だけで数が膨大」を防ぎ、見た目の赤いリング(塊)の数に近づける。大きいほど大きくまとめる(数が減る)。
static const int32 kKESCMCountMergeRadius = 8;

// 旧版べた載せ(kescmShowOriginal)で重ねる画像の解像度(dpi)。スクリプト実行時に、対象ページの旧版を
// この解像度で1枚だけラスタ化(オフスクリーン1枚=即破棄)し、不透明でページ矩形いっぱいに重ねる。
// 高いほど鮮明・メモリ大(A4・300dpi で約26〜35MB/ページ)。覗いたページの分だけ保持する。
// 72dpi = 標準(非HiDPI)100%拡大でドキュメント1inch=72px と1:1一致する値。メモリ最軽量(A4で約2MB/頁)・
// 押下時のラスタ化も最速。2x HiDPIで拡大して粗ければ 144 へ戻す。
static const PMReal kKESCMOrigResolution = 72.0;

// 一時トーストメッセージ(画面=可視領域の中央に少し出て自動で消える「ChangeMarker ON」等)。ズーム不変サイズ。
static const PMReal kKESCMToastTextPx    = 36.0;	// 文字サイズ(画面px)
static const PMReal kKESCMToastPadPx     = 16.0;	// 文字周りの内側余白(画面px)。大きいほど背景ボックスが広い
static const uint32 kKESCMToastDefaultMs = 2500;	// 既定の表示時間(ms)。表示後この時間で自動的に消える


//========================================================================================
// KESCMOverlayEntry
//   1ページ分のオーバーレイ。★SnapshotUtilsEx / AGMImageAccessor は保持しない。
//   生成時に画素を自前バッファ(buf)へコピーし、buf を指す自前の AGMImageRecord(rec)を組む。
//   こうしてオフスクリーンを完全に切り離す(非推奨 GetAGMImageRecord も不使用)ことで、
//   破棄時クラッシュ(保持した accessor/snapshot の delete 時の二重解放)を回避する。
//   dist は 72dpi 差分マスクの「チェスボード距離変換」(★案A)。変化のあったページにだけ作られる。
//========================================================================================
struct KESCMOverlayEntry
{
	uint8*         buf;			// 自前の ARGB バッファ(リング画像)。所有
	AGMImageRecord rec;			// buf を指す自前の画像レコード(blit 用)
	uint8*         dist;		// ★案A: 差分マスクのチェスボード距離変換(w*h, uint8, 0=変化画素, clamp255)。所有。
								//   リング = 0<dist<=radius。BuildRing が膨張なしの1パスで塗れる(mask は dist 生成後は破棄)。
	uint8*         bgRed;		// 対象ページが「赤っぽい」画素=1 のマップ(w*h)。リングの青切替に使う。所有(nil可)
	int32          w, h;
	int32          rowBytes;	// buf の行バイト数(= rec.byteWidth)
	int32          bpp;			// バイト/ピクセル
	int32          lastRadius;	// 最後に描いたリング半径(px)。-1=未描画
	int32          changeCount;	// このページの変更(枠)の数=マスクの連結成分数。テキスト表示用

	KESCMOverlayEntry() : buf(nil), dist(nil), bgRed(nil), w(0), h(0), rowBytes(0), bpp(0), lastRadius(-1), changeCount(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KESCMOverlayEntry()
	{
		if (buf)   delete[] buf;
		if (dist)  delete[] dist;
		if (bgRed) delete[] bgRed;
	}
};


//========================================================================================
// KESCMOrigImage
//   1ページ分の「旧版(比較相手)」の不透明画像。kescmShowOriginal 実行時にその場で生成して保持し、
//   トグルが ON の間、対応するページの矩形いっぱいに不透明 blit する(べた載せ)。
//   SnapshotUtilsEx/accessor は保持せず、画素を buf へコピーして自前 rec を組む(切り離し=破棄時クラッシュ回避)。
//========================================================================================
struct KESCMOrigImage
{
	uint8*         buf;			// 自前の画像バッファ(不透明)。所有
	AGMImageRecord rec;			// buf を指す自前の画像レコード(blit 用)
	int32          w, h;
	int32          rowBytes;
	int32          bpp;

	KESCMOrigImage() : buf(nil), w(0), h(0), rowBytes(0), bpp(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KESCMOrigImage()
	{
		if (buf) delete[] buf;
	}
};


//========================================================================================
// KESCMDrawEventHandler
//   ページUID→オーバーレイの集合を保持し、スプレッド描画時に、そのスプレッドに属する
//   各ページのリングを blit する。リング太さは描画時のズームに追従。非永続=.indd に残らない。
//========================================================================================
class KESCMDrawEventHandler : public CPMUnknown<IDrwEvtHandler>
{
public:
	KESCMDrawEventHandler(IPMUnknown* boss) : CPMUnknown<IDrwEvtHandler>(boss) {}
	~KESCMDrawEventHandler() {}

	virtual void Register(IDrwEvtDispatcher* d);
	virtual void UnRegister(IDrwEvtDispatcher* d);
	virtual bool16 HandleDrawEvent(ClassID eventID, void* eventData);

	// ページUID → オーバーレイ。変化のあったページだけ登録される。
	static std::map<UID, KESCMOverlayEntry*> sEntries;
	// 全エントリが属する単一ドキュメント。別dbをmarkしたら作り直す(UIDはdb内のみ一意なため)。
	static IDataBase* sDB;
	// 上書き表示(変更リング＋ページX＋オーバーセット印)の master 表示トグル。データ(sEntries)は消さず
	// 表示だけ切り替える。★既定=kFalse(非表示)。シングルミドルボタンを押している間だけ kTrue にして枠等を
	// 表示し、離すと kFalse に戻す。kFalse の間はこれら全部を描かない。旧版べた載せ(sShowOriginal)は
	// このトグルの影響を受けない(ダブルクリックで別管理)。
	static bool16 sMarksVisible;
	// 変更ページに対角線×を出すか(任意トグル kescmShowPageX)。マークとは独立に保持(DropAllでは変えない)。
	static bool16 sShowPageX;
	// 【テスト】オーバーセットのフレーム右下に「四角＋＋」を出すか(任意トグル kescmShowOverset)。
	// マーク(sEntries)とは完全に独立。ONなら sEntries が空でもハンドラが走り、その時々の overset を毎回検出して描く。
	static bool16 sShowOverset;

	// 旧版べた載せ(kescmShowOriginal / kescmHideOriginal)。マーク(sEntries)とは完全に独立。
	// 実行時に覗いたページの旧版画像を sOrigImages に保持し、sShowOriginal が ON の間その db のページに不透明 blit する。
	static std::map<UID, KESCMOrigImage*> sOrigImages;	// ページUID(新) → 旧版画像
	static IDataBase* sOrigDB;							// 旧版画像が属する単一ドキュメント(別dbに切替えたら作り直す)
	static bool16 sShowOriginal;						// べた載せ表示 ON/OFF(既定 OFF)
	static PMReal sOrigScale;							// 旧版画像をラスタ化した時の content→window スケール(ズーム×デバイス倍率)。
														// 再 peek 時にズームが変わっていたら作り直す基準。0=未設定
	static PMReal sPeekOpacity;							// 覗き中(peek)の旧版べた載せの不透明度。Shift＋ミドル=1.0(不透明)/
														// Ctrl＋ミドル=0.5(半透明)。(A2)描画ブロックが参照する

	// 一時トースト(画面=可視領域の中央に少し出て自動で消えるメッセージ)。マーク等とは完全に独立。
	// sToastDB のドキュメントの前面ビューにだけ描く。自動消去は IIdleTask(KESCMToastIdleTask)が担う。
	static PMString   sToastMsg;		// 表示文字列
	static bool16     sToastVisible;	// 表示中か(タイマで kFalse に戻す)
	static IDataBase* sToastDB;			// トーストを描くドキュメント(前面)

	// 距離変換 dist を使い、buf(ARGB)へリング(0<dist<=radius)を1パスで描く(★案A=膨張不要)。
	// 各リング画素の色は、その位置の背景が赤っぽい(bgRed[idx])なら青、そうでなければ赤。
	// リング以外の画素は透明(alpha=0)。dist は KESCMDistTransform で事前生成(0=変化画素)。
	static void BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
		const uint8* dist, const uint8* bgRed, int32 radius);

	// target/source を 72dpi ARGB でラスタ化→差分マスク作成。変化px数>0 のときだけ
	// sEntries[target.UID] にエントリ登録(既存は置換)。changed に「変化したか」を返す。
	static ErrorCode MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed);

	// sourceRef(旧)を resolution(dpi)で1枚だけラスタ化し、不透明画像を sOrigImages[target.UID] に
	// 保持(既存は置換)。オフスクリーンは即破棄=同時に1枚しか生存しない(安全)。
	// resolution 既定=kKESCMOrigResolution。peek 経路では現在のズームから dpi=72×スケールを渡して常にくっきり。
	static ErrorCode MakeOrigImage(const UIDRef& targetRef, const UIDRef& sourceRef, const PMReal& resolution = kKESCMOrigResolution);

	// 全エントリ破棄(kescmClearMarks / 別ドキュメント切替時)。
	static void DropAll()
	{
		for (std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.begin(); it != sEntries.end(); ++it)
			delete it->second;
		sEntries.clear();
		sDB = nil;
	}

	// 旧版画像を全破棄(kescmClearMarks / 別ドキュメント切替時)。表示トグルもOFFへ。
	static void DropAllOrig()
	{
		for (std::map<UID, KESCMOrigImage*>::iterator it = sOrigImages.begin(); it != sOrigImages.end(); ++it)
			delete it->second;
		sOrigImages.clear();
		sOrigDB = nil;
		sShowOriginal = kFalse;
		sOrigScale = 0.0;
	}
};

CREATE_PMINTERFACE(KESCMDrawEventHandler, kKESCMDrawEventHandlerImpl)

std::map<UID, KESCMOverlayEntry*> KESCMDrawEventHandler::sEntries;
IDataBase* KESCMDrawEventHandler::sDB = nil;
bool16 KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定=非表示。枠等はシングルミドル押下中だけ表示(master トグル)
bool16 KESCMDrawEventHandler::sShowPageX = kTrue;	// 既定=表示
bool16 KESCMDrawEventHandler::sShowOverset = kFalse;	// 既定=非表示(テスト機能。kescmShowOverset(true)で有効化)
std::map<UID, KESCMOrigImage*> KESCMDrawEventHandler::sOrigImages;
IDataBase* KESCMDrawEventHandler::sOrigDB = nil;
bool16 KESCMDrawEventHandler::sShowOriginal = kFalse;	// 既定=非表示(kescmShowOriginal で ON)
PMReal KESCMDrawEventHandler::sOrigScale = 0.0;	// ラスタ化時のズームスケール(0=未設定)
PMReal KESCMDrawEventHandler::sPeekOpacity = 1.0;	// 既定=不透明(Shift peek)。Ctrl peek で 0.5 にする
PMString   KESCMDrawEventHandler::sToastMsg;
bool16     KESCMDrawEventHandler::sToastVisible = kFalse;	// 既定=非表示
IDataBase* KESCMDrawEventHandler::sToastDB = nil;


void KESCMDrawEventHandler::BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
	const uint8* dist, const uint8* bgRed, int32 radius)
{
	if (buf == nil || dist == nil || wt <= 0 || ht <= 0 || bpp < 3)
		return;
	if (radius < 1) radius = 1;
	const int32 colorOff = bpp - 3;
	const uint8 rad = (radius > 255) ? 255 : (uint8)radius;	// dist は uint8 clamp255。半径上限は200<255。

	// ★案A: 距離変換の1パス塗り。リング = 0<dist<=radius(=「半径内に変化画素があり、かつ自身は変化画素でない」)。
	// 旧版の横膨張+縦膨張(各 O(W*H) のスライディングウィンドウ)が消え、ズーム段ごとの仕事が約1/3。
	// チェスボード距離ゆえ角型リングで形状は従来と同一。
	for (int32 y = 0; y < ht; ++y)
	{
		uint8* rowB = buf + (size_t)y * rb;
		const uint8* drow = dist + (size_t)y * wt;
		const uint8* brow = (bgRed != nil) ? (bgRed + (size_t)y * wt) : nil;
		for (int32 x = 0; x < wt; ++x)
		{
			uint8* pixT = rowB + (size_t)x * bpp;	// ARGB 先頭=alpha
			uint8* px = pixT + colorOff;
			const uint8 d = drow[x];
			if (d != 0 && d <= rad)
			{
				// リング画素。下の実ページが赤っぽければ青、そうでなければ赤(画素単位)。
				const bool useAlt = (brow != nil && brow[x]);
				px[0] = useAlt ? kKESCMRingAltR : kKESCMRingR;
				px[1] = useAlt ? kKESCMRingAltG : kKESCMRingG;
				px[2] = useAlt ? kKESCMRingAltB : kKESCMRingB;
				if (bpp >= 4) pixT[0] = kKESCMRingAlpha;	// 半透明
			}
			else { px[0] = 255; px[1] = 255; px[2] = 255; if (bpp >= 4) pixT[0] = 0; }	// 透明
		}
	}
}


//========================================================================================
// ヘルパ: マスク(0/1)を半径 radius で膨張して out(0/1)へ。BuildRing と同じ分離スライディング
//   ウィンドウ(O(W*H))。カウント用に近接変化を併合する目的。out は呼び出し側が確保(w*h)。
//========================================================================================
static void KESCMDilateMask(const uint8* mask, int32 wt, int32 ht, int32 radius, uint8* out)
{
	if (mask == nil || out == nil || wt <= 0 || ht <= 0)
		return;
	const size_t N = (size_t)wt * ht;
	if (radius < 1) { memcpy(out, mask, N); return; }
	uint8* H = new uint8[N];		// 横方向膨張の中間
	if (H == nil) { memcpy(out, mask, N); return; }
	// 横方向膨張
	for (int32 y = 0; y < ht; ++y)
	{
		const uint8* mrow = mask + (size_t)y * wt;
		uint8* hrow = H + (size_t)y * wt;
		int32 cnt = 0, lo = 0, hi = -1;
		for (int32 x = 0; x < wt; ++x)
		{
			const int32 wantHi = (x + radius >= wt) ? wt - 1 : x + radius;
			const int32 wantLo = (x - radius < 0) ? 0 : x - radius;
			while (hi < wantHi) { ++hi; cnt += mrow[hi]; }
			while (lo < wantLo) { cnt -= mrow[lo]; ++lo; }
			hrow[x] = (cnt > 0) ? 1 : 0;
		}
	}
	// 縦方向膨張(H → out)
	for (int32 x = 0; x < wt; ++x)
	{
		int32 cnt = 0, lo = 0, hi = -1;
		for (int32 y = 0; y < ht; ++y)
		{
			const int32 wantHi = (y + radius >= ht) ? ht - 1 : y + radius;
			const int32 wantLo = (y - radius < 0) ? 0 : y - radius;
			while (hi < wantHi) { ++hi; cnt += H[(size_t)hi * wt + x]; }
			while (lo < wantLo) { cnt -= H[(size_t)lo * wt + x]; ++lo; }
			out[(size_t)y * wt + x] = (cnt > 0) ? 1 : 0;
		}
	}
	delete[] H;
}


//========================================================================================
// ヘルパ: 差分マスク(0/1)の連結成分数を数える(=この頁の「変更=枠」の数)。
//   4近傍のフラッドフィル。固定半径で膨張したマスクに対して数えるのでズームに依らず一定。
//========================================================================================
static int32 KESCMCountComponents(const uint8* mask, int32 wt, int32 ht)
{
	if (mask == nil || wt <= 0 || ht <= 0)
		return 0;
	const size_t N = (size_t)wt * ht;
	std::vector<uint8> seen(N, 0);
	std::vector<int32> stack;
	int32 count = 0;
	for (int32 y0 = 0; y0 < ht; ++y0)
	{
		for (int32 x0 = 0; x0 < wt; ++x0)
		{
			const size_t s0 = (size_t)y0 * wt + x0;
			if (mask[s0] == 0 || seen[s0])
				continue;
			++count;
			stack.push_back((int32)s0);
			seen[s0] = 1;
			while (!stack.empty())
			{
				const int32 idx = stack.back(); stack.pop_back();
				const int32 x = idx % wt, y = idx / wt;
				// 4近傍。マスク=1 かつ未訪問なら同一成分。
				if (x > 0)      { const size_t n = idx - 1;  if (mask[n] && !seen[n]) { seen[n] = 1; stack.push_back((int32)n); } }
				if (x < wt - 1) { const size_t n = idx + 1;  if (mask[n] && !seen[n]) { seen[n] = 1; stack.push_back((int32)n); } }
				if (y > 0)      { const size_t n = idx - wt; if (mask[n] && !seen[n]) { seen[n] = 1; stack.push_back((int32)n); } }
				if (y < ht - 1) { const size_t n = idx + wt; if (mask[n] && !seen[n]) { seen[n] = 1; stack.push_back((int32)n); } }
			}
		}
	}
	return count;
}


//========================================================================================
// ヘルパ: 差分マスク(0/1)のチェスボード距離変換 → out(uint8, 0=変化画素, clamp255)。★案A。
//   各画素に「最も近い変化画素までのチェスボード距離(=max(|dx|,|dy|))」を入れる。リング描画は
//   out の閾値処理(0<out<=radius)だけで済む。8近傍・全コスト1の二パス chamfer(前進+後退)。
//   out は呼び出し側が確保(w*h)。
//========================================================================================
static void KESCMDistTransform(const uint8* mask, int32 wt, int32 ht, uint8* out)
{
	if (mask == nil || out == nil || wt <= 0 || ht <= 0)
		return;
	const size_t N = (size_t)wt * ht;
	for (size_t i = 0; i < N; ++i)
		out[i] = mask[i] ? 0 : (uint8)255;

	// 前進パス(左上→右下): 既処理の (左, 上, 左上, 右上) から +1。
	for (int32 y = 0; y < ht; ++y)
	{
		for (int32 x = 0; x < wt; ++x)
		{
			const size_t idx = (size_t)y * wt + x;
			if (out[idx] == 0) continue;
			int32 best = out[idx];
			if (x > 0)                    { int32 v = (int32)out[idx - 1]      + 1; if (v < best) best = v; }
			if (y > 0)                    { int32 v = (int32)out[idx - wt]     + 1; if (v < best) best = v; }
			if (y > 0 && x > 0)           { int32 v = (int32)out[idx - wt - 1] + 1; if (v < best) best = v; }
			if (y > 0 && x < wt - 1)      { int32 v = (int32)out[idx - wt + 1] + 1; if (v < best) best = v; }
			if (best > 255) best = 255;
			out[idx] = (uint8)best;
		}
	}
	// 後退パス(右下→左上): 既処理の (右, 下, 右下, 左下) から +1。
	for (int32 y = ht - 1; y >= 0; --y)
	{
		for (int32 x = wt - 1; x >= 0; --x)
		{
			const size_t idx = (size_t)y * wt + x;
			if (out[idx] == 0) continue;
			int32 best = out[idx];
			if (x < wt - 1)               { int32 v = (int32)out[idx + 1]      + 1; if (v < best) best = v; }
			if (y < ht - 1)               { int32 v = (int32)out[idx + wt]     + 1; if (v < best) best = v; }
			if (y < ht - 1 && x > 0)      { int32 v = (int32)out[idx + wt - 1] + 1; if (v < best) best = v; }
			if (y < ht - 1 && x < wt - 1) { int32 v = (int32)out[idx + wt + 1] + 1; if (v < best) best = v; }
			if (best > 255) best = 255;
			out[idx] = (uint8)best;
		}
	}
}


ErrorCode KESCMDrawEventHandler::MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed)
{
	changed = kFalse;
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// ★案1(ラスタ化 3→2): 旧版は別途 72dpi の target(snapL)もラスタ化していたが、その画素は
	//   BuildRing が buf を全上書きするため一切使われていなかった。低解像度の寸法は高解像度から割り戻し、
	//   背景の「赤っぽい」判定(bgRed)も高解像度 target をプーリングして作るので、snapL は不要=削除。
	// 【高解像度】差分検出用。target / source を高dpi(kKESCMResolution×kKESCMHiResMul)でラスタ化。
	// 低解像度では平均化で消える細線/微小ズレを満額の差分画素として拾い、取りこぼしを防ぐ。
	const PMReal hiRes = kKESCMResolution * kKESCMHiResMul;
	SnapshotUtilsEx* snapTH = new SnapshotUtilsEx(targetRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsRGB, kTrue);
	ErrorCode drewTH = snapTH->Draw(IShape::kPreviewMode);
	AGMImageAccessor* accTH = (drewTH == kSuccess) ? snapTH->CreateAGMImageAccessor() : nil;

	SnapshotUtilsEx* snapSH = new SnapshotUtilsEx(sourceRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsRGB, kTrue);
	ErrorCode drewSH = snapSH->Draw(IShape::kPreviewMode);
	AGMImageAccessor* accSH = (drewSH == kSuccess) ? snapSH->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (accTH != nil && accSH != nil)
	{
		// 高解像度(比較)の寸法・バッファ
		Int32Rect bth = accTH->GetBounds();
		Int32Rect bsh = accSH->GetBounds();
		const int32 wth = bth.right - bth.left, hth = bth.bottom - bth.top;
		const int32 wsh = bsh.right - bsh.left, hsh = bsh.bottom - bsh.top;
		const int32 rbTH = (int32)accTH->GetRowBytes();
		const int32 rbSH = (int32)accSH->GetRowBytes();
		const int32 bppH = (int32)accTH->GetBitsPerPixel() / 8;
		const uint8* ptH = accTH->GetBaseAddr();
		const uint8* psH = accSH->GetBaseAddr();

		// 低解像度(保存・表示)の寸法は高解像度から割り戻す。buf は ARGB の自前バッファ(行パディング無し)。
		int32 wl = ::ToInt32(::Round(PMReal(wth) / kKESCMHiResMul));
		int32 hl = ::ToInt32(::Round(PMReal(hth) / kKESCMHiResMul));
		if (wl < 1) wl = 1;
		if (hl < 1) hl = 1;
		const int32 bppL = bppH;			// 両スナップ addTransparencyAlpha=kTrue ゆえ ARGB(=4)想定
		const int32 rbL = wl * bppL;		// 自前バッファ=行パディング無し

		if (ptH != nil && psH != nil &&
			wth == wsh && hth == hsh && rbTH == rbSH && rbTH > 0 &&
			bppH >= 3 && wl > 0 && hl > 0)
		{
			const size_t N = (size_t)wl * hl;
			uint8*  M     = new uint8[N];	// 低解像度マスク(保存): プーリング結果
			uint16* cntHi = new uint16[N];	// 低解像度セルごとの「高解像度の変化画素数」(プーリング用一時)
			if (M != nil && cntHi != nil)
			{
				memset(cntHi, 0, N * sizeof(uint16));

				// 【高解像度で比較 → 低解像度セルへ散らす(scatter)】
				// 高解像度の各画素を差分判定(生RGB最大チャンネル差>しきい値)し、変化していたら
				// 対応する低解像度セルのカウンタを増やす。セル写像は寸法比(高/低が整数倍でなくてもよい)。
				const int32 colorOffH = bppH - 3;
				for (int32 y = 0; y < hth; ++y)
				{
					const uint8* rowT = ptH + (size_t)y * rbTH;
					const uint8* rowS = psH + (size_t)y * rbTH;
					int32 yl = (int32)((int64)y * hl / hth);
					if (yl >= hl) yl = hl - 1;
					uint16* cntRow = cntHi + (size_t)yl * wl;
					for (int32 x = 0; x < wth; ++x)
					{
						const uint8* px = rowT + (size_t)x * bppH + colorOffH;
						const uint8* sx = rowS + (size_t)x * bppH + colorOffH;
						const int dR = (px[0] > sx[0]) ? px[0] - sx[0] : sx[0] - px[0];
						const int dG = (px[1] > sx[1]) ? px[1] - sx[1] : sx[1] - px[1];
						const int dB = (px[2] > sx[2]) ? px[2] - sx[2] : sx[2] - px[2];
						int cm = dR; if (dG > cm) cm = dG; if (dB > cm) cm = dB;
						if (cm > kKESCMDiffThr)
						{
							int32 xl = (int32)((int64)x * wl / wth);
							if (xl >= wl) xl = wl - 1;
							if (cntRow[xl] < 0xFFFF) ++cntRow[xl];
						}
					}
				}

				// 【マックスプーリング】セル内の高解像度変化画素が min-count 以上なら低解像度マスク=1。
				// 1個でも(min=1)立てれば取りこぼしゼロ。min を上げると縁ノイズ耐性が増す。
				size_t diffCount = 0;
				for (size_t i = 0; i < N; ++i)
				{
					uint8 m = (cntHi[i] >= (uint16)kKESCMPoolMinCount) ? 1 : 0;
					M[i] = m;
					if (m) ++diffCount;
				}
				delete[] cntHi; cntHi = nil;

				if (diffCount == 0)
				{
					// 変化なし: エントリを作らない。
					delete[] M;
					status = kSuccess;	// 成功・ただし changed=false
				}
				else
				{
					// 背景(対象ページ)の「赤っぽい」画素マップを、高解像度 target をプーリングして作る
					// (案1: 低解像度 snapL を廃止。低解像度セル中心の高解像度画素1点を代表サンプルに)。
					const int32 colorOffT = bppH - 3;
					uint8* BG = new uint8[N];
					if (BG != nil)
					{
						for (int32 y = 0; y < hl; ++y)
						{
							int32 yh = (int32)(((int64)y * hth + hth / 2) / hl);
							if (yh >= hth) yh = hth - 1;
							const uint8* rowT = ptH + (size_t)yh * rbTH;
							for (int32 x = 0; x < wl; ++x)
							{
								int32 xh = (int32)(((int64)x * wth + wth / 2) / wl);
								if (xh >= wth) xh = wth - 1;
								const uint8* px = rowT + (size_t)xh * bppH + colorOffT;
								const int r = px[0], g = px[1], b = px[2];
								BG[(size_t)y * wl + x] = (r - g > kKESCMRedBgDom && r - b > kKESCMRedBgDom) ? 1 : 0;
							}
						}
					}

					// ★buf を指す自前 AGMImageRecord を組んで切り離す(buf は下で BuildRing が全画素を書くので
					//   ラスタ画素のコピーは不要)。SnapshotUtilsEx / accessor は保持しない(下で即破棄)。
					//   GetAGMImageRecord も呼ばない=破棄時クラッシュ(保持 accessor の delete)を根本回避。
					KESCMOverlayEntry* e = new KESCMOverlayEntry();
					e->w = wl;  e->h = hl;  e->rowBytes = rbL;  e->bpp = bppL;
					e->bgRed = BG;  e->lastRadius = kKESCMBaseRadius;
					// この頁の変更(枠)の数。生 M をそのまま数えると、文字変更で各グリフ片が
					// 別成分になり数が膨大になる。固定半径で膨張して近接変化を併合してから数え、
					// 見た目の赤い塊(リング)の数に近づける。
					{
						uint8* Dn = new uint8[N];	// 併合用の一時マスク(1byte/px)
						if (Dn != nil)
						{
							KESCMDilateMask(M, wl, hl, kKESCMCountMergeRadius, Dn);
							e->changeCount = KESCMCountComponents(Dn, wl, hl);
							delete[] Dn;
						}
						else
						{
							e->changeCount = KESCMCountComponents(M, wl, hl);	// 確保失敗時は生 M
						}
					}
					// ★案A: mask M から距離変換 dist を1回だけ作って保持(以後の BuildRing はこれ1つで描ける)。
					//   dist 生成後、mask M はもう不要なので解放(常駐メモリは dist が mask を置換=純増ゼロ)。
					e->dist = new uint8[N];
					if (e->dist != nil)
						KESCMDistTransform(M, wl, hl, e->dist);
					delete[] M;

					// 初回リング(基準半径)を buf へ直接描く(dist 確保失敗時のみ透明クリアで安全に)。
					// buf 確保失敗(nil)時はここでは触らない。描画側(HandleDrawEvent)が e->buf==nil で skip する。
					e->buf = new uint8[(size_t)rbL * hl];
					if (e->buf != nil)
					{
						if (e->dist != nil)
							BuildRing(e->buf, rbL, bppL, wl, hl, e->dist, BG, kKESCMBaseRadius);
						else
							memset(e->buf, 0, (size_t)rbL * hl);
					}
					e->rec.bounds.xMin = 0;             e->rec.bounds.yMin = 0;
					e->rec.bounds.xMax = (int16)wl;     e->rec.bounds.yMax = (int16)hl;
					e->rec.baseAddr     = e->buf;
					e->rec.byteWidth    = rbL;
					// ARGB(alpha 先頭)。HasAlpha フラグを立てないと透明画素が不透明白で描かれる。
					// 既定が ARGB 順なので SwapAlpha は不要(RGBA なら | kColorSpaceSwapAlpha)。
					e->rec.colorSpace   = (int16)(kRGBColorSpace | kColorSpaceHasAlpha);
					e->rec.bitsPerPixel = (int16)(bppL * 8);
					e->rec.decodeArray  = nil;
					e->rec.colorTab.numColors = 0;  e->rec.colorTab.theColors = nil;

					// 既存エントリがあれば置換。
					UID key = targetRef.GetUID();
					std::map<UID, KESCMOverlayEntry*>::iterator old = sEntries.find(key);
					if (old != sEntries.end()) { delete old->second; sEntries.erase(old); }
					sEntries[key] = e;

					// dist / bgRed / buf は entry が所有(mask M は dist 生成後に解放済み)。スナップショットは下の後始末で即破棄。
					changed = kTrue;
					status = kSuccess;
				}
			}
			else
			{
				if (M)     delete[] M;
				if (cntHi) delete[] cntHi;
			}
		}
	}

	// 後始末: 2つのスナップショット/アクセサを破棄(案1でラスタ化は2回=低解像度 snapL は廃止)。
	if (accSH)  delete accSH;
	if (snapSH) delete snapSH;
	if (accTH)  delete accTH;
	if (snapTH) delete snapTH;
	return status;
}


ErrorCode KESCMDrawEventHandler::MakeOrigImage(const UIDRef& targetRef, const UIDRef& sourceRef, const PMReal& resolution)
{
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// source(旧)を resolution(dpi)で不透明ラスタ化。addTransparencyAlpha=kFalse=ページを不透明に描く(べた載せ用)。
	// オフスクリーンは1枚だけ。画素を自前 buf へコピーしたら即破棄(下)＝同時に複数生存しない=安全。
	SnapshotUtilsEx* snap = new SnapshotUtilsEx(sourceRef, 1.0, 1.0, resolution, resolution, 0.0, SnapshotUtilsEx::kCsRGB, kFalse);
	ErrorCode drew = snap->Draw(IShape::kPreviewMode);
	AGMImageAccessor* acc = (drew == kSuccess) ? snap->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (acc != nil)
	{
		Int32Rect b = acc->GetBounds();
		const int32 w = b.right - b.left, h = b.bottom - b.top;
		const int32 rb = (int32)acc->GetRowBytes();
		const int32 bpp = (int32)acc->GetBitsPerPixel() / 8;
		const uint8* p = acc->GetBaseAddr();
		// AGMImageRecord.bounds は int16。300dpi で超大型ページ(幅/高さ>32767px≒109inch)だと破綻するので弾く。
		if (p != nil && w > 0 && h > 0 && rb > 0 && bpp >= 3 && b.right <= 32767 && b.bottom <= 32767)
		{
			KESCMOrigImage* o = new KESCMOrigImage();
			o->w = w;  o->h = h;  o->rowBytes = rb;  o->bpp = bpp;
			o->buf = new uint8[(size_t)rb * h];
			memcpy(o->buf, p, (size_t)rb * h);
			// 不透明保証: ARGB(alpha 先頭)なら alpha を 255 に揃える(べた載せ=下が透けない)。
			// ★案D: まず格子状(約8×8点)にサンプリングし、全サンプルが既に 255(不透明)なら O(W*H) の
			//   全画素ループを丸ごと省く。ラスタが既に不透明(addTransparencyAlpha=kFalse)なら書き込みを回避。
			//   サンプルに非255が1つでもあれば従来どおり全画素を 255 に揃える(自己補正=どちらでも正しい)。
			if (bpp >= 4)
			{
				bool16 alreadyOpaque = kTrue;
				const int32 sy = (h > 8) ? h / 8 : 1;
				const int32 sx = (w > 8) ? w / 8 : 1;
				for (int32 y = 0; y < h && alreadyOpaque; y += sy)
				{
					const uint8* row = o->buf + (size_t)y * rb;
					for (int32 x = 0; x < w; x += sx)
						if (row[(size_t)x * bpp] != 255) { alreadyOpaque = kFalse; break; }
				}
				if (!alreadyOpaque)
				{
					for (int32 y = 0; y < h; ++y)
					{
						uint8* row = o->buf + (size_t)y * rb;
						for (int32 x = 0; x < w; ++x)
							row[(size_t)x * bpp] = 255;
					}
				}
			}
			o->rec.bounds.xMin = (int16)b.left;   o->rec.bounds.yMin = (int16)b.top;
			o->rec.bounds.xMax = (int16)b.right;  o->rec.bounds.yMax = (int16)b.bottom;
			o->rec.baseAddr     = o->buf;
			o->rec.byteWidth    = rb;
			o->rec.colorSpace   = (int16)((bpp >= 4) ? (kRGBColorSpace | kColorSpaceHasAlpha) : kRGBColorSpace);
			o->rec.bitsPerPixel = (int16)acc->GetBitsPerPixel();
			o->rec.decodeArray  = nil;
			o->rec.colorTab.numColors = 0;  o->rec.colorTab.theColors = nil;

			// 既存があれば置換。
			UID key = targetRef.GetUID();
			std::map<UID, KESCMOrigImage*>::iterator old = sOrigImages.find(key);
			if (old != sOrigImages.end()) { delete old->second; sOrigImages.erase(old); }
			sOrigImages[key] = o;
			status = kSuccess;
		}
	}

	if (acc)  delete acc;
	if (snap) delete snap;
	return status;
}


void KESCMDrawEventHandler::Register(IDrwEvtDispatcher* d)
{
	// スプレッド単位で配られる描画イベント。ポートは spread 座標。
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
}

void KESCMDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
}


//========================================================================================
// 【テスト】オーバーセット目印: 角の点(spread座標 cornerX,cornerY)に「白塗りの四角＋赤い十字」をズーム不変で描く。
// 白塗りは枠の外へ少しはみ出して外周の白リムにし(背景に埋もれない)、その上に赤い枠と十字を描く。
// マークは常に軸そろえ(回転しない)。leftSide=kTrue なら角から左下へ伸ばす(縦書き=左下角用)、kFalse は右下へ。
// 線は spread 座標のまま stroke。
//========================================================================================
static void KESCMDrawOversetMarker(IGraphicsPort* gPort, const PMReal& cornerX, const PMReal& cornerY, bool16 leftSide, PMReal sxr)
{
	const PMReal S      = (sxr > 0) ? (kKESCMOversetPx     / sxr) : kKESCMOversetPx;
	const PMReal lwBody = (sxr > 0) ? (kKESCMOversetLinePx / sxr) : kKESCMOversetLinePx;
	const PMReal h      = (sxr > 0) ? (kKESCMOversetHaloPx / sxr) : kKESCMOversetHaloPx;	// 白リムのはみ出し

	// 角の点を基準に外側へ伸ばす。横書き=右下(右へ)、縦書き=左下(左へ)。下方向は共通。
	const PMReal x0 = leftSide ? (cornerX - S) : cornerX;
	const PMReal y0 = cornerY;
	const PMReal x1 = x0 + S, y1 = y0 + S;
	const PMReal cx = (x0 + x1) / PMReal(2.0), cy = (y0 + y1) / PMReal(2.0);
	const PMReal ins = S * kKESCMOversetPlusInsetFrac;	// 十字の腕の内側余白

	AutoGSave ag(gPort);
	gPort->setopacity(PMReal(1.0), kFalse);

	// 白塗り(枠より h だけ各辺はみ出す=外周の白リム兼 内部の白地)。
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->rectfill(x0 - h, y0 - h, S + PMReal(2.0) * h, S + PMReal(2.0) * h);

	// 赤い枠。
	gPort->setrgbcolor(PMReal(1.0), PMReal(0.0), PMReal(0.0));
	gPort->setlinewidth(lwBody);
	gPort->rectpath(x0, y0, S, S);
	gPort->stroke();

	// 赤い十字(白地の上)。
	gPort->newpath();
	gPort->moveto(cx, y0 + ins); gPort->lineto(cx, y1 - ins);	// 縦
	gPort->stroke();
	gPort->newpath();
	gPort->moveto(x0 + ins, cy); gPort->lineto(x1 - ins, cy);	// 横
	gPort->stroke();
}

// このスプレッドの全ページアイテムを走査し、オーバーセットのテキストフレームに目印を描く。
static void KESCMDrawOversetForSpread(IGraphicsPort* gPort, IDataBase* db, ISpread* spread, PMReal sxr)
{
	if (gPort == nil || db == nil || spread == nil)
		return;
	const int32 np = spread->GetNumPages();
	for (int32 p = 0; p < np; ++p)
	{
		UIDList items(db);
		// このページが「所有」するアイテム(ページ自身・ペーストボードは除外)。
		spread->GetItemsOnPage(p, &items, kFalse, kFalse, kFalse);
		const int32 cnt = items.Length();
		for (int32 k = 0; k < cnt; ++k)
		{
			InterfacePtr<IGeometry> geo(db, items[k], UseDefaultIID());
			if (geo == nil)
				continue;
			int32 tflags = 0;
			// IsTextFrame の textflags にイン/アウト/オーバーセット状態が入る(kTF_Overset=4)。
			if (!Utils<IFrameUtils>()->IsTextFrame(geo, &tflags))
				continue;
			if (!(tflags & IFrameUtils::kTF_Overset))
				continue;

			// 縦書きか? フレーム→ストーリー(text model)の IStoryOptions::IsVertical() を per-story で見る。
			// (ITextOptions の書字方向は文書既定しか返らず使えなかった。)
			// 縦書き=あふれの出口(アウトポート)は左下、横書き=右下。
			bool16 vertical = kFalse;
			UID tfUID = Utils<IFrameUtils>()->GetTextFrameUID(geo);
			if (tfUID != kInvalidUID)
			{
				InterfacePtr<IMultiColumnTextFrame> mctf(db, tfUID, UseDefaultIID());
				if (mctf != nil)
				{
					InterfacePtr<IStoryOptions> so(db, mctf->GetTextModelUID(), UseDefaultIID());
					if (so != nil && so->IsVertical())
						vertical = kTrue;
				}
			}

			// フレーム inner bbox の「下の角の点」を spread 座標へ変換。
			// ※矩形ごと変換すると回転時に軸そろえbboxになり角がずれる。点で変換すれば
			//   回転後の実際の角(アウトポート位置)が得られる。マーク自身は軸そろえで描く。
			PMRect pr = geo->GetPathBoundingBox();		// inner 座標(未回転)
			PMMatrix m = ::InnerToSpreadMatrix(geo);		// 回転・移動を含む
			PMPoint corner(vertical ? pr.Left() : pr.Right(), pr.Bottom());	// 縦=左下 / 横=右下
			m.Transform(&corner);							// → spread 座標(回転反映)
			KESCMDrawOversetMarker(gPort, corner.X(), corner.Y(), vertical, sxr);
		}
	}
}

// ビューから IPanorama を取る。ページアイテム系の子ウィジェットは panorama を持たないため、
// CTracker::QueryPanorama と同じく自身→親(LayoutWidget)の順で辿る。呼び出し側で Release すること。
static IPanorama* KESCMQueryPanorama(IControlView* view)
{
	if (view == nil)
		return nil;
	IPanorama* pano = (IPanorama*)view->QueryInterface(IID_IPANORAMA);
	if (pano != nil)
		return pano;
	InterfacePtr<IWidgetParent> parent(view, IID_IWIDGETPARENT);
	if (parent == nil)
		return nil;
	return (IPanorama*)parent->QueryParentFor(IID_IPANORAMA);
}

// 変更数テキストのサイズ倍率 M(uiZoom)。2点 (zoomLo,mulLo)(zoomHi,mulHi) を通す反比例 M = a - b/zoom。
// b = (mulHi-mulLo)/(1/zoomLo - 1/zoomHi)、a = mulHi + b/zoomHi。範囲外は [mulLo, mulHi] にクランプ。
// uiZoom<=0(ビュー/パノラマ不明)なら 1.0(=現状サイズ)を返す。
static PMReal KESCMCountSizeMul(PMReal uiZoom)
{
	if (uiZoom <= 0.0)
		return PMReal(1.0);
	const PMReal invLo = PMReal(1.0) / kKESCMCountZoomLo;
	const PMReal invHi = PMReal(1.0) / kKESCMCountZoomHi;
	const PMReal b = (kKESCMCountMulHi - kKESCMCountMulLo) / (invLo - invHi);
	const PMReal a = kKESCMCountMulHi + b * invHi;
	PMReal m = a - b / uiZoom;
	if (m < kKESCMCountMulLo) m = kKESCMCountMulLo;	// 5%未満でも 0.8倍で下げ止め
	if (m > kKESCMCountMulHi) m = kKESCMCountMulHi;	// 100%超でも 3倍で頭打ち(青天井にしない)
	return m;
}

// トースト用の文字列幅の概算(em 単位の合計)。SDK に「選択フォントで任意文字列を実測する」軽量 API が
// 無いため、文字種別の代表値を合計して近似する(プロポーショナルフォントで一律 0.5em より実幅に近い)。
// 大文字・記号は広め、小文字は中、スペースは狭め、非 ASCII(全角=CJK 等)は約 1em とみなす。
static PMReal KESCMEstimateTextEm(const UTF16TextChar* buf, int32 n)
{
	PMReal sum = 0.0;
	for (int32 i = 0; i < n; ++i)
	{
		const UTF16TextChar c = buf[i];
		PMReal w;
		if (c == 0x20)                      w = PMReal(0.30);	// 半角スペース
		else if (c >= 0x41 && c <= 0x5A)    w = PMReal(0.80);	// 大文字 A-Z(広め。右端の詰まり対策で少し大きめ)
		else if (c >= 0x61 && c <= 0x7A)    w = PMReal(0.50);	// 小文字 a-z
		else if (c >= 0x30 && c <= 0x39)    w = PMReal(0.55);	// 数字 0-9
		else if (c < 0x80)                  w = PMReal(0.50);	// その他 ASCII(記号等)
		else                                w = PMReal(1.00);	// 非 ASCII(全角=CJK 等)
		sum = sum + w;
	}
	return sum;
}

//========================================================================================
// 一時トースト描画: 前面ビューの可視領域中央に、半透明の暗いボックス＋白文字でメッセージを描く。
//   画面中央の content(pasteboard) 点を求め、それを含むスプレッドの描画イベントでだけ1回描く
//   (複数スプレッドが見えていても二重に出さない)。サイズはズーム不変(画面px / sxr)。
//========================================================================================
static void KESCMDrawToast(IGraphicsPort* gPort, IDataBase* db, ISpread* spread, IControlView* view, PMReal sxr)
{
	if (!KESCMDrawEventHandler::sToastVisible)
		return;
	if (db == nil || db != KESCMDrawEventHandler::sToastDB)
		return;
	if (gPort == nil || spread == nil || view == nil || sxr <= 0)
		return;

	InterfacePtr<IPanorama> pano(KESCMQueryPanorama(view));
	if (pano == nil)
		return;

	// 可視領域の中心(content=pasteboard 座標)を IPanorama から直接取得する。view と panorama が別ウィジェット
	// でも確実(GetFrame の手計算が不要)。GetContentLocationAtFrameCenter() = 可視領域中心の content 座標。
	PMPoint center = pano->GetContentLocationAtFrameCenter();
	const PMReal cX = center.X();
	const PMReal cY = center.Y();

	// この中心が「このスプレッド」のいずれかのページ(pasteboard bbox)内にあるか? あれば content→spread の
	// 平行移動を求める(spread は pasteboard 内で軸そろえ=平行移動のみ)。無ければ別スプレッドが描く。
	const int32 np = spread->GetNumPages();
	bool16 here = kFalse;
	PMReal sX = 0, sY = 0;
	for (int32 p = 0; p < np; ++p)
	{
		InterfacePtr<IGeometry> geo(db, spread->GetNthPageUID(p), UseDefaultIID());
		if (geo == nil)
			continue;
		PMRect inner = geo->GetPathBoundingBox();
		PMRect pb = inner;  PMMatrix mPB = ::InnerToPasteboardMatrix(geo);  mPB.Transform(&pb);
		PMReal L = pb.Left(), R = pb.Right(), T = pb.Top(), B = pb.Bottom();
		if (L > R) { PMReal t = L; L = R; R = t; }
		if (T > B) { PMReal t = T; T = B; B = t; }
		if (cX >= L && cX <= R && cY >= T && cY <= B)
		{
			PMRect sp = inner;  PMMatrix mS = ::InnerToSpreadMatrix(geo);  mS.Transform(&sp);
			sX = cX + (sp.Left() - pb.Left());	// content → spread(平行移動)
			sY = cY + (sp.Top()  - pb.Top());
			here = kTrue;
			break;
		}
	}
	if (!here)
		return;

	PMString msg = KESCMDrawEventHandler::sToastMsg;
	msg.SetTranslatable(kFalse);
	const int32 nch = msg.NumUTF16TextChars();
	if (nch <= 0)
		return;
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IPMFont> theFont(fontMgr ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil);
	if (theFont == nil)
		return;

	const PMReal fpt   = kKESCMToastTextPx / sxr;			// 文字pt(ズーム不変)
	const PMReal pad   = kKESCMToastPadPx  / sxr;
	const UTF16TextChar* mbuf = msg.GrabUTF16Buffer(nil);	// 文字バッファ(幅見積もりと描画で共用)
	const PMReal textW = fpt * KESCMEstimateTextEm(mbuf, nch);	// 文字種別の概算幅合計(大文字広め/小文字狭め)
	const PMReal boxW  = textW + pad * PMReal(2.0);
	const PMReal boxH  = fpt   + pad * PMReal(2.0);
	const PMReal x0    = sX - boxW / PMReal(2.0);
	const PMReal y0    = sY - boxH / PMReal(2.0);

	AutoGSave ag(gPort);
	// 背景: ほぼ不透明の暗いボックス(下の青線/ガイドが透けてまだらにならないよう不透明寄りに)。
	gPort->setopacity(PMReal(0.92), kFalse);
	gPort->setrgbcolor(PMReal(0.10), PMReal(0.10), PMReal(0.10));
	gPort->rectfill(x0, y0, boxW, boxH);
	// 細い白枠。
	gPort->setopacity(PMReal(1.0), kFalse);
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->setlinewidth(PMReal(1.0) / sxr);
	gPort->rectpath(x0, y0, boxW, boxH);
	gPort->stroke();
	// 白文字(中央)。show は baseline 左端基準。縦中央 ≒ y0 + pad + fpt*0.78。
	gPort->selectfont(theFont, fpt);
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	const PMReal tx = sX - textW / PMReal(2.0);
	const PMReal ty = y0 + pad + fpt * PMReal(0.78);
	gPort->show(tx, ty, nch, mbuf, (IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
}

bool16 KESCMDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;
	if (ded->flags & IShape::kPrinting)		// 画面のみ(印刷/PDF には描かない)
		return kFalse;
	if (ded->flags & IShape::kPreviewMode)	// スナップショット描画には乗らない(自己参照防止)
		return kFalse;
	// マークも overset も 旧版べた載せ も トースト も無ければ何もしない。
	if (sEntries.empty() && !sShowOverset && !(sShowOriginal && !sOrigImages.empty()) && !sToastVisible)
		return kFalse;

	GraphicsData* gd = ded->gd;
	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;

	// changedBy = 今描いているスプレッド。
	InterfacePtr<ISpread> spread(ded->changedBy, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	IDataBase* db = ::GetDataBase(ded->changedBy);
	if (db == nil)
		return kFalse;

	// 画面スケール(ズーム)を一度だけ取得。画面描画時のみ非nil。
	PMReal sxr = 0.0;
	PMReal countMul = 1.0;	// 変更数テキストのサイズ倍率(拡大率連動)。ビュー/パノラマ不明時は 1.0(=現状サイズ)
	IControlView* zview = gd->GetView();
	InterfacePtr<IPanorama> pano;	// 可視上端(縦位置)と UIズーム(文字倍率)の両方に使う。画面描画時のみ非nil
	if (zview != nil)
	{
		PMMatrix toWin = zview->GetContentToWindowMatrix();	// content→window(画面px), 現ズーム
		sxr = toWin.GetXScale(); if (sxr < 0) sxr = -sxr;
		pano.reset(KESCMQueryPanorama(zview));	// 自身→親(LayoutWidget)で IPanorama を辿る(attach=addref済みを所有)
		if (pano != nil)
		{
			// UIズーム値=モニタ倍率を含まない「ユーザーに見える拡大率」(5%→0.05, 100%→1.0)。
			// sxr(=ズーム×デバイス倍率)ではなくこちらでサイズ倍率を決める(ユーザー指定の 5%/100% に合わせる)。
			const PMReal uiZoom = pano->GetXScaleFactor(kFalse);
			countMul = KESCMCountSizeMul(uiZoom);
		}
	}

	// 今描いている「このスプレッド」を覗いている(旧版べた載せ中)か。覗きで旧版が乗るのはマウス下の1スプレッド
	// だけ(そのページが sOrigImages にある)。覗き中のスプレッドだけ旧版をきれいに見せたいので、マーク
	// (＋/枠/X/変更数)を描かない。それ以外のスプレッドは通常どおりマークを描く。
	bool16 peekingThisSpread = kFalse;
	if (sShowOriginal && !sOrigImages.empty() && sOrigDB != nil && db == sOrigDB)
	{
		const int32 npChk = spread->GetNumPages();
		for (int32 i = 0; i < npChk; ++i)
			if (sOrigImages.find(spread->GetNthPageUID(i)) != sOrigImages.end())
			{ peekingThisSpread = kTrue; break; }
	}

	// (A) 【テスト】オーバーセット目印 — マーク(sEntries)とは独立。ONなら毎回その時々の overset を検出して描く。
	// master 表示トグル(sMarksVisible)が OFF の間、またはこのスプレッドを覗き中は描かない。
	if (sShowOverset && sMarksVisible && !peekingThisSpread)
		KESCMDrawOversetForSpread(gPort, db, spread, sxr);

	// (A2) 旧版べた載せ — マーク(sEntries)とは独立。覗き中のスプレッドの各ページに旧版画像を不透明で
	// ページ矩形いっぱいに blit する。
	if (peekingThisSpread)
	{
		const int32 npo = spread->GetNumPages();
		for (int32 i = 0; i < npo; ++i)
		{
			const UID pageUID = spread->GetNthPageUID(i);
			std::map<UID, KESCMOrigImage*>::iterator it = sOrigImages.find(pageUID);
			if (it == sOrigImages.end())
				continue;
			KESCMOrigImage* o = it->second;
			if (o == nil || o->buf == nil || o->w <= 0 || o->h <= 0)
				continue;
			InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
			if (pageGeo == nil)
				continue;
			PMRect pr = pageGeo->GetPathBoundingBox();		// ページ inner
			PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
			m.Transform(&pr);								// → spread(=描画ポート)座標
			AutoGSave ag(gPort);
			gPort->setopacity(sPeekOpacity, kFalse);		// Shift peek=1.0(不透明) / Ctrl peek=0.5(半透明)
			gPort->translate(pr.Left(), pr.Top());
			gPort->scale(pr.Width() / o->w, pr.Height() / o->h);	// 旧版画像をページ矩形にフィット
			gPort->image(&o->rec, PMMatrix(), 0);			// 旧版を sPeekOpacity で重ねる
		}
	}

	// (A3) 一時トースト(画面中央) — マーク/旧版とは独立。前面ドキュメントの可視領域中央に1回描く。
	// 覗き中(旧版べた載せ)や枠非表示でも常に出す(下の (B) の早期 return より前で描く)。
	KESCMDrawToast(gPort, db, spread, zview, sxr);

	// (B) 変更オーバーレイ(リング＋X＋変更数) — マーク済みドキュメントが現スプレッドの db と一致する時だけ。
	// master 表示トグル(sMarksVisible)が OFF の間、またはこのスプレッドを覗き中(旧版べた載せ中)は描かない
	// (データは保持=再表示で即復帰)。覗いていない他のスプレッドのマークは通常どおり残る。
	if (peekingThisSpread || !sMarksVisible || sEntries.empty() || sDB == nil || db != sDB)
		return kFalse;

	// このスプレッドの各ページについて、エントリがあれば描く。
	const int32 np = spread->GetNumPages();
	for (int32 i = 0; i < np; ++i)
	{
		const UID pageUID = spread->GetNthPageUID(i);
		std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.find(pageUID);
		if (it == sEntries.end())
			continue;
		KESCMOverlayEntry* e = it->second;
		if (e == nil || e->buf == nil)
			continue;

		const int32 iw = e->w, ih = e->h;
		InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
		if (iw <= 0 || ih <= 0 || pageGeo == nil)
			continue;

		// 【座標の肝】kEndSpreadMessage の描画ポートは spread 座標。ページ inner bbox を
		// InnerToSpreadMatrix で spread 座標へ変換してフィットさせる。
		PMRect pr = pageGeo->GetPathBoundingBox();			// ページ inner
		PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
		m.Transform(&pr);									// → spread(=描画ポート)座標

		// 【リング太さのズーム適応】このページの実寸と現ズームから「画面 kKESCMRingTargetPx 相当」の
		// 膨張半径(画像px)を逆算。前回と違えば描き直し。拡大時は下限(2)に張り付くので再計算が止まる。
		if (e->dist != nil && sxr > 0)
		{
			PMReal denom = (pr.Width() / PMReal(iw)) * sxr;		// 画面px / 画像px
			if (denom > PMReal(0.0001))
			{
				int32 R = ::ToInt32(::Round(kKESCMRingTargetPx / denom));
				if (R < 2) R = 2;									// 最小2px(量子化後は最小4px)
				if (R > 200) R = 200;								// 過大膨張の上限
				// ★案B: 量子化を 2px→4px 刻みに。ズーム中に R が変わる回数(=BuildRing 再計算)がほぼ半減。
				// 代償=太さの段階がやや粗い。最小は 4、200 は 200 に丸まる。
				R = ((R + 2) / 4) * 4;								// 4px 量子化
				if (R != e->lastRadius)
				{
					BuildRing(e->buf, e->rowBytes, e->bpp, e->w, e->h, e->dist, e->bgRed, R);
					e->lastRadius = R;
				}
			}
		}

		// 【描画順】まず枠の画像(リング)を blit し、その上に × と枠の数を重ねる。
		// translate/scale はこの gsave 内だけ。閉じれば spread 座標に戻るので後続の ×/数 はそのまま描ける。
		{
			AutoGSave ag(gPort);
			gPort->translate(pr.Left(), pr.Top());				// ページ左上へ
			gPort->scale(pr.Width() / iw, pr.Height() / ih);	// リング画像をページ矩形にフィット
			gPort->image(&e->rec, PMMatrix(), 0);				// 自前レコード(buf を指す)を blit
		}

		// この頁の変更数「N chg」を常時表示(★Xは廃止・トグル無し=sShowPageX は参照しない)。リング画像の上に重ねる。
		// 文字サイズは拡大率連動(画面px = 既定px × countMul、px→pt は /sxr)。framelabel 流: session→IFontMgr→
		// 既定フォントを selectfont し show(数字=白ストローク縁＋赤fill＋赤ストローク(やや太字)、語=青fill のみ)。
		{
			AutoGSave agx(gPort);
			if (e->changeCount > 0)
			{
				InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
				InterfacePtr<IPMFont> theFont(fontMgr ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil);
				if (theFont != nil)
				{
					// 画面pxサイズに拡大率連動の倍率 countMul を掛けてから px→pt(/sxr)へ。
					const PMReal numPt  = (sxr > 0) ? (kKESCMCountTextPx * countMul / sxr) : (pr.Width() / PMReal(24));
					const PMReal wordPt = (sxr > 0) ? (kKESCMCountWordPx * countMul / sxr) : (pr.Width() / PMReal(48));
					const PMReal inset  = (sxr > 0) ? (kKESCMCountInsetPx / sxr) : (pr.Width() / PMReal(80));

					// 数字と、その後ろの語("chg")を別サイズで描く。
					PMString numStr;  numStr.SetTranslatable(kFalse);  numStr.AppendNumber(e->changeCount);
					PMString wordStr; wordStr.SetTranslatable(kFalse);
					wordStr.Append(" chg");	// 先頭空白で数字と間隔
					const int32 numCh  = numStr.NumUTF16TextChars();
					const int32 wordCh = wordStr.NumUTF16TextChars();

					// 概算幅(≒0.5em/字)。数字+語をひとまとまりとして横中央に置く。show は baseline 左端基準。
					const PMReal numW  = numPt  * PMReal(0.5) * PMReal(numCh);
					const PMReal wordW = wordPt * PMReal(0.5) * PMReal(wordCh);
					const PMReal startX = (pr.Left() + pr.Right()) / PMReal(2.0) - (numW + wordW) / PMReal(2.0);

					// 【縦位置=拡大率に追従】可視範囲の上端を IPanorama から content(pasteboard)座標で取り、spread 座標へ
					// 換算して「ページ上端」と「可視上端」の下にある方を基準にする。拡大でページ上端が画面外に出ても
					// 数が画面内の上方に残る(縮小時/パノラマ不明時はページ上端=従来挙動)。横位置は従来どおりページ横中央。
					// pano->GetContentLocationAtFrameOrigin() = 「フレーム左上に来ている content 座標の点」=スクロール/
					// ズームを反映した可視上端。旧 zview->GetFrame().Top() は親(frame)座標で content 座標ではないため
					// 拡大時に基準がずれて数値が画面外/不可視になっていた(IPanorama で修正)。
					PMReal anchorTop = pr.Top();
					if (pano != nil)	// トップで取得済みの panorama を再利用
					{
						PMRect pagePB = pageGeo->GetPathBoundingBox();
						PMMatrix mpb = ::InnerToPasteboardMatrix(pageGeo);
						mpb.Transform(&pagePB);						// ページを content(pasteboard)座標へ
						const PMReal visiblePBTop = pano->GetContentLocationAtFrameOrigin().Y();	// 可視上端(pasteboard)
						const PMReal visibleTopSpread = visiblePBTop + (pr.Top() - pagePB.Top());	// pasteboard→spread はY平行移動(非回転前提)
						if (visibleTopSpread > anchorTop)			// 可視上端がページ上端より下=ページ上端が画面外
							anchorTop = visibleTopSpread;
					}
					PMReal off = inset + numPt;
					const PMReal cap = pr.Height() * kKESCMCountTopFracMax;	// 縮小時の下方ドリフト上限
					if (off > cap) off = cap;
					PMReal ty = anchorTop + off;					// 数字・語の共通ベースライン
					const PMReal maxTy = pr.Bottom() - inset;		// ページ下端より下へは出さない
					if (ty > maxTy) ty = maxTy;

					gPort->setopacity(PMReal(1.0), kFalse);							// 読みやすく不透明
					gPort->selectfont(theFont, numPt);
					const UTF16TextChar* numBuf = numStr.GrabUTF16Buffer(nil);

					// 数字の白い縁: 先に白を太めのストロークで描いてハローを作る。
					gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));		// 白
					gPort->setlinewidth(numPt * kKESCMCountHaloFrac);
					gPort->show(startX, ty, numCh, numBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));

					// 数字の本体: 赤fill＋同色の細い赤ストロークで少し太らせる。ストロークは輪郭中心なので
					// 半分が外へ膨らみ、白縁を僅かに侵食して赤を太く見せる(白縁は外側に残る=「赤文字に白枠」)。
					gPort->setrgbcolor(kKESCMCountNumR, kKESCMCountNumG, kKESCMCountNumB);
					gPort->setlinewidth(numPt * kKESCMCountBodyFrac);
					gPort->show(startX, ty, numCh, numBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText | IGraphicsPort::kStrokeText));

					// 語: 小さめ・細め(fill のみ=ストローク無し)・青。数字の直後・同じベースライン。
					gPort->setrgbcolor(kKESCMMarkR, kKESCMMarkG, kKESCMMarkB);
					gPort->selectfont(theFont, wordPt);
					gPort->show(startX + numW, ty, wordCh, wordStr.GrabUTF16Buffer(nil),
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
				}
			}
		}
	}

	return kFalse;	// 他のハンドラ・描画を続行させる
}


//========================================================================================
// KESCMDrawEventSrvc
//   kDrawEventService サービスとして自身を登録する。アプリ起動時にこのサービスが見つかり、
//   同じ boss 上の IDrwEvtHandler が描画イベントディスパッチャに登録される。
//========================================================================================
class KESCMDrawEventSrvc : public CServiceProvider
{
public:
	KESCMDrawEventSrvc(IPMUnknown* boss) : CServiceProvider(boss) {}
	~KESCMDrawEventSrvc() {}

	virtual ServiceID GetServiceID() { return kDrawEventService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	virtual void GetName(PMString* pName) { pName->SetKey("KESCMDrawEventSrvc\0"); }
	virtual IPlugIn::ThreadingPolicy GetThreadingPolicy() const { return IPlugIn::kMainThreadOnly; }
};

CREATE_PMINTERFACE(KESCMDrawEventSrvc, kKESCMDrawEventSrvcImpl)


//========================================================================================
// ヘルパ: ドキュメント内の全ページUIDを、スプレッド順・ページ順で平坦に集める。
//========================================================================================
static void KESCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out)
{
	if (db == nil)
		return;
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return;
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			out.push_back(spread->GetNthPageUID(p));
	}
}


//========================================================================================
// ミドルボタン peek — 共有状態とヘルパ。
//   ミドルボタンを押している間だけ、マウス下スプレッドの旧版を不透明べた載せし、離すと隠す。
//   IEventWatcher はグローバル(スクリプト引数を持てない)ので、比較相手の旧ドキュメントは先に
//   Document.kescmArmMousePeek(sourceDoc) で登録しておく。watcher(KESCMPeekWatcher)とスクリプト
//   メソッドが下の arm 状態を共有する。全部を1つの翻訳単位に置くことで、watcher が MakeOrigImage /
//   マウス下スプレッド判定 / sOrigImages を直接再利用できる。
//========================================================================================
static IDataBase* sPeekTargetDB = nil;	// 表示中(新)ドキュメント。使用前に「まだ開いているか」を検証する。
static IDataBase* sPeekSourceDB = nil;	// peek 中に重ねる旧ドキュメント。
static bool16     sPeekArmed    = kFalse;

// Shift＋ミドル=旧版を不透明(100%)で / Ctrl(=Win, IEvent::CmdKeyDown)＋ミドル=旧版を 50% で重ねて peek。
// 押下中だけ表示し、ミドルを離すと消す(修飾キーは離してもよい)。判定はミドル押下時に1回見るだけ。
static const PMReal kKESCMPeekSemiOpacity = 0.5;	// Ctrl＋ミドル時の旧版の不透明度(0..1)
static bool16 sPeekActive        = kFalse;	// Shift/Ctrl+ミドルを押し込み中(=覗き表示中)か
static bool16 sSingleShowing     = kFalse;	// シングルのミドル押下中(=全マークを一時的に表示中)か。離すと隠す
// ミドル押下中だけハンドツール(掴んで移動)に一時切替。離すと元のツールへ戻す。
static ITool*  sSavedTool  = nil;	// 切替前のツール(ref を保持。Restore で Release)
static bool16  sHandActive = kFalse;	// ハンドツールに一時切替中か

// peek 試行の結果(スクリプトの状態文字列用。watcher は無視する)。
enum KESCMPeekResult { kKESCMPeekNoView = 0, kKESCMPeekNoSpread = 1, kKESCMPeekShown = 2, kKESCMPeekNoChange = 3 };

// 前面レイアウトビューで「マウス下スプレッド」の旧版べた載せを表示する。
//   targetDB=表示中(新)ドキュメント, sourceDB=重ねる旧ドキュメント。
//   そのスプレッドが既にキャッシュ済みなら再利用(即時)。未キャッシュなら旧キャッシュを捨てて、その
//   スプレッドだけをその場でラスタ化(保持は常に1スプレッド)。成功時に sShowOriginal を立てて再描画。
//   outSpread/outPages は任意(nil 可)。
static KESCMPeekResult KESCMPeekShowUnderMouse(IDataBase* targetDB, IDataBase* sourceDB,
	int32* outSpread, int32* outPages)
{
	if (outSpread) *outSpread = -1;
	if (outPages)  *outPages = 0;
	if (targetDB == nil || sourceDB == nil)
		return kKESCMPeekNoView;

	// 前面レイアウトビュー(マウスが乗っているビュー)。前面が layout でなければ nil。
	InterfacePtr<IControlView> view(Utils<ILayoutUIUtils>()->QueryFrontView());
	if (view == nil)
		return kKESCMPeekNoView;

	// 現在のズーム(content→window スケール=ズーム×デバイス倍率)から、画面と 1:1 になる解像度を決める。
	// dpi = 72 × スケール。1:1 のとき最も綺麗(画像px=画面px)。
	PMReal curScale = view->GetContentToWindowMatrix().GetXScale();
	if (curScale < 0) curScale = -curScale;
	if (curScale <= 0) curScale = 1.0;

	// 【低ズームの下限=UI 50%】UIズーム(ユーザーに見える拡大率, デバイス倍率を含まない)が 50% を下回る時は
	// 「50% 相当の解像度」で頭打ちにする。50%以上は画面と 1:1 のままくっきり。50%未満は画像が画面より高精細に
	// なり、縮小blit(点サンプリング)で多少粗くなる(=10% などは汚くてよい、という方針)。下限を UI% で決めるので
	// デバイス倍率に依らず、画面に見える 50% がそのまま境界になる。パノラマ不明時は 1:1(従来=全ズーム綺麗)。
	PMReal effScale = curScale;
	InterfacePtr<IPanorama> peekPano(KESCMQueryPanorama(view));
	if (peekPano != nil)
	{
		const PMReal uiZoom = peekPano->GetXScaleFactor(kFalse);	// UIズーム(例: 0.5=50%)
		if (uiZoom > 0)
		{
			const PMReal deviceScale = curScale / uiZoom;			// 画面デバイス倍率(=curScale/uiZoom)
			const PMReal flooredZoom = (uiZoom < PMReal(0.5)) ? PMReal(0.5) : uiZoom;	// UI 50% で頭打ち
			effScale = flooredZoom * deviceScale;
		}
	}

	PMReal peekDpi = PMReal(72.0) * effScale;
	if (peekDpi < 16.0)  peekDpi = 16.0;	// 安全下限(degenerate 回避。通常は効かない)
	if (peekDpi > 300.0) peekDpi = 300.0;	// 過大メモリ防止(300dpi A4 ≒ 35MB/頁)

	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	const PMReal mx = pt.X(), my = pt.Y();

	// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。新→旧の通し番号対応に使う。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);

	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kKESCMPeekNoSpread;

	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	for (int32 s = 0; s < ns; ++s)
	{
		InterfacePtr<ISpread> spread(targetDB, spreadList->GetNthSpreadUID(s), UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();

		// マウスがこのスプレッドのいずれかのページ上にあるか?
		bool16 inThis = kFalse;
		for (int32 p = 0; p < np; ++p)
		{
			InterfacePtr<IGeometry> geo(targetDB, spread->GetNthPageUID(p), UseDefaultIID());
			if (geo == nil)
				continue;
			PMRect bb = geo->GetPathBoundingBox();
			PMMatrix m = ::InnerToPasteboardMatrix(geo);
			m.Transform(&bb);
			PMReal L = bb.Left(), R = bb.Right(), T = bb.Top(), B = bb.Bottom();
			if (L > R) { PMReal t = L; L = R; R = t; }
			if (T > B) { PMReal t = T; T = B; B = t; }
			if (mx >= L && mx <= R && my >= T && my <= B) { inThis = kTrue; break; }
		}

		if (inThis)
		{
			// 【未更新スプレッドの早期スキップ】このドキュメントで比較が実行済み(sDB==targetDB)で、かつ
			// このスプレッドのどのページも変化エントリ(sEntries)に無いなら、旧版は現行と同一=重ねる意味が
			// 無い。重いラスタ化を丸ごと省いて即 return する(旧版を出さない)。比較が未実行(sDB!=targetDB)
			// なら変化の有無を判定できないので、従来どおりラスタ化する(全スキップしない)。
			if (KESCMDrawEventHandler::sDB == targetDB)
			{
				bool16 anyChanged = kFalse;
				for (int32 p = 0; p < np; ++p)
					if (KESCMDrawEventHandler::sEntries.find(spread->GetNthPageUID(p)) !=
					    KESCMDrawEventHandler::sEntries.end())
					{ anyChanged = kTrue; break; }
				if (!anyChanged)
				{
					if (outSpread) *outSpread = s;
					if (outPages)  *outPages = 0;
					return kKESCMPeekNoChange;
				}
			}

			// このスプレッドは既に丸ごとキャッシュ済みか?(同じ db かつ 全ページが sOrigImages にある) → 再利用(即時)。
			bool16 cached = (KESCMDrawEventHandler::sOrigDB == targetDB);
			for (int32 p = 0; p < np && cached; ++p)
				if (KESCMDrawEventHandler::sOrigImages.find(spread->GetNthPageUID(p)) ==
				    KESCMDrawEventHandler::sOrigImages.end())
					cached = kFalse;
			// ズームが変わっていたら(キャッシュ時と解像度が合わない)作り直す。差が2%以内なら再利用。
			if (cached && KESCMDrawEventHandler::sOrigScale > 0)
			{
				PMReal d = effScale - KESCMDrawEventHandler::sOrigScale;
				if (d < 0) d = -d;
				if (d > KESCMDrawEventHandler::sOrigScale * PMReal(0.02))
					cached = kFalse;
			}

			int32 captured = 0;
			if (cached)
			{
				captured = np;	// ラスタ化不要=キャッシュがこのスプレッドを覆っている
			}
			else
			{
				KESCMDrawEventHandler::DropAllOrig();		// 覗くのは1スプレッドだけ=他は破棄
				KESCMDrawEventHandler::sOrigDB = targetDB;
				KESCMDrawEventHandler::sOrigScale = effScale;	// このラスタ化解像度を記録(再 peek の作り直し判定用)
				for (int32 p = 0; p < np; ++p)
				{
					const int32 gi = globalIndex + p;
					if (gi < (int32)sPages.size())
					{
						UIDRef tRef(targetDB, spread->GetNthPageUID(p));
						UIDRef sRef(sourceDB, sPages[gi]);
						if (KESCMDrawEventHandler::MakeOrigImage(tRef, sRef, peekDpi) == kSuccess)
							++captured;
					}
				}
			}
			KESCMDrawEventHandler::sShowOriginal = kTrue;

			InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
			if (doc != nil)
				Utils<ILayoutUtils>()->InvalidateViews(doc);

			if (outSpread) *outSpread = s;
			if (outPages)  *outPages = captured;
			return kKESCMPeekShown;
		}
		globalIndex += np;
	}
	return kKESCMPeekNoSpread;
}


// ミドル押下中だけ一時的にハンドツール(掴んで移動)へ切り替える。元のツールを覚えておく(離すと戻す)。
// 既に切替中なら何もしない(ハンド自身を「元のツール」として覚えてしまわないため)。
static void KESCMEnterHandTool()
{
	if (sHandActive)
		return;
	ITool* cur  = Utils<IToolBoxUtils>()->QueryActiveTool(kPointerToolBoss);	// +1 ref
	ITool* hand = Utils<IToolBoxUtils>()->QueryTool(kGrabberHandToolBoss);	// +1 ref
	if (hand != nil)
	{
		sSavedTool = cur;	// ref を保持(下の Restore で Release)。cur が nil でも可
		Utils<IToolBoxUtils>()->SetActiveTool(hand, kPointerToolBoss);
		hand->Release();
		sHandActive = kTrue;
	}
	else if (cur != nil)
	{
		cur->Release();
	}
}

// 覚えていた元のツールへ戻す(ハンドに切替えていた場合のみ)。
static void KESCMRestoreTool()
{
	if (!sHandActive)
		return;
	if (sSavedTool != nil)
	{
		Utils<IToolBoxUtils>()->SetActiveTool(sSavedTool, kPointerToolBoss);
		sSavedTool->Release();
		sSavedTool = nil;
	}
	sHandActive = kFalse;
}

// Shift／Ctrl＋ミドル押下を検出したときの共通処理: 「保持中だけ覗く」状態に入り、マウス下スプレッドの旧版を
// opacity(Shift=1.0 不透明 / Ctrl=0.5 半透明)で表示。覗き中もハンドツールにして「旧状態で掴んで移動」できるように。
// 覗き中は枠等(マーク)は不要なので sMarksVisible=kFalse のまま(既定が非表示)＝旧版だけが乗る。覗いている
// スプレッドは旧版が覆い、他スプレッドも非表示なので、画面全体が枠なしの「旧版/現行のみ」になる。
static void KESCMBeginPeekHold(PMReal opacity)
{
	sPeekActive = kTrue;
	KESCMDrawEventHandler::sPeekOpacity = opacity;	// 旧版の不透明度(描画時に (A2) ブロックが参照)
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 覗き中は枠等を出さない(旧版だけ)
	KESCMEnterHandTool();	// 旧状態で掴んで移動
	KESCMPeekShowUnderMouse(sPeekTargetDB, sPeekSourceDB, nil, nil);
}


// トースト表示(後方で定義)。WatchEvent から呼ぶための前方宣言。
static void KESCMShowToast(IDataBase* db, const PMString& msg, uint32 ms);

// Alt＋ミドルクリック: マウス下スプレッドだけを再比較して枠(リング)を更新する(部分更新)。
//   targetDB=新(arm 済み表示中) / sourceDB=旧(arm 済み比較相手)。新→旧ページは平坦通し番号で対応。
//   ・各ページを MakeEntry で取り直し(編集後の差分に更新)。変化が無くなったページは古い枠を消す。
//   ・旧版画像キャッシュ(sOrigImages)は古いので破棄(次の peek で作り直し)。
//   見つかったスプレッドの index(0始まり)を outSpread に、変化ページ数を outChanged に返す。戻り=見つかったか。
static bool16 KESCMRefreshSpreadUnderMouse(IDataBase* targetDB, IDataBase* sourceDB, int32* outSpread, int32* outChanged)
{
	if (outSpread)  *outSpread = -1;
	if (outChanged) *outChanged = 0;
	if (targetDB == nil || sourceDB == nil)
		return kFalse;

	InterfacePtr<IControlView> view(Utils<ILayoutUIUtils>()->QueryFrontView());
	if (view == nil)
		return kFalse;

	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	const PMReal mx = pt.X(), my = pt.Y();

	// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);

	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kFalse;

	// マークの所属ドキュメントを合わせる(別 doc にマークがあった場合のみ総入れ替え=通常は一致で何もしない)。
	if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sDB != targetDB)
		KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	for (int32 s = 0; s < ns; ++s)
	{
		InterfacePtr<ISpread> spread(targetDB, spreadList->GetNthSpreadUID(s), UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();

		// マウスがこのスプレッドのいずれかのページ上にあるか?
		bool16 inThis = kFalse;
		for (int32 p = 0; p < np; ++p)
		{
			InterfacePtr<IGeometry> geo(targetDB, spread->GetNthPageUID(p), UseDefaultIID());
			if (geo == nil)
				continue;
			PMRect bb = geo->GetPathBoundingBox();
			PMMatrix m = ::InnerToPasteboardMatrix(geo);
			m.Transform(&bb);
			PMReal L = bb.Left(), R = bb.Right(), T = bb.Top(), B = bb.Bottom();
			if (L > R) { PMReal t = L; L = R; R = t; }
			if (T > B) { PMReal t = T; T = B; B = t; }
			if (mx >= L && mx <= R && my >= T && my <= B) { inThis = kTrue; break; }
		}

		if (inThis)
		{
			// このスプレッドの各ページを再比較して枠を更新。新→旧は globalIndex で対応。
			int32 changedCount = 0;
			for (int32 p = 0; p < np; ++p)
			{
				const int32 gi = globalIndex + p;
				if (gi >= (int32)sPages.size())
					continue;
				const UID tUID = spread->GetNthPageUID(p);
				bool16 changed = kFalse;
				KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tUID), UIDRef(sourceDB, sPages[gi]), changed);
				if (changed)
					++changedCount;
				else
				{
					// 変化が無くなったページ → 古い枠が残っていれば消す(更新で消えるべき)。
					std::map<UID, KESCMOverlayEntry*>::iterator old = KESCMDrawEventHandler::sEntries.find(tUID);
					if (old != KESCMDrawEventHandler::sEntries.end())
					{ delete old->second; KESCMDrawEventHandler::sEntries.erase(old); }
				}
			}

			// 旧版画像キャッシュは古いので破棄(次の peek で現ズームで作り直し)。
			KESCMDrawEventHandler::DropAllOrig();

			InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
			if (doc != nil)
				Utils<ILayoutUtils>()->InvalidateViews(doc);

			if (outSpread)  *outSpread = s;
			if (outChanged) *outChanged = changedCount;
			return kTrue;
		}
		globalIndex += np;
	}
	return kFalse;
}


// マーク(枠/X/変更数/オーバーセット)の表示を切り替えた後、マークが属するドキュメント(sDB)を再描画して
// 即反映する。arm の有無に依らず使えるよう、peek 用の sPeekTargetDB ではなく sDB を使う(arm 不要)。
static void KESCMInvalidateMarksDoc()
{
	IDataBase* db = KESCMDrawEventHandler::sDB;
	if (db == nil)
		return;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);
}


//========================================================================================
// KESCMPeekWatcher
//   非消費のイベントウォッチャ。peek が arm 済み(kescmArmMousePeek)の間、Shift＋ミドルボタンを押すと
//   マウス下スプレッドの旧版べた載せを表示し、ミドルを離すと隠す。非消費=ミドルボタン本来の動作も走る。
//========================================================================================
class KESCMPeekWatcher : public CPMUnknown<IEventWatcher>
{
public:
	KESCMPeekWatcher(IPMUnknown* boss) : CPMUnknown<IEventWatcher>(boss), fWatching(kFalse) {}
	~KESCMPeekWatcher() {}

	IEventDispatcher::EventTypeList WatchEvent(IEvent* e);
	void StartWatching();
	void StopWatching();

private:
	bool16 fWatching;
};

CREATE_PMINTERFACE(KESCMPeekWatcher, kKESCMPeekWatcherImpl)

IEventDispatcher::EventTypeList KESCMPeekWatcher::WatchEvent(IEvent* e)
{
	// 興味=ミドル押下/解放のみ。毎回返す(空を返すと監視解除される)。Shift 判定は押下イベントで見る。
	IEventDispatcher::EventTypeList interest(IEvent::kMButtonDn, IEvent::kMButtonUp);

	if (e == nil)
		return interest;

	const IEvent::EventType type = e->GetType();
	if (type != IEvent::kMButtonDn && type != IEvent::kMButtonUp)
		return interest;

	// 旧版べた載せ(peek)の検証は arm 済みの時だけ。シングルの枠表示は arm 不要なので、ここで素通りさせる。
	if (sPeekArmed)
	{
		// arm 済みドキュメントがまだ開いているか検証(片方を閉じた後のダングリング参照を防ぐ)。
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList == nil ||
		    docList->FindDocByDataBase(sPeekTargetDB) == nil ||
		    docList->FindDocByDataBase(sPeekSourceDB) == nil)
		{
			// ドキュメントが消えた → disarm して全部破棄。
			KESCMRestoreTool();	// ハンドに切替え中なら元へ戻す
			sPeekArmed = kFalse;
			sPeekTargetDB = nil;
			sPeekSourceDB = nil;
			sPeekActive = kFalse;
			sSingleShowing = kFalse;
			KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)へ
			KESCMDrawEventHandler::DropAllOrig();
			return interest;
		}
	}

	if (type == IEvent::kMButtonDn)
	{
		if (sPeekArmed && e->ShiftKeyDown())
		{
			// Shift＋ミドル押下: マウス下スプレッドの旧版べた載せ(peek)を不透明(100%)で開始。押下中だけ表示。
			// 判定はこの押下時の修飾キー状態のみ。以後キーを離しても変わらず、ミドルを離すと消える。
			KESCMBeginPeekHold(PMReal(1.0));
		}
		else if (sPeekArmed && e->CmdKeyDown())
		{
			// Ctrl(=Win, CmdKeyDown)＋ミドル押下: 同じ peek を 50% 透明で重ねる(現行ページと半々のゴースト比較)。
			KESCMBeginPeekHold(kKESCMPeekSemiOpacity);
		}
		else if (sPeekArmed && e->OptionAltKeyDown())
		{
			// Alt(=Win, OptionAltKeyDown)＋ミドル押下(momentary): マウス下スプレッドだけ枠を再検出して更新。
			// 旧版画像キャッシュは破棄(次 peek で作り直し)。完了したら「spread N updated」をトースト表示。
			int32 sp = -1;
			if (KESCMRefreshSpreadUnderMouse(sPeekTargetDB, sPeekSourceDB, &sp, nil))
			{
				PMString msg("spread ");
				msg.SetTranslatable(kFalse);
				msg.AppendNumber(sp + 1);	// スプレッド番号(1始まり)
				msg.Append(" markers refreshed");
				KESCMShowToast(sPeekTargetDB, msg, kKESCMToastDefaultMs);
			}
		}
		else if (!e->ShiftKeyDown() && !e->CmdKeyDown() && !e->OptionAltKeyDown())
		{
			// シングル動作(修飾キーなしミドル押下中): 全マーク(リング＋変更数＋オーバーセット)を「表示」にして、
			// ハンドツールに切替えて「枠を見ながら掴んで移動」できるようにする。離す(kMButtonUp)と非表示へ戻す。
			// マークが何も無い(エントリも無く overset も OFF)時は反応しない=素のミドルクリックを邪魔しない。
			const bool16 haveContent =
				(!KESCMDrawEventHandler::sEntries.empty()) || KESCMDrawEventHandler::sShowOverset;
			if (haveContent)
			{
				sSingleShowing = kTrue;
				KESCMDrawEventHandler::sMarksVisible = kTrue;	// 押下中だけ枠等を表示
				KESCMEnterHandTool();	// 枠を見ながら掴んで移動
				KESCMInvalidateMarksDoc();
			}
		}
		// (Shift/Ctrl を押していて arm 未済 → 何もしない。Shift/Ctrl は peek 専用に予約)
	}
	else // kMButtonUp
	{
		// ミドルを離したら、ハンドに切替えていた場合は元のツールへ戻す(シングル/ダブル共通)。
		KESCMRestoreTool();

		if (sPeekActive)
		{
			// Shift＋ミドルを離した(ミドル解放) → 旧版を隠す(マークは触らない)。キャッシュは保持(再 peek は即時)。
			sPeekActive = kFalse;
			if (KESCMDrawEventHandler::sShowOriginal)
			{
				KESCMDrawEventHandler::sShowOriginal = kFalse;
				InterfacePtr<IDocument> doc(sPeekTargetDB, sPeekTargetDB->GetRootUID(), UseDefaultIID());
				if (doc != nil)
					Utils<ILayoutUtils>()->InvalidateViews(doc);
			}
		}
		else if (sSingleShowing)
		{
			// シングルの押下を離した → 押下中だけ表示していた全マークを非表示(既定)へ戻す。
			sSingleShowing = kFalse;
			KESCMDrawEventHandler::sMarksVisible = kFalse;
			KESCMInvalidateMarksDoc();
		}
	}
	return interest;
}

void KESCMPeekWatcher::StartWatching()
{
	if (fWatching) return;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IEventDispatcher> dispatcher(app, UseDefaultIID());
	if (dispatcher)
	{
		dispatcher->AddEventWatcher(this, IEventDispatcher::EventTypeList(IEvent::kMButtonDn, IEvent::kMButtonUp));
		fWatching = kTrue;
	}
}

void KESCMPeekWatcher::StopWatching()
{
	if (!fWatching) return;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IEventDispatcher> dispatcher(app, UseDefaultIID());
	if (dispatcher)
		dispatcher->RemoveEventWatcher(this, IEventDispatcher::EventTypeList());	// 空=全種の監視を解除
	fWatching = kFalse;
}


//========================================================================================
// KESCMPeekStartup
//   アプリ起動時に peek ウォッチャを生成して監視を開始する。
//========================================================================================
class KESCMPeekStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KESCMPeekStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss), fWatcher(nil) {}
	~KESCMPeekStartup() {}

	virtual void Startup();
	virtual void Shutdown();

private:
	IEventWatcher* fWatcher;
};

CREATE_PMINTERFACE(KESCMPeekStartup, kKESCMPeekStartupImpl)

void KESCMPeekStartup::Startup()
{
	fWatcher = ::CreateObject2<IEventWatcher>(kKESCMPeekWatcherBoss);
	if (fWatcher)
		fWatcher->StartWatching();
}

void KESCMPeekStartup::Shutdown()
{
	if (fWatcher)
	{
		fWatcher->StopWatching();
		fWatcher->Release();
		fWatcher = nil;
	}
}


//========================================================================================
// KESCMToastIdleTask — 一時トーストの自動消去タイマ。
//   KESCMShowToast() が AddTask(this, ms) で登録 → ms 後に RunTask が呼ばれてトーストを非表示にし
//   再描画する。kEndOfTime を返して自分をキューから外す(タイマ本体オブジェクトはセッション中
//   sToastTask に保持して再利用)。
//========================================================================================
static IIdleTask* sToastTask   = nil;	// タイマ本体(起動中に1度だけ生成して再利用)
static bool16     sToastQueued = kFalse;	// タイマが現在キューに入っているか(二重 AddTask 防止)

class KESCMToastIdleTask : public CPMUnknown<IIdleTask>
{
public:
	KESCMToastIdleTask(IPMUnknown* boss) : CPMUnknown<IIdleTask>(boss) {}
	~KESCMToastIdleTask() {}

	virtual uint32 RunTask(uint32 appFlags, IdleTimer* timeCheck);
	virtual void InstallTask(uint32 millisecsBeforeFirstRun);
	virtual void UninstallTask();
	virtual const char* TaskName() { return "KESCMToastIdleTask"; }
};

CREATE_PMINTERFACE(KESCMToastIdleTask, kKESCMToastIdleTaskImpl)

uint32 KESCMToastIdleTask::RunTask(uint32 /*appFlags*/, IdleTimer* /*timeCheck*/)
{
	// トーストを消して再描画。対象ドキュメントが既に閉じていれば再描画はスキップ(ダングリング回避)。
	KESCMDrawEventHandler::sToastVisible = kFalse;
	IDataBase* db = KESCMDrawEventHandler::sToastDB;
	KESCMDrawEventHandler::sToastDB = nil;
	if (db != nil)
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList != nil && docList->FindDocByDataBase(db) != nil)
		{
			InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
			if (doc != nil)
				Utils<ILayoutUtils>()->InvalidateViews(doc);
		}
	}
	sToastQueued = kFalse;
	return IIdleTask::kEndOfTime;	// 自分をキューから除去(オブジェクトは sToastTask に保持=次の表示で再利用)
}

void KESCMToastIdleTask::InstallTask(uint32 millisecsBeforeFirstRun)
{
	InterfacePtr<IIdleTaskMgr> mgr(GetExecutionContextSession(), UseDefaultIID());
	if (mgr != nil)
		mgr->AddTask(this, millisecsBeforeFirstRun);
}

void KESCMToastIdleTask::UninstallTask()
{
	InterfacePtr<IIdleTaskMgr> mgr(GetExecutionContextSession(), UseDefaultIID());
	if (mgr != nil)
		mgr->RemoveTask(this);
}

// 画面中央に msg を ms ミリ秒だけ表示し、その後自動で消す。db=描画するドキュメント(前面)。
// 直近の表示タイマが生きていれば取り消して入れ直す(=最後の表示から ms 後に消える)。
static void KESCMShowToast(IDataBase* db, const PMString& msg, uint32 ms)
{
	KESCMDrawEventHandler::sToastMsg = msg;
	KESCMDrawEventHandler::sToastMsg.SetTranslatable(kFalse);
	KESCMDrawEventHandler::sToastVisible = kTrue;
	KESCMDrawEventHandler::sToastDB = db;

	// 即時に1回描く。
	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	// 自動消去タイマ(IIdleTask)。タイマ本体はセッション中1個を生成して再利用。
	InterfacePtr<IIdleTaskMgr> mgr(GetExecutionContextSession(), UseDefaultIID());
	if (mgr == nil)
		return;
	if (sToastTask == nil)
		sToastTask = ::CreateObject2<IIdleTask>(kKESCMToastIdleTaskBoss);	// +1 ref, セッション保持
	if (sToastTask == nil)
		return;
	if (sToastQueued)			// 直近のタイマを取り消して入れ直す(同一タスクの二重 AddTask は不可のため)
		mgr->RemoveTask(sToastTask);
	mgr->AddTask(sToastTask, ms);
	sToastQueued = kTrue;
}


//========================================================================================
// KESCMScriptProvider
//   Page / Document オブジェクトに kescmMarkChanges / kescmMarkChangesDoc / kescmClearMarks を生やす。
//========================================================================================
class KESCMScriptProvider : public CScriptProvider
{
public:
	KESCMScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	~KESCMScriptProvider() {}

	virtual ErrorCode HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent);

private:
	// kescmMarkChanges(sourcePage): このページ1枚にエントリを追加(複数回呼ぶと貯まる)。
	ErrorCode MarkChanges(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmMarkChangesDoc(sourceDoc): このドキュメント全ページを sourceDoc と突き合わせて総入れ替え。
	ErrorCode MarkChangesDoc(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmClearMarks(): オーバーレイを全消去。
	ErrorCode ClearMarks(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmShowPageX(flag): 変更ページの対角線×の表示/非表示を切り替え(任意, 既定=表示)。
	ErrorCode ShowPageX(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// 【テスト】kescmShowOverset(flag): オーバーセットフレームの「四角＋＋」目印の表示/非表示(任意, 既定=表示)。
	ErrorCode ShowOverset(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmShowOriginal(sourcePage): このページに sourcePage(旧)の画像を高解像度で生成し不透明べた載せ(表示ON)。
	ErrorCode ShowOriginal(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmHideOriginal(): 旧版べた載せの表示を OFF(画像キャッシュは保持。再表示は即時)。Page/Document 両対応。
	ErrorCode HideOriginal(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmShowOriginalUnderMouse(sourceDoc): マウスが乗っているスプレッドを判定し、その各ページに sourceDoc の
	// 同インデックスのページ画像を不透明べた載せ(そのスプレッドだけ保持)。Document 対応。
	ErrorCode ShowOriginalUnderMouse(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmArmMousePeek(sourceDoc): ミドルボタン peek を arm(比較相手の旧ドキュメントを登録)。Document 対応。
	ErrorCode ArmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmDisarmMousePeek(): ミドルボタン peek を解除し、旧版べた載せを隠してキャッシュも解放。Document 対応。
	ErrorCode DisarmMousePeek(ScriptID methodID, IScriptRequestData* data, IScript* parent);

	// kescmToast(message): 画面中央に message を少し表示して自動で消す。Page/Document 対応。
	ErrorCode Toast(ScriptID methodID, IScriptRequestData* data, IScript* parent);
};

CREATE_PMINTERFACE(KESCMScriptProvider, kKESCMScriptProviderImpl)


ErrorCode KESCMScriptProvider::HandleMethod(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	ErrorCode status = kFailure;
	switch (methodID.Get())
	{
	case e_KESCMMarkChanges:
		status = MarkChanges(methodID, data, parent);
		break;
	case e_KESCMMarkChangesDoc:
		status = MarkChangesDoc(methodID, data, parent);
		break;
	case e_KESCMClearMarks:
		status = ClearMarks(methodID, data, parent);
		break;
	case e_KESCMShowPageX:
		status = ShowPageX(methodID, data, parent);
		break;
	case e_KESCMShowOverset:
		status = ShowOverset(methodID, data, parent);
		break;
	case e_KESCMShowOriginal:
		status = ShowOriginal(methodID, data, parent);
		break;
	case e_KESCMHideOriginal:
		status = HideOriginal(methodID, data, parent);
		break;
	case e_KESCMShowOriginalUnderMouse:
		status = ShowOriginalUnderMouse(methodID, data, parent);
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
	default:
		status = CScriptProvider::HandleMethod(methodID, data, parent);
	}
	return status;
}


/* MarkChanges
   呼び出し元ページ(parent=新)と引数 sourcePage(旧, 別ドキュメント可)を比較し、変化があれば
   このページ用のエントリを追加する(置換)。複数回呼べば複数ページに貯まる。
*/
ErrorCode KESCMScriptProvider::MarkChanges(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef targetRef = ::GetUIDRef(parent);
	IDataBase* targetDB = targetRef.GetDataBase();
	if (targetDB == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;

	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMSourcePage, arg) != kSuccess)
		return kFailure;
	InterfacePtr<IScript> srcScript(arg.QueryObject());
	if (srcScript == nil)
		return kFailure;
	UIDRef sourceRef = ::GetUIDRef(srcScript);
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// 別ドキュメントを対象にしたら作り直す(UIDはdb内のみ一意)。
	if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sDB != targetDB)
		KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	bool16 changed = kFalse;
	ErrorCode ec = KESCMDrawEventHandler::MakeEntry(targetRef, sourceRef, changed);

	// 即時再描画(ズームを跨がないで反映)。何も付けない=書類が dirty にならない。
	InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);

	PMString report;
	report.SetTranslatable(kFalse);
	if (ec == kSuccess)
		report.Append(changed ? "kescm: marked (changed)" : "kescm: no change on this page");
	else
		report.Append("kescm: failed to rasterize");

	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
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
	UIDRef srcDocRef = ::GetUIDRef(srcScript);
	IDataBase* sourceDB = srcDocRef.GetDataBase();
	if (sourceDB == nil)
		return kFailure;

	// ドキュメント単位の総入れ替え。
	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	// 両ドキュメントのページUIDをドキュメント順に平坦列挙。
	std::vector<UID> tPages, sPages;
	KESCMCollectPageUIDs(targetDB, tPages);
	KESCMCollectPageUIDs(sourceDB, sPages);

	const size_t n = (tPages.size() < sPages.size()) ? tPages.size() : sPages.size();
	int32 changedCount = 0;
	for (size_t i = 0; i < n; ++i)
	{
		bool16 changed = kFalse;
		KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
		if (changed) ++changedCount;
	}

	InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append("kescm: pages compared="); report.AppendNumber((int32)n);
	report.Append(" changed="); report.AppendNumber(changedCount);
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

	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::DropAllOrig();	// 旧版べた載せのキャッシュも解放(メモリ開放)

	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	PMString report("marks cleared");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ShowPageX
   変更ページに重ねる対角線×の表示/非表示を切り替える。引数 flag は任意(省略時は表示=kTrue)。
   マーク自体(エントリ)は触らず、トグルだけ変えて再描画する。Page でも Document でも可。
*/
ErrorCode KESCMScriptProvider::ShowPageX(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	bool16 flag = kTrue;	// 省略時は表示
	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMShowPageXFlag, arg) == kSuccess)
		arg.GetBoolean(&flag);

	KESCMDrawEventHandler::sShowPageX = flag;

	// 再描画(エントリはそのまま、×の有無だけ反映)。
	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(flag ? "kescm: page X shown" : "kescm: page X hidden");
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ShowOverset  【テスト機能】
   オーバーセット(あふれ)のテキストフレーム右下に出す「四角＋＋」目印の表示/非表示を切り替える。
   引数 flag は任意(省略時は表示=kTrue)。マーク(sEntries)とは独立。Page でも Document でも可。
*/
ErrorCode KESCMScriptProvider::ShowOverset(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	bool16 flag = kTrue;	// 省略時は表示
	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMShowOversetFlag, arg) == kSuccess)
		arg.GetBoolean(&flag);

	KESCMDrawEventHandler::sShowOverset = flag;

	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(flag ? "kescm: overset marks shown" : "kescm: overset marks hidden");
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ShowOriginal
   呼び出し元ページ(parent=新)に、引数 sourcePage(旧, 別ドキュメント可)の画像を高解像度でその場生成し、
   不透明べた載せで重ねる(表示ON)。スプレッドの各ページに対して呼べば、そのスプレッド全体が旧版で覆われる。
   生成するオフスクリーンは常に1枚=即破棄なので安全。マーク(リング)とは独立。
*/
ErrorCode KESCMScriptProvider::ShowOriginal(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	UIDRef targetRef = ::GetUIDRef(parent);
	IDataBase* targetDB = targetRef.GetDataBase();
	if (targetDB == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;

	ScriptData arg;
	if (data->ExtractRequestData(p_KESCMSourcePage, arg) != kSuccess)
		return kFailure;
	InterfacePtr<IScript> srcScript(arg.QueryObject());
	if (srcScript == nil)
		return kFailure;
	UIDRef sourceRef = ::GetUIDRef(srcScript);
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// 別ドキュメントを覗き始めたら旧版キャッシュを作り直す(UIDはdb内のみ一意)。
	if (KESCMDrawEventHandler::sOrigDB != nil && KESCMDrawEventHandler::sOrigDB != targetDB)
		KESCMDrawEventHandler::DropAllOrig();
	KESCMDrawEventHandler::sOrigDB = targetDB;

	ErrorCode ec = KESCMDrawEventHandler::MakeOrigImage(targetRef, sourceRef);
	KESCMDrawEventHandler::sShowOriginal = kTrue;

	InterfacePtr<IDocument> doc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(ec == kSuccess ? "kescm: original shown on this page" : "kescm: failed to rasterize original");
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* HideOriginal
   旧版べた載せの表示を OFF にする(画像キャッシュは保持＝再表示は即時、メモリ開放は kescmClearMarks)。
   Page でも Document でも可。
*/
ErrorCode KESCMScriptProvider::HideOriginal(ScriptID methodID, IScriptRequestData* data, IScript* parent)
{
	KESCMDrawEventHandler::sShowOriginal = kFalse;

	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	PMString report("kescm: original hidden");
	report.SetTranslatable(kFalse);
	ScriptData rd; rd.SetPMString(report);
	data->AppendReturnData(parent, methodID, rd);
	return kSuccess;
}


/* ShowOriginalUnderMouse
   いま「マウスが乗っているスプレッド」(アクティブ・スプレッドではない)を前面レイアウトビューから判定し、
   そのスプレッドの各ページを sourceDoc の同じ通し番号のページ画像で不透明べた載せする(そのスプレッドだけ保持)。
   マウス位置 → 前面ビューでコンテンツ(ペーストボード)座標へ変換 → 各ページの InnerToPasteboard 矩形で内外判定。
*/
ErrorCode KESCMScriptProvider::ShowOriginalUnderMouse(ScriptID methodID, IScriptRequestData* data, IScript* parent)
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
	UIDRef srcDocRef = ::GetUIDRef(srcScript);
	IDataBase* sourceDB = srcDocRef.GetDataBase();
	if (sourceDB == nil)
		return kFailure;

	// マウス下スプレッドの検出・旧版べた載せは共有ヘルパに委譲(ミドルボタン peek と同じ経路)。
	int32 foundSpread = -1, capturedPages = 0;
	KESCMPeekResult res = KESCMPeekShowUnderMouse(targetDB, sourceDB, &foundSpread, &capturedPages);

	PMString report;
	report.SetTranslatable(kFalse);
	if (res == kKESCMPeekShown)
	{
		report.Append("kescm: original under mouse (spread ");
		report.AppendNumber(foundSpread);
		report.Append(", ");
		report.AppendNumber(capturedPages);
		report.Append(" pages)");
	}
	else if (res == kKESCMPeekNoChange)
		report.Append("kescm: spread under mouse has no changes (skipped)");
	else if (res == kKESCMPeekNoView)
		report.Append("kescm: no front layout view");
	else
		report.Append("kescm: mouse not over a spread");

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
	UIDRef srcDocRef = ::GetUIDRef(srcScript);
	IDataBase* sourceDB = srcDocRef.GetDataBase();
	if (sourceDB == nil)
		return kFailure;

	// arm 対象が変わったら古い peek キャッシュは捨てる。
	if (sPeekSourceDB != sourceDB || sPeekTargetDB != targetDB)
		KESCMDrawEventHandler::DropAllOrig();

	sPeekTargetDB = targetDB;
	sPeekSourceDB = sourceDB;
	sPeekArmed = kTrue;
	sPeekActive = kFalse;			// 覗き状態を初期化
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)へ。arm 中も枠は押下中だけ表示

	// 画面中央に「ChangeMarker ON」を少し表示(自動消去)。
	{
		PMString onMsg("ChangeMarker ON");
		onMsg.SetTranslatable(kFalse);
		KESCMShowToast(targetDB, onMsg, kKESCMToastDefaultMs);
	}

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
	KESCMRestoreTool();	// ハンドに切替え中なら元のツールへ戻す
	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;
	sPeekActive = kFalse;
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)のまま。枠等はシングルミドル押下中だけ
	KESCMDrawEventHandler::DropAllOrig();	// sShowOriginal も OFF にし、キャッシュを解放

	UIDRef ref = ::GetUIDRef(parent);
	IDataBase* db = ref.GetDataBase();
	if (db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		if (doc != nil)
			Utils<ILayoutUtils>()->InvalidateViews(doc);
	}

	// 画面中央に「ChangeMarker OFF」を少し表示(自動消去)。
	{
		PMString offMsg("ChangeMarker OFF");
		offMsg.SetTranslatable(kFalse);
		KESCMShowToast(db, offMsg, kKESCMToastDefaultMs);
	}

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
