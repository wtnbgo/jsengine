// ============================================================
// vrmkit/actor.js — VRMActor: VRM 1 体の「キャラクター」抽象
// ============================================================
//
// VRM (three-vrm) を包んで以下をまとめて面倒みる:
//   - root (THREE.Group): 位置 / 向きはこの Group を動かす。
//     vrm.scene 自体は rotateVRM0 が回転を書き込むため直接触らない。
//   - VRMA モーション再生: addClip() で登録 → playEmote() で単発再生
//     (AnimationMixer + クロスフェード、 終了で自動的に procedural に復帰)
//   - 表情 (expression): setExpression / setEmotion (フェード付き)
//   - 自動まばたき / 口パク (setMouthOpen)
//   - 視線 (lookAtTarget = camera 等)
//   - procedural 歩行 / アイドル (locomotionSpeed を設定するだけ)
//
// 毎フレーム actor.update(dtSec) を呼ぶこと (vrm.update も内部で呼ぶ)。

import * as THREE from "three";
import { createVRMAnimationClip } from "../lib/three-vrm-animation.module.min.js";

// 感情系 expression (排他的に使う想定のグループ)
const EMOTION_NAMES = ["happy", "angry", "sad", "relaxed", "surprised", "neutral"];

export class VRMActor {
    constructor(vrm, opts) {
        opts = opts || {};
        this.vrm = vrm;
        this.name = opts.name || "";
        this.root = new THREE.Group();
        this.root.add(vrm.scene);

        // --- モーション (VRMA) ---
        this.mixer = new THREE.AnimationMixer(vrm.scene);
        this.clips = {};            // name -> AnimationClip (このモデル用にリターゲット済)
        this.currentAction = null;  // 再生中の AnimationAction
        this.emoteActive = false;   // 単発モーション再生中 (procedural を止める)
        this._pendingStop = null;   // { action, t } フェードアウト完了後に stop する
        const self = this;
        this.mixer.addEventListener("finished", function(e) { self._onActionFinished(e); });

        // --- 表情 ---
        this._exprTargets = {};     // name -> { target, speed }
        this.autoBlink = opts.autoBlink !== false;
        this._blinkWait = 1.5 + Math.random() * 3;
        this._blinkPhase = -1;      // -1 = 待機中, 0..1 = まばたき進行
        this._mouthOpen = 0;

        // --- 移動 ---
        this.locomotionSpeed = 0;   // m/s。 0 でアイドル、 >0 で歩行アニメ
        this._speedSmooth = 0;      // locomotionSpeed のスムージング値 (急変ではねるのを防ぐ)
        this._targetYaw = null;     // faceTowards のスムーズ回頭目標
        this.turnSpeed = 10;        // 回頭の速さ (大きいほど機敏)
        this._walkTime = Math.random() * 10;  // 複数体で位相をずらす
        this.armsDownAngle = (opts.armsDownAngle !== undefined) ? opts.armsDownAngle : 1.15;

        // 腕を「下ろす」Z 回転の符号は正規化リグのバインド向きで変わるので、
        // レスト姿勢の左腕の伸びる方向を実測して決める。 rotateVRM0 が
        // vrm.scene に π 回転を入れるため、 ワールドではなく vrm.scene
        // ローカル系 (= 正規化リグの親系) で測るのがポイント。
        this._armSign = 1;
        {
            const lu = this.bone("leftUpperArm");
            const lo = this.bone("leftLowerArm");
            if (lu && lo) {
                const pu = new THREE.Vector3(), po = new THREE.Vector3();
                vrm.scene.updateMatrixWorld(true);
                lu.getWorldPosition(pu);
                lo.getWorldPosition(po);
                vrm.scene.worldToLocal(pu);
                vrm.scene.worldToLocal(po);
                // ローカル左腕が -X 向き (VRM0) なら +、 +X 向き (VRM1) なら -
                this._armSign = (po.x - pu.x) < 0 ? 1 : -1;
            }
        }
        // _armSign は同時に「リグの正面が -Z (VRM0) か +Z (VRM1) か」も表す。
        // 前後方向の回転 (脚の振り出し / 膝 / 前傾) は全部この符号を掛ける。

        // 骨盤の上下動 / 横揺れ用にヒップのレスト位置を保存
        this._hipsRest = null;
        {
            const hips = this.bone("hips");
            if (hips) this._hipsRest = hips.position.clone();
        }

        // VRMA ロコモーション (setLocomotionClips で有効化。 無ければ procedural)
        this._locoClips = null;   // { idle, walk, run? } → clips のキー名
        this._locoState = null;   // 現在再生中の状態名
        this._locoAction = null;
        this._locoBaseSpeed = 1.4; // walk クリップの想定移動速度 (m/s)、 timeScale 計算用
    }

