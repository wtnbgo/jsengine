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
#include <duktape.h>
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
    tvg::SwCanvas *canvas = nullptr;
    GLuint glTexture = 0;

    // dirty rect 追跡（更新領域の矩形）
    int dirtyX0 = 0, dirtyY0 = 0, dirtyX1 = 0, dirtyY1 = 0;
    bool hasDirty = false;

    std::vector<PathCmd> pathCmds;
    DrawState state;
    std::vector<DrawState> stateStack;

    ~Canvas2DData() {
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
    }

    // 全体を dirty にする
    void markAllDirty() {
        dirtyX0 = 0; dirtyY0 = 0;
        dirtyX1 = (int)width; dirtyY1 = (int)height;
        hasDirty = true;
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

    // ThorVG で1つのシェイプを描画してバッファに合成
    void renderShape(tvg::Shape *shape) {
        canvas->add(shape);
        canvas->update();
        canvas->draw(false);
        canvas->sync();
        canvas->remove(shape);
        // shape の bounds から dirty 領域を推定（全体を dirty にする簡易版）
        markAllDirty();
    }

    // ThorVG で Text を描画してバッファに合成
    void renderText(tvg::Text *text) {
        canvas->add(text);
        canvas->update();
        canvas->draw(false);
        canvas->sync();
        canvas->remove(text);
        markAllDirty();
    }

    // ピクセルバッファの一部を直接クリア
    void clearPixels(int x, int y, int w, int h) {
        int x0 = SDL_max(0, x);
        int y0 = SDL_max(0, y);
        int x1 = SDL_min((int)width, x + w);
        int y1 = SDL_min((int)height, y + h);
        for (int py = y0; py < y1; py++) {
            memset(&pixels[py * width + x0], 0, (x1 - x0) * sizeof(uint32_t));
        }
        markDirty(x, y, w, h);
    }

    // ピクセルバッファに直接ビットブリット（スケーリング対応）
    void blitPixels(const uint8_t *srcRGBA, int srcW, int srcH,
                    int sx, int sy, int sw, int sh,
                    int dx, int dy, int dw, int dh) {
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

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

static Canvas2DData* get_data(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "\xff" "data");
    auto *d = (Canvas2DData*)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);
    return d;
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

static duk_ret_t ctx_fillRect(duk_context *ctx) {
    auto *d = get_data(ctx);
    float x=(float)duk_get_number(ctx,0), y=(float)duk_get_number(ctx,1);
    float w=(float)duk_get_number(ctx,2), h=(float)duk_get_number(ctx,3);
    auto *shape = tvg::Shape::gen();
    shape->appendRect(x,y,w,h,0,0);
    uint8_t a = (uint8_t)(d->state.fillStyle.a * d->state.globalAlpha);
    shape->fill(d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b, a);
    shape->transform(d->state.transform);
    d->renderShape(shape);
    return 0;
}

static duk_ret_t ctx_strokeRect(duk_context *ctx) {
    auto *d = get_data(ctx);
    float x=(float)duk_get_number(ctx,0), y=(float)duk_get_number(ctx,1);
    float w=(float)duk_get_number(ctx,2), h=(float)duk_get_number(ctx,3);
    auto *shape = tvg::Shape::gen();
    shape->appendRect(x,y,w,h,0,0);
    uint8_t a = (uint8_t)(d->state.strokeStyle.a * d->state.globalAlpha);
    shape->strokeFill(d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b, a);
    shape->strokeWidth(d->state.lineWidth);
    shape->transform(d->state.transform);
    d->renderShape(shape);
    return 0;
}

static duk_ret_t ctx_clearRect(duk_context *ctx) {
    auto *d = get_data(ctx);
    int x=(int)duk_get_number(ctx,0), y=(int)duk_get_number(ctx,1);
    int w=(int)duk_get_number(ctx,2), h=(int)duk_get_number(ctx,3);
    d->clearPixels(x, y, w, h);
    return 0;
}

// ============================================================
// パス
// ============================================================

static duk_ret_t ctx_beginPath(duk_context *ctx) { get_data(ctx)->pathCmds.clear(); return 0; }
static duk_ret_t ctx_moveTo(duk_context *ctx) {
    auto *d = get_data(ctx);
    PathCmd c = {PathMoveTo, {(float)duk_get_number(ctx,0),(float)duk_get_number(ctx,1)}};
    d->pathCmds.push_back(c); return 0;
}
static duk_ret_t ctx_lineTo(duk_context *ctx) {
    auto *d = get_data(ctx);
    PathCmd c = {PathLineTo, {(float)duk_get_number(ctx,0),(float)duk_get_number(ctx,1)}};
    d->pathCmds.push_back(c); return 0;
}
static duk_ret_t ctx_bezierCurveTo(duk_context *ctx) {
    auto *d = get_data(ctx);
    PathCmd c = {PathCubicTo, {(float)duk_get_number(ctx,0),(float)duk_get_number(ctx,1),
        (float)duk_get_number(ctx,2),(float)duk_get_number(ctx,3),
        (float)duk_get_number(ctx,4),(float)duk_get_number(ctx,5)}};
    d->pathCmds.push_back(c); return 0;
}
static duk_ret_t ctx_rect(duk_context *ctx) {
    auto *d = get_data(ctx);
    PathCmd c = {PathRect, {(float)duk_get_number(ctx,0),(float)duk_get_number(ctx,1),
        (float)duk_get_number(ctx,2),(float)duk_get_number(ctx,3)}};
    d->pathCmds.push_back(c); return 0;
}
static duk_ret_t ctx_arc(duk_context *ctx) {
    auto *d = get_data(ctx);
    float cx=(float)duk_get_number(ctx,0), cy=(float)duk_get_number(ctx,1), r=(float)duk_get_number(ctx,2);
    float sa=(float)duk_get_number(ctx,3), ea=(float)duk_get_number(ctx,4);
    bool ccw = duk_get_top(ctx) > 5 ? (duk_get_boolean(ctx,5)!=0) : false;
    float sd = sa*(float)(180.0/M_PI), ed = ea*(float)(180.0/M_PI);
    float sweep = ed - sd;
    if (ccw) { if (sweep > 0) sweep -= 360.0f; } else { if (sweep < 0) sweep += 360.0f; }
    PathCmd c = {PathArc, {cx, cy, r, sd, sweep, 0}};
    d->pathCmds.push_back(c); return 0;
}
static duk_ret_t ctx_closePath(duk_context *ctx) {
    PathCmd c = {PathClose, {}}; get_data(ctx)->pathCmds.push_back(c); return 0;
}
static duk_ret_t ctx_fill(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (d->pathCmds.empty()) return 0;
    auto *shape = build_shape(d->pathCmds);
    uint8_t a = (uint8_t)(d->state.fillStyle.a * d->state.globalAlpha);
    shape->fill(d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b, a);
    shape->transform(d->state.transform);
    d->renderShape(shape);
    return 0;
}
static duk_ret_t ctx_stroke(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (d->pathCmds.empty()) return 0;
    auto *shape = build_shape(d->pathCmds);
    uint8_t a = (uint8_t)(d->state.strokeStyle.a * d->state.globalAlpha);
    shape->strokeFill(d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b, a);
    shape->strokeWidth(d->state.lineWidth);
    shape->transform(d->state.transform);
    d->renderShape(shape);
    return 0;
}

// ============================================================
// テキスト
// ============================================================

static duk_ret_t ctx_fillText(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *str = duk_require_string(ctx, 0);
    float x = (float)duk_get_number(ctx, 1), y = (float)duk_get_number(ctx, 2);
    auto *text = tvg::Text::gen();
    // フォント未ロード時はスキップ（ThorVG ハング回避）
    if (text->font(d->state.fontName.c_str()) != tvg::Result::Success) {
        text->unref();
        return 0;
    }
    text->size(d->state.fontSize);
    text->text(str);
    text->fill(d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b);
    text->opacity((uint8_t)(d->state.fillStyle.a * d->state.globalAlpha));
    tvg::Matrix t = {1,0,x, 0,1,y - d->state.fontSize * 0.85f, 0,0,1};
    text->transform(mat_mul(d->state.transform, t));
    d->renderText(text);
    return 0;
}

static duk_ret_t ctx_strokeText(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *str = duk_require_string(ctx, 0);
    float x = (float)duk_get_number(ctx, 1), y = (float)duk_get_number(ctx, 2);
    auto *text = tvg::Text::gen();
    if (text->font(d->state.fontName.c_str()) != tvg::Result::Success) {
        text->unref();
        return 0;
    }
    text->size(d->state.fontSize);
    text->text(str);
    text->outline(d->state.lineWidth, d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b);
    text->opacity((uint8_t)(d->state.strokeStyle.a * d->state.globalAlpha));
    tvg::Matrix t = {1,0,x, 0,1,y - d->state.fontSize * 0.85f, 0,0,1};
    text->transform(mat_mul(d->state.transform, t));
    d->renderText(text);
    return 0;
}

static duk_ret_t ctx_measureText(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *str = duk_require_string(ctx, 0);
    float estimatedWidth = (float)strlen(str) * d->state.fontSize * 0.6f;
    duk_idx_t obj = duk_push_object(ctx);
    duk_push_number(ctx, estimatedWidth);
    duk_put_prop_string(ctx, obj, "width");
    return 1;
}

// ============================================================
// drawImage (ThorVG Picture ベース)
// ============================================================

// ソースの RGBA データから Canvas2DData のバッファ上に描画する
// ThorVG Picture でロードし、translate/scale/clip で位置・サイズ・切り出しを制御
static void drawImageViaPicture(Canvas2DData *d,
    const uint8_t *srcRGBA, int srcW, int srcH,
    int sx, int sy, int sw, int sh,
    int dx, int dy, int dw, int dh)
{
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (!srcRGBA || srcW <= 0 || srcH <= 0) return;

    // 切り出しが必要な場合（sx,sy != 0 or sw,sh != srcW,srcH）は
    // 切り出し済みのピクセルデータを作成する
    const uint32_t *pixelData = nullptr;
    int loadW = srcW, loadH = srcH;
    std::vector<uint32_t> clipped;

    bool needClip = (sx != 0 || sy != 0 || sw != srcW || sh != srcH);
    if (needClip) {
        // 切り出し範囲をクランプ
        int cx0 = SDL_max(0, sx), cy0 = SDL_max(0, sy);
        int cx1 = SDL_min(srcW, sx + sw), cy1 = SDL_min(srcH, sy + sh);
        int cw = cx1 - cx0, ch = cy1 - cy0;
        if (cw <= 0 || ch <= 0) return;

        clipped.resize(cw * ch);
        for (int py = 0; py < ch; py++) {
            for (int px = 0; px < cw; px++) {
                int si = ((cy0 + py) * srcW + (cx0 + px)) * 4;
                uint8_t r = srcRGBA[si], g = srcRGBA[si+1], b = srcRGBA[si+2], a = srcRGBA[si+3];
                // ABGR8888S (straight alpha) for ThorVG
                clipped[py * cw + px] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
            }
        }
        pixelData = clipped.data();
        loadW = cw; loadH = ch;
    } else {
        // RGBA → ABGR8888S 変換
        clipped.resize(srcW * srcH);
        for (int i = 0; i < srcW * srcH; i++) {
            int si = i * 4;
            uint8_t r = srcRGBA[si], g = srcRGBA[si+1], b = srcRGBA[si+2], a = srcRGBA[si+3];
            clipped[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }
        pixelData = clipped.data();
    }

    auto *pic = tvg::Picture::gen();
    if (pic->load(pixelData, loadW, loadH, tvg::ColorSpace::ABGR8888S, true) != tvg::Result::Success) {
        // フォールバック: blitPixels
        d->blitPixels(srcRGBA, srcW, srcH, sx, sy, sw, sh, dx, dy, dw, dh);
        pic->unref();
        return;
    }

    // スケーリング + 位置指定
    float scaleX = (float)dw / loadW;
    float scaleY = (float)dh / loadH;
    tvg::Matrix m = {scaleX, 0, (float)dx, 0, scaleY, (float)dy, 0, 0, 1};
    pic->transform(m);
    pic->opacity((uint8_t)(d->state.globalAlpha * 255));

    d->canvas->add(pic);
    d->canvas->update();
    d->canvas->draw(false);
    d->canvas->sync();
    d->canvas->remove(pic);
    d->markDirty(dx, dy, dw, dh);
}

static duk_ret_t ctx_drawImage(duk_context *ctx) {
    auto *d = get_data(ctx);
    int argc = duk_get_top(ctx);

    // source オブジェクトからピクセルデータを取得
    const uint8_t *srcData = nullptr;
    int srcW = 0, srcH = 0;

    if (duk_is_object(ctx, 0)) {
        duk_get_prop_string(ctx, 0, "width");
        srcW = duk_to_int(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "height");
        srcH = duk_to_int(ctx, -1); duk_pop(ctx);

        // data (ImageBitmap / createImageBitmap)
        if (duk_has_prop_string(ctx, 0, "data")) {
            duk_get_prop_string(ctx, 0, "data");
            duk_size_t sz = 0;
            srcData = (const uint8_t*)duk_get_buffer_data(ctx, -1, &sz);
            duk_pop(ctx);
        }
        // _data (Image シム)
        if (!srcData && duk_has_prop_string(ctx, 0, "_data")) {
            duk_get_prop_string(ctx, 0, "_data");
            duk_size_t sz = 0;
            srcData = (const uint8_t*)duk_get_buffer_data(ctx, -1, &sz);
            duk_pop(ctx);
        }
    }
    if (!srcData || srcW <= 0 || srcH <= 0) return 0;

    int sx=0, sy=0, sw=srcW, sh=srcH;
    int dx=0, dy=0, dw=srcW, dh=srcH;

    if (argc >= 9) {
        sx=(int)duk_get_number(ctx,1); sy=(int)duk_get_number(ctx,2);
        sw=(int)duk_get_number(ctx,3); sh=(int)duk_get_number(ctx,4);
        dx=(int)duk_get_number(ctx,5); dy=(int)duk_get_number(ctx,6);
        dw=(int)duk_get_number(ctx,7); dh=(int)duk_get_number(ctx,8);
    } else if (argc >= 5) {
        dx=(int)duk_get_number(ctx,1); dy=(int)duk_get_number(ctx,2);
        dw=(int)duk_get_number(ctx,3); dh=(int)duk_get_number(ctx,4);
    } else {
        dx=(int)duk_get_number(ctx,1); dy=(int)duk_get_number(ctx,2);
    }

    drawImageViaPicture(d, srcData, srcW, srcH, sx, sy, sw, sh, dx, dy, dw, dh);
    return 0;
}

// ============================================================
// getImageData / putImageData
// ============================================================

static duk_ret_t ctx_getImageData(duk_context *ctx) {
    auto *d = get_data(ctx);
    int x=(int)duk_get_number(ctx,0), y=(int)duk_get_number(ctx,1);
    int w=(int)duk_get_number(ctx,2), h=(int)duk_get_number(ctx,3);

    duk_idx_t obj = duk_push_object(ctx);
    duk_push_int(ctx, w); duk_put_prop_string(ctx, obj, "width");
    duk_push_int(ctx, h); duk_put_prop_string(ctx, obj, "height");

    void *buf = duk_push_buffer(ctx, w * h * 4, 0);
    uint8_t *out = (uint8_t*)buf;
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
    duk_put_prop_string(ctx, obj, "data");
    return 1;
}

static duk_ret_t ctx_putImageData(duk_context *ctx) {
    auto *d = get_data(ctx);
    int dx = (int)duk_get_number(ctx, 1), dy = (int)duk_get_number(ctx, 2);

    if (!duk_is_object(ctx, 0)) return 0;
    duk_get_prop_string(ctx, 0, "width");
    int w = duk_to_int(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, 0, "height");
    int h = duk_to_int(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, 0, "data");
    duk_size_t sz = 0;
    const uint8_t *src = (const uint8_t*)duk_get_buffer_data(ctx, -1, &sz);
    duk_pop(ctx);
    if (!src) return 0;

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
    return 0;
}

// ============================================================
// 変換 / 状態
// ============================================================

static duk_ret_t ctx_save(duk_context *ctx) { get_data(ctx)->stateStack.push_back(get_data(ctx)->state); return 0; }
static duk_ret_t ctx_restore(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (!d->stateStack.empty()) { d->state = d->stateStack.back(); d->stateStack.pop_back(); }
    return 0;
}
static duk_ret_t ctx_translate(duk_context *ctx) {
    auto *d = get_data(ctx);
    tvg::Matrix t = {1,0,(float)duk_get_number(ctx,0), 0,1,(float)duk_get_number(ctx,1), 0,0,1};
    d->state.transform = mat_mul(d->state.transform, t); return 0;
}
static duk_ret_t ctx_rotate(duk_context *ctx) {
    auto *d = get_data(ctx);
    float a=(float)duk_get_number(ctx,0), c=cosf(a), s=sinf(a);
    tvg::Matrix r = {c,-s,0, s,c,0, 0,0,1};
    d->state.transform = mat_mul(d->state.transform, r); return 0;
}
static duk_ret_t ctx_scale(duk_context *ctx) {
    auto *d = get_data(ctx);
    tvg::Matrix sc = {(float)duk_get_number(ctx,0),0,0, 0,(float)duk_get_number(ctx,1),0, 0,0,1};
    d->state.transform = mat_mul(d->state.transform, sc); return 0;
}
static duk_ret_t ctx_setTransform(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (duk_get_top(ctx) >= 6) {
        d->state.transform = {
            (float)duk_get_number(ctx,0), (float)duk_get_number(ctx,2), (float)duk_get_number(ctx,4),
            (float)duk_get_number(ctx,1), (float)duk_get_number(ctx,3), (float)duk_get_number(ctx,5),
            0, 0, 1
        };
    } else {
        d->state.transform = {1,0,0, 0,1,0, 0,0,1};
    }
    return 0;
}

// ============================================================
// flush: バッファ → GL テクスチャアップロード
// ============================================================

static duk_ret_t ctx_flush(duk_context *ctx) {
    auto *d = get_data(ctx);
    d->flushDirty();
    return 0;
}

// ============================================================
// プロパティ getter/setter
// ============================================================

static duk_ret_t ctx_get_fillStyle(duk_context *ctx) {
    auto *d = get_data(ctx); char buf[32];
    snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)", d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b, d->state.fillStyle.a/255.0f);
    duk_push_string(ctx, buf); return 1;
}
static duk_ret_t ctx_set_fillStyle(duk_context *ctx) { get_data(ctx)->state.fillStyle = parse_color(duk_to_string(ctx,0)); return 0; }
static duk_ret_t ctx_get_strokeStyle(duk_context *ctx) {
    auto *d = get_data(ctx); char buf[32];
    snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)", d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b, d->state.strokeStyle.a/255.0f);
    duk_push_string(ctx, buf); return 1;
}
static duk_ret_t ctx_set_strokeStyle(duk_context *ctx) { get_data(ctx)->state.strokeStyle = parse_color(duk_to_string(ctx,0)); return 0; }
static duk_ret_t ctx_get_lineWidth(duk_context *ctx) { duk_push_number(ctx, get_data(ctx)->state.lineWidth); return 1; }
static duk_ret_t ctx_set_lineWidth(duk_context *ctx) { get_data(ctx)->state.lineWidth = (float)duk_require_number(ctx,0); return 0; }
static duk_ret_t ctx_get_globalAlpha(duk_context *ctx) { duk_push_number(ctx, get_data(ctx)->state.globalAlpha); return 1; }
static duk_ret_t ctx_set_globalAlpha(duk_context *ctx) { get_data(ctx)->state.globalAlpha = (float)duk_require_number(ctx,0); return 0; }
static duk_ret_t ctx_get_font(duk_context *ctx) {
    auto *d = get_data(ctx); char buf[256];
    snprintf(buf, sizeof(buf), "%.0fpx %s", d->state.fontSize, d->state.fontName.c_str());
    duk_push_string(ctx, buf); return 1;
}
static duk_ret_t ctx_set_font(duk_context *ctx) {
    auto *d = get_data(ctx); const char *str = duk_to_string(ctx,0);
    float size = 16.0f; char name[256] = "";
    if (sscanf(str, "%fpx %255[^\n]", &size, name) >= 1) { d->state.fontSize = size; if (name[0]) d->state.fontName = name; }
    return 0;
}
static duk_ret_t ctx_get_textAlign(duk_context *ctx) { duk_push_string(ctx, get_data(ctx)->state.textAlign.c_str()); return 1; }
static duk_ret_t ctx_set_textAlign(duk_context *ctx) { get_data(ctx)->state.textAlign = duk_to_string(ctx,0); return 0; }
static duk_ret_t ctx_get_lineCap(duk_context *ctx) {
    auto *d = get_data(ctx); const char *v = "butt";
    if (d->state.lineCap == tvg::StrokeCap::Round) v = "round";
    else if (d->state.lineCap == tvg::StrokeCap::Square) v = "square";
    duk_push_string(ctx, v); return 1;
}
static duk_ret_t ctx_set_lineCap(duk_context *ctx) {
    auto *d = get_data(ctx); const char *v = duk_to_string(ctx,0);
    if (strcmp(v,"round")==0) d->state.lineCap = tvg::StrokeCap::Round;
    else if (strcmp(v,"square")==0) d->state.lineCap = tvg::StrokeCap::Square;
    else d->state.lineCap = tvg::StrokeCap::Butt; return 0;
}
static duk_ret_t ctx_get_lineJoin(duk_context *ctx) {
    auto *d = get_data(ctx); const char *v = "miter";
    if (d->state.lineJoin == tvg::StrokeJoin::Round) v = "round";
    else if (d->state.lineJoin == tvg::StrokeJoin::Bevel) v = "bevel";
    duk_push_string(ctx, v); return 1;
}
static duk_ret_t ctx_set_lineJoin(duk_context *ctx) {
    auto *d = get_data(ctx); const char *v = duk_to_string(ctx,0);
    if (strcmp(v,"round")==0) d->state.lineJoin = tvg::StrokeJoin::Round;
    else if (strcmp(v,"bevel")==0) d->state.lineJoin = tvg::StrokeJoin::Bevel;
    else d->state.lineJoin = tvg::StrokeJoin::Miter; return 0;
}

