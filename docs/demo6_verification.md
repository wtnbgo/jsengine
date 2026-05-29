# Demo 6 (Canvas2D drawImage / getImageData / putImageData) 確認手順

`-demo 6` で起動、または起動後 `6` キーで Demo 6 に切替。
画面は **500 × 700**。`data/test_pattern.png` をテスト画像として使用。

## ブラウザ側の「正解」リファレンス

`docs/demo6_reference.html` をブラウザで開くと、同じ描画コードをブラウザの Canvas 2D API で実行した結果が見られます。

```
cd D:/test/jsengine
python -m http.server 8000
```
で `http://localhost:8000/docs/demo6_reference.html` を開いてください。

## 描画内容（上から下、3 セクション）

### セクション 1 — drawImage の引数バリアント (Y=0〜220)

タイトル: `Demo 6: drawImage / ImageData` (18px OpenSans-Bold, 白)

| ラベル | 位置 | 内容 | ポイント |
|--------|------|------|---------|
| original | (15,35) | `drawImage(img, dx, dy)` 3引数 | 等倍描画 |
| resize 120x70 | (105,35) | `drawImage(img, dx, dy, 120, 70)` 5引数 | リサイズ |
| clip+scale | (245,35) | `drawImage(img, 16,16, 32,32, 245,35, 80,80)` 9引数 | 切り出し+拡大 |
| tiled (Y=155) | x=10,80,150,220,290 | 同じ画像を 5 連 | 同一参照の繰返し |

### セクション 2 — Image / HTMLCanvasElement (Y=230〜430)

**ブラウザでは native なので「動くのが当たり前」のセクション**。jsengine 側は RPG Maker MV 互換のシム経由で同じ流れを再現できるかを検証している。

- **A: direct** — `Image → drawImage` (画像を直接描画)
- **B: canvas copy** — 別 canvas にコピーして `getImageData` で不透明ピクセル数をカウント
- **C: HTMLCanvas** — HTMLCanvasElement 経由で同じことをもう一度
- **D: canvas.data** — jsengine 側固有の getter（ブラウザは N/A）

A は画像、B/C は数値（`N/total opaque`）、D は jsengine では `yes(NB)` / ブラウザでは `N/A`。

### セクション 3 — getImageData / putImageData (Y=430〜600)

- 左 (10,460): 元画像 — 橙矩形 80×50 + 青矩形 80×50（重なる）
- 中 (150,460): **copy** — `getImageData → putImageData` でそのまま貼り付け
- 右 (280,460): **inverted** — RGB を 255 から引いた色反転バージョン

下にラベル `original / copy / inverted`。

## チェックポイント

- [ ] **drawImage 3 引数**: 等倍で画像が出る
- [ ] **drawImage 5 引数**: 120×70 に伸縮される
- [ ] **drawImage 9 引数**: 画像の左上 32×32 領域だけが切り出され、80×80 に拡大される
- [ ] **tiled**: 5 つの同じ画像が等間隔
- [ ] **Image シム** (jsengine): `_data=yes(NB)` で何バイトか表示、`w/h` が読み取れる
- [ ] **HTMLCanvasElement シム** (jsengine): `C: HTMLCanvas N/total opaque` で 0 以上、`D: canvas.data=yes(NB)` 表示
- [ ] **getImageData/putImageData copy**: 元と完全に同じ見た目
- [ ] **inverted**: 橙 → 青、青 → 橙、背景 → 補色（半透明α込み）

## よくある不具合パターン

- **9 引数 drawImage が壊れる**: ソース矩形のクリップ処理ミス → 全体が縮小されて出る
- **tiled で 1 つしか出ない**: 同じ Image 参照を複数回 drawImage で使った時のキャッシュ問題
- **getImageData が全 0**: テクスチャ → CPU 読み戻しがない（ThorVG はバッファ retained なので戻せるはず）
- **putImageData inverted が反映されない**: `data` 配列の変更が putImageData 側に渡っていない
- **Image._data が NO**: PNG デコードの失敗 / 同期ロード経路の問題

## jsengine 固有の注意

- Demo 6 は `Image` / `HTMLCanvasElement` のシムを Demo 5 のロード時に同時に登録するので、Demo 5 を一度通してから Demo 6 を見るか、起動時オプションで両方シムをロードする
- `test_pattern.png` は `data/` 直下。base path が変わると見つからない
- ブラウザ側は `../data/test_pattern.png` を相対パスで取りに行く（reference HTML は `docs/` から見て一段上の `data/`）
