//========================================================================================
//
//  KESCMConstants.h
//
//  ChangeMarker (KESCM) のチューニング定数。描画エンジン/トースト/peek/色サンプラなど
//  複数のファイルで共有する。すべて static const なので各 TU が自分のコピーを持つ(ODR 問題なし)。
//
//========================================================================================
#ifndef __KESCMConstants_h__
#define __KESCMConstants_h__

#include "BaseType.h"
#include "PMReal.h"

static const PMReal kKESCMRingTargetPx = 8.0;	// リングの目標太さ(画面px)。ズームに依らず一定に見せる
static const uint8 kKESCMRingAlpha = 255;	// リングの基本アルファ(0..255)。「通常」=不透明(255)。薄表示は setopacity 側で行う(25%→255×0.25=実25%)
static const PMReal kKESCMFaintOpacity = 0.25;	// 薄表示(ミドルのみ/印刷25%系)の不透明度。小さいほど薄い。★現在25%
// 変化判定: 常に CMYK でラスタ化して比較し、CMYK 4ch のどれかがこのしきい値を超えて違えば「変化」とする。
// しきい値=0 は「どんな差も拾う」(=CMYK 1単位でも検出)。CMYK の微差は RGB へ変換すると丸めで消えるため、
// CMYK のまま比較するのが要点(ユーザーは CMYK 数値で考える)。画像/効果の再描画ゆらぎでノイズが出るなら 1〜2 に上げる。
static const int   kKESCMCmykThr = 0;
static const int32 kKESCMBaseRadius = 4;	// リング初期半径(画像px)。描画時にズームから再算出するための初期値
static const PMReal kKESCMResolution = 72.0;	// 保存・表示のラスタ解像度(dpi)。リング画像/マスクはこの解像度で持つ(軽い)
// 【取りこぼし防止】比較だけ高解像度で行い、結果を低解像度に圧縮(マックスプーリング)して記憶する。
// 比較解像度 = kKESCMResolution × kKESCMHiResMul。低解像度では平均化で消える細線/微小ズレを満額で拾う。
static const PMReal kKESCMHiResMul    = 2.0;	// 比較解像度の倍率(2=144dpi)。上げるほど検出力↑/一時メモリ↑。300dpi 相当なら≒4.17
static const int32  kKESCMPoolMinCount = 1;	// プーリング: 低解像度1セル内の「高解像度の変化画素数」がこの値以上で変化と判定。
											// 1=最高感度(縁ノイズも拾う)/大きいほどノイズ耐性↑(取りこぼしのリスクも僅かに増)

// リング色: 通常は赤。ただし枠の下の実ページが「赤っぽい」画素の上では、半透明の赤枠が背景に埋もれて
// 見えなくなるため、視認性確保のためにシアンへ切り替える(画素単位)。シアン=赤の補色(色相180°反対)で、
// 明るさも高い(輝度≒0.79)ため赤上で明暗・色相とも最大コントラスト。純青は暗く細線で沈むため不採用。
static const uint8 kKESCMRingR = 255, kKESCMRingG = 0,   kKESCMRingB = 0;		// 通常(赤)
static const uint8 kKESCMRingAltR = 0,   kKESCMRingAltG = 255, kKESCMRingAltB = 255;	// 赤背景の上(シアン=赤の補色)
static const int   kKESCMRedBgDom = 25;	// 背景を「赤っぽい」と判定する R 優位の閾値(R が G,B の双方より これ以上大きい)。小さいほどピンク/薄い赤も拾う

// 変更数テキストの色。数字=赤、後ろの語=シアン。setrgbcolor(0..1)。リング(赤/シアン)とは別系統。
static const PMReal kKESCMCountNumR  = 1.0, kKESCMCountNumG  = 0.0, kKESCMCountNumB  = 0.0;	// 数字 赤
static const PMReal kKESCMMarkR = 0.0, kKESCMMarkG = 1.0, kKESCMMarkB = 1.0;					// 語 シアン

// 変更数テキスト(各ページの変更=枠の数を「N chg」で表示)。常時表示(トグル無し。X廃止)。ズームで大きさ不変。
// 位置=各ページ上端から少し下、ページ「内側」に横中央で置く。数字は赤fill＋細い赤ストロークでやや太字、白い縁(ハロー)付き。
// 白縁=本体の前に白を太めのストロークで描き(線幅=文字サイズ×kKESCMCountHaloFrac)。ストロークは輪郭中心なので
// 見える白縁は線幅の約半分。本体の赤fill＋赤ストロークが内側半分を覆い、外側に白リムが残る。
static const PMReal kKESCMCountTextPx   = 20.0;	// 数字の文字サイズ(画面px)
static const PMReal kKESCMCountInsetPx  = 6.0;	// ページ上端からの内側余白(画面px)
static const PMReal kKESCMCountHaloFrac = 0.16;	// 白縁の太さ(文字サイズに対する比)。見える縁はこの約半分。大きいほど太い縁
static const PMReal kKESCMCountPrintHaloFrac = 0.08;	// 印刷時のみの白縁太さ(文字サイズ比)。画面の半分=白縁を少なく
static const PMReal kKESCMCountBodyFrac = 0.05;	// 赤本体を太らせるストローク幅(文字サイズ比)。大きいほど太い赤文字。0でfillのみ(最も細い)
static const PMReal kKESCMCountPrintThinFrac = 0.03;	// 印刷時のみ: 赤fillの外縁を白で削って細く見せるストローク幅(文字サイズ比)。大きいほど細い。0.1は削りすぎでステムが消えるため0.03(削る総幅=numPt×値)
static const PMReal kKESCMCountWordThinFrac  = 0.04;	// 印刷時のみ: 語("chg")の外縁を白で削って細く見せるストローク幅(語サイズ比)。半分くらい細く。大きいほど細い(削りすぎると消える)
// 数字の後ろに続く語(" chg")。小さめ・細め(fill のみ=ストローク無し)で添える。
static const PMReal kKESCMCountWordPx   = 11.0;	// 語の文字サイズ(画面px)。数字より小さめ
static const PMReal kKESCMCountDropPx   = 5.0;	// 数字ベースラインをページ上端基準からさらに下げる量(画面px)
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
static const PMReal kKESCMToastTextPx    = 28.8;	// 文字サイズ(画面px)。従来36.0の80%
static const PMReal kKESCMToastPadPx     = 12.8;	// 文字周りの内側余白(画面px)。大きいほど背景ボックスが広い。従来16.0の80%
static const uint32 kKESCMToastDefaultMs = 2500;	// 既定の表示時間(ms)。表示後この時間で自動的に消える

// クリック点 CMYK サンプリング(Shift＋Ctrl＋Alt＋ミドル)。クリック周りの極小領域だけを高dpi・CMYK で
// ラスタ化し、中心1画素の生値(0..255)を新・旧で読む。AA は OFF(ベクター縁の中間色を避ける)。
static const PMReal kKESCMSampleDpi    = 300.0;	// サンプリングのラスタ解像度(dpi)
static const PMReal kKESCMSampleHalfPt = 1.0;	// サンプル領域の半幅(pt)。300dpi で約2pt四方≒8px→中心1画素を読む

#endif // __KESCMConstants_h__
