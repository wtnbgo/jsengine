/**
 * WebGL 2.0 compatible bindings for Duktape + OpenGL ES 3.0
 *
 * Based on https://github.com/mrautio/duktape-webgl
 * Adapted for GLES 3.0 (via glad/gles2.h)
 */

#ifndef DUKWEBGL_H_INCLUDED
#define DUKWEBGL_H_INCLUDED

#include <duktape.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* WebGL バインディングを duktape コンテキストに登録 */
void dukwebgl_bind(duk_context *ctx);

#if defined(__cplusplus)
}
#endif

#endif /* DUKWEBGL_H_INCLUDED */
