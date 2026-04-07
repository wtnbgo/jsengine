/**
 * Canvas 2D API (ブラウザ互換サブセット)
 *
 * ビットマップ保持型: 各描画操作は即座にピクセルバッファに反映される。
 * ThorVG SwCanvas で描画し、結果をバッファに蓄積。
 * clearRect で明示的にクリアされるまで内容を維持。
 */

#include "canvas2d.h"
#include "jsengine.hpp"
#include "glad/gles2.h"
#include <thorvg.h>
#include <quickjs.h>
#include <SDL3/SDL.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// ThorVG 初期化 / 終了
// ============================================================

// ColorSpace ベンチマーク（起動時に実行）
static void benchmark_colorspace() {
    SDL_Log("=== ColorSpace benchmark ===");
    tvg::ColorSpace modes[] = { tvg::ColorSpace::ARGB8888, tvg::ColorSpace::ABGR8888S };
    const char *names[] = {"ARGB8888", "ABGR8888S"};
    for (int m = 0; m < 2; m++) {
        uint32_t w = 816, h = 624;
        std::vector<uint32_t> buf(w * h, 0);
        auto *canvas = tvg::SwCanvas::gen(tvg::EngineOption::None);
        canvas->target(buf.data(), w, w, h, modes[m]);
        Uint64 start = SDL_GetTicks();
        for (int i = 0; i < 500; i++) {
            auto *shape = tvg::Shape::gen();
            shape->appendRect((float)(i % 50) * 16.0f, (float)(i / 50) * 16.0f, 14, 14, 0, 0);
            shape->fill((uint8_t)(i % 256), (uint8_t)((i*7) % 256), (uint8_t)((i*13) % 256), 200);
            canvas->add(shape);
            canvas->update();
            canvas->draw(false);
            canvas->sync();
            canvas->remove(shape);
        }
        Uint64 elapsed = SDL_GetTicks() - start;
        int nonzero = 0;
        for (uint32_t i = 0; i < w * h; i++) { if (buf[i] != 0) nonzero++; }
        SDL_Log("  %s: 500x draw in %dms, %d non-zero pixels", names[m], (int)elapsed, nonzero);
        delete canvas;
    }
    SDL_Log("=== benchmark done ===");
}

void canvas2d_init() {
    tvg::Initializer::init(1);
    SDL_Log("Canvas2D (ThorVG) initialized");
    // benchmark_colorspace(); // 必要時にコメント解除
}

void canvas2d_uninit() {
    tvg::Initializer::term();
}

// ============================================================
// 色パース
// ============================================================

struct Color4 { uint8_t r, g, b, a; };

static Color4 parse_color(const char *str) {
    Color4 c = {0, 0, 0, 255};
    if (!str) return c;
    if (str[0] == '#') {
        size_t len = strlen(str + 1);
        unsigned long val = strtoul(str + 1, nullptr, 16);
        if (len == 6) { c.r = (val>>16)&0xFF; c.g = (val>>8)&0xFF; c.b = val&0xFF; }
        else if (len == 8) { c.r = (val>>24)&0xFF; c.g = (val>>16)&0xFF; c.b = (val>>8)&0xFF; c.a = val&0xFF; }
        else if (len == 3) { c.r = ((val>>8)&0xF)*17; c.g = ((val>>4)&0xF)*17; c.b = (val&0xF)*17; }
    } else if (strncmp(str, "rgba(", 5) == 0) {
        float r,g,b,a; sscanf(str+5, "%f,%f,%f,%f", &r,&g,&b,&a);
        c.r=(uint8_t)r; c.g=(uint8_t)g; c.b=(uint8_t)b; c.a=(uint8_t)(a*255);
    } else if (strncmp(str, "rgb(", 4) == 0) {
        float r,g,b; sscanf(str+4, "%f,%f,%f", &r,&g,&b);
        c.r=(uint8_t)r; c.g=(uint8_t)g; c.b=(uint8_t)b;
    } else if (strcmp(str,"white")==0) { c.r=c.g=c.b=255; }
    else if (strcmp(str,"red")==0) { c.r=255; }
    else if (strcmp(str,"green")==0) { c.g=128; }
    else if (strcmp(str,"blue")==0) { c.b=255; }
    else if (strcmp(str,"yellow")==0) { c.r=255;c.g=255; }
    else if (strcmp(str,"cyan")==0) { c.g=255;c.b=255; }
    else if (strcmp(str,"magenta")==0) { c.r=255;c.b=255; }
    else if (strcmp(str,"gray")==0||strcmp(str,"grey")==0) { c.r=c.g=c.b=128; }
    return c;
}

// ============================================================
// パスコマンド
// ============================================================

enum PathOp { PathMoveTo, PathLineTo, PathCubicTo, PathClose, PathRect, PathArc };
struct PathCmd { PathOp op; float args[7]; };

// ============================================================
// Canvas2D 内部データ — ビットマップ保持型
// ============================================================

struct DrawState {
    Color4 fillStyle   = {0, 0, 0, 255};
    Color4 strokeStyle = {0, 0, 0, 255};
    float lineWidth    = 1.0f;
    float globalAlpha  = 1.0f;
    tvg::StrokeCap lineCap   = tvg::StrokeCap::Butt;
    tvg::StrokeJoin lineJoin = tvg::StrokeJoin::Miter;
    std::string fontName;
    float fontSize = 16.0f;
    std::string textAlign = "left";
    tvg::Matrix transform = {1,0,0, 0,1,0, 0,0,1};
};

struct Canvas2DData {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> pixels;  // ARGB8888 ビットマップバッファ（保持型）
    std::vector<uint8_t> rgbaCache; // RGBA キャッシュ（_getRGBA / data getter 用）
    bool rgbaCacheDirty = true;
    tvg::SwCanvas *canvas = nullptr;
    GLuint glTexture = 0;

    // dirty rect 追跡（更新領域の矩形）
    int dirtyX0 = 0, dirtyY0 = 0, dirtyX1 = 0, dirtyY1 = 0;
    bool hasDirty = false;

    std::vector<PathCmd> pathCmds;
    DrawState state;
    std::vector<DrawState> stateStack;

    ~Canvas2DData() {
        // 蓄積中の Paint を解放
        for (auto *p : pendingPaints) p->unref();
        pendingPaints.clear();
        if (canvas) delete canvas;
        if (glTexture) glDeleteTextures(1, &glTexture);
    }

