/**
 * WebGL 2.0 compatible bindings for Duktape + OpenGL ES 3.0
 *
 * Based on https://github.com/mrautio/duktape-webgl
 * Adapted for GLES 3.0 (via glad/gles2.h)
 */

#include "dukwebgl.h"
#include "glad/gles2.h"
#include <duktape.h>
#include <cstdlib>
#include <cstring>

// ============================================================
// ヘルパー: WebGL オブジェクト（GLuint ID をラップ）
// ============================================================

static void dukwebgl_create_object_uint(duk_context *ctx, GLuint id) {
    if (id == 0) {
        duk_push_null(ctx);
        return;
    }
    duk_idx_t obj = duk_push_object(ctx);
    duk_push_uint(ctx, id);
    duk_put_prop_string(ctx, obj, "_id");
}

static GLuint dukwebgl_get_object_id_uint(duk_context *ctx, duk_idx_t obj_idx) {
    GLuint ret = 0;
    if (duk_is_object(ctx, obj_idx)) {
        duk_get_prop_string(ctx, obj_idx, "_id");
        ret = (GLuint)duk_to_uint(ctx, -1);
        duk_pop(ctx);
    }
    return ret;
}

static void dukwebgl_create_object_int(duk_context *ctx, GLint id) {
    if (id < 0) {
        duk_push_null(ctx);
        return;
    }
    duk_idx_t obj = duk_push_object(ctx);
    duk_push_int(ctx, id);
    duk_put_prop_string(ctx, obj, "_id");
}

static GLint dukwebgl_get_object_id_int(duk_context *ctx, duk_idx_t obj_idx) {
    GLint ret = -1;
    if (duk_is_object(ctx, obj_idx)) {
        duk_get_prop_string(ctx, obj_idx, "_id");
        ret = (GLint)duk_to_int(ctx, -1);
        duk_pop(ctx);
    }
    return ret;
}

// ============================================================
// ヘルパー: ピクセルデータ取得
// ============================================================

static void* dukwebgl_get_pixels(duk_context *ctx, duk_idx_t idx) {
    if (duk_is_buffer_data(ctx, idx)) {
        return duk_get_buffer_data(ctx, idx, NULL);
    }
    if (duk_is_object(ctx, idx) && duk_has_prop_string(ctx, idx, "data")) {
        duk_get_prop_string(ctx, idx, "data");
        if (duk_is_buffer_data(ctx, -1)) {
            void *p = duk_get_buffer_data(ctx, -1, NULL);
            duk_pop(ctx);
            return p;
        }
        duk_pop(ctx);
    }
    return NULL;
}

// ============================================================
// WebGL コンテキスト情報
// ============================================================

static duk_ret_t dukwebgl_getContextAttributes(duk_context *ctx) {
    duk_idx_t obj = duk_push_object(ctx);
    duk_push_boolean(ctx, 1); duk_put_prop_string(ctx, obj, "alpha");
    duk_push_boolean(ctx, 1); duk_put_prop_string(ctx, obj, "depth");
    duk_push_boolean(ctx, 1); duk_put_prop_string(ctx, obj, "stencil");
    duk_push_boolean(ctx, 1); duk_put_prop_string(ctx, obj, "antialias");
    duk_push_boolean(ctx, 1); duk_put_prop_string(ctx, obj, "premultipliedAlpha");
    duk_push_boolean(ctx, 0); duk_put_prop_string(ctx, obj, "preserveDrawingBuffer");
    duk_push_string(ctx, "default"); duk_put_prop_string(ctx, obj, "powerPreference");
    duk_push_boolean(ctx, 0); duk_put_prop_string(ctx, obj, "failIfMajorPerformanceCaveat");
    return 1;
}

static duk_ret_t dukwebgl_isContextLost(duk_context *ctx) {
    duk_push_false(ctx);
    return 1;
}

static duk_ret_t dukwebgl_getSupportedExtensions(duk_context *ctx) {
    duk_push_array(ctx);
    return 1;
}

static duk_ret_t dukwebgl_getExtension(duk_context *ctx) {
    duk_push_null(ctx);
    return 1;
}

// ============================================================
// シェーダ / プログラム
// ============================================================

static duk_ret_t dukwebgl_createShader(duk_context *ctx) {
    GLenum type = (GLenum)duk_get_uint(ctx, 0);
    GLuint shader = glCreateShader(type);
    dukwebgl_create_object_uint(ctx, shader);
    return 1;
}

static duk_ret_t dukwebgl_deleteShader(duk_context *ctx) {
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteShader(shader);
    return 0;
}

static duk_ret_t dukwebgl_shaderSource(duk_context *ctx) {
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 0);
    const GLchar *source = (const GLchar *)duk_get_string(ctx, 1);
    glShaderSource(shader, 1, &source, NULL);
    return 0;
}

static duk_ret_t dukwebgl_compileShader(duk_context *ctx) {
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 0);
    glCompileShader(shader);
    return 0;
}

static duk_ret_t dukwebgl_getShaderParameter(duk_context *ctx) {
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 0);
    GLenum pname = (GLenum)duk_get_uint(ctx, 1);
    GLint value = 0;
    glGetShaderiv(shader, pname, &value);
    switch (pname) {
    case GL_DELETE_STATUS:
    case GL_COMPILE_STATUS:
        duk_push_boolean(ctx, value == GL_TRUE ? 1 : 0);
        break;
    case GL_SHADER_TYPE:
        duk_push_uint(ctx, (GLuint)value);
        break;
    default:
        duk_push_undefined(ctx);
        break;
    }
    return 1;
}

static duk_ret_t dukwebgl_getShaderInfoLog(duk_context *ctx) {
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 0);
    GLchar infoLog[4096];
    GLsizei length = 0;
    glGetShaderInfoLog(shader, sizeof(infoLog), &length, infoLog);
    duk_push_string(ctx, infoLog);
    return 1;
}

static duk_ret_t dukwebgl_getShaderSource(duk_context *ctx) {
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 0);
    GLchar source[65536];
    GLsizei length = 0;
    glGetShaderSource(shader, sizeof(source), &length, source);
    duk_push_string(ctx, source);
    return 1;
}

static duk_ret_t dukwebgl_isShader(duk_context *ctx) {
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 0);
    duk_push_boolean(ctx, glIsShader(shader));
    return 1;
}

static duk_ret_t dukwebgl_createProgram(duk_context *ctx) {
    GLuint program = glCreateProgram();
    dukwebgl_create_object_uint(ctx, program);
    return 1;
}

static duk_ret_t dukwebgl_deleteProgram(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteProgram(program);
    return 0;
}

static duk_ret_t dukwebgl_attachShader(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 1);
    glAttachShader(program, shader);
    return 0;
}

static duk_ret_t dukwebgl_detachShader(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    GLuint shader = dukwebgl_get_object_id_uint(ctx, 1);
    glDetachShader(program, shader);
    return 0;
}

static duk_ret_t dukwebgl_linkProgram(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    glLinkProgram(program);
    return 0;
}

static duk_ret_t dukwebgl_useProgram(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    glUseProgram(program);
    return 0;
}

static duk_ret_t dukwebgl_validateProgram(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    glValidateProgram(program);
    return 0;
}

static duk_ret_t dukwebgl_isProgram(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    duk_push_boolean(ctx, glIsProgram(program));
    return 1;
}

static duk_ret_t dukwebgl_getProgramParameter(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    GLenum pname = (GLenum)duk_get_uint(ctx, 1);
    GLint value = 0;
    glGetProgramiv(program, pname, &value);
    switch (pname) {
    case GL_DELETE_STATUS:
    case GL_LINK_STATUS:
    case GL_VALIDATE_STATUS:
        duk_push_boolean(ctx, value == GL_TRUE ? 1 : 0);
        break;
    case GL_ATTACHED_SHADERS:
    case GL_ACTIVE_ATTRIBUTES:
    case GL_ACTIVE_UNIFORMS:
    case GL_TRANSFORM_FEEDBACK_VARYINGS:
    case GL_ACTIVE_UNIFORM_BLOCKS:
        duk_push_int(ctx, value);
        break;
    case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
        duk_push_uint(ctx, (GLuint)value);
        break;
    default:
        duk_push_undefined(ctx);
        break;
    }
    return 1;
}

static duk_ret_t dukwebgl_getProgramInfoLog(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    GLchar infoLog[4096];
    GLsizei length = 0;
    glGetProgramInfoLog(program, sizeof(infoLog), &length, infoLog);
    duk_push_string(ctx, infoLog);
    return 1;
}

static duk_ret_t dukwebgl_bindAttribLocation(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    GLuint index = (GLuint)duk_get_uint(ctx, 1);
    const char *name = duk_get_string(ctx, 2);
    glBindAttribLocation(program, index, name);
    return 0;
}

static duk_ret_t dukwebgl_getAttribLocation(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    const char *name = duk_get_string(ctx, 1);
    GLint loc = glGetAttribLocation(program, name);
    duk_push_int(ctx, loc);
    return 1;
}

static duk_ret_t dukwebgl_getUniformLocation(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    const char *name = duk_get_string(ctx, 1);
    GLint loc = glGetUniformLocation(program, name);
    dukwebgl_create_object_int(ctx, loc);
    return 1;
}

static duk_ret_t dukwebgl_getActiveAttrib(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    GLuint index = (GLuint)duk_get_uint(ctx, 1);
    GLchar name[1024];
    GLsizei length = 0;
    GLenum type;
    GLint size;
    glGetActiveAttrib(program, index, sizeof(name), &length, &size, &type, name);
    if (length <= 0) { duk_push_undefined(ctx); return 1; }
    duk_idx_t obj = duk_push_object(ctx);
    duk_push_string(ctx, name); duk_put_prop_string(ctx, obj, "name");
    duk_push_uint(ctx, type); duk_put_prop_string(ctx, obj, "type");
    duk_push_int(ctx, size); duk_put_prop_string(ctx, obj, "size");
    return 1;
}

static duk_ret_t dukwebgl_getActiveUniform(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    GLuint index = (GLuint)duk_get_uint(ctx, 1);
    GLchar name[1024];
    GLsizei length = 0;
    GLenum type;
    GLint size;
    glGetActiveUniform(program, index, sizeof(name), &length, &size, &type, name);
    if (length <= 0) { duk_push_undefined(ctx); return 1; }
    duk_idx_t obj = duk_push_object(ctx);
    duk_push_string(ctx, name); duk_put_prop_string(ctx, obj, "name");
    duk_push_uint(ctx, type); duk_put_prop_string(ctx, obj, "type");
    duk_push_int(ctx, size); duk_put_prop_string(ctx, obj, "size");
    return 1;
}

// ============================================================
// バッファ
// ============================================================

static duk_ret_t dukwebgl_createBuffer(duk_context *ctx) {
    GLuint id;
    glGenBuffers(1, &id);
    dukwebgl_create_object_uint(ctx, id);
    return 1;
}

