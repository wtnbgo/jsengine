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
    this.width = width || 1;
    this.height = height || 1;
    this.style = {};
    this.classList = { add: function(){}, remove: function(){} };
    this._glContext = null;
}

HTMLCanvasElement.prototype.getContext = function(type, options) {
    if (type === "webgl2" || type === "webgl" || type === "experimental-webgl") {
        // 既存の gl オブジェクトを返す
        // pixi.js は canvas.width/height も参照するので同期させる
        this._glContext = gl;
        // canvas プロパティを gl に設定
        gl.canvas = this;
        return gl;
    }
    if (type === "2d") {
        // Canvas2D を返す
        var c = new Canvas2D(this.width, this.height);
        return c;
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
    if (!gl.getExtension) {
        gl.getExtension = function(name) { return null; };
    }
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
}

// src が設定されたら createImageBitmap で読み込む
Object.defineProperty(Image.prototype, "src", {
    set: function(url) {
        this._src = url;
        var self = this;
        // 同期的にロード試行
        try {
            var bitmap = createImageBitmap(url);
            self.width = bitmap.width;
            self.height = bitmap.height;
            self._data = bitmap.data;
            self.complete = true;
            if (typeof self.onload === "function") {
                setTimeout(function() { self.onload(); }, 0);
            }
        } catch(e) {
            self.complete = true;
            if (typeof self.onerror === "function") {
                setTimeout(function() { self.onerror(e); }, 0);
            }
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
