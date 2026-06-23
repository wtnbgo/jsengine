cmake_minimum_required(VERSION 3.22)

# UTF-8 source
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

set(PROJECT_NAME jsengine)
set(PROJECT_VERSION 1.00)

option(SDL3_DYNAMIC "use dynamic SDL3" OFF)

# REPL (Read-Eval-Print Loop): stdin から JS を評価したり、ファイルチャネル経由で
# 外部エージェント (AI 等) から JS を駆動するための機能。 組込みターゲット等で
# stdin を持たない / バイナリを小さくしたい場合は OFF にする。
option(JSENGINE_USE_REPL "Enable interactive console REPL and file-channel REPL" ON)

# WebM 動画再生 (wamsoft/movie-player + libvpx/libvorbis/libopus)。 OFF にすると
# 関連 JS API (MoviePlayer / HTMLVideoElement の native パス) が無効化される
# (HTMLVideoElement は sysinit の no-op シムにフォールバック)。 libvpx を含む
# vcpkg 依存が増えるので、 不要なターゲットは OFF を選ぶ。
option(JSENGINE_USE_MOVIE_PLAYER "Enable WebM video playback via movie-player module" ON)

# ----------------------------------------------------
# プラットフォーム差し替えフック
#
# 親プロジェクト (jsengine_nx 等) が SDL や main を自前で用意するための
# キャッシュ変数群。スタンドアロンビルドでは全部 OFF / 空でよい。
# ----------------------------------------------------
option(JSENGINE_PLATFORM_OVERRIDE
    "Skip default platform setup (FetchContent SDL, DLL copy, etc.) and let parent project drive it" OFF)
option(JSENGINE_SDL_PROVIDED
    "SDL3 / SDL3_image targets are provided by parent project (skip FetchContent)"
    ${JSENGINE_PLATFORM_OVERRIDE})
option(JSENGINE_SKIP_DEFAULT_MAIN
    "Skip default src/main.cpp (replaced via JSENGINE_SRC_PLATFORM)"
    ${JSENGINE_PLATFORM_OVERRIDE})
set(JSENGINE_SRC_PLATFORM "" CACHE STRING
    "Additional platform-specific sources injected by parent project")
set(JSENGINE_DEFINES_PLATFORM "" CACHE STRING
    "Additional platform-specific compile definitions")

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

include(FetchContent)

if(NOT JSENGINE_SDL_PROVIDED)

# ----------------------------------------------------
# SDL (shared)
# ----------------------------------------------------

set(SDL_SHARED ON CACHE BOOL "Build SDL shared library")
set(SDL_STATIC OFF CACHE BOOL "Build SDL static library")

set(SDLIMAGE_BMP ON)
set(SDLIMAGE_JPG ON)
set(SDLIMAGE_PNG ON)
set(SDLIMAGE_ANI OFF)
set(SDLIMAGE_AVIF OFF)
set(SDLIMAGE_GIF OFF)
set(SDLIMAGE_JXL OFF)
set(SDLIMAGE_LBM OFF)
set(SDLIMAGE_PCX OFF)
set(SDLIMAGE_PNM OFF)
set(SDLIMAGE_QOI OFF)
set(SDLIMAGE_SVG OFF)
set(SDLIMAGE_TGA OFF)
set(SDLIMAGE_TIF OFF)
set(SDLIMAGE_WEBP OFF)
set(SDLIMAGE_XCF OFF)
set(SDLIMAGE_XPM OFF)
set(SDLIMAGE_XV OFF)

FetchContent_Declare(
	SDL3
	GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
	GIT_TAG release-3.4.0
)

FetchContent_Declare(
	SDL3_image
	GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
	GIT_TAG release-3.4.0
)

# SDL3_image が vendored zlib/libpng を使わないように、システム（vcpkg）を先に検出
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
set(SDLIMAGE_VENDORED OFF CACHE BOOL "Use vendored third-party libraries" FORCE)

FetchContent_MakeAvailable(SDL3)
FetchContent_MakeAvailable(SDL3_image)

endif() # NOT JSENGINE_SDL_PROVIDED

# ----------------------------------------------------
# glad
# ----------------------------------------------------

add_subdirectory(glad ${CMAKE_CURRENT_BINARY_DIR}/glad)