static duk_ret_t dukwebgl_deleteBuffer(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteBuffers(1, &id);
    return 0;
}

static duk_ret_t dukwebgl_isBuffer(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    duk_push_boolean(ctx, glIsBuffer(id));
    return 1;
}

static duk_ret_t dukwebgl_bindBuffer(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLuint buffer = dukwebgl_get_object_id_uint(ctx, 1);
    glBindBuffer(target, buffer);
    return 0;
}

static duk_ret_t dukwebgl_bufferData(duk_context *ctx) {
    int argc = duk_get_top(ctx);
    GLenum target = (GLenum)duk_get_uint(ctx, 0);

    duk_size_t data_size = 0;
    GLvoid *data = NULL;
    if (duk_is_buffer_data(ctx, 1)) {
        data = duk_get_buffer_data(ctx, 1, &data_size);
    } else {
        data_size = (duk_size_t)duk_get_uint(ctx, 1);
    }
    GLenum usage = (GLenum)duk_get_uint(ctx, 2);

    if (argc > 3) {
        GLuint src_offset = (GLuint)duk_get_uint(ctx, 3);
        data = (GLvoid*)((char*)data + src_offset);
        data_size -= src_offset;
        if (argc > 4) {
            GLuint length = (GLuint)duk_get_uint(ctx, 4);
            if (length > 0 && length <= data_size) data_size = length;
        }
    }
    glBufferData(target, (GLsizeiptr)data_size, data, usage);
    return 0;
}

static duk_ret_t dukwebgl_bufferSubData(duk_context *ctx) {
    int argc = duk_get_top(ctx);
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLintptr offset = (GLintptr)duk_get_uint(ctx, 1);

    duk_size_t data_size = 0;
    GLvoid *data = NULL;
    if (duk_is_buffer_data(ctx, 2)) {
        data = duk_get_buffer_data(ctx, 2, &data_size);
    }
    if (argc > 3) {
        GLuint src_offset = (GLuint)duk_get_uint(ctx, 3);
        data = (GLvoid*)((char*)data + src_offset);
        data_size -= src_offset;
        if (argc > 4) {
            GLuint length = (GLuint)duk_get_uint(ctx, 4);
            if (length > 0 && length <= data_size) data_size = length;
        }
    }
    glBufferSubData(target, offset, (GLsizeiptr)data_size, data);
    return 0;
}

// ============================================================
// テクスチャ
// ============================================================

static duk_ret_t dukwebgl_createTexture(duk_context *ctx) {
    GLuint id;
    glGenTextures(1, &id);
    dukwebgl_create_object_uint(ctx, id);
    return 1;
}

static duk_ret_t dukwebgl_deleteTexture(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteTextures(1, &id);
    return 0;
}

static duk_ret_t dukwebgl_isTexture(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    duk_push_boolean(ctx, glIsTexture(id));
    return 1;
}

static duk_ret_t dukwebgl_bindTexture(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLuint texture = dukwebgl_get_object_id_uint(ctx, 1);
    glBindTexture(target, texture);
    return 0;
}

static duk_ret_t dukwebgl_activeTexture(duk_context *ctx) {
    GLenum texture = (GLenum)duk_get_uint(ctx, 0);
    glActiveTexture(texture);
    return 0;
}

static duk_ret_t dukwebgl_texParameteri(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLenum pname = (GLenum)duk_get_uint(ctx, 1);
    GLint param = (GLint)duk_get_int(ctx, 2);
    glTexParameteri(target, pname, param);
    return 0;
}

static duk_ret_t dukwebgl_texParameterf(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLenum pname = (GLenum)duk_get_uint(ctx, 1);
    GLfloat param = (GLfloat)duk_get_number(ctx, 2);
    glTexParameterf(target, pname, param);
    return 0;
}

static duk_ret_t dukwebgl_generateMipmap(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    glGenerateMipmap(target);
    return 0;
}

static duk_ret_t dukwebgl_texImage2D(duk_context *ctx) {
    int argc = duk_get_top(ctx);
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLint level = (GLint)duk_get_int(ctx, 1);
    GLint internalformat = (GLint)duk_get_int(ctx, 2);

    if (argc >= 9) {
        // texImage2D(target, level, internalformat, width, height, border, format, type, pixels)
        GLsizei width = (GLsizei)duk_get_int(ctx, 3);
        GLsizei height = (GLsizei)duk_get_int(ctx, 4);
        GLint border = (GLint)duk_get_int(ctx, 5);
        GLenum format = (GLenum)duk_get_uint(ctx, 6);
        GLenum type = (GLenum)duk_get_uint(ctx, 7);
        void *pixels = dukwebgl_get_pixels(ctx, 8);
        glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    } else if (argc >= 6) {
        // texImage2D(target, level, internalformat, format, type, source)
        GLenum format = (GLenum)duk_get_uint(ctx, 3);
        GLenum type = (GLenum)duk_get_uint(ctx, 4);
        GLsizei width = 0, height = 0;
        void *pixels = NULL;
        if (duk_is_object(ctx, 5)) {
            if (duk_has_prop_string(ctx, 5, "width")) {
                duk_get_prop_string(ctx, 5, "width");
                width = (GLsizei)duk_get_int(ctx, -1); duk_pop(ctx);
            }
            if (duk_has_prop_string(ctx, 5, "height")) {
                duk_get_prop_string(ctx, 5, "height");
                height = (GLsizei)duk_get_int(ctx, -1); duk_pop(ctx);
            }
            pixels = dukwebgl_get_pixels(ctx, 5);
        }
        glTexImage2D(target, level, internalformat, width, height, 0, format, type, pixels);
    }
    return 0;
}

static duk_ret_t dukwebgl_texSubImage2D(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLint level = (GLint)duk_get_int(ctx, 1);
    GLint xoffset = (GLint)duk_get_int(ctx, 2);
    GLint yoffset = (GLint)duk_get_int(ctx, 3);
    GLsizei width = (GLsizei)duk_get_int(ctx, 4);
    GLsizei height = (GLsizei)duk_get_int(ctx, 5);
    GLenum format = (GLenum)duk_get_uint(ctx, 6);
    GLenum type = (GLenum)duk_get_uint(ctx, 7);
    void *pixels = dukwebgl_get_pixels(ctx, 8);
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    return 0;
}

static duk_ret_t dukwebgl_texImage3D(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLint level = (GLint)duk_get_int(ctx, 1);
    GLint internalformat = (GLint)duk_get_int(ctx, 2);
    GLsizei width = (GLsizei)duk_get_int(ctx, 3);
    GLsizei height = (GLsizei)duk_get_int(ctx, 4);
    GLsizei depth = (GLsizei)duk_get_int(ctx, 5);
    GLint border = (GLint)duk_get_int(ctx, 6);
    GLenum format = (GLenum)duk_get_uint(ctx, 7);
    GLenum type = (GLenum)duk_get_uint(ctx, 8);
    void *pixels = dukwebgl_get_pixels(ctx, 9);
    glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels);
    return 0;
}

static duk_ret_t dukwebgl_copyTexImage2D(duk_context *ctx) {
    glCopyTexImage2D(
        (GLenum)duk_get_uint(ctx, 0), (GLint)duk_get_int(ctx, 1),
        (GLenum)duk_get_uint(ctx, 2),
        (GLint)duk_get_int(ctx, 3), (GLint)duk_get_int(ctx, 4),
        (GLsizei)duk_get_int(ctx, 5), (GLsizei)duk_get_int(ctx, 6),
        (GLint)duk_get_int(ctx, 7));
    return 0;
}

static duk_ret_t dukwebgl_copyTexSubImage2D(duk_context *ctx) {
    glCopyTexSubImage2D(
        (GLenum)duk_get_uint(ctx, 0), (GLint)duk_get_int(ctx, 1),
        (GLint)duk_get_int(ctx, 2), (GLint)duk_get_int(ctx, 3),
        (GLint)duk_get_int(ctx, 4), (GLint)duk_get_int(ctx, 5),
        (GLsizei)duk_get_int(ctx, 6), (GLsizei)duk_get_int(ctx, 7));
    return 0;
}

static duk_ret_t dukwebgl_pixelStorei(duk_context *ctx) {
    GLenum pname = (GLenum)duk_get_uint(ctx, 0);
    GLint param = (GLint)duk_get_int(ctx, 1);
    glPixelStorei(pname, param);
    return 0;
}

static duk_ret_t dukwebgl_readPixels(duk_context *ctx) {
    GLint x = (GLint)duk_get_int(ctx, 0);
    GLint y = (GLint)duk_get_int(ctx, 1);
    GLsizei width = (GLsizei)duk_get_int(ctx, 2);
    GLsizei height = (GLsizei)duk_get_int(ctx, 3);
    GLenum format = (GLenum)duk_get_uint(ctx, 4);
    GLenum type = (GLenum)duk_get_uint(ctx, 5);
    void *pixels = duk_get_buffer_data(ctx, 6, NULL);
    glReadPixels(x, y, width, height, format, type, pixels);
    return 0;
}

// ============================================================
// フレームバッファ / レンダーバッファ
// ============================================================

static duk_ret_t dukwebgl_createFramebuffer(duk_context *ctx) {
    GLuint id;
    glGenFramebuffers(1, &id);
    dukwebgl_create_object_uint(ctx, id);
    return 1;
}

static duk_ret_t dukwebgl_deleteFramebuffer(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteFramebuffers(1, &id);
    return 0;
}

static duk_ret_t dukwebgl_isFramebuffer(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    duk_push_boolean(ctx, glIsFramebuffer(id));
    return 1;
}

static duk_ret_t dukwebgl_bindFramebuffer(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLuint fb = dukwebgl_get_object_id_uint(ctx, 1);
    glBindFramebuffer(target, fb);
    return 0;
}

static duk_ret_t dukwebgl_framebufferTexture2D(duk_context *ctx) {
    glFramebufferTexture2D(
        (GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1),
        (GLenum)duk_get_uint(ctx, 2), dukwebgl_get_object_id_uint(ctx, 3),
        (GLint)duk_get_int(ctx, 4));
    return 0;
}

static duk_ret_t dukwebgl_framebufferRenderbuffer(duk_context *ctx) {
    glFramebufferRenderbuffer(
        (GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1),
        (GLenum)duk_get_uint(ctx, 2), dukwebgl_get_object_id_uint(ctx, 3));
    return 0;
}

static duk_ret_t dukwebgl_checkFramebufferStatus(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    duk_push_uint(ctx, glCheckFramebufferStatus(target));
    return 1;
}

static duk_ret_t dukwebgl_createRenderbuffer(duk_context *ctx) {
    GLuint id;
    glGenRenderbuffers(1, &id);
    dukwebgl_create_object_uint(ctx, id);
    return 1;
}

static duk_ret_t dukwebgl_deleteRenderbuffer(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteRenderbuffers(1, &id);
    return 0;
}

static duk_ret_t dukwebgl_isRenderbuffer(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    duk_push_boolean(ctx, glIsRenderbuffer(id));
    return 1;
}

