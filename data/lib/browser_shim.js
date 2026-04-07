// ============================================================
// ブラウザ API シム (pixi.js 動作用)
// ============================================================
// polyfill.js の後に読み込むこと

// --- window ---
if (typeof window === "undefined") {
    var window = this; // duktape のグローバルオブジェクト
}
window.window = window;
window.self = window;
window.devicePixelRatio = 1;
window.innerWidth = 1280;
window.innerHeight = 720;
window.console = console;
window.setTimeout = setTimeout;
window.clearTimeout = clearTimeout;
window.setInterval = setInterval;
window.clearInterval = clearInterval;
window.requestAnimationFrame = requestAnimationFrame;
window.cancelAnimationFrame = cancelAnimationFrame;
window.addEventListener = addEventListener;
window.removeEventListener = removeEventListener;

// --- navigator ---
if (typeof navigator === "undefined") {
    var navigator = {};
}
navigator.userAgent = "Mozilla/5.0 (jsengine; duktape) AppleWebKit/537.36";
navigator.platform = "jsengine";
window.navigator = navigator;

// --- location ---
if (typeof location === "undefined") {
    var location = { href: "", protocol: "file:", hostname: "", pathname: "" };
}
window.location = location;

// --- HTMLCanvasElement シム ---
// pixi.js が document.createElement("canvas") で取得してくるキャンバス
function HTMLCanvasElement(width, height) {
    this._width = width || 1;
    this._height = height || 1;
    this.style = {};
    this.classList = { add: function(){}, remove: function(){} };
    this._glContext = null;
}

// width/height setter: 変更時に Canvas2D を再作成
Object.defineProperty(HTMLCanvasElement.prototype, "width", {
    get: function() { return this._width; },
    set: function(v) {
        v = Math.max(1, v | 0);
        this._width = v;
        // キャッシュ済み Canvas2D があればリサイズ
        if (this._ctx2d && typeof this._ctx2d._resize === "function") {
            this._ctx2d._resize(this._width, this._height);
        }
    }
});
Object.defineProperty(HTMLCanvasElement.prototype, "height", {
    get: function() { return this._height; },
    set: function(v) {
        v = Math.max(1, v | 0);
        this._height = v;
        if (this._ctx2d && typeof this._ctx2d._resize === "function") {
            this._ctx2d._resize(this._width, this._height);
        }
    }
});

// data プロパティ: texImage2D 連携用 RGBA データを返す
// Canvas2D の内部バッファは ARGB8888 premultiplied なので、
// RGBA premultiplied に変換して返す（pixi.js が UNPACK_PREMULTIPLY_ALPHA を期待する場合に対応）
Object.defineProperty(HTMLCanvasElement.prototype, "data", {
    get: function() {
        if (this._ctx2d && typeof this._ctx2d._getRGBA === "function") {
            return this._ctx2d._getRGBA();
        }
        if (this._ctx2d && typeof this._ctx2d.getImageData === "function") {
            var imgData = this._ctx2d.getImageData(0, 0, this.width, this.height);
            return imgData ? imgData.data : null;
        }
        return null;
    }
});

HTMLCanvasElement.prototype.getContext = function(type, options) {
    // 2D コンテキストをキャッシュ
    if (type === "2d" && this._ctx2d) return this._ctx2d;

    if (type === "2d") {
        // C++ Canvas2D を使用（ビットマップ保持型）
        var canvas = this;
        var c2d = new Canvas2D(canvas.width || 1, canvas.height || 1);
        c2d.canvas = canvas;
        // createLinearGradient 等の未実装メソッドのダミー
        if (!c2d.createLinearGradient) c2d.createLinearGradient = function() { return { addColorStop: function(){} }; };
        if (!c2d.createRadialGradient) c2d.createRadialGradient = function() { return { addColorStop: function(){} }; };
        if (!c2d.createPattern) c2d.createPattern = function() { return null; };
        if (!c2d.createImageData) c2d.createImageData = function(w,h) { return c2d.getImageData(0,0,w,h); };
        if (!c2d.resetTransform) c2d.resetTransform = function() { c2d.setTransform(1,0,0,1,0,0); };
        if (!c2d.clip) c2d.clip = function() {};
        canvas._ctx2d = c2d;
        return c2d;
    }

    if (type === "webgl2" || type === "webgl" || type === "experimental-webgl") {
        // 既存の gl オブジェクトを返す
        // pixi.js は canvas.width/height も参照するので同期させる
        this._glContext = gl;
        // canvas プロパティを gl に設定
        gl.canvas = this;
        return gl;
    }
    return null;
};

