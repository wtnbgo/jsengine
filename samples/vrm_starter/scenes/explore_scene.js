// ============================================================
// scenes/explore_scene.js — 3D 探索モード
// ============================================================
//
//   - WASD / 矢印 / 左スティックで移動 (カメラ相対)
//   - マウス左ドラッグでカメラ回転、 ホイールでズーム (OrbitFollowRig)
//   - 1〜7 キーで VRMA モーション再生
//   - NPC に近づいて E で会話 (DialogueScene を push)
//   - Tab でノベルモードへ (main.js 側で処理)

const Scene = globalThis.Scene;
const SceneManager = globalThis.SceneManager;
const Input = globalThis.Input;

import * as VK from "../vrmkit/vrmkit.js";
import { DialogueScene } from "./dialogue_scene.js";

const THREE = VK.THREE;

const WALK_SPEED = 1.7;   // m/s
const NPC_SPEED = 0.9;
const TALK_DIST = 2.0;

// 1〜7 キー → VRMA クリップ名 (main.js の addClip と対応)
const EMOTE_KEYS = {
    Digit1: "showFull", Digit2: "greeting", Digit3: "peace", Digit4: "shoot",
    Digit5: "spin", Digit6: "pose", Digit7: "squat",
};

// ステージはモード切替をまたいで使い回す (ジオメトリの作り直し防止)
let _world = null;
function getWorld() {
    if (!_world) {
        const scene = new THREE.Scene();
        VK.buildExploreStage(scene);
        _world = { scene: scene };
    }
    return _world;
}

// 「E: 話しかける」ヒント (静的内容なので一度だけ描く)
let _talkHint = null;
function getTalkHint() {
    if (!_talkHint) {
        _talkHint = new VK.CanvasOverlay(340, 52, 1280, 720);
        const c = _talkHint.canvas;
        c.fillStyle = "rgba(16,20,44,0.8)";
        c.fillRect(0, 0, 340, 52);
        c.strokeStyle = "rgba(140,170,255,0.85)";
        c.lineWidth = 2;
        c.strokeRect(1, 1, 338, 50);
        c.font = "22px NotoSansJP-Regular";
        c.textAlign = "center";
        c.textBaseline = "middle";
        c.fillStyle = "#ffffff";
        c.fillText("E: 話しかける", 170, 27);
        c.flush();
    }
    return _talkHint;
}

// NPC 会話スクリプト
function makeTalkScript() {
    return [
        { motion: { actor: "b", clip: "greeting" } },
        { expr: { actor: "b", emotion: "happy" } },
        { say: { actor: "b", name: "ミドリ", text: "やっほー。ここは 3D 探索モードだよ。\n1〜7 キーで VRMA モーションを再生できるから試してみて。" } },
        { expr: { actor: "b", emotion: "relaxed" } },
        { say: { actor: "b", name: "ミドリ", text: "Tab キーを押すとノベルゲームスタイルの会話モードに切り替わるよ。そっちも見ていってね。" } },
        { expr: { actor: "b", emotion: null } },
        { end: true },
    ];
}

export class ExploreScene extends Scene {
    // app: main.js の共有コンテキスト { renderer, camera, actors: {a,b}, novelUI }
    constructor(app) {
        super();
        this.app = app;
    }

    enter() {
        const world = getWorld();
        this.scene = world.scene;
        this.player = this.app.actors.a;
        this.npc = this.app.actors.b;

        this.player.reset();
        this.npc.reset();
        this.scene.add(this.player.root);
        this.scene.add(this.npc.root);
        this.player.setPosition(0, 0, 0);
        this.player.setHeading(0);
        this.npc.setPosition(2.5, 0, 3.0);
        this.npc.setHeading(Math.PI);
        this._npcTarget = null;
        // 配置確定後にスプリングボーンを静止状態へ (表示瞬間の髪はね防止)
        this.player.settle();
        this.npc.settle();

        this.player.lookAtTarget = this.app.camera;
        this.npc.lookAtTarget = this.app.camera;

        this.rig = new VK.OrbitFollowRig({ angle: Math.PI, pitch: 0.22, dist: 3.6 });
        this.app.camera.fov = 40;
        this.app.camera.updateProjectionMatrix();

        this._nearNPC = false;
        this._tmp = new THREE.Vector3();
    }