    // dirty rect を拡張（描画操作のたびに呼ぶ）
    void markDirty(int x, int y, int w, int h) {
        int nx0 = SDL_max(0, x);
        int ny0 = SDL_max(0, y);
        int nx1 = SDL_min((int)width, x + w);
        int ny1 = SDL_min((int)height, y + h);
        if (nx0 >= nx1 || ny0 >= ny1) return;
        if (!hasDirty) {
            dirtyX0 = nx0; dirtyY0 = ny0;
            dirtyX1 = nx1; dirtyY1 = ny1;
            hasDirty = true;
        } else {
            if (nx0 < dirtyX0) dirtyX0 = nx0;
            if (ny0 < dirtyY0) dirtyY0 = ny0;
            if (nx1 > dirtyX1) dirtyX1 = nx1;
            if (ny1 > dirtyY1) dirtyY1 = ny1;
        }
        rgbaCacheDirty = true;
    }

    // 全体を dirty にする
    void markAllDirty() {
        dirtyX0 = 0; dirtyY0 = 0;
        dirtyX1 = (int)width; dirtyY1 = (int)height;
        hasDirty = true;
        rgbaCacheDirty = true;
    }

    bool inFlush = false;  // 再入防止

    // RGBA キャッシュを更新して返す（dirty 時のみ全変換）
    const uint8_t* getRGBACache() {
        if (!inFlush) {
            inFlush = true;
            flushPaints();
            inFlush = false;
        }
        size_t n = width * height;
        if (rgbaCache.size() != n * 4) {
            rgbaCache.resize(n * 4);
            rgbaCacheDirty = true;
        }
        if (!rgbaCacheDirty) return rgbaCache.data();
        for (size_t i = 0; i < n; i++) {
            uint32_t argb = pixels[i];
            rgbaCache[i*4]   = (argb >> 16) & 0xFF; // R
            rgbaCache[i*4+1] = (argb >> 8) & 0xFF;  // G
            rgbaCache[i*4+2] = argb & 0xFF;          // B
            rgbaCache[i*4+3] = (argb >> 24) & 0xFF;  // A
        }
        rgbaCacheDirty = false;
        return rgbaCache.data();
    }

    // dirty 領域のみ GL テクスチャにアップロード
    void flushDirty() {
        if (!hasDirty) return;
        int dx = dirtyX0, dy = dirtyY0;
        int dw = dirtyX1 - dirtyX0, dh = dirtyY1 - dirtyY0;
        if (dw <= 0 || dh <= 0) { hasDirty = false; return; }

        // dirty 領域のみ ARGB → RGBA 変換してアップロード
        std::vector<uint32_t> rgba(dw * dh);
        for (int py = 0; py < dh; py++) {
            for (int px = 0; px < dw; px++) {
                uint32_t argb = pixels[(dy + py) * width + (dx + px)];
                uint8_t a = (argb >> 24) & 0xFF;
                uint8_t r = (argb >> 16) & 0xFF;
                uint8_t g = (argb >> 8) & 0xFF;
                uint8_t b = argb & 0xFF;
                rgba[py * dw + px] = r | (g << 8) | (b << 16) | (a << 24);
            }
        }
        glBindTexture(GL_TEXTURE_2D, glTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, dx, dy, dw, dh,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        hasDirty = false;
    }

    // 蓄積された Paint のリスト（flush 時にまとめて描画）
    std::vector<tvg::Paint*> pendingPaints;
    bool hasPending = false;

    // 描画操作を蓄積（即座には描画しない）
    void addPaint(tvg::Paint *paint) {
        pendingPaints.push_back(paint);
        hasPending = true;
    }

    // 蓄積された描画をまとめてバッファに反映
    void flushPaints() {
        if (!hasPending) return;
        for (auto *p : pendingPaints) {
            canvas->add(p);
        }
        canvas->update();
        canvas->draw(false);
        canvas->sync();
        canvas->remove(); // 全 paint を除去
        pendingPaints.clear();
        hasPending = false;
        markAllDirty();
    }

    // ピクセルバッファの一部を直接クリア（先に蓄積分を反映）
    void clearPixels(int x, int y, int w, int h) {
        flushPaints();
        int x0 = SDL_max(0, x);
        int y0 = SDL_max(0, y);
        int x1 = SDL_min((int)width, x + w);
        int y1 = SDL_min((int)height, y + h);
        for (int py = y0; py < y1; py++) {
            memset(&pixels[py * width + x0], 0, (x1 - x0) * sizeof(uint32_t));
        }
        markDirty(x, y, w, h);
    }

    // ピクセルバッファに直接ビットブリット（スケーリング対応、先に蓄積分を反映）
    void blitPixels(const uint8_t *srcRGBA, int srcW, int srcH,
                    int sx, int sy, int sw, int sh,
                    int dx, int dy, int dw, int dh) {
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
        flushPaints();

        for (int py = 0; py < dh; py++) {
            int dstY = dy + py;
            if (dstY < 0 || dstY >= (int)height) continue;
            // ソース Y をスケーリング計算
            int srcY = sy + (py * sh) / dh;
            if (srcY < 0 || srcY >= srcH) continue;

            for (int px = 0; px < dw; px++) {
                int dstX = dx + px;
                if (dstX < 0 || dstX >= (int)width) continue;
                // ソース X をスケーリング計算
                int srcX = sx + (px * sw) / dw;
                if (srcX < 0 || srcX >= srcW) continue;

                int si = (srcY * srcW + srcX) * 4;
                uint8_t sr = srcRGBA[si], sg = srcRGBA[si+1], sb = srcRGBA[si+2], sa = srcRGBA[si+3];
                if (sa == 0) continue;

                // ARGB8888: (A<<24)|(R<<16)|(G<<8)|B
                uint32_t &dst = pixels[dstY * width + dstX];
                if (sa == 255) {
                    dst = (255u << 24) | ((uint32_t)sr << 16) | ((uint32_t)sg << 8) | sb;
                } else {
                    uint8_t da = (dst >> 24) & 0xFF;
                    uint8_t dr = (dst >> 16) & 0xFF;
                    uint8_t dg = (dst >> 8) & 0xFF;
                    uint8_t db = dst & 0xFF;
                    uint8_t oa = sa + (da * (255 - sa)) / 255;
                    uint8_t or_ = oa ? (uint8_t)((sr * sa + dr * da * (255 - sa) / 255) / oa) : 0;
                    uint8_t og = oa ? (uint8_t)((sg * sa + dg * da * (255 - sa) / 255) / oa) : 0;
                    uint8_t ob = oa ? (uint8_t)((sb * sa + db * da * (255 - sa) / 255) / oa) : 0;
                    dst = ((uint32_t)oa << 24) | ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | ob;
                }
            }
        }
        markDirty(dx, dy, dw, dh);
    }
};

// ============================================================
// QuickJS クラス定義
// ============================================================

static JSClassID js_canvas2d_class_id;

static void js_canvas2d_finalizer(JSRuntime *rt, JSValue val) {
    Canvas2DData *d = (Canvas2DData *)JS_GetOpaque(val, js_canvas2d_class_id);
    delete d;
}

static JSClassDef js_canvas2d_class = {
    "Canvas2D",
    js_canvas2d_finalizer, // finalizer
};

static Canvas2DData* get_data(JSContext *ctx, JSValueConst this_val) {
    return (Canvas2DData *)JS_GetOpaque(this_val, js_canvas2d_class_id);
}

static tvg::Matrix mat_mul(const tvg::Matrix &a, const tvg::Matrix &b) {
    return {
        a.e11*b.e11+a.e12*b.e21+a.e13*b.e31, a.e11*b.e12+a.e12*b.e22+a.e13*b.e32, a.e11*b.e13+a.e12*b.e23+a.e13*b.e33,
        a.e21*b.e11+a.e22*b.e21+a.e23*b.e31, a.e21*b.e12+a.e22*b.e22+a.e23*b.e32, a.e21*b.e13+a.e22*b.e23+a.e23*b.e33,
        a.e31*b.e11+a.e32*b.e21+a.e33*b.e31, a.e31*b.e12+a.e32*b.e22+a.e33*b.e32, a.e31*b.e13+a.e32*b.e23+a.e33*b.e33
    };
}

// 円弧をベジェ曲線で近似
static void shape_arc(tvg::Shape *shape, float cx, float cy, float r, float startDeg, float sweepDeg) {
    if (fabsf(sweepDeg) < 0.001f) return;
    int segments = (int)(fabsf(sweepDeg) / 90.0f) + 1;
    float segRad = (sweepDeg / segments) * (float)(M_PI / 180.0);
    float curAngle = startDeg * (float)(M_PI / 180.0);
    float sx = cx + r * cosf(curAngle), sy = cy + r * sinf(curAngle);
    shape->moveTo(sx, sy);
    for (int i = 0; i < segments; i++) {
        float a1 = curAngle, a2 = curAngle + segRad;
        float alpha = 4.0f * tanf(segRad * 0.25f) / 3.0f;
        float x2 = cx + r*cosf(a2), y2 = cy + r*sinf(a2);
        shape->cubicTo(
            cx + r*cosf(a1) - alpha*r*sinf(a1), cy + r*sinf(a1) + alpha*r*cosf(a1),
            x2 + alpha*r*sinf(a2), y2 - alpha*r*cosf(a2), x2, y2);
        curAngle = a2;
    }
}

static tvg::Shape* build_shape(const std::vector<PathCmd> &cmds) {
    auto *shape = tvg::Shape::gen();
    for (auto &c : cmds) {
        switch (c.op) {
        case PathMoveTo: shape->moveTo(c.args[0], c.args[1]); break;
        case PathLineTo: shape->lineTo(c.args[0], c.args[1]); break;
        case PathCubicTo: shape->cubicTo(c.args[0],c.args[1],c.args[2],c.args[3],c.args[4],c.args[5]); break;
        case PathClose: shape->close(); break;
        case PathRect: shape->appendRect(c.args[0],c.args[1],c.args[2],c.args[3],0,0); break;
        case PathArc: shape_arc(shape, c.args[0],c.args[1],c.args[2],c.args[3],c.args[4]); break;
        }
    }
    return shape;
}

// ============================================================
// 矩形（即座にバッファに描画）
// ============================================================

static JSValue ctx_fillRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double x, y, w, h;
    JS_ToFloat64(ctx, &x, argv[0]); JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]); JS_ToFloat64(ctx, &h, argv[3]);
    auto *shape = tvg::Shape::gen();
    shape->appendRect((float)x,(float)y,(float)w,(float)h,0,0);
    uint8_t a = (uint8_t)(d->state.fillStyle.a * d->state.globalAlpha);
    shape->fill(d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b, a);
    shape->transform(d->state.transform);
    d->addPaint(shape);
    return JS_UNDEFINED;
}