static duk_ret_t dukwebgl_bindRenderbuffer(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLuint rb = dukwebgl_get_object_id_uint(ctx, 1);
    glBindRenderbuffer(target, rb);
    return 0;
}

static duk_ret_t dukwebgl_renderbufferStorage(duk_context *ctx) {
    glRenderbufferStorage(
        (GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1),
        (GLsizei)duk_get_int(ctx, 2), (GLsizei)duk_get_int(ctx, 3));
    return 0;
}

static duk_ret_t dukwebgl_drawBuffers(duk_context *ctx) {
    if (!duk_is_array(ctx, 0)) return DUK_RET_TYPE_ERROR;
    duk_get_prop_string(ctx, 0, "length");
    unsigned int length = duk_to_uint(ctx, -1);
    duk_pop(ctx);
    GLenum *bufs = (GLenum*)malloc(sizeof(GLenum) * length);
    if (!bufs) return DUK_RET_ERROR;
    for (unsigned int i = 0; i < length; i++) {
        duk_get_prop_index(ctx, 0, i);
        bufs[i] = (GLenum)duk_to_uint(ctx, -1);
        duk_pop(ctx);
    }
    glDrawBuffers((GLsizei)length, bufs);
    free(bufs);
    return 0;
}

static duk_ret_t dukwebgl_blitFramebuffer(duk_context *ctx) {
    glBlitFramebuffer(
        (GLint)duk_get_int(ctx, 0), (GLint)duk_get_int(ctx, 1),
        (GLint)duk_get_int(ctx, 2), (GLint)duk_get_int(ctx, 3),
        (GLint)duk_get_int(ctx, 4), (GLint)duk_get_int(ctx, 5),
        (GLint)duk_get_int(ctx, 6), (GLint)duk_get_int(ctx, 7),
        (GLbitfield)duk_get_uint(ctx, 8), (GLenum)duk_get_uint(ctx, 9));
    return 0;
}

// ============================================================
// VAO (WebGL 2.0 / GLES 3.0)
// ============================================================

static duk_ret_t dukwebgl_createVertexArray(duk_context *ctx) {
    GLuint id;
    glGenVertexArrays(1, &id);
    dukwebgl_create_object_uint(ctx, id);
    return 1;
}

static duk_ret_t dukwebgl_deleteVertexArray(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteVertexArrays(1, &id);
    return 0;
}

static duk_ret_t dukwebgl_isVertexArray(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    duk_push_boolean(ctx, glIsVertexArray(id));
    return 1;
}

static duk_ret_t dukwebgl_bindVertexArray(duk_context *ctx) {
    GLuint vao = dukwebgl_get_object_id_uint(ctx, 0);
    glBindVertexArray(vao);
    return 0;
}

// ============================================================
// 頂点属性
// ============================================================

static duk_ret_t dukwebgl_enableVertexAttribArray(duk_context *ctx) {
    GLuint index = (GLuint)duk_get_uint(ctx, 0);
    glEnableVertexAttribArray(index);
    return 0;
}

static duk_ret_t dukwebgl_disableVertexAttribArray(duk_context *ctx) {
    GLuint index = (GLuint)duk_get_uint(ctx, 0);
    glDisableVertexAttribArray(index);
    return 0;
}

static duk_ret_t dukwebgl_vertexAttribPointer(duk_context *ctx) {
    GLuint index = (GLuint)duk_get_uint(ctx, 0);
    GLint size = (GLint)duk_get_int(ctx, 1);
    GLenum type = (GLenum)duk_get_uint(ctx, 2);
    GLboolean normalized = duk_get_boolean(ctx, 3) ? GL_TRUE : GL_FALSE;
    GLsizei stride = (GLsizei)duk_get_int(ctx, 4);
    GLintptr offset = (GLintptr)duk_get_int(ctx, 5);
    glVertexAttribPointer(index, size, type, normalized, stride, (const void*)offset);
    return 0;
}

static duk_ret_t dukwebgl_vertexAttribIPointer(duk_context *ctx) {
    GLuint index = (GLuint)duk_get_uint(ctx, 0);
    GLint size = (GLint)duk_get_int(ctx, 1);
    GLenum type = (GLenum)duk_get_uint(ctx, 2);
    GLsizei stride = (GLsizei)duk_get_int(ctx, 3);
    GLintptr offset = (GLintptr)duk_get_int(ctx, 4);
    glVertexAttribIPointer(index, size, type, stride, (const void*)offset);
    return 0;
}

static duk_ret_t dukwebgl_vertexAttribDivisor(duk_context *ctx) {
    GLuint index = (GLuint)duk_get_uint(ctx, 0);
    GLuint divisor = (GLuint)duk_get_uint(ctx, 1);
    glVertexAttribDivisor(index, divisor);
    return 0;
}

static duk_ret_t dukwebgl_vertexAttrib1f(duk_context *ctx) {
    glVertexAttrib1f((GLuint)duk_get_uint(ctx, 0), (GLfloat)duk_get_number(ctx, 1));
    return 0;
}
static duk_ret_t dukwebgl_vertexAttrib2f(duk_context *ctx) {
    glVertexAttrib2f((GLuint)duk_get_uint(ctx, 0),
        (GLfloat)duk_get_number(ctx, 1), (GLfloat)duk_get_number(ctx, 2));
    return 0;
}
static duk_ret_t dukwebgl_vertexAttrib3f(duk_context *ctx) {
    glVertexAttrib3f((GLuint)duk_get_uint(ctx, 0),
        (GLfloat)duk_get_number(ctx, 1), (GLfloat)duk_get_number(ctx, 2),
        (GLfloat)duk_get_number(ctx, 3));
    return 0;
}
static duk_ret_t dukwebgl_vertexAttrib4f(duk_context *ctx) {
    glVertexAttrib4f((GLuint)duk_get_uint(ctx, 0),
        (GLfloat)duk_get_number(ctx, 1), (GLfloat)duk_get_number(ctx, 2),
        (GLfloat)duk_get_number(ctx, 3), (GLfloat)duk_get_number(ctx, 4));
    return 0;
}

// ============================================================
// Uniform
// ============================================================

static duk_ret_t dukwebgl_uniform1i(duk_context *ctx) {
    GLint loc = dukwebgl_get_object_id_int(ctx, 0);
    glUniform1i(loc, (GLint)duk_get_int(ctx, 1));
    return 0;
}
static duk_ret_t dukwebgl_uniform2i(duk_context *ctx) {
    GLint loc = dukwebgl_get_object_id_int(ctx, 0);
    glUniform2i(loc, (GLint)duk_get_int(ctx, 1), (GLint)duk_get_int(ctx, 2));
    return 0;
}
static duk_ret_t dukwebgl_uniform3i(duk_context *ctx) {
    GLint loc = dukwebgl_get_object_id_int(ctx, 0);
    glUniform3i(loc, (GLint)duk_get_int(ctx, 1), (GLint)duk_get_int(ctx, 2), (GLint)duk_get_int(ctx, 3));
    return 0;
}
static duk_ret_t dukwebgl_uniform4i(duk_context *ctx) {
    GLint loc = dukwebgl_get_object_id_int(ctx, 0);
    glUniform4i(loc, (GLint)duk_get_int(ctx, 1), (GLint)duk_get_int(ctx, 2),
        (GLint)duk_get_int(ctx, 3), (GLint)duk_get_int(ctx, 4));
    return 0;
}

static duk_ret_t dukwebgl_uniform1f(duk_context *ctx) {
    GLint loc = dukwebgl_get_object_id_int(ctx, 0);
    glUniform1f(loc, (GLfloat)duk_get_number(ctx, 1));
    return 0;
}
static duk_ret_t dukwebgl_uniform2f(duk_context *ctx) {
    GLint loc = dukwebgl_get_object_id_int(ctx, 0);
    glUniform2f(loc, (GLfloat)duk_get_number(ctx, 1), (GLfloat)duk_get_number(ctx, 2));
    return 0;
}
static duk_ret_t dukwebgl_uniform3f(duk_context *ctx) {
    GLint loc = dukwebgl_get_object_id_int(ctx, 0);
    glUniform3f(loc, (GLfloat)duk_get_number(ctx, 1), (GLfloat)duk_get_number(ctx, 2),
        (GLfloat)duk_get_number(ctx, 3));
    return 0;
}
static duk_ret_t dukwebgl_uniform4f(duk_context *ctx) {
    GLint loc = dukwebgl_get_object_id_int(ctx, 0);
    glUniform4f(loc, (GLfloat)duk_get_number(ctx, 1), (GLfloat)duk_get_number(ctx, 2),
        (GLfloat)duk_get_number(ctx, 3), (GLfloat)duk_get_number(ctx, 4));
    return 0;
}

// uniform*fv / uniform*iv
#define DEFINE_UNIFORM_FV(name, cType, glFunc) \
    static duk_ret_t dukwebgl_##name(duk_context *ctx) { \
        GLint loc = dukwebgl_get_object_id_int(ctx, 0); \
        duk_size_t count = 0; \
        const cType *value = (const cType *)duk_get_buffer_data(ctx, 1, &count); \
        glFunc(loc, (GLsizei)count, value); \
        return 0; \
    }

DEFINE_UNIFORM_FV(uniform1fv, GLfloat, glUniform1fv)
DEFINE_UNIFORM_FV(uniform2fv, GLfloat, glUniform2fv)
DEFINE_UNIFORM_FV(uniform3fv, GLfloat, glUniform3fv)
DEFINE_UNIFORM_FV(uniform4fv, GLfloat, glUniform4fv)
DEFINE_UNIFORM_FV(uniform1iv, GLint, glUniform1iv)
DEFINE_UNIFORM_FV(uniform2iv, GLint, glUniform2iv)
DEFINE_UNIFORM_FV(uniform3iv, GLint, glUniform3iv)
DEFINE_UNIFORM_FV(uniform4iv, GLint, glUniform4iv)
DEFINE_UNIFORM_FV(uniform1uiv, GLuint, glUniform1uiv)
DEFINE_UNIFORM_FV(uniform2uiv, GLuint, glUniform2uiv)
DEFINE_UNIFORM_FV(uniform3uiv, GLuint, glUniform3uiv)
DEFINE_UNIFORM_FV(uniform4uiv, GLuint, glUniform4uiv)

// uniformMatrix*fv
#define DEFINE_UNIFORM_MATRIX(name, glFunc) \
    static duk_ret_t dukwebgl_##name(duk_context *ctx) { \
        GLint loc = dukwebgl_get_object_id_int(ctx, 0); \
        GLboolean transpose = duk_get_boolean(ctx, 1) ? GL_TRUE : GL_FALSE; \
        duk_size_t count = 0; \
        const GLfloat *value = (const GLfloat *)duk_get_buffer_data(ctx, 2, &count); \
        glFunc(loc, 1, transpose, value); \
        return 0; \
    }

