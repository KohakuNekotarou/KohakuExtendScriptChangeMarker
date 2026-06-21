# KohakuExtendScriptChangeMarker (KESCM)

Adobe InDesign C++ SDK Plug-In.

InDesign の Script DOM に `kescmMarkChangesDoc()` / `kescmMarkChanges()` /
`kescmShowPageX()` / `kescmClearMarks()` メソッドを追加する ScriptProvider プラグインです。
2 つのドキュメント（旧版・現行）をページ単位でオフスクリーンにレンダリングしてピクセル比較し、
変化した箇所を画面上に赤い枠（リング）で重ねて表示します。表示は非印刷・非永続なので、
ドキュメントには一切残りません（再実行で再生成）。

## 用途

改訂前後のドキュメントを突き合わせ、「どのページの・どこが変わったか」を一目で把握します。
何百ページでも、低ズームで複数スプレッドが同時に見えても、変化したページすべてにマークが出ます。

## 使い方 (ExtendScript / JavaScript)

```js
// 現行ドキュメントの各ページを、旧版ドキュメントの同じページ番号と比較してマーク
app.documents[0].kescmMarkChangesDoc(app.documents[1]);

// 1 ページだけ比較 (現行ページ ⇔ 旧版ページ)
app.documents[0].pages[0].kescmMarkChanges(app.documents[1].pages[0]);

// 変更ページの対角線 × と変更数表示の ON / OFF (省略時は表示)
app.documents[0].kescmShowPageX(false);

// すべてのマークを消去
app.documents[0].kescmClearMarks();
```

変化した領域は赤い枠（背景が赤いところは青に切り替え）で囲まれ、変更ページにはページ対角線の
× と変更箇所数「N chg」が重ねて表示されます。枠の太さ・文字サイズはズームに追従して一定の
見た目を保ちます。

比較は高解像度で行い、その結果をマックスプーリングで低解像度マスクに圧縮して保持するため、
小さな差（細線・微小なズレ）の取りこぼしを抑えつつ、常駐メモリは低く保ちます。表示は
DrawEventHandler による画面描画で、`.indd` には保存されません。

## ビルド

本リポジトリはプラグインの**ソースのみ**を含みます。ビルドには Adobe InDesign SDK
(21.3.0.60) が別途必要です。SDK の `source/sdksamples/KESCM/` に本ソースを配置して
ビルドしてください。

## 構成

| ファイル | 役割 |
|---|---|
| `KESCMScriptProvider.cpp` | レンダリング → ピクセル比較 → オーバーレイ描画の本体ロジック |
| `KESCM.fr` | メソッド名・引数・対象 DOM クラスの宣言 |
| `KESCMScriptingDefs.h` | ScriptID 定義 |
| `KESCMID.h` / `KESCMID.cpp` | プラグイン ID・各種 ID |
| `KESCMFactoryList.h` | ファクトリ登録 |
| `KESCMNoStrip.cpp` | リンカ最適化除けの参照保持 |
| `TriggerResourceDeps.cpp` | リソース依存トリガ |
| `KESCM.rc` | Windows リソース |
| `KESCM_enUS.fr` / `KESCM_jaJP.fr` | 文字列リソース (英 / 日) |

## 作成について

本プラグインは **KohakuNekotarou** が、Anthropic の AI **Claude（Claude Code / Opus 4.8）**
と協働して設計・実装しました。SDK API の調査、設計方針の検討、C++ コードの実装、
InDesign 実機（COM 経由）での動作検証まで、対話を通じて二人三脚で進めています。

## 注意

Adobe InDesign SDK 本体は含まれていません。SDK の入手・利用は Adobe のライセンスに
従ってください。
