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
  # libjpeg-turbo's GNUInstallDirs needs a cache entry (standalone build has none); the normal
  # variable must stay set too, because a parent project may already hold an empty cache entry.
  if(NOT DEFINED CACHE{CMAKE_INSTALL_DOCDIR})
    set(CMAKE_INSTALL_DOCDIR "doc" CACHE PATH "Documentation directory")
  endif()
  set(CMAKE_INSTALL_DOCDIR "doc")
  set(CMAKE_SKIP_INSTALL_RULES TRUE)
  set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
  set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
  set(WITH_SIMD OFF CACHE BOOL "" FORCE)
  set(WITH_TURBOJPEG ON CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(libjpeg_turbo
    GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
    # Pinned by commit (tag 3.0.3, checked 2026-11-18): a moved tag cannot change what we build. Update with approval only.
    GIT_TAG 7fa4b5b762c9a99b46b0b7838f5fd55071b92ea5)
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
    # Pinned by commit (tag v1.4.0, checked 2026-11-18). Update with approval only.
    GIT_TAG 845d5476a866141ba35ac133f856fa62f0b7445f)
  FetchContent_MakeAvailable(libwebp)
endif()

# ------------------------------------------------------------------ xgcore
add_library(xgcore STATIC
  core/protocol.cpp
  core/tiles.cpp
  capture/monitors.cpp
  core/encoder.cpp
  core/flow.cpp
  core/downscale.cpp
  core/thread_priority.cpp
  core/governor.cpp
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
    capture/win_multi_capture.cpp
  )
  target_link_libraries(xgcore PUBLIC d3d11 dxgi gdi32)
else()
  target_sources(xgcore PRIVATE
    capture/x11_capture.cpp
  )
  find_package(X11 COMPONENTS Xext Xdamage Xrandr QUIET)
  if(TARGET X11::X11)
    target_link_libraries(xgcore PUBLIC X11::X11)
    foreach(_xlib X11::Xext X11::Xdamage X11::Xrandr)
      if(TARGET ${_xlib})
        target_link_libraries(xgcore PUBLIC ${_xlib})
      endif()
    endforeach()
  else()
    target_link_libraries(xgcore PUBLIC X11 Xext Xdamage Xrandr)
  endif()
endif()

# ------------------------------------------------------------------ standalone targets
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
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
    tests/test_thread_priority.cpp
    tests/test_governor.cpp
    tests/test_capabilities.cpp
    tests/test_privacy.cpp
    tests/test_x11_shm.cpp
    tests/test_monitors.cpp
    tests/test_x11_monitors.cpp
  )
  target_include_directories(xgtests PRIVATE core capture agent third_party)
  target_link_libraries(xgtests PRIVATE xgcore)

  add_test(NAME xgtests COMMAND xgtests)

  # The X11 capture tests draw on the root window: run them on a private Xvfb, only when xvfb-run exists.
  find_program(XG_XVFB_RUN xvfb-run)
  if(XG_XVFB_RUN)
    add_test(NAME xgtests_x11 COMMAND ${XG_XVFB_RUN} -a -s "-screen 0 1920x1080x24" $<TARGET_FILE:xgtests> "-tc=X11 capture*")
    set_tests_properties(xgtests_x11 PROPERTIES ENVIRONMENT "XG_TEST_DISPOSABLE_DISPLAY=1")
  endif()

  add_executable(xgbench bench/load_agents.cpp)
  target_include_directories(xgbench PRIVATE core capture agent third_party)
  target_link_libraries(xgbench PRIVATE xgcore)

  add_executable(xgstreambench bench/stream_bench.cpp)
  target_include_directories(xgstreambench PRIVATE core capture agent third_party)
  target_link_libraries(xgstreambench PRIVATE xgcore)
  # Smoke test only: one second per phase, checks that the bench runs and prints its rows.
  add_test(NAME xgstreambench_smoke COMMAND xgstreambench --seconds 1 --workload idle --workload typing)
  set_tests_properties(xgstreambench_smoke PROPERTIES PASS_REGULAR_EXPRESSION "idle .*typing .*fps")
endif()
