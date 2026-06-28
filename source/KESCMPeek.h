//========================================================================================
//
//  KESCMPeek.h
//
//  ミドルボタンの「peek(覗き)」。修飾キー＋ミドルを押している間だけ、カーソル下スプレッドの旧版を
//  表示する(または再比較する)。離すと元に戻す。peek 状態(arm 済みの target/source DB、押下中フラグ)と
//  イベントウォッチャ／起動サービスを所有する。ここで公開するのは KESCMBaseScreenOpacity だけで、
//  arm/disarm/状態アクセサは KESCMCore.h にある。
//
//========================================================================================
#ifndef __KESCMPeek_h__
#define __KESCMPeek_h__

#include "PMReal.h"

// 常時表示マークの画面上の「基準」不透明度。印刷設定から決まる(印刷ON＋faint => 約0.3、それ以外 1.0)。
// peek を離したときの経路と KESCMDoSetPrintMarks が使う。実体は KESCMPeek.cpp。
PMReal KESCMBaseScreenOpacity();

#endif // __KESCMPeek_h__
