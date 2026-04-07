// main.js - デモスクリプト
// 数字キー 1～8 でデモ切り替え
//   1: 頂点カラー三角形（WASD移動、ホイール透明度）
//   2: Canvas2D 図形描画（矩形、円、パス）
//   3: Canvas2D テキスト描画（要フォントファイル）
//   4: Canvas2D アニメーション（回転する図形）
//   5: pixi.js v5 テスト
//   6: Canvas2D drawImage / getImageData / putImageData テスト
//   7: Canvas2D dirty rect 差分更新テスト
//   8: three.js r128 テスト
// Space: ビープ音再生  R: リセット
// 起動オプション: -demo N で初期デモモード指定

// コマンドライン引数からオプション解析
var initialDemo = 0;
if (typeof __args !== "undefined") {
    for (var _ai = 0; _ai < __args.length; _ai++) {
        if (__args[_ai] === "-demo" && _ai + 1 < __args.length) {
            initialDemo = parseInt(__args[_ai + 1]) || 0;
        }
    }
}

// ============================================================
// シェーダソース
// ============================================================

var vertexShaderSource =
    "#version 300 es\n" +
    "layout(location = 0) in vec2 aPosition;\n" +
    "layout(location = 1) in vec3 aColor;\n" +
    "uniform vec2 uOffset;\n" +
    "out vec3 vColor;\n" +
    "void main() {\n" +
    "    gl_Position = vec4(aPosition + uOffset, 0.0, 1.0);\n" +
    "    vColor = aColor;\n" +
    "}\n";

var fragmentShaderSource =
    "#version 300 es\n" +
    "precision mediump float;\n" +
    "in vec3 vColor;\n" +
    "uniform float uAlpha;\n" +
    "out vec4 fragColor;\n" +
    "void main() {\n" +
    "    fragColor = vec4(vColor, uAlpha);\n" +
    "}\n";

var texVertSource =
    "#version 300 es\n" +
    "layout(location = 0) in vec2 aPosition;\n" +
    "layout(location = 1) in vec2 aTexCoord;\n" +
    "out vec2 vTexCoord;\n" +
    "void main() {\n" +
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n" +
    "    vTexCoord = aTexCoord;\n" +
    "}\n";

var texFragSource =
    "#version 300 es\n" +
    "precision mediump float;\n" +
    "in vec2 vTexCoord;\n" +
    "uniform sampler2D uTexture;\n" +
    "out vec4 fragColor;\n" +
    "void main() {\n" +
    "    fragColor = texture(uTexture, vTexCoord);\n" +
    "}\n";

// ============================================================
// GL 変数
// ============================================================

var program = null, vao = null, vbo = null;
var uOffsetLoc = null, uAlphaLoc = null;
var texProgram = null, texVao = null, texVbo = null, texIbo = null;
var texTexture = null;
var offsetX = 0.0, offsetY = 0.0, moveSpeed = 0.02;
var mouseX = 0.0, mouseY = 0.0, mouseDown = false;
var alpha = 1.0;
var keysDown = {};

// デモモード
var demoMode = 1;
var time = 0;

// Canvas2D
var canvas2d = null;

// ============================================================
// ヘルパー
// ============================================================

function compileShader(type, source) {
    var s = gl.createShader(type);
    gl.shaderSource(s, source);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        console.error("Shader error: " + gl.getShaderInfoLog(s));
        return null;
    }
    return s;
}

function createProgramGL(vsSrc, fsSrc) {
    var vs = compileShader(gl.VERTEX_SHADER, vsSrc);
    var fs = compileShader(gl.FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return null;
    var prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        console.error("Link error: " + gl.getProgramInfoLog(prog));
        return null;
    }
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    return prog;
}

// テクスチャを画面全体に描画するクワッド
function initFullscreenQuad() {
    texProgram = createProgramGL(texVertSource, texFragSource);
    if (!texProgram) return;
    var verts = new Float32Array([
        -1, 1,  0, 0,
         1, 1,  1, 0,
         1,-1,  1, 1,
        -1,-1,  0, 1
    ]);
    var idx = new Uint16Array([0,1,2, 0,2,3]);
    texVao = gl.createVertexArray();
    gl.bindVertexArray(texVao);
    texVbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, texVbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);
    texIbo = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, texIbo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 16, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 16, 8);
    gl.bindVertexArray(null);
}

// Canvas2D テクスチャを左上寄せ原寸で描画
// screenW/screenH: ウィンドウサイズ
var screenW = 1280, screenH = 720;

