// ============================================================
// vrmkit/script_runner.js — ノベルスクリプト実行エンジン
// ============================================================
//
// コマンドの配列を上から順に実行する小さなインタプリタ。
// say / choice / wait で停止し、 それ以外 (camera / motion / expr ...) は
// 即時実行して次へ進む。
//
// スクリプト形式 (1 コマンド = 1 オブジェクト、 キーで種別判定):
//   { camera: { preset, actor, actors:[a,b], xOffset, yaw, snap } }
//       preset: "closeUp"|"bustUp"|"waistUp"|"fullBody"|"twoShot"
//       twoShot は actors 2 人指定 (省略時 preset 以外のキャラ全員)
//   { motion: { actor, clip, wait:false, loop:false } }
//       wait:true でモーション終了までスクリプトを止める
//   { expr:   { actor, emotion:"happy", weight:1, fade:0.3 } }
//       emotion は排他 (他の感情表情を 0 へ)。 name 指定で任意 expression
//   { look:   { actor, target:"camera"|null } }
//   { say:    { name:"表示名", actor:"a", text:"..." } }
//       actor 指定でタイプライタ中に口パク
//   { wait:   600 }                    — ミリ秒
//   { choice: [ { label, jump } ] }    — 選択肢。 jump 省略で次へ進む
//   { label:  "name" } / { jump: "name" }
//   { do:     function(ctx) {} }       — 任意コード実行 (エスケープハッチ)
//   { end:    true }                   — onEnd() を呼んで停止
//
// 使い方:
//   const runner = new ScriptRunner(script, {
//       actors: { a: actorA, b: actorB },   // VRMActor
//       ui: novelUI,                        // NovelUI
//       camera: novelCamera,                // NovelCamera (省略可)
//       threeCamera: camera,                // look target 用 (省略可)
//       onEnd: () => { ... },
//   });
//   runner.start();
//   毎フレーム: runner.update(dtSec);   入力: runner.handleEvent(e);

export class ScriptRunner {
    constructor(script, ctx) {
        this.script = script;
        this.ctx = ctx;
        this._index = 0;
        this._state = "idle";  // idle | say | choice | wait | motion | ended
        this._waitMs = 0;
        this._waitActor = null;
        this._choices = null;
        this._speaker = null;   // 口パク対象の VRMActor
        this._labels = {};
        for (let i = 0; i < script.length; i++) {
            if (script[i].label) this._labels[script[i].label] = i;
        }
    }

    get running() { return this._state !== "ended" && this._state !== "idle"; }
    get ended() { return this._state === "ended"; }

    start() {
        this._index = 0;
        this._state = "run";
        this._run();
    }

    _actor(id) {
        const a = this.ctx.actors ? this.ctx.actors[id] : null;
        if (!a) console.warn("ScriptRunner: unknown actor '" + id + "'");
        return a;
    }

    _jumpTo(label) {
        if (this._labels[label] === undefined) {
            console.error("ScriptRunner: unknown label '" + label + "'");
            this._state = "ended";
            return false;
        }
        this._index = this._labels[label];
        return true;
    }