HTMLCanvasElement.prototype.addEventListener = function(type, cb) {
    addEventListener(type, cb);
};
HTMLCanvasElement.prototype.removeEventListener = function(type, cb) {
    removeEventListener(type, cb);
};
HTMLCanvasElement.prototype.getBoundingClientRect = function() {
    return { x: 0, y: 0, width: this.width, height: this.height, top: 0, left: 0, bottom: this.height, right: this.width };
};
HTMLCanvasElement.prototype.setAttribute = function() {};
HTMLCanvasElement.prototype.getAttribute = function(name) {
    if (name === "width") return this.width;
    if (name === "height") return this.height;
    return null;
};

window.HTMLCanvasElement = HTMLCanvasElement;

// --- WebGLRenderingContext / WebGL2RenderingContext ---
// pixi.js が window.WebGLRenderingContext の存在をチェックする
if (typeof WebGLRenderingContext === "undefined") {
    var WebGLRenderingContext = function() {};
    window.WebGLRenderingContext = WebGLRenderingContext;
}
// WebGL2RenderingContext は既に dukwebgl で登録済みだが window にも設定
window.WebGL2RenderingContext = WebGL2RenderingContext;

// gl オブジェクトに pixi.js が必要とするメソッドを追加
if (typeof gl !== "undefined") {
    if (!gl.getContextAttributes) {
        gl.getContextAttributes = function() {
            return {
                alpha: false,
                depth: true,
                stencil: true,
                antialias: false,
                premultipliedAlpha: false,
                preserveDrawingBuffer: false,
                failIfMajorPerformanceCaveat: false
            };
        };
    }
    // getExtension を拡張（GLES3 標準機能を WebGL1 拡張としてエクスポート）
    var _origGetExtension = gl.getExtension;
    gl.getExtension = function(name) {
        if (name === "OES_vertex_array_object") return gl._vaoExt;
        if (name === "OES_element_index_uint") return {};
        if (name === "ANGLE_instanced_arrays") return {
            drawArraysInstancedANGLE: function(m,f,c,n) { gl.drawArraysInstanced(m,f,c,n); },
            drawElementsInstancedANGLE: function(m,c,t,o,n) { gl.drawElementsInstanced(m,c,t,o,n); },
            vertexAttribDivisorANGLE: function(i,d) { gl.vertexAttribDivisor(i,d); },
            VERTEX_ATTRIB_ARRAY_DIVISOR_ANGLE: 0x88FE
        };
        if (name === "OES_texture_float") return {};
        if (name === "OES_texture_half_float") return { HALF_FLOAT_OES: 0x8D61 };
        if (name === "WEBGL_depth_texture") return { UNSIGNED_INT_24_8_WEBGL: 0x84FA };
        if (name === "EXT_blend_minmax") return { MIN_EXT: gl.MIN, MAX_EXT: gl.MAX };
        if (name === "WEBGL_draw_buffers") return {
            drawBuffersWEBGL: function(bufs) { gl.drawBuffers(bufs); },
            MAX_DRAW_BUFFERS_WEBGL: gl.MAX_DRAW_BUFFERS || 8,
            COLOR_ATTACHMENT0_WEBGL: gl.COLOR_ATTACHMENT0
        };
        if (name === "OES_standard_derivatives") return {};
        if (name === "EXT_frag_depth") return {};
        if (name === "EXT_shader_texture_lod") return {};
        if (name === "EXT_color_buffer_float") return {};
        if (name === "WEBGL_lose_context") return {
            loseContext: function() {},
            restoreContext: function() {}
        };
        if (_origGetExtension) return _origGetExtension.call(gl, name);
        return null;
    };
    if (!gl.drawingBufferWidth) {
        Object.defineProperty(gl, "drawingBufferWidth", {
            get: function() { return gl.canvas ? gl.canvas.width : 1280; }
        });
    }
    if (!gl.drawingBufferHeight) {
        Object.defineProperty(gl, "drawingBufferHeight", {
            get: function() { return gl.canvas ? gl.canvas.height : 720; }
        });
    }
    // OES_vertex_array_object 拡張のメソッドをマップ（pixi.js v4 用）
    // GLES3 では VAO は標準機能なのでエイリアスを作成
    gl._vaoExt = {
        createVertexArrayOES: function() { return gl.createVertexArray(); },
        deleteVertexArrayOES: function(vao) { gl.deleteVertexArray(vao); },
        bindVertexArrayOES: function(vao) { gl.bindVertexArray(vao); },
        isVertexArrayOES: function(vao) { return gl.isVertexArray(vao); },
        VERTEX_ARRAY_BINDING_OES: gl.VERTEX_ARRAY_BINDING
    };

    // pixi.js が使う追加メソッド
    if (!gl.getShaderPrecisionFormat) {
        gl.getShaderPrecisionFormat = function(shaderType, precisionType) {
            return { rangeMin: 127, rangeMax: 127, precision: 23 };
        };
    }
    if (!gl.getInternalformatParameter) {
        gl.getInternalformatParameter = function(target, internalformat, pname) {
            return new Int32Array([4]); // SAMPLES 用のダミー値
        };
    }
    if (!gl.getBufferSubData) {
        gl.getBufferSubData = function() {};
    }
    if (!gl.fenceSync) {
        gl.fenceSync = function() { return {}; };
        gl.deleteSync = function() {};
        gl.clientWaitSync = function() { return 0x911D; }; // ALREADY_SIGNALED
        gl.waitSync = function() {};
    }
    if (!gl.invalidateFramebuffer) {
        gl.invalidateFramebuffer = function() {};
    }
    if (!gl.renderbufferStorageMultisample) {
        gl.renderbufferStorageMultisample = function(target, samples, internalformat, width, height) {
            gl.renderbufferStorage(target, internalformat, width, height);
        };
    }
    if (!gl.texStorage2D) {
        gl.texStorage2D = function() {};
    }
    if (!gl.texStorage3D) {
        gl.texStorage3D = function() {};
    }
}