static JSValue ctx_strokeRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double x, y, w, h;
    JS_ToFloat64(ctx, &x, argv[0]); JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]); JS_ToFloat64(ctx, &h, argv[3]);
    auto *shape = tvg::Shape::gen();
    shape->appendRect((float)x,(float)y,(float)w,(float)h,0,0);
    uint8_t a = (uint8_t)(d->state.strokeStyle.a * d->state.globalAlpha);
    shape->strokeFill(d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b, a);
    shape->strokeWidth(d->state.lineWidth);
    shape->transform(d->state.transform);
    d->addPaint(shape);
    return JS_UNDEFINED;
}

static JSValue ctx_clearRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double x, y, w, h;
    JS_ToFloat64(ctx, &x, argv[0]); JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]); JS_ToFloat64(ctx, &h, argv[3]);
    d->clearPixels((int)x, (int)y, (int)w, (int)h);
    return JS_UNDEFINED;
}

// ============================================================
// パス
// ============================================================

static JSValue ctx_beginPath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    get_data(ctx, this_val)->pathCmds.clear();
    return JS_UNDEFINED;
}

static JSValue ctx_moveTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double x, y;
    JS_ToFloat64(ctx, &x, argv[0]); JS_ToFloat64(ctx, &y, argv[1]);
    PathCmd c = {PathMoveTo, {(float)x,(float)y}};
    d->pathCmds.push_back(c);
    return JS_UNDEFINED;
}

static JSValue ctx_lineTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double x, y;
    JS_ToFloat64(ctx, &x, argv[0]); JS_ToFloat64(ctx, &y, argv[1]);
    PathCmd c = {PathLineTo, {(float)x,(float)y}};
    d->pathCmds.push_back(c);
    return JS_UNDEFINED;
}

static JSValue ctx_bezierCurveTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double cp1x, cp1y, cp2x, cp2y, x, y;
    JS_ToFloat64(ctx, &cp1x, argv[0]); JS_ToFloat64(ctx, &cp1y, argv[1]);
    JS_ToFloat64(ctx, &cp2x, argv[2]); JS_ToFloat64(ctx, &cp2y, argv[3]);
    JS_ToFloat64(ctx, &x, argv[4]); JS_ToFloat64(ctx, &y, argv[5]);
    PathCmd c = {PathCubicTo, {(float)cp1x,(float)cp1y,(float)cp2x,(float)cp2y,(float)x,(float)y}};
    d->pathCmds.push_back(c);
    return JS_UNDEFINED;
}

