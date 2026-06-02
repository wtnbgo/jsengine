// ============================================================
// SaveData — localStorage 上のセーブスロット管理
// ============================================================
//
// ゴール:
//   - スロット (1〜N) 単位でゲームデータを永続化
//   - メタ情報 (savedAt / label / schemaVersion) と data 本体を分離
//   - "Continue" 用の latestSlot 自動追跡
//   - Quick save (スロットとは独立した 1 枠)
//   - スキーマバージョン変更時の migrate コールバック
//
// localStorage キー設計:
//   {namespace}:slot:{n}   → JSON  { version, savedAt, label, data }
//   {namespace}:quick      → JSON  { version, savedAt, label, data }
//   {namespace}:meta       → JSON  { latestSlot }
//
// 使い方:
//   SaveData.init({
//       namespace: "demo11",
//       slots: 3,
//       schemaVersion: 1,
//       migrate: function(data, fromVer, toVer) { return data; },  // 任意
//   });
//   SaveData.save(0, { score: 42, x: 100, y: 200 }, { label: "Stage 1" });
//   var data  = SaveData.load(0);          // → ゲームデータ or null
//   var info  = SaveData.info(0);          // → { exists, savedAt, label, schemaVersion }
//   var list  = SaveData.list();           // → [info(0), info(1), ...]
//   var slot  = SaveData.latestSlot();     // → 最後に save した slot or -1
//   var top   = SaveData.loadLatest();     // → { slot, data } or null
//   SaveData.delete(0);
//   SaveData.quickSave({ ... });
//   var q = SaveData.quickLoad();

(function() {

if (typeof localStorage === "undefined") {
    console.error("framework/save_data.js: localStorage が無い環境");
    return;
}

var cfg = {
    namespace:     "default",
    slots:         3,
    schemaVersion: 1,
    migrate:       null,    // function(data, fromVer, toVer) -> data | null
};
var initialized = false;

function keySlot(n)  { return cfg.namespace + ":slot:" + n; }
function keyQuick()  { return cfg.namespace + ":quick"; }
function keyMeta()   { return cfg.namespace + ":meta"; }

function readJson(key) {
    try {
        var raw = localStorage.getItem(key);
        if (!raw) return null;
        return JSON.parse(raw);
    } catch (_) { return null; }
}
function writeJson(key, obj) {
    try { localStorage.setItem(key, JSON.stringify(obj)); return true; }
    catch (e) { console.error("SaveData: write failed:", key, e); return false; }
}
function removeKey(key) {
    try { localStorage.removeItem(key); } catch (_) {}
}

function readMeta() {
    return readJson(keyMeta()) || { latestSlot: -1 };
}
function writeMeta(m) { writeJson(keyMeta(), m); }

function makeEnvelope(data, opts) {
    return {
        version: cfg.schemaVersion,
        savedAt: Date.now(),
        label:   (opts && typeof opts.label === "string") ? opts.label : null,
        data:    data,
    };
}

function infoOf(env) {
    if (!env) return { exists: false };
    return {
        exists:        true,
        savedAt:       env.savedAt || 0,
        label:         env.label || null,
        schemaVersion: typeof env.version === "number" ? env.version : 0,
    };
}

// envelope を schemaVersion に追随。migrate 失敗時は null を返す。
function unwrap(env) {
    if (!env) return null;
    var v = (typeof env.version === "number") ? env.version : 0;
    if (v === cfg.schemaVersion) return env.data;
    if (!cfg.migrate) {
        console.warn("SaveData: schema mismatch (" + v + " -> " + cfg.schemaVersion + ") but no migrate provided. Returning null.");
        return null;
    }
    try {
        var migrated = cfg.migrate(env.data, v, cfg.schemaVersion);
        return migrated == null ? null : migrated;
    } catch (e) {
        console.error("SaveData: migrate threw:", e);
        return null;
    }
}

function ensureInit() {
    if (!initialized) {
        console.warn("SaveData: init() が呼ばれていません (デフォルト namespace='default' で動作)");
    }
}

globalThis.SaveData = {
    init: function(opts) {
        opts = opts || {};
        if (typeof opts.namespace === "string")    cfg.namespace     = opts.namespace;
        if (typeof opts.slots === "number")        cfg.slots         = Math.max(1, opts.slots | 0);
        if (typeof opts.schemaVersion === "number") cfg.schemaVersion = opts.schemaVersion | 0;
        if (typeof opts.migrate === "function")    cfg.migrate       = opts.migrate;
        initialized = true;
    },

    save: function(slot, data, opts) {
        ensureInit();
        if (slot < 0 || slot >= cfg.slots) {
            console.error("SaveData.save: slot out of range:", slot);
            return false;
        }
        var env = makeEnvelope(data, opts);
        if (!writeJson(keySlot(slot), env)) return false;
        var m = readMeta();
        m.latestSlot = slot;
        writeMeta(m);
        return true;
    },

    load: function(slot) {
        ensureInit();
        if (slot < 0 || slot >= cfg.slots) return null;
        return unwrap(readJson(keySlot(slot)));
    },

    delete: function(slot) {
        ensureInit();
        if (slot < 0 || slot >= cfg.slots) return;
        removeKey(keySlot(slot));
        var m = readMeta();
        if (m.latestSlot === slot) {
            // 次の latestSlot を計算 (savedAt 最新)
            var newest = -1;
            var newestAt = -1;
            for (var i = 0; i < cfg.slots; i++) {
                var env = readJson(keySlot(i));
                if (env && env.savedAt > newestAt) {
                    newest = i;
                    newestAt = env.savedAt;
                }
            }
            m.latestSlot = newest;
            writeMeta(m);
        }
    },

    exists: function(slot) {
        if (slot < 0 || slot >= cfg.slots) return false;
        return readJson(keySlot(slot)) != null;
    },

    info: function(slot) {
        if (slot < 0 || slot >= cfg.slots) return { exists: false };
        return infoOf(readJson(keySlot(slot)));
    },

    list: function() {
        ensureInit();
        var out = [];
        for (var i = 0; i < cfg.slots; i++) {
            var inf = infoOf(readJson(keySlot(i)));
            inf.slot = i;
            out.push(inf);
        }
        return out;
    },

    // 直近 save した slot (delete でも追随)。無ければ -1
    latestSlot: function() {
        ensureInit();
        return readMeta().latestSlot;
    },

    // latestSlot の data を返す ({ slot, data } 形)
    loadLatest: function() {
        var slot = this.latestSlot();
        if (slot < 0) return null;
        var data = this.load(slot);
        if (data == null) return null;
        return { slot: slot, data: data };
    },

    // --- Quick save (slot とは独立) ---
    quickSave: function(data, opts) {
        ensureInit();
        return writeJson(keyQuick(), makeEnvelope(data, opts));
    },
    quickLoad: function() {
        ensureInit();
        return unwrap(readJson(keyQuick()));
    },
    quickInfo: function() {
        return infoOf(readJson(keyQuick()));
    },
    quickDelete: function() {
        removeKey(keyQuick());
    },

    // --- 全消去 (デバッグ用) ---
    wipeAll: function() {
        for (var i = 0; i < cfg.slots; i++) removeKey(keySlot(i));
        removeKey(keyQuick());
        removeKey(keyMeta());
    },

    // 内部用 (デバッグ向け)
    _config: function() { return Object.assign({}, cfg); },
};

console.log("framework/save_data.js loaded");

})();
