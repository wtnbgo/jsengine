# Duktape Bug: String.prototype.replace 64-bit underflow

## Summary

`String.prototype.replace` with global RegExp (`/g` flag) causes a `RangeError: buffer too long` on 64-bit platforms when `match_start_boff < prev_match_end_boff` due to unsigned integer underflow.

## Affected Version

- Duktape 2.7.0 (latest release as of 2026-04)
- Likely affects all versions on 64-bit platforms

## Platform

- Windows 11 x64, MSVC (x64 target)
- `sizeof(duk_size_t) == 8` (64-bit `size_t`)

## Root Cause

In `duk_bi_string.c`, function `duk_bi_string_prototype_replace`, line:

```c
tmp_sz = (duk_size_t) (match_start_boff - prev_match_end_boff);
```

Both `match_start_boff` and `prev_match_end_boff` are `duk_uint32_t`. When `match_start_boff < prev_match_end_boff`, the subtraction underflows to a large `duk_uint32_t` value (~4 billion). This value is then cast to `duk_size_t`.

On **32-bit** platforms, `duk_size_t` is 32-bit, so the large value wraps to a valid (but incorrect) offset, and the subsequent `DUK_HBUFFER_MAX_BYTELEN` check (0x7ffffffe) may catch it or the BufWriter overflow check `new_sz < curr_off` catches the wrap.

On **64-bit** platforms, `duk_size_t` is 64-bit. The underflowed `duk_uint32_t` value (e.g., 0xFFFFFFEF = -17 as uint32) is **zero-extended** to 64-bit (0x00000000FFFFFFEF = 4294967279), which:
1. Passes the BufWriter overflow check (`new_sz = curr_off + 4294967279 + add_sz` does NOT wrap on 64-bit)
2. Results in `new_sz ≈ 4.3 GB`, which exceeds `DUK_HBUFFER_MAX_BYTELEN` (0x7FFFFFFE ≈ 2.1 GB)
3. Triggers `DUK_ERROR_RANGE(thr, "buffer too long")`

The same issue exists in the trailer copy:

```c
tmp_sz = (duk_size_t) (DUK_HSTRING_GET_BYTELEN(h_input) - prev_match_end_boff);
```

## Observed Error

```
RangeError: buffer too long
    at [anon] (duk_hbuffer_ops.c:26) internal
    at replace () native strict preventsyield
```

Debug output showing the actual values:

```
DUKTAPE BW: too large! curr_off=0xbda sz=0xffffffef add_sz=0x336 new_sz=0x100000eff
```

- `curr_off = 0xBDA` (3034 bytes written so far)
- `sz = 0xFFFFFFEF` (-17 as uint32, zero-extended to uint64 = 4294967279)
- `new_sz = 0x100000EFF` (4294971135) — exceeds max

## Reproduction

The bug is triggered by three.js r128 shader compilation, specifically in `acquireProgram` → `WebGLProgram` constructor → `resolveIncludes` function which uses `String.prototype.replace` with a global multiline RegExp:

```javascript
var pattern = /^[ \t]*#include +<([\w\d./]+)>/gm;
shaderSource.replace(pattern, function(match, name) {
    return resolvedChunk;
});
```

The shader source is ~6000-8000 characters with multiple `#include` directives resolved recursively.

## Fix

Add underflow guards before the subtraction:

```c
// Before (buggy):
tmp_sz = (duk_size_t) (match_start_boff - prev_match_end_boff);
DUK_BW_WRITE_ENSURE_BYTES(thr, bw, DUK_HSTRING_GET_DATA(h_input) + prev_match_end_boff, tmp_sz);

// After (fixed):
if (match_start_boff >= prev_match_end_boff) {
    tmp_sz = (duk_size_t) (match_start_boff - prev_match_end_boff);
    DUK_BW_WRITE_ENSURE_BYTES(thr, bw, DUK_HSTRING_GET_DATA(h_input) + prev_match_end_boff, tmp_sz);
}
```

Same fix for the trailer copy:

```c
// Before:
tmp_sz = (duk_size_t) (DUK_HSTRING_GET_BYTELEN(h_input) - prev_match_end_boff);
DUK_BW_WRITE_ENSURE_BYTES(thr, bw, DUK_HSTRING_GET_DATA(h_input) + prev_match_end_boff, tmp_sz);

// After:
if (DUK_HSTRING_GET_BYTELEN(h_input) >= prev_match_end_boff) {
    tmp_sz = (duk_size_t) (DUK_HSTRING_GET_BYTELEN(h_input) - prev_match_end_boff);
    DUK_BW_WRITE_ENSURE_BYTES(thr, bw, DUK_HSTRING_GET_DATA(h_input) + prev_match_end_boff, tmp_sz);
}
```

## Locations in Source (duktape 2.7.0 combined source)

- Line 43502: mid-string copy before replacement
- Line 43675: trailer copy after all replacements

In the original source file: `src-input/duk_bi_string.c`, function `duk_bi_string_prototype_replace`.

## Additional Notes

The BufWriter overflow check in `duk_bw_resize` (duk_util_bufwriter.c):

```c
if (DUK_UNLIKELY(new_sz < curr_off)) { /* overflow */ }
```

This check assumes 32-bit arithmetic where overflow wraps. On 64-bit, the addition `curr_off + sz + add_sz` does not wrap, so `new_sz` remains larger than `curr_off`, and the check passes. The error is then caught by `duk_hbuffer_resize`'s `DUK_HBUFFER_MAX_BYTELEN` check, which produces the "buffer too long" error message.

A more robust overflow check would be:

```c
if (DUK_UNLIKELY(new_sz < curr_off || new_sz > DUK_HBUFFER_MAX_BYTELEN)) { /* overflow or too large */ }
```