static JSValue ctx_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double x, y, w, h;
    JS_ToFloat64(ctx, &x, argv[0]); JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]); JS_ToFloat64(ctx, &h, argv[3]);
    PathCmd c = {PathRect, {(float)x,(float)y,(float)w,(float)h}};
    d->pathCmds.push_back(c);
    return JS_UNDEFINED;
}

static JSValue ctx_arc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double cx, cy, r, sa, ea;
    JS_ToFloat64(ctx, &cx, argv[0]); JS_ToFloat64(ctx, &cy, argv[1]);
    JS_ToFloat64(ctx, &r, argv[2]);
    JS_ToFloat64(ctx, &sa, argv[3]); JS_ToFloat64(ctx, &ea, argv[4]);
    bool ccw = (argc > 5) ? (JS_ToBool(ctx, argv[5]) != 0) : false;
    float sd = (float)(sa * (180.0/M_PI)), ed = (float)(ea * (180.0/M_PI));
    float sweep = ed - sd;
    if (ccw) { if (sweep > 0) sweep -= 360.0f; } else { if (sweep < 0) sweep += 360.0f; }
    PathCmd c = {PathArc, {(float)cx, (float)cy, (float)r, sd, sweep, 0}};
    d->pathCmds.push_back(c);
    return JS_UNDEFINED;
}

static JSValue ctx_closePath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    PathCmd c = {PathClose, {}};
    get_data(ctx, this_val)->pathCmds.push_back(c);
    return JS_UNDEFINED;
}

static JSValue ctx_fill(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    if (d->pathCmds.empty()) return JS_UNDEFINED;
    auto *shape = build_shape(d->pathCmds);
    uint8_t a = (uint8_t)(d->state.fillStyle.a * d->state.globalAlpha);
    shape->fill(d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b, a);
    shape->transform(d->state.transform);
    d->addPaint(shape);
    return JS_UNDEFINED;
}

static JSValue ctx_stroke(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    if (d->pathCmds.empty()) return JS_UNDEFINED;
    auto *shape = build_shape(d->pathCmds);
    uint8_t a = (uint8_t)(d->state.strokeStyle.a * d->state.globalAlpha);
    shape->strokeFill(d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b, a);
    shape->strokeWidth(d->state.lineWidth);
    shape->transform(d->state.transform);
    d->addPaint(shape);
    return JS_UNDEFINED;
}

// ============================================================
// テキスト
// ============================================================

static JSValue ctx_fillText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    const char *str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_EXCEPTION;
    double x, y;
    JS_ToFloat64(ctx, &x, argv[1]); JS_ToFloat64(ctx, &y, argv[2]);
    auto *text = tvg::Text::gen();
    // フォント未ロード時はスキップ（ThorVG ハング回避）
    if (text->font(d->state.fontName.c_str()) != tvg::Result::Success) {
        text->unref();
        JS_FreeCString(ctx, str);
        return JS_UNDEFINED;
    }
    text->size(d->state.fontSize);
    text->text(str);
    text->fill(d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b);
    text->opacity((uint8_t)(d->state.fillStyle.a * d->state.globalAlpha));
    tvg::Matrix t = {1,0,(float)x, 0,1,(float)y - d->state.fontSize * 0.85f, 0,0,1};
    text->transform(mat_mul(d->state.transform, t));
    d->addPaint(text);
    JS_FreeCString(ctx, str);
    return JS_UNDEFINED;
}

static JSValue ctx_strokeText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    const char *str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_EXCEPTION;
    double x, y;
    JS_ToFloat64(ctx, &x, argv[1]); JS_ToFloat64(ctx, &y, argv[2]);
    auto *text = tvg::Text::gen();
    if (text->font(d->state.fontName.c_str()) != tvg::Result::Success) {
        text->unref();
        JS_FreeCString(ctx, str);
        return JS_UNDEFINED;
    }
    text->size(d->state.fontSize);
    text->text(str);
    text->outline(d->state.lineWidth, d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b);
    text->opacity((uint8_t)(d->state.strokeStyle.a * d->state.globalAlpha));
    tvg::Matrix t = {1,0,(float)x, 0,1,(float)y - d->state.fontSize * 0.85f, 0,0,1};
    text->transform(mat_mul(d->state.transform, t));
    d->addPaint(text);
    JS_FreeCString(ctx, str);
    return JS_UNDEFINED;
}

static JSValue ctx_measureText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    const char *str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_EXCEPTION;
    float estimatedWidth = (float)strlen(str) * d->state.fontSize * 0.6f;
    JS_FreeCString(ctx, str);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "width", JS_NewFloat64(ctx, estimatedWidth));
    return obj;
}

// ============================================================
// drawImage (ThorVG Picture ベース)
// ============================================================