    // VRMA ベースのロコモーションを有効化する。
    //   actor.addClip("idle", idleAnim); actor.addClip("walk", walkAnim);
    //   actor.setLocomotionClips({ idle: "idle", walk: "walk", baseSpeed: 1.4 });
    // 以降 locomotionSpeed に応じて idle/walk をクロスフェードし、
    // walk の再生速度も移動速度に同期する。 procedural 歩行は使われなくなる。
    setLocomotionClips(opts) {
        if (!opts) {
            this._locoClips = null;
            if (this._locoAction) { this._locoAction.fadeOut(0.2); this._locoAction = null; }
            this._locoState = null;
            return;
        }
        this._locoClips = { idle: opts.idle || "idle", walk: opts.walk || "walk" };
        if (opts.baseSpeed) this._locoBaseSpeed = opts.baseSpeed;
        this._locoState = null;  // 次の update で開始
    }

    // ---- 位置 / 向き ----
    get position() { return this.root.position; }
    setPosition(x, y, z) { this.root.position.set(x, y, z); }
    // モデル正面は +Z (core.js の rotateVRM0 済み前提)
    // setHeading は即時、 faceTowards はスムーズに回頭する (update 内で補間)。
    // 即時回転はスプリングボーンに大きな速度が入って髪がはねるので、
    // 移動中の向き変更は faceTowards を使うこと。
    setHeading(yaw) { this.root.rotation.y = yaw; this._targetYaw = yaw; }
    faceTowards(dx, dz) { this._targetYaw = Math.atan2(dx, dz); }

    // ---- VRMA モーション ----
    // vrmAnimation (core.loadVRMA の戻り値) をこのモデル用のクリップとして登録
    addClip(name, vrmAnimation) {
        this.clips[name] = createVRMAnimationClip(vrmAnimation, this.vrm);
    }

    // ロコモーション VRMA を一時停止 (エモート開始時)
    _suspendLoco(fade) {
        if (this._locoAction) {
            this._locoAction.fadeOut(fade);
            this._locoAction = null;
            this._locoState = null;   // エモート終了後に自動で再開される
        }
    }

    // 単発再生 (挨拶 / ポーズ等)。 再生中は procedural 停止、 終了後フェードで復帰。
    // opts: { fadeIn=0.25, fadeOut=0.3 }
    playEmote(name, opts) {
        opts = opts || {};
        const clip = this.clips[name];
        if (!clip) { console.warn("VRMActor.playEmote: no clip '" + name + "'"); return null; }
        const fadeIn = (opts.fadeIn !== undefined) ? opts.fadeIn : 0.25;
        this._fadeOutDur = (opts.fadeOut !== undefined) ? opts.fadeOut : 0.45;
        if (this.currentAction) this.currentAction.fadeOut(fadeIn);
        this._suspendLoco(fadeIn);
        if (this._pendingStop) { this._pendingStop.action.stop(); this._pendingStop = null; }
        // 現在のポーズ (アイドル/歩行) がフェード元になり、 そこからクリップへ
        // クロスフェードする。 先にポーズをリセットすると一瞬 T ポーズが挟まって
        // スプリングボーンがはねるので、 ここではリセットしない。
        const action = this.mixer.clipAction(clip);
        action.reset();
        action.setLoop(THREE.LoopOnce, 1);
        // 終端ポーズを保持したまま fadeOut で元ポーズへ戻す。
        // false だとクリップ終了の瞬間にアクションが書き込みをやめて
        // 即座に元ポーズへスナップし、 fadeOut が効かず「はねる」
        action.clampWhenFinished = true;
        action.fadeIn(fadeIn);
        action.play();
        this.currentAction = action;
        this.emoteActive = true;
        return action;
    }

