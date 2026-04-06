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
# duktape
# ----------------------------------------------------

find_package(unofficial-duktape CONFIG REQUIRED)

# ----------------------------------------------------
# other libraries
# ----------------------------------------------------

# vcpkg でインストールされた glm を見つける
find_package(glm CONFIG REQUIRED)

# ----------------------------------------------------
# executable source
# ----------------------------------------------------

set(SOURCES
	src/app.cpp
	src/jsengine.cpp
	src/dukwebgl.cpp
	src/main.cpp
)

# ----------------------------------------------------
# executable
# ----------------------------------------------------

# その他のプラットフォームは実行ファイルとして作成
add_executable(${PROJECT_NAME}
	${SOURCES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	SDL3::SDL3
	SDL3_image::SDL3_image
	glm::glm
	glad
	unofficial::duktape::duktape
)


add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		$<TARGET_FILE:SDL3::SDL3>
		$<TARGET_FILE_DIR:${PROJECT_NAME}>

	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		$<TARGET_FILE:SDL3_image::SDL3_image>
		$<TARGET_FILE_DIR:${PROJECT_NAME}>
)


