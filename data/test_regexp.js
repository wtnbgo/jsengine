// 正規表現 replace テスト
console.log("=== RegExp replace test ===");

// テスト1: 単純な g 置換
try {
    var r1 = "hello world".replace(/o/g, "0");
    console.log("test1: " + r1 + (r1 === "hell0 w0rld" ? " OK" : " FAIL"));
} catch(e) {
    console.error("test1 error: " + e);
}

// テスト2: 空マッチあり の g 置換（three.js で使われるパターンに近い）
try {
    var r2 = "abc".replace(/(?:)/g, "-");
    console.log("test2: '" + r2 + "' (expected '-a-b-c-')" + (r2 === "-a-b-c-" ? " OK" : " FAIL"));
} catch(e) {
    console.error("test2 error: " + e);
}

// テスト3: #include パターン（three.js シェーダプリプロセッサ）
try {
    var shader = "#include <common>\nvoid main() {\n#include <fog>\n}";
    var r3 = shader.replace(/#include\s+<(\w+)>/g, function(match, name) {
        return "// resolved: " + name;
    });
    console.log("test3: " + r3.substring(0, 50) + "... OK");
} catch(e) {
    console.error("test3 error: " + e);
}

// テスト4: /\n/g 置換
try {
    var r4 = "a\nb\nc".replace(/\n/g, "\\n");
    console.log("test4: " + r4 + (r4 === "a\\nb\\nc" ? " OK" : " FAIL"));
} catch(e) {
    console.error("test4 error: " + e);
}

// テスト5: 大きな文字列の置換
try {
    var big = "";
    for (var i = 0; i < 1000; i++) big += "abc\n";
    var r5 = big.replace(/\n/g, "");
    console.log("test5: len=" + r5.length + " (expected 3000)" + (r5.length === 3000 ? " OK" : " FAIL"));
} catch(e) {
    console.error("test5 error: " + e);
}

// テスト6: three.js のシェーダで使われるパターンの再現
try {
    var src = "precision highp float;\n#define SHADER_NAME MeshNormalMaterial\nuniform mat4 modelViewMatrix;";
    var r6 = src.replace(/precision\s+(highp|mediump|lowp)\s+float\s*;/g, "");
    console.log("test6: '" + r6.substring(0, 40) + "...' OK");
} catch(e) {
    console.error("test6 error: " + e);
}

// テスト7: three.js の Ur パターン（/^[ \t]*#include +<([\w\d./]+)>/gm）
try {
    var Ur = /^[ \t]*#include +<([\w\d.\/]+)>/gm;
    var shaderChunks = { common: "// common chunk\n", fog_fragment: "// fog\n" };
    var testShader = "#include <common>\nvoid main() {\n  #include <fog_fragment>\n}\n";
    var r7 = testShader.replace(Ur, function(match, name) {
        return shaderChunks[name] || "// unknown: " + name;
    });
    console.log("test7: " + r7.length + " chars OK");
    console.log("  result: " + r7.substring(0, 60));
} catch(e) {
    console.error("test7 error: " + e);
}

// テスト8: 大きなシェーダチャンクの再帰的 replace
try {
    var bigShader = "";
    for (var bi = 0; bi < 50; bi++) {
        bigShader += "#include <chunk" + bi + ">\n";
        bigShader += "uniform float u" + bi + ";\n";
    }
    var r8 = bigShader.replace(Ur, function(m, name) {
        return "// resolved " + name + "\n";
    });
    console.log("test8: input=" + bigShader.length + " output=" + r8.length + " OK");
} catch(e) {
    console.error("test8 error: " + e);
    if (e.stack) console.error(e.stack);
}

// テスト9: three.js の実際のシェーダチャンク展開を再現
// 再帰的 replace（kr は展開結果にさらに #include があれば再帰する）
try {
    var chunks = {};
    // 大きなチャンクを作成（three.js のチャンクは数百行になる）
    for (var ci = 0; ci < 30; ci++) {
        var chunk = "";
        for (var li = 0; li < 20; li++) {
            chunk += "// chunk" + ci + " line " + li + " padding padding padding padding\n";
        }
        if (ci > 0) {
            // 一部のチャンクは他のチャンクを include する（再帰）
            chunk = "#include <chunk" + (ci - 1) + ">\n" + chunk;
        }
        chunks["chunk" + ci] = chunk;
    }

    var resolveIncludes = function(src) {
        var pattern = /^[ \t]*#include +<([\w\d.\/]+)>/gm;
        return src.replace(pattern, function(match, name) {
            var c = chunks[name];
            if (c === undefined) return "// unknown: " + name;
            return resolveIncludes(c); // 再帰
        });
    };

    var mainShader = "#include <chunk29>\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    console.log("test9: resolving shader (" + mainShader.length + " chars)...");
    var r9 = resolveIncludes(mainShader);
    console.log("test9: result=" + r9.length + " chars OK");
} catch(e) {
    console.error("test9 error: " + e);
    if (e.stack) console.error(e.stack);
}