function drawCanvas2DAt(c2d) {
    var w = c2d.width, h = c2d.height;
    // NDC 座標: 左上 = (-1,1), 右下 = (1,-1)
    var x0 = -1.0;
    var y0 = 1.0;
    var x1 = -1.0 + (w / screenW) * 2.0;
    var y1 = 1.0 - (h / screenH) * 2.0;

    var verts = new Float32Array([
        x0, y0,  0, 0,
        x1, y0,  1, 0,
        x1, y1,  1, 1,
        x0, y1,  0, 1
    ]);

    gl.useProgram(texProgram);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, c2d.texture);
    gl.bindVertexArray(texVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, texVbo);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, verts);
    gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
    gl.bindVertexArray(null);
}

// ============================================================
// デモ1: 頂点カラー三角形
// ============================================================

function initDemo1() {
    program = createProgramGL(vertexShaderSource, fragmentShaderSource);
    if (!program) return;
    uOffsetLoc = gl.getUniformLocation(program, "uOffset");
    uAlphaLoc = gl.getUniformLocation(program, "uAlpha");
    var vertices = new Float32Array([
         0.0,  0.5,  1.0, 0.0, 0.0,
        -0.5, -0.5,  0.0, 1.0, 0.0,
         0.5, -0.5,  0.0, 0.0, 1.0
    ]);
    vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 20, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 20, 8);
    gl.bindVertexArray(null);
}

