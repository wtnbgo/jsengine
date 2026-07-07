# anime25d — Anime2.5DRig 移植サンプル

[852wa/Anime2.5DRig](https://github.com/852wa/Anime2.5DRig) (MIT) の jsengine 移植。
パーツ分け PSD を自動リギングして、アイドルモーション・まばたき・口パク・髪物理付きで
動く 2.5D アバターを表示する。表示ロジックは上流の index.html ランタイムを忠実に移植
(メッシュワープ + ステンシルによる瞳クリップ + 2重バネ物理)。

**機能カット**: マイク口パク / ウェブカメラ追従 (MediaPipe) は外部デバイス・CDN 依存のため
移植対象外。マウス追従は残してある。

## 起動方法

```bash
jsengine.exe -data samples/anime25d
```

起動後、左上の **「画像を読み込む」** ボタン (または O キー) からパーツ分け PSD を
選択する (`fs.showOpenFileDialog` によるネイティブダイアログ。任意の場所の PSD を開ける)。

- 初回はリグ生成 (画像処理) に **1 分前後** かかる (QuickJS のインタプリタ実行のため。
  ブラウザ V8 では数秒の処理)。生成結果は選んだ PSD の隣に `<名前>.psd.rig` として
  キャッシュされ、**同じファイルの 2 回目以降は即表示**。PSD の内容が変わると
  キャッシュは自動で無効化・再生成される。

## 操作

| 入力 | 動作 |
|---|---|
| 画像を読み込む / O | PSD ファイル選択 (別モデルへの切替もこれで) |
| 右パネル | スライダー / トグル / 表情プリセット / レイヤー順 (マウス操作、ホイールでスクロール) |
| マウス追従 ON 時 | アバター表示域でマウスを動かすと顔と視線が追従 |
| B | 背景切替 (ダーク / グリーンバック / グレー) |
| R | 数値リセット |

## 既知の制限と改善計画

- **リグ生成中はウィンドウが「応答なし」になる** (約 1 分)。生成は JS メインスレッドの
  同期処理で、その間 SDL のイベントポンプが止まるため。処理自体は正常に進んでいる。
  改善計画 (未実装):
  1. rigger.js の buildRig をレイヤー単位のチャンク実行 (ジェネレータ化) に分割し、
     フレームごとに 1 レイヤーずつ処理 → 応答なし回避 + プログレスバー表示
  2. エンジン側に Worker (別スレッド QuickJS) を実装してビルドを完全バックグラウンド化
  3. Node.js で `.rig` を事前生成する tools スクリプト (rigger.js は Node でも動く)
- 口の開きは差分切替 + 変形の簡易表現 (上流と同じ制限)

## フォルダ構成 (ライブラリ分割)

```
samples/anime25d/
  main.js               エントリ。ロード画面 → リグ生成/キャッシュ → 表示 + UI
  lib/                  上流ライブラリ (無改変・MIT)
    ag-psd.min.js         PSD パーサ
    rigger.js             自動リグ生成 (純 TypedArray 実装)
    genericparts.js       汎用閉じ目・閉じ口差分 (内蔵データ)
  rig25d/               ★ 組み込みライブラリ (3 分割)
    loader.js             データ作製部: PSD → rig 定義 + バイナリキャッシュ (Rig25D)
    avatar.js             表示機能部: rig を GL 描画 + 自動アニメ (Avatar25D)
    ui_panel.js           テスト操作 UI 部: Canvas2D パネル (UIPanel25D)
  psd/                  モデル PSD (リポジトリ未同梱、下記参照) + .rig キャッシュ
  fonts/                NotoSansJP (UI 用、OFL)
```

## 組み込みライブラリとしての使い方

UI なしでアバターだけ自作アプリに組み込む場合は `lib/` 3 本 + `rig25d/loader.js` +
`rig25d/avatar.js` を loadScript して:

```js
// 1. データ作製部 — PSD からリグ生成 (キャッシュ付き)
var rig = Rig25D.loadRig("psd/mymodel.psd");
// キャッシュ不要なら: Rig25D.buildFromPsd(fs.readBinary("psd/mymodel.psd"))

// 2. 表示機能部
var avatar = new Avatar25D();
avatar.setRig(rig);

// パラメータは T (target) に書くと平滑化されて反映される
avatar.T.mouthOpen = 0.6;              // キー一覧は Avatar25D.PARAM_DEFAULTS
avatar.applyPreset("smile");           // neutral/smile/usume/surprise/jito/winkL/winkR
avatar.auto.blink = true;              // idle/blink/rand/talk/mouse/phys
avatar.mouse.x = 0.3; avatar.mouse.in = true;   // auto.mouse=true でマウス追従

// 毎フレーム
avatar.update(dtMs);
avatar.render(x, y, scale, screenW, screenH);   // 現在の framebuffer に合成描画
```

- `render()` はステンシルバッファを使う (瞳を白目内にクリップ)。呼出し前に
  そのフレームでステンシルを使い終えていること。color バッファはクリアしない。
- テクスチャは premultiplied alpha でアップロードし
  `blendFunc(ONE, ONE_MINUS_SRC_ALPHA)` で合成する。
- レイヤー順の入替は `avatar.moveLayer(i, ±1)`。

## リグキャッシュ (.rig) について

`Rig25D.loadRig` は PSD と同じ場所に `<psd名>.rig` を書く
(ヘッダ + rig 定義 JSON + 各パーツの RGBA blob)。PSD のサイズ + 先頭 64KB の
ハッシュで照合し、PSD が変われば自動再生成。UI の「リグ再生成」ボタンか
`Rig25D.clearCache(path)` で手動破棄もできる。

## モデル PSD (リポジトリ未同梱)

サンプル PSD の絵の権利は各作者に帰属するためリポジトリに含まれない。
「画像を読み込む」から任意の場所の PSD を開けるので配置場所は自由:

- Anime2.5DRig 同梱の `sample.psd` (ローカル checkout: `../Anime2.5DRig/sample.psd`)
- 自作のパーツ分け PSD — レイヤー命名規約 (face / eyewhite / irides / eyelash /
  eye_close / eyebrow / mouth_open / mouth_close / front hair / back hair 等) は
  [上流 README](https://github.com/852wa/Anime2.5DRig#readme) 参照
- 一枚絵は [see-through](https://github.com/shitagaki-lab/see-through) 等で
  レイヤー分解した PSD をそのまま使える (`mouth`→`mouth_open` の自動リネーム、
  足りない閉じ目・閉じ口差分の自動生成は rigger 側で行われる)

## デバッグ

`-replfile <dir>` 付きで起動すると外部から JS を評価できる。
`globalThis.__anime25d` に `isReady()/state()/avatar()/panel()/rigInfo()/loadPsd(path)` を
公開しているので、`captureScreen(path)` と組み合わせて AI エージェントによる自動検証が可能
(`loadPsd` はダイアログを介さず直接ロードする自動化用フック)。

## ライセンス

- `lib/` (ag-psd / rigger / genericparts): MIT (Anime2.5DRig / ag-psd)
- `fonts/`: SIL OFL (fonts/Licenses/ 参照)
- モデル PSD: 各作者に帰属 (リポジトリ未同梱)