DEFINE_UNIFORM_MATRIX(uniformMatrix2fv, glUniformMatrix2fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix3fv, glUniformMatrix3fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix4fv, glUniformMatrix4fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix2x3fv, glUniformMatrix2x3fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix2x4fv, glUniformMatrix2x4fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix3x2fv, glUniformMatrix3x2fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix3x4fv, glUniformMatrix3x4fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix4x2fv, glUniformMatrix4x2fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix4x3fv, glUniformMatrix4x3fv)

// ============================================================
// 描画
// ============================================================

static duk_ret_t dukwebgl_drawArrays(duk_context *ctx) {
    GLenum mode = (GLenum)duk_get_uint(ctx, 0);
    GLint first = (GLint)duk_get_int(ctx, 1);
    GLsizei count = (GLsizei)duk_get_int(ctx, 2);
    glDrawArrays(mode, first, count);
    return 0;
}

static duk_ret_t dukwebgl_drawElements(duk_context *ctx) {
    GLenum mode = (GLenum)duk_get_uint(ctx, 0);
    GLsizei count = (GLsizei)duk_get_int(ctx, 1);
    GLenum type = (GLenum)duk_get_uint(ctx, 2);
    GLintptr offset = (GLintptr)duk_get_int(ctx, 3);
    glDrawElements(mode, count, type, (const void*)offset);
    return 0;
}

static duk_ret_t dukwebgl_drawArraysInstanced(duk_context *ctx) {
    GLenum mode = (GLenum)duk_get_uint(ctx, 0);
    GLint first = (GLint)duk_get_int(ctx, 1);
    GLsizei count = (GLsizei)duk_get_int(ctx, 2);
    GLsizei instanceCount = (GLsizei)duk_get_int(ctx, 3);
    glDrawArraysInstanced(mode, first, count, instanceCount);
    return 0;
}

static duk_ret_t dukwebgl_drawElementsInstanced(duk_context *ctx) {
    GLenum mode = (GLenum)duk_get_uint(ctx, 0);
    GLsizei count = (GLsizei)duk_get_int(ctx, 1);
    GLenum type = (GLenum)duk_get_uint(ctx, 2);
    GLintptr offset = (GLintptr)duk_get_int(ctx, 3);
    GLsizei instanceCount = (GLsizei)duk_get_int(ctx, 4);
    glDrawElementsInstanced(mode, count, type, (const void*)offset, instanceCount);
    return 0;
}

// ============================================================
// ステート管理
// ============================================================

