// ============================================================
// vrmkit/overlay.js — Canvas2D → GL 合成オーバーレイ
// ============================================================
//
// Canvas2D (ThorVG) の内容を 3D 描画の上にアルファ合成する。
// three.js の描画後に draw() を呼ぶだけ。 GL state は毎フレーム
// renderer.resetState() されるので保存 / 復元は不要 (ただし
// three を使わないローディング画面でも使えるよう非破壊を心掛ける)。
//
// Canvas2D のテクスチャは premultiplied alpha なので
// blendFunc(ONE, ONE_MINUS_SRC_ALPHA) で合成する。

let _prog = null;
let _vao = null;
let _vbo = null;

function _ensureGL() {
    if (_prog) return true;
    const vsSrc =
        "#version 300 es\n" +
        "layout(location = 0) in vec2 aPosition;\n" +
        "layout(location = 1) in vec2 aTexCoord;\n" +
        "out vec2 vTexCoord;\n" +
        "void main() { gl_Position = vec4(aPosition, 0.0, 1.0); vTexCoord = aTexCoord; }\n";
    const fsSrc =
        "#version 300 es\n" +
        "precision mediump float;\n" +
        "in vec2 vTexCoord;\n" +
        "uniform sampler2D uTexture;\n" +
        "out vec4 fragColor;\n" +
        "void main() { fragColor = texture(uTexture, vTexCoord); }\n";
    function compile(type, src) {
        const s = gl.createShader(type);
        gl.shaderSource(s, src);
        gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
            console.error("overlay shader error: " + gl.getShaderInfoLog(s));
            return null;
        }
        return s;
    }
    const vs = compile(gl.VERTEX_SHADER, vsSrc);
    const fs = compile(gl.FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return false;
    _prog = gl.createProgram();
    gl.attachShader(_prog, vs);
    gl.attachShader(_prog, fs);
    gl.linkProgram(_prog);
    if (!gl.getProgramParameter(_prog, gl.LINK_STATUS)) {
        console.error("overlay link error: " + gl.getProgramInfoLog(_prog));
        _prog = null;
        return false;
    }
    gl.deleteShader(vs);
    gl.deleteShader(fs);

    _vao = gl.createVertexArray();
    gl.bindVertexArray(_vao);
    _vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, _vbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(16), gl.DYNAMIC_DRAW);
    const ibo = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 1, 2, 0, 2, 3]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 16, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 16, 8);
    gl.bindVertexArray(null);
    return true;
}

export class CanvasOverlay {
    // w × h の Canvas2D を作る。 screenW/H は合成先のスクリーンサイズ。
    constructor(w, h, screenW, screenH) {
        this.canvas = new Canvas2D(w, h);
        this.width = w;
        this.height = h;
        this.screenW = screenW || 1280;
        this.screenH = screenH || 720;
    }

    // this.canvas に描画したあと canvas.flush() を呼んでから draw() すること
    // (x, y) = スクリーン上のピクセル座標 (左上原点)
    draw(x, y) {
        if (!_ensureGL()) return;
        x = x || 0;
        y = y || 0;
        const x0 = -1.0 + (x / this.screenW) * 2.0;
        const y0 = 1.0 - (y / this.screenH) * 2.0;
        const x1 = -1.0 + ((x + this.width) / this.screenW) * 2.0;
        const y1 = 1.0 - ((y + this.height) / this.screenH) * 2.0;
        const verts = new Float32Array([
            x0, y0, 0, 0,
            x1, y0, 1, 0,
            x1, y1, 1, 1,
            x0, y1, 0, 1,
        ]);
        gl.useProgram(_prog);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.canvas.texture);
        gl.bindVertexArray(_vao);
        gl.bindBuffer(gl.ARRAY_BUFFER, _vbo);
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, verts);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.disable(gl.SCISSOR_TEST);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
        gl.bindVertexArray(null);
    }
}
