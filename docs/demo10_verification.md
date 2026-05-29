# Demo 10 (pixi.ui ウィジェットショーケース) 確認手順

`-demo 10` で起動、または起動後 `10` キー（または `0`）で Demo 10 に切替。
画面は **1280 × 720**。pixi.js v7.4.3 + pixi.ui v1.2.4。

## ブラウザ側の「正解」リファレンス

`docs/demo10_reference.html` を**ローカル HTTP サーバー経由で**開いてください（pixi 系は `file://` で動かないことが多い）。

```
cd D:/test/jsengine
python -m http.server 8000
```
で `http://localhost:8000/docs/demo10_reference.html` を開く。

ブラウザ側は jsengine と同じ `data/lib/pixi.min.js` / `pixi-ui-deps-shim.js` / `pixi-ui.js` を読み込むので、**ライブラリは完全同じバージョン**。違いはイベント系統だけ。

## ウィジェット配置

| 位置 | ウィジェット | 操作 | 期待挙動 |
|------|-------------|------|---------|
| (40,100) 160×50 | Button "Cycle Color" | クリック | 背景色が 5 色 (#1a2030 → #402030 → ...) でサイクル変更 |
| (220,110) 28×28 | CheckBox "Animate" | クリック | ON/OFF 切替、ON で ProgressBar が進む |
| (40,200) 300×12 | Slider (0〜100) | ハンドルドラッグ | 値が変化、下段のログに反映 |
| (40,285) 300×18 | ProgressBar | (時間連動) | CheckBox ON 時のみ 0.4/frame で進む、100 で 0 に戻る |
| (700,100) 240×280 | ScrollBox (12 アイテム) | マウスホイール | アイテムが縦スクロール |
| (40,640) | ログテキスト | (表示専用) | 直近のウィジェット操作を表示 |

## チェックポイント (ブラウザ側)

- [ ] Button をクリック → ログが `Button pressed: color #N` に変化 + 背景色変更
- [ ] CheckBox をクリック → ログが `Checkbox: ON` / `OFF` に変化
- [ ] CheckBox ON → ProgressBar の緑バーが左から右に伸びる
- [ ] Slider のハンドルをドラッグ → 青いフィルが追従、ログに `Slider: N`
- [ ] ScrollBox 内でホイール → アイテムが縦スクロール

## チェックポイント (jsengine 側)

ブラウザ側で全部期待通り動く前提で、**jsengine 側で同じ操作をしたときに同じ反応があるか**を確認する。

- [ ] Button **クリック反応**: ブラウザ動作との差分を観察
- [ ] CheckBox クリック反応
- [ ] Slider ドラッグ反応 (押し始め / ドラッグ中 / 離す)
- [ ] ScrollBox ホイール反応

## 既知の問題 / 追究ポイント (jsengine 側)

1. **クリック反応問題 (最大課題)**
   - pixi.js v7 はマウスイベントを `pointerdown` / `pointermove` / `pointerup` で受ける
   - jsengine 側は `browser_shim.js` で SDL3 のマウスイベント → DOM Event 風オブジェクト変換
   - **要確認**: pointer 系イベントが pixi の interaction system に正しく届いているか
   - stage.interactive = true / stage.hitArea = pixiUiApp.screen の設定は init で実施済み

2. **Debug 版の白帯** (Canvas2D 修正後に再確認すべき項目)
   - 以前は Canvas2D テキスト描画パス由来の白帯があった疑い
   - commit `fe5ddf4` (Canvas2D テキスト強化) で解消されている可能性が高い
   - Debug ビルドで Demo 10 を起動して、Button や Slider 周辺に白い縞・帯が出ないか確認

3. **Tweedle 即時遷移**
   - `pixi-ui-deps-shim.js` の Tween は値を即座にゴールへセットする実装
   - Button のフェードや Slider ハンドルの慣性などのアニメーションは付かない
   - ウィジェットの基本機能 (押下イベント・値変更) には影響なし

## 切り分け手順

ブラウザ側で動作確認したあと、jsengine 側で「どこまで動いて、どこから動かないか」を以下の順で見る:

1. **コンソールログ確認**: `console.log("pixi.ui demo initialized")` が出ているか
2. **ウィジェット表示**: 配置・色・テキストはブラウザと同じか
3. **マウス座標**: jsengine 側の addEventListener で `mousedown` の `clientX/Y` が正しく取れているか
4. **stage.interactive の到達**: pixi 内部の `pointertarget` まで届くか
5. **Signal.emit**: `button.onPress.emit()` が呼ばれるか