    // ループ再生 (ダンス等を流し続けたい場合)
    playLoop(name, opts) {
        opts = opts || {};
        const clip = this.clips[name];
        if (!clip) { console.warn("VRMActor.playLoop: no clip '" + name + "'"); return null; }
        const fadeIn = (opts.fadeIn !== undefined) ? opts.fadeIn : 0.25;
        if (this.currentAction) this.currentAction.fadeOut(fadeIn);
        this._suspendLoco(fadeIn);
        if (this._pendingStop) { this._pendingStop.action.stop(); this._pendingStop = null; }
        const action = this.mixer.clipAction(clip);
        action.reset();
        action.setLoop(THREE.LoopRepeat, Infinity);
        action.fadeIn(fadeIn);
        action.play();
        this.currentAction = action;
        this.emoteActive = true;
        return action;
    }

    // 再生中モーションを中断してフェードアウト (移動入力でキャンセルする時等)
    cancelMotion(fade) {
        if (!this.currentAction) return;
        fade = (fade !== undefined) ? fade : 0.2;
        this.currentAction.fadeOut(fade);
        this._pendingStop = { action: this.currentAction, t: fade };
        this.currentAction = null;
        this.emoteActive = false;
    }

    get isMotionPlaying() { return this.emoteActive; }

    _onActionFinished(e) {
        if (e.action !== this.currentAction) return;
        const fade = this._fadeOutDur || 0.3;
        e.action.fadeOut(fade);
        this._pendingStop = { action: e.action, t: fade };
        this.currentAction = null;
        this.emoteActive = false;
    }

    // ---- 表情 ----
    _expressionAvailable(name) {
        const em = this.vrm.expressionManager;
        return !!(em && em.expressionMap && em.expressionMap[name]);
    }
    expressionNames() {
        const em = this.vrm.expressionManager;
        return (em && em.expressionMap) ? Object.keys(em.expressionMap) : [];
    }
    // weight へフェード付きで遷移。 fade 秒 (0 で即時)
    setExpression(name, weight, fade) {
        if (!this._expressionAvailable(name)) return;
        fade = (fade !== undefined) ? fade : 0.25;
        if (fade <= 0) {
            this.vrm.expressionManager.setValue(name, weight);
            delete this._exprTargets[name];
        } else {
            this._exprTargets[name] = { target: weight, speed: 1 / fade };
        }
    }
    // 感情表情を排他的に設定 (他の感情はフェードで 0 へ)。 name=null で全解除
    setEmotion(name, weight, fade) {
        weight = (weight !== undefined) ? weight : 1.0;
        for (let i = 0; i < EMOTION_NAMES.length; i++) {
            const n = EMOTION_NAMES[i];
            if (n === name) this.setExpression(n, weight, fade);
            else if (this._expressionAvailable(n)) {
                const cur = this.vrm.expressionManager.getValue(n);
                if (cur > 0 || this._exprTargets[n]) this.setExpression(n, 0, fade);
            }
        }
    }
    // 口パク用 (タイプライタ中に外部から毎フレーム設定)
    setMouthOpen(v) { this._mouthOpen = v; }

    // ---- 視線 ----
    set lookAtTarget(obj) { if (this.vrm.lookAt) this.vrm.lookAt.target = obj; }
    get lookAtTarget() { return this.vrm.lookAt ? this.vrm.lookAt.target : null; }

    // ---- humanoid ボーン (normalized) ----
    bone(name) {
        return this.vrm.humanoid ? this.vrm.humanoid.getNormalizedBoneNode(name) : null;
    }
    // head 等のワールド座標 (カメラフレーミング用)。
    // シーン配置直後 (レンダ前) でも正しい値になるよう親方向の行列を更新する。
    boneWorldPosition(name, out) {
        out = out || new THREE.Vector3();
        const node = this.bone(name);
        if (node) {
            node.updateWorldMatrix(true, false);
            node.getWorldPosition(out);
        } else {
            out.copy(this.root.position);
        }
        return out;
    }