# ----------------------------------------------------
# QuickJS-ng (vcpkg, または親プロジェクトの FetchContent)
# ----------------------------------------------------

# 親プロジェクトが quickjs-ng を構築済みの場合 (例: jsengine_nx の ext/quickjs-ng)
# は find_package をスキップ。
if(NOT TARGET qjs AND NOT TARGET qjs::qjs)
	find_package(qjs CONFIG REQUIRED)
endif()

# ----------------------------------------------------
# thorvg + (freetype vcpkg) + harfbuzz 
# ----------------------------------------------------

# ThorVG loader options (FORCE で既存キャッシュを上書き)
set(TVG_BUILD_SHARED OFF CACHE BOOL "Build shared library" FORCE)
set(TVG_LOADER_SVG OFF CACHE BOOL "Enable SVG Loader" FORCE)
set(TVG_LOADER_PNG OFF CACHE BOOL "Enable PNG Loader" FORCE)
set(TVG_LOADER_JPG OFF CACHE BOOL "Enable JPG Loader" FORCE)
set(TVG_LOADER_LOTTIE OFF CACHE BOOL "Enable Lottie Loader" FORCE)
# TTF と FT は排他的: FT (FreeType + HarfBuzz) を有効にして多言語テキストレンダリングを使用
set(TVG_LOADER_TTF OFF CACHE BOOL "Enable TTF Loader" FORCE)
set(TVG_LOADER_FT ON CACHE BOOL "Enable FreeType+HarfBuzz multilingual text loader" FORCE)
set(TVG_LOADER_WEBP OFF CACHE BOOL "Enable WebP Loader" FORCE)
set(TVG_LOG OFF CACHE BOOL "Enable log message" FORCE)
set(TVG_FILE_IO ON CACHE BOOL "Enable File IO calls" FORCE)

set(TVG_ENGINE_SW ON CACHE BOOL "Enable Software Rasterizer Engine" FORCE)
set(TVG_ENGINE_GL ON CACHE BOOL "Enable OpenGL Rasterizer Engine" FORCE)

# --- FreeType (vcpkg) ---
# 親プロジェクトで既に find_package 済み / add_subdirectory 済みなら再実行しない
if(NOT TARGET freetype AND NOT TARGET Freetype::Freetype)
	find_package(Freetype CONFIG REQUIRED)
endif()

# --- HarfBuzz (vcpkg, または親プロジェクトの FetchContent) ---
# 親プロジェクトが harfbuzz を構築済みの場合 (例: jsengine_nx の ext/harfbuzz)
# は find_package をスキップする。
if(NOT TARGET harfbuzz AND NOT TARGET harfbuzz::harfbuzz)
	find_package(harfbuzz CONFIG REQUIRED)
endif()

# --- ThorVG ---
# 開発用: USE_LOCAL_THORVG=ON で ../thorvg のローカルチェックアウトを直接使う。
# 既存ユーザー / CI 向けには従来通り wtnbgo/thorvg (branch: cmake) を FetchContent。
option(USE_LOCAL_THORVG "Use ../thorvg local checkout instead of FetchContent" OFF)
if(USE_LOCAL_THORVG)
	set(_THORVG_LOCAL_DIR "${CMAKE_SOURCE_DIR}/../thorvg")
	if(NOT EXISTS "${_THORVG_LOCAL_DIR}/CMakeLists.txt")
		message(FATAL_ERROR
			"USE_LOCAL_THORVG=ON but ${_THORVG_LOCAL_DIR}/CMakeLists.txt not found")
	endif()
	message(STATUS "Using local ThorVG at ${_THORVG_LOCAL_DIR}")
	add_subdirectory("${_THORVG_LOCAL_DIR}" thorvg_build EXCLUDE_FROM_ALL)
	# 下流の target_include_directories で参照する変数を FetchContent と合わせる
	set(thorvg_SOURCE_DIR "${_THORVG_LOCAL_DIR}")
else()
	FetchContent_Declare(
		thorvg
		EXCLUDE_FROM_ALL
		GIT_REPOSITORY https://github.com/wtnbgo/thorvg.git
		GIT_TAG cmake
	)
	FetchContent_MakeAvailable(thorvg)
