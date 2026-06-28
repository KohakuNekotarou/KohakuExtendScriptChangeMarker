//========================================================================================
//
//  KESCMDrawEventHandler.cpp
//
//  差分オーバーレイ描画エンジンの実装(旧 KESCMScriptProvider.cpp から分離)。リング/変更数/
//  旧版べた載せ/トーストの描画、比較ラスタ化(MakeEntry/MakeOrigImage)、各種画像ヘルパを持つ。
//  共有状態(static メンバ)と KESCMQueryPanorama は KESCMDrawEventHandler.h で公開している。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル / 描画 / ラスタ化(エンジンが使う SDK ヘッダ):
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IGeometry.h"
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "ILayoutUIUtils.h"
#include "IApplication.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IShape.h"
#include "IDrwEvtHandler.h"
#include "IDrwEvtDispatcher.h"
#include "CServiceProvider.h"
#include "DocumentContextID.h"
#include "GraphicsID.h"
#include "GraphicsData.h"
#include "IViewPortAttributes.h"
#include "OutPrvID.h"
#include "IGraphicsPort.h"
#include "AutoGSave.h"
#include "IControlView.h"
#include "IPanorama.h"
#include "IWidgetParent.h"
#include "ISession.h"
#include "IFontMgr.h"
#include "IPMFont.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMReal.h"
#include "TransformUtils.h"
#include "SnapshotUtilsEx.h"
#include "AGMImageAccessor.h"
#include "GraphicsExternal.h"
#include "IXPUtils.h"

#include <map>
#include <vector>
#include <string.h>

// プロジェクト内インクルード:
#include "KESCMID.h"
#include "KESCMDrawEventHandler.h"

CREATE_PMINTERFACE(KESCMDrawEventHandler, kKESCMDrawEventHandlerImpl)

