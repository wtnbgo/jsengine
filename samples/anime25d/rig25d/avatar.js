// ============================================================
// rig25d/avatar.js — 表示機能部 (Anime2.5DRig index.html のランタイム移植)
// ============================================================
//
// Rigger.buildRig() が返す rig 定義を受け取り、メッシュワープ + ステンシル
// (WebGL1 相当の API、jsengine の WebGL2/GLES3 上で動作) で描画する。
// 元実装との差分:
//   - rAF 駆動 → jsengine の update(dtMs) / render() ライフサイクル
//   - キャンバス全面描画 → render(x, y, scale) で任意位置に合成
//   - マイク口パク / ウェブカメラ追従は機能カット (マウス追従は残置)
//   - UI (DOM) は分離 (ui_panel.js 参照)
//
// 使い方:
//   const av = new Avatar25D();
//   av.setRig(rig);                     // Rig25DLoader 等で作った rig 定義
//   av.T.mouthOpen = 1;                 // パラメータは T (target) に書く
//   av.auto.blink = false;              // 自動動作トグル
//   毎フレーム: av.update(dtMs); av.render(x, y, scale);
//
// MIT License (original: https://github.com/852wa/Anime2.5DRig)
"use strict";

(function () {

    function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }
    function smooth(t) { t = clamp(t, 0, 1); return t * t * (3 - 2 * t); }

    // パラメータ初期値 (index.html の P と同一)
    var PARAM_DEFAULTS = {
        angleX: 0, angleY: 0, angleZ: 0, eyeOpenL: 1, eyeOpenR: 1, eyeX: 0, eyeY: 0, brow: 0,
        mouthOpen: 0, mouthForm: 0, mouthCY: 0, body: 0, physAmp: 2, soft: 2,
        browAngL: 0, browAngR: 0, browAngSym: 0, bangL: 0, bangC: 0, bangR: 0,
        armY: 0, armPos: 0, bust: 2.5, bustY: 1, irisScale: 1, mouthEase: 0.45, eyeEase: 0.3,
        fhAmp: 2, fhSoft: 0.4, eyeCY: 0, eyeCAng: 0, mouthCAng: 0, eyeScaleL: 1, eyeScaleR: 1, mouthScale: 1
    };

    var PRESETS = {
        neutral:  { eyeOpenL: 1, eyeOpenR: 1, brow: 0, mouthOpen: 0, mouthForm: 0, irisScale: 1 },
        smile:    { eyeOpenL: 0, eyeOpenR: 0, brow: 0.45, mouthOpen: 0, mouthForm: 0.9, irisScale: 1 },
        usume:    { eyeOpenL: 0.5, eyeOpenR: 0.5, brow: 0.35, mouthOpen: 1, mouthForm: 0.8, irisScale: 1 },
        surprise: { eyeOpenL: 1, eyeOpenR: 1, brow: 1, mouthOpen: 0.75, mouthForm: -0.1, irisScale: 0.7 },
        jito:     { eyeOpenL: 0.4, eyeOpenR: 0.4, brow: -0.6, mouthOpen: 0, mouthForm: -0.4, irisScale: 1 },
        winkL:    { eyeOpenL: 0, eyeOpenR: 1, brow: 0.2, mouthOpen: 0.4, mouthForm: 0.7, irisScale: 1 },
        winkR:    { eyeOpenL: 1, eyeOpenR: 0, brow: 0.2, mouthOpen: 0.4, mouthForm: 0.7, irisScale: 1 }
    };

    // ---------- 共有 GL リソース ----------
    var _prog = null, _locPos, _locUV, _locScreen, _locOfs, _locScale, _locCut, _locAl;

    function ensureProgram() {
        if (_prog) return true;
        function sh(type, src) {
            var s = gl.createShader(type);
            gl.shaderSource(s, src); gl.compileShader(s);
            if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
                console.error("rig25d shader error: " + gl.getShaderInfoLog(s));
                return null;
            }
            return s;
        }
        // GLSL ES 1.00 (GLES3 でもそのままコンパイル可)。 aPos はモデルの
        // ピクセル座標のまま流し、 uOfs/uScale でスクリーン位置へ変換する。
        var vs = sh(gl.VERTEX_SHADER,
            "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV;" +
            "uniform vec2 uScreen; uniform vec2 uOfs; uniform float uScale;" +
            "void main(){ vUV=aUV; vec2 p=(aPos*uScale+uOfs)/uScreen*2.0-1.0;" +
            " gl_Position=vec4(p.x,-p.y,0.0,1.0); }");
        var fs = sh(gl.FRAGMENT_SHADER,
            "precision mediump float; varying vec2 vUV; uniform sampler2D uTex;" +
            "uniform float uCut; uniform float uAlpha;" +
            "void main(){ vec4 c=texture2D(uTex,vUV); if(c.a<uCut) discard; gl_FragColor=c*uAlpha; }");
        if (!vs || !fs) return false;
        _prog = gl.createProgram();
        gl.attachShader(_prog, vs); gl.attachShader(_prog, fs);
        gl.linkProgram(_prog);
        if (!gl.getProgramParameter(_prog, gl.LINK_STATUS)) {
            console.error("rig25d link error: " + gl.getProgramInfoLog(_prog));
            _prog = null;
            return false;
        }
        _locPos = gl.getAttribLocation(_prog, "aPos");
        _locUV = gl.getAttribLocation(_prog, "aUV");
        _locScreen = gl.getUniformLocation(_prog, "uScreen");
        _locOfs = gl.getUniformLocation(_prog, "uOfs");
        _locScale = gl.getUniformLocation(_prog, "uScale");
        _locCut = gl.getUniformLocation(_prog, "uCut");
        _locAl = gl.getUniformLocation(_prog, "uAlpha");
        return true;
    }

    function mkTex(img) {
        var t = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, t);
        // {width,height,data} を straight alpha のままアップロードすると
        // premultiplied blend (ONE, ONE_MINUS_SRC_ALPHA) と不整合になるので
        // UNPACK_PREMULTIPLY_ALPHA_WEBGL で C++ 側に premultiply させる
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return t;
    }

    // ============================================================
    // Avatar25D
    // ============================================================
    function Avatar25D() {
        this.layers = [];
        this.A = null;               // anchors
        this.CW = 768; this.CH = 768;
        this.FS = 1;                 // faceScale
        this.NP = null; this.BP = null; this.FC = null; this.CHEST = null;
        this.bounce = { x: 0, v: 0, dy: 0 };

        this.T = Object.assign({}, PARAM_DEFAULTS);      // UI が書く目標値
        this.cur = Object.assign({}, PARAM_DEFAULTS);    // 平滑化後の現在値
        this.auto = { idle: true, blink: true, rand: true, talk: true, mouse: false, phys: true };
        this.mouse = { x: 0, y: 0, in: false };          // -1..1 正規化。auto.mouse=true で追従

        // アニメーション内部状態 (時刻は update の積算、単位 ms)
        this.t = 0;
        this._blinkT = -1; this._nextBlink = 1800;
        this._rnd = { ax: 0, ay: 0, az: 0, bd: 0, ex: 0, ey: 0 }; this._nextRnd = 0;
        this._talkOn = false; this._talkV = 0; this._talkTgt = 0;
        this._nextTalkState = 0; this._nextSyl = 0;
        this.warnings = [];
    }

    Avatar25D.PARAM_DEFAULTS = PARAM_DEFAULTS;
    Avatar25D.PRESETS = PRESETS;

    Avatar25D.prototype.applyPreset = function (name) {
        var p = PRESETS[name];
        if (!p) return;
        for (var k in p) this.T[k] = p[k];
    };

    Avatar25D.prototype.resetParams = function () {
        Object.assign(this.T, PARAM_DEFAULTS);
    };

    Avatar25D.prototype.dispose = function () {
        for (var i = 0; i < this.layers.length; i++) {
            var L = this.layers[i];
            gl.deleteTexture(L.tex); gl.deleteBuffer(L.vboPos);
            gl.deleteBuffer(L.vboUV); gl.deleteBuffer(L.ibo);
        }
        this.layers = [];
    };

    // レイヤー描画順の入替 (テスト UI 用)
    Avatar25D.prototype.moveLayer = function (i, dir) {
        var j = i + dir;
        if (i < 0 || i >= this.layers.length || j < 0 || j >= this.layers.length) return false;
        var tmp = this.layers[i]; this.layers[i] = this.layers[j]; this.layers[j] = tmp;
        return true;
    };

    // ---------- rig 適用 (index.html applyRig 移植) ----------
    Avatar25D.prototype.setRig = function (rig) {
        this.dispose();
        var A = this.A = rig.anchors;
        this.CW = rig.canvas.w; this.CH = rig.canvas.h;
        this.FS = A.faceScale;
        this.NP = A.neckPivot; this.BP = A.bodyPivot;
        this.FC = { x: A.face.cx, y: A.face.cy };
        this.CHEST = {
            cx: this.NP.cx, cy: A.neckBottom + (A.face.y1 - A.face.y0) * 0.60,
            rx: (A.face.x1 - A.face.x0) * 0.60, ry: (A.face.y1 - A.face.y0) * 0.45
        };
        this.warnings = rig.warnings || [];
        var CW = this.CW;

        for (var li = 0; li < rig.layers.length; li++) {
            var Lr = rig.layers[li];
            var L = Object.assign({}, Lr);
            var cell = (L.phys ? 30 : 42) * Math.max(0.6, CW / 768);
            var nx = Math.max(2, Math.round(L.w / cell)), ny = Math.max(2, Math.round(L.h / cell));
            var nv = (nx + 1) * (ny + 1);
            var base = new Float32Array(nv * 2), uv = new Float32Array(nv * 2);
            var k = 0, i, j;
            for (j = 0; j <= ny; j++) for (i = 0; i <= nx; i++) {
                base[k] = L.x + L.w * i / nx; base[k + 1] = L.y + L.h * j / ny;
                uv[k] = i / nx; uv[k + 1] = j / ny; k += 2;
            }
            var idx = [];
            for (j = 0; j < ny; j++) for (i = 0; i < nx; i++) {
                var a = j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
                idx.push(a, b, c, b, d, c);
            }
            L.base = base; L.cur = new Float32Array(base); L.nIdx = idx.length;
            L.bn = Rigger.baseName(L.name.replace(/_(l|r)$/, ""));

            if (L.strands && L.strands.length) {
                var S = L.strands, nS = S.length;
                var spacing = 120;
                if (nS > 1) {
                    var ds = [];
                    for (var s = 1; s < nS; s++) ds.push(S[s].x - S[s - 1].x);
                    ds.sort(function (a, b) { return a - b; });
                    spacing = ds[ds.length >> 1];
                }
                var sig = spacing * 0.6;
                L.sw = new Float32Array(nv * nS); L.su = new Float32Array(nv);
                L.spr = S.map(function (s, si) {
                    return { stiff: { x: 0, v: 0, dx: 0 }, soft: { x: 0, v: 0, dx: 0 }, phase: si * 1.37 + L.z };
                });
                for (var v = 0; v < nv; v++) {
                    var x = base[v * 2], y = base[v * 2 + 1];
                    var tot = 0, ss;
                    for (ss = 0; ss < nS; ss++) {
                        var w = Math.exp(-Math.pow((x - S[ss].x) / sig, 2));
                        L.sw[v * nS + ss] = w; tot += w;
                    }
                    var rY = 0, tY = 0;
                    if (tot > 1e-6) {
                        for (ss = 0; ss < nS; ss++) {
                            L.sw[v * nS + ss] /= tot;
                            rY += L.sw[v * nS + ss] * S[ss].rootY;
                            tY += L.sw[v * nS + ss] * S[ss].tipY;
                        }
                    } else { L.sw[v * nS + 0] = 1; rY = S[0].rootY; tY = S[0].tipY; }
                    L.su[v] = Math.min(1, Math.max(0, (y - rY) / Math.max(1, tY - rY)));
                }
                if (L.bn === "front hair") {
                    var fw = A.face.x1 - A.face.x0, fcx = A.face.cx;
                    var f = 36, b1 = fcx - fw * 0.22, b2 = fcx + fw * 0.22;
                    L.bw = new Float32Array(nv * 3);
                    for (var v2 = 0; v2 < nv; v2++) {
                        var xx = base[v2 * 2];
                        var s1 = smooth((xx - b1) / f + 0.5), s2 = smooth((xx - b2) / f + 0.5);
                        L.bw[v2 * 3] = 1 - s1; L.bw[v2 * 3 + 1] = s1 * (1 - s2); L.bw[v2 * 3 + 2] = s2;
                    }
                }
            }

            L.vboPos = gl.createBuffer(); L.vboUV = gl.createBuffer(); L.ibo = gl.createBuffer();
            gl.bindBuffer(gl.ARRAY_BUFFER, L.vboUV);
            gl.bufferData(gl.ARRAY_BUFFER, uv, gl.STATIC_DRAW);
            gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, L.ibo);
            gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idx), gl.STATIC_DRAW);
            L.tex = mkTex(L.img);
            delete L.img;
            this.layers.push(L);
        }
    };

    // ---------- 差分クロスフェード (index.html fadeAlpha 移植) ----------
    Avatar25D.prototype._fadeAlpha = function (L, e) {
        if (!L.fade) return 1;
        if (L.fade === "eyeOpen") {
            var v = L.side === "L" ? e.eyeOpenL : e.eyeOpenR;
            return smooth((v - (0.10 + e.eyeEase * 0.45)) / 0.15);
        }
        if (L.fade === "eyeClose") {
            var v2 = L.side === "L" ? e.eyeOpenL : e.eyeOpenR;
            return 1 - smooth((v2 - (0.10 + e.eyeEase * 0.45)) / 0.15);
        }
        if (L.fade === "mouthOpen") return smooth((e.mouthOpen - (0.05 + e.mouthEase * 0.35)) / 0.12);
        if (L.fade === "mouthClose") return 1 - smooth((e.mouthOpen - (0.05 + e.mouthEase * 0.35)) / 0.12);
        return 1;
    };

    // ---------- 頂点変形 (index.html deform 移植) ----------
    Avatar25D.prototype._deform = function (L, e) {
        var A = this.A, NP = this.NP, BP = this.BP, FC = this.FC, CHEST = this.CHEST, FS = this.FS;
        var bounce = this.bounce;
        var b = L.base, o = L.cur, n = b.length;
        var isHead = L.group === "head";
        var az = e.angleZ * 0.07, cz = Math.cos(az), sz = Math.sin(az);
        var ab = e.body * 0.028, cb = Math.cos(ab), sb = Math.sin(ab);
        var bn = L.bn;
        var eyeSide = L.side, EA = eyeSide === "L" ? A.eyeL : (eyeSide === "R" ? A.eyeR : null);
        var vOpen = eyeSide === "L" ? e.eyeOpenL : e.eyeOpenR;
        var mo = e.mouthOpen;
        var mHalfW = (A.mouth.x1 - A.mouth.x0) / 2;
        var nS = L.strands ? L.strands.length : 0;
        var bcx = L.x + L.w / 2, bcy = L.y + L.h / 2;
        var isFH = (bn === "front hair");
        for (var k = 0; k < n; k += 2) {
            var x = b[k], y = b[k + 1];
            var vi = k >> 1;
            // --- closed-eye / mouth scale ---
            if (EA && bn === "eye_close") {
                var sE = eyeSide === "L" ? e.eyeScaleL : e.eyeScaleR;
                if (sE !== 1) {
                    var cxE = (EA.x0 + EA.x1) / 2, cyE = (EA.y0 + EA.y1) / 2;
                    x = cxE + (x - cxE) * sE; y = cyE + (y - cyE) * sE;
                }
            }
            if (bn === "mouth_open" || bn === "mouth_close") {
                var sM = e.mouthScale;
                if (sM !== 1) { x = A.mouth.cx + (x - A.mouth.cx) * sM; y = A.mouth.cy + (y - A.mouth.cy) * sM; }
            }
            // --- local features ---
            if (L.fade === "eyeOpen" && EA) {
                if (bn === "irides") {
                    var isc = e.irisScale;
                    x = EA.icx + (x - EA.icx) * isc; y = EA.icy + (y - EA.icy) * isc;
                    x += e.eyeX * 11 * FS; y += e.eyeY * 6 * FS;
                    var tl = smooth((0.32 - vOpen) / 0.32);   // iris stays round until nearly closed
                    y = EA.closeY + (y - EA.closeY) * (1 - 0.80 * tl);
                } else {
                    y = EA.closeY + (y - EA.closeY) * (1 - 0.85 * (1 - vOpen));   // lid compression
                }
            }
            if (L.fade === "eyeClose" && EA) {
                y -= vOpen * 3;
                y += e.eyeCY * 14 * FS;
                var thE = e.eyeCAng * 0.3 * (eyeSide === "L" ? 1 : -1);
                if (thE) {
                    var ct = Math.cos(thE), st = Math.sin(thE), rx = x - bcx, ry = y - bcy;
                    x = bcx + rx * ct - ry * st; y = bcy + rx * st + ry * ct;
                }
            }
            if (bn === "eyebrow") {
                y += (-e.brow * 9 + (1 - vOpen) * 3.5) * FS;
                var th = (eyeSide === "L" ? (e.browAngL + e.browAngSym) : (e.browAngR - e.browAngSym)) * 0.30;
                if (th) {
                    var ct2 = Math.cos(th), st2 = Math.sin(th), rx2 = x - bcx, ry2 = y - bcy;
                    x = bcx + rx2 * ct2 - ry2 * st2; y = bcy + rx2 * st2 + ry2 * ct2;
                }
            }
            if (L.fade === "mouthOpen") {
                y = A.mouth.y0 + (y - A.mouth.y0) * (0.5 + 0.5 * mo);
                var q = Math.pow(Math.abs(x - A.mouth.cx) / (mHalfW + 4), 1.5);
                y -= e.mouthForm * 6 * FS * (q - 0.35);
            }
            if (L.fade === "mouthClose") {
                y += e.mouthCY * 14 * FS;   // 形（笑い）は閉じ口には適用しない
                var thM = e.mouthCAng * 0.35;
                if (thM) {
                    var ct3 = Math.cos(thM), st3 = Math.sin(thM), rx3 = x - A.mouth.cx, ry3 = y - A.mouth.cy;
                    x = A.mouth.cx + rx3 * ct3 - ry3 * st3; y = A.mouth.cy + rx3 * st3 + ry3 * ct3;
                }
            }
            if (bn === "face" && y > A.mouth.cy) {
                y += mo * 6 * FS * smooth((y - A.mouth.cy) / (A.face.y1 - A.mouth.cy));
            }
            // --- head transform ---
            var hw = isHead ? 1 : (L.group === "body" ? 0.16 : 0);   // body subtly follows head XYZ
            if (bn === "neck") hw = 0.55 * smooth((A.neckBottom - y) / Math.max(1, A.neckBottom - A.neckTop));
            if (hw > 0) {
                var rxh = x - NP.cx, ryh = y - NP.cy;
                var rxh2 = rxh * cz - ryh * sz, ryh2 = rxh * sz + ryh * cz;
                x += (rxh2 - rxh) * hw; y += (ryh2 - ryh) * hw;
                var dd = L.depth;
                x += hw * FS * (e.angleX * (14 + 40 * (dd - 1)) + e.angleX * (NP.cy - y) * 0.028);
                y += hw * FS * (-e.angleY * (9 + 30 * (dd - 1)) - e.angleY * (dd - 1) * (y - FC.y) * 0.05);
            }
            // --- breathing ---
            y -= (L.group === "body" ? e.breath * 2.0 : e.breathHead * 1.6) * FS;
            if (bn === "topwear" && y < CHEST.cy) y -= e.breath * 2.2 * FS * smooth((CHEST.cy - y) / (CHEST.ry * 2));   // shoulders rise
            if (bn === "topwear") x = NP.cx + (x - NP.cx) * (1 + e.breath * 0.003);
            // --- bust jiggle ---
            if (bn === "topwear") {
                var gx = (x - CHEST.cx) / CHEST.rx, gy = (y - (CHEST.cy + e.bustY * 70 * FS)) / CHEST.ry;
                y += bounce.dy * e.bust * Math.exp(-gx * gx - gy * gy);
            }
            // --- arms ---
            if (bn === "handwear") {
                var wA = smooth((y - L.y) / L.h * 1.15);
                y -= e.armY * 30 * FS * wA;
                y += e.armPos * 40 * FS;
                x += e.armY * 6 * FS * wA * (x < NP.cx ? 1 : -1);
            }
            // --- bang blocks ---
            if (L.bw && L.su) {
                var m = Math.pow(L.su[vi], 1.4) * 22 * FS;
                x += (e.bangL * L.bw[vi * 3] + e.bangC * L.bw[vi * 3 + 1] + e.bangR * L.bw[vi * 3 + 2]) * m;
            }
            // --- hair strand physics (stiff top, fluffy bottom; front hair has own params) ---
            if (nS && this.auto.phys) {
                var u = isFH ? Math.min(1, L.su[vi] * 1.6) : L.su[vi];
                var amp = Math.pow(u, isFH ? 1.8 : 2.1) * (isFH ? e.fhAmp : e.physAmp);
                var softMix = Math.pow(u, 1.2) * (isFH ? e.fhSoft : e.soft);
                var dx = 0;
                for (var s2 = 0; s2 < nS; s2++) {
                    var w2 = L.sw[vi * nS + s2]; if (w2 < 0.001) continue;
                    var sp = L.spr[s2];
                    dx += w2 * (sp.stiff.dx * (1 - softMix) + sp.soft.dx * softMix);
                }
                x += dx * amp; y += Math.abs(dx) * amp * 0.12;
            }
            o[k] = x; o[k + 1] = y;
        }
        // --- body rotation (around bottom center) ---
        if (Math.abs(ab) > 1e-4) {
            for (var k2 = 0; k2 < n; k2 += 2) {
                var rxb = o[k2] - BP.cx, ryb = o[k2 + 1] - BP.cy;
                o[k2] = BP.cx + rxb * cb - ryb * sb; o[k2 + 1] = BP.cy + rxb * sb + ryb * cb;
            }
        }
    };

    // ---------- 毎フレーム更新 (index.html tick の状態更新部の移植) ----------
    Avatar25D.prototype.update = function (dtMs) {
        if (!this.layers.length || !this.A) return;
        var dt = Math.min(0.05, dtMs / 1000);
        this.t += dtMs;
        var now = this.t, t = now / 1000;
        var auto = this.auto;
        var tgt = Object.assign({}, this.T);

        if (auto.mouse && this.mouse.in) {
            tgt.angleX = clamp(this.mouse.x * 0.9, -1, 1); tgt.angleY = clamp(-this.mouse.y * 0.7, -1, 1);
            tgt.eyeX = clamp(this.mouse.x * 1.2, -1, 1); tgt.eyeY = clamp(-this.mouse.y * 0.8, -1, 1);
        }
        if (auto.idle) {
            tgt.angleX += 0.13 * Math.sin(t * 0.42) + 0.05 * Math.sin(t * 1.13);
            tgt.angleY += 0.08 * Math.sin(t * 0.31 + 1.7);
            tgt.angleZ += 0.07 * Math.sin(t * 0.23 + 0.5);
            tgt.body += 0.10 * Math.sin(t * 0.19 + 2.1);
        }
        if (auto.rand) {
            if (now > this._nextRnd) {
                this._nextRnd = now + 1400 + Math.random() * 2600;
                var r = this._rnd;
                r.ax = (Math.random() * 2 - 1) * 0.55; r.ay = (Math.random() * 2 - 1) * 0.40;
                r.az = (Math.random() * 2 - 1) * 0.35; r.bd = (Math.random() * 2 - 1) * 0.30;
                r.ex = (Math.random() * 2 - 1) * 0.60; r.ey = (Math.random() * 2 - 1) * 0.35;
            }
            var rr = this._rnd;
            tgt.angleX = clamp(tgt.angleX + rr.ax, -1, 1); tgt.angleY = clamp(tgt.angleY + rr.ay, -1, 1);
            tgt.angleZ = clamp(tgt.angleZ + rr.az, -1, 1); tgt.body = clamp(tgt.body + rr.bd, -1, 1);
            tgt.eyeX = clamp(tgt.eyeX + rr.ex, -1, 1); tgt.eyeY = clamp(tgt.eyeY + rr.ey, -1, 1);
        }
        if (auto.talk) {
            if (now > this._nextTalkState) {
                this._talkOn = !this._talkOn;
                this._nextTalkState = now + (this._talkOn ? 1200 + Math.random() * 2200 : 600 + Math.random() * 1800);
            }
            if (this._talkOn && now > this._nextSyl) {
                this._nextSyl = now + 70 + Math.random() * 110;
                this._talkTgt = Math.random() < 0.25 ? 0.04 : 0.25 + Math.random() * 0.75;
            }
            if (!this._talkOn) this._talkTgt = 0;
            this._talkV += (this._talkTgt - this._talkV) * Math.min(1, dt * 22);
            tgt.mouthOpen = Math.max(tgt.mouthOpen, this._talkV);
        }
        if (auto.blink) {
            if (this._blinkT < 0 && now > this._nextBlink) {
                this._blinkT = 0;
                this._nextBlink = now + 1600 + Math.random() * 3800;
                if (Math.random() < 0.18) this._nextBlink = now + 280;
            }
            if (this._blinkT >= 0) {
                this._blinkT += dt;
                var d = this._blinkT, v;
                if (d < 0.08) v = 1 - d / 0.08;
                else if (d < 0.42) v = 0;
                else if (d < 0.58) v = (d - 0.42) / 0.16;
                else { v = 1; this._blinkT = -1; }
                tgt.eyeOpenL = Math.min(tgt.eyeOpenL, v); tgt.eyeOpenR = Math.min(tgt.eyeOpenR, v);
            }
        }
        var cur = this.cur;
        for (var k in cur) cur[k] += (tgt[k] - cur[k]) * Math.min(1, dt * 14);
        var e = this._e = Object.assign({}, cur);
        e.breath = 0.5 + 0.5 * Math.sin(t * 2 * Math.PI / 3.4);
        e.breathHead = 0.5 + 0.5 * Math.sin(t * 2 * Math.PI / 3.4 - 0.6);   // head follows chest with a lag

        // strand springs
        var FS = this.FS, NP = this.NP, FC = this.FC;
        var headDX = (e.angleX * 14 + e.angleZ * 0.07 * (NP.cy - FC.y)) * FS;
        for (var li = 0; li < this.layers.length; li++) {
            var L = this.layers[li];
            if (!L.spr) continue;
            for (var si = 0; si < L.spr.length; si++) {
                var sp = L.spr[si];
                var wind = auto.idle ? (1.8 * Math.sin(t * 0.8 + sp.phase) + 1.0 * Math.sin(t * 1.9 + sp.phase * 2.3)) : 0;
                var txv = headDX + wind * FS;
                var kk = 70, cc = 9;
                var axv = -kk * (sp.stiff.x - txv) - cc * sp.stiff.v;
                sp.stiff.v += axv * dt; sp.stiff.x += sp.stiff.v * dt;
                sp.stiff.dx = -(sp.stiff.x - txv) * 2.2;
                kk = 16; cc = 1.3;
                axv = -kk * (sp.soft.x - txv) - cc * sp.soft.v;
                sp.soft.v += axv * dt; sp.soft.x += sp.soft.v * dt;
                sp.soft.dx = -(sp.soft.x - txv) * 3.0;
            }
        }
        // bust bounce
        var bounce = this.bounce;
        var bustTgt = (e.breath * 3.0 - e.angleY * 6.0 + e.body * 4.0) * FS;
        var kb = 140, cb2 = 4.2;
        var aa = -kb * (bounce.x - bustTgt) - cb2 * bounce.v;
        bounce.v += aa * dt; bounce.x += bounce.v * dt;
        bounce.dy = -(bounce.x - bustTgt) * 3.0;
    };

    // ---------- 描画 (index.html tick の描画部の移植) ----------
    // (x, y) = スクリーン左上のピクセル座標、 scale = 表示倍率。
    // screenW/H = 現在のウィンドウ (バックバッファ) サイズ。
    Avatar25D.prototype.render = function (x, y, scale, screenW, screenH) {
        if (!this.layers.length || !this.A || !this._e) return;
        if (!ensureProgram()) return;
        var e = this._e;

        gl.useProgram(_prog);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.disable(gl.SCISSOR_TEST);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        gl.activeTexture(gl.TEXTURE0);
        // 共有バックバッファなので color はクリアせず stencil のみクリア
        gl.clearStencil(0);
        gl.clear(gl.STENCIL_BUFFER_BIT);
        gl.uniform2f(_locScreen, screenW, screenH);
        gl.uniform2f(_locOfs, x, y);
        gl.uniform1f(_locScale, scale);
        gl.uniform1f(_locCut, 0.0);
        gl.enableVertexAttribArray(_locPos);
        gl.enableVertexAttribArray(_locUV);

        for (var li = 0; li < this.layers.length; li++) {
            var L = this.layers[li];
            var fa = this._fadeAlpha(L, e);
            // eyewhite は fade=0 でもステンシル書込のために必ず描く
            if (fa < 0.004 && !(L.fade === "eyeOpen" && L.name.indexOf("eyewhite") === 0)) continue;
            this._deform(L, e);
            gl.uniform1f(_locAl, fa);
            gl.bindBuffer(gl.ARRAY_BUFFER, L.vboPos);
            gl.bufferData(gl.ARRAY_BUFFER, L.cur, gl.DYNAMIC_DRAW);
            gl.vertexAttribPointer(_locPos, 2, gl.FLOAT, false, 0, 0);
            gl.bindBuffer(gl.ARRAY_BUFFER, L.vboUV);
            gl.vertexAttribPointer(_locUV, 2, gl.FLOAT, false, 0, 0);
            gl.bindTexture(gl.TEXTURE_2D, L.tex);
            gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, L.ibo);
            if (L.name.indexOf("eyewhite") === 0) {
                // 白目: ステンシルに 1 を書き込む (虹彩のクリップ領域)
                gl.enable(gl.STENCIL_TEST);
                gl.stencilFunc(gl.ALWAYS, 1, 0xff);
                gl.stencilOp(gl.KEEP, gl.KEEP, gl.REPLACE);
                gl.uniform1f(_locCut, 0.25);
                gl.drawElements(gl.TRIANGLES, L.nIdx, gl.UNSIGNED_SHORT, 0);
                gl.disable(gl.STENCIL_TEST);
                gl.uniform1f(_locCut, 0.0);
            } else if (L.name.indexOf("irides") === 0) {
                // 虹彩: 白目領域 (stencil==1) 内のみ描画
                gl.enable(gl.STENCIL_TEST);
                gl.stencilFunc(gl.EQUAL, 1, 0xff);
                gl.stencilOp(gl.KEEP, gl.KEEP, gl.KEEP);
                gl.drawElements(gl.TRIANGLES, L.nIdx, gl.UNSIGNED_SHORT, 0);
                gl.disable(gl.STENCIL_TEST);
            } else {
                gl.drawElements(gl.TRIANGLES, L.nIdx, gl.UNSIGNED_SHORT, 0);
            }
        }
        gl.disableVertexAttribArray(_locPos);
        gl.disableVertexAttribArray(_locUV);
    };

    globalThis.Avatar25D = Avatar25D;
})();