    // ---- procedural 歩行 / アイドル ----
    //
    // sin 波 1 本ではなく、 実際の歩容の要素を個別にモデル化する:
    //   - 脚: 股関節の振り出し + スイング期のみの膝屈曲 + 足首の接地補正
    //   - 骨盤: ストライドに同期した回旋 (yaw) / 側方傾斜 (roll) / 上下動 (2 歩で 1 周期)
    //   - 体幹: 骨盤と逆方向のカウンター回旋 + わずかな前傾
    //   - 腕: 同側の脚と逆位相の振り + 肘の追従的な曲げ
    //   - 頭: 体幹の回旋を打ち消して視線を安定させる
    // 前後方向の回転符号はリグのバインド向き (this._armSign) で反転する。
    _applyLocomotionPose(dt) {
        const hum = this.vrm.humanoid;
        if (!hum) return;
        const fwd = this._armSign;   // +1: リグ正面 -Z (VRM0) / -1: +Z (VRM1)
        const b = function(n) { return hum.getNormalizedBoneNode(n); };
        const hips = b("hips"), sp = b("spine"), ch = b("chest") || b("upperChest");
        const neck = b("neck"), head = b("head");
        const ll = b("leftUpperLeg"), rl = b("rightUpperLeg");
        const llo = b("leftLowerLeg"), rlo = b("rightLowerLeg");
        const lf = b("leftFoot"), rf = b("rightFoot");
        const la = b("leftUpperArm"), ra = b("rightUpperArm");
        const lla = b("leftLowerArm"), rla = b("rightLowerArm");
        const az = this.armsDownAngle * this._armSign;   // T ポーズの腕を体側に下ろす

        // 急に locomotionSpeed が変わってもポーズが跳ねないよう、
        // スムージング済みの速度 (_speedSmooth、 update で更新) で振幅を決める
        const speed = this._speedSmooth;
        if (speed > 0.02) {
            // 歩幅と歩調を移動速度に追従させる。 g は全体の「歩きらしさ」強度で、
            // 歩き始め / 止まり際は自然に振幅が小さくなる
            const g = Math.min(1, speed / 1.0);
            const stepRate = 3.6 + speed * 1.5;              // rad/s (2 歩 = 2π)
            this._walkTime += dt * stepRate;
            const p = this._walkTime;                        // 左脚の位相。 右脚は +π
            const stride = Math.min(0.62, 0.15 + speed * 0.30);  // 股関節の振り幅

            // 片脚ぶんの歩容 (phase): 股関節 / 膝 / 足首
            // スイング期 (脚が前に振り出される区間) = cos(phase) > 0 とみなす
            const leg = function(hip, knee, foot, phase) {
                const swing = Math.max(0, Math.cos(phase));      // 0..1 スイング包絡
                if (hip) hip.rotation.x = fwd * stride * Math.sin(phase);
                // 膝: スイング中に大きく畳む + 接地中もわずかに緩める。 屈曲方向は後ろのみ
                if (knee) knee.rotation.x = -fwd * ((0.45 + 0.5 * g) * swing * swing + 0.08);
                // 足首: スイング前半でつま先を上げ、 蹴り出しで伸ばす
                if (foot) foot.rotation.x = fwd * g * (0.35 * swing - 0.18 - 0.12 * Math.sin(phase));
            };
            leg(ll, llo, lf, p);
            leg(rl, rlo, rf, p + Math.PI);

            // 骨盤: 回旋 (前に出る脚側が前へ) / 側方ロール / 上下動 (2 歩で 2 回)
            if (hips) {
                hips.rotation.y = 0.10 * g * Math.sin(p);
                hips.rotation.z = 0.045 * g * Math.cos(p);
                hips.rotation.x = fwd * 0.04 * g;            // わずかな前傾
                if (this._hipsRest) {
                    hips.position.copy(this._hipsRest);
                    hips.position.y += g * (-0.018 + 0.018 * Math.abs(Math.cos(p)));
                    hips.position.x += 0.012 * g * Math.cos(p);  // 支持脚側へ重心移動
                }
            }
            // 体幹: 骨盤とのカウンター回旋 + 前傾。 胸でさらに打ち消す
            if (sp) {
                sp.rotation.y = -0.13 * g * Math.sin(p);
                sp.rotation.x = fwd * 0.05 * g;
                sp.rotation.z = -0.03 * g * Math.cos(p);
            }
            if (ch) ch.rotation.y = -0.05 * g * Math.sin(p);
            // 頭: 回旋の残りを打ち消して視線安定 + 微小な上下
            if (neck) neck.rotation.y = 0.05 * g * Math.sin(p);
            if (head) head.rotation.x = fwd * g * (-0.02 + 0.015 * Math.cos(2 * p));

            // 腕: 同側の脚と逆位相。 肩から振り、 肘は引くときに軽く曲がる
            const armAmp = Math.min(0.42, speed * 0.26);
            const armL = -Math.sin(p), armR = Math.sin(p);   // 左脚前 → 左腕後ろ
            if (la) { la.rotation.x = fwd * armAmp * armL; la.rotation.z = az * 0.94; }
            if (ra) { ra.rotation.x = fwd * armAmp * armR; ra.rotation.z = -az * 0.94; }
            if (lla) lla.rotation.x = fwd * (0.12 + g * (0.10 + 0.16 * Math.max(0, armL)));
            if (rla) rla.rotation.x = fwd * (0.12 + g * (0.10 + 0.16 * Math.max(0, armR)));
        } else {
            // アイドル: 呼吸 + ゆっくりした重心移動 + 腕を下ろした立ちポーズ
            this._walkTime += dt;
            const t = this._walkTime;
            const br = Math.sin(t * 1.6) * 0.02;             // 呼吸
            const sway = Math.sin(t * 0.45);                 // 重心のゆらぎ
            const zero = function(n) { if (n) { n.rotation.set(0, 0, 0); } };
            zero(ll); zero(rl); zero(llo); zero(rlo); zero(lf); zero(rf);
            if (llo) llo.rotation.x = -fwd * 0.04;           // 膝を突っ張らせない
            if (rlo) rlo.rotation.x = -fwd * 0.04;
            if (hips) {
                hips.rotation.set(0, 0.02 * sway, 0.008 * sway);
                if (this._hipsRest) {
                    hips.position.copy(this._hipsRest);
                    hips.position.x += 0.006 * sway;
                }
            }
            if (sp) sp.rotation.set(br + fwd * 0.01, -0.015 * sway, 0);
            if (ch) ch.rotation.set(br * 0.5, 0, 0);
            if (neck) neck.rotation.set(0, 0.01 * sway, 0);
            if (head) head.rotation.set(-br * 0.5, 0, 0);
            if (la) { la.rotation.set(0, 0, az - br * 0.5 * this._armSign); }
            if (ra) { ra.rotation.set(0, 0, -az + br * 0.5 * this._armSign); }
            if (lla) lla.rotation.set(fwd * 0.12, 0, 0);
            if (rla) rla.rotation.set(fwd * 0.12, 0, 0);
        }
    }

