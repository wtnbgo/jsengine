// main.js - デモスクリプト
// 数字キー 1～4 でデモ切り替え
//   1: 頂点カラー三角形 + テクスチャクワッド（WASD移動、ホイール透明度）
//   2: Canvas2D 図形描画（矩形、円、パス、グラデーション風）
//   3: Canvas2D テキスト描画（要フォントファイル）
//   4: Canvas2D アニメーション（回転する図形）
// Space: ビープ音再生  R: リセット

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

// Canvas2D テクスチャを全画面に描画
function drawCanvas2DFullscreen(c2d) {
    gl.useProgram(texProgram);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, c2d.texture);
    gl.bindVertexArray(texVao);
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
// 初期化
// ============================================================

initDemo1();
initFullscreenQuad();

canvas2d = new Canvas2D(512, 512);

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

console.log("Demo ready. Press 1-4 to switch demos, Space for beep, R to reset");

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
        drawCanvas2DFullscreen(canvas2d);
    } else if (demoMode === 3) {
        renderDemo3();
        drawCanvas2DFullscreen(canvas2d);
    } else if (demoMode === 4) {
        renderDemo4();
        drawCanvas2DFullscreen(canvas2d);
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
