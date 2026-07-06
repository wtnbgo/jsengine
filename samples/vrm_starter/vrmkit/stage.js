// ============================================================
// vrmkit/stage.js — シーンの舞台 (地面 / ライト / 背景) プリセット
// ============================================================

import * as THREE from "three";

// 3D 探索モード用: 草地 + 石畳の広場 + 柱 + 空色フォグ + 太陽光
export function buildExploreStage(scene, opts) {
    opts = opts || {};
    const half = opts.half || 20;

    scene.background = new THREE.Color(0x87b5e5);
    scene.fog = new THREE.Fog(0x87b5e5, 22, 55);

    // 地面 (草)
    const ground = new THREE.Mesh(
        new THREE.PlaneGeometry(half * 2.5, half * 2.5),
        new THREE.MeshLambertMaterial({ color: 0x5a7a48 })
    );
    ground.rotation.x = -Math.PI / 2;
    scene.add(ground);

    // 中央の石畳広場
    const plaza = new THREE.Mesh(
        new THREE.CircleGeometry(7, 40),
        new THREE.MeshLambertMaterial({ color: 0x9a9488 })
    );
    plaza.rotation.x = -Math.PI / 2;
    plaza.position.y = 0.01;
    scene.add(plaza);

    // 広場を囲む柱
    const pillarMat = new THREE.MeshLambertMaterial({ color: 0xd8d0c0 });
    for (let i = 0; i < 8; i++) {
        const a = (i / 8) * Math.PI * 2;
        const p = new THREE.Mesh(new THREE.CylinderGeometry(0.25, 0.3, 3.2, 12), pillarMat);
        p.position.set(Math.sin(a) * 8.5, 1.6, Math.cos(a) * 8.5);
        scene.add(p);
    }

    // 木 (幹 + 円錐の葉) を散らす
    const trunkMat = new THREE.MeshLambertMaterial({ color: 0x6a4a30 });
    const leafMat = new THREE.MeshLambertMaterial({ color: 0x3a6a34 });
    const treePos = [
        [-14, -12], [13, -14], [-15, 10], [15, 12], [-10, 16], [11, 15], [-16, -2], [16, 2],
    ];
    for (let i = 0; i < treePos.length; i++) {
        const tx = treePos[i][0], tz = treePos[i][1];
        const trunk = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.3, 2.0, 8), trunkMat);
        trunk.position.set(tx, 1.0, tz);
        scene.add(trunk);
        const leaf = new THREE.Mesh(new THREE.ConeGeometry(1.6, 3.2, 10), leafMat);
        leaf.position.set(tx, 3.4, tz);
        scene.add(leaf);
    }

    // ライト: 太陽 + fill + 環境光
    const sun = new THREE.DirectionalLight(0xfff4d4, 2.2);
    sun.position.set(5, 10, 4);
    scene.add(sun);
    const fill = new THREE.DirectionalLight(0xc0d8ff, 0.7);
    fill.position.set(-3, 4, -5);
    scene.add(fill);
    scene.add(new THREE.AmbientLight(0xffffff, 0.6));

    return { half: half };
}

// ノベルモード用: 落ち着いた背景 + ポートレートライティング
// (キャラは原点付近、 カメラは +Z 側から向く前提)
export function buildNovelStage(scene, opts) {
    opts = opts || {};
    const bg = (opts.background !== undefined) ? opts.background : 0x2c3050;

    scene.background = new THREE.Color(bg);
    scene.fog = null;

    // 背景ボード (キャラ後方、 わずかに明るいパネルで奥行き感)
    const board = new THREE.Mesh(
        new THREE.PlaneGeometry(24, 12),
        new THREE.MeshLambertMaterial({ color: 0x3a4066 })
    );
    board.position.set(0, 4, -6);
    scene.add(board);

    // 床 (スポットライト風の明るい円)
    const floor = new THREE.Mesh(
        new THREE.CircleGeometry(6, 48),
        new THREE.MeshLambertMaterial({ color: 0x50587e })
    );
    floor.rotation.x = -Math.PI / 2;
    scene.add(floor);

    // ポートレートライティング: key (正面上手) + fill (下手弱) + rim (背後)
    const key = new THREE.DirectionalLight(0xfff2e0, 2.4);
    key.position.set(1.5, 3, 4);
    scene.add(key);
    const fill = new THREE.DirectionalLight(0xbcd0ff, 0.9);
    fill.position.set(-2.5, 1.5, 3);
    scene.add(fill);
    const rim = new THREE.DirectionalLight(0x8ab0ff, 1.6);
    rim.position.set(0, 3, -4);
    scene.add(rim);
    scene.add(new THREE.AmbientLight(0xffffff, 0.55));

    return {};
}