    // ---- VRMA ロコモーション (setLocomotionClips 有効時) ----
    _updateLocomotionVRMA() {
        const desired = (this.locomotionSpeed > 0.05) ? "walk" : "idle";
        const clipName = this._locoClips[desired];
        if (this._locoState !== desired) {
            const clip = this.clips[clipName];
            if (!clip) return;  // クリップ未登録なら何もしない (procedural は使わない)
            const action = this.mixer.clipAction(clip);
            action.reset();
            action.setLoop(THREE.LoopRepeat, Infinity);
            action.fadeIn(0.25);
            action.play();
            if (this._locoAction) this._locoAction.fadeOut(0.25);
            this._locoAction = action;
            this._locoState = desired;
        }
        // walk の再生速度を移動速度に同期
        if (this._locoAction && this._locoState === "walk") {
            this._locoAction.timeScale = Math.max(0.5, this.locomotionSpeed / this._locoBaseSpeed);
        }
    }

    // 配置確定後に呼ぶと、 スプリングボーン (髪 / 服の揺れ) を現在の
    // ポーズ / 位置で静止状態に張り直す。 これを呼ばずに表示すると、
    // ロード時の位置から配置先への「瞬間移動」が物理の速度として入り、
    // 表示された瞬間に髪がはね上がる。 シーン enter やテレポート直後に呼ぶこと。
    settle() {
        // 現在のロコモーションポーズ (腕下ろし等) を先に適用してから張り直す
        if (!this._locoClips) this._applyLocomotionPose(0.0001);
        if (this.vrm.humanoid && this.vrm.humanoid.update) this.vrm.humanoid.update();
        this.root.updateMatrixWorld(true);
        const sbm = this.vrm.springBoneManager;
        if (sbm && sbm.reset) sbm.reset();
    }

    // 位置設定 + 物理静止をまとめたテレポートヘルパー
    teleportTo(x, y, z, yaw) {
        this.root.position.set(x, y, z);
        if (yaw !== undefined) this.root.rotation.y = yaw;
        this.settle();
    }

