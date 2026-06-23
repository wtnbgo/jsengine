# JS ファイルを C++ ソースに埋め込む汎用スクリプト。
# 入力:
#   INPUT  = 入力 JS パス
#   OUTPUT = 出力 C++ パス
#   SYMBOL = 配列のシンボル名 (例: g_sysinit_js)。 長さは ${SYMBOL}_len。
#
# JS の中身を unsigned char 配列としてバイト単位で埋め込む。
# raw string literal は JS 内の "...)..." 文字列等と衝突しうるので HEX 配列に統一。
# 末尾に null (0x00) を 1 byte 追加 (長さには含めない)。 QuickJS JS_Eval が null
# 終端を期待するため。

file(READ "${INPUT}" CONTENT HEX)
string(LENGTH "${CONTENT}" HEX_LEN)
math(EXPR BYTE_LEN "${HEX_LEN} / 2")

get_filename_component(INPUT_NAME "${INPUT}" NAME)

set(OUT "// Auto-generated from ${INPUT_NAME} - do not edit by hand.\n")
string(APPEND OUT "// 編集は ${INPUT_NAME} に対して行うこと。\n\n")
string(APPEND OUT "extern const unsigned char ${SYMBOL}[];\n")
string(APPEND OUT "extern const unsigned int  ${SYMBOL}_len;\n\n")
string(APPEND OUT "const unsigned char ${SYMBOL}[] = {\n    ")

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

string(APPEND OUT "0x00\n};\n")
string(APPEND OUT "const unsigned int ${SYMBOL}_len = ${BYTE_LEN};\n")

file(WRITE "${OUTPUT}" "${OUT}")
