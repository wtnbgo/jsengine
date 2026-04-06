// main.js - サンプルスクリプト（入力イベント・テクスチャ動作確認付き）

// ============================================================
// シェーダソース: 頂点カラー三角形用
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

// ============================================================
// シェーダソース: テクスチャクワッド用
// ============================================================

var texVertSource =
    "#version 300 es\n" +
    "layout(location = 0) in vec2 aPosition;\n" +
    "layout(location = 1) in vec2 aTexCoord;\n" +
    "uniform vec2 uOffset;\n" +
    "out vec2 vTexCoord;\n" +
    "void main() {\n" +
    "    gl_Position = vec4(aPosition + uOffset, 0.0, 1.0);\n" +
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
// 変数
// ============================================================

var program = null;
var vao = null;
var vbo = null;
var uOffsetLoc = null;
var uAlphaLoc = null;

var texProgram = null;
var texVao = null;
var texVbo = null;
var texIbo = null;
var texTexture = null;
var texOffsetLoc = null;

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
// シェーダコンパイルヘルパー
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

function createProgram(vsSrc, fsSrc) {
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

// ============================================================
// GL 初期化: 頂点カラー三角形
// ============================================================

function initTriangle() {
    program = createProgram(vertexShaderSource, fragmentShaderSource);
    if (!program) return false;

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
    return true;
}

// ============================================================
// GL 初期化: テクスチャクワッド（ImageBitmap 使用）
// ============================================================

function initTexturedQuad() {
    texProgram = createProgram(texVertSource, texFragSource);
    if (!texProgram) return false;

    texOffsetLoc = gl.getUniformLocation(texProgram, "uOffset");

    // sample.png を ImageBitmap として読み込み
    var img = createImageBitmap("sample.png");
    console.log("Loaded image: " + img.width + "x" + img.height);

    // テクスチャ作成
    texTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texTexture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, img.width, img.height, 0,
                  gl.RGBA, gl.UNSIGNED_BYTE, img);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.bindTexture(gl.TEXTURE_2D, null);

    // クワッド頂点: position(xy) + texcoord(uv)
    var quadVerts = new Float32Array([
        // x,    y,    u,   v
        -0.3,  0.3,  0.0, 0.0,   // 左上
         0.3,  0.3,  1.0, 0.0,   // 右上
         0.3, -0.3,  1.0, 1.0,   // 右下
        -0.3, -0.3,  0.0, 1.0    // 左下
    ]);

    var indices = new Uint16Array([0, 1, 2, 0, 2, 3]);

    texVao = gl.createVertexArray();
    gl.bindVertexArray(texVao);

    texVbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, texVbo);
    gl.bufferData(gl.ARRAY_BUFFER, quadVerts, gl.STATIC_DRAW);

    texIbo = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, texIbo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);

    // aPosition (location=0): 2 floats, stride=16, offset=0
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 16, 0);
    // aTexCoord (location=1): 2 floats, stride=16, offset=8
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 16, 8);

    gl.bindVertexArray(null);

    console.log("Textured quad initialized");
    return true;
}

// ============================================================
// 初期化実行
// ============================================================

initTriangle();
initTexturedQuad();

gl.enable(gl.BLEND);
gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

// ============================================================
// localStorage サンプル: 起動回数カウンター
// ============================================================

var launchCount = parseInt(localStorage.getItem("launchCount")) || 0;
launchCount++;
localStorage.setItem("launchCount", String(launchCount));
console.log("Launch count: " + launchCount);

// ============================================================
// Web Audio サンプル: スペースキーでビープ音再生
// ============================================================

var audioCtx = new AudioContext();
console.log("AudioContext state: " + audioCtx.state + ", sampleRate: " + audioCtx.sampleRate);

// ============================================================
// イベントリスナー登録
// ============================================================

addEventListener("keydown", function(e) {
    keysDown[e.code] = true;

    // スペースキーでビープ音再生
    if (e.code === "Space" && !e.repeat) {
        var beep = audioCtx.createBufferSource("beep.wav");
        beep.volume = 0.5;
        beep.start();
        console.log("Beep!");
    }

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
        offsetX += dx / 640.0;
        offsetY -= dy / 360.0;
        mouseX = e.clientX;
        mouseY = e.clientY;
    }
});

addEventListener("wheel", function(e) {
    alpha += e.deltaY * 0.001;
    if (alpha < 0.05) alpha = 0.05;
    if (alpha > 1.0) alpha = 1.0;
});

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
    if (keysDown["KeyW"] || keysDown["ArrowUp"])    offsetY += moveSpeed;
    if (keysDown["KeyS"] || keysDown["ArrowDown"])   offsetY -= moveSpeed;
    if (keysDown["KeyA"] || keysDown["ArrowLeft"])   offsetX -= moveSpeed;
    if (keysDown["KeyD"] || keysDown["ArrowRight"])  offsetX += moveSpeed;

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

    // テクスチャクワッド描画（右寄り）
    gl.useProgram(texProgram);
    gl.uniform2f(texOffsetLoc, offsetX + 0.5, offsetY);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texTexture);
    gl.bindVertexArray(texVao);
    gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);

    // 頂点カラー三角形描画（左寄り）
    gl.useProgram(program);
    gl.uniform2f(uOffsetLoc, offsetX - 0.5, offsetY);
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
    if (texVbo) gl.deleteBuffer(texVbo);
    if (texIbo) gl.deleteBuffer(texIbo);
    if (texVao) gl.deleteVertexArray(texVao);
    if (texTexture) gl.deleteTexture(texTexture);
    if (texProgram) gl.deleteProgram(texProgram);
    console.log("done");
}
