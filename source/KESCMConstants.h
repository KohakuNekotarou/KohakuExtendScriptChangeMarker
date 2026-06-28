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
