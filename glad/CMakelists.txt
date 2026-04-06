set(LIB_NAME glad)

# Force static library.
add_library(${LIB_NAME} STATIC
  src/egl.c
  src/gles2.c
)

target_include_directories(${LIB_NAME}
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