// --- document ---
if (typeof document === "undefined") {
    var document = {};
}
document.createElement = function(tag) {
    if (tag === "canvas") {
        return new HTMLCanvasElement(1, 1);
    }
    if (tag === "div" || tag === "span" || tag === "p") {
        return {
            style: {},
            classList: { add: function(){}, remove: function(){} },
            appendChild: function() {},
            removeChild: function() {},
            addEventListener: function() {},
            removeEventListener: function() {},
            setAttribute: function() {},
            getAttribute: function() { return null; },
            innerHTML: "",
            textContent: ""
        };
    }
    return {};
};
document.createElementNS = function(ns, tag) {
    return document.createElement(tag);
};
document.getElementById = function() { return null; };
document.querySelector = function() { return null; };
document.querySelectorAll = function() { return []; };
document.documentElement = { style: {} };
document.body = {
    style: {},
    appendChild: function() {},
    removeChild: function() {},
    addEventListener: function() {},
    removeEventListener: function() {}
};
document.head = document.body;
document.addEventListener = function(type, cb) { addEventListener(type, cb); };
document.removeEventListener = function(type, cb) { removeEventListener(type, cb); };
window.document = document;

// --- Image ---
function Image(width, height) {
    this.width = width || 0;
    this.height = height || 0;
    this.src = "";
    this.onload = null;
    this.onerror = null;
    this.crossOrigin = "";
    this.complete = false;
    this._data = null;
    this._listeners = {};
}

Image.prototype.addEventListener = function(type, cb) {
    if (!this._listeners[type]) this._listeners[type] = [];
    this._listeners[type].push(cb);
};
Image.prototype.removeEventListener = function(type, cb) {
    if (this._listeners[type]) {
        this._listeners[type] = this._listeners[type].filter(function(f) { return f !== cb; });
    }
};

// src が設定されたら createImageBitmap で読み込む
Object.defineProperty(Image.prototype, "src", {
    set: function(url) {
        this._src = url;
        var self = this;
        // 同期的にロード試行
        try {
            // URL からクエリパラメータを除去
            var cleanUrl = url.split("?")[0];
            if (!cleanUrl || cleanUrl.length === 0) {
                // 空URLは無視
                return;
            }
            var bitmap = createImageBitmap(cleanUrl);
            self.width = bitmap.width;
            self.height = bitmap.height;
            self._data = bitmap.data;
            self.complete = true;
            // onload + addEventListener('load') コールバック実行
            // setTimeout 内で _listeners を参照（src 設定後に addEventListener される場合に対応）
            setTimeout(function() {
                if (typeof self.onload === "function") self.onload();
                var cbs = (self._listeners && self._listeners["load"]) || [];
                for (var i = 0; i < cbs.length; i++) cbs[i].call(self);
            }, 0);
        } catch(e) {
            if (cleanUrl && cleanUrl.length > 0) {
                console.error("Image load error: " + cleanUrl + " => " + e);
            }
            self.complete = true;
            setTimeout(function() {
                if (typeof self.onerror === "function") self.onerror(e);
                var cbs = (self._listeners && self._listeners["error"]) || [];
                for (var i = 0; i < cbs.length; i++) cbs[i].call(self, e);
            }, 0);
        }
    },
    get: function() { return this._src || ""; }
});

