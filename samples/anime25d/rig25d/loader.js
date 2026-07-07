// ============================================================
// rig25d/loader.js — データ作製部 (PSD → rig 定義 + バイナリキャッシュ)
// ============================================================
//
// ag-psd + Rigger (Anime2.5DRig) で PSD からリグ定義を生成する。
// buildRig は画像処理が重く QuickJS だと 1280px 級 PSD で 1 分近く
// かかるため、生成結果を <psd>.rig にキャッシュして 2 回目以降は
// 即ロードする (PSD のサイズ+ハッシュで自動無効化)。
//
// 依存: lib/ag-psd.min.js, lib/rigger.js, lib/genericparts.js を
//       先に loadScript しておくこと。
//
// API:
//   Rig25D.loadRig("psd/sample.psd")          → rig (キャッシュ利用)
//   Rig25D.buildFromPsd(arrayBuffer, opts)    → rig (キャッシュなし)
//   Rig25D.clearCache("psd/sample.psd")       → キャッシュ削除
"use strict";

(function () {

    var CACHE_MAGIC = 0x52323544;   // 'R25D'
    var CACHE_VERSION = 1;

    // ---------- utf8 ----------
    function utf8Encode(str) {
        if (typeof TextEncoder !== "undefined") return new TextEncoder().encode(str);
        var out = [];
        for (var i = 0; i < str.length; i++) {
            var c = str.codePointAt(i);
            if (c > 0xffff) i++;
            if (c < 0x80) out.push(c);
            else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
            else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
            else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
        }
        return new Uint8Array(out);
    }
    function utf8Decode(bytes) {
        return new TextDecoder().decode(bytes);
    }

    // ---------- PSD ハッシュ (先頭 64KB の FNV-1a + 全長) ----------
    function hashOf(buf) {
        var u8 = new Uint8Array(buf, 0, Math.min(65536, buf.byteLength));
        var h = 0x811c9dc5;
        for (var i = 0; i < u8.length; i++) {
            h ^= u8[i];
            h = (h * 0x01000193) >>> 0;
        }
        return h >>> 0;
    }

    // ---------- ag-psd の canvas 依存を純 JS 実装に差し替え ----------
    var _canvasInit = false;
    function ensureAgPsdInit() {
        if (_canvasInit || !globalThis.agPsd) return;
        if (agPsd.initializeCanvas) {
            agPsd.initializeCanvas(
                function createCanvas(w, h) {
                    var c = document.createElement("canvas");
                    c.width = w; c.height = h;
                    return c;
                },
                undefined,
                function createImageData(w, h) {
                    return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) };
                }
            );
        }
        _canvasInit = true;
    }

    // ---------- 汎用閉じ目/閉じ口差分 ----------
    function genericOpts() {
        var GP = globalThis.GenericParts;
        if (!GP) return {};
        var g = { eyeL: GP.get("eyeL"), eyeR: GP.get("eyeR"), mouth: GP.get("mouth") };
        return (g.eyeL || g.mouth) ? { generic: g } : {};
    }

    // ---------- rig ⇄ バイナリキャッシュ ----------
    // layout: [u32 magic][u32 ver][u32 srcSize][u32 srcHash][u32 jsonLen][json][RGBA blobs...]
    function serializeRig(rig, srcSize, srcHash) {
        var meta = {
            canvas: rig.canvas, anchors: rig.anchors,
            warnings: rig.warnings, synth: rig.synth,
            layers: rig.layers.map(function (L) {
                var m = {};
                for (var k in L) if (k !== "img") m[k] = L[k];
                m.imgW = L.img.width; m.imgH = L.img.height;
                return m;
            })
        };
        var json = utf8Encode(JSON.stringify(meta));
        var blobLen = 0;
        rig.layers.forEach(function (L) { blobLen += L.img.data.byteLength; });
        var total = 20 + json.byteLength + blobLen;
        var buf = new ArrayBuffer(total);
        var dv = new DataView(buf), u8 = new Uint8Array(buf);
        dv.setUint32(0, CACHE_MAGIC, true);
        dv.setUint32(4, CACHE_VERSION, true);
        dv.setUint32(8, srcSize >>> 0, true);
        dv.setUint32(12, srcHash >>> 0, true);
        dv.setUint32(16, json.byteLength, true);
        u8.set(json, 20);
        var ofs = 20 + json.byteLength;
        rig.layers.forEach(function (L) {
            u8.set(new Uint8Array(L.img.data.buffer, L.img.data.byteOffset, L.img.data.byteLength), ofs);
            ofs += L.img.data.byteLength;
        });
        return buf;
    }

    function deserializeRig(buf, srcSize, srcHash) {
        if (buf.byteLength < 20) return null;
        var dv = new DataView(buf);
        if (dv.getUint32(0, true) !== CACHE_MAGIC) return null;
        if (dv.getUint32(4, true) !== CACHE_VERSION) return null;
        if (srcSize != null && dv.getUint32(8, true) !== (srcSize >>> 0)) return null;
        if (srcHash != null && dv.getUint32(12, true) !== (srcHash >>> 0)) return null;
        var jsonLen = dv.getUint32(16, true);
        var meta = JSON.parse(utf8Decode(new Uint8Array(buf, 20, jsonLen)));
        var ofs = 20 + jsonLen;
        meta.layers.forEach(function (m) {
            var len = m.imgW * m.imgH * 4;
            m.img = { width: m.imgW, height: m.imgH, data: new Uint8ClampedArray(buf.slice(ofs, ofs + len)) };
            delete m.imgW; delete m.imgH;
            ofs += len;
        });
        return { canvas: meta.canvas, layers: meta.layers, anchors: meta.anchors,
                 warnings: meta.warnings || [], synth: meta.synth || {} };
    }

    // ---------- 公開 API ----------
    var Rig25D = {
        // PSD の ArrayBuffer からリグ生成 (キャッシュなし・重い)
        buildFromPsd: function (buf, opts) {
            opts = opts || {};
            ensureAgPsdInit();
            var psd = agPsd.readPsd(new Uint8Array(buf), { useImageData: true, skipThumbnail: true });
            Rigger.cleanPsdLayers(psd);
            var o = opts.noGeneric ? {} : genericOpts();
            return Rigger.buildRig(psd, o);
        },

        cachePathOf: function (psdPath) { return psdPath + ".rig"; },

        // キャッシュがあれば即ロード、無ければ生成して保存。
        // onStage(stage) : "cache" | "parse" | "build" | "save" の進捗通知 (同期)
        loadRig: function (psdPath, opts) {
            opts = opts || {};
            var buf = fs.readBinary(psdPath);
            var srcSize = buf.byteLength, srcHash = hashOf(buf);
            var cachePath = this.cachePathOf(psdPath);
            if (!opts.noCache && fs.exists(cachePath)) {
                if (opts.onStage) opts.onStage("cache");
                try {
                    var rig = deserializeRig(fs.readBinary(cachePath), srcSize, srcHash);
                    if (rig) return rig;
                    console.log("[rig25d] cache stale: " + cachePath);
                } catch (e) {
                    console.log("[rig25d] cache read failed: " + e.message);
                }
            }
            if (opts.onStage) opts.onStage("build");
            var t0 = Date.now();
            var rig2 = this.buildFromPsd(buf, opts);
            console.log("[rig25d] buildRig: " + rig2.layers.length + " parts, " + (Date.now() - t0) + "ms");
            if (!opts.noCache) {
                if (opts.onStage) opts.onStage("save");
                try {
                    fs.writeBinary(cachePath, serializeRig(rig2, srcSize, srcHash));
                    console.log("[rig25d] cache saved: " + cachePath);
                } catch (e2) {
                    console.log("[rig25d] cache save failed: " + e2.message);
                }
            }
            return rig2;
        },

        clearCache: function (psdPath) {
            var p = this.cachePathOf(psdPath);
            if (fs.exists(p)) fs.remove(p);
        }
    };

    globalThis.Rig25D = Rig25D;
})();
