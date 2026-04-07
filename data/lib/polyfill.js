// ============================================================
// ES6 ポリフィル (duktape 用)
// pixi.js v5 動作に必要な Map, Set, WeakMap, Promise, Array.from
// ============================================================

// --- Map ---
if (typeof Map === "undefined") {
    (function() {
        function Map(iterable) {
            this._keys = [];
            this._vals = [];
            this.size = 0;
            if (iterable) {
                for (var i = 0; i < iterable.length; i++) {
                    this.set(iterable[i][0], iterable[i][1]);
                }
            }
        }
        Map.prototype._indexOf = function(key) {
            for (var i = 0; i < this._keys.length; i++) {
                if (this._keys[i] === key) return i;
            }
            return -1;
        };
        Map.prototype.set = function(key, value) {
            var idx = this._indexOf(key);
            if (idx >= 0) {
                this._vals[idx] = value;
            } else {
                this._keys.push(key);
                this._vals.push(value);
                this.size++;
            }
            return this;
        };
        Map.prototype.get = function(key) {
            var idx = this._indexOf(key);
            return idx >= 0 ? this._vals[idx] : undefined;
        };
        Map.prototype.has = function(key) {
            return this._indexOf(key) >= 0;
        };
        Map.prototype.delete = function(key) {
            var idx = this._indexOf(key);
            if (idx >= 0) {
                this._keys.splice(idx, 1);
                this._vals.splice(idx, 1);
                this.size--;
                return true;
            }
            return false;
        };
        Map.prototype.clear = function() {
            this._keys = [];
            this._vals = [];
            this.size = 0;
        };
        Map.prototype.forEach = function(cb, thisArg) {
            for (var i = 0; i < this._keys.length; i++) {
                cb.call(thisArg, this._vals[i], this._keys[i], this);
            }
        };
        Map.prototype.keys = function() {
            return this._keys.slice();
        };
        Map.prototype.values = function() {
            return this._vals.slice();
        };
        Map.prototype.entries = function() {
            var result = [];
            for (var i = 0; i < this._keys.length; i++) {
                result.push([this._keys[i], this._vals[i]]);
            }
            return result;
        };
        this.Map = Map;
    })();
}

// --- Set ---
if (typeof Set === "undefined") {
    (function() {
        function Set(iterable) {
            this._vals = [];
            this.size = 0;
            if (iterable) {
                for (var i = 0; i < iterable.length; i++) {
                    this.add(iterable[i]);
                }
            }
        }
        Set.prototype.add = function(value) {
            if (!this.has(value)) {
                this._vals.push(value);
                this.size++;
            }
            return this;
        };
        Set.prototype.has = function(value) {
            for (var i = 0; i < this._vals.length; i++) {
                if (this._vals[i] === value) return true;
            }
            return false;
        };
        Set.prototype.delete = function(value) {
            for (var i = 0; i < this._vals.length; i++) {
                if (this._vals[i] === value) {
                    this._vals.splice(i, 1);
                    this.size--;
                    return true;
                }
            }
            return false;
        };
        Set.prototype.clear = function() {
            this._vals = [];
            this.size = 0;
        };
        Set.prototype.forEach = function(cb, thisArg) {
            for (var i = 0; i < this._vals.length; i++) {
                cb.call(thisArg, this._vals[i], this._vals[i], this);
            }
        };
        Set.prototype.values = function() {
            return this._vals.slice();
        };
        Set.prototype.keys = Set.prototype.values;
        this.Set = Set;
    })();
}

// --- WeakMap ---
if (typeof WeakMap === "undefined") {
    (function() {
        var counter = 0;
        var propName = "__wm_" + Math.random().toString(36).slice(2);
        function WeakMap() {
            this._id = propName + "_" + (counter++);
        }
        WeakMap.prototype.set = function(key, value) {
            if (typeof key !== "object" || key === null) throw new TypeError("WeakMap key must be an object");
            Object.defineProperty(key, this._id, { value: value, configurable: true, writable: true });
            return this;
        };
        WeakMap.prototype.get = function(key) {
            return (typeof key === "object" && key !== null) ? key[this._id] : undefined;
        };
        WeakMap.prototype.has = function(key) {
            return (typeof key === "object" && key !== null) && key.hasOwnProperty(this._id);
        };
        WeakMap.prototype.delete = function(key) {
            if (this.has(key)) {
                delete key[this._id];
                return true;
            }
            return false;
        };
        this.WeakMap = WeakMap;
    })();
}

// --- WeakSet ---
if (typeof WeakSet === "undefined") {
    (function() {
        var counter = 0;
        var propName = "__ws_" + Math.random().toString(36).slice(2);
        function WeakSet() {
            this._id = propName + "_" + (counter++);
        }
        WeakSet.prototype.add = function(value) {
            if (typeof value !== "object" || value === null) throw new TypeError("WeakSet value must be an object");
            Object.defineProperty(value, this._id, { value: true, configurable: true });
            return this;
        };
        WeakSet.prototype.has = function(value) {
            return (typeof value === "object" && value !== null) && value[this._id] === true;
        };
        WeakSet.prototype.delete = function(value) {
            if (this.has(value)) {
                delete value[this._id];
                return true;
            }
            return false;
        };
        this.WeakSet = WeakSet;
    })();
}

