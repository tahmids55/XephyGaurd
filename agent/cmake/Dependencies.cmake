# Third-party dependencies.
# OpenSSL, SQLite3 and Threads come from the system (or vcpkg on Windows).
# Header-only / small libraries are fetched at configure time and pinned to a tag.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

find_package(Threads REQUIRED)
find_package(OpenSSL 3.0 REQUIRED)
find_package(SQLite3 REQUIRED)

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(tomlplusplus 3.4 QUIET)
if(NOT tomlplusplus_FOUND)
  FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(tomlplusplus)
endif()

if(XG_BUILD_TESTS)
  find_package(Catch2 3 QUIET)
  if(NOT Catch2_FOUND)
    FetchContent_Declare(Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.7.1
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
  endif()
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