    // ---- 毎フレーム更新 ----
    update(dtSec) {
        // ロード直後やウィンドウ停止からの復帰でフレーム delta が跳ねると
        // スプリングボーンが暴れるのでクランプする
        if (dtSec > 0.05) dtSec = 0.05;

        // 移動速度のスムージング (歩き出し / 停止でポーズが 1 フレームで
        // フル振幅に切り替わって髪がはねるのを防ぐ)
        {
            const k = 1 - Math.exp(-dtSec * 6);
            this._speedSmooth += (this.locomotionSpeed - this._speedSmooth) * k;
            if (this.locomotionSpeed === 0 && this._speedSmooth < 0.02) this._speedSmooth = 0;
        }
        // faceTowards のスムーズ回頭 (即時回転は物理に大きな速度が入る)
        if (this._targetYaw !== null) {
            let diff = this._targetYaw - this.root.rotation.y;
            diff = ((diff + Math.PI) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2) - Math.PI;
            this.root.rotation.y += diff * (1 - Math.exp(-dtSec * this.turnSpeed));
        }
        // 1) ロコモーション (VRMA 再生中 / フェードアウト中は触らない)。
        //    setLocomotionClips 済みなら VRMA ループ、 無ければ procedural 歩行
        if (!this.emoteActive && !this._pendingStop) {
            if (this._locoClips) this._updateLocomotionVRMA();
            else this._applyLocomotionPose(dtSec);
        }
        // 2) mixer (VRMA)。 フェードアウトが終わったら action を止めて姿勢をリセット
        this.mixer.update(dtSec);
        if (this._pendingStop) {
            this._pendingStop.t -= dtSec;
            if (this._pendingStop.t <= 0) {
                this._pendingStop.action.stop();
                this._pendingStop = null;
                if (this.vrm.humanoid && this.vrm.humanoid.resetNormalizedPose) {
                    this.vrm.humanoid.resetNormalizedPose();
                }
                // リセット直後に同フレームでロコモーションポーズを適用する。
                // これをしないと T ポーズが 1 フレーム描画されてしまう
                if (!this._locoClips) this._applyLocomotionPose(0.0001);
            }
        }
        // 3) 表情フェード
        const em = this.vrm.expressionManager;
        if (em) {
            for (const name in this._exprTargets) {
                const et = this._exprTargets[name];
                const cur = em.getValue(name);
                const diff = et.target - cur;
                const step = et.speed * dtSec;
                if (Math.abs(diff) <= step) {
                    em.setValue(name, et.target);
                    delete this._exprTargets[name];
                } else {
                    em.setValue(name, cur + Math.sign(diff) * step);
                }
            }
            // 自動まばたき
            if (this.autoBlink && this._expressionAvailable("blink")) {
                if (this._blinkPhase < 0) {
                    this._blinkWait -= dtSec;
                    if (this._blinkWait <= 0) this._blinkPhase = 0;
                } else {
                    this._blinkPhase += dtSec / 0.22;  // まばたき 1 回 0.22 秒
                    if (this._blinkPhase >= 1) {
                        this._blinkPhase = -1;
                        this._blinkWait = 1.5 + Math.random() * 3.5;
                        em.setValue("blink", 0);
                    } else {
                        em.setValue("blink", Math.sin(Math.PI * this._blinkPhase));
                    }
                }
            }
            // 口パク
            if (this._expressionAvailable("aa")) {
                em.setValue("aa", this._mouthOpen);
            }
        }
        // 4) vrm.update: expression / lookAt / springbone / normalized→raw 反映
        this.vrm.update(dtSec);
    }

    // シーン切替時などに状態を初期化
    reset() {
        this.cancelMotion(0);
        if (this._pendingStop) { this._pendingStop.action.stop(); this._pendingStop = null; }
        this._locoAction = null;
        this._locoState = null;
        this.mixer.stopAllAction();
        if (this.vrm.humanoid && this.vrm.humanoid.resetNormalizedPose) {
            this.vrm.humanoid.resetNormalizedPose();
        }
        const hipsBone = this.bone("hips");
        if (hipsBone && this._hipsRest) hipsBone.position.copy(this._hipsRest);
        const em = this.vrm.expressionManager;
        if (em && em.expressionMap) {
            for (const name in em.expressionMap) em.setValue(name, 0);
        }
        this._exprTargets = {};
        this._mouthOpen = 0;
        this.locomotionSpeed = 0;
    }
}
