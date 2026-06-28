//========================================================================================
//
//  KESCMColorSampler.cpp
//
//  クリック点 CMYK サンプリングの実装(旧 KESCMScriptProvider.cpp から分離)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"
#include "IDataBase.h"
#include "IControlView.h"
#include "ILayoutUIUtils.h"
#include "IGeometry.h"
#include "IShape.h"
#include "TransformUtils.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"
#include "PMString.h"
#include "SnapshotUtilsEx.h"
#include "AGMImageAccessor.h"

#include <vector>

#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // KESCMDrawEventHandler::sRasterizing
#include "KESCMCore.h"               // KESCMCollectPageUIDs
#include "KESCMColorSampler.h"

// pageRef のページを、spreadPt(そのページの spread 座標)まわりの極小領域だけ CMYK・高dpi でラスタ化し、
// 中心1画素の C,M,Y,K 生値(0..255)を out[4] に読む。アクセサ/スナップショットは即破棄(保持ゼロで
// 破棄時クラッシュを回避)。成功で kTrue。
static bool16 KESCMReadCmykPixel(const UIDRef& pageRef, const PMPoint& spreadPt, uint8 out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0;
	if (pageRef.GetDataBase() == nil || pageRef.GetUID() == kInvalidUID)
		return kFalse;

	// クリック点まわりの極小矩形(spread 座標)。boundsToSpreadMatrix=identity(=既に spread 座標)。
	const PMReal hp = kKESCMSampleHalfPt;
	PMRect clip(spreadPt.X() - hp, spreadPt.Y() - hp, spreadPt.X() + hp, spreadPt.Y() + hp);

	SnapshotUtilsEx* snap = new SnapshotUtilsEx(clip, PMMatrix(), pageRef, 1.0, 1.0,
		kKESCMSampleDpi, 72.0, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	KESCMDrawEventHandler::sRasterizing = kTrue;	// この Draw 中の再入でマークを描かせない
	ErrorCode drew = snap->Draw(IShape::kPreviewMode, kTrue /*fullRes*/, 7.0, kFalse /*AA off*/);
	KESCMDrawEventHandler::sRasterizing = kFalse;
	AGMImageAccessor* acc = (drew == kSuccess) ? snap->CreateAGMImageAccessor() : nil;

	bool16 ok = kFalse;
	if (acc != nil)
	{
		Int32Rect b = acc->GetBounds();
		const int32 w = b.right - b.left, h = b.bottom - b.top;
		const int32 rb = (int32)acc->GetRowBytes();
		const int32 bpp = (int32)acc->GetBitsPerPixel() / 8;
		const uint8* base = acc->GetBaseAddr();
		if (base != nil && w > 0 && h > 0 && rb > 0 && bpp >= 4)
		{
			const int32 cx = w / 2, cy = h / 2;	// 中心画素=クリック点
			const uint8* px = base + (size_t)cy * rb + (size_t)cx * bpp;
			out[0] = px[0]; out[1] = px[1]; out[2] = px[2]; out[3] = px[3];	// C,M,Y,K(offset 0)
			ok = kTrue;
		}
		delete acc;
	}
	delete snap;
	return ok;
}

// 0..255 の値を必ず3桁(ゼロ埋め)で追記する。Target/Source の C/M/Y/K の桁を縦に揃えて見やすくするため
// (AppendNumber はゼロ埋めしないので桁ごとに分けて出す)。
static void KESCMAppend3(PMString& s, int32 v)
{
	if (v < 0)   v = 0;
	if (v > 999) v = 999;
	s.AppendNumber(v / 100);
	s.AppendNumber((v / 10) % 10);
	s.AppendNumber(v % 10);
}

// CMYK ラスタの 8bit 値(0..255) を、本来の CMYK 数値である 0..100% に四捨五入で換算する。
// 例: 255→100 / 0→0 / 128→50。(v*100+127)/255 で round。
static int32 KESCMByteToPct(uint8 v)
{
	return ((int32)v * 100 + 127) / 255;
}

// Shift＋Ctrl＋Alt＋ミドルクリック: マウス下ページのクリック点 CMYK 生値を新(target)・旧(source)で
// サンプリングし、"Target C000 …(改行)Source C000 …"(各値3桁ゼロ埋め)を outMsg に組む。成功で kTrue。
//   新→旧ページは平坦通し番号で対応。クリック点を inner(ページ内)座標へ戻し、新/旧それぞれの spread
//   座標へ写してから各ページを極小ラスタ化する(新旧の幾何一致が前提)。
bool16 KESCMSampleCmykUnderMouse(IDataBase* targetDB, IDataBase* sourceDB, PMString& outMsg)
{
	if (targetDB == nil || sourceDB == nil)
		return kFalse;

	InterfacePtr<IControlView> view(Utils<ILayoutUIUtils>()->QueryFrontView());
	PMReal mx = 0.0, my = 0.0;
	if (!KESCMQueryMouseContentPoint(view, mx, my))
		return kFalse;

	// マウス下のページを特定(平坦通し番号も取得)。共有ヘルパ KESCMFindPageUnderMouse に集約。
	KESCMPageHit hit;
	if (!KESCMFindPageUnderMouse(targetDB, mx, my, hit))
		return kFalse;

	// 新→旧ページ対応(平坦通し番号)。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);
	const int32 gi = hit.globalPageBase + hit.hitPageIndex;
	if (gi >= (int32)sPages.size())
		return kFalse;

	const UID tPageUID = hit.hitPageUID;
	const UID sPageUID = sPages[gi];
	InterfacePtr<IGeometry> tGeo(targetDB, tPageUID, UseDefaultIID());
	InterfacePtr<IGeometry> sGeo(sourceDB, sPageUID, UseDefaultIID());
	if (tGeo == nil || sGeo == nil)
		return kFalse;

	// クリック点(pasteboard) → ページ内(inner)座標 → 新/旧それぞれの spread 座標。
	PMMatrix mPB = ::InnerToPasteboardMatrix(tGeo);
	if (mPB.IsSingular())
		return kFalse;
	PMPoint inner(mx, my);
	mPB.Inverse().Transform(&inner);

	PMPoint tSpreadPt(inner.X(), inner.Y());
	::InnerToSpreadMatrix(tGeo).Transform(&tSpreadPt);
	PMPoint sSpreadPt(inner.X(), inner.Y());
	::InnerToSpreadMatrix(sGeo).Transform(&sSpreadPt);

	uint8 cN[4], cO[4];
	const bool16 okN = KESCMReadCmykPixel(UIDRef(targetDB, tPageUID), tSpreadPt, cN);
	const bool16 okO = KESCMReadCmykPixel(UIDRef(sourceDB, sPageUID), sSpreadPt, cO);
	if (!okN || !okO)
		return kFalse;

	// ラベルは ASCII。1行目=Target(新/cN)、改行(LF)、2行目=Source(旧/cO)。各値はラスタ8bit(0..255)を
	// 本来の CMYK 数値 0..100% に換算し、3桁ゼロ埋めで桁を縦に揃える。
	outMsg.SetTranslatable(kFalse);
	outMsg.Append("Target\tC"); KESCMAppend3(outMsg, KESCMByteToPct(cN[0]));	// TAB=ラベル列/値列の区切り(値の桁を固定列で縦揃え)
	outMsg.Append(" M");       KESCMAppend3(outMsg, KESCMByteToPct(cN[1]));
	outMsg.Append(" Y");       KESCMAppend3(outMsg, KESCMByteToPct(cN[2]));
	outMsg.Append(" K");       KESCMAppend3(outMsg, KESCMByteToPct(cN[3]));
	outMsg.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
	outMsg.Append("Source\tC"); KESCMAppend3(outMsg, KESCMByteToPct(cO[0]));	// TAB=ラベル列/値列の区切り(値の桁を固定列で縦揃え)
	outMsg.Append(" M");       KESCMAppend3(outMsg, KESCMByteToPct(cO[1]));
	outMsg.Append(" Y");       KESCMAppend3(outMsg, KESCMByteToPct(cO[2]));
	outMsg.Append(" K");       KESCMAppend3(outMsg, KESCMByteToPct(cO[3]));
	return kTrue;
}
