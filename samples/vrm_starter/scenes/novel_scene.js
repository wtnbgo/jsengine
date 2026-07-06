// ============================================================
// scenes/novel_scene.js — ノベルゲームスタイル表示モード
// ============================================================
//
// 望遠カメラ (NovelCamera) で VRM を 2D 立ち絵風に構図し、
// メッセージウィンドウ + 選択肢 + 表情 + VRMA ジェスチャで
// 会話シーンを進める。 スクリプトは ScriptRunner のコマンド配列。
//
//   - クリック / Enter / Space: 読み進め (タイプ中はスキップ)
//   - 選択肢: マウス or ↑↓ + Enter
//   - Tab: 3D 探索モードへ (main.js 側で処理)

const Scene = globalThis.Scene;

import * as VK from "../vrmkit/vrmkit.js";

const THREE = VK.THREE;

let _world = null;
function getWorld() {
    if (!_world) {
        const scene = new THREE.Scene();
        VK.buildNovelStage(scene);
        _world = { scene: scene };
    }
    return _world;
}

// デモスクリプト: 構図 / 表情 / VRMA / 選択肢分岐のショーケース
function makeDemoScript() {
    return [
        { camera: { preset: "twoShot", actors: ["a", "b"], snap: true } },
        { wait: 500 },
        { motion: { actor: "a", clip: "greeting" } },
        { expr: { actor: "a", emotion: "happy" } },
        { say: { actor: "a", name: "アオイ", text: "こんにちは！ vrmkit のノベルモードへようこそ。\nVRM の表情と VRMA モーション、カメラ構図を組み合わせて会話シーンを作れます。" } },
        { camera: { preset: "bustUp", actor: "b", xOffset: -0.08 } },
        { expr: { actor: "b", emotion: "relaxed" } },
        { say: { actor: "b", name: "ミドリ", text: "カメラは望遠レンズ (fov 20°) でパースを圧縮しているの。\nだから 3D モデルなのに 2D の立ち絵みたいな画角になるんだよ。" } },
        { camera: { preset: "closeUp", actor: "a", xOffset: 0.1 } },
        { expr: { actor: "a", emotion: "surprised", fade: 0.12 } },
        { say: { actor: "a", name: "アオイ", text: "わっ、急にクローズアップになった！？" } },
        { camera: { preset: "twoShot", actors: ["a", "b"] } },
        { expr: { actor: "a", emotion: "happy" } },
        { expr: { actor: "b", emotion: "happy" } },
        { say: { actor: "b", name: "ミドリ", text: "ふふ。それじゃあ、どのモーションが見たい？" } },
        { choice: [
            { label: "Vサイン", jump: "peace" },
            { label: "くるっと回る", jump: "spin" },
            { label: "モデルポーズ", jump: "pose" },
        ] },

        { label: "peace" },
        { camera: { preset: "waistUp", actor: "a" } },
        { motion: { actor: "a", clip: "peace", wait: true } },
        { expr: { actor: "a", emotion: "happy" } },
        { say: { actor: "a", name: "アオイ", text: "ぶいっ！" } },
        { jump: "after" },

        { label: "spin" },
        { camera: { preset: "fullBody", actor: "a" } },
        { motion: { actor: "a", clip: "spin", wait: true } },
        { expr: { actor: "a", emotion: "happy" } },
        { say: { actor: "a", name: "アオイ", text: "くるくる〜っ。スプリングボーンで髪と服も揺れてるよ。" } },
        { jump: "after" },

        { label: "pose" },
        { camera: { preset: "fullBody", actor: "a" } },
        { motion: { actor: "a", clip: "pose", wait: true } },
        { expr: { actor: "a", emotion: "relaxed" } },
        { say: { actor: "a", name: "アオイ", text: "どう？ モデルさんっぽいでしょ。" } },
        { jump: "after" },

        { label: "after" },
        { camera: { preset: "twoShot", actors: ["a", "b"] } },
        { motion: { actor: "b", clip: "greeting" } },
        { say: { actor: "b", name: "ミドリ", text: "スクリプトは vrmkit/script_runner.js のコマンド配列を書くだけ。\n詳しくは samples/vrm_starter/README.md を見てね。" } },
        { expr: { actor: "a", emotion: "happy" } },
        { say: { actor: "a", name: "アオイ", text: "Tab キーで 3D 探索モードにも切り替えられます。\nクリックすると最初からもう一度見られるよ。それじゃ、また！" } },
        { expr: { actor: "a", emotion: null, fade: 0.6 } },
        { expr: { actor: "b", emotion: null, fade: 0.6 } },
        { end: true },
    ];
}

export class NovelScene extends Scene {
    // app: main.js の共有コンテキスト { renderer, camera, actors: {a,b}, novelUI }
    constructor(app) {
        super();
        this.app = app;
    }

    enter() {
        const world = getWorld();
        this.scene = world.scene;
        this.a = this.app.actors.a;
        this.b = this.app.actors.b;

        this.a.reset();
        this.b.reset();
        this.scene.add(this.a.root);
        this.scene.add(this.b.root);
        // 下手 (画面左) にアオイ、 上手 (画面右) にミドリ。 わずかに内向き。
        this.a.setPosition(-0.55, 0, 0);
        this.a.setHeading(0.15);
        this.b.setPosition(0.55, 0, 0);
        this.b.setHeading(-0.15);
        this.a.lookAtTarget = this.app.camera;
        this.b.lookAtTarget = this.app.camera;
        // 配置確定後にスプリングボーンを静止状態へ (表示瞬間の髪はね防止)
        this.a.settle();
        this.b.settle();

        this.novelCam = new VK.NovelCamera(this.app.camera, { fov: 20 });

        const self = this;
        this._ended = false;
        this.runner = new VK.ScriptRunner(makeDemoScript(), {
            actors: { a: this.a, b: this.b },
            ui: this.app.novelUI,
            camera: this.novelCam,
            threeCamera: this.app.camera,
            onEnd: function() { self._ended = true; },
        });
        this.runner.start();
    }

    exit() {
        this.app.novelUI.hide();
        this.scene.remove(this.a.root);
        this.scene.remove(this.b.root);
    }

    update(dt) {
        const dts = dt / 1000;
        this.a.update(dts);
        this.b.update(dts);
        this.runner.update(dts);
        this.novelCam.update(dts);
    }

    render() {
        VK.renderFrame(this.app.renderer, this.scene, this.app.camera);
        this.app.novelUI.draw();
    }

    handleEvent(e) {
        if (this._ended) {
            // 終了後はクリック / Enter でリスタート
            if ((e.type === "mousedown" && e.button === 0) ||
                (e.type === "keydown" && (e.code === "Enter" || e.code === "Space"))) {
                this._ended = false;
                this.runner.start();
            }
            return;
        }
        this.runner.handleEvent(e);
    }
}
