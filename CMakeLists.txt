cmake_minimum_required(VERSION 3.22)

# UTF-8 source
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

set(PROJECT_NAME jsengine)
set(PROJECT_VERSION 1.00)

option(SDL3_DYNAMIC "use dynamic SDL3" OFF)

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

include(FetchContent)

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

FetchContent_MakeAvailable(SDL3)
FetchContent_MakeAvailable(SDL3_image)

# ----------------------------------------------------
# glad
# ----------------------------------------------------

add_subdirectory(glad ${CMAKE_CURRENT_BINARY_DIR}/glad)

# ----------------------------------------------------
# duktape（自前ビルド）
# ----------------------------------------------------


# ----------------------------------------------------
# thorvg + (freetype vcpkg) + harfbuzz 
# ----------------------------------------------------

# ThorVG loader options (shared by both paths)
option(TVG_BUILD_SHARED "Build shared library" OFF)
option(TVG_LOADER_SVG "Enable SVG Loader" OFF)
option(TVG_LOADER_PNG "Enable PNG Loader" OFF)
option(TVG_LOADER_JPG "Enable JPG Loader" OFF)
option(TVG_LOADER_LOTTIE "Enable Lottie Loader" OFF)
option(TVG_LOADER_TTF "Enable TTF Loader" ON)
option(TVG_LOADER_WEBP "Enable WebP Loader" OFF)
option(TVG_LOG "Enable log message" OFF)
option(TVG_FILE_IO "Enable File IO calls" ON)

option(TVG_ENGINE_SW "Enable Software Rasterizer Engine" ON)
option(TVG_ENGINE_GL "Enable OpenGL Rasterizer Engine" ON)

# --- ThorVG ---
FetchContent_Declare(
	thorvg
	EXCLUDE_FROM_ALL
	GIT_REPOSITORY https://github.com/wtnbgo/thorvg.git
	GIT_TAG cmake
)
FetchContent_MakeAvailable(thorvg)

# --- FreeType (vcpkg) ---
find_package(freetype CONFIG REQUIRED)

# --- HarfBuzz 13.1.1 (FetchContent, ICU なし) ---
set(HB_HAVE_FREETYPE ON CACHE BOOL "Enable freetype interop helpers" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "Disable ICU" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
	harfbuzz_source
	GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
	GIT_TAG 13.1.1
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(harfbuzz_source)


# ----------------------------------------------------
# other libraries
# ----------------------------------------------------

# vcpkg でインストールされた glm を見つける
find_package(glm CONFIG REQUIRED)

# vorbis / opus (オプション)
find_package(Vorbis CONFIG QUIET)
find_package(OpusFile CONFIG QUIET)

# ----------------------------------------------------
# executable source
# ----------------------------------------------------

set(SOURCES
	src/app.cpp
	src/jsengine.cpp
	src/dukwebgl.cpp
	src/webaudio.cpp
	src/canvas2d.cpp
	src/duktape/duktape.c
	src/audio/AudioEngine.cpp
	src/audio/AudioStream.cpp
	src/main.cpp
)

if(Vorbis_FOUND)
	list(APPEND SOURCES src/audio/ma_libvorbis.c)
endif()
if(OpusFile_FOUND)
	list(APPEND SOURCES src/audio/ma_libopus.c)
endif()

# ----------------------------------------------------
# executable
# ----------------------------------------------------

# その他のプラットフォームは実行ファイルとして作成
add_executable(${PROJECT_NAME}
	${SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
	"${thorvg_SOURCE_DIR}/inc"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/duktape"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	SDL3::SDL3
	SDL3_image::SDL3_image
	glm::glm
	glad
	thorvg
	freetype
	harfbuzz
)


if(Vorbis_FOUND)
	target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_VORBIS)
	target_link_libraries(${PROJECT_NAME} PRIVATE Vorbis::vorbisfile Vorbis::vorbis)
endif()
if(OpusFile_FOUND)
	target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_OPUS)
	target_link_libraries(${PROJECT_NAME} PRIVATE OpusFile::opusfile)
endif()


add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		$<TARGET_FILE:SDL3::SDL3>
		$<TARGET_FILE_DIR:${PROJECT_NAME}>

	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		$<TARGET_FILE:SDL3_image::SDL3_image>
		$<TARGET_FILE_DIR:${PROJECT_NAME}>
)