    // 停止コマンドに当たるまで実行を進める
    _run() {
        const ui = this.ctx.ui;
        let guard = 0;
        while (this._index < this.script.length) {
            if (++guard > 10000) { console.error("ScriptRunner: infinite loop?"); break; }
            const cmd = this.script[this._index++];

            if (cmd.label !== undefined) continue;

            if (cmd.jump !== undefined) {
                if (!this._jumpTo(cmd.jump)) return;
                continue;
            }

            if (cmd.do !== undefined) {
                try { cmd.do(this.ctx); } catch (e) { console.error("ScriptRunner do: " + e); }
                continue;
            }

            if (cmd.camera !== undefined && this.ctx.camera) {
                const cc = cmd.camera;
                if (cc.preset === "twoShot") {
                    let ids = cc.actors;
                    if (!ids) ids = Object.keys(this.ctx.actors).slice(0, 2);
                    const a = this._actor(ids[0]), b = this._actor(ids[1]);
                    if (a && b) this.ctx.camera.frameTwo(a, b, cc);
                } else {
                    const a = this._actor(cc.actor);
                    if (a) this.ctx.camera.frameActor(a, cc.preset || "bustUp", cc);
                }
                continue;
            }

            if (cmd.expr !== undefined) {
                const a = this._actor(cmd.expr.actor);
                if (a) {
                    const weight = (cmd.expr.weight !== undefined) ? cmd.expr.weight : 1.0;
                    const fade = (cmd.expr.fade !== undefined) ? cmd.expr.fade : 0.25;
                    if (cmd.expr.emotion !== undefined) a.setEmotion(cmd.expr.emotion, weight, fade);
                    else if (cmd.expr.name) a.setExpression(cmd.expr.name, weight, fade);
                }
                continue;
            }

            if (cmd.look !== undefined) {
                const a = this._actor(cmd.look.actor);
                if (a) {
                    a.lookAtTarget = (cmd.look.target === "camera") ? (this.ctx.threeCamera || null) : null;
                }
                continue;
            }

            if (cmd.motion !== undefined) {
                const a = this._actor(cmd.motion.actor);
                if (a) {
                    if (cmd.motion.loop) a.playLoop(cmd.motion.clip);
                    else a.playEmote(cmd.motion.clip);
                    if (cmd.motion.wait) {
                        // モーション見せ場では前のセリフを引っ込める
                        this.ctx.ui.hideWindow();
                        this._waitActor = a;
                        this._state = "motion";
                        return;
                    }
                }
                continue;
            }

            if (cmd.say !== undefined) {
                ui.say(cmd.say.name, cmd.say.text);
                this._speaker = cmd.say.actor ? this._actor(cmd.say.actor) : null;
                this._state = "say";
                return;
            }

            if (cmd.wait !== undefined) {
                this._waitMs = cmd.wait;
                this._state = "wait";
                return;
            }

            if (cmd.choice !== undefined) {
                this._choices = cmd.choice;
                ui.showChoices(cmd.choice);
                this._state = "choice";
                return;
            }

            if (cmd.end !== undefined) {
                this._finish();
                return;
            }

            console.warn("ScriptRunner: unknown command " + JSON.stringify(cmd));
        }
        // スクリプト末尾に到達
        this._finish();
    }

    _finish() {
        this._state = "ended";
        this._stopMouth();
        this.ctx.ui.hide();
        if (this.ctx.onEnd) this.ctx.onEnd();
    }

    _stopMouth() {
        if (this._speaker) this._speaker.setMouthOpen(0);
        this._speaker = null;
        this._mouthT = 0;
    }

    // 入力イベント。 シーンの handleEvent から呼ぶ
    handleEvent(e) {
        if (this._state === "ended") return;
        const act = this.ctx.ui.handleEvent(e);
        if (!act) return;
        if (act.type === "advance" && this._state === "say") {
            if (this.ctx.ui.isTyping) {
                this.ctx.ui.completeText();
            } else {
                this._stopMouth();
                this._state = "run";
                this._run();
            }
        } else if (act.type === "choice" && this._state === "choice") {
            const chosen = this._choices[act.index];
            this.ctx.ui.hideChoices();
            this._choices = null;
            this._state = "run";
            if (chosen.jump !== undefined) {
                if (this._jumpTo(chosen.jump)) this._run();
            } else {
                this._run();
            }
        }
    }

    update(dtSec) {
        this.ctx.ui.update(dtSec);

        if (this._state === "wait") {
            this._waitMs -= dtSec * 1000;
            if (this._waitMs <= 0) {
                this._state = "run";
                this._run();
            }
        } else if (this._state === "motion") {
            if (!this._waitActor || !this._waitActor.isMotionPlaying) {
                this._waitActor = null;
                this._state = "run";
                this._run();
            }
        } else if (this._state === "say" && this._speaker) {
            // タイプライタ中の口パク
            if (this.ctx.ui.isTyping) {
                this._mouthT = (this._mouthT || 0) + dtSec;
                this._speaker.setMouthOpen(0.18 + 0.4 * Math.abs(Math.sin(this._mouthT * 9)));
            } else {
                this._speaker.setMouthOpen(0);
            }
        }
    }
}
