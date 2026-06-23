// ============================================================
// 画像デコード並列化プール 実装
// ============================================================
#include "image_decode_pool.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace jsengine {

namespace {

struct Job {
    // 入力 (ワーカーが触る、 メイン → ワーカーへの転送)
    std::vector<uint8_t> input;

    // 出力 (ワーカーが書く、 ワーカー → メインへの転送)
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width*height*4 バイト
    std::string error;           // 空なら成功

    // Promise 解決用 (メインスレッドからのみ触る)
    JSContext *ctx = nullptr;
    JSValue resolveFunc = JS_UNDEFINED;
    JSValue rejectFunc  = JS_UNDEFINED;
};

class ImageDecodePool {
public:
    void start(int numWorkers) {
        if (running_.load()) return;
        running_.store(true);
        int n = numWorkers;
        if (n <= 0) {
            unsigned hw = std::thread::hardware_concurrency();
            n = std::max(2u, hw > 2 ? hw / 2 : 2u);  // 半分くらい、 最低 2
        }
        for (int i = 0; i < n; i++) {
            workers_.emplace_back([this, i]{ this->workerLoop(i); });
        }
        SDL_Log("ImageDecodePool: started with %d workers (hw=%u)", n, std::thread::hardware_concurrency());
    }

    void shutdown() {
        if (!running_.exchange(false)) return;
        pendingCv_.notify_all();
        for (auto &t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
        // ペンディング / 完了キューに残ったジョブは捨てる (解放のみ)。
        // JSValue 参照は呼出側 (ctx 寿命) が落ちる時に GC されるので
        // ここで明示 free しなくても問題ない。
        std::lock_guard<std::mutex> l1(pendingMu_);
        std::lock_guard<std::mutex> l2(completedMu_);
        while (!pending_.empty())   pending_.pop();
        while (!completed_.empty()) completed_.pop();
    }

    void submit(std::unique_ptr<Job> job) {
        inFlight_.fetch_add(1, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(pendingMu_);
            pending_.push(std::move(job));
        }
        pendingCv_.notify_one();
    }

    // in-flight (queue 内 or worker が処理中) なジョブがあるか。
    // pending と completed をチェック + busy worker のヒューリスティック:
    // total submitted - total completed > 0 で判定する。
    bool hasInFlight() const {
        return inFlight_.load(std::memory_order_acquire) > 0;
    }

    // メインスレッドから per-frame で呼ぶ。
    void drain(JSContext *ctx) {
        std::queue<std::unique_ptr<Job>> local;
        {
            std::lock_guard<std::mutex> lk(completedMu_);
            std::swap(local, completed_);
        }
        while (!local.empty()) {
            auto job = std::move(local.front());
            local.pop();
            resolveJob(ctx, *job);
            inFlight_.fetch_sub(1, std::memory_order_release);
        }
    }

private:
    void workerLoop(int /*wid*/) {
        while (true) {
            std::unique_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lk(pendingMu_);
                pendingCv_.wait(lk, [&]{
                    return !pending_.empty() || !running_.load();
                });
                if (!running_.load() && pending_.empty()) return;
                job = std::move(pending_.front());
                pending_.pop();
            }
            decodeJob(*job);
            {
                std::lock_guard<std::mutex> lk(completedMu_);
                completed_.push(std::move(job));
            }
        }
    }

    static void decodeJob(Job &job) {
        SDL_IOStream *io = SDL_IOFromConstMem(job.input.data(), job.input.size());
        if (!io) {
            job.error = std::string("SDL_IOFromConstMem failed: ") + SDL_GetError();
            return;
        }
        SDL_Surface *surface = IMG_Load_IO(io, 1);  // 1 = io 自動 close
        if (!surface) {
            job.error = std::string("IMG_Load_IO failed: ") + SDL_GetError();
            return;
        }
        SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        if (!rgba) {
            job.error = std::string("SDL_ConvertSurface failed: ") + SDL_GetError();
            return;
        }
        int w = rgba->w, h = rgba->h;
        size_t sz = (size_t)w * h * 4;
        job.width = w;
        job.height = h;
        job.rgba.resize(sz);
        if (rgba->pitch == w * 4) {
            memcpy(job.rgba.data(), rgba->pixels, sz);
        } else {
            for (int y = 0; y < h; y++) {
                memcpy(job.rgba.data() + (size_t)y * w * 4,
                       (uint8_t*)rgba->pixels + (size_t)y * rgba->pitch,
                       (size_t)w * 4);
            }
        }
        SDL_DestroySurface(rgba);
    }

    static void resolveJob(JSContext *ctx, Job &job) {
        if (!job.error.empty()) {
            JSValue err = JS_NewError(ctx);
            JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, job.error.c_str()));
            JS_Call(ctx, job.rejectFunc, JS_UNDEFINED, 1, &err);
            JS_FreeValue(ctx, err);
        } else {
            JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "width",  JS_NewInt32(ctx, job.width));
            JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, job.height));
            JS_SetPropertyStr(ctx, obj, "data",   JS_NewArrayBufferCopy(ctx, job.rgba.data(), job.rgba.size()));
            JS_Call(ctx, job.resolveFunc, JS_UNDEFINED, 1, &obj);
            JS_FreeValue(ctx, obj);
        }
        JS_FreeValue(ctx, job.resolveFunc);
        JS_FreeValue(ctx, job.rejectFunc);
        job.resolveFunc = JS_UNDEFINED;
        job.rejectFunc  = JS_UNDEFINED;
    }

    std::atomic<bool> running_{false};
    std::atomic<int>  inFlight_{0};  // submit-但drain で差分カウント
    std::vector<std::thread> workers_;
    std::mutex pendingMu_;
    std::condition_variable pendingCv_;
    std::queue<std::unique_ptr<Job>> pending_;
    std::mutex completedMu_;
    std::queue<std::unique_ptr<Job>> completed_;
};

ImageDecodePool g_pool;

}  // anonymous

void image_decode_pool_init(int numWorkers) {
    g_pool.start(numWorkers);
}

void image_decode_pool_shutdown() {
    g_pool.shutdown();
}

JSValue submit_decode_async(JSContext *ctx, const uint8_t *buf, size_t bufSize) {
    // Promise capability: resolve/reject 関数を取り出す。
    JSValue resolvingFuncs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, resolvingFuncs[0]);
        JS_FreeValue(ctx, resolvingFuncs[1]);
        return promise;
    }

    auto job = std::make_unique<Job>();
    job->ctx = ctx;
    job->resolveFunc = resolvingFuncs[0];
    job->rejectFunc  = resolvingFuncs[1];
    job->input.assign(buf, buf + bufSize);

    g_pool.submit(std::move(job));
    return promise;
}

void drain_completed(JSContext *ctx) {
    g_pool.drain(ctx);
}

bool has_in_flight() {
    return g_pool.hasInFlight();
}

}  // namespace jsengine
