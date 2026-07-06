// ============================================================
// vrmkit — jsengine 用 VRM ベースシステム (index)
// ============================================================
//
// main.js からは loadModule("vrmkit/vrmkit.js") 一発で全 API を取得する。
// lib/ (three / GLTFLoader / three-vrm / three-vrm-animation) への import は
// すべてこのパッケージ内で完結している。
//
//   const VK = loadModule("vrmkit/vrmkit.js");
//   VK.THREE / VK.createRenderer / VK.loadVRM / VK.loadVRMA / VK.renderFrame
//   VK.VRMActor / VK.OrbitFollowRig / VK.NovelCamera
//   VK.buildExploreStage / VK.buildNovelStage
//   VK.CanvasOverlay / VK.NovelUI / VK.ScriptRunner

export * from "./core.js";
export { VRMActor } from "./actor.js";
export { OrbitFollowRig, NovelCamera } from "./camera_rig.js";
export * from "./stage.js";
export { CanvasOverlay } from "./overlay.js";
export { NovelUI } from "./novel_ui.js";
export { ScriptRunner } from "./script_runner.js";