static duk_ret_t ctx_get_texture(duk_context *ctx) {
    auto *d = get_data(ctx);
    // テクスチャ取得時に dirty 領域を自動フラッシュ
    d->flushDirty();
    if (d->glTexture == 0) { duk_push_null(ctx); }
    else { duk_idx_t obj = duk_push_object(ctx); duk_push_uint(ctx, d->glTexture); duk_put_prop_string(ctx, obj, "_id"); }
    return 1;
}

// Canvas2D.loadFont(path)
// Canvas2D.loadFont(path)          — ファイルパスでロード（ThorVG 内部名で登録）
// Canvas2D.loadFont(path, alias)   — alias 名で登録（メモリロード）
static duk_ret_t static_loadFont(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    const char *alias = duk_get_top(ctx) > 1 ? duk_get_string(ctx, 1) : nullptr;
    JsEngine *engine = JsEngine::getInstance();
    std::string resolved = engine ? engine->resolvePath(path) : path;

    if (alias) {
        // SDL_LoadFile でフォントデータを読み込み、alias 名で ThorVG に登録
        size_t dataSize = 0;
        void *data = SDL_LoadFile(resolved.c_str(), &dataSize);
        if (!data) {
            return duk_error(ctx, DUK_ERR_ERROR, "Failed to load font file: %s", resolved.c_str());
        }
        auto result = tvg::Text::load(alias, (const char*)data, (uint32_t)dataSize, "ttf", true);
        SDL_free(data);
        if (result != tvg::Result::Success) {
            return duk_error(ctx, DUK_ERR_ERROR, "Failed to register font '%s' from: %s", alias, resolved.c_str());
        }
        SDL_Log("Font loaded: %s as '%s'", resolved.c_str(), alias);
    } else {
        // ファイルパスでロード（ThorVG 内部名で登録）
        auto result = tvg::Text::load(resolved.c_str());
        if (result != tvg::Result::Success) {
            return duk_error(ctx, DUK_ERR_ERROR, "Failed to load font: %s", resolved.c_str());
        }
        SDL_Log("Font loaded: %s", resolved.c_str());
    }
    return 0;
}