// ソースの RGBA データから Canvas2DData のバッファ上に描画する
static void drawImageViaPicture(Canvas2DData *d,
    const uint8_t *srcRGBA, int srcW, int srcH,
    int sx, int sy, int sw, int sh,
    int dx, int dy, int dw, int dh)
{
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (!srcRGBA || srcW <= 0 || srcH <= 0) return;

    // 切り出しが必要な場合は切り出し済みデータを作成
    const uint32_t *pixelData = nullptr;
    int loadW = srcW, loadH = srcH;
    std::vector<uint32_t> clipped;

    bool needClip = (sx != 0 || sy != 0 || sw != srcW || sh != srcH);
    if (needClip) {
        int cx0 = SDL_max(0, sx), cy0 = SDL_max(0, sy);
        int cx1 = SDL_min(srcW, sx + sw), cy1 = SDL_min(srcH, sy + sh);
        int cw = cx1 - cx0, ch = cy1 - cy0;
        if (cw <= 0 || ch <= 0) return;

        clipped.resize(cw * ch);
        for (int py = 0; py < ch; py++) {
            for (int px = 0; px < cw; px++) {
                int si = ((cy0 + py) * srcW + (cx0 + px)) * 4;
                uint8_t r = srcRGBA[si], g = srcRGBA[si+1], b = srcRGBA[si+2], a = srcRGBA[si+3];
                clipped[py * cw + px] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
            }
        }
        pixelData = clipped.data();
        loadW = cw; loadH = ch;
    } else {
        clipped.resize(srcW * srcH);
        for (int i = 0; i < srcW * srcH; i++) {
            int si = i * 4;
            uint8_t r = srcRGBA[si], g = srcRGBA[si+1], b = srcRGBA[si+2], a = srcRGBA[si+3];
            clipped[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }
        pixelData = clipped.data();
    }

    // 蓄積描画時は blitPixels にフォールバック（Picture + addPaint の問題回避）
    d->blitPixels(srcRGBA, srcW, srcH, sx, sy, sw, sh, dx, dy, dw, dh);
}

static JSValue ctx_drawImage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);

    // source オブジェクトからピクセルデータをローカルにコピー
    std::vector<uint8_t> srcCopy;
    int srcW = 0, srcH = 0;

    if (JS_IsObject(argv[0])) {
        JSValue wVal = JS_GetPropertyStr(ctx, argv[0], "width");
        JS_ToInt32(ctx, &srcW, wVal); JS_FreeValue(ctx, wVal);
        JSValue hVal = JS_GetPropertyStr(ctx, argv[0], "height");
        JS_ToInt32(ctx, &srcH, hVal); JS_FreeValue(ctx, hVal);

        // data (ImageBitmap / createImageBitmap) → _data (Image シム) の順に試行
        size_t bufSz = 0;
        uint8_t *bufPtr = nullptr;

        JSValue dataVal = JS_GetPropertyStr(ctx, argv[0], "data");
        bufPtr = JS_GetArrayBuffer(ctx, &bufSz, dataVal);
        if (!bufPtr) {
            JS_FreeValue(ctx, dataVal);
            dataVal = JS_GetPropertyStr(ctx, argv[0], "_data");
            bufPtr = JS_GetArrayBuffer(ctx, &bufSz, dataVal);
        }
        // ローカルにコピー（GC によるポインタ無効化を防止）
        if (bufPtr && bufSz >= (size_t)(srcW * srcH * 4)) {
            srcCopy.assign(bufPtr, bufPtr + srcW * srcH * 4);
        }
        JS_FreeValue(ctx, dataVal);
    }
    if (srcCopy.empty() || srcW <= 0 || srcH <= 0) return JS_UNDEFINED;

    int sx=0, sy=0, sw=srcW, sh=srcH;
    int dx=0, dy=0, dw=srcW, dh=srcH;

    if (argc >= 9) {
        double v;
        JS_ToFloat64(ctx, &v, argv[1]); sx=(int)v;
        JS_ToFloat64(ctx, &v, argv[2]); sy=(int)v;
        JS_ToFloat64(ctx, &v, argv[3]); sw=(int)v;
        JS_ToFloat64(ctx, &v, argv[4]); sh=(int)v;
        JS_ToFloat64(ctx, &v, argv[5]); dx=(int)v;
        JS_ToFloat64(ctx, &v, argv[6]); dy=(int)v;
        JS_ToFloat64(ctx, &v, argv[7]); dw=(int)v;
        JS_ToFloat64(ctx, &v, argv[8]); dh=(int)v;
    } else if (argc >= 5) {
        double v;
        JS_ToFloat64(ctx, &v, argv[1]); dx=(int)v;
        JS_ToFloat64(ctx, &v, argv[2]); dy=(int)v;
        JS_ToFloat64(ctx, &v, argv[3]); dw=(int)v;
        JS_ToFloat64(ctx, &v, argv[4]); dh=(int)v;
    } else {
        double v;
        JS_ToFloat64(ctx, &v, argv[1]); dx=(int)v;
        JS_ToFloat64(ctx, &v, argv[2]); dy=(int)v;
    }

    drawImageViaPicture(d, srcCopy.data(), srcW, srcH, sx, sy, sw, sh, dx, dy, dw, dh);
    return JS_UNDEFINED;
}

// ============================================================
// getImageData / putImageData
// ============================================================

static JSValue ctx_getImageData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    d->flushPaints(); // 蓄積分を先にバッファに反映
    double xd, yd, wd, hd;
    JS_ToFloat64(ctx, &xd, argv[0]); JS_ToFloat64(ctx, &yd, argv[1]);
    JS_ToFloat64(ctx, &wd, argv[2]); JS_ToFloat64(ctx, &hd, argv[3]);
    int x=(int)xd, y=(int)yd, w=(int)wd, h=(int)hd;

    // ピクセルデータを準備
    size_t sz = (size_t)(w * h * 4);
    std::vector<uint8_t> out(sz);
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int srcX = x + px, srcY = y + py;
            int di = (py * w + px) * 4;
            if (srcX >= 0 && srcX < (int)d->width && srcY >= 0 && srcY < (int)d->height) {
                uint32_t argb = d->pixels[srcY * d->width + srcX];
                out[di]   = (argb >> 16) & 0xFF; // R
                out[di+1] = (argb >> 8) & 0xFF;  // G
                out[di+2] = argb & 0xFF;          // B
                out[di+3] = (argb >> 24) & 0xFF;  // A
            } else {
                out[di] = out[di+1] = out[di+2] = out[di+3] = 0;
            }
        }
    }

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, w));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, h));
    JS_SetPropertyStr(ctx, obj, "data", JS_NewArrayBufferCopy(ctx, out.data(), sz));
    return obj;
}

static JSValue ctx_putImageData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double dxd, dyd;
    JS_ToFloat64(ctx, &dxd, argv[1]); JS_ToFloat64(ctx, &dyd, argv[2]);
    int dx = (int)dxd, dy = (int)dyd;

    if (!JS_IsObject(argv[0])) return JS_UNDEFINED;

    int w = 0, h = 0;
    JSValue wVal = JS_GetPropertyStr(ctx, argv[0], "width");
    JS_ToInt32(ctx, &w, wVal); JS_FreeValue(ctx, wVal);
    JSValue hVal = JS_GetPropertyStr(ctx, argv[0], "height");
    JS_ToInt32(ctx, &h, hVal); JS_FreeValue(ctx, hVal);

    JSValue dataVal = JS_GetPropertyStr(ctx, argv[0], "data");
    size_t sz = 0;
    const uint8_t *src = JS_GetArrayBuffer(ctx, &sz, dataVal);
    JS_FreeValue(ctx, dataVal);
    if (!src) return JS_UNDEFINED;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int dstX = dx + px, dstY = dy + py;
            if (dstX < 0 || dstX >= (int)d->width || dstY < 0 || dstY >= (int)d->height) continue;
            int si = (py * w + px) * 4;
            uint32_t argb = ((uint32_t)src[si+3] << 24) | ((uint32_t)src[si] << 16) |
                            ((uint32_t)src[si+1] << 8) | src[si+2];
            d->pixels[dstY * d->width + dstX] = argb;
        }
    }
    d->markDirty(dx, dy, w, h);
    return JS_UNDEFINED;
}