// テスト10: three.js の実際のシェーダチャンク（THREE.ShaderChunk 使用）
if (typeof THREE !== "undefined" && THREE.ShaderChunk) {
    try {
        var chunkNames = Object.keys(THREE.ShaderChunk);
        console.log("test10: THREE.ShaderChunk has " + chunkNames.length + " chunks");
        // 最大のチャンクのサイズ
        var maxLen = 0, maxName = "";
        for (var cni = 0; cni < chunkNames.length; cni++) {
            var cl = THREE.ShaderChunk[chunkNames[cni]].length;
            if (cl > maxLen) { maxLen = cl; maxName = chunkNames[cni]; }
        }
        console.log("test10: largest chunk: " + maxName + " (" + maxLen + " chars)");

        // 実際に kr (resolveIncludes) を呼ぶ
        var testVert = THREE.ShaderLib.normal.vertexShader;
        console.log("test10: normal.vertexShader = " + testVert.length + " chars");
        console.log("test10: first 100 chars: " + testVert.substring(0, 100));

        var resolveThree = function(src) {
            return src.replace(/^[ \t]*#include +<([\w\d.\/]+)>/gm, function(m, name) {
                var c = THREE.ShaderChunk[name];
                if (!c) return "// missing: " + name;
                return resolveThree(c);
            });
        };
        console.log("test10: resolving...");
        var r10 = resolveThree(testVert);
        console.log("test10: resolved=" + r10.length + " chars OK");
    } catch(e) {
        console.error("test10 error: " + e);
        if (e.stack) console.error(e.stack);
    }
} else {
    console.log("test10: skipped (THREE not loaded)");
}

// テスト11: three.js の WebGLProgram をシミュレート
// シェーダの全 replace パターンを順番に実行
if (typeof THREE !== "undefined" && THREE.ShaderChunk) {
    try {
        // resolveIncludes
        var resolveInc = function(src) {
            return src.replace(/^[ \t]*#include +<([\w\d.\/]+)>/gm, function(m, name) {
                var c = THREE.ShaderChunk[name];
                if (!c) return "";
                return resolveInc(c);
            });
        };

        // MeshNormalMaterial の fragment shader
        var fragSrc = THREE.ShaderLib.normal.fragmentShader;
        console.log("test11: fragmentShader = " + fragSrc.length + " chars");

        console.log("test11: resolving includes...");
        var resolved = resolveInc(fragSrc);
        console.log("test11: resolved = " + resolved.length + " chars");

        // replace パターン（three.js の prefixFragment + replace チェーン）
        console.log("test11: applying replace chain...");
        var r = resolved;
        r = r.replace(/NUM_DIR_LIGHTS/g, "0");
        console.log("test11: after NUM_DIR_LIGHTS: " + r.length);
        r = r.replace(/NUM_SPOT_LIGHTS/g, "0");
        console.log("test11: after NUM_SPOT_LIGHTS: " + r.length);
        r = r.replace(/NUM_POINT_LIGHTS/g, "0");
        console.log("test11: after NUM_POINT_LIGHTS: " + r.length);
        r = r.replace(/NUM_HEMI_LIGHTS/g, "0");
        console.log("test11: after NUM_HEMI_LIGHTS: " + r.length);
        r = r.replace(/NUM_RECT_AREA_LIGHTS/g, "0");
        console.log("test11: after NUM_RECT_AREA_LIGHTS: " + r.length);
        r = r.replace(/NUM_CLIPPING_PLANES/g, "0");
        console.log("test11: after NUM_CLIPPING_PLANES: " + r.length);
        r = r.replace(/UNION_CLIPPING_PLANES/g, "0");
        console.log("test11: after UNION_CLIPPING_PLANES: " + r.length);

        // unroll loop
        r = r.replace(/#pragma unroll_loop_start\s+for\s*\(\s*int\s+i\s*=\s*(\d+)\s*;\s*i\s*<\s*(\d+)\s*;\s*i\s*\+\+\s*\)\s*\{([\s\S]+?)\}\s+#pragma unroll_loop_end/g, "");
        console.log("test11: after unroll: " + r.length);

        console.log("test11: ALL OK! Final length = " + r.length);
    } catch(e) {
        console.error("test11 error: " + e);
        if (e.stack) console.error(e.stack);
    }
}

console.log("=== RegExp test done ===");