endif()


# ----------------------------------------------------
# movie-player (wamsoft/movie-player) — WebM 動画デコード
#
# 非 Windows / 非 Android では nestegg + libvpx + libvorbis + libopus で動く。
# Android 経路は AMediaCodec ベース (今は jsengine では非対応)。
# 静的ライブラリとしてリンクし、 IMoviePlayer / IAudioSink の 2 ヘッダだけ使う。
# IAudioSink 実装は src/video.cpp 側で SDL_AudioStream ベースで自前実装。
# ----------------------------------------------------
if(JSENGINE_USE_MOVIE_PLAYER)
	set(MOVIE_PLAYER_BUILD_TEST OFF CACHE BOOL "movie-player test (no, we drive it ourselves)" FORCE)
	# movie-player CMake は BUILD_TEST という変数名なので念のため両方押さえる
	set(BUILD_TEST OFF CACHE BOOL "movie-player test off" FORCE)

	# USE_LOCAL_THORVG と同じ流儀で、 開発時は ../krkrz/external/movie-player の
	# ローカルチェックアウトを add_subdirectory する。 デフォルト OFF。
	option(USE_LOCAL_MOVIE_PLAYER "Use local movie-player checkout instead of FetchContent" OFF)
	set(JSENGINE_LOCAL_MOVIE_PLAYER_PATH "${CMAKE_SOURCE_DIR}/../../work/kirikiri/krkrz/external/movie-player"
		CACHE STRING "Path to local movie-player checkout (only used if USE_LOCAL_MOVIE_PLAYER=ON)")

	if(USE_LOCAL_MOVIE_PLAYER)
		if(NOT EXISTS "${JSENGINE_LOCAL_MOVIE_PLAYER_PATH}/CMakeLists.txt")
			message(FATAL_ERROR "USE_LOCAL_MOVIE_PLAYER=ON but ${JSENGINE_LOCAL_MOVIE_PLAYER_PATH}/CMakeLists.txt not found")
		endif()
		message(STATUS "Using local movie-player at ${JSENGINE_LOCAL_MOVIE_PLAYER_PATH}")
		add_subdirectory("${JSENGINE_LOCAL_MOVIE_PLAYER_PATH}" movie_player_build EXCLUDE_FROM_ALL)
		set(movie_player_SOURCE_DIR "${JSENGINE_LOCAL_MOVIE_PLAYER_PATH}")
	else()
		FetchContent_Declare(
			movie_player
			EXCLUDE_FROM_ALL
			GIT_REPOSITORY https://github.com/wamsoft/movie-player.git
			GIT_TAG main
		)
		FetchContent_MakeAvailable(movie_player)
	endif()
endif()

# ----------------------------------------------------
# other libraries
# ----------------------------------------------------

# vcpkg でインストールされた glm — 現在未使用
# find_package(glm CONFIG REQUIRED)

# vorbis / opus (オプション)
find_package(Vorbis CONFIG QUIET)
find_package(OpusFile CONFIG QUIET)

# ----------------------------------------------------
# executable source
# ----------------------------------------------------

set(SOURCES
	src/app.cpp
	src/jsengine.cpp
	src/webgl.cpp
	src/webaudio.cpp
	src/webgamepad.cpp
	src/canvas2d.cpp
	src/audio/AudioEngine.cpp
	src/audio/AudioStream.cpp
	src/image_decode_pool.cpp
	src/main.cpp
)

if(JSENGINE_USE_REPL)
	list(APPEND SOURCES src/repl.cpp)
endif()

if(JSENGINE_USE_MOVIE_PLAYER)
	list(APPEND SOURCES src/video.cpp)
endif()

