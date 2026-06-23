# sysinit.js を C++ ソース (builtin_sysinit.cpp) に埋め込む。
# INPUT  = 入力 JS パス
# OUTPUT = 出力 C++ パス
#
# JS の中身を unsigned char 配列としてバイト単位で埋め込む。
# raw string literal は JS 内の "...)..." 文字列等と衝突しうるので
# HEX 配列に統一する。

file(READ "${INPUT}" CONTENT HEX)
string(LENGTH "${CONTENT}" HEX_LEN)
math(EXPR BYTE_LEN "${HEX_LEN} / 2")

set(OUT "// Auto-generated from src/sysinit.js — do not edit by hand.\n")
string(APPEND OUT "// 編集は src/sysinit.js に対して行うこと。\n\n")
string(APPEND OUT "extern const unsigned char g_sysinit_js[];\n")
string(APPEND OUT "extern const unsigned int  g_sysinit_js_len;\n\n")
string(APPEND OUT "const unsigned char g_sysinit_js[] = {\n    ")

set(COL 0)
set(IDX 0)
while(IDX LESS HEX_LEN)
    string(SUBSTRING "${CONTENT}" ${IDX} 2 BYTE)
    string(APPEND OUT "0x${BYTE},")
    math(EXPR COL "${COL} + 1")
    if(COL EQUAL 16)
        string(APPEND OUT "\n    ")
        set(COL 0)
    else()
        string(APPEND OUT " ")
    endif()
    math(EXPR IDX "${IDX} + 2")
endwhile()

string(APPEND OUT "0x00\n};\n")  # QuickJS JS_Eval は null 終端を期待するので 1 byte 追加 (長さには含めない)
string(APPEND OUT "const unsigned int g_sysinit_js_len = ${BYTE_LEN};\n")

file(WRITE "${OUTPUT}" "${OUT}")