// ============================================================
// 変換 / 状態
// ============================================================

static JSValue ctx_save(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    d->stateStack.push_back(d->state);
    return JS_UNDEFINED;
}

static JSValue ctx_restore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    if (!d->stateStack.empty()) { d->state = d->stateStack.back(); d->stateStack.pop_back(); }
    return JS_UNDEFINED;
}

static JSValue ctx_translate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double tx, ty;
    JS_ToFloat64(ctx, &tx, argv[0]); JS_ToFloat64(ctx, &ty, argv[1]);
    tvg::Matrix t = {1,0,(float)tx, 0,1,(float)ty, 0,0,1};
    d->state.transform = mat_mul(d->state.transform, t);
    return JS_UNDEFINED;
}

static JSValue ctx_rotate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double a;
    JS_ToFloat64(ctx, &a, argv[0]);
    float c = cosf((float)a), s = sinf((float)a);
    tvg::Matrix r = {c,-s,0, s,c,0, 0,0,1};
    d->state.transform = mat_mul(d->state.transform, r);
    return JS_UNDEFINED;
}

static JSValue ctx_scale(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    double sx, sy;
    JS_ToFloat64(ctx, &sx, argv[0]); JS_ToFloat64(ctx, &sy, argv[1]);
    tvg::Matrix sc = {(float)sx,0,0, 0,(float)sy,0, 0,0,1};
    d->state.transform = mat_mul(d->state.transform, sc);
    return JS_UNDEFINED;
}

// resize: バッファサイズを変更（内容はクリアされる）
static JSValue ctx_resize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    uint32_t w, h;
    JS_ToUint32(ctx, &w, argv[0]);
    JS_ToUint32(ctx, &h, argv[1]);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == d->width && h == d->height) return JS_UNDEFINED;

    // 蓄積分を破棄
    for (auto *p : d->pendingPaints) p->unref();
    d->pendingPaints.clear();
    d->hasPending = false;

    d->width = w;
    d->height = h;
    d->pixels.assign(w * h, 0);
    d->rgbaCache.clear();
    d->rgbaCacheDirty = true;
    d->hasDirty = false;

    // SwCanvas のターゲットを再設定
    d->canvas->target(d->pixels.data(), w, w, h, tvg::ColorSpace::ARGB8888);

    // GL テクスチャも再作成
    if (d->glTexture) {
        glBindTexture(GL_TEXTURE_2D, d->glTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // JS 側の width/height プロパティも更新
    JS_SetPropertyStr(ctx, this_val, "width", JS_NewUint32(ctx, w));
    JS_SetPropertyStr(ctx, this_val, "height", JS_NewUint32(ctx, h));

    return JS_UNDEFINED;
}

static JSValue ctx_setTransform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    if (argc >= 6) {
        double a, b, c, dd, e, f;
        JS_ToFloat64(ctx, &a, argv[0]); JS_ToFloat64(ctx, &b, argv[1]);
        JS_ToFloat64(ctx, &c, argv[2]); JS_ToFloat64(ctx, &dd, argv[3]);
        JS_ToFloat64(ctx, &e, argv[4]); JS_ToFloat64(ctx, &f, argv[5]);
        d->state.transform = {
            (float)a, (float)c, (float)e,
            (float)b, (float)dd, (float)f,
            0, 0, 1
        };
    } else {
        d->state.transform = {1,0,0, 0,1,0, 0,0,1};
    }
    return JS_UNDEFINED;
}

// ============================================================
// flush: バッファ → GL テクスチャアップロード
// ============================================================

// _getRGBA: ARGB8888 premultiplied → RGBA premultiplied に変換して返す
// texImage2D 連携用（pixi.js の UNPACK_PREMULTIPLY_ALPHA と合わせて使う）
static JSValue ctx_getRGBA(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    d->flushPaints();
    size_t n = d->width * d->height;
    size_t sz = n * 4;
    std::vector<uint8_t> out(sz);
    // ARGB → RGBA（premultiplied のまま、バイト順のみ入れ替え）
    for (size_t i = 0; i < n; i++) {
        uint32_t argb = d->pixels[i];
        out[i*4]   = (argb >> 16) & 0xFF; // R
        out[i*4+1] = (argb >> 8) & 0xFF;  // G
        out[i*4+2] = argb & 0xFF;          // B
        out[i*4+3] = (argb >> 24) & 0xFF;  // A
    }
    return JS_NewArrayBufferCopy(ctx, out.data(), sz);
}

static JSValue ctx_flush(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    d->flushPaints();  // 蓄積描画をバッファに反映
    d->flushDirty();   // dirty 領域を GL テクスチャにアップロード
    return JS_UNDEFINED;
}

// ============================================================
// プロパティ getter/setter
// ============================================================

static JSValue ctx_get_fillStyle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val); char buf[32];
    snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)", d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b, d->state.fillStyle.a/255.0f);
    return JS_NewString(ctx, buf);
}

static JSValue ctx_set_fillStyle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *str = JS_ToCString(ctx, argv[0]);
    if (str) { get_data(ctx, this_val)->state.fillStyle = parse_color(str); JS_FreeCString(ctx, str); }
    return JS_UNDEFINED;
}

static JSValue ctx_get_strokeStyle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val); char buf[32];
    snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)", d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b, d->state.strokeStyle.a/255.0f);
    return JS_NewString(ctx, buf);
}

static JSValue ctx_set_strokeStyle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *str = JS_ToCString(ctx, argv[0]);
    if (str) { get_data(ctx, this_val)->state.strokeStyle = parse_color(str); JS_FreeCString(ctx, str); }
    return JS_UNDEFINED;
}

static JSValue ctx_get_lineWidth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewFloat64(ctx, get_data(ctx, this_val)->state.lineWidth);
}

static JSValue ctx_set_lineWidth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    double v; JS_ToFloat64(ctx, &v, argv[0]);
    get_data(ctx, this_val)->state.lineWidth = (float)v;
    return JS_UNDEFINED;
}

static JSValue ctx_get_globalAlpha(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewFloat64(ctx, get_data(ctx, this_val)->state.globalAlpha);
}

static JSValue ctx_set_globalAlpha(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    double v; JS_ToFloat64(ctx, &v, argv[0]);
    get_data(ctx, this_val)->state.globalAlpha = (float)v;
    return JS_UNDEFINED;
}

