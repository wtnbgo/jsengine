// ============================================================
// scenes/dialogue_scene.js — 会話オーバーレイシーン
// ============================================================
//
// 既存シーン (3D 探索など) の上に push して使う汎用の会話シーン。
// 下のシーンは pauseBelow で update が止まるが render は続くので、
// 「3D 画面の上にメッセージウィンドウ」になる。
//
//   SceneManager.push(new DialogueScene({
//       script: [...],                // ScriptRunner のコマンド配列
//       actors: { a: actorA, ... },   // スクリプトから参照する VRMActor
//       ui: novelUI,                  // 共有 NovelUI
//       camera: novelCamera | null,   // カメラ演出を使うなら NovelCamera
//       threeCamera: camera,          // look target 用
//       updateActors: [actorA, ...],  // 会話中も update し続けるアクター
//       onEnd: () => {},              // 終了時 (pop は自動で行う)
//   }), null, { pauseBelow: true });

const Scene = globalThis.Scene;
const SceneManager = globalThis.SceneManager;

import { ScriptRunner } from "../vrmkit/script_runner.js";

export class DialogueScene extends Scene {
    constructor(opts) {
        super();
        this.opts = opts;
        this.runner = null;
    }

    enter() {
        const opts = this.opts;
        const self = this;
        this.runner = new ScriptRunner(opts.script, {
            actors: opts.actors,
            ui: opts.ui,
            camera: opts.camera || null,
            threeCamera: opts.threeCamera || null,
            onEnd: function() {
                SceneManager.pop();
                if (opts.onEnd) opts.onEnd();
            },
        });
        this.runner.start();
    }

    exit() {
        this.opts.ui.hide();
    }

    update(dt) {
        const dts = dt / 1000;
        const list = this.opts.updateActors || [];
        for (let i = 0; i < list.length; i++) list[i].update(dts);
        this.runner.update(dts);
        if (this.opts.camera) this.opts.camera.update(dts);
    }

    render() {
        this.opts.ui.draw();
    }

    handleEvent(e) {
        this.runner.handleEvent(e);
    }
}
