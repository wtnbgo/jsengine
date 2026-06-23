cmake_minimum_required(VERSION 3.22)

# UTF-8 source
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

set(PROJECT_NAME jsengine)
set(PROJECT_VERSION 1.00)

option(SDL3_DYNAMIC "use dynamic SDL3" OFF)

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
	src/main.cpp
)

# ----------------------------------------------------
# 内蔵初期化スクリプト (sysinit.js → builtin_sysinit.cpp)
# main.js のロード前に自動評価される。 開発時は -sysinit <path> で
# 外部ファイルに差し替え可能 (ビルドし直しを避けるため)。
# ----------------------------------------------------
set(SYSINIT_JS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/sysinit.js)
set(SYSINIT_CPP_OUT ${CMAKE_CURRENT_BINARY_DIR}/builtin_sysinit.cpp)
add_custom_command(
	OUTPUT ${SYSINIT_CPP_OUT}
	COMMAND ${CMAKE_COMMAND}
		-DINPUT=${SYSINIT_JS_SRC}
		-DOUTPUT=${SYSINIT_CPP_OUT}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_sysinit.cmake
	DEPENDS ${SYSINIT_JS_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_sysinit.cmake
	COMMENT "Embedding sysinit.js"
)
list(APPEND SOURCES ${SYSINIT_CPP_OUT})

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