window.Image = Image;
window.HTMLImageElement = Image;

// --- URL ---
if (typeof URL === "undefined") {
    var URL = {};
}
URL.createObjectURL = function() { return "blob:dummy"; };
URL.revokeObjectURL = function() {};
window.URL = URL;

// --- XMLHttpRequest (最小シム) ---
function XMLHttpRequest() {
    this.readyState = 0;
    this.status = 0;
    this.response = null;
    this.responseType = "";
    this.onload = null;
    this.onerror = null;
    this.onreadystatechange = null;
}
XMLHttpRequest.prototype.open = function(method, url) {
    this._method = method;
    this._url = url;
    this.readyState = 1;
};
XMLHttpRequest.prototype.setRequestHeader = function() {};
XMLHttpRequest.prototype.send = function() {
    var self = this;
    try {
        var data = fs.readText(self._url);
        self.readyState = 4;
        self.status = 200;
        if (self.responseType === "arraybuffer") {
            // テキストからバッファに変換（簡易）
            var buf = new Uint8Array(data.length);
            for (var i = 0; i < data.length; i++) buf[i] = data.charCodeAt(i);
            self.response = buf.buffer;
        } else {
            self.response = data;
            self.responseText = data;
        }
    } catch(e) {
        self.readyState = 4;
        self.status = 404;
    }
    if (typeof self.onreadystatechange === "function") {
        setTimeout(function() { self.onreadystatechange(); }, 0);
    }
    if (self.status === 200 && typeof self.onload === "function") {
        setTimeout(function() { self.onload(); }, 0);
    }
    if (self.status !== 200 && typeof self.onerror === "function") {
        setTimeout(function() { self.onerror(); }, 0);
    }
};
window.XMLHttpRequest = XMLHttpRequest;

// --- fetch (最小シム) ---
window.fetch = function(url, options) {
    return new Promise(function(resolve, reject) {
        try {
            var data = fs.readText(url);
            resolve({
                ok: true,
                status: 200,
                json: function() { return Promise.resolve(JSON.parse(data)); },
                text: function() { return Promise.resolve(data); },
                arrayBuffer: function() {
                    var buf = new Uint8Array(data.length);
                    for (var i = 0; i < data.length; i++) buf[i] = data.charCodeAt(i);
                    return Promise.resolve(buf.buffer);
                }
            });
        } catch(e) {
            reject(e);
        }
    });
};

// --- Event / CustomEvent ---
function Event(type, options) {
    this.type = type;
    this.bubbles = options && options.bubbles || false;
    this.cancelable = options && options.cancelable || false;
    this.defaultPrevented = false;
}
Event.prototype.preventDefault = function() { this.defaultPrevented = true; };
Event.prototype.stopPropagation = function() {};

function CustomEvent(type, options) {
    Event.call(this, type, options);
    this.detail = options && options.detail || null;
}
CustomEvent.prototype = Object.create(Event.prototype);
window.Event = Event;
window.CustomEvent = CustomEvent;

// pixi.js が参照するイベントクラス
function MouseEvent(type, opts) { Event.call(this, type, opts); }
MouseEvent.prototype = Object.create(Event.prototype);
function PointerEvent(type, opts) { Event.call(this, type, opts); }
PointerEvent.prototype = Object.create(Event.prototype);
function TouchEvent(type, opts) { Event.call(this, type, opts); }
TouchEvent.prototype = Object.create(Event.prototype);
function KeyboardEvent(type, opts) { Event.call(this, type, opts); }
KeyboardEvent.prototype = Object.create(Event.prototype);
function WheelEvent(type, opts) { Event.call(this, type, opts); }
WheelEvent.prototype = Object.create(Event.prototype);
function FocusEvent(type, opts) { Event.call(this, type, opts); }
FocusEvent.prototype = Object.create(Event.prototype);
window.MouseEvent = MouseEvent;
window.PointerEvent = PointerEvent;
window.TouchEvent = TouchEvent;
window.KeyboardEvent = KeyboardEvent;
window.WheelEvent = WheelEvent;
window.FocusEvent = FocusEvent;

// --- globalThis ---
if (typeof globalThis === "undefined") {
    this.globalThis = window;
}

console.log("browser_shim.js loaded");
