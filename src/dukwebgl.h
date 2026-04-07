/**
 * WebGL 2.0 compatible bindings for QuickJS + OpenGL ES 3.0
 *
 * Based on https://github.com/mrautio/duktape-webgl
 * Adapted for GLES 3.0 (via glad/gles2.h)
 */

#ifndef DUKWEBGL_H_INCLUDED
#define DUKWEBGL_H_INCLUDED

#include <quickjs.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* WebGL バインディングを QuickJS コンテキストに登録 */
void dukwebgl_bind(JSContext *ctx);

#if defined(__cplusplus)
}
#endif

#endif /* DUKWEBGL_H_INCLUDED */
