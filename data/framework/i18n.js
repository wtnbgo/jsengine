// ============================================================
// I18n — 文字列辞書 + ロケール切替
// ============================================================
//
// ゴール:
//   - シンプルな key/value 辞書 (フラットなドット記法を推奨)
//   - 複数 locale を持ち、setLocale で切替
//   - 欠落キーは fallbackLocale → key そのものへ順にフォールバック
//   - {name} 形式の文字列補間
//   - localStorage 永続化 (任意)
//   - 切替時に onChange リスナー発火 → 各 PIXI シーンが再描画
//
// 提供 (globalThis.I18n):
//   I18n.init({ defaultLocale, fallbackLocale, locales, persistKey, autoRestore })
//   I18n.addLocale(locale, dict)              — 後から辞書追加 / マージ
//   I18n.setLocale(locale)                    — 切替 (onChange 発火、persistKey なら保存)
//   I18n.getLocale() / I18n.getAvailable()
//   I18n.t(key, params?)                      — 文字列取得。{name} を params.name で置換
//   I18n.onChange(cb) / I18n.offChange(cb)    — locale 変更通知
//
// 使い方:
//   await fetch などで JSON dict を取って:
//   I18n.init({
//       defaultLocale: "en",
//       fallbackLocale: "en",
//       locales: { en: enDict, ja: jaDict, "zh-CN": zhDict },
//       persistKey: "demo11_locale",
//       autoRestore: true,
//   });
//
//   var s = I18n.t("menu.start");
//   var s = I18n.t("greet.hello", { name: "Alice" });   // "Hello, Alice!" 等
//
//   // シーン側
//   var listener = function() { refreshMyTexts(); };
//   I18n.onChange(listener);
//   // exit 時に I18n.offChange(listener);

(function() {

if (typeof globalThis.I18n !== "undefined") return;

var locales = {};         // { localeName: { key: text, ... }, ... }
var current = null;       // 現在の locale
var fallback = null;      // 欠落時のフォールバック locale
var defaultLoc = null;
var listeners = [];
var persistKey = null;

function notify() {
    for (var i = 0; i < listeners.length; i++) {
        try { listeners[i](current); } catch (e) { console.error("I18n listener threw:", e); }
    }
}

function lookupKey(locale, key) {
    var d = locales[locale];
    if (!d) return null;
    return (key in d) ? d[key] : null;
}

// "{name}" を params.name で置換 (見つからなければそのまま残す)
function interpolate(s, params) {
    if (!params) return s;
    return s.replace(/\{(\w+)\}/g, function(_, name) {
        return (name in params) ? String(params[name]) : "{" + name + "}";
    });
}

globalThis.I18n = {
    init: function(opts) {
        opts = opts || {};
        defaultLoc = opts.defaultLocale || "en";
        fallback   = opts.fallbackLocale || defaultLoc;
        if (opts.locales) {
            for (var k in opts.locales) {
                if (opts.locales.hasOwnProperty(k)) locales[k] = opts.locales[k];
            }
        }
        persistKey = opts.persistKey || null;
        current = defaultLoc;
        if (opts.autoRestore && persistKey) {
            try {
                var saved = localStorage.getItem(persistKey);
                if (saved && locales[saved]) current = saved;
            } catch (_) {}
        }
    },

    addLocale: function(locale, dict) {
        if (!locales[locale]) locales[locale] = {};
        for (var k in dict) {
            if (dict.hasOwnProperty(k)) locales[locale][k] = dict[k];
        }
    },

    getLocale: function() { return current; },

    setLocale: function(locale) {
        if (!locales[locale]) {
            console.warn("I18n.setLocale: unknown locale:", locale);
            return false;
        }
        if (current === locale) return true;
        current = locale;
        if (persistKey) {
            try { localStorage.setItem(persistKey, locale); } catch (_) {}
        }
        notify();
        return true;
    },

    getAvailable: function() { return Object.keys(locales); },

    t: function(key, params) {
        var s = lookupKey(current, key);
        if (s == null && fallback && fallback !== current) {
            s = lookupKey(fallback, key);
        }
        if (s == null) return key;  // 最終的にキーそのものを返す (欠落の可視化)
        return interpolate(s, params);
    },

    onChange: function(cb) {
        if (typeof cb === "function") listeners.push(cb);
    },
    offChange: function(cb) {
        var i = listeners.indexOf(cb);
        if (i >= 0) listeners.splice(i, 1);
    },

    // デバッグ用
    _dump: function() { return { current: current, fallback: fallback, locales: locales }; },
};

console.log("framework/i18n.js loaded");

})();
