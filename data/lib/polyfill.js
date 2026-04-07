// ============================================================
// ポリフィル (QuickJS 用)
// QuickJS は ES2023 対応のため、Map/Set/WeakMap/Promise 等は不要。
// ここにはブラウザ互換性のために必要な補完のみ残す。
// ============================================================

// --- Object.getOwnPropertyDescriptors ---
if (!Object.getOwnPropertyDescriptors) {
    Object.getOwnPropertyDescriptors = function(obj) {
        var result = {};
        var keys = Object.getOwnPropertyNames(obj);
        for (var i = 0; i < keys.length; i++) {
            result[keys[i]] = Object.getOwnPropertyDescriptor(obj, keys[i]);
        }
        return result;
    };
}

console.log("polyfill.js loaded");
