//========================================================================================
//
//  KESCMColorSampler.h
//
//  Shift＋Ctrl＋Alt＋ミドルクリックで、クリック点の CMYK 生値を新(target)・旧(source)でサンプリング
//  して "Target C.. / Source C.." の文字列に組む。クリック点まわりの極小領域だけを高dpi・CMYK で
//  ラスタ化して中心1画素を読む。
//
//========================================================================================
#ifndef __KESCMColorSampler_h__
#define __KESCMColorSampler_h__

#include "BaseType.h"
#include "PMString.h"

class IDataBase;

bool16 KESCMSampleCmykUnderMouse(IDataBase* targetDB, IDataBase* sourceDB, PMString& outMsg);

#endif // __KESCMColorSampler_h__
