# ChangeMarker (KESCM)

Adobe InDesign C++ プラグイン。**2つのドキュメント（旧版／現行）をページ単位でピクセル比較し、変化した箇所を画面上にオーバーレイ表示**します。表示は非印刷・非永続（DrawEventHandler 描画）なので、ドキュメントには一切残りません。再実行で再生成されます。

> KESCM = **K**ohaku **E**xtend**S**cript **C**hange **M**arker

## 主な機能

- **ページ単位のピクセル差分**：変化した領域を赤いリング（枠）で囲む。リング太さはズームに追従して一定の見た目。
- **取りこぼし防止**：比較は高解像度で行い、結果をマックスプーリングで低解像度マスクに圧縮して記憶（常駐メモリは低解像度のまま、検出は高解像度の精度）。
- **背景適応色**：枠の下が赤っぽい画素なら枠を青に切り替えて埋もれを防止。
- **変更ページの目印**：ページ対角線の × と、変更数「N chg」（白縁付き）をページ内に表示。
- **複数ページ／低ズーム対応**：何百ページでも、5% 表示で複数スプレッドが見えても、変化したページすべてに表示。

## スクリプト API（ExtendScript）

```javascript
// 現行ドキュメントの各ページを、旧版ドキュメントの同じページ番号と比較してマーク
app.documents[0].kescmMarkChangesDoc(app.documents[1]);

// 1ページだけ比較（現行ページ ⇔ 旧版ページ）
app.documents[0].pages[0].kescmMarkChanges(app.documents[1].pages[0]);

// 対角線×と変更数の表示／非表示
app.documents[0].kescmShowPageX(false);

// すべてのマークを消去
app.documents[0].kescmClearMarks();
```

## ビルド

Adobe InDesign Plug-in SDK のサンプル群（`source/sdksamples/`）配下に置き、SDK のビルド手順に従ってビルドします（Release / x64）。

## チューニング

主要パラメーターは `KESCMScriptProvider.cpp` 冒頭の定数で調整できます（リング太さ・色・しきい値・比較解像度 `kKESCMHiResMul`・プーリング感度 `kKESCMPoolMinCount` など）。
