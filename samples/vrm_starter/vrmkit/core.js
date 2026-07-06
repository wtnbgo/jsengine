// ============================================================
// vrmkit/core.js — レンダラ生成 / VRM ロード / VRMA ロード
// ============================================================
//
// jsengine 上で three.js + three-vrm を使うための共通処理をまとめた
// エントリモジュール。 lib/ 以下の ESM は全部ここ経由で import する
// (main.js 側から loadModule で直接 lib を読むと解決パスの違いで
//  二重インスタンス化する恐れがあるため、 入口を一本化している)。
//
// - createRenderer(): エンジンのグローバル gl を包む THREE.WebGLRenderer
// - loadVRM(path):   VRM 0.x / 1.0 を Promise で返す (向きは +Z に正規化)
// - loadVRMA(path):  .vrma (VRM Animation) を Promise で返す
// - renderFrame():   resetState + render (エンジンと GL 共有のため必須)

import * as THREE from "three";
import { GLTFLoader } from "../lib/GLTFLoader.js";
import * as VRMLib from "../lib/three-vrm.module.min.js";
import * as VRMALib from "../lib/three-vrm-animation.module.min.js";

export { THREE, VRMLib, VRMALib };
export const { VRMLoaderPlugin, VRMUtils } = VRMLib;
export const { VRMAnimationLoaderPlugin, createVRMAnimationClip, VRMLookAtQuaternionProxy } = VRMALib;

// レンダラ生成。 エンジンのグローバル gl / HTMLCanvasElement シムを使う。
export function createRenderer(opts) {
    opts = opts || {};
    const width = opts.width || 1280;
    const height = opts.height || 720;
    const canvas = new HTMLCanvasElement(width, height);
    canvas.width = width;
    canvas.height = height;
    gl.canvas = canvas;
    const renderer = new THREE.WebGLRenderer({ canvas: canvas, context: gl, antialias: false });
    renderer.setSize(width, height, false);
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    return { renderer: renderer, canvas: canvas, width: width, height: height };
}

// VRM ロード (Promise)。 ロード後に以下を適用済み:
//   - VRMUtils.removeUnnecessaryVertices / combineSkeletons
//   - VRMUtils.rotateVRM0: VRM 0.x を VRM 1.0 と同じ +Z 向きに回転
//     (以降、 モデルの正面は常に +Z。 heading = atan2(dx, dz) で計算できる)
//   - 全メッシュ frustumCulled = false (SkinnedMesh のカリング誤判定対策)
//   - MToon マテリアルの texture uniform に .matrix が無い場合の補完
//   - VRMLookAtQuaternionProxy を scene に追加 (VRMA の lookAt トラック用。
//     createVRMAnimationClip より前に追加されている必要がある)
export function loadVRM(path) {
    return new Promise(function(resolve, reject) {
        try {
            const loader = new GLTFLoader();
            loader.register(function(parser) { return new VRMLib.VRMLoaderPlugin(parser); });
            const buf = fs.readBinary(path);
            loader.parse(buf, "", function(gltf) {
                const vrm = gltf.userData.vrm;
                if (!vrm) { reject(new Error("no VRM data: " + path)); return; }
                VRMLib.VRMUtils.removeUnnecessaryVertices(gltf.scene);
                VRMLib.VRMUtils.combineSkeletons(gltf.scene);
                VRMLib.VRMUtils.rotateVRM0(vrm);
                vrm.scene.traverse(function(child) {
                    if (child.isMesh) {
                        child.frustumCulled = false;
                        if (child.material && child.material.uniforms) {
                            for (const key in child.material.uniforms) {
                                const u = child.material.uniforms[key];
                                if (u && u.value && u.value.isTexture && !u.value.matrix) {
                                    u.value.matrix = new THREE.Matrix3();
                                }
                            }
                        }
                    }
                });
                if (vrm.lookAt) {
                    const proxy = new VRMALib.VRMLookAtQuaternionProxy(vrm.lookAt);
                    proxy.name = "VRMLookAtQuaternionProxy";
                    vrm.scene.add(proxy);
                }
                resolve(vrm);
            }, function(err) { reject(err); });
        } catch (e) { reject(e); }
    });
}

// VRMA (VRM Animation) ロード (Promise)。 VRMAnimation オブジェクトを返す。
// VRMActor.addClip(name, vrmAnimation) に渡すとそのモデル用の
// AnimationClip にリターゲットされる。
export function loadVRMA(path) {
    return new Promise(function(resolve, reject) {
        try {
            const loader = new GLTFLoader();
            loader.register(function(parser) { return new VRMALib.VRMAnimationLoaderPlugin(parser); });
            const buf = fs.readBinary(path);
            loader.parse(buf, "", function(gltf) {
                const anims = gltf.userData.vrmAnimations;
                if (!anims || anims.length === 0) { reject(new Error("no VRM animation: " + path)); return; }
                resolve(anims[0]);
            }, function(err) { reject(err); });
        } catch (e) { reject(e); }
    });
}

// 1 フレーム描画。 three.js はエンジンと GL コンテキストを共有しているので
// 描画前の resetState() が必須 (state キャッシュの食い違い防止)。
export function renderFrame(renderer, scene, camera) {
    try {
        renderer.resetState();
        renderer.render(scene, camera);
    } catch (e) {
        if (!renderFrame._errCount) renderFrame._errCount = 0;
        if (renderFrame._errCount++ < 3) {
            console.error("renderFrame: " + e);
            if (e.stack) console.error(e.stack);
        }
    }
}
