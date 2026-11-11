cmake_minimum_required(VERSION 3.20)
project(xephyguard_agent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(XG_BUILD_TESTS "Build unit tests" ON)
option(XG_WITH_X11 "Build X11 window/clipboard/indicator support into the Linux helper" ON)

include(cmake/Dependencies.cmake)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# ---------------------------------------------------------------- core library
file(GLOB XG_CORE_SOURCES CONFIGURE_DEPENDS core/src/*.cpp)
add_library(xg_core STATIC ${XG_CORE_SOURCES})
target_include_directories(xg_core PUBLIC core/include)
target_link_libraries(xg_core PUBLIC nlohmann_json::nlohmann_json tomlplusplus::tomlplusplus
                                     OpenSSL::SSL OpenSSL::Crypto SQLite::SQLite3 Threads::Threads)
target_compile_definitions(xg_core PUBLIC XG_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_link_libraries(xg_core PUBLIC ws2_32 crypt32)
  target_compile_definitions(xg_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endif()

# ------------------------------------------------------------ platform adapters
if(WIN32)
  file(GLOB XG_PLATFORM_SOURCES CONFIGURE_DEPENDS platform/windows/*.cpp)
  add_library(xg_platform STATIC ${XG_PLATFORM_SOURCES})
  target_link_libraries(xg_platform PUBLIC xg_core iphlpapi wtsapi32 userenv advapi32 shell32 user32 ole32 setupapi psapi dnsapi)
  target_include_directories(xg_platform PUBLIC platform/windows)
else()
  file(GLOB XG_PLATFORM_SOURCES CONFIGURE_DEPENDS platform/linux/*.cpp)
  add_library(xg_platform STATIC ${XG_PLATFORM_SOURCES})
  target_link_libraries(xg_platform PUBLIC xg_core)
  target_include_directories(xg_platform PUBLIC platform/linux)
  if(XG_WITH_X11)
    find_package(X11 QUIET)
    if(X11_FOUND AND X11_Xfixes_FOUND)
      target_link_libraries(xg_platform PUBLIC X11::X11 X11::Xfixes)
      target_compile_definitions(xg_platform PUBLIC XG_HAVE_X11=1)
    endif()
  endif()
endif()

# ----------------------------------------------------------------- screen streaming
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../xephyguard-screen ${CMAKE_CURRENT_BINARY_DIR}/xephyguard-screen EXCLUDE_FROM_ALL)

# ----------------------------------------------------------------- executables
add_executable(xephyguard-agent apps/agent_main.cpp)
target_link_libraries(xephyguard-agent PRIVATE xg_platform)
add_executable(xephyguard-helper apps/helper_main.cpp apps/screen_streamer.cpp)
target_link_libraries(xephyguard-helper PRIVATE xg_platform xgcore)
if(WIN32)
  target_link_libraries(xephyguard-helper PRIVATE winhttp d3d11 dxgi gdi32)
  set_target_properties(xephyguard-helper PROPERTIES WIN32_EXECUTABLE ON)
endif()

# ----------------------------------------------------------------------- tests
if(XG_BUILD_TESTS)
  enable_testing()
  file(GLOB XG_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(xg_tests ${XG_TEST_SOURCES})
  target_link_libraries(xg_tests PRIVATE xg_core Catch2::Catch2WithMain)
  target_compile_definitions(xg_tests PRIVATE XG_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
  include(Catch)
  catch_discover_tests(xg_tests)
endif()

include(packaging/linux/CPackConfig.cmake OPTIONAL)
