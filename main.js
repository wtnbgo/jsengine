// main.js - サンプルスクリプト

// シェーダソース
var vertexShaderSource =
    "#version 300 es\n" +
    "layout(location = 0) in vec2 aPosition;\n" +
    "layout(location = 1) in vec3 aColor;\n" +
    "out vec3 vColor;\n" +
    "void main() {\n" +
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n" +
    "    vColor = aColor;\n" +
    "}\n";

var fragmentShaderSource =
    "#version 300 es\n" +
    "precision mediump float;\n" +
    "in vec3 vColor;\n" +
    "out vec4 fragColor;\n" +
    "void main() {\n" +
    "    fragColor = vec4(vColor, 1.0);\n" +
    "}\n";

var program = null;
var vao = null;
var vbo = null;

// 初期化
function initGL() {
    // シェーダコンパイル
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

    // プログラムリンク
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

    // 頂点データ: position(xy) + color(rgb)
    var vertices = new Float32Array([
        // x,    y,    r,   g,   b
         0.0,  0.5,  1.0, 0.0, 0.0,  // 上 (赤)
        -0.5, -0.5,  0.0, 1.0, 0.0,  // 左下 (緑)
         0.5, -0.5,  0.0, 0.0, 1.0   // 右下 (青)
    ]);

    vao = gl.createVertexArray();
    gl.bindVertexArray(vao);

    vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.STATIC_DRAW);

    // aPosition (location=0): 2 floats, stride=20, offset=0
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 20, 0);

    // aColor (location=1): 3 floats, stride=20, offset=8
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 20, 8);

    gl.bindVertexArray(null);

    console.log("GL initialized");
    return true;
}

initGL();

// 毎フレーム呼ばれる
function update(dt) {
}

// 毎フレーム描画
function render() {
    gl.clearColor(0.2, 0.2, 0.2, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(program);
    gl.bindVertexArray(vao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindVertexArray(null);
}

// 終了時
function done() {
    if (vbo) gl.deleteBuffer(vbo);
    if (vao) gl.deleteVertexArray(vao);
    if (program) gl.deleteProgram(program);
    console.log("done");
}