# ----------------------------------------------------
# 内蔵 JS スクリプトの埋め込み (cmake/embed_js.cmake 経由)
#  - sysinit.js: ブラウザシム。 main.js のロード前に自動評価される。
#                開発時は -sysinit <path> で外部ファイルに差し替え可能。
#  - rpgmv_main.js: RPG Maker MV プロジェクト起動用ブートストラップ。
#                   -rpgmv <project-path> 指定時に通常の main.js の代わりに走る。
# ----------------------------------------------------
function(jsengine_embed_js JS_SRC CPP_OUT SYMBOL)
	add_custom_command(
		OUTPUT ${CPP_OUT}
		COMMAND ${CMAKE_COMMAND}
			-DINPUT=${JS_SRC}
			-DOUTPUT=${CPP_OUT}
			-DSYMBOL=${SYMBOL}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_js.cmake
		DEPENDS ${JS_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_js.cmake
		COMMENT "Embedding ${JS_SRC}"
	)
endfunction()

set(SYSINIT_CPP_OUT ${CMAKE_CURRENT_BINARY_DIR}/builtin_sysinit.cpp)
jsengine_embed_js(${CMAKE_CURRENT_SOURCE_DIR}/src/sysinit.js ${SYSINIT_CPP_OUT} g_sysinit_js)
list(APPEND SOURCES ${SYSINIT_CPP_OUT})

set(RPGMV_MAIN_CPP_OUT ${CMAKE_CURRENT_BINARY_DIR}/builtin_rpgmv_main.cpp)
jsengine_embed_js(${CMAKE_CURRENT_SOURCE_DIR}/src/rpgmv_main.js ${RPGMV_MAIN_CPP_OUT} g_rpgmv_main_js)
list(APPEND SOURCES ${RPGMV_MAIN_CPP_OUT})

if(Vorbis_FOUND)
	list(APPEND SOURCES src/audio/ma_libvorbis.c)
endif()
if(OpusFile_FOUND)
	list(APPEND SOURCES src/audio/ma_libopus.c)
endif()

# 親プロジェクトが main を差し替える場合は src/main.cpp を除外
if(JSENGINE_SKIP_DEFAULT_MAIN)
	list(REMOVE_ITEM SOURCES src/main.cpp)
endif()
# 親プロジェクト経由で追加された機種固有ソース
if(JSENGINE_SRC_PLATFORM)
	list(APPEND SOURCES ${JSENGINE_SRC_PLATFORM})
endif()

# ----------------------------------------------------
# executable
# ----------------------------------------------------

# その他のプラットフォームは実行ファイルとして作成
add_executable(${PROJECT_NAME}
	${SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${thorvg_SOURCE_DIR}/inc"
)

# 親プロジェクトから注入された機種固有 define
if(JSENGINE_DEFINES_PLATFORM)
	target_compile_definitions(${PROJECT_NAME} PRIVATE ${JSENGINE_DEFINES_PLATFORM})
endif()

if(JSENGINE_USE_REPL)
	target_compile_definitions(${PROJECT_NAME} PRIVATE JSENGINE_USE_REPL)
endif()

if(JSENGINE_USE_MOVIE_PLAYER)
	target_compile_definitions(${PROJECT_NAME} PRIVATE JSENGINE_USE_MOVIE_PLAYER)
	target_link_libraries(${PROJECT_NAME} PRIVATE movieplayer)
endif()

# OS スレッドスタックサイズを増やす（GLTFLoader の再帰的ノード読み込みに必要）
if(MSVC)
	target_link_options(${PROJECT_NAME} PRIVATE /STACK:16777216) # 16MB
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
	SDL3::SDL3
	SDL3_image::SDL3_image
	# glm::glm   # 現在未使用
	glad
	thorvg
	freetype
	harfbuzz::harfbuzz
	qjs
)


if(Vorbis_FOUND)
	target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_VORBIS)
	target_link_libraries(${PROJECT_NAME} PRIVATE Vorbis::vorbisfile Vorbis::vorbis)
endif()
if(OpusFile_FOUND)
	target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_OPUS)
	target_link_libraries(${PROJECT_NAME} PRIVATE OpusFile::opusfile)
endif()


# SDL3 / SDL3_image を dynamic でビルドしたときに実行ファイル横にコピー。
# 親プロジェクトが SDL を提供する場合 (静的 / プラットフォーム固有配置) は不要。
if(NOT JSENGINE_PLATFORM_OVERRIDE)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		$<TARGET_FILE:SDL3::SDL3>
		$<TARGET_FILE_DIR:${PROJECT_NAME}>

	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		$<TARGET_FILE:SDL3_image::SDL3_image>
		$<TARGET_FILE_DIR:${PROJECT_NAME}>
)
endif()