std::map<UID, KESCMOverlayEntry*> KESCMDrawEventHandler::sEntries;
IDataBase* KESCMDrawEventHandler::sDB = nil;
bool16 KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定=非表示。枠等はシングルミドル押下中だけ表示(master トグル)
PMReal KESCMDrawEventHandler::sMarkScreenOpacity = 1.0;	// 既定=不透明。ミドルのみ=25%/Shift+Alt=不透明/印刷25%中の常時表示=25%
bool16 KESCMDrawEventHandler::sPrintMarks = kFalse;	// 既定=画面のみ(印刷/PDF には出さない)
bool16 KESCMDrawEventHandler::sPrintFaint = kTrue;	// 既定=印刷時は約25%(パネルの既定ラジオ「25%」と一致)。印刷OFF中は未参照
bool16 KESCMDrawEventHandler::sRasterizing = kFalse;	// 自前ラスタ化中だけ kTrue(自己参照防止)
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
	// ★端クリップ対策: バッファ(=ページ矩形)端から radius 以内に変化があると、外側の帯がページ端を
	//   越える分はバッファ外=描かれず、その辺の枠が痩せて欠ける。対策は「端から radius 以内の変化画素を
	//   内側帯として塗る」だけでよい。ある変化画素が x<radius にあれば、領域は左端から radius 以内に
	//   到達済み=左の外側帯は必ずクリップされるので、接触判定(旧 drow[0] 等)は不要。4辺とも対称に扱う。

	// 距離変換の1パス塗り。リング = 0<dist<=radius(=「半径内に変化画素があり、かつ自身は変化画素でない」)。
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
			bool16 ring = (d != 0 && d <= rad);		// 外側の帯(従来)
			if (!ring && d == 0)
			{
				// 変化画素が端から radius 以内にあれば、その端の外側帯はクリップ済み=内側に補填する。
				if (x < radius            || (wt - 1 - x) < radius ||
				    y < radius            || (ht - 1 - y) < radius)
					ring = kTrue;
			}
			if (ring)
			{
				// リング画素。下の実ページが赤っぽければ青、そうでなければ赤(画素単位)。
				const bool useAlt = (brow != nil && brow[x]);
				px[0] = useAlt ? kKESCMRingAltR : kKESCMRingR;
				px[1] = useAlt ? kKESCMRingAltG : kKESCMRingG;
				px[2] = useAlt ? kKESCMRingAltB : kKESCMRingB;
				if (bpp >= 4) pixT[0] = kKESCMRingAlpha;	// リング画素の基本アルファ(=255 不透明)。薄表示は setopacity 側
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
// ヘルパ: 差分マスク(0/1)のチェスボード距離変換 → out(uint8, 0=変化画素, clamp255)。
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

	// ラスタ化は3回から2回へ削減。旧版は別途 72dpi の target(snapL)もラスタ化していたが、その画素は
	//   BuildRing が buf を全上書きするため一切使われていなかった。低解像度の寸法は高解像度から割り戻し、
	//   背景の「赤っぽい」判定(bgRed)も高解像度 target をプーリングして作るので、snapL は不要=削除。
	// 【高解像度】差分検出用。target / source を高dpi(kKESCMResolution×kKESCMHiResMul)でラスタ化。
	// 低解像度では平均化で消える細線/微小ズレを満額の差分画素として拾い、取りこぼしを防ぐ。
	const PMReal hiRes = kKESCMResolution * kKESCMHiResMul;
	// 比較は常に CMYK 4ch を不透明ラスタ化して行う(CMYK の微差が RGB 変換で消えるのを回避)。
	// 表示リングは別途 ARGB で合成するので、比較ラスタは不透明(addTransparencyAlpha=kFalse)でよい。
	SnapshotUtilsEx* snapTH = new SnapshotUtilsEx(targetRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	sRasterizing = kTrue;	// この Draw 中に再入する HandleDrawEvent はマークを描かない(自己参照防止)
	ErrorCode drewTH = snapTH->Draw(IShape::kPreviewMode);
	sRasterizing = kFalse;
	AGMImageAccessor* accTH = (drewTH == kSuccess) ? snapTH->CreateAGMImageAccessor() : nil;

	SnapshotUtilsEx* snapSH = new SnapshotUtilsEx(sourceRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	sRasterizing = kTrue;
	ErrorCode drewSH = snapSH->Draw(IShape::kPreviewMode);
	sRasterizing = kFalse;
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
		const int32 bppL = 4;				// 表示リングは常に自前 ARGB(=4)合成。比較ラスタの ch 数(RGB=4/CMYK=4)とは独立
		const int32 rbL = wl * bppL;		// 自前バッファ=行パディング無し

		if (ptH != nil && psH != nil &&
			wth == wsh && hth == hsh && rbTH == rbSH && rbTH > 0 &&
			bppH >= 4 && wl > 0 && hl > 0)
		{
			const size_t N = (size_t)wl * hl;
			uint8*  M     = new uint8[N];	// 低解像度マスク(保存): プーリング結果
			uint16* cntHi = new uint16[N];	// 低解像度セルごとの「高解像度の変化画素数」(プーリング用一時)
			if (M != nil && cntHi != nil)
			{
				memset(cntHi, 0, N * sizeof(uint16));

				// 【高解像度で比較 → 低解像度セルへ散らす(scatter)】
				// 高解像度の各画素を差分判定(生の各チャンネル最大差>しきい値)し、変化していたら
				// 対応する低解像度セルのカウンタを増やす。セル写像は寸法比(高/低が整数倍でなくてもよい)。
				// CMYK 比較: 先頭から4ch(offset=0)。各chの最大差がしきい値(kKESCMCmykThr)を超えたら変化画素。
				const int  nch       = 4;
				const int32 colorOffH = 0;
				const int  thr        = kKESCMCmykThr;
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
						int cm = 0;
						for (int c = 0; c < nch; ++c)
						{
							const int d = (px[c] > sx[c]) ? px[c] - sx[c] : sx[c] - px[c];
							if (d > cm) cm = d;
						}
						if (cm > thr)
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
					// (低解像度 snapL を廃止。低解像度セル中心の高解像度画素1点を代表サンプルに)。
					// CMYK 経路は RGB が無いので、サンプル CMYK を近似 RGB に変換してから同じ R 優位判定を使う。
					const int32 colorOffT = 0;
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
								// CMYK(0..255) → 近似 RGB: ch=(255-ink)*(255-K)/255 の簡易式
								const int C = px[0], Mk = px[1], Yk = px[2], K = px[3];
								const int r = (255 - C)  * (255 - K) / 255;
								const int g = (255 - Mk) * (255 - K) / 255;
								const int b = (255 - Yk) * (255 - K) / 255;
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
					// mask M から距離変換 dist を1回だけ作って保持(以後の BuildRing はこれ1つで描ける)。
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

	// 後始末: 2つのスナップショット/アクセサを破棄(ラスタ化は2回=低解像度 snapL は廃止)。
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
	sRasterizing = kTrue;	// この Draw 中に再入する HandleDrawEvent はマークを描かない(自己参照防止)
	ErrorCode drew = snap->Draw(IShape::kPreviewMode);
	sRasterizing = kFalse;
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
			uint8* obuf = (o != nil) ? new uint8[(size_t)rb * h] : nil;
			if (o == nil || obuf == nil)
			{
				// allocation failed: free any partial state and bail (same safety as MakeEntry)
				if (obuf) delete[] obuf;
				if (o)    delete o;
				if (acc)  delete acc;
				if (snap) delete snap;
				return kFailure;
			}
			o->buf = obuf;
			o->w = w;  o->h = h;  o->rowBytes = rb;  o->bpp = bpp;
			memcpy(o->buf, p, (size_t)rb * h);
			// 不透明保証: ARGB(alpha 先頭)なら alpha を 255 に揃える(べた載せ=下が透けない)。
			// まず格子状(約8×8点)にサンプリングし、全サンプルが既に 255(不透明)なら O(W*H) の
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
	// スプレッド単位で配られる描画イベント。ポートは spread 座標。枠/変更数はこちらで描く。
	// トーストもこちらで描く=スプレッド/ペーストボード帯の「前面」分(帯外はクリップされる)。
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
	// ウィンドウ単位(全スプレッド描画後に1回)。ポートは CTM=pasteboard。スプレッド/ペーストボードの
	// 背面に来るため、トーストの「帯外=カンバス背景」分だけをこちらで描く(2系統併用で全域カバー)。
	d->RegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this, kDEHLowestPriority);
}

void KESCMDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
	d->UnRegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this);
}


// ビューから IPanorama を取る。ページアイテム系の子ウィジェットは panorama を持たないため、
// CTracker::QueryPanorama と同じく自身→親(LayoutWidget)の順で辿る。呼び出し側で Release すること。
IPanorama* KESCMQueryPanorama(IControlView* view)
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
// pasteboard 座標 → このスプレッドの spread 座標 への変換オフセット(= pasteboard - spread)。
//   pasteboard 座標はドキュメント全体で1つ。スプレッドは pasteboard 上で(主に縦に)積まれ、各々が
//   オフセットを持つ(spread[0] だけ偶然 0)。同一の inner 原点(0,0)を InnerToSpreadMatrix と
//   InnerToPasteboardMatrix の両方で写し、その差を取ればこのスプレッドのオフセットになる。
//   pasteboard 中心からこれを引けば、そのスプレッドの spread 座標における中心が得られる。
//========================================================================================
static PMPoint KESCMSpreadOffsetFromPasteboard(IDataBase* db, ISpread* spread)
{
	PMPoint off(0.0, 0.0);
	if (db == nil || spread == nil || spread->GetNumPages() < 1)
		return off;
	InterfacePtr<IGeometry> pg(db, spread->GetNthPageUID(0), UseDefaultIID());
	if (pg == nil)
		return off;
	PMMatrix mS = ::InnerToSpreadMatrix(pg);
	PMMatrix mP = ::InnerToPasteboardMatrix(pg);
	PMPoint ps(0.0, 0.0), pp(0.0, 0.0);
	mS.Transform(&ps);
	mP.Transform(&pp);
	return PMPoint(pp.X() - ps.X(), pp.Y() - ps.Y());
}

//========================================================================================
// 一時トースト描画: 指定中心(centerPort)に、半透明の暗いボックス＋白文字でメッセージを描く。
//   中心は「呼び出すポートの座標系」で渡す:
//     - kEndSpreadMessage(per-spread)        → spread 座標(スプレッド/ペーストボード帯の前面)
//     - kAfterLastSpreadDrawMessage(ウィンドウ)→ pasteboard 座標(帯外=カンバス背景)
//   この2系統を併用すると、各画素はどちらか一方が担当=二重描き無しで全域(スプレッド+ペースト
//   ボード+カンバス)をカバーできる。可視/db/中心の有効性チェックは呼び出し側で済ませる。
//   サイズはズーム不変(画面px / sxr)。
//========================================================================================
static void KESCMDrawToast(IGraphicsPort* gPort, PMReal sxr, const PMPoint& centerPort)
{
	if (gPort == nil || sxr <= 0)
		return;

	const PMReal sX = centerPort.X();
	const PMReal sY = centerPort.Y();

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

	// 改行(LF=0x0A)で行に分割(最大 kMaxLines 行)。改行が無ければ 1 行=従来どおり。各行は中央寄せで縦に積む。
	const int32 kMaxLines = 8;
	int32 lineStart[kMaxLines], lineLen[kMaxLines], nLines = 0;
	int32 st = 0;
	for (int32 i = 0; i <= nch; ++i)
	{
		if (i == nch || mbuf[i] == 0x0A)
		{
			if (nLines < kMaxLines) { lineStart[nLines] = st; lineLen[nLines] = i - st; ++nLines; }
			st = i + 1;
		}
	}
	if (nLines <= 0)
		return;

	// 各行を任意の TAB(0x09)で「ラベル列(seg0)」と「値列(seg1)」に分ける。値列を全行同じ X(固定列)から
	// 描くと、ラベル(Target/Source)の実幅が違っても値(C000 …)の桁がぴったり揃う。TAB が無ければその行は
	// seg0 のみ＝中央寄せ(単一行トースト等は従来どおり)。
	int32 seg0Len[kMaxLines], seg1Off[kMaxLines], seg1Len[kMaxLines];
	bool16 anyTab = kFalse;
	for (int32 L = 0; L < nLines; ++L)
	{
		int32 tabRel = -1;
		for (int32 k = 0; k < lineLen[L]; ++k)
			if (mbuf[lineStart[L] + k] == 0x09) { tabRel = k; break; }
		if (tabRel >= 0)
		{
			seg0Len[L] = tabRel;
			seg1Off[L] = tabRel + 1;
			seg1Len[L] = lineLen[L] - (tabRel + 1);
			anyTab = kTrue;
		}
		else { seg0Len[L] = lineLen[L]; seg1Off[L] = lineLen[L]; seg1Len[L] = 0; }
	}

	// 列幅(em)とコンテンツ幅(em)を見積もる。TAB あり=ラベル列幅+間隔+値列幅、無し=行全体の最大幅。
	PMReal col0Em = 0.0, col1Em = 0.0, maxLineEm = 0.0;
	for (int32 L = 0; L < nLines; ++L)
	{
		const PMReal e0 = KESCMEstimateTextEm(&mbuf[lineStart[L]], seg0Len[L]);
		const PMReal e1 = (seg1Len[L] > 0) ? KESCMEstimateTextEm(&mbuf[lineStart[L] + seg1Off[L]], seg1Len[L]) : PMReal(0.0);
		if (e0 > col0Em) col0Em = e0;
		if (e1 > col1Em) col1Em = e1;
		const PMReal whole = KESCMEstimateTextEm(&mbuf[lineStart[L]], lineLen[L]);
		if (whole > maxLineEm) maxLineEm = whole;
	}
	const PMReal gapEm     = PMReal(0.6);					// ラベル列と値列の間隔(em)
	const PMReal contentEm = anyTab ? (col0Em + gapEm + col1Em) : maxLineEm;

	const PMReal textW   = fpt * contentEm;
	const PMReal lineGap = fpt * PMReal(1.20);				// 行送り(2 行目以降の縦間隔)
	const PMReal boxW    = textW + pad * PMReal(2.0);
	const PMReal boxH    = fpt + PMReal(nLines - 1) * lineGap + pad * PMReal(2.0);
	const PMReal x0      = sX - boxW / PMReal(2.0);
	const PMReal y0      = sY - boxH / PMReal(2.0);

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
	// 白文字。show は baseline 左端基準。1 行目の縦中央 ≒ y0 + pad + fpt*0.78。
	gPort->selectfont(theFont, fpt);
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	const PMReal contentLeft = x0 + pad;					// テキストブロックの左端(ボックス内で中央寄せ)
	const PMReal col1X       = contentLeft + fpt * (col0Em + gapEm);	// 値列(seg1)の共通左端=固定列
	for (int32 L = 0; L < nLines; ++L)
	{
		const PMReal ty = y0 + pad + fpt * PMReal(0.78) + PMReal(L) * lineGap;
		if (anyTab)
		{
			// 列レイアウト: ラベルは左端、値は固定列(col1X)から。両行で値の桁が揃う。
			if (seg0Len[L] > 0)
				gPort->show(contentLeft, ty, seg0Len[L], &mbuf[lineStart[L]], (IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
			if (seg1Len[L] > 0)
				gPort->show(col1X, ty, seg1Len[L], &mbuf[lineStart[L] + seg1Off[L]], (IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
		}
		else
		{
			// 中央寄せ(従来=単一行トースト等)。
			if (lineLen[L] <= 0)
				continue;
			const PMReal lw = fpt * KESCMEstimateTextEm(&mbuf[lineStart[L]], lineLen[L]);
			const PMReal tx = sX - lw / PMReal(2.0);
			gPort->show(tx, ty, lineLen[L], &mbuf[lineStart[L]], (IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
		}
	}
}


//========================================================================================
// 印刷/PDF 用のリング描画。画面は image() blit でよいが(画素 alpha を honor する)、印刷のフラットナ
// 経路は blit 画像の部分 alpha を honor せず枠が不透明になる。そこで transparencyeffect サンプルと
// 同じ作法=リング形状を「グレーのアルファサーバ」にして純色のベクター fill を setopacity で半透明に
// 描く(透明合成エンジンが honor する)。赤と青(背景適応)を保つため、赤画素・青画素それぞれのグレー
// マスクで2回 fill する。呼び出し側で translate/scale 済み(user 空間 = 画像px)であること。
//   e->buf は ARGB(先頭=alpha, 続いて R,G,B)。
//========================================================================================
static void KESCMDrawRingForPrint(IGraphicsPort* gPort, KESCMOverlayEntry* e)
{
	if (gPort == nil || e == nil || e->buf == nil || e->w <= 0 || e->h <= 0 || e->bpp < 4)
		return;
	// 透明合成ユーティリティ(アルファサーバ生成/解放に使う)。実行中アプリでは常在するが、
	// transparencyeffect サンプル流に、取得できなければ何もしない(クラッシュ回避)。以後この1個を使い回す。
	Utils<IXPUtils> xpUtils;
	if (!xpUtils)
		return;
	const int32 w = e->w, h = e->h, rb = e->rowBytes, bpp = e->bpp;
	const size_t N = (size_t)w * h;

	// e->buf(ARGB)から、赤リング画素=255 / 青リング画素=255 の2枚のグレーマスクを作る。
	uint8* maskR = new uint8[N];
	uint8* maskB = new uint8[N];
	if (maskR == nil || maskB == nil) { if (maskR) delete[] maskR; if (maskB) delete[] maskB; return; }
	for (int32 y = 0; y < h; ++y)
	{
		const uint8* row = e->buf + (size_t)y * rb;
		for (int32 x = 0; x < w; ++x)
		{
			const uint8* px  = row + (size_t)x * bpp;	// [alpha, R, G, B]
			const size_t idx = (size_t)y * w + x;
			if (px[0] != 0)								// リング画素(alpha!=0)
			{
				const bool16 blue = (px[3] > px[1]);	// B>R = 青(背景適応で青に切り替わった画素)
				maskR[idx] = blue ? 0 : 255;
				maskB[idx] = blue ? 255 : 0;
			}
			else { maskR[idx] = 0; maskB[idx] = 0; }
		}
	}

	// リングの不透明度。通常=画面と同じ(kKESCMRingAlpha/255=1.0 不透明) / faint=約25%(kKESCMFaintOpacity)。
	const PMReal op = KESCMDrawEventHandler::sPrintFaint ? kKESCMFaintOpacity : (kKESCMRingAlpha / PMReal(255.0));
	struct PassDef { uint8* buf; uint8 r, g, b; };
	PassDef passes[2] = { { maskR, 255, 0, 0 }, { maskB, 0, 0, 255 } };	// 赤 / 青

	for (int p = 0; p < 2; ++p)
	{
		// マスクを指すグレー(8bpp, alpha無し)の AGMImageRecord。アルファサーバは gray colorspace 必須。
		AGMImageRecord mrec;
		mrec.bounds.xMin = 0;            mrec.bounds.yMin = 0;
		mrec.bounds.xMax = (int16)w;     mrec.bounds.yMax = (int16)h;
		mrec.baseAddr     = passes[p].buf;
		mrec.byteWidth    = w;								// 1byte/px, 行パディング無し
		mrec.colorSpace   = (int16)kGrayColorSpace;
		mrec.bitsPerPixel = 8;
		mrec.decodeArray  = nil;
		mrec.colorTab.numColors = 0;     mrec.colorTab.theColors = nil;

		PMMatrix idm;										// 恒等。user 空間=画像px なので画素(x,y)→user(x,y)
		AGMPaint* alphaPaint = xpUtils->CreateImagePaintServer(&mrec, &idm, 0, nil);
		if (alphaPaint != nil)
		{
			AutoGSave ag(gPort);
			gPort->SetAlphaServer(alphaPaint, kTrue, PMMatrix());	// 形状=リング画素(per-pixel)
			gPort->setopacity(op, kFalse);							// 半透明(透明合成が honor)
			gPort->setrgbcolor(passes[p].r / PMReal(255.0), passes[p].g / PMReal(255.0), passes[p].b / PMReal(255.0));
			gPort->newpath();
			gPort->rectpath(PMReal(0.0), PMReal(0.0), PMReal(w), PMReal(h));	// user 空間=画像px(呼び出し側で translate/scale 済)
			gPort->fill();
			xpUtils->ReleasePaintServer(alphaPaint);
		}
	}

	delete[] maskR;
	delete[] maskB;
}


bool16 KESCMDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;
	// 自前のラスタ化(MakeEntry の比較スナップショット / MakeOrigImage の旧版スナップショット)中の再入は
	// 描かない(自己参照=マークがスナップショットに写り込む feedback を防ぐ)。以前は kPreviewMode ビットで
	// 弾いていたが、それは PDF 書き出しの kPDFExportMode と同一ビット(4096)で export を巻き込んでいたため、
	// 明示的な再入フラグ sRasterizing に置き換えた。
	if (sRasterizing)
		return kFalse;
	// 印刷文脈か(kPrinting=512)。印刷時はマークの ON/OFF を sPrintMarks で決める。通常の画面描画では立たない。
	// ※PDF 書き出し(File>Export)はこのスプレッド描画イベントを発火しないため対象外(print-to-PDF を使う)。
	// 自己参照(自前スナップショット)は上の sRasterizing で防ぐので、ここで kPreviewMode は見ない。
	const bool16 printing = (ded->flags & IShape::kPrinting) != 0;
	// オーバープリントプレビュー(OPP)中か。OPP は「印刷の見え方」を画面でシミュレートするモードなので、
	// 枠は基本非印刷=OPP でも「枠の印刷」OFF なら隠す(以前 kPreviewMode で弾いていた挙動の復帰)。ただし
	// kPreviewMode(4096) は PDF 書き出しの kPDFExportMode と同一ビットで衝突するため使わず、OPP 専用の
	// ビューポート属性 kSepPrvOPPEnabledVPAttr で正確に判定する(PDF export とは衝突しない)。
	bool16 overprintPreview = kFalse;
	IViewPortAttributes* vpa = ded->gd->GetViewPortAttributes();	// ded->gd は冒頭で非nil確認済み
	if (vpa != nil)
		overprintPreview = (vpa->GetAttr(kSepPrvOPPEnabledVPAttr, 0) != 0);
	// 印刷 or オーバープリントプレビューで「枠の印刷」が OFF のときは描かない(枠は基本非印刷)。
	if ((printing || overprintPreview) && !sPrintMarks)
		return kFalse;
	// マークも 旧版べた載せ も トースト も無ければ何もしない。
	if (sEntries.empty() && !(sShowOriginal && !sOrigImages.empty()) && !sToastVisible)
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
	PMPoint centerPb(0.0, 0.0);		// 可視領域の中心(pasteboard 座標)。トーストの基準位置に使う
	bool16  hasCenter = kFalse;		// 上記が有効か(panorama を辿れた=画面描画時のみ)
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
			centerPb = pano->GetContentLocationAtFrameCenter();	// 可視中心(content=pasteboard 座標)
			hasCenter = kTrue;
		}
	}

	// ★印刷/PDF 時は「100% 表示の見た目」に固定する(ズーム連動を切る)。印刷ポートには view が無く
	// sxr=0 / pano=nil になるので、実効 sxr=1.0(=100%・deviceScale 1 相当)と 100% の文字倍率を与える。
	// これでリング太さ・数値サイズ・数値位置の各式が、画面 100% 表示時とちょうど同じ値になる(下流の
	// ズーム適応式・フォールバック式をそのまま使い回せる)。画面描画(printing=false)は従来どおりズーム連動。
	if (printing)
	{
		sxr = 1.0;
		countMul = KESCMCountSizeMul(1.0);
	}

	// ★ウィンドウ単位イベント(全スプレッド描画後, CTM=pasteboard)= トーストの「帯外(カンバス背景)」担当。
	// このポートはスプレッド/ペーストボードの背面に来るため、何も被さらないカンバス部分にだけ見える。
	// スプレッド/ペーストボード帯の前面分は per-spread(kEndSpreadMessage)側で描く(下)。
	// 枠/旧版/変更数もスプレッド単位側で描く。トーストは一時メッセージなので印刷/PDF には出さない。
	if (eventID == ClassID(kAfterLastSpreadDrawMessage))
	{
		if (!printing && sToastVisible && db == sToastDB && hasCenter && sxr > 0)
			KESCMDrawToast(gPort, sxr, centerPb);	// pasteboard 座標の中心へ直接
		return kFalse;
	}

	// 今描いている「このスプレッド」を覗いている(旧版べた載せ中)か。覗きで旧版が乗るのはマウス下の1スプレッド
	// だけ(そのページが sOrigImages にある)。覗き中のスプレッドだけ旧版をきれいに見せたいので、マーク
	// (枠/変更数)を描かない。それ以外のスプレッドは通常どおりマークを描く。
	bool16 peekingThisSpread = kFalse;
	if (!printing && sShowOriginal && !sOrigImages.empty() && sOrigDB != nil && db == sOrigDB)
	{
		const int32 npChk = spread->GetNumPages();
		for (int32 i = 0; i < npChk; ++i)
			if (sOrigImages.find(spread->GetNthPageUID(i)) != sOrigImages.end())
			{ peekingThisSpread = kTrue; break; }
	}

	// 旧版べた載せ — マーク(sEntries)とは独立。覗き中のスプレッドの各ページに旧版画像を不透明で
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

	// トースト — スプレッド/ペーストボード帯の「前面」に出すため per-spread(spread座標)ポートでも描く。
	// このポートは帯にクリップされる(帯外は欠ける)ので、帯外=カンバスは kAfterLastSpreadDrawMessage 側が担う。
	// window 中心(centerPb=pasteboard座標)をこのスプレッドのオフセット分だけ引いて spread 座標の中心へ変換する。
	// per-spread は可視スプレッドごとに発火するので、箱が複数スプレッドにまたがっても各帯で前面に出る。
	if (!printing && sToastVisible && db == sToastDB && hasCenter && sxr > 0)
	{
		PMPoint off = KESCMSpreadOffsetFromPasteboard(db, spread);
		PMPoint cS(centerPb.X() - off.X(), centerPb.Y() - off.Y());
		KESCMDrawToast(gPort, sxr, cS);
	}

	// 変更オーバーレイ(リング＋変更数) — マーク済みドキュメントが現スプレッドの db と一致する時だけ。
	// master 表示トグル(sMarksVisible)が OFF の間、またはこのスプレッドを覗き中(旧版べた載せ中)は描かない
	// (データは保持=再表示で即復帰)。覗いていない他のスプレッドのマークは通常どおり残る。
	// ★印刷マーク(sPrintMarks)が ON の間は、ミドル押下に関係なく常に描く(画面=WYSIWYG / 印刷・PDF にも出る)。
	if (peekingThisSpread || !(sPrintMarks || sMarksVisible) || sEntries.empty() || sDB == nil || db != sDB)
		return kFalse;

	// 画面マークの実効不透明度。sMarkScreenOpacity は常に実効値を保持する(下の各ソースが設定):
	//   ・既定/印刷通常 = 1.0(不透明)  ・印刷25%選択中(常時表示) = 0.3
	//   ・ミドルのみ押下中 = 0.25        ・Shift+Alt 押下中 = 1.0(不透明=印刷25%中でも不透明で確認できる)
	// 離すと印刷設定に応じた基準値(KESCMBaseScreenOpacity)へ戻る。printing 経路はここを使わない。
	const PMReal screenMarkOp = sMarkScreenOpacity;

	// 変更数テキスト用の既定フォントは、このスプレッドの全ページで共通。ページループ内で毎回
	// 取得すると変更ページ数ぶん無駄に問い合わせるので、ループ外で1回だけ取得して使い回す。
	InterfacePtr<IFontMgr> countFontMgr(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IPMFont> countFont(countFontMgr ? countFontMgr->QueryFont(countFontMgr->GetDefaultFontName()) : nil);

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
				// 量子化を 2px→4px 刻みに。ズーム中に R が変わる回数(=BuildRing 再計算)がほぼ半減。
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
			gPort->scale(pr.Width() / iw, pr.Height() / ih);	// 画像px → ページ矩形にフィット
			// ★印刷/PDF 時は image() blit だと枠が不透明になる(フラットナが画像の部分 alpha を honor しない)。
			// アルファサーバ＋純色ベクター fill＋setopacity で半透明に描く(透明合成エンジンが honor)。
			// 画面描画(printing=false)は従来の ARGB blit のまま(画素 alpha を honor=検証済みの見た目を維持)。
			if (printing)
				KESCMDrawRingForPrint(gPort, e);
			else
			{
				// 画面 blit は image() の画素 alpha に加えてポート opacity も honor する。薄表示(Shift+Alt+ミドル)
				// 中や 25%設定中は screenMarkOp(≒0.25)、通常は 1.0。AutoGSave 内なので閉じれば元へ戻る。
				gPort->setopacity(screenMarkOp, kFalse);
				gPort->image(&e->rec, PMMatrix(), 0);			// 自前レコード(buf を指す)を blit
			}
		}

		// この頁の変更数「N chg」を常時表示(常時・トグル無し)。リング画像の上に重ねる。
		// 文字サイズは拡大率連動(画面px = 既定px × countMul、px→pt は /sxr)。framelabel 流: session→IFontMgr→
		// 既定フォントを selectfont し show(数字=白ストローク縁＋赤fill＋赤ストローク(やや太字)、語=青fill のみ)。
		{
			AutoGSave agx(gPort);
			if (e->changeCount > 0)
			{
				IPMFont* theFont = countFont;	// ループ外で取得済みの既定フォントを使い回す
				if (theFont != nil)
				{
					// 画面pxサイズに拡大率連動の倍率 countMul を掛けてから px→pt(/sxr)へ。
					const PMReal numPt  = (sxr > 0) ? (kKESCMCountTextPx * countMul / sxr) : (pr.Width() / PMReal(24));
					const PMReal wordPt = (sxr > 0) ? (kKESCMCountWordPx * countMul / sxr) : (pr.Width() / PMReal(48));

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

					// 【縦位置=ページ上端に固定】数字の上端をページ上端にフラッシュで揃える(スクロール追従なし)。
					// show は baseline 基準ゆえ、概ね文字高さぶん(≒0.78em)下げて文字上端をページ上端に合わせ、
					// さらに kKESCMCountDropPx(画面px)ぶん下へ。drop は px→spread 換算(/sxr)で一定px見えにする。
					// 横位置は従来どおりページ横中央(startX)。数字・語の共通ベースライン。
					const PMReal drop = (sxr > 0) ? (kKESCMCountDropPx / sxr) : (pr.Width() / PMReal(240));
					const PMReal ty = pr.Top() + numPt * PMReal(0.78) + drop;

					// 変更数テキストの不透明度: 印刷時は通常=1.0 / faint=約25%(リングと同率)。画面時は screenMarkOp
					// (通常=1.0 / Shift+Alt薄表示中・25%設定中=約0.25)。リングと同じ実効値で揃える。
					const PMReal textOp = printing ? (sPrintFaint ? kKESCMFaintOpacity : PMReal(1.0)) : screenMarkOp;
					// 薄表示中(<1.0)は数字の重ね描き(赤fill＋赤太らせ)が半透明で足し合わさり濃い赤が残るため、太字化を省く。
					const bool16 faintText = (textOp < PMReal(0.999));

					// テキストの不透明度。画面=screenMarkOp / 印刷=通常1.0・faint0.25。なお印刷フラットナは
					// text show() の setopacity を無視するため、印刷の薄表示は別途「白縁を半分＋外縁を白で削って
					// 細く見せる」で表現している(透明化は断念済み)。ここでは画面の薄表示にだけ効く。
					gPort->setopacity(textOp, kFalse);
					gPort->selectfont(theFont, numPt);
					const UTF16TextChar* numBuf = numStr.GrabUTF16Buffer(nil);

					// 数字の白い縁: 先に白を太めのストロークで描いてハローを作る。印刷時は白縁を半分に細く。
					gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));		// 白
					gPort->setlinewidth(numPt * (printing ? kKESCMCountPrintHaloFrac : kKESCMCountHaloFrac));
					gPort->show(startX, ty, numCh, numBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));

					// 数字の本体: まず塗り(kFillText 単独)で中までベタ赤に。合成フラグ(kFillText|kStrokeText)だと
					// このポートでは塗りが効かず輪郭だけ赤になるため、塗りと縁取りを2回の show に分ける。
					gPort->setrgbcolor(kKESCMCountNumR, kKESCMCountNumG, kKESCMCountNumB);
					gPort->show(startX, ty, numCh, numBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
					// 同色の細い赤ストロークで少し太らせる。輪郭中心ゆえ半分が外へ膨らみ、外側の白ハローを
					// 僅かに侵食して赤を太く見せる(白ハローは更に外側に残る=「赤文字に白縁」)。
					// 印刷時は太らせない(細く出したいので fill のみに留める)。
					if (!faintText && !printing)
					{
						gPort->setlinewidth(numPt * kKESCMCountBodyFrac);
						gPort->show(startX, ty, numCh, numBuf,
							(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));
					}
					// 印刷時のみ: 紙白(白)を細いストロークで上描きして赤fillの外縁を削り、数字を細く見せる
					// (ストロークは輪郭中心ゆえ内側半分が赤を侵食=erode。白ハローは更に外側に残る)。白紙前提。
					if (printing)
					{
						gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));	// 紙白
						gPort->setlinewidth(numPt * kKESCMCountPrintThinFrac);
						gPort->show(startX, ty, numCh, numBuf,
							(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));
					}

					// 語: 小さめ・細め(fill のみ=ストローク無し)・青。数字の直後・同じベースライン。
					gPort->setrgbcolor(kKESCMMarkR, kKESCMMarkG, kKESCMMarkB);
					gPort->selectfont(theFont, wordPt);
					const UTF16TextChar* wordBuf = wordStr.GrabUTF16Buffer(nil);
					gPort->show(startX + numW, ty, wordCh, wordBuf,
						(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kFillText));
					// 印刷時のみ: 数字と同様に紙白を細いストロークで上描きして語の外縁を削り、半分くらい細く見せる。
					if (printing)
					{
						gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));	// 紙白
						gPort->setlinewidth(wordPt * kKESCMCountWordThinFrac);
						gPort->show(startX + numW, ty, wordCh, wordBuf,
							(IGraphicsPort::TextGraphicsFlags)(IGraphicsPort::kStrokeText));
					}
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

