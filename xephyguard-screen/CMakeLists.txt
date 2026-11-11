cmake_minimum_required(VERSION 3.20)
project(xephyguard_screen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XG_SANITIZE "Enable address and undefined sanitizers" OFF)
if(XG_SANITIZE)
  if(MSVC)
    add_compile_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  # Suppress third-party warnings in external headers
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ------------------------------------------------------------- dependencies
# xxHash
add_library(xxhash_lib STATIC third_party/xxhash.c)
target_include_directories(xxhash_lib PUBLIC third_party)

# libjpeg-turbo
find_package(TurboJPEG QUIET)
if(NOT TurboJPEG_FOUND)
  set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
  set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
  set(WITH_SIMD OFF CACHE BOOL "" FORCE)
  set(WITH_TURBOJPEG ON CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(libjpeg_turbo
    GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
    GIT_TAG 3.0.3
    GIT_SHALLOW TRUE)
  FetchContent_GetProperties(libjpeg_turbo)
  if(NOT libjpeg_turbo_POPULATED)
    FetchContent_Populate(libjpeg_turbo)
  endif()
  set(JPEGCMAKE "${libjpeg_turbo_SOURCE_DIR}/CMakeLists.txt")
  if(EXISTS "${JPEGCMAKE}")
    file(READ "${JPEGCMAKE}" JPEGCMAKE_CONTENT)
    if(JPEGCMAKE_CONTENT MATCHES "if\\(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR\\)")
      string(REPLACE "if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)" "if(FALSE)" JPEGCMAKE_CONTENT "${JPEGCMAKE_CONTENT}")
      string(REPLACE "enable_testing()" "if(FALSE)\nenable_testing()" JPEGCMAKE_CONTENT "${JPEGCMAKE_CONTENT}")
      string(REPLACE "# INSTALLATION" "endif()\n# INSTALLATION" JPEGCMAKE_CONTENT "${JPEGCMAKE_CONTENT}")
      file(WRITE "${JPEGCMAKE}" "${JPEGCMAKE_CONTENT}")
    endif()
  endif()
  if(NOT TARGET turbojpeg-static)
    add_subdirectory(${libjpeg_turbo_SOURCE_DIR} ${libjpeg_turbo_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()
endif()

# libwebp
find_package(WebP QUIET)
if(NOT WebP_FOUND)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG v1.4.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(libwebp)
endif()

# ------------------------------------------------------------------ xgcore
add_library(xgcore STATIC
  core/protocol.cpp
  core/tiles.cpp
  core/encoder.cpp
  core/flow.cpp
  core/downscale.cpp
  capture/fake_capture.cpp
  agent/agent_state.cpp
  agent/privacy.cpp
  agent/capabilities.cpp
  agent/indicator.cpp
)
target_include_directories(xgcore PUBLIC
  core
  capture
  agent
  third_party
  ${libjpeg_turbo_SOURCE_DIR}
  ${libjpeg_turbo_BINARY_DIR}
  ${libwebp_SOURCE_DIR}/src
)
target_link_libraries(xgcore PUBLIC xxhash_lib turbojpeg-static webp)

if(WIN32)
  target_sources(xgcore PRIVATE
    capture/win_gdi_capture.cpp
    capture/win_dxgi_capture.cpp
  )
  target_link_libraries(xgcore PUBLIC d3d11 dxgi gdi32)
else()
  target_sources(xgcore PRIVATE
    capture/x11_capture.cpp
  )
endif()

# ------------------------------------------------------------------ xgtests
enable_testing()

add_executable(xgtests
  tests/test_main.cpp
  tests/test_protocol.cpp
  tests/test_tiles.cpp
  tests/test_encoder.cpp
  tests/test_flow.cpp
  tests/test_pipeline.cpp
  tests/test_agent_state.cpp
  tests/test_downscale.cpp
  tests/test_capabilities.cpp
  tests/test_privacy.cpp
)
target_include_directories(xgtests PRIVATE core capture agent third_party)
target_link_libraries(xgtests PRIVATE xgcore)

add_test(NAME xgtests COMMAND xgtests)
