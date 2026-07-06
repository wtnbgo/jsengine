# VRM Starter Kit (vrmkit)

jsengine 上で VRM を使うアプリケーションのベースシステムと、そのひな型サンプル一式。
再利用ライブラリ **vrmkit/** と、それを使った 2 つのモード実装 (**3D 探索** / **ノベルゲームスタイル表示**) で構成される。

## 起動方法

リポジトリルートから:

```bash
jsengine.exe -data samples/vrm_starter
# Makefile 経由なら
make run ARGS="-data samples/vrm_starter"
```

## 操作

| 入力 | 動作 |
|---|---|
| Tab / Pad Y | モード切替 (3D 探索 ⇄ ノベル) |
| F1 | ヘルプ HUD 切替 |
| **3D 探索モード** | |
| WASD / 矢印 / 左スティック | 移動 (カメラ相対) |
| マウス左ドラッグ / ホイール | カメラ回転 / ズーム |
| 1〜7 | VRMA モーション再生 |
| E / Pad A | NPC の近くで会話 (3D の上に会話オーバーレイ) |
| **ノベルモード** | |
| クリック / Enter / Space | 読み進め (タイプ中はスキップ) |
| ↑↓ + Enter / マウス | 選択肢 |

## フォルダ構成

```
samples/vrm_starter/
  main.js                 エントリ。アセットロード → SceneManager 起動、モード切替
  vrmkit/                 ★ 再利用ライブラリ (ESM)。 loadModule("vrmkit/vrmkit.js") で全 API 取得
    core.js               createRenderer / loadVRM / loadVRMA / renderFrame
    actor.js              VRMActor (VRM 1 体のキャラクター抽象)
    camera_rig.js         OrbitFollowRig (三人称) / NovelCamera (2D 風構図)
    stage.js              buildExploreStage / buildNovelStage (舞台プリセット)
    overlay.js            CanvasOverlay (Canvas2D → GL アルファ合成)
    novel_ui.js           NovelUI (メッセージウィンドウ / 名前 / タイプライタ / 選択肢)
    script_runner.js      ScriptRunner (ノベルスクリプト実行)
    vrmkit.js             index (全 re-export)
  scenes/
    explore_scene.js      3D 探索モードのひな型
    novel_scene.js        ノベルモードのひな型 (デモスクリプト入り)
    dialogue_scene.js     任意シーンの上に重ねる汎用会話オーバーレイ
  framework/              data/framework から流用 (scene_manager / input_action)
  lib/                    three.js r176 / GLTFLoader / three-vrm v3.5.1 /
                          three-vrm-animation v3.5.1 (VRMA 用) の ESM ビルド
  models/                 AvatarSample_A / B (VRoid サンプル VRM)
  vrma/                   VRoid Project VRMA モーションパック 7 種
  fonts/                  NotoSansJP (UI 用) + OpenSans
```

## vrmkit API 概要

### core

```js
const VK = loadModule("vrmkit/vrmkit.js");
const { renderer } = VK.createRenderer({ width: 1280, height: 720 });
const vrm  = await VK.loadVRM("models/AvatarSample_A.vrm");   // 向きは +Z に正規化済み
const anim = await VK.loadVRMA("vrma/VRMA_02.vrma");          // VRMAnimation
VK.renderFrame(renderer, scene, camera);   // resetState + render (毎フレーム)
```

`loadVRM` は VRMUtils の最適化、`rotateVRM0` (VRM0/1 の向き統一)、`frustumCulled=false`、
MToon テクスチャ行列補完、VRMA lookAt 用 `VRMLookAtQuaternionProxy` の追加まで済ませて返す。
three.js / three-vrm への import は vrmkit 内で完結しているので、
**lib/ 以下を main.js から直接 loadModule しないこと** (モジュール二重ロード防止)。

### VRMActor — キャラクター抽象

```js
const actor = new VK.VRMActor(vrm, { name: "アオイ" });
scene.add(actor.root);                 // 位置/向きは root (Group) を動かす
actor.setPosition(0, 0, 0);
actor.faceTowards(dx, dz);             // モデル正面は常に +Z
actor.addClip("greeting", anim);       // VRMA をこのモデル用にリターゲット登録
actor.playEmote("greeting");           // 単発再生 (終了で自動的にアイドルへ復帰)
actor.playLoop("dance");               // ループ再生
actor.cancelMotion();                  // 中断 (移動入力時など)
actor.setEmotion("happy", 1, 0.3);     // 感情表情 (排他 + フェード)。null で全解除
actor.setExpression("blink", 1, 0);    // 任意 expression
actor.setMouthOpen(0.5);               // 口パク ("aa")
actor.lookAtTarget = camera;           // 視線追従
actor.locomotionSpeed = 1.7;           // >0 で歩行、0 でアイドル (呼吸+自動まばたき)
actor.settle();                        // スプリングボーンを現在位置で静止状態に張り直す
actor.teleportTo(x, y, z, yaw);        // 位置設定 + settle をまとめて行う
actor.update(dtSec);                   // 毎フレーム必須 (vrm.update 込み)
```

**重要**: シーンに配置した直後やテレポート直後は必ず `settle()` を呼ぶこと。
呼ばないと移動量がスプリングボーンの速度として入り、表示された瞬間に髪や服が
はね上がる。`update()` はフレーム delta を 50ms にクランプするので、
ロード直後の巨大 delta で物理が暴れることはない。

内部では VRMA 再生 (AnimationMixer) と procedural ポーズが排他制御される。
腕を下ろす回転の符号は正規化リグのバインド向き (VRM0 = -Z / VRM1 = +Z) をロード時に実測して自動判定。

### 歩行モーションについて (procedural と VRMA の 2 系統)

**VRM Humanoid はボーン構成 (55 ボーンのマッピング) の標準化であって、
モーションデータ自体は含まれない**。歩行などのモーションは別途用意する。

1. **内蔵 procedural 歩容** (デフォルト): 股関節の振り出し + スイング期の膝屈曲 +
   足首の接地補正 + 骨盤の回旋/側方傾斜/上下動 + 体幹カウンター回旋 + 腕振り + 肘の追従を
   個別にモデル化した歩行サイクル。データ不要でどの VRM でも動く。
2. **VRMA ロコモーション** (推奨、データを用意できる場合):
   `vrma/walk.vrma` と `vrma/idle.vrma` を置くだけで自動で切り替わる (main.js の boot 参照)。
   ```js
   actor.addClip("walk", walkAnim);
   actor.addClip("idle", idleAnim);
   actor.setLocomotionClips({ idle: "idle", walk: "walk", baseSpeed: 1.4 });
   // 以降 locomotionSpeed で idle/walk をクロスフェード + walk の再生速度も同期
   ```
   歩行 VRMA の入手先の例:
   - BOOTH の「3Dモーション」カテゴリ (歩行モーションパック多数。無料〜有料、ライセンス要確認)
   - Mixamo の歩行 FBX を Blender (VRM アドオン) や web ツールで .vrma に変換
   - three-vrm 公式の `loadMixamoAnimation.js` 方式で FBX を実行時リターゲット
     (FBXLoader の組み込みが必要。Mixamo データの再配布は不可なので各自ダウンロード)

### カメラ

```js
// 三人称追従 (3D 探索)
const rig = new VK.OrbitFollowRig({ angle: Math.PI, dist: 3.6 });
rig.handleEvent(e);                    // mousedown/up/move/wheel を流すだけ
rig.apply(camera, actor.position);     // render 時に呼ぶ

// ノベル構図 (望遠 fov20° でパース圧縮 = 2D 立ち絵風)
const cam = new VK.NovelCamera(camera, { fov: 20 });
cam.frameActor(actor, "bustUp", { xOffset: 0.1, yaw: 0 });
// preset: closeUp / bustUp / waistUp / fullBody。humanoid の head ボーン基準
cam.frameTwo(actorA, actorB);          // twoShot
cam.update(dtSec);                     // スムーズ遷移 (毎フレーム)
```

### NovelUI + ScriptRunner

```js
const ui = new VK.NovelUI();           // 1280x720 Canvas2D オーバーレイ
const runner = new VK.ScriptRunner(script, {
    actors: { a: actorA, b: actorB },
    ui: ui,
    camera: novelCam,                  // 省略可
    threeCamera: camera,               // look target 用
    onEnd: () => { ... },
});
runner.start();
// 毎フレーム: runner.update(dtSec); ui.draw();
// 入力:      runner.handleEvent(e);
```

### スクリプトコマンド一覧

```js
[
  { camera: { preset: "twoShot", actors: ["a","b"], snap: true } },
  { camera: { preset: "bustUp", actor: "a", xOffset: 0.1, yaw: 0.2 } },
  { motion: { actor: "a", clip: "greeting" } },            // 即時 (次コマンドへ)
  { motion: { actor: "a", clip: "spin", wait: true } },    // 終了まで待つ (窓は自動で隠れる)
  { expr:   { actor: "a", emotion: "happy", fade: 0.3 } }, // happy/angry/sad/relaxed/surprised
  { expr:   { actor: "a", name: "blink", weight: 1 } },    // 任意 expression
  { look:   { actor: "a", target: "camera" } },            // null で解除
  { say:    { actor: "a", name: "アオイ", text: "こんにちは！\n改行も OK" } },  // actor 指定で口パク
  { wait:   600 },                                         // ms
  { choice: [ { label: "はい", jump: "yes" }, { label: "いいえ" } ] },
  { label:  "yes" },
  { jump:   "end" },
  { do:     (ctx) => { /* 任意コード */ } },
  { end:    true },
]
```

## 新しいアプリを作るときの流れ

1. `samples/vrm_starter` をコピーして新しいフォルダ名にする (自己完結なのでフォルダごと動く)
2. `models/` に自分の VRM、`vrma/` にモーションを置き、`main.js` の `boot()` を書き換える
3. 3D 移動主体なら `scenes/explore_scene.js`、会話主体なら `scenes/novel_scene.js` を元に改造
4. 会話イベントは `DialogueScene` を push するだけでどのシーンにも重ねられる

## デバッグ

`-replfile <dir>` 付きで起動すると外部から JS を評価できる。`globalThis.__vrmstarter` に
`app` (actors/camera/renderer) と `switchMode()` を公開しているので、
`captureScreen(path)` と組み合わせて AI エージェントによる自動検証が可能。

## アセット (リポジトリ未同梱 — 各自配置が必要)

モデルとモーションは**ライセンス上の理由 (二次配布不可) でリポジトリに含まれない**。
clone した場合は以下を配置してから起動すること:

- `models/AvatarSample_A.vrm`, `models/AvatarSample_B.vrm` —
  [VRoid プロジェクトのサンプルモデル](https://vroid.com/studio/sample-models) 等から入手
  (別の VRM を使う場合は main.js の `boot()` のパスを書き換える)
- `vrma/VRMA_01.vrma` 〜 `VRMA_07.vrma` —
  [VRoid Project VRMA モーションパック (BOOTH、無料)](https://booth.pm/ja/items/5512385)。
  商用利用時はクレジット表記が必要: 「キャラクターアニメーション: ピクシブ株式会社 VRoidプロジェクト」

リポジトリに同梱しているもの:

- `fonts/` — SIL OFL (Licenses/ 参照)
- `lib/three*.js` / `lib/GLTFLoader.js` — MIT (three.js / @pixiv/three-vrm / @pixiv/three-vrm-animation)