function renderDemo1() {
    gl.useProgram(program);
    gl.uniform2f(uOffsetLoc, offsetX, offsetY);
    gl.uniform1f(uAlphaLoc, alpha);
    gl.bindVertexArray(vao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindVertexArray(null);
}

// ============================================================
// デモ2: Canvas2D 図形描画
// ============================================================

function renderDemo2() {
    var c = canvas2d;

    // 背景
    c.fillStyle = "#1a1a2e";
    c.fillRect(0, 0, 512, 512);

    // 矩形（塗り）
    c.fillStyle = "#e94560";
    c.fillRect(30, 30, 120, 80);

    // 矩形（枠線）
    c.strokeStyle = "#0f3460";
    c.lineWidth = 4;
    c.strokeRect(180, 30, 120, 80);

    // 半透明矩形
    c.globalAlpha = 0.5;
    c.fillStyle = "#16213e";
    c.fillRect(60, 60, 120, 80);
    c.globalAlpha = 1.0;

    // 円（パスで描画）
    c.beginPath();
    c.arc(400, 80, 50, 0, Math.PI * 2);
    c.closePath();
    c.fillStyle = "#533483";
    c.fill();

    // 半円
    c.beginPath();
    c.arc(400, 80, 50, 0, Math.PI);
    c.closePath();
    c.fillStyle = "rgba(255,200,0,0.7)";
    c.fill();

    // 三角形パス
    c.beginPath();
    c.moveTo(50, 250);
    c.lineTo(150, 180);
    c.lineTo(150, 320);
    c.closePath();
    c.fillStyle = "#2b9348";
    c.fill();
    c.strokeStyle = "white";
    c.lineWidth = 2;
    c.stroke();

    // ベジェ曲線
    c.beginPath();
    c.moveTo(200, 250);
    c.bezierCurveTo(250, 150, 350, 350, 450, 250);
    c.strokeStyle = "#ff6b6b";
    c.lineWidth = 3;
    c.stroke();

    // 複数矩形パターン
    var colors = ["#e63946", "#457b9d", "#2a9d8f", "#e9c46a", "#f4a261"];
    for (var i = 0; i < 5; i++) {
        c.fillStyle = colors[i];
        c.fillRect(30 + i * 60, 380, 50, 50);
    }

    // 同心円
    for (var j = 5; j > 0; j--) {
        c.beginPath();
        c.arc(400, 400, j * 20, 0, Math.PI * 2);
        c.closePath();
        c.fillStyle = "rgba(" + (j*50) + "," + (255-j*40) + "," + (j*30+100) + ",0.6)";
        c.fill();
    }

    c.flush();
}

// ============================================================
// デモ3: Canvas2D テキスト描画
// ============================================================

function renderDemo3() {
    var c = canvas2d;

    // 背景
    c.fillStyle = "#0d1117";
    c.fillRect(0, 0, 512, 512);

    // OpenSans Bold タイトル
    c.font = "48px OpenSans-Bold";
    c.fillStyle = "#58a6ff";
    c.fillText("Canvas2D", 30, 80);

    // OpenSans Regular サブタイトル
    c.font = "32px OpenSans-Regular";
    c.fillStyle = "#f0f6fc";
    c.fillText("ThorVG + duktape", 30, 140);

    // Roboto Regular
    c.font = "24px Roboto-Regular";
    c.fillStyle = "#8b949e";
    c.fillText("OpenGL ES 3.0 Rendering", 30, 190);

    // 複数フォント・色
    var items = [
        {text: "OpenSans Regular",  font: "24px OpenSans-Regular",  color: "#ff7b72", y: 250},
        {text: "OpenSans Bold",     font: "24px OpenSans-Bold",     color: "#3fb950", y: 290},
        {text: "Roboto Regular",    font: "24px Roboto-Regular",    color: "#58a6ff", y: 330},
        {text: "RobotoMono",        font: "24px RobotoMono-VariableFont_wght", color: "#d29922", y: 370},
    ];
    for (var i = 0; i < items.length; i++) {
        c.font = items[i].font;
        c.fillStyle = items[i].color;
        c.fillText(items[i].text, 30, items[i].y);

        // 色見本矩形
        c.fillRect(350, items[i].y - 18, 60, 22);
    }

    // ストロークテキスト
    c.font = "40px OpenSans-Bold";
    c.strokeStyle = "#f78166";
    c.lineWidth = 2;
    c.strokeText("Outline Text", 30, 450);

    // サイズバリエーション
    c.fillStyle = "#bc8cff";
    var sizes = [12, 16, 20, 28, 36];
    var tx = 300;
    for (var j = 0; j < sizes.length; j++) {
        c.font = sizes[j] + "px OpenSans-Regular";
        c.fillText(sizes[j] + "px", tx, 450 + (j > 0 ? 10 : 0));
        tx += sizes[j] * 2 + 10;
    }

    c.flush();
}

// ============================================================
// デモ4: Canvas2D アニメーション
// ============================================================

function renderDemo4() {
    var c = canvas2d;

    // 背景
    c.fillStyle = "#16213e";
    c.fillRect(0, 0, 512, 512);

    var cx = 256, cy = 256;

    // 回転する四角形群
    for (var i = 0; i < 6; i++) {
        c.save();
        c.translate(cx, cy);
        c.rotate(time * 0.001 * (i + 1) * 0.5 + i * Math.PI / 3);

        var size = 40 + i * 15;
        var hue = (i * 60 + Math.floor(time * 0.05)) % 360;
        // HSL 簡易変換
        var r = 0, g = 0, b = 0;
        var h = hue / 60;
        var x = 1 - Math.abs(h % 2 - 1);
        if      (h < 1) { r = 1; g = x; }
        else if (h < 2) { r = x; g = 1; }
        else if (h < 3) { g = 1; b = x; }
        else if (h < 4) { g = x; b = 1; }
        else if (h < 5) { r = x; b = 1; }
        else            { r = 1; b = x; }

        c.globalAlpha = 0.7;
        c.fillStyle = "rgba(" + Math.floor(r*255) + "," + Math.floor(g*255) + "," + Math.floor(b*255) + ",1)";
        c.fillRect(-size/2, -size/2, size, size);

        c.strokeStyle = "white";
        c.lineWidth = 1;
        c.strokeRect(-size/2, -size/2, size, size);
        c.globalAlpha = 1.0;

        c.restore();
    }

    // 中央の回転する円
    c.save();
    c.translate(cx, cy);
    var circleR = 30 + Math.sin(time * 0.002) * 10;
    c.beginPath();
    c.arc(0, 0, circleR, 0, Math.PI * 2);
    c.closePath();
    c.fillStyle = "white";
    c.fill();
    c.restore();

    // 軌道上の小さな円
    for (var j = 0; j < 8; j++) {
        var angle = time * 0.001 + j * Math.PI / 4;
        var orbitR = 150;
        var px = cx + Math.cos(angle) * orbitR;
        var py = cy + Math.sin(angle) * orbitR;
        c.beginPath();
        c.arc(px, py, 10, 0, Math.PI * 2);
        c.closePath();
        c.fillStyle = "rgba(255,255,255,0.5)";
        c.fill();
    }

    // FPS / 時間表示
    c.font = "18px RobotoMono-VariableFont_wght";
    c.fillStyle = "rgba(255,255,255,0.8)";
    c.fillText("t=" + (time * 0.001).toFixed(1) + "s", 10, 30);
    c.fillText("Demo 4: Animation", 10, 500);

    c.flush();
}

// ============================================================
// デモ5: pixi.js v5
// ============================================================

var pixiApp = null;
var pixiBox = null;
var pixiInited = false;

function initDemo5() {
    if (pixiInited) return;
    pixiInited = true;

    // ポリフィル・シム読み込み
    loadScript("lib/polyfill.js");
    loadScript("lib/browser_shim.js");
    loadScript("lib/pixi.min.js");
    console.log("PIXI " + PIXI.VERSION + " loaded");

    pixiApp = new PIXI.Application({
        width: 1280,
        height: 720,
        backgroundColor: 0x1099bb,
        backgroundAlpha: 1,
        resolution: 1,
        antialias: false,
        transparent: false,
        clearBeforeRender: true,
        preserveDrawingBuffer: false
    });

    // デバッグ: canvas サイズと GL 状態を確認
    var view = pixiApp.renderer.view || pixiApp.view;
    if (view) {
        console.log("pixi view: " + view.width + "x" + view.height);
    }
    console.log("pixi screen: " + pixiApp.screen.width + "x" + pixiApp.screen.height);
    console.log("pixi renderer size: " + pixiApp.renderer.width + "x" + pixiApp.renderer.height);

    var g = new PIXI.Graphics();

    // 赤い矩形
    g.beginFill(0xFF3300);
    g.drawRect(100, 100, 250, 180);
    g.endFill();

    // 白枠 + 青い円
    g.lineStyle(4, 0xFFFFFF, 1);
    g.beginFill(0x66CCFF);
    g.drawCircle(600, 300, 100);
    g.endFill();

    // 緑の角丸矩形
    g.lineStyle(2, 0x00FF00, 1);
    g.beginFill(0x00AA00, 0.5);
    g.drawRoundedRect(900, 150, 200, 150, 20);
    g.endFill();

    // 黄色い三角形
    g.beginFill(0xFFFF00, 0.8);
    g.moveTo(400, 500);
    g.lineTo(500, 350);
    g.lineTo(600, 500);
    g.closePath();
    g.endFill();

    // ピンクの線
    g.lineStyle(3, 0xFF00FF, 1);
    g.moveTo(50, 600);
    g.lineTo(400, 650);
    g.lineTo(750, 580);
    g.lineTo(1100, 670);

    pixiApp.stage.addChild(g);

    // 回転する白い矩形
    pixiBox = new PIXI.Graphics();
    pixiBox.beginFill(0xFFFFFF);
    pixiBox.drawRect(-30, -30, 60, 60);
    pixiBox.endFill();
    pixiBox.x = 640;
    pixiBox.y = 360;
    pixiApp.stage.addChild(pixiBox);

    console.log("pixi.js demo initialized");
}

function renderDemo5() {
    if (!pixiApp) return;
    if (pixiBox) {
        pixiBox.rotation = time * 0.002;
        pixiBox.x = 640 + Math.cos(time * 0.001) * 200;
        pixiBox.y = 360 + Math.sin(time * 0.0015) * 100;
    }
    // 他のデモで GL ステートが変更されている可能性があるのでリセット
    pixiApp.renderer.reset();
    pixiApp.renderer.render(pixiApp.stage);
}

// ============================================================
// デモ6: Canvas2D drawImage / getImageData / putImageData テスト
// ============================================================

var canvas2d_demo6 = null;

function renderDemo6() {
    if (!canvas2d_demo6) {
        canvas2d_demo6 = new Canvas2D(500, 700);
    }
    var c = canvas2d_demo6;

    // 背景クリア
    c.clearRect(0, 0, 500, 700);

    // 背景色
    c.fillStyle = "#222233";
    c.fillRect(0, 0, 500, 700);

    // タイトル
    c.font = "18px OpenSans-Bold";
    c.fillStyle = "white";
    c.fillText("Demo 6: drawImage / ImageData", 10, 20);

    // --- drawImage テスト ---
    if (testPatternImg) {
        // 1. そのまま描画 (3引数: img, dx, dy)
        c.fillStyle = "#444466";
        c.fillRect(10, 30, 74, 74);
        c.drawImage(testPatternImg, 15, 35);
        c.font = "12px OpenSans-Regular";
        c.fillStyle = "white";
        c.fillText("original", 15, 118);

        // 2. リサイズ描画 (5引数: img, dx, dy, dw, dh)
        c.fillStyle = "#444466";
        c.fillRect(100, 30, 124, 74);
        c.drawImage(testPatternImg, 105, 35, 120, 70);
        c.fillStyle = "white";
        c.fillText("resize 120x70", 105, 118);

        // 3. 切り出し描画 (9引数: img, sx, sy, sw, sh, dx, dy, dw, dh)
        c.fillStyle = "#444466";
        c.fillRect(240, 30, 84, 84);
        c.drawImage(testPatternImg, 16, 16, 32, 32, 245, 35, 80, 80);
        c.fillStyle = "white";
        c.fillText("clip+scale", 245, 128);

        // 4. 同じ画像を複数回配置
        c.fillStyle = "white";
        c.fillText("tiled:", 10, 150);
        for (var i = 0; i < 5; i++) {
            c.drawImage(testPatternImg, 10 + i * 70, 155);
        }
    } else {
        c.fillStyle = "#ff4444";
        c.font = "16px OpenSans-Regular";
        c.fillText("test_pattern.png not loaded", 10, 80);
    }

    // === セクション2: Image シム経由テスト (Y=240〜) ===
    c.fillStyle = "#333355";
    c.fillRect(0, 230, 500, 200);
    c.font = "14px OpenSans-Bold";
    c.fillStyle = "#ffcc00";
    c.fillText("Image shim drawImage (RPG Maker MV style):", 10, 250);

    if (typeof Image === "undefined") {
        c.fillStyle = "#ff4444";
        c.fillText("Image not available (load polyfill first via Demo 5)", 10, 270);
    }
    var testImg = (typeof Image !== "undefined") ? new Image() : null;
    if (testImg) testImg.src = "test_pattern.png";
    c.font = "12px OpenSans-Regular";
    if (testImg) {
        c.fillStyle = "white";
        c.fillText("_data=" + (testImg._data ? "yes(" + testImg._data.byteLength + "B)" : "NO") +
            "  w=" + testImg.width + "  h=" + testImg.height, 10, 270);
    }

    if (testImg && testImg._data) {
        // A: Image シム → Canvas2D drawImage
        c.fillStyle = "#444466";
        c.fillRect(10, 280, 74, 74);
        c.drawImage(testImg, 15, 285);
        c.fillStyle = "white";
        c.fillText("A: direct", 15, 370);

        // B: 別 Canvas2D にコピーして getImageData で確認
        var testCanvas = new Canvas2D(testImg.width, testImg.height);
        testCanvas.drawImage(testImg, 0, 0);
        var td = testCanvas.getImageData(0, 0, testImg.width, testImg.height);
        var nonzero = 0;
        if (td && td.data) {
            for (var ti = 0; ti < td.data.length; ti += 4) {
                if (td.data[ti+3] !== 0) nonzero++;
            }
        }
        c.fillStyle = "white";
        c.fillText("B: canvas copy " + nonzero + "/" + (testImg.width * testImg.height) + " opaque", 100, 310);
        // C: HTMLCanvasElement 経由（RPG Maker MV の Bitmap._createCanvas と同じフロー）
        if (typeof HTMLCanvasElement !== "undefined") {
            var htmlCanvas = new HTMLCanvasElement(testImg.width, testImg.height);
            htmlCanvas.width = testImg.width;
            htmlCanvas.height = testImg.height;
            var htmlCtx = htmlCanvas.getContext("2d");
            htmlCtx.drawImage(testImg, 0, 0);
            var td2 = htmlCtx.getImageData(0, 0, testImg.width, testImg.height);
            var nonzero2 = 0;
            if (td2 && td2.data) {
                for (var ti2 = 0; ti2 < td2.data.length; ti2 += 4) {
                    if (td2.data[ti2+3] !== 0) nonzero2++;
                }
            }
            c.fillStyle = "white";
            c.fillText("C: HTMLCanvas " + nonzero2 + "/" + (testImg.width * testImg.height) + " opaque", 100, 330);

            // data getter テスト（texImage2D で使われる）
            var canvasData = htmlCanvas.data;
            c.fillText("D: canvas.data=" + (canvasData ? "yes(" + canvasData.byteLength + "B)" : "NO"), 100, 350);
        }
    } else {
        c.fillStyle = "#ff4444";
        c.fillText("Image._data is empty!", 10, 290);
    }

    // === セクション3: getImageData / putImageData (Y=440〜) ===
    c.fillStyle = "#222233";
    c.fillRect(0, 430, 500, 200);
    c.font = "14px OpenSans-Bold";
    c.fillStyle = "#ffcc00";
    c.fillText("getImageData / putImageData:", 10, 450);

    c.fillStyle = "#ff6600";
    c.fillRect(10, 460, 80, 50);
    c.fillStyle = "#0066ff";
    c.fillRect(50, 480, 80, 50);

    // getImageData で読み出し
    var imgData = c.getImageData(10, 460, 120, 70);

    if (imgData && imgData.data) {
        // コピー1: そのまま貼り付け
        c.putImageData(imgData, 150, 460);
        c.font = "12px OpenSans-Regular";
        c.fillStyle = "white";
        c.fillText("copy", 150, 545);

        // コピー2: 色反転して貼り付け
        var inverted = c.getImageData(10, 460, 120, 70);
        for (var pi = 0; pi < inverted.data.length; pi += 4) {
            if (inverted.data[pi + 3] > 0) {
                inverted.data[pi]     = 255 - inverted.data[pi];
                inverted.data[pi + 1] = 255 - inverted.data[pi + 1];
                inverted.data[pi + 2] = 255 - inverted.data[pi + 2];
            }
        }
        c.putImageData(inverted, 280, 460);
        c.fillStyle = "white";
        c.fillText("inverted", 280, 545);
    }

    // original ラベル
    c.font = "12px OpenSans-Regular";
    c.fillStyle = "white";
    c.fillText("original", 10, 545);

    c.flush();
}

// ============================================================
// デモ7: Canvas2D dirty rect 差分更新テスト
// ============================================================
// 大きなキャンバスに一度だけ背景を描画し、毎フレーム小さな領域だけ更新する。
// flush() / texture 取得時に dirty 領域だけが GL テクスチャにアップロードされる。

var canvas2d_demo7 = null;
var demo7Inited = false;

function renderDemo7() {
    if (!canvas2d_demo7) {
        canvas2d_demo7 = new Canvas2D(640, 480);
    }
    var c = canvas2d_demo7;

    // 初回のみ: 背景を描画（大きな描画は一度だけ）
    if (!demo7Inited) {
        // 背景グラデーション風
        for (var y = 0; y < 480; y += 4) {
            var r = Math.floor(y * 255 / 480);
            c.fillStyle = "rgba(" + r + ",50," + (255-r) + ",1)";
            c.fillRect(0, y, 640, 4);
        }
        // グリッド線
        c.strokeStyle = "rgba(255,255,255,0.2)";
        c.lineWidth = 1;
        for (var gx = 0; gx < 640; gx += 40) {
            c.beginPath(); c.moveTo(gx, 0); c.lineTo(gx, 480); c.stroke();
        }
        for (var gy = 0; gy < 480; gy += 40) {
            c.beginPath(); c.moveTo(0, gy); c.lineTo(640, gy); c.stroke();
        }
        // タイトル
        c.font = "20px OpenSans-Bold";
        c.fillStyle = "white";
        c.fillText("Demo 7: Dirty Rect Update", 10, 30);
        c.font = "14px OpenSans-Regular";
        c.fillText("Background drawn once, only moving rect updates each frame", 10, 55);

        demo7Inited = true;
    }

    // 毎フレーム: 前フレームの矩形をクリアして新位置に描画
    // 小さな領域だけ更新される → dirty rect で効率的にテクスチャ反映
    var t = time * 0.001;
    var bx = 300 + Math.cos(t) * 200;
    var by = 240 + Math.sin(t * 1.3) * 150;
    var bw = 60, bh = 60;

    // 前回位置のクリア（前フレームの位置を保存）
    if (c._prevX !== undefined) {
        c.clearRect(c._prevX - 2, c._prevY - 2, bw + 4, bh + 4);
        // 背景の再描画（クリアした部分のみ）
        var cx0 = Math.floor(c._prevX - 2);
        var cy0 = Math.floor(c._prevY - 2);
        for (var ry = cy0; ry < cy0 + bh + 4 && ry < 480; ry += 4) {
            if (ry < 0) continue;
            var rr = Math.floor(ry * 255 / 480);
            c.fillStyle = "rgba(" + rr + ",50," + (255-rr) + ",1)";
            c.fillRect(cx0, ry, bw + 4, Math.min(4, cy0 + bh + 4 - ry));
        }
    }

    // 新位置に描画
    c.fillStyle = "rgba(255,255,0,0.9)";
    c.fillRect(bx, by, bw, bh);
    c.strokeStyle = "white";
    c.lineWidth = 2;
    c.strokeRect(bx, by, bw, bh);

    // 位置保存
    c._prevX = bx;
    c._prevY = by;

    // flush は drawCanvas2DAt 内の texture getter で自動実行
    // dirty 領域のみテクスチャに反映される
}

// ============================================================
// デモ8: three.js r128 テスト
// ============================================================

var threeInited = false;
var threeScene = null;
var threeCamera = null;
var threeRenderer = null;
var threeCube = null;

function initDemo8() {
    if (threeInited) return;
    threeInited = true;

    // ポリフィル・シム読み込み（未ロードの場合）
    if (typeof Map === "undefined") loadScript("lib/polyfill.js");
    if (typeof window === "undefined" || !window.HTMLCanvasElement) loadScript("lib/browser_shim.js");

    // three.js ES5 ビルド読み込み
    console.log("Loading three.js...");
    try {
        loadScript("lib/three.es5.js");
        console.log("THREE loaded: r" + THREE.REVISION);
    } catch(e) {
        console.error("three.js load failed: " + e);
        if (e.stack) console.error(e.stack);
        return;
    }

    try {
        // HTMLCanvasElement を作成して WebGL コンテキストを取得
        var canvas = new HTMLCanvasElement(1280, 720);
        canvas.width = 1280;
        canvas.height = 720;

        // gl に canvas を紐付け
        gl.canvas = canvas;

        // WebGLRenderer 作成
        threeRenderer = new THREE.WebGLRenderer({
            canvas: canvas,
            context: gl,
            antialias: false
        });
        threeRenderer.setSize(1280, 720, false);
        threeRenderer.setClearColor(0x222233, 1);
        console.log("THREE.WebGLRenderer created");

        // シーンとカメラ
        threeScene = new THREE.Scene();
        threeCamera = new THREE.PerspectiveCamera(75, 1280 / 720, 0.1, 1000);
        threeCamera.position.z = 5;

        // 立方体
        var geometry = new THREE.BoxGeometry(1, 1, 1);
        var material = new THREE.MeshNormalMaterial();
        threeCube = new THREE.Mesh(geometry, material);
        threeScene.add(threeCube);

        // 追加: ワイヤーフレーム球
        var sphereGeo = new THREE.SphereGeometry(0.5, 16, 12);
        var sphereMat = new THREE.MeshBasicMaterial({ color: 0x00ff88, wireframe: true });
        var sphere = new THREE.Mesh(sphereGeo, sphereMat);
        sphere.position.x = 2;
        threeScene.add(sphere);

        // 追加: 平面
        var planeGeo = new THREE.PlaneGeometry(4, 4);
        var planeMat = new THREE.MeshBasicMaterial({ color: 0x444466, side: THREE.DoubleSide });
        var plane = new THREE.Mesh(planeGeo, planeMat);
        plane.position.y = -1;
        plane.rotation.x = Math.PI / 2;
        threeScene.add(plane);

        console.log("three.js scene created");
    } catch(e) {
        console.error("three.js init error: " + e);
        if (e.stack) console.error(e.stack);
    }
}

function renderDemo8() {
    if (!threeRenderer || !threeScene || !threeCamera) return;

    if (threeCube) {
        threeCube.rotation.x = time * 0.001;
        threeCube.rotation.y = time * 0.0013;
    }

    try {
        threeRenderer.resetState();
        threeRenderer.render(threeScene, threeCamera);
    } catch(e) {
        // 初回エラーのみ表示（スタックトレース付き）
        if (time < 100) {
            console.error("three.js render: " + e);
            if (e.stack) console.error(e.stack);
        }
    }
}

// ============================================================
// 初期化
// ============================================================

initDemo1();
initFullscreenQuad();

canvas2d = new Canvas2D(512, 512);

// テスト用画像読み込み
var testPatternImg = null;
try {
    testPatternImg = createImageBitmap("test_pattern.png");
    console.log("Test pattern loaded: " + testPatternImg.width + "x" + testPatternImg.height);
} catch(e) {
    console.error("Test pattern load failed: " + e);
}

// フォント読み込み
try {
    Canvas2D.loadFont("fonts/OpenSans-Regular.ttf");
    Canvas2D.loadFont("fonts/OpenSans-Bold.ttf");
    Canvas2D.loadFont("fonts/Roboto-Regular.ttf");
    Canvas2D.loadFont("fonts/RobotoMono-VariableFont_wght.ttf");
    console.log("Fonts loaded");
} catch(e) {
    console.error("Font load error: " + e);
}

gl.enable(gl.BLEND);
gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

// localStorage サンプル
var launchCount = parseInt(localStorage.getItem("launchCount")) || 0;
launchCount++;
localStorage.setItem("launchCount", String(launchCount));
console.log("Launch count: " + launchCount);

// Audio
var audioCtx = new AudioContext();

console.log("Demo ready. Press 1-8 to switch demos, Space for beep, R to reset");

// 起動オプションで初期デモモード設定
if (initialDemo > 0) {
    demoMode = initialDemo;
    console.log("Initial demo mode: " + demoMode);
    if (demoMode === 5) initDemo5();
    if (demoMode === 8) initDemo8();
}

// ============================================================
// イベント
// ============================================================

addEventListener("keydown", function(e) {
    keysDown[e.code] = true;

    if (e.code === "Space" && !e.repeat) {
        var beep = audioCtx.createBufferSource("beep.wav");
        beep.volume = 0.5;
        beep.start();
    }

    // デモ切り替え
    if (e.key === "1") { demoMode = 1; console.log("Demo 1: Triangle + Texture"); }
    if (e.key === "2") { demoMode = 2; console.log("Demo 2: Canvas2D Shapes"); }
    if (e.key === "3") { demoMode = 3; console.log("Demo 3: Canvas2D Text"); }
    if (e.key === "4") { demoMode = 4; console.log("Demo 4: Canvas2D Animation"); }
    if (e.key === "5") { demoMode = 5; initDemo5(); console.log("Demo 5: pixi.js"); }
    if (e.key === "6") { demoMode = 6; console.log("Demo 6: drawImage/ImageData"); }
    if (e.key === "7") { demoMode = 7; demo7Inited = false; console.log("Demo 7: Dirty Rect"); }
    if (e.key === "8") { demoMode = 8; initDemo8(); console.log("Demo 8: three.js"); }

    if (e.key === "r" || e.key === "R") {
        offsetX = 0.0; offsetY = 0.0; alpha = 1.0;
    }
});

addEventListener("keyup", function(e) {
    keysDown[e.code] = false;
});

addEventListener("mousedown", function(e) {
    if (e.button === 0) { mouseDown = true; mouseX = e.clientX; mouseY = e.clientY; }
});
addEventListener("mouseup", function(e) {
    if (e.button === 0) { mouseDown = false; }
});
addEventListener("mousemove", function(e) {
    if (mouseDown) {
        offsetX += (e.clientX - mouseX) / 640.0;
        offsetY -= (e.clientY - mouseY) / 360.0;
        mouseX = e.clientX; mouseY = e.clientY;
    }
});
addEventListener("wheel", function(e) {
    alpha += e.deltaY * 0.001;
    if (alpha < 0.05) alpha = 0.05;
    if (alpha > 1.0) alpha = 1.0;
});

// ============================================================
// 毎フレーム
// ============================================================

function update(dt) {
    time += dt;

    if (demoMode === 1) {
        if (keysDown["KeyW"] || keysDown["ArrowUp"])    offsetY += moveSpeed;
        if (keysDown["KeyS"] || keysDown["ArrowDown"])   offsetY -= moveSpeed;
        if (keysDown["KeyA"] || keysDown["ArrowLeft"])   offsetX -= moveSpeed;
        if (keysDown["KeyD"] || keysDown["ArrowRight"])  offsetX += moveSpeed;
        if (offsetX < -1) offsetX = -1; if (offsetX > 1) offsetX = 1;
        if (offsetY < -1) offsetY = -1; if (offsetY > 1) offsetY = 1;
    }
}

function render() {
    gl.clearColor(0.15, 0.15, 0.15, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    if (demoMode === 1) {
        renderDemo1();
    } else if (demoMode === 2) {
        renderDemo2();
        drawCanvas2DAt(canvas2d);
    } else if (demoMode === 3) {
        renderDemo3();
        drawCanvas2DAt(canvas2d);
    } else if (demoMode === 4) {
        renderDemo4();
        drawCanvas2DAt(canvas2d);
    } else if (demoMode === 5) {
        renderDemo5();
    } else if (demoMode === 6) {
        renderDemo6();
        drawCanvas2DAt(canvas2d_demo6);
    } else if (demoMode === 7) {
        renderDemo7();
        drawCanvas2DAt(canvas2d_demo7);
    } else if (demoMode === 8) {
        renderDemo8();
    }
}

// ============================================================
// 終了
// ============================================================

function done() {
    if (vbo) gl.deleteBuffer(vbo);
    if (vao) gl.deleteVertexArray(vao);
    if (program) gl.deleteProgram(program);
    if (texVbo) gl.deleteBuffer(texVbo);
    if (texIbo) gl.deleteBuffer(texIbo);
    if (texVao) gl.deleteVertexArray(texVao);
    if (texProgram) gl.deleteProgram(texProgram);
    console.log("done");
}