static JSValue ctx_get_font(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val); char buf[256];
    snprintf(buf, sizeof(buf), "%.0fpx %s", d->state.fontSize, d->state.fontName.c_str());
    return JS_NewString(ctx, buf);
}

static JSValue ctx_set_font(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    const char *str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_UNDEFINED;
    float size = 16.0f; char name[256] = "";
    if (sscanf(str, "%fpx %255[^\n]", &size, name) >= 1) { d->state.fontSize = size; if (name[0]) d->state.fontName = name; }
    JS_FreeCString(ctx, str);
    return JS_UNDEFINED;
}

static JSValue ctx_get_textAlign(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewString(ctx, get_data(ctx, this_val)->state.textAlign.c_str());
}

static JSValue ctx_set_textAlign(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *str = JS_ToCString(ctx, argv[0]);
    if (str) { get_data(ctx, this_val)->state.textAlign = str; JS_FreeCString(ctx, str); }
    return JS_UNDEFINED;
}

static JSValue ctx_get_lineCap(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val); const char *v = "butt";
    if (d->state.lineCap == tvg::StrokeCap::Round) v = "round";
    else if (d->state.lineCap == tvg::StrokeCap::Square) v = "square";
    return JS_NewString(ctx, v);
}

static JSValue ctx_set_lineCap(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    const char *v = JS_ToCString(ctx, argv[0]);
    if (!v) return JS_UNDEFINED;
    if (strcmp(v,"round")==0) d->state.lineCap = tvg::StrokeCap::Round;
    else if (strcmp(v,"square")==0) d->state.lineCap = tvg::StrokeCap::Square;
    else d->state.lineCap = tvg::StrokeCap::Butt;
    JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}

static JSValue ctx_get_lineJoin(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val); const char *v = "miter";
    if (d->state.lineJoin == tvg::StrokeJoin::Round) v = "round";
    else if (d->state.lineJoin == tvg::StrokeJoin::Bevel) v = "bevel";
    return JS_NewString(ctx, v);
}

static JSValue ctx_set_lineJoin(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    const char *v = JS_ToCString(ctx, argv[0]);
    if (!v) return JS_UNDEFINED;
    if (strcmp(v,"round")==0) d->state.lineJoin = tvg::StrokeJoin::Round;
    else if (strcmp(v,"bevel")==0) d->state.lineJoin = tvg::StrokeJoin::Bevel;
    else d->state.lineJoin = tvg::StrokeJoin::Miter;
    JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}

static JSValue ctx_get_texture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *d = get_data(ctx, this_val);
    // テクスチャ取得時に蓄積描画 + dirty 領域を自動フラッシュ
    d->flushPaints();
    d->flushDirty();
    if (d->glTexture == 0) return JS_NULL;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_id", JS_NewUint32(ctx, d->glTexture));
    return obj;
}

// globalCompositeOperation (ダミー)
static JSValue ctx_get_globalCompositeOperation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewString(ctx, "source-over");
}

static JSValue ctx_set_globalCompositeOperation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// imageSmoothingEnabled (ダミー)
static JSValue ctx_get_imageSmoothingEnabled(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewBool(ctx, 1);
}