// --- Promise (簡易同期版) ---
if (typeof Promise === "undefined") {
    (function() {
        var PENDING = 0, FULFILLED = 1, REJECTED = 2;

        function Promise(executor) {
            this._state = PENDING;
            this._value = undefined;
            this._handlers = [];
            var self = this;
            try {
                executor(
                    function(value) { self._resolve(value); },
                    function(reason) { self._reject(reason); }
                );
            } catch(e) {
                self._reject(e);
            }
        }

        Promise.prototype._resolve = function(value) {
            if (this._state !== PENDING) return;
            if (value && typeof value.then === "function") {
                var self = this;
                value.then(function(v) { self._resolve(v); }, function(r) { self._reject(r); });
                return;
            }
            this._state = FULFILLED;
            this._value = value;
            this._flush();
        };

        Promise.prototype._reject = function(reason) {
            if (this._state !== PENDING) return;
            this._state = REJECTED;
            this._value = reason;
            this._flush();
        };

        Promise.prototype._flush = function() {
            if (this._state === PENDING) return;
            var handlers = this._handlers;
            this._handlers = [];
            for (var i = 0; i < handlers.length; i++) {
                handlers[i]();
            }
        };

        Promise.prototype.then = function(onFulfilled, onRejected) {
            var self = this;
            return new Promise(function(resolve, reject) {
                function handle() {
                    try {
                        var cb = self._state === FULFILLED ? onFulfilled : onRejected;
                        if (typeof cb === "function") {
                            resolve(cb(self._value));
                        } else if (self._state === FULFILLED) {
                            resolve(self._value);
                        } else {
                            reject(self._value);
                        }
                    } catch(e) {
                        reject(e);
                    }
                }
                if (self._state !== PENDING) {
                    handle();
                } else {
                    self._handlers.push(handle);
                }
            });
        };

        Promise.prototype.catch = function(onRejected) {
            return this.then(null, onRejected);
        };

        Promise.prototype.finally = function(onFinally) {
            return this.then(
                function(value) { onFinally(); return value; },
                function(reason) { onFinally(); throw reason; }
            );
        };

        Promise.resolve = function(value) {
            return new Promise(function(resolve) { resolve(value); });
        };

        Promise.reject = function(reason) {
            return new Promise(function(_, reject) { reject(reason); });
        };

        Promise.all = function(promises) {
            return new Promise(function(resolve, reject) {
                var results = [];
                var remaining = promises.length;
                if (remaining === 0) { resolve(results); return; }
                for (var i = 0; i < promises.length; i++) {
                    (function(idx) {
                        Promise.resolve(promises[idx]).then(function(value) {
                            results[idx] = value;
                            if (--remaining === 0) resolve(results);
                        }, reject);
                    })(i);
                }
            });
        };

        Promise.race = function(promises) {
            return new Promise(function(resolve, reject) {
                for (var i = 0; i < promises.length; i++) {
                    Promise.resolve(promises[i]).then(resolve, reject);
                }
            });
        };

        this.Promise = Promise;
    })();
}

// --- Array.from ---
if (!Array.from) {
    Array.from = function(arrayLike, mapFn, thisArg) {
        var result = [];
        var len = arrayLike.length >>> 0;
        for (var i = 0; i < len; i++) {
            result.push(mapFn ? mapFn.call(thisArg, arrayLike[i], i) : arrayLike[i]);
        }
        return result;
    };
}

// --- Array.prototype.find ---
if (!Array.prototype.find) {
    Array.prototype.find = function(predicate, thisArg) {
        for (var i = 0; i < this.length; i++) {
            if (predicate.call(thisArg, this[i], i, this)) return this[i];
        }
        return undefined;
    };
}

// --- Array.prototype.findIndex ---
if (!Array.prototype.findIndex) {
    Array.prototype.findIndex = function(predicate, thisArg) {
        for (var i = 0; i < this.length; i++) {
            if (predicate.call(thisArg, this[i], i, this)) return i;
        }
        return -1;
    };
}

// --- Array.prototype.fill ---
if (!Array.prototype.fill) {
    Array.prototype.fill = function(value, start, end) {
        var len = this.length >>> 0;
        start = start || 0;
        end = end === undefined ? len : end;
        if (start < 0) start = Math.max(len + start, 0);
        if (end < 0) end = Math.max(len + end, 0);
        for (var i = start; i < end && i < len; i++) {
            this[i] = value;
        }
        return this;
    };
}

// --- Reflect.get / Reflect.set ---
// duktape の組み込み Reflect.get は "unsupported" を投げるので強制上書き
if (typeof Reflect !== "undefined") {
    {
        Reflect.get = function(target, prop, receiver) {
            var desc = Object.getOwnPropertyDescriptor(target, prop);
            if (desc && desc.get) {
                return desc.get.call(receiver || target);
            }
            if (desc) return desc.value;
            var proto = Object.getPrototypeOf(target);
            if (proto) return Reflect.get(proto, prop, receiver);
            return undefined;
        };
    }
    {
        Reflect.set = function(target, prop, value, receiver) {
            var desc = Object.getOwnPropertyDescriptor(target, prop);
            if (desc && desc.set) {
                desc.set.call(receiver || target, value);
                return true;
            }
            target[prop] = value;
            return true;
        };
    }
}

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