// ============================================================
// ファイナライザ / コンストラクタ
// ============================================================

static duk_ret_t ctx_finalizer(duk_context *ctx) {
    duk_get_prop_string(ctx, 0, "\xff" "data");
    if (duk_is_pointer(ctx, -1)) delete (Canvas2DData*)duk_get_pointer(ctx, -1);
    duk_pop(ctx); return 0;
}

static duk_ret_t canvas2d_constructor(duk_context *ctx) {
    if (!duk_is_constructor_call(ctx)) return DUK_RET_TYPE_ERROR;

    uint32_t w = (uint32_t)duk_require_uint(ctx, 0);
    uint32_t h = (uint32_t)duk_require_uint(ctx, 1);

    auto *data = new Canvas2DData();
    data->width = w; data->height = h;
    data->pixels.resize(w * h, 0);
    // EngineOption::None で dirty region（部分描画最適化）を無効化する。
    // Default のままだと draw()/sync() 後に fulldraw フラグが false になり、
    // 次回の draw() 時に preRender() が変更領域を 0x00000000 でクリアしてから
    // 再描画するため、描画周辺の背景が黒で塗りつぶされてしまう。
    data->canvas = tvg::SwCanvas::gen(tvg::EngineOption::None);
    // ARGB8888 (premultiplied) を使用。ABGR8888S は ThorVG の postRender() で
    // 毎回全バッファ rasterUnpremultiply が走るため 100倍以上遅くなる。
    // テクスチャアップロード時に dirty rect 分だけ ARGB→RGBA 変換する。
    data->canvas->target(data->pixels.data(), w, w, h, tvg::ColorSpace::ARGB8888);

    glGenTextures(1, &data->glTexture);
    glBindTexture(GL_TEXTURE_2D, data->glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    duk_push_this(ctx);
    duk_idx_t obj = duk_get_top_index(ctx);

    duk_push_pointer(ctx, data); duk_put_prop_string(ctx, obj, "\xff" "data");
    duk_push_uint(ctx, w); duk_put_prop_string(ctx, obj, "width");
    duk_push_uint(ctx, h); duk_put_prop_string(ctx, obj, "height");

    #define M(name, func, n) duk_push_c_function(ctx, func, n); duk_put_prop_string(ctx, obj, name)
    M("fillRect", ctx_fillRect, 4); M("strokeRect", ctx_strokeRect, 4); M("clearRect", ctx_clearRect, 4);
    M("beginPath", ctx_beginPath, 0); M("moveTo", ctx_moveTo, 2); M("lineTo", ctx_lineTo, 2);
    M("bezierCurveTo", ctx_bezierCurveTo, 6); M("rect", ctx_rect, 4);
    M("arc", ctx_arc, DUK_VARARGS); M("closePath", ctx_closePath, 0);
    M("fill", ctx_fill, 0); M("stroke", ctx_stroke, 0);
    M("fillText", ctx_fillText, 3); M("strokeText", ctx_strokeText, 3); M("measureText", ctx_measureText, 1);
    M("drawImage", ctx_drawImage, DUK_VARARGS);
    M("getImageData", ctx_getImageData, 4); M("putImageData", ctx_putImageData, 3);
    M("save", ctx_save, 0); M("restore", ctx_restore, 0);
    M("translate", ctx_translate, 2); M("rotate", ctx_rotate, 1); M("scale", ctx_scale, 2);
    M("setTransform", ctx_setTransform, DUK_VARARGS);
    M("flush", ctx_flush, 0);
    #undef M

    #define P(name, g, s) duk_push_string(ctx, name); duk_push_c_function(ctx, g, 0); duk_push_c_function(ctx, s, 1); \
        duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER|DUK_DEFPROP_HAVE_SETTER|DUK_DEFPROP_SET_ENUMERABLE)
    P("fillStyle", ctx_get_fillStyle, ctx_set_fillStyle);
    P("strokeStyle", ctx_get_strokeStyle, ctx_set_strokeStyle);
    P("lineWidth", ctx_get_lineWidth, ctx_set_lineWidth);
    P("globalAlpha", ctx_get_globalAlpha, ctx_set_globalAlpha);
    P("font", ctx_get_font, ctx_set_font);
    P("textAlign", ctx_get_textAlign, ctx_set_textAlign);
    P("lineCap", ctx_get_lineCap, ctx_set_lineCap);
    P("lineJoin", ctx_get_lineJoin, ctx_set_lineJoin);
    #undef P

    // globalCompositeOperation (ダミー)
    duk_push_string(ctx, "globalCompositeOperation");
    duk_push_c_function(ctx, [](duk_context*c)->duk_ret_t{ duk_push_string(c,"source-over"); return 1; }, 0);
    duk_push_c_function(ctx, [](duk_context*)->duk_ret_t{ return 0; }, 1);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER|DUK_DEFPROP_HAVE_SETTER|DUK_DEFPROP_SET_ENUMERABLE);

    // imageSmoothingEnabled (ダミー)
    duk_push_string(ctx, "imageSmoothingEnabled");
    duk_push_c_function(ctx, [](duk_context*c)->duk_ret_t{ duk_push_true(c); return 1; }, 0);
    duk_push_c_function(ctx, [](duk_context*)->duk_ret_t{ return 0; }, 1);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER|DUK_DEFPROP_HAVE_SETTER|DUK_DEFPROP_SET_ENUMERABLE);

    duk_push_string(ctx, "texture");
    duk_push_c_function(ctx, ctx_get_texture, 0);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_SET_ENUMERABLE);

    duk_push_c_function(ctx, ctx_finalizer, 1);
    duk_set_finalizer(ctx, obj);

    duk_pop(ctx);
    return 0;
}

// ============================================================
// バインディング登録
// ============================================================

void canvas2d_bind(duk_context *ctx) {
    duk_push_c_function(ctx, canvas2d_constructor, 2);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "prototype");
    duk_push_c_function(ctx, static_loadFont, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "loadFont");
    duk_put_global_string(ctx, "Canvas2D");
}