static duk_ret_t dukwebgl_enable(duk_context *ctx) {
    glEnable((GLenum)duk_get_uint(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_disable(duk_context *ctx) {
    glDisable((GLenum)duk_get_uint(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_isEnabled(duk_context *ctx) {
    duk_push_boolean(ctx, glIsEnabled((GLenum)duk_get_uint(ctx, 0)));
    return 1;
}

static duk_ret_t dukwebgl_viewport(duk_context *ctx) {
    glViewport((GLint)duk_get_int(ctx, 0), (GLint)duk_get_int(ctx, 1),
               (GLsizei)duk_get_int(ctx, 2), (GLsizei)duk_get_int(ctx, 3));
    return 0;
}
static duk_ret_t dukwebgl_scissor(duk_context *ctx) {
    glScissor((GLint)duk_get_int(ctx, 0), (GLint)duk_get_int(ctx, 1),
              (GLsizei)duk_get_int(ctx, 2), (GLsizei)duk_get_int(ctx, 3));
    return 0;
}

static duk_ret_t dukwebgl_clearColor(duk_context *ctx) {
    glClearColor((GLfloat)duk_get_number(ctx, 0), (GLfloat)duk_get_number(ctx, 1),
                 (GLfloat)duk_get_number(ctx, 2), (GLfloat)duk_get_number(ctx, 3));
    return 0;
}
static duk_ret_t dukwebgl_clearDepth(duk_context *ctx) {
    glClearDepthf((GLfloat)duk_get_number(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_clearStencil(duk_context *ctx) {
    glClearStencil((GLint)duk_get_int(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_clear(duk_context *ctx) {
    glClear((GLbitfield)duk_get_uint(ctx, 0));
    return 0;
}

static duk_ret_t dukwebgl_colorMask(duk_context *ctx) {
    glColorMask(duk_get_boolean(ctx, 0), duk_get_boolean(ctx, 1),
                duk_get_boolean(ctx, 2), duk_get_boolean(ctx, 3));
    return 0;
}
static duk_ret_t dukwebgl_depthMask(duk_context *ctx) {
    glDepthMask(duk_get_boolean(ctx, 0) ? GL_TRUE : GL_FALSE);
    return 0;
}
static duk_ret_t dukwebgl_depthFunc(duk_context *ctx) {
    glDepthFunc((GLenum)duk_get_uint(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_depthRange(duk_context *ctx) {
    glDepthRangef((GLfloat)duk_get_number(ctx, 0), (GLfloat)duk_get_number(ctx, 1));
    return 0;
}

static duk_ret_t dukwebgl_blendFunc(duk_context *ctx) {
    glBlendFunc((GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1));
    return 0;
}
static duk_ret_t dukwebgl_blendFuncSeparate(duk_context *ctx) {
    glBlendFuncSeparate((GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1),
                        (GLenum)duk_get_uint(ctx, 2), (GLenum)duk_get_uint(ctx, 3));
    return 0;
}
static duk_ret_t dukwebgl_blendEquation(duk_context *ctx) {
    glBlendEquation((GLenum)duk_get_uint(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_blendEquationSeparate(duk_context *ctx) {
    glBlendEquationSeparate((GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1));
    return 0;
}
static duk_ret_t dukwebgl_blendColor(duk_context *ctx) {
    glBlendColor((GLfloat)duk_get_number(ctx, 0), (GLfloat)duk_get_number(ctx, 1),
                 (GLfloat)duk_get_number(ctx, 2), (GLfloat)duk_get_number(ctx, 3));
    return 0;
}

static duk_ret_t dukwebgl_stencilFunc(duk_context *ctx) {
    glStencilFunc((GLenum)duk_get_uint(ctx, 0), (GLint)duk_get_int(ctx, 1), (GLuint)duk_get_uint(ctx, 2));
    return 0;
}
static duk_ret_t dukwebgl_stencilFuncSeparate(duk_context *ctx) {
    glStencilFuncSeparate((GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1),
                          (GLint)duk_get_int(ctx, 2), (GLuint)duk_get_uint(ctx, 3));
    return 0;
}
static duk_ret_t dukwebgl_stencilMask(duk_context *ctx) {
    glStencilMask((GLuint)duk_get_uint(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_stencilMaskSeparate(duk_context *ctx) {
    glStencilMaskSeparate((GLenum)duk_get_uint(ctx, 0), (GLuint)duk_get_uint(ctx, 1));
    return 0;
}
static duk_ret_t dukwebgl_stencilOp(duk_context *ctx) {
    glStencilOp((GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1), (GLenum)duk_get_uint(ctx, 2));
    return 0;
}
static duk_ret_t dukwebgl_stencilOpSeparate(duk_context *ctx) {
    glStencilOpSeparate((GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1),
                        (GLenum)duk_get_uint(ctx, 2), (GLenum)duk_get_uint(ctx, 3));
    return 0;
}

static duk_ret_t dukwebgl_cullFace(duk_context *ctx) {
    glCullFace((GLenum)duk_get_uint(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_frontFace(duk_context *ctx) {
    glFrontFace((GLenum)duk_get_uint(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_lineWidth(duk_context *ctx) {
    glLineWidth((GLfloat)duk_get_number(ctx, 0));
    return 0;
}
static duk_ret_t dukwebgl_polygonOffset(duk_context *ctx) {
    glPolygonOffset((GLfloat)duk_get_number(ctx, 0), (GLfloat)duk_get_number(ctx, 1));
    return 0;
}
static duk_ret_t dukwebgl_sampleCoverage(duk_context *ctx) {
    glSampleCoverage((GLfloat)duk_get_number(ctx, 0), duk_get_boolean(ctx, 1) ? GL_TRUE : GL_FALSE);
    return 0;
}

static duk_ret_t dukwebgl_hint(duk_context *ctx) {
    glHint((GLenum)duk_get_uint(ctx, 0), (GLenum)duk_get_uint(ctx, 1));
    return 0;
}

static duk_ret_t dukwebgl_flush(duk_context *ctx) {
    glFlush();
    return 0;
}
static duk_ret_t dukwebgl_finish(duk_context *ctx) {
    glFinish();
    return 0;
}

static duk_ret_t dukwebgl_getError(duk_context *ctx) {
    duk_push_uint(ctx, glGetError());
    return 1;
}

// ============================================================
// getParameter
// ============================================================

static duk_ret_t dukwebgl_getParameter(duk_context *ctx) {
    GLenum pname = (GLenum)duk_get_uint(ctx, 0);
    switch (pname) {
    // GLenum returns
    case GL_ACTIVE_TEXTURE:
    case GL_CULL_FACE_MODE:
    case GL_DEPTH_FUNC:
    case GL_FRONT_FACE:
    case GL_GENERATE_MIPMAP_HINT:
    case GL_BLEND_DST_ALPHA:
    case GL_BLEND_DST_RGB:
    case GL_BLEND_EQUATION_RGB:
    case GL_BLEND_EQUATION_ALPHA:
    case GL_BLEND_SRC_ALPHA:
    case GL_BLEND_SRC_RGB:
    case GL_STENCIL_BACK_FAIL:
    case GL_STENCIL_BACK_FUNC:
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
    case GL_STENCIL_BACK_PASS_DEPTH_PASS:
    case GL_STENCIL_FAIL:
    case GL_STENCIL_FUNC:
    case GL_STENCIL_PASS_DEPTH_FAIL:
    case GL_STENCIL_PASS_DEPTH_PASS:
    {
        GLint v = 0; glGetIntegerv(pname, &v);
        duk_push_uint(ctx, (GLenum)v);
        break;
    }
    // GLint returns
    case GL_ALPHA_BITS:
    case GL_BLUE_BITS:
    case GL_DEPTH_BITS:
    case GL_GREEN_BITS:
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
    case GL_MAX_RENDERBUFFER_SIZE:
    case GL_MAX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_TEXTURE_SIZE:
    case GL_MAX_VARYING_VECTORS:
    case GL_MAX_VERTEX_ATTRIBS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_UNIFORM_VECTORS:
    case GL_PACK_ALIGNMENT:
    case GL_RED_BITS:
    case GL_SAMPLE_BUFFERS:
    case GL_SAMPLES:
    case GL_STENCIL_BACK_REF:
    case GL_STENCIL_BITS:
    case GL_STENCIL_CLEAR_VALUE:
    case GL_STENCIL_REF:
    case GL_SUBPIXEL_BITS:
    case GL_UNPACK_ALIGNMENT:
    {
        GLint v = 0; glGetIntegerv(pname, &v);
        duk_push_int(ctx, v);
        break;
    }
    // GLfloat returns
    case GL_DEPTH_CLEAR_VALUE:
    case GL_LINE_WIDTH:
    case GL_POLYGON_OFFSET_FACTOR:
    case GL_POLYGON_OFFSET_UNITS:
    case GL_SAMPLE_COVERAGE_VALUE:
    {
        GLfloat v = 0; glGetFloatv(pname, &v);
        duk_push_number(ctx, v);
        break;
    }
    // GLboolean returns
    case GL_BLEND:
    case GL_CULL_FACE:
    case GL_DEPTH_TEST:
    case GL_DEPTH_WRITEMASK:
    case GL_DITHER:
    case GL_POLYGON_OFFSET_FILL:
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
    case GL_SAMPLE_COVERAGE:
    case GL_SAMPLE_COVERAGE_INVERT:
    case GL_SCISSOR_TEST:
    case GL_STENCIL_TEST:
    {
        GLint v = 0; glGetIntegerv(pname, &v);
        duk_push_boolean(ctx, v == GL_TRUE ? 1 : 0);
        break;
    }
    // string returns
    case GL_VENDOR:
    case GL_RENDERER:
    case GL_VERSION:
    case GL_SHADING_LANGUAGE_VERSION:
        duk_push_string(ctx, (const char*)glGetString(pname));
        break;
    default:
        duk_push_undefined(ctx);
        break;
    }
    return 1;
}

// ============================================================
// Uniform Block (WebGL2 / GLES3)
// ============================================================

static duk_ret_t dukwebgl_getUniformBlockIndex(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    const char *name = duk_get_string(ctx, 1);
    GLuint idx = glGetUniformBlockIndex(program, name);
    duk_push_uint(ctx, idx);
    return 1;
}

static duk_ret_t dukwebgl_uniformBlockBinding(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    GLuint blockIndex = (GLuint)duk_get_uint(ctx, 1);
    GLuint blockBinding = (GLuint)duk_get_uint(ctx, 2);
    glUniformBlockBinding(program, blockIndex, blockBinding);
    return 0;
}

static duk_ret_t dukwebgl_bindBufferBase(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLuint index = (GLuint)duk_get_uint(ctx, 1);
    GLuint buffer = dukwebgl_get_object_id_uint(ctx, 2);
    glBindBufferBase(target, index, buffer);
    return 0;
}

static duk_ret_t dukwebgl_bindBufferRange(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLuint index = (GLuint)duk_get_uint(ctx, 1);
    GLuint buffer = dukwebgl_get_object_id_uint(ctx, 2);
    GLintptr offset = (GLintptr)duk_get_int(ctx, 3);
    GLsizeiptr size = (GLsizeiptr)duk_get_int(ctx, 4);
    glBindBufferRange(target, index, buffer, offset, size);
    return 0;
}

// ============================================================
// Transform Feedback (WebGL2 / GLES3)
// ============================================================

static duk_ret_t dukwebgl_createTransformFeedback(duk_context *ctx) {
    GLuint id;
    glGenTransformFeedbacks(1, &id);
    dukwebgl_create_object_uint(ctx, id);
    return 1;
}

static duk_ret_t dukwebgl_deleteTransformFeedback(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteTransformFeedbacks(1, &id);
    return 0;
}

static duk_ret_t dukwebgl_bindTransformFeedback(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLuint id = dukwebgl_get_object_id_uint(ctx, 1);
    glBindTransformFeedback(target, id);
    return 0;
}

static duk_ret_t dukwebgl_beginTransformFeedback(duk_context *ctx) {
    glBeginTransformFeedback((GLenum)duk_get_uint(ctx, 0));
    return 0;
}

static duk_ret_t dukwebgl_endTransformFeedback(duk_context *ctx) {
    (void)ctx;
    glEndTransformFeedback();
    return 0;
}

static duk_ret_t dukwebgl_transformFeedbackVaryings(duk_context *ctx) {
    GLuint program = dukwebgl_get_object_id_uint(ctx, 0);
    // arg1 is array of strings
    if (!duk_is_array(ctx, 1)) return DUK_RET_TYPE_ERROR;
    duk_get_prop_string(ctx, 1, "length");
    GLsizei count = (GLsizei)duk_to_int(ctx, -1);
    duk_pop(ctx);
    const char **varyings = (const char**)malloc(sizeof(char*) * count);
    if (!varyings) return DUK_RET_ERROR;
    for (GLsizei i = 0; i < count; i++) {
        duk_get_prop_index(ctx, 1, i);
        varyings[i] = duk_to_string(ctx, -1);
    }
    GLenum bufferMode = (GLenum)duk_get_uint(ctx, 2);
    glTransformFeedbackVaryings(program, count, varyings, bufferMode);
    // pop all strings
    for (GLsizei i = 0; i < count; i++) duk_pop(ctx);
    free(varyings);
    return 0;
}

// ============================================================
// Query (WebGL2 / GLES3)
// ============================================================

static duk_ret_t dukwebgl_createQuery(duk_context *ctx) {
    GLuint id;
    glGenQueries(1, &id);
    dukwebgl_create_object_uint(ctx, id);
    return 1;
}

static duk_ret_t dukwebgl_deleteQuery(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteQueries(1, &id);
    return 0;
}

static duk_ret_t dukwebgl_beginQuery(duk_context *ctx) {
    GLenum target = (GLenum)duk_get_uint(ctx, 0);
    GLuint id = dukwebgl_get_object_id_uint(ctx, 1);
    glBeginQuery(target, id);
    return 0;
}

static duk_ret_t dukwebgl_endQuery(duk_context *ctx) {
    glEndQuery((GLenum)duk_get_uint(ctx, 0));
    return 0;
}

// ============================================================
// Sampler (WebGL2 / GLES3)
// ============================================================

static duk_ret_t dukwebgl_createSampler(duk_context *ctx) {
    GLuint id;
    glGenSamplers(1, &id);
    dukwebgl_create_object_uint(ctx, id);
    return 1;
}

static duk_ret_t dukwebgl_deleteSampler(duk_context *ctx) {
    GLuint id = dukwebgl_get_object_id_uint(ctx, 0);
    glDeleteSamplers(1, &id);
    return 0;
}

static duk_ret_t dukwebgl_bindSampler(duk_context *ctx) {
    GLuint unit = (GLuint)duk_get_uint(ctx, 0);
    GLuint sampler = dukwebgl_get_object_id_uint(ctx, 1);
    glBindSampler(unit, sampler);
    return 0;
}

static duk_ret_t dukwebgl_samplerParameteri(duk_context *ctx) {
    GLuint sampler = dukwebgl_get_object_id_uint(ctx, 0);
    GLenum pname = (GLenum)duk_get_uint(ctx, 1);
    GLint param = (GLint)duk_get_int(ctx, 2);
    glSamplerParameteri(sampler, pname, param);
    return 0;
}

static duk_ret_t dukwebgl_samplerParameterf(duk_context *ctx) {
    GLuint sampler = dukwebgl_get_object_id_uint(ctx, 0);
    GLenum pname = (GLenum)duk_get_uint(ctx, 1);
    GLfloat param = (GLfloat)duk_get_number(ctx, 2);
    glSamplerParameterf(sampler, pname, param);
    return 0;
}

// ============================================================
// clearBuffer (WebGL2 / GLES3)
// ============================================================

static duk_ret_t dukwebgl_clearBufferfv(duk_context *ctx) {
    GLenum buffer = (GLenum)duk_get_uint(ctx, 0);
    GLint drawbuffer = (GLint)duk_get_int(ctx, 1);
    const GLfloat *value = (const GLfloat *)duk_get_buffer_data(ctx, 2, NULL);
    glClearBufferfv(buffer, drawbuffer, value);
    return 0;
}

static duk_ret_t dukwebgl_clearBufferiv(duk_context *ctx) {
    GLenum buffer = (GLenum)duk_get_uint(ctx, 0);
    GLint drawbuffer = (GLint)duk_get_int(ctx, 1);
    const GLint *value = (const GLint *)duk_get_buffer_data(ctx, 2, NULL);
    glClearBufferiv(buffer, drawbuffer, value);
    return 0;
}

static duk_ret_t dukwebgl_clearBufferuiv(duk_context *ctx) {
    GLenum buffer = (GLenum)duk_get_uint(ctx, 0);
    GLint drawbuffer = (GLint)duk_get_int(ctx, 1);
    const GLuint *value = (const GLuint *)duk_get_buffer_data(ctx, 2, NULL);
    glClearBufferuiv(buffer, drawbuffer, value);
    return 0;
}

static duk_ret_t dukwebgl_clearBufferfi(duk_context *ctx) {
    GLenum buffer = (GLenum)duk_get_uint(ctx, 0);
    GLint drawbuffer = (GLint)duk_get_int(ctx, 1);
    GLfloat depth = (GLfloat)duk_get_number(ctx, 2);
    GLint stencil = (GLint)duk_get_int(ctx, 3);
    glClearBufferfi(buffer, drawbuffer, depth, stencil);
    return 0;
}

// ============================================================
// 定数登録
// ============================================================

static void dukwebgl_bind_constants(duk_context *ctx) {

#define PUSH_CONST(name) \
    duk_push_uint(ctx, GL_##name); \
    duk_put_prop_string(ctx, -2, #name)

    // データ型
    PUSH_CONST(BYTE);
    PUSH_CONST(UNSIGNED_BYTE);
    PUSH_CONST(SHORT);
    PUSH_CONST(UNSIGNED_SHORT);
    PUSH_CONST(INT);
    PUSH_CONST(UNSIGNED_INT);
    PUSH_CONST(FLOAT);
    PUSH_CONST(HALF_FLOAT);

    // プリミティブ
    PUSH_CONST(POINTS);
    PUSH_CONST(LINES);
    PUSH_CONST(LINE_LOOP);
    PUSH_CONST(LINE_STRIP);
    PUSH_CONST(TRIANGLES);
    PUSH_CONST(TRIANGLE_STRIP);
    PUSH_CONST(TRIANGLE_FAN);

    // ブレンド
    PUSH_CONST(ZERO);
    PUSH_CONST(ONE);
    PUSH_CONST(SRC_COLOR);
    PUSH_CONST(ONE_MINUS_SRC_COLOR);
    PUSH_CONST(DST_COLOR);
    PUSH_CONST(ONE_MINUS_DST_COLOR);
    PUSH_CONST(SRC_ALPHA);
    PUSH_CONST(ONE_MINUS_SRC_ALPHA);
    PUSH_CONST(DST_ALPHA);
    PUSH_CONST(ONE_MINUS_DST_ALPHA);
    PUSH_CONST(CONSTANT_COLOR);
    PUSH_CONST(ONE_MINUS_CONSTANT_COLOR);
    PUSH_CONST(CONSTANT_ALPHA);
    PUSH_CONST(ONE_MINUS_CONSTANT_ALPHA);
    PUSH_CONST(SRC_ALPHA_SATURATE);
    PUSH_CONST(FUNC_ADD);
    PUSH_CONST(FUNC_SUBTRACT);
    PUSH_CONST(FUNC_REVERSE_SUBTRACT);
    PUSH_CONST(MIN);
    PUSH_CONST(MAX);

    // バッファ
    PUSH_CONST(ARRAY_BUFFER);
    PUSH_CONST(ELEMENT_ARRAY_BUFFER);
    PUSH_CONST(UNIFORM_BUFFER);
    PUSH_CONST(TRANSFORM_FEEDBACK_BUFFER);
    PUSH_CONST(COPY_READ_BUFFER);
    PUSH_CONST(COPY_WRITE_BUFFER);
    PUSH_CONST(PIXEL_PACK_BUFFER);
    PUSH_CONST(PIXEL_UNPACK_BUFFER);
    PUSH_CONST(STREAM_DRAW);
    PUSH_CONST(STREAM_READ);
    PUSH_CONST(STREAM_COPY);
    PUSH_CONST(STATIC_DRAW);
    PUSH_CONST(STATIC_READ);
    PUSH_CONST(STATIC_COPY);
    PUSH_CONST(DYNAMIC_DRAW);
    PUSH_CONST(DYNAMIC_READ);
    PUSH_CONST(DYNAMIC_COPY);
    PUSH_CONST(ARRAY_BUFFER_BINDING);
    PUSH_CONST(ELEMENT_ARRAY_BUFFER_BINDING);
    PUSH_CONST(BUFFER_SIZE);
    PUSH_CONST(BUFFER_USAGE);

    // クリアビット
    PUSH_CONST(DEPTH_BUFFER_BIT);
    PUSH_CONST(STENCIL_BUFFER_BIT);
    PUSH_CONST(COLOR_BUFFER_BIT);

    // 有効/無効
    PUSH_CONST(BLEND);
    PUSH_CONST(CULL_FACE);
    PUSH_CONST(DEPTH_TEST);
    PUSH_CONST(DITHER);
    PUSH_CONST(POLYGON_OFFSET_FILL);
    PUSH_CONST(SAMPLE_ALPHA_TO_COVERAGE);
    PUSH_CONST(SAMPLE_COVERAGE);
    PUSH_CONST(SCISSOR_TEST);
    PUSH_CONST(STENCIL_TEST);
    PUSH_CONST(RASTERIZER_DISCARD);

    // 面
    PUSH_CONST(FRONT);
    PUSH_CONST(BACK);
    PUSH_CONST(FRONT_AND_BACK);
    PUSH_CONST(CW);
    PUSH_CONST(CCW);

    // 深度 / ステンシル
    PUSH_CONST(NEVER);
    PUSH_CONST(LESS);
    PUSH_CONST(EQUAL);
    PUSH_CONST(LEQUAL);
    PUSH_CONST(GREATER);
    PUSH_CONST(NOTEQUAL);
    PUSH_CONST(GEQUAL);
    PUSH_CONST(ALWAYS);
    PUSH_CONST(KEEP);
    PUSH_CONST(REPLACE);
    PUSH_CONST(INCR);
    PUSH_CONST(DECR);
    PUSH_CONST(INVERT);
    PUSH_CONST(INCR_WRAP);
    PUSH_CONST(DECR_WRAP);

    // シェーダ
    PUSH_CONST(VERTEX_SHADER);
    PUSH_CONST(FRAGMENT_SHADER);
    PUSH_CONST(COMPILE_STATUS);
    PUSH_CONST(LINK_STATUS);
    PUSH_CONST(VALIDATE_STATUS);
    PUSH_CONST(DELETE_STATUS);
    PUSH_CONST(SHADER_TYPE);
    PUSH_CONST(ATTACHED_SHADERS);
    PUSH_CONST(ACTIVE_ATTRIBUTES);
    PUSH_CONST(ACTIVE_UNIFORMS);
    PUSH_CONST(ACTIVE_UNIFORM_BLOCKS);
    PUSH_CONST(TRANSFORM_FEEDBACK_BUFFER_MODE);
    PUSH_CONST(TRANSFORM_FEEDBACK_VARYINGS);
    PUSH_CONST(MAX_VERTEX_ATTRIBS);
    PUSH_CONST(MAX_VERTEX_UNIFORM_VECTORS);
    PUSH_CONST(MAX_VARYING_VECTORS);
    PUSH_CONST(MAX_COMBINED_TEXTURE_IMAGE_UNITS);
    PUSH_CONST(MAX_VERTEX_TEXTURE_IMAGE_UNITS);
    PUSH_CONST(MAX_TEXTURE_IMAGE_UNITS);
    PUSH_CONST(MAX_FRAGMENT_UNIFORM_VECTORS);
    PUSH_CONST(CURRENT_PROGRAM);

    // uniform 型
    PUSH_CONST(FLOAT_VEC2);
    PUSH_CONST(FLOAT_VEC3);
    PUSH_CONST(FLOAT_VEC4);
    PUSH_CONST(INT_VEC2);
    PUSH_CONST(INT_VEC3);
    PUSH_CONST(INT_VEC4);
    PUSH_CONST(BOOL);
    PUSH_CONST(BOOL_VEC2);
    PUSH_CONST(BOOL_VEC3);
    PUSH_CONST(BOOL_VEC4);
    PUSH_CONST(FLOAT_MAT2);
    PUSH_CONST(FLOAT_MAT3);
    PUSH_CONST(FLOAT_MAT4);
    PUSH_CONST(SAMPLER_2D);
    PUSH_CONST(SAMPLER_3D);
    PUSH_CONST(SAMPLER_CUBE);
    PUSH_CONST(SAMPLER_2D_SHADOW);

    // テクスチャ
    PUSH_CONST(TEXTURE_2D);
    PUSH_CONST(TEXTURE_3D);
    PUSH_CONST(TEXTURE_CUBE_MAP);
    PUSH_CONST(TEXTURE_CUBE_MAP_POSITIVE_X);
    PUSH_CONST(TEXTURE_CUBE_MAP_NEGATIVE_X);
    PUSH_CONST(TEXTURE_CUBE_MAP_POSITIVE_Y);
    PUSH_CONST(TEXTURE_CUBE_MAP_NEGATIVE_Y);
    PUSH_CONST(TEXTURE_CUBE_MAP_POSITIVE_Z);
    PUSH_CONST(TEXTURE_CUBE_MAP_NEGATIVE_Z);
    PUSH_CONST(TEXTURE_2D_ARRAY);
    PUSH_CONST(TEXTURE0);
    PUSH_CONST(TEXTURE1);
    PUSH_CONST(TEXTURE2);
    PUSH_CONST(TEXTURE3);
    PUSH_CONST(TEXTURE4);
    PUSH_CONST(TEXTURE5);
    PUSH_CONST(TEXTURE6);
    PUSH_CONST(TEXTURE7);
    PUSH_CONST(TEXTURE8);
    PUSH_CONST(TEXTURE9);
    PUSH_CONST(TEXTURE10);
    PUSH_CONST(TEXTURE11);
    PUSH_CONST(TEXTURE12);
    PUSH_CONST(TEXTURE13);
    PUSH_CONST(TEXTURE14);
    PUSH_CONST(TEXTURE15);
    PUSH_CONST(TEXTURE16);
    PUSH_CONST(TEXTURE17);
    PUSH_CONST(TEXTURE18);
    PUSH_CONST(TEXTURE19);
    PUSH_CONST(TEXTURE20);
    PUSH_CONST(TEXTURE21);
    PUSH_CONST(TEXTURE22);
    PUSH_CONST(TEXTURE23);
    PUSH_CONST(TEXTURE24);
    PUSH_CONST(TEXTURE25);
    PUSH_CONST(TEXTURE26);
    PUSH_CONST(TEXTURE27);
    PUSH_CONST(TEXTURE28);
    PUSH_CONST(TEXTURE29);
    PUSH_CONST(TEXTURE30);
    PUSH_CONST(TEXTURE31);
    PUSH_CONST(ACTIVE_TEXTURE);
    PUSH_CONST(TEXTURE_MIN_FILTER);
    PUSH_CONST(TEXTURE_MAG_FILTER);
    PUSH_CONST(TEXTURE_WRAP_S);
    PUSH_CONST(TEXTURE_WRAP_T);
    PUSH_CONST(TEXTURE_WRAP_R);
    PUSH_CONST(TEXTURE_MIN_LOD);
    PUSH_CONST(TEXTURE_MAX_LOD);
    PUSH_CONST(TEXTURE_BASE_LEVEL);
    PUSH_CONST(TEXTURE_MAX_LEVEL);
    PUSH_CONST(TEXTURE_COMPARE_MODE);
    PUSH_CONST(TEXTURE_COMPARE_FUNC);
    PUSH_CONST(NEAREST);
    PUSH_CONST(LINEAR);
    PUSH_CONST(NEAREST_MIPMAP_NEAREST);
    PUSH_CONST(LINEAR_MIPMAP_NEAREST);
    PUSH_CONST(NEAREST_MIPMAP_LINEAR);
    PUSH_CONST(LINEAR_MIPMAP_LINEAR);
    PUSH_CONST(REPEAT);
    PUSH_CONST(CLAMP_TO_EDGE);
    PUSH_CONST(MIRRORED_REPEAT);
    PUSH_CONST(MAX_TEXTURE_SIZE);
    PUSH_CONST(MAX_CUBE_MAP_TEXTURE_SIZE);

    // ピクセルフォーマット
    PUSH_CONST(ALPHA);
    PUSH_CONST(RGB);
    PUSH_CONST(RGBA);
    PUSH_CONST(LUMINANCE);
    PUSH_CONST(LUMINANCE_ALPHA);
    PUSH_CONST(DEPTH_COMPONENT);
    PUSH_CONST(DEPTH_COMPONENT16);
    PUSH_CONST(DEPTH_COMPONENT24);
    PUSH_CONST(DEPTH_COMPONENT32F);
    PUSH_CONST(DEPTH_STENCIL);
    PUSH_CONST(DEPTH24_STENCIL8);
    PUSH_CONST(DEPTH32F_STENCIL8);
    PUSH_CONST(R8);
    PUSH_CONST(RG8);
    PUSH_CONST(RGB8);
    PUSH_CONST(RGBA8);
    PUSH_CONST(R16F);
    PUSH_CONST(RG16F);
    PUSH_CONST(RGB16F);
    PUSH_CONST(RGBA16F);
    PUSH_CONST(R32F);
    PUSH_CONST(RG32F);
    PUSH_CONST(RGB32F);
    PUSH_CONST(RGBA32F);
    PUSH_CONST(R8I);
    PUSH_CONST(R8UI);
    PUSH_CONST(R16I);
    PUSH_CONST(R16UI);
    PUSH_CONST(R32I);
    PUSH_CONST(R32UI);
    PUSH_CONST(RG8I);
    PUSH_CONST(RG8UI);
    PUSH_CONST(RG16I);
    PUSH_CONST(RG16UI);
    PUSH_CONST(RG32I);
    PUSH_CONST(RG32UI);
    PUSH_CONST(RGBA8I);
    PUSH_CONST(RGBA8UI);
    PUSH_CONST(RGBA16I);
    PUSH_CONST(RGBA16UI);
    PUSH_CONST(RGBA32I);
    PUSH_CONST(RGBA32UI);
    PUSH_CONST(RED);
    PUSH_CONST(RED_INTEGER);
    PUSH_CONST(RG);
    PUSH_CONST(RG_INTEGER);
    PUSH_CONST(RGB_INTEGER);
    PUSH_CONST(RGBA_INTEGER);
    PUSH_CONST(SRGB8);
    PUSH_CONST(SRGB8_ALPHA8);
    PUSH_CONST(RGB10_A2);
    PUSH_CONST(RGB10_A2UI);
    PUSH_CONST(R11F_G11F_B10F);
    PUSH_CONST(RGB9_E5);
    PUSH_CONST(UNSIGNED_SHORT_5_6_5);
    PUSH_CONST(UNSIGNED_SHORT_4_4_4_4);
    PUSH_CONST(UNSIGNED_SHORT_5_5_5_1);
    PUSH_CONST(UNSIGNED_INT_2_10_10_10_REV);
    PUSH_CONST(UNSIGNED_INT_10F_11F_11F_REV);
    PUSH_CONST(UNSIGNED_INT_5_9_9_9_REV);
    PUSH_CONST(UNSIGNED_INT_24_8);
    PUSH_CONST(FLOAT_32_UNSIGNED_INT_24_8_REV);

    // フレームバッファ
    PUSH_CONST(FRAMEBUFFER);
    PUSH_CONST(READ_FRAMEBUFFER);
    PUSH_CONST(DRAW_FRAMEBUFFER);
    PUSH_CONST(RENDERBUFFER);
    PUSH_CONST(COLOR_ATTACHMENT0);
    PUSH_CONST(COLOR_ATTACHMENT1);
    PUSH_CONST(COLOR_ATTACHMENT2);
    PUSH_CONST(COLOR_ATTACHMENT3);
    PUSH_CONST(COLOR_ATTACHMENT4);
    PUSH_CONST(COLOR_ATTACHMENT5);
    PUSH_CONST(COLOR_ATTACHMENT6);
    PUSH_CONST(COLOR_ATTACHMENT7);
    PUSH_CONST(COLOR_ATTACHMENT8);
    PUSH_CONST(COLOR_ATTACHMENT9);
    PUSH_CONST(COLOR_ATTACHMENT10);
    PUSH_CONST(COLOR_ATTACHMENT11);
    PUSH_CONST(COLOR_ATTACHMENT12);
    PUSH_CONST(COLOR_ATTACHMENT13);
    PUSH_CONST(COLOR_ATTACHMENT14);
    PUSH_CONST(COLOR_ATTACHMENT15);
    PUSH_CONST(DEPTH_ATTACHMENT);
    PUSH_CONST(STENCIL_ATTACHMENT);
    PUSH_CONST(DEPTH_STENCIL_ATTACHMENT);
    PUSH_CONST(FRAMEBUFFER_COMPLETE);
    PUSH_CONST(FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    PUSH_CONST(FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
    PUSH_CONST(FRAMEBUFFER_INCOMPLETE_DIMENSIONS);
    PUSH_CONST(FRAMEBUFFER_UNSUPPORTED);
    PUSH_CONST(FRAMEBUFFER_BINDING);
    PUSH_CONST(RENDERBUFFER_BINDING);
    PUSH_CONST(MAX_RENDERBUFFER_SIZE);
    PUSH_CONST(NONE);

    // ピクセルストア
    PUSH_CONST(PACK_ALIGNMENT);
    PUSH_CONST(UNPACK_ALIGNMENT);
    PUSH_CONST(PACK_ROW_LENGTH);
    PUSH_CONST(PACK_SKIP_PIXELS);
    PUSH_CONST(PACK_SKIP_ROWS);
    PUSH_CONST(UNPACK_ROW_LENGTH);
    PUSH_CONST(UNPACK_IMAGE_HEIGHT);
    PUSH_CONST(UNPACK_SKIP_PIXELS);
    PUSH_CONST(UNPACK_SKIP_ROWS);
    PUSH_CONST(UNPACK_SKIP_IMAGES);

    // エラー
    PUSH_CONST(NO_ERROR);
    PUSH_CONST(INVALID_ENUM);
    PUSH_CONST(INVALID_VALUE);
    PUSH_CONST(INVALID_OPERATION);
    PUSH_CONST(INVALID_FRAMEBUFFER_OPERATION);
    PUSH_CONST(OUT_OF_MEMORY);

    // Transform Feedback
    PUSH_CONST(INTERLEAVED_ATTRIBS);
    PUSH_CONST(SEPARATE_ATTRIBS);

    // Misc
    PUSH_CONST(VENDOR);
    PUSH_CONST(RENDERER);
    PUSH_CONST(VERSION);
    PUSH_CONST(SHADING_LANGUAGE_VERSION);
    PUSH_CONST(GENERATE_MIPMAP_HINT);
    PUSH_CONST(FASTEST);
    PUSH_CONST(NICEST);
    PUSH_CONST(DONT_CARE);

    // Compare mode
    PUSH_CONST(COMPARE_REF_TO_TEXTURE);

    // color
    PUSH_CONST(COLOR);
    PUSH_CONST(DEPTH);
    PUSH_CONST(STENCIL);

#undef PUSH_CONST
}

// ============================================================
// バインド登録: dukwebgl_bind
// ============================================================

#define BIND_FUNC(jsName, cFunc, nargs) \
    duk_push_c_function(ctx, cFunc, nargs); \
    duk_put_prop_string(ctx, -2, #jsName)

void dukwebgl_bind(duk_context *ctx) {
    // WebGL2RenderingContext コンストラクタ
    duk_push_c_function(ctx, [](duk_context *c) -> duk_ret_t {
        duk_push_object(c);
        if (!duk_is_constructor_call(c)) return DUK_RET_TYPE_ERROR;
        return 0;
    }, 0);

    // prototype オブジェクト
    duk_push_object(ctx);

    // 定数登録
    dukwebgl_bind_constants(ctx);

    // コンテキスト情報
    BIND_FUNC(getContextAttributes, dukwebgl_getContextAttributes, 0);
    BIND_FUNC(isContextLost, dukwebgl_isContextLost, 0);
    BIND_FUNC(getSupportedExtensions, dukwebgl_getSupportedExtensions, 0);
    BIND_FUNC(getExtension, dukwebgl_getExtension, 1);
    BIND_FUNC(getParameter, dukwebgl_getParameter, 1);
    BIND_FUNC(getError, dukwebgl_getError, 0);

    // シェーダ
    BIND_FUNC(createShader, dukwebgl_createShader, 1);
    BIND_FUNC(deleteShader, dukwebgl_deleteShader, 1);
    BIND_FUNC(shaderSource, dukwebgl_shaderSource, 2);
    BIND_FUNC(compileShader, dukwebgl_compileShader, 1);
    BIND_FUNC(getShaderParameter, dukwebgl_getShaderParameter, 2);
    BIND_FUNC(getShaderInfoLog, dukwebgl_getShaderInfoLog, 1);
    BIND_FUNC(getShaderSource, dukwebgl_getShaderSource, 1);
    BIND_FUNC(isShader, dukwebgl_isShader, 1);

    // プログラム
    BIND_FUNC(createProgram, dukwebgl_createProgram, 0);
    BIND_FUNC(deleteProgram, dukwebgl_deleteProgram, 1);
    BIND_FUNC(attachShader, dukwebgl_attachShader, 2);
    BIND_FUNC(detachShader, dukwebgl_detachShader, 2);
    BIND_FUNC(linkProgram, dukwebgl_linkProgram, 1);
    BIND_FUNC(useProgram, dukwebgl_useProgram, 1);
    BIND_FUNC(validateProgram, dukwebgl_validateProgram, 1);
    BIND_FUNC(isProgram, dukwebgl_isProgram, 1);
    BIND_FUNC(getProgramParameter, dukwebgl_getProgramParameter, 2);
    BIND_FUNC(getProgramInfoLog, dukwebgl_getProgramInfoLog, 1);
    BIND_FUNC(bindAttribLocation, dukwebgl_bindAttribLocation, 3);
    BIND_FUNC(getAttribLocation, dukwebgl_getAttribLocation, 2);
    BIND_FUNC(getUniformLocation, dukwebgl_getUniformLocation, 2);
    BIND_FUNC(getActiveAttrib, dukwebgl_getActiveAttrib, 2);
    BIND_FUNC(getActiveUniform, dukwebgl_getActiveUniform, 2);

    // バッファ
    BIND_FUNC(createBuffer, dukwebgl_createBuffer, 0);
    BIND_FUNC(deleteBuffer, dukwebgl_deleteBuffer, 1);
    BIND_FUNC(isBuffer, dukwebgl_isBuffer, 1);
    BIND_FUNC(bindBuffer, dukwebgl_bindBuffer, 2);
    BIND_FUNC(bufferData, dukwebgl_bufferData, DUK_VARARGS);
    BIND_FUNC(bufferSubData, dukwebgl_bufferSubData, DUK_VARARGS);

    // テクスチャ
    BIND_FUNC(createTexture, dukwebgl_createTexture, 0);
    BIND_FUNC(deleteTexture, dukwebgl_deleteTexture, 1);
    BIND_FUNC(isTexture, dukwebgl_isTexture, 1);
    BIND_FUNC(bindTexture, dukwebgl_bindTexture, 2);
    BIND_FUNC(activeTexture, dukwebgl_activeTexture, 1);
    BIND_FUNC(texParameteri, dukwebgl_texParameteri, 3);
    BIND_FUNC(texParameterf, dukwebgl_texParameterf, 3);
    BIND_FUNC(generateMipmap, dukwebgl_generateMipmap, 1);
    BIND_FUNC(texImage2D, dukwebgl_texImage2D, DUK_VARARGS);
    BIND_FUNC(texSubImage2D, dukwebgl_texSubImage2D, 9);
    BIND_FUNC(texImage3D, dukwebgl_texImage3D, DUK_VARARGS);
    BIND_FUNC(copyTexImage2D, dukwebgl_copyTexImage2D, 8);
    BIND_FUNC(copyTexSubImage2D, dukwebgl_copyTexSubImage2D, 8);
    BIND_FUNC(pixelStorei, dukwebgl_pixelStorei, 2);
    BIND_FUNC(readPixels, dukwebgl_readPixels, 7);

    // フレームバッファ / レンダーバッファ
    BIND_FUNC(createFramebuffer, dukwebgl_createFramebuffer, 0);
    BIND_FUNC(deleteFramebuffer, dukwebgl_deleteFramebuffer, 1);
    BIND_FUNC(isFramebuffer, dukwebgl_isFramebuffer, 1);
    BIND_FUNC(bindFramebuffer, dukwebgl_bindFramebuffer, 2);
    BIND_FUNC(framebufferTexture2D, dukwebgl_framebufferTexture2D, 5);
    BIND_FUNC(framebufferRenderbuffer, dukwebgl_framebufferRenderbuffer, 4);
    BIND_FUNC(checkFramebufferStatus, dukwebgl_checkFramebufferStatus, 1);
    BIND_FUNC(createRenderbuffer, dukwebgl_createRenderbuffer, 0);
    BIND_FUNC(deleteRenderbuffer, dukwebgl_deleteRenderbuffer, 1);
    BIND_FUNC(isRenderbuffer, dukwebgl_isRenderbuffer, 1);
    BIND_FUNC(bindRenderbuffer, dukwebgl_bindRenderbuffer, 2);
    BIND_FUNC(renderbufferStorage, dukwebgl_renderbufferStorage, 4);
    BIND_FUNC(drawBuffers, dukwebgl_drawBuffers, 1);
    BIND_FUNC(blitFramebuffer, dukwebgl_blitFramebuffer, 10);

    // VAO
    BIND_FUNC(createVertexArray, dukwebgl_createVertexArray, 0);
    BIND_FUNC(deleteVertexArray, dukwebgl_deleteVertexArray, 1);
    BIND_FUNC(isVertexArray, dukwebgl_isVertexArray, 1);
    BIND_FUNC(bindVertexArray, dukwebgl_bindVertexArray, 1);

    // 頂点属性
    BIND_FUNC(enableVertexAttribArray, dukwebgl_enableVertexAttribArray, 1);
    BIND_FUNC(disableVertexAttribArray, dukwebgl_disableVertexAttribArray, 1);
    BIND_FUNC(vertexAttribPointer, dukwebgl_vertexAttribPointer, 6);
    BIND_FUNC(vertexAttribIPointer, dukwebgl_vertexAttribIPointer, 5);
    BIND_FUNC(vertexAttribDivisor, dukwebgl_vertexAttribDivisor, 2);
    BIND_FUNC(vertexAttrib1f, dukwebgl_vertexAttrib1f, 2);
    BIND_FUNC(vertexAttrib2f, dukwebgl_vertexAttrib2f, 3);
    BIND_FUNC(vertexAttrib3f, dukwebgl_vertexAttrib3f, 4);
    BIND_FUNC(vertexAttrib4f, dukwebgl_vertexAttrib4f, 5);

    // Uniform (scalar)
    BIND_FUNC(uniform1i, dukwebgl_uniform1i, 2);
    BIND_FUNC(uniform2i, dukwebgl_uniform2i, 3);
    BIND_FUNC(uniform3i, dukwebgl_uniform3i, 4);
    BIND_FUNC(uniform4i, dukwebgl_uniform4i, 5);
    BIND_FUNC(uniform1f, dukwebgl_uniform1f, 2);
    BIND_FUNC(uniform2f, dukwebgl_uniform2f, 3);
    BIND_FUNC(uniform3f, dukwebgl_uniform3f, 4);
    BIND_FUNC(uniform4f, dukwebgl_uniform4f, 5);

    // Uniform (vector)
    BIND_FUNC(uniform1fv, dukwebgl_uniform1fv, 2);
    BIND_FUNC(uniform2fv, dukwebgl_uniform2fv, 2);
    BIND_FUNC(uniform3fv, dukwebgl_uniform3fv, 2);
    BIND_FUNC(uniform4fv, dukwebgl_uniform4fv, 2);
    BIND_FUNC(uniform1iv, dukwebgl_uniform1iv, 2);
    BIND_FUNC(uniform2iv, dukwebgl_uniform2iv, 2);
    BIND_FUNC(uniform3iv, dukwebgl_uniform3iv, 2);
    BIND_FUNC(uniform4iv, dukwebgl_uniform4iv, 2);
    BIND_FUNC(uniform1uiv, dukwebgl_uniform1uiv, 2);
    BIND_FUNC(uniform2uiv, dukwebgl_uniform2uiv, 2);
    BIND_FUNC(uniform3uiv, dukwebgl_uniform3uiv, 2);
    BIND_FUNC(uniform4uiv, dukwebgl_uniform4uiv, 2);

    // Uniform (matrix)
    BIND_FUNC(uniformMatrix2fv, dukwebgl_uniformMatrix2fv, DUK_VARARGS);
    BIND_FUNC(uniformMatrix3fv, dukwebgl_uniformMatrix3fv, DUK_VARARGS);
    BIND_FUNC(uniformMatrix4fv, dukwebgl_uniformMatrix4fv, DUK_VARARGS);
    BIND_FUNC(uniformMatrix2x3fv, dukwebgl_uniformMatrix2x3fv, DUK_VARARGS);
    BIND_FUNC(uniformMatrix2x4fv, dukwebgl_uniformMatrix2x4fv, DUK_VARARGS);
    BIND_FUNC(uniformMatrix3x2fv, dukwebgl_uniformMatrix3x2fv, DUK_VARARGS);
    BIND_FUNC(uniformMatrix3x4fv, dukwebgl_uniformMatrix3x4fv, DUK_VARARGS);
    BIND_FUNC(uniformMatrix4x2fv, dukwebgl_uniformMatrix4x2fv, DUK_VARARGS);
    BIND_FUNC(uniformMatrix4x3fv, dukwebgl_uniformMatrix4x3fv, DUK_VARARGS);

    // 描画
    BIND_FUNC(drawArrays, dukwebgl_drawArrays, 3);
    BIND_FUNC(drawElements, dukwebgl_drawElements, 4);
    BIND_FUNC(drawArraysInstanced, dukwebgl_drawArraysInstanced, 4);
    BIND_FUNC(drawElementsInstanced, dukwebgl_drawElementsInstanced, 5);

    // ステート
    BIND_FUNC(enable, dukwebgl_enable, 1);
    BIND_FUNC(disable, dukwebgl_disable, 1);
    BIND_FUNC(isEnabled, dukwebgl_isEnabled, 1);
    BIND_FUNC(viewport, dukwebgl_viewport, 4);
    BIND_FUNC(scissor, dukwebgl_scissor, 4);
    BIND_FUNC(clearColor, dukwebgl_clearColor, 4);
    BIND_FUNC(clearDepth, dukwebgl_clearDepth, 1);
    BIND_FUNC(clearStencil, dukwebgl_clearStencil, 1);
    BIND_FUNC(clear, dukwebgl_clear, 1);
    BIND_FUNC(colorMask, dukwebgl_colorMask, 4);
    BIND_FUNC(depthMask, dukwebgl_depthMask, 1);
    BIND_FUNC(depthFunc, dukwebgl_depthFunc, 1);
    BIND_FUNC(depthRange, dukwebgl_depthRange, 2);
    BIND_FUNC(blendFunc, dukwebgl_blendFunc, 2);
    BIND_FUNC(blendFuncSeparate, dukwebgl_blendFuncSeparate, 4);
    BIND_FUNC(blendEquation, dukwebgl_blendEquation, 1);
    BIND_FUNC(blendEquationSeparate, dukwebgl_blendEquationSeparate, 2);
    BIND_FUNC(blendColor, dukwebgl_blendColor, 4);
    BIND_FUNC(stencilFunc, dukwebgl_stencilFunc, 3);
    BIND_FUNC(stencilFuncSeparate, dukwebgl_stencilFuncSeparate, 4);
    BIND_FUNC(stencilMask, dukwebgl_stencilMask, 1);
    BIND_FUNC(stencilMaskSeparate, dukwebgl_stencilMaskSeparate, 2);
    BIND_FUNC(stencilOp, dukwebgl_stencilOp, 3);
    BIND_FUNC(stencilOpSeparate, dukwebgl_stencilOpSeparate, 4);
    BIND_FUNC(cullFace, dukwebgl_cullFace, 1);
    BIND_FUNC(frontFace, dukwebgl_frontFace, 1);
    BIND_FUNC(lineWidth, dukwebgl_lineWidth, 1);
    BIND_FUNC(polygonOffset, dukwebgl_polygonOffset, 2);
    BIND_FUNC(sampleCoverage, dukwebgl_sampleCoverage, 2);
    BIND_FUNC(hint, dukwebgl_hint, 2);
    BIND_FUNC(flush, dukwebgl_flush, 0);
    BIND_FUNC(finish, dukwebgl_finish, 0);

    // clearBuffer (WebGL2)
    BIND_FUNC(clearBufferfv, dukwebgl_clearBufferfv, DUK_VARARGS);
    BIND_FUNC(clearBufferiv, dukwebgl_clearBufferiv, DUK_VARARGS);
    BIND_FUNC(clearBufferuiv, dukwebgl_clearBufferuiv, DUK_VARARGS);
    BIND_FUNC(clearBufferfi, dukwebgl_clearBufferfi, 4);

    // Uniform Block
    BIND_FUNC(getUniformBlockIndex, dukwebgl_getUniformBlockIndex, 2);
    BIND_FUNC(uniformBlockBinding, dukwebgl_uniformBlockBinding, 3);
    BIND_FUNC(bindBufferBase, dukwebgl_bindBufferBase, 3);
    BIND_FUNC(bindBufferRange, dukwebgl_bindBufferRange, 5);

    // Transform Feedback
    BIND_FUNC(createTransformFeedback, dukwebgl_createTransformFeedback, 0);
    BIND_FUNC(deleteTransformFeedback, dukwebgl_deleteTransformFeedback, 1);
    BIND_FUNC(bindTransformFeedback, dukwebgl_bindTransformFeedback, 2);
    BIND_FUNC(beginTransformFeedback, dukwebgl_beginTransformFeedback, 1);
    BIND_FUNC(endTransformFeedback, dukwebgl_endTransformFeedback, 0);
    BIND_FUNC(transformFeedbackVaryings, dukwebgl_transformFeedbackVaryings, 3);

    // Query
    BIND_FUNC(createQuery, dukwebgl_createQuery, 0);
    BIND_FUNC(deleteQuery, dukwebgl_deleteQuery, 1);
    BIND_FUNC(beginQuery, dukwebgl_beginQuery, 2);
    BIND_FUNC(endQuery, dukwebgl_endQuery, 1);

    // Sampler
    BIND_FUNC(createSampler, dukwebgl_createSampler, 0);
    BIND_FUNC(deleteSampler, dukwebgl_deleteSampler, 1);
    BIND_FUNC(bindSampler, dukwebgl_bindSampler, 2);
    BIND_FUNC(samplerParameteri, dukwebgl_samplerParameteri, 3);
    BIND_FUNC(samplerParameterf, dukwebgl_samplerParameterf, 3);

    // prototype 設定
    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_global_string(ctx, "WebGL2RenderingContext");

    // グローバルに gl オブジェクトとして即座に使えるインスタンスも作成
    duk_eval_string(ctx, "new WebGL2RenderingContext()");
    duk_put_global_string(ctx, "gl");
}

#undef BIND_FUNC
