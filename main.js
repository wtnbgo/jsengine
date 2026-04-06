// main.js - サンプルスクリプト（入力イベント動作確認付き）

// シェーダソース（uOffset uniform で三角形を移動）
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

var program = null;
var vao = null;
var vbo = null;
var uOffsetLoc = null;
var uAlphaLoc = null;

// 三角形の位置（キーボード WASD / 矢印キーで移動）
var offsetX = 0.0;
var offsetY = 0.0;
var moveSpeed = 0.02;

// マウス操作用
var mouseX = 0.0;
var mouseY = 0.0;
var mouseDown = false;

// ホイールで透明度を変更
var alpha = 1.0;

// 押下中のキーを追跡
var keysDown = {};

// ============================================================
// GL 初期化
// ============================================================
function initGL() {
    var vs = gl.createShader(gl.VERTEX_SHADER);
    gl.shaderSource(vs, vertexShaderSource);
    gl.compileShader(vs);
    if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) {
        console.error("VS error: " + gl.getShaderInfoLog(vs));
        return false;
    }

    var fs = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(fs, fragmentShaderSource);
    gl.compileShader(fs);
    if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) {
        console.error("FS error: " + gl.getShaderInfoLog(fs));
        return false;
    }

    program = gl.createProgram();
    gl.attachShader(program, vs);
    gl.attachShader(program, fs);
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        console.error("Link error: " + gl.getProgramInfoLog(program));
        return false;
    }
    gl.deleteShader(vs);
    gl.deleteShader(fs);

    uOffsetLoc = gl.getUniformLocation(program, "uOffset");
    uAlphaLoc = gl.getUniformLocation(program, "uAlpha");

    // 頂点データ: position(xy) + color(rgb)
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

    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    console.log("GL initialized");
    return true;
}

initGL();

// ============================================================
// イベントリスナー登録
// ============================================================

// キーボード: WASD / 矢印キーで移動、R でリセット
addEventListener("keydown", function(e) {
    keysDown[e.code] = true;

    if (e.key === "r" || e.key === "R") {
        offsetX = 0.0;
        offsetY = 0.0;
        alpha = 1.0;
        console.log("Reset position and alpha");
    }
});

addEventListener("keyup", function(e) {
    keysDown[e.code] = false;
});

// マウス: ドラッグで三角形を移動
addEventListener("mousedown", function(e) {
    if (e.button === 0) {
        mouseDown = true;
        mouseX = e.clientX;
        mouseY = e.clientY;
    }
});

addEventListener("mouseup", function(e) {
    if (e.button === 0) {
        mouseDown = false;
    }
});

addEventListener("mousemove", function(e) {
    if (mouseDown) {
        var dx = e.clientX - mouseX;
        var dy = e.clientY - mouseY;
        // ピクセル差分を NDC に変換（画面幅 1280, 高さ 720 想定）
        offsetX += dx / 640.0;
        offsetY -= dy / 360.0;
        mouseX = e.clientX;
        mouseY = e.clientY;
    }
});

// ホイール: 透明度を変更
addEventListener("wheel", function(e) {
    alpha += e.deltaY * 0.001;
    if (alpha < 0.05) alpha = 0.05;
    if (alpha > 1.0) alpha = 1.0;
});

// タッチ: タッチ位置に三角形を移動
addEventListener("touchstart", function(e) {
    if (e.touches.length > 0) {
        var t = e.touches[0];
        offsetX = (t.clientX / 640.0) - 1.0;
        offsetY = 1.0 - (t.clientY / 360.0);
    }
});

addEventListener("touchmove", function(e) {
    if (e.touches.length > 0) {
        var t = e.touches[0];
        offsetX = (t.clientX / 640.0) - 1.0;
        offsetY = 1.0 - (t.clientY / 360.0);
    }
});

// ============================================================
// 毎フレーム更新
// ============================================================
function update(dt) {
    // キーボード入力による移動
    if (keysDown["KeyW"] || keysDown["ArrowUp"])    offsetY += moveSpeed;
    if (keysDown["KeyS"] || keysDown["ArrowDown"])   offsetY -= moveSpeed;
    if (keysDown["KeyA"] || keysDown["ArrowLeft"])   offsetX -= moveSpeed;
    if (keysDown["KeyD"] || keysDown["ArrowRight"])  offsetX += moveSpeed;

    // 画面内にクランプ
    if (offsetX < -1.0) offsetX = -1.0;
    if (offsetX >  1.0) offsetX =  1.0;
    if (offsetY < -1.0) offsetY = -1.0;
    if (offsetY >  1.0) offsetY =  1.0;
}

// ============================================================
// 毎フレーム描画
// ============================================================
function render() {
    gl.clearColor(0.2, 0.2, 0.2, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(program);
    gl.uniform2f(uOffsetLoc, offsetX, offsetY);
    gl.uniform1f(uAlphaLoc, alpha);

    gl.bindVertexArray(vao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindVertexArray(null);
}

// ============================================================
// 終了時
// ============================================================
function done() {
    if (vbo) gl.deleteBuffer(vbo);
    if (vao) gl.deleteVertexArray(vao);
    if (program) gl.deleteProgram(program);
    console.log("done");
}