    exit() {
        this.scene.remove(this.player.root);
        this.scene.remove(this.npc.root);
    }

    update(dt) {
        const dts = dt / 1000;

        // --- プレイヤー移動 (カメラ相対) ---
        const ix = Input.getValue("moveRight") - Input.getValue("moveLeft");
        const iz = Input.getValue("moveDown") - Input.getValue("moveUp");
        const moving = (ix * ix + iz * iz) > 0.01;
        if (moving) {
            if (this.player.isMotionPlaying) this.player.cancelMotion();
            const mv = this._tmp.set(ix, 0, iz);
            // カメラの水平角ぶん回してスクリーン相対 → ワールド方向に変換
            mv.applyAxisAngle(new THREE.Vector3(0, 1, 0), this.rig.angle);
            mv.normalize().multiplyScalar(WALK_SPEED * dts);
            this.player.position.x += mv.x;
            this.player.position.z += mv.z;
            this.player.faceTowards(mv.x, mv.z);
            this.player.locomotionSpeed = WALK_SPEED;
        } else {
            this.player.locomotionSpeed = 0;
        }

        // --- NPC: プレイヤーが近いと立ち止まって向く / 遠いと広場を散歩 ---
        const px = this.player.position.x, pz = this.player.position.z;
        const nx = this.npc.position.x, nz = this.npc.position.z;
        const distToPlayer = Math.hypot(px - nx, pz - nz);
        this._nearNPC = distToPlayer < TALK_DIST;
        if (this._nearNPC) {
            this.npc.locomotionSpeed = 0;
            if (!this.npc.isMotionPlaying) this.npc.faceTowards(px - nx, pz - nz);
        } else {
            if (!this._npcTarget || Math.hypot(this._npcTarget.x - nx, this._npcTarget.z - nz) < 0.5) {
                const a = Math.random() * Math.PI * 2;
                const r = 2 + Math.random() * 5;
                this._npcTarget = { x: Math.sin(a) * r, z: Math.cos(a) * r };
            }
            const dx = this._npcTarget.x - nx, dz = this._npcTarget.z - nz;
            const d = Math.hypot(dx, dz);
            this.npc.position.x += (dx / d) * NPC_SPEED * dts;
            this.npc.position.z += (dz / d) * NPC_SPEED * dts;
            this.npc.faceTowards(dx, dz);
            this.npc.locomotionSpeed = NPC_SPEED;
        }

        // --- 会話開始 ---
        if (this._nearNPC && Input.isJustPressed("talk")) {
            this.player.locomotionSpeed = 0;
            this.npc.locomotionSpeed = 0;
            this.npc.cancelMotion();
            this.npc.faceTowards(px - nx, pz - nz);
            this.player.faceTowards(nx - px, nz - pz);
            SceneManager.push(new DialogueScene({
                script: makeTalkScript(),
                actors: this.app.actors,
                ui: this.app.novelUI,
                threeCamera: this.app.camera,
                updateActors: [this.player, this.npc],
            }), null, { pauseBelow: true });
            return;
        }

        this.player.update(dts);
        this.npc.update(dts);
    }

    render() {
        this.rig.apply(this.app.camera, this.player.position);
        VK.renderFrame(this.app.renderer, this.scene, this.app.camera);
        if (this._nearNPC && SceneManager.top() === this) {
            getTalkHint().draw((1280 - 340) / 2, 630);
        }
    }

    handleEvent(e) {
        this.rig.handleEvent(e);
        if (e.type === "keydown" && EMOTE_KEYS[e.code]) {
            this.player.playEmote(EMOTE_KEYS[e.code]);
        }
    }
}
