/**
 * Canvas 2D API (ブラウザ互換サブセット)
 *
 * ThorVG SwCanvas でオフスクリーン描画し、GL テクスチャにアップロード
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
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// ThorVG 初期化 / 終了
// ============================================================

void canvas2d_init() {
    tvg::Initializer::init(1);
    SDL_Log("Canvas2D (ThorVG) initialized");
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
// パスコマンド記録（duplicate が無いため自前で管理）
// ============================================================

enum PathOp { PathMoveTo, PathLineTo, PathCubicTo, PathClose, PathRect, PathArc };

struct PathCmd {
    PathOp op;
    float args[7]; // 最大7引数 (arc)
};

// 円弧をベジェ曲線で近似してシェイプに追加
// cx,cy: 中心, r: 半径, startDeg: 開始角度(度), sweepDeg: 掃引角度(度)
static void shape_arc(tvg::Shape *shape, float cx, float cy, float r, float startDeg, float sweepDeg) {
    if (fabsf(sweepDeg) < 0.001f) return;
    // 90度以下のセグメントに分割
    int segments = (int)(fabsf(sweepDeg) / 90.0f) + 1;
    float segSweep = sweepDeg / segments;
    float segRad = segSweep * (float)(M_PI / 180.0);

    float curAngle = startDeg * (float)(M_PI / 180.0);
    float sx = cx + r * cosf(curAngle);
    float sy = cy + r * sinf(curAngle);
    shape->moveTo(sx, sy);

    for (int i = 0; i < segments; i++) {
        float a1 = curAngle;
        float a2 = curAngle + segRad;
        // ベジェ近似の制御点
        float alpha = 4.0f * tanf(segRad * 0.25f) / 3.0f;
        float x1 = cx + r * cosf(a1);
        float y1 = cy + r * sinf(a1);
        float x2 = cx + r * cosf(a2);
        float y2 = cy + r * sinf(a2);
        float cp1x = x1 - alpha * r * sinf(a1);
        float cp1y = y1 + alpha * r * cosf(a1);
        float cp2x = x2 + alpha * r * sinf(a2);
        float cp2y = y2 - alpha * r * cosf(a2);
        shape->cubicTo(cp1x, cp1y, cp2x, cp2y, x2, y2);
        curAngle = a2;
    }
}

// パスコマンドから tvg::Shape を構築
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
// Canvas2D 内部データ
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
    std::vector<uint32_t> pixels;
    tvg::SwCanvas *canvas = nullptr;
    GLuint glTexture = 0;
    bool dirty = true;

    std::vector<PathCmd> pathCmds;
    DrawState state;
    std::vector<DrawState> stateStack;

    ~Canvas2DData() {
        if (canvas) delete canvas;
        if (glTexture) glDeleteTextures(1, &glTexture);
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

// ============================================================
// 矩形
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
    d->canvas->add(shape);
    d->dirty = true;
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
    shape->strokeCap(d->state.lineCap);
    shape->strokeJoin(d->state.lineJoin);
    shape->transform(d->state.transform);
    d->canvas->add(shape);
    d->dirty = true;
    return 0;
}

static duk_ret_t ctx_clearRect(duk_context *ctx) {
    auto *d = get_data(ctx);
    float x=(float)duk_get_number(ctx,0), y=(float)duk_get_number(ctx,1);
    float w=(float)duk_get_number(ctx,2), h=(float)duk_get_number(ctx,3);
    // ピクセルバッファを直接クリア（変換なしの簡易版）
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    for (int py = iy; py < iy + ih && py < (int)d->height; py++) {
        if (py < 0) continue;
        for (int px = ix; px < ix + iw && px < (int)d->width; px++) {
            if (px < 0) continue;
            d->pixels[py * d->width + px] = 0;
        }
    }
    d->dirty = true;
    return 0;
}

// ============================================================
// パス
// ============================================================

static duk_ret_t ctx_beginPath(duk_context *ctx) {
    get_data(ctx)->pathCmds.clear();
    return 0;
}

static duk_ret_t ctx_moveTo(duk_context *ctx) {
    auto *d = get_data(ctx);
    PathCmd c = {PathMoveTo, {(float)duk_get_number(ctx,0),(float)duk_get_number(ctx,1)}};
    d->pathCmds.push_back(c);
    return 0;
}

static duk_ret_t ctx_lineTo(duk_context *ctx) {
    auto *d = get_data(ctx);
    PathCmd c = {PathLineTo, {(float)duk_get_number(ctx,0),(float)duk_get_number(ctx,1)}};
    d->pathCmds.push_back(c);
    return 0;
}

static duk_ret_t ctx_bezierCurveTo(duk_context *ctx) {
    auto *d = get_data(ctx);
    PathCmd c = {PathCubicTo, {
        (float)duk_get_number(ctx,0),(float)duk_get_number(ctx,1),
        (float)duk_get_number(ctx,2),(float)duk_get_number(ctx,3),
        (float)duk_get_number(ctx,4),(float)duk_get_number(ctx,5)}};
    d->pathCmds.push_back(c);
    return 0;
}

static duk_ret_t ctx_rect(duk_context *ctx) {
    auto *d = get_data(ctx);
    PathCmd c = {PathRect, {(float)duk_get_number(ctx,0),(float)duk_get_number(ctx,1),
        (float)duk_get_number(ctx,2),(float)duk_get_number(ctx,3)}};
    d->pathCmds.push_back(c);
    return 0;
}

static duk_ret_t ctx_arc(duk_context *ctx) {
    auto *d = get_data(ctx);
    float cx=(float)duk_get_number(ctx,0), cy=(float)duk_get_number(ctx,1);
    float r=(float)duk_get_number(ctx,2);
    float sa=(float)duk_get_number(ctx,3), ea=(float)duk_get_number(ctx,4);
    bool ccw = duk_get_top(ctx) > 5 ? (duk_get_boolean(ctx,5)!=0) : false;
    float sd = sa * (float)(180.0/M_PI), ed = ea * (float)(180.0/M_PI);
    float sweep = ed - sd;
    if (ccw) { if (sweep > 0) sweep -= 360.0f; }
    else     { if (sweep < 0) sweep += 360.0f; }
    PathCmd c = {PathArc, {cx, cy, r, sd, sweep, 0}};
    d->pathCmds.push_back(c);
    return 0;
}

static duk_ret_t ctx_closePath(duk_context *ctx) {
    PathCmd c = {PathClose, {}};
    get_data(ctx)->pathCmds.push_back(c);
    return 0;
}

static duk_ret_t ctx_fill(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (d->pathCmds.empty()) return 0;
    auto *shape = build_shape(d->pathCmds);
    uint8_t a = (uint8_t)(d->state.fillStyle.a * d->state.globalAlpha);
    shape->fill(d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b, a);
    shape->transform(d->state.transform);
    d->canvas->add(shape);
    d->dirty = true;
    return 0;
}

static duk_ret_t ctx_stroke(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (d->pathCmds.empty()) return 0;
    auto *shape = build_shape(d->pathCmds);
    uint8_t a = (uint8_t)(d->state.strokeStyle.a * d->state.globalAlpha);
    shape->strokeFill(d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b, a);
    shape->strokeWidth(d->state.lineWidth);
    shape->strokeCap(d->state.lineCap);
    shape->strokeJoin(d->state.lineJoin);
    shape->transform(d->state.transform);
    d->canvas->add(shape);
    d->dirty = true;
    return 0;
}

// ============================================================
// テキスト
// ============================================================

static duk_ret_t ctx_fillText(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *str = duk_require_string(ctx, 0);
    float x = (float)duk_get_number(ctx, 1);
    float y = (float)duk_get_number(ctx, 2);

    auto *text = tvg::Text::gen();
    text->font(d->state.fontName.c_str());
    text->size(d->state.fontSize);
    text->text(str);
    text->fill(d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b);
    uint8_t a = (uint8_t)(d->state.fillStyle.a * d->state.globalAlpha);
    text->opacity(a);

    // y をベースライン位置として扱う（フォントサイズ分上にオフセット）
    tvg::Matrix t = {1,0,x, 0,1,y - d->state.fontSize * 0.85f, 0,0,1};
    text->transform(mat_mul(d->state.transform, t));

    d->canvas->add(text);
    d->dirty = true;
    return 0;
}

static duk_ret_t ctx_strokeText(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *str = duk_require_string(ctx, 0);
    float x = (float)duk_get_number(ctx, 1);
    float y = (float)duk_get_number(ctx, 2);

    auto *text = tvg::Text::gen();
    text->font(d->state.fontName.c_str());
    text->size(d->state.fontSize);
    text->text(str);
    text->outline(d->state.lineWidth, d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b);
    uint8_t a = (uint8_t)(d->state.strokeStyle.a * d->state.globalAlpha);
    text->opacity(a);

    tvg::Matrix t = {1,0,x, 0,1,y - d->state.fontSize * 0.85f, 0,0,1};
    text->transform(mat_mul(d->state.transform, t));

    d->canvas->add(text);
    d->dirty = true;
    return 0;
}

static duk_ret_t ctx_measureText(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *str = duk_require_string(ctx, 0);

    auto *text = tvg::Text::gen();
    text->font(d->state.fontName.c_str());
    text->size(d->state.fontSize);
    text->text(str);

    // bounds を取得するために一時的に canvas に追加して update
    // 簡易実装: フォントサイズからの推定
    float estimatedWidth = (float)strlen(str) * d->state.fontSize * 0.6f;

    duk_idx_t obj = duk_push_object(ctx);
    duk_push_number(ctx, estimatedWidth);
    duk_put_prop_string(ctx, obj, "width");

    // text は canvas に追加しないので手動解放できないが、
    // tvg::Text のデストラクタは protected なので ref/unref を使う
    // gen() で作ったものは unref で解放
    text->unref();

    return 1;
}

// ============================================================
// 変換
// ============================================================

static duk_ret_t ctx_save(duk_context *ctx) {
    auto *d = get_data(ctx);
    d->stateStack.push_back(d->state);
    return 0;
}

static duk_ret_t ctx_restore(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (!d->stateStack.empty()) {
        d->state = d->stateStack.back();
        d->stateStack.pop_back();
    }
    return 0;
}

static duk_ret_t ctx_translate(duk_context *ctx) {
    auto *d = get_data(ctx);
    float tx=(float)duk_get_number(ctx,0), ty=(float)duk_get_number(ctx,1);
    tvg::Matrix t = {1,0,tx, 0,1,ty, 0,0,1};
    d->state.transform = mat_mul(d->state.transform, t);
    return 0;
}

static duk_ret_t ctx_rotate(duk_context *ctx) {
    auto *d = get_data(ctx);
    float a = (float)duk_get_number(ctx, 0);
    float c = cosf(a), s = sinf(a);
    tvg::Matrix r = {c,-s,0, s,c,0, 0,0,1};
    d->state.transform = mat_mul(d->state.transform, r);
    return 0;
}

static duk_ret_t ctx_scale(duk_context *ctx) {
    auto *d = get_data(ctx);
    float sx=(float)duk_get_number(ctx,0), sy=(float)duk_get_number(ctx,1);
    tvg::Matrix sc = {sx,0,0, 0,sy,0, 0,0,1};
    d->state.transform = mat_mul(d->state.transform, sc);
    return 0;
}

// ============================================================
// flush
// ============================================================

static duk_ret_t ctx_flush(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (!d->dirty) return 0;

    memset(d->pixels.data(), 0, d->pixels.size() * sizeof(uint32_t));

    d->canvas->update();
    d->canvas->draw(true);
    d->canvas->sync();

    // ARGB8888 (premultiplied) → GL RGBA (straight)
    size_t n = d->width * d->height;
    std::vector<uint32_t> rgba(n);
    for (size_t i = 0; i < n; i++) {
        uint32_t argb = d->pixels[i];
        uint8_t a = (argb >> 24) & 0xFF;
        uint8_t r = (argb >> 16) & 0xFF;
        uint8_t g = (argb >> 8) & 0xFF;
        uint8_t b = argb & 0xFF;
        if (a > 0 && a < 255) {
            r = (uint8_t)((r * 255) / a);
            g = (uint8_t)((g * 255) / a);
            b = (uint8_t)((b * 255) / a);
        }
        rgba[i] = r | (g << 8) | (b << 16) | (a << 24);
    }

    glBindTexture(GL_TEXTURE_2D, d->glTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, d->width, d->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // 次フレーム用にクリア
    d->canvas->remove();
    d->dirty = false;
    return 0;
}

// ============================================================
// プロパティ getter/setter
// ============================================================

static duk_ret_t ctx_get_fillStyle(duk_context *ctx) {
    auto *d = get_data(ctx);
    char buf[32]; snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)",
        d->state.fillStyle.r, d->state.fillStyle.g, d->state.fillStyle.b, d->state.fillStyle.a/255.0f);
    duk_push_string(ctx, buf); return 1;
}
static duk_ret_t ctx_set_fillStyle(duk_context *ctx) {
    get_data(ctx)->state.fillStyle = parse_color(duk_to_string(ctx, 0)); return 0;
}
static duk_ret_t ctx_get_strokeStyle(duk_context *ctx) {
    auto *d = get_data(ctx);
    char buf[32]; snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)",
        d->state.strokeStyle.r, d->state.strokeStyle.g, d->state.strokeStyle.b, d->state.strokeStyle.a/255.0f);
    duk_push_string(ctx, buf); return 1;
}
static duk_ret_t ctx_set_strokeStyle(duk_context *ctx) {
    get_data(ctx)->state.strokeStyle = parse_color(duk_to_string(ctx, 0)); return 0;
}
static duk_ret_t ctx_get_lineWidth(duk_context *ctx) { duk_push_number(ctx, get_data(ctx)->state.lineWidth); return 1; }
static duk_ret_t ctx_set_lineWidth(duk_context *ctx) { get_data(ctx)->state.lineWidth = (float)duk_require_number(ctx,0); return 0; }
static duk_ret_t ctx_get_globalAlpha(duk_context *ctx) { duk_push_number(ctx, get_data(ctx)->state.globalAlpha); return 1; }
static duk_ret_t ctx_set_globalAlpha(duk_context *ctx) { get_data(ctx)->state.globalAlpha = (float)duk_require_number(ctx,0); return 0; }

static duk_ret_t ctx_get_font(duk_context *ctx) {
    auto *d = get_data(ctx);
    char buf[256]; snprintf(buf, sizeof(buf), "%.0fpx %s", d->state.fontSize, d->state.fontName.c_str());
    duk_push_string(ctx, buf); return 1;
}
static duk_ret_t ctx_set_font(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *str = duk_to_string(ctx, 0);
    float size = 16.0f; char name[256] = "";
    if (sscanf(str, "%fpx %255[^\n]", &size, name) >= 1) {
        d->state.fontSize = size;
        if (name[0]) d->state.fontName = name;
    }
    return 0;
}

static duk_ret_t ctx_get_textAlign(duk_context *ctx) { duk_push_string(ctx, get_data(ctx)->state.textAlign.c_str()); return 1; }
static duk_ret_t ctx_set_textAlign(duk_context *ctx) { get_data(ctx)->state.textAlign = duk_to_string(ctx,0); return 0; }

static duk_ret_t ctx_get_lineCap(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *v = "butt";
    if (d->state.lineCap == tvg::StrokeCap::Round) v = "round";
    else if (d->state.lineCap == tvg::StrokeCap::Square) v = "square";
    duk_push_string(ctx, v); return 1;
}
static duk_ret_t ctx_set_lineCap(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *v = duk_to_string(ctx, 0);
    if (strcmp(v,"round")==0) d->state.lineCap = tvg::StrokeCap::Round;
    else if (strcmp(v,"square")==0) d->state.lineCap = tvg::StrokeCap::Square;
    else d->state.lineCap = tvg::StrokeCap::Butt;
    return 0;
}
static duk_ret_t ctx_get_lineJoin(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *v = "miter";
    if (d->state.lineJoin == tvg::StrokeJoin::Round) v = "round";
    else if (d->state.lineJoin == tvg::StrokeJoin::Bevel) v = "bevel";
    duk_push_string(ctx, v); return 1;
}
static duk_ret_t ctx_set_lineJoin(duk_context *ctx) {
    auto *d = get_data(ctx);
    const char *v = duk_to_string(ctx, 0);
    if (strcmp(v,"round")==0) d->state.lineJoin = tvg::StrokeJoin::Round;
    else if (strcmp(v,"bevel")==0) d->state.lineJoin = tvg::StrokeJoin::Bevel;
    else d->state.lineJoin = tvg::StrokeJoin::Miter;
    return 0;
}

static duk_ret_t ctx_get_texture(duk_context *ctx) {
    auto *d = get_data(ctx);
    if (d->glTexture == 0) { duk_push_null(ctx); }
    else {
        duk_idx_t obj = duk_push_object(ctx);
        duk_push_uint(ctx, d->glTexture);
        duk_put_prop_string(ctx, obj, "_id");
    }
    return 1;
}

// Canvas2D.loadFont(path)
static duk_ret_t static_loadFont(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    JsEngine *engine = JsEngine::getInstance();
    std::string resolved = engine ? engine->resolvePath(path) : path;
    auto result = tvg::Text::load(resolved.c_str());
    if (result != tvg::Result::Success) {
        return duk_error(ctx, DUK_ERR_ERROR, "Failed to load font: %s", resolved.c_str());
    }
    SDL_Log("Font loaded: %s", resolved.c_str());
    return 0;
}

// ============================================================
// ファイナライザ / コンストラクタ
// ============================================================

static duk_ret_t ctx_finalizer(duk_context *ctx) {
    duk_get_prop_string(ctx, 0, "\xff" "data");
    if (duk_is_pointer(ctx, -1)) delete (Canvas2DData*)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    return 0;
}

static duk_ret_t canvas2d_constructor(duk_context *ctx) {
    if (!duk_is_constructor_call(ctx)) return DUK_RET_TYPE_ERROR;

    uint32_t w = (uint32_t)duk_require_uint(ctx, 0);
    uint32_t h = (uint32_t)duk_require_uint(ctx, 1);

    auto *data = new Canvas2DData();
    data->width = w; data->height = h;
    data->pixels.resize(w * h, 0);
    data->canvas = tvg::SwCanvas::gen();
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

    duk_push_pointer(ctx, data);
    duk_put_prop_string(ctx, obj, "\xff" "data");
    duk_push_uint(ctx, w); duk_put_prop_string(ctx, obj, "width");
    duk_push_uint(ctx, h); duk_put_prop_string(ctx, obj, "height");

    #define M(name, func, n) duk_push_c_function(ctx, func, n); duk_put_prop_string(ctx, obj, name)
    M("fillRect", ctx_fillRect, 4);
    M("strokeRect", ctx_strokeRect, 4);
    M("clearRect", ctx_clearRect, 4);
    M("beginPath", ctx_beginPath, 0);
    M("moveTo", ctx_moveTo, 2);
    M("lineTo", ctx_lineTo, 2);
    M("bezierCurveTo", ctx_bezierCurveTo, 6);
    M("rect", ctx_rect, 4);
    M("arc", ctx_arc, DUK_VARARGS);
    M("closePath", ctx_closePath, 0);
    M("fill", ctx_fill, 0);
    M("stroke", ctx_stroke, 0);
    M("fillText", ctx_fillText, 3);
    M("strokeText", ctx_strokeText, 3);
    M("measureText", ctx_measureText, 1);
    M("save", ctx_save, 0);
    M("restore", ctx_restore, 0);
    M("translate", ctx_translate, 2);
    M("rotate", ctx_rotate, 1);
    M("scale", ctx_scale, 2);
    M("flush", ctx_flush, 0);
    #undef M

    #define P(name, g, s) duk_push_string(ctx, name); \
        duk_push_c_function(ctx, g, 0); duk_push_c_function(ctx, s, 1); \
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
    duk_push_c_function(ctx, static_loadFont, 1);
    duk_put_prop_string(ctx, -2, "loadFont");
    duk_put_global_string(ctx, "Canvas2D");
}