static JSValue ctx_set_imageSmoothingEnabled(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// Canvas2D.loadFont(path) / Canvas2D.loadFont(path, alias)
static JSValue static_loadFont(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    const char *alias = nullptr;
    if (argc > 1) {
        alias = JS_ToCString(ctx, argv[1]);
    }

    JsEngine *engine = JsEngine::getInstance();
    std::string resolved = engine ? engine->resolvePath(path) : path;
    JS_FreeCString(ctx, path);

    if (alias) {
        // SDL_LoadFile でフォントデータを読み込み、alias 名で ThorVG に登録
        size_t dataSize = 0;
        void *data = SDL_LoadFile(resolved.c_str(), &dataSize);
        if (!data) {
            JS_FreeCString(ctx, alias);
            return JS_ThrowInternalError(ctx, "Failed to load font file: %s", resolved.c_str());
        }
        auto result = tvg::Text::load(alias, (const char*)data, (uint32_t)dataSize, "ttf", true);
        SDL_free(data);
        if (result != tvg::Result::Success) {
            const char *a = alias; // save before free
            JS_FreeCString(ctx, alias);
            return JS_ThrowInternalError(ctx, "Failed to register font from: %s", resolved.c_str());
        }
        SDL_Log("Font loaded: %s as '%s'", resolved.c_str(), alias);
        JS_FreeCString(ctx, alias);
    } else {
        // ファイルパスでロード（ThorVG 内部名で登録）
        auto result = tvg::Text::load(resolved.c_str());
        if (result != tvg::Result::Success) {
            return JS_ThrowInternalError(ctx, "Failed to load font: %s", resolved.c_str());
        }
        SDL_Log("Font loaded: %s", resolved.c_str());
    }
    return JS_UNDEFINED;
}

// ============================================================
// コンストラクタ
// ============================================================

// getter/setter を定義するヘルパーマクロ
#define DEFINE_GETSET(obj, name, getter, setter) \
    do { \
        JSAtom a_ = JS_NewAtom(ctx, name); \
        JS_DefinePropertyGetSet(ctx, obj, a_, \
            JS_NewCFunction(ctx, getter, "get " name, 0), \
            JS_NewCFunction(ctx, setter, "set " name, 1), \
            JS_PROP_ENUMERABLE); \
        JS_FreeAtom(ctx, a_); \
    } while(0)

// getter のみ定義するヘルパーマクロ
#define DEFINE_GETTER(obj, name, getter) \
    do { \
        JSAtom a_ = JS_NewAtom(ctx, name); \
        JS_DefinePropertyGetSet(ctx, obj, a_, \
            JS_NewCFunction(ctx, getter, "get " name, 0), \
            JS_UNDEFINED, \
            JS_PROP_ENUMERABLE); \
        JS_FreeAtom(ctx, a_); \
    } while(0)

static JSValue canvas2d_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    uint32_t w, h;
    JS_ToUint32(ctx, &w, argv[0]);
    JS_ToUint32(ctx, &h, argv[1]);

    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_canvas2d_class_id);
    JS_FreeValue(ctx, proto);

    auto *data = new Canvas2DData();
    data->width = w; data->height = h;
    data->pixels.resize(w * h, 0);
    // EngineOption::None で dirty region（部分描画最適化）を無効化する。
    data->canvas = tvg::SwCanvas::gen(tvg::EngineOption::None);
    // ARGB8888 (premultiplied) を使用
    data->canvas->target(data->pixels.data(), w, w, h, tvg::ColorSpace::ARGB8888);

    glGenTextures(1, &data->glTexture);
    glBindTexture(GL_TEXTURE_2D, data->glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    JS_SetOpaque(obj, data);

    // width / height プロパティ
    JS_SetPropertyStr(ctx, obj, "width", JS_NewUint32(ctx, w));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewUint32(ctx, h));

    // メソッド登録
    JS_SetPropertyStr(ctx, obj, "fillRect", JS_NewCFunction(ctx, ctx_fillRect, "fillRect", 4));
    JS_SetPropertyStr(ctx, obj, "strokeRect", JS_NewCFunction(ctx, ctx_strokeRect, "strokeRect", 4));
    JS_SetPropertyStr(ctx, obj, "clearRect", JS_NewCFunction(ctx, ctx_clearRect, "clearRect", 4));
    JS_SetPropertyStr(ctx, obj, "beginPath", JS_NewCFunction(ctx, ctx_beginPath, "beginPath", 0));
    JS_SetPropertyStr(ctx, obj, "moveTo", JS_NewCFunction(ctx, ctx_moveTo, "moveTo", 2));
    JS_SetPropertyStr(ctx, obj, "lineTo", JS_NewCFunction(ctx, ctx_lineTo, "lineTo", 2));
    JS_SetPropertyStr(ctx, obj, "bezierCurveTo", JS_NewCFunction(ctx, ctx_bezierCurveTo, "bezierCurveTo", 6));
    JS_SetPropertyStr(ctx, obj, "rect", JS_NewCFunction(ctx, ctx_rect, "rect", 4));
    JS_SetPropertyStr(ctx, obj, "arc", JS_NewCFunction(ctx, ctx_arc, "arc", 6));
    JS_SetPropertyStr(ctx, obj, "closePath", JS_NewCFunction(ctx, ctx_closePath, "closePath", 0));
    JS_SetPropertyStr(ctx, obj, "fill", JS_NewCFunction(ctx, ctx_fill, "fill", 0));
    JS_SetPropertyStr(ctx, obj, "stroke", JS_NewCFunction(ctx, ctx_stroke, "stroke", 0));
    JS_SetPropertyStr(ctx, obj, "fillText", JS_NewCFunction(ctx, ctx_fillText, "fillText", 3));
    JS_SetPropertyStr(ctx, obj, "strokeText", JS_NewCFunction(ctx, ctx_strokeText, "strokeText", 3));
    JS_SetPropertyStr(ctx, obj, "measureText", JS_NewCFunction(ctx, ctx_measureText, "measureText", 1));
    JS_SetPropertyStr(ctx, obj, "drawImage", JS_NewCFunction(ctx, ctx_drawImage, "drawImage", 9));
    JS_SetPropertyStr(ctx, obj, "getImageData", JS_NewCFunction(ctx, ctx_getImageData, "getImageData", 4));
    JS_SetPropertyStr(ctx, obj, "putImageData", JS_NewCFunction(ctx, ctx_putImageData, "putImageData", 3));
    JS_SetPropertyStr(ctx, obj, "save", JS_NewCFunction(ctx, ctx_save, "save", 0));
    JS_SetPropertyStr(ctx, obj, "restore", JS_NewCFunction(ctx, ctx_restore, "restore", 0));
    JS_SetPropertyStr(ctx, obj, "translate", JS_NewCFunction(ctx, ctx_translate, "translate", 2));
    JS_SetPropertyStr(ctx, obj, "rotate", JS_NewCFunction(ctx, ctx_rotate, "rotate", 1));
    JS_SetPropertyStr(ctx, obj, "scale", JS_NewCFunction(ctx, ctx_scale, "scale", 2));
    JS_SetPropertyStr(ctx, obj, "setTransform", JS_NewCFunction(ctx, ctx_setTransform, "setTransform", 6));
    JS_SetPropertyStr(ctx, obj, "flush", JS_NewCFunction(ctx, ctx_flush, "flush", 0));
    JS_SetPropertyStr(ctx, obj, "_getRGBA", JS_NewCFunction(ctx, ctx_getRGBA, "_getRGBA", 0));
    JS_SetPropertyStr(ctx, obj, "_resize", JS_NewCFunction(ctx, ctx_resize, "_resize", 2));

    // getter/setter プロパティ
    DEFINE_GETSET(obj, "fillStyle", ctx_get_fillStyle, ctx_set_fillStyle);
    DEFINE_GETSET(obj, "strokeStyle", ctx_get_strokeStyle, ctx_set_strokeStyle);
    DEFINE_GETSET(obj, "lineWidth", ctx_get_lineWidth, ctx_set_lineWidth);
    DEFINE_GETSET(obj, "globalAlpha", ctx_get_globalAlpha, ctx_set_globalAlpha);
    DEFINE_GETSET(obj, "font", ctx_get_font, ctx_set_font);
    DEFINE_GETSET(obj, "textAlign", ctx_get_textAlign, ctx_set_textAlign);
    DEFINE_GETSET(obj, "lineCap", ctx_get_lineCap, ctx_set_lineCap);
    DEFINE_GETSET(obj, "lineJoin", ctx_get_lineJoin, ctx_set_lineJoin);
    DEFINE_GETSET(obj, "globalCompositeOperation", ctx_get_globalCompositeOperation, ctx_set_globalCompositeOperation);
    DEFINE_GETSET(obj, "imageSmoothingEnabled", ctx_get_imageSmoothingEnabled, ctx_set_imageSmoothingEnabled);

    // texture getter (read-only)
    DEFINE_GETTER(obj, "texture", ctx_get_texture);

    return obj;
}

#undef DEFINE_GETSET
#undef DEFINE_GETTER

// ============================================================
// バインディング登録
// ============================================================

void canvas2d_bind(JSContext *ctx) {
    // Canvas2D クラスを登録
    JS_NewClassID(JS_GetRuntime(ctx), &js_canvas2d_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_canvas2d_class_id, &js_canvas2d_class);

    // コンストラクタをグローバルに登録
    JSValue ctor = JS_NewCFunction2(ctx, canvas2d_constructor, "Canvas2D", 2, JS_CFUNC_constructor, 0);

    // static メソッド: Canvas2D.loadFont(path [, alias])
    JS_SetPropertyStr(ctx, ctor, "loadFont", JS_NewCFunction(ctx, static_loadFont, "loadFont", 2));

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "Canvas2D", ctor);
    JS_FreeValue(ctx, global);
}
