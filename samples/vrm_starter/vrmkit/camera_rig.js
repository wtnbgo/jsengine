// ============================================================
// vrmkit/camera_rig.js — カメラリグ 2 種
// ============================================================
//
// OrbitFollowRig : 3D 探索用の三人称追従カメラ。
//                  マウスドラッグで回転、 ホイールでズーム。
//                  handleEvent(e) にエンジンのイベントを流し込むだけで動く。
//
// NovelCamera    : ノベルゲーム風の「2D 的画角」フレーミングカメラ。
//                  望遠 (fov 20°) でパースを圧縮し、 humanoid ボーン位置から
//                  closeUp / bustUp / waistUp / fullBody / twoShot の構図を計算。
//                  frameActor()/frameTwo() で目標を設定 → update(dt) でスムーズ遷移。

import * as THREE from "three";

export class OrbitFollowRig {
    constructor(opts) {
        opts = opts || {};
        this.angle = (opts.angle !== undefined) ? opts.angle : Math.PI;  // プレイヤー背後から
        this.pitch = (opts.pitch !== undefined) ? opts.pitch : 0.25;
        this.dist = (opts.dist !== undefined) ? opts.dist : 3.5;
        this.minDist = opts.minDist || 1.2;
        this.maxDist = opts.maxDist || 10.0;
        this.height = (opts.height !== undefined) ? opts.height : 1.3;  // 注視点の高さ
        this.rotateSpeed = opts.rotateSpeed || 0.005;
        this._dragging = false;
        this._lastX = 0;
        this._lastY = 0;
    }

    // mousedown / mouseup / mousemove / wheel を受け取る
    handleEvent(e) {
        if (e.type === "mousedown" && e.button === 0) {
            this._dragging = true; this._lastX = e.clientX; this._lastY = e.clientY;
        } else if (e.type === "mouseup" && e.button === 0) {
            this._dragging = false;
        } else if (e.type === "mousemove" && this._dragging) {
            this.angle -= (e.clientX - this._lastX) * this.rotateSpeed;
            this.pitch += (e.clientY - this._lastY) * this.rotateSpeed;
            if (this.pitch < -1.2) this.pitch = -1.2;
            if (this.pitch > 1.2) this.pitch = 1.2;
            this._lastX = e.clientX; this._lastY = e.clientY;
        } else if (e.type === "wheel") {
            this.dist += e.deltaY * 0.005;
            if (this.dist < this.minDist) this.dist = this.minDist;
            if (this.dist > this.maxDist) this.dist = this.maxDist;
        }
    }

    // targetPos (THREE.Vector3、 キャラの足元) を注視するようにカメラを配置
    apply(camera, targetPos) {
        const cy = targetPos.y + this.height + Math.sin(this.pitch) * this.dist * 0.5;
        const hDist = Math.cos(this.pitch) * this.dist;
        camera.position.set(
            targetPos.x + Math.sin(this.angle) * hDist,
            cy,
            targetPos.z + Math.cos(this.angle) * hDist
        );
        camera.lookAt(targetPos.x, targetPos.y + this.height * 0.75, targetPos.z);
    }
}

// 構図プリセット: head ボーン基準の [中心の頭からの下オフセット, 画面半分の高さ(m)]
const FRAME_PRESETS = {
    closeUp: { centerDown: 0.05, halfHeight: 0.26 },
    bustUp:  { centerDown: 0.30, halfHeight: 0.55 },
    waistUp: { centerDown: 0.50, halfHeight: 0.85 },
    fullBody: null,  // 特殊処理 (身長から計算)
};

export class NovelCamera {
    constructor(camera, opts) {
        opts = opts || {};
        this.camera = camera;
        this.fov = opts.fov || 20;            // 望遠でパース圧縮 = 2D 的画角
        this.easeSpeed = opts.easeSpeed || 5; // 大きいほど速く追従
        this._desiredPos = new THREE.Vector3(0, 1.3, 4);
        this._desiredLook = new THREE.Vector3(0, 1.3, 0);
        this._curPos = null;   // 初回 update で snap
        this._curLook = null;
        this._tmpA = new THREE.Vector3();
        this._tmpB = new THREE.Vector3();
    }

    // 1 人を構図に収める。
    // preset: "closeUp" | "bustUp" | "waistUp" | "fullBody"
    // opts: {
    //   xOffset: -0.5..0.5  — 被写体を画面横方向にずらす (画面幅比。 正で被写体が右へ)
    //   yaw:     カメラの回り込み角 (rad)。 0 = 真正面 (+Z 側) から
    //   snap:    true で遷移なし即時
    // }
    frameActor(actor, preset, opts) {
        opts = opts || {};
        const head = actor.boneWorldPosition("head", this._tmpA);
        let centerY, halfH;
        if (preset === "fullBody" || !FRAME_PRESETS[preset]) {
            const rootY = actor.root.position.y;
            const height = (head.y - rootY) + 0.12;  // 頭頂ぶんのマージン
            halfH = height / 2 + 0.08;
            centerY = rootY + height / 2;
        } else {
            const p = FRAME_PRESETS[preset];
            centerY = head.y - p.centerDown;
            halfH = p.halfHeight;
        }
        this._setFrame(head.x, centerY, head.z, halfH, opts);
    }

    // 2 人を 1 画面に収める (twoShot)
    frameTwo(actorA, actorB, opts) {
        opts = opts || {};
        const ha = actorA.boneWorldPosition("head", this._tmpA);
        const hb = actorB.boneWorldPosition("head", this._tmpB);
        const cx = (ha.x + hb.x) / 2;
        const cz = (ha.z + hb.z) / 2;
        const centerY = (ha.y + hb.y) / 2 - 0.35;
        const halfW = Math.abs(ha.x - hb.x) / 2 + 0.55;
        const halfH = Math.max(0.65, halfW / this.camera.aspect + 0.05);
        this._setFrame(cx, centerY, cz, halfH, opts);
    }

    _setFrame(cx, cy, cz, halfH, opts) {
        const yaw = opts.yaw || 0;
        const xOffset = opts.xOffset || 0;
        const fovRad = (this.fov * Math.PI) / 180;
        const dist = halfH / Math.tan(fovRad / 2);
        // 注視点を横にずらすと被写体は逆方向に動くので符号反転
        const frameW = 2 * halfH * this.camera.aspect;
        const lookX = cx - xOffset * frameW;
        this._desiredLook.set(lookX, cy, cz);
        this._desiredPos.set(
            lookX + Math.sin(yaw) * dist,
            cy,
            cz + Math.cos(yaw) * dist
        );
        if (opts.snap) { this._curPos = null; this._curLook = null; }
    }

    update(dtSec) {
        if (!this._curPos) {
            this._curPos = this._desiredPos.clone();
            this._curLook = this._desiredLook.clone();
        } else {
            const k = 1 - Math.exp(-dtSec * this.easeSpeed);
            this._curPos.lerp(this._desiredPos, k);
            this._curLook.lerp(this._desiredLook, k);
        }
        if (this.camera.fov !== this.fov) {
            this.camera.fov = this.fov;
            this.camera.updateProjectionMatrix();
        }
        this.camera.position.copy(this._curPos);
        this.camera.lookAt(this._curLook.x, this._curLook.y, this._curLook.z);
    }
}
