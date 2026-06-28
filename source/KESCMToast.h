//========================================================================================
//
//  KESCMToast.h
//
//  画面中央に少し出て自動で消える一時トースト(「ChangeMarker ON」等)の表示制御。実体の文字列/
//  表示フラグ/対象 DB は KESCMDrawEventHandler の static(sToastMsg/sToastVisible/sToastDB)に持ち、
//  描画はエンジンの HandleDrawEvent が行う。ここはタイマ(IIdleTask)と表示/消去の入口だけを担う。
//
//========================================================================================
#ifndef __KESCMToast_h__
#define __KESCMToast_h__

#include "BaseType.h"
#include "PMString.h"

class IDataBase;

// 画面中央に msg を ms ミリ秒だけ表示して自動で消す(db=前面ドキュメント)。
void KESCMShowToast(IDataBase* db, const PMString& msg, uint32 ms);

// 押下中だけ表示するトースト(自動消去なし。色サンプル用)と、その消去。
void KESCMShowHoldToast(IDataBase* db, const PMString& msg);
void KESCMHideHoldToast();

// セッション終了時にトーストタイマ本体を解放する(KESCMPeekStartup::Shutdown から呼ぶ)。
void KESCMShutdownToast();

#endif // __KESCMToast_h__
