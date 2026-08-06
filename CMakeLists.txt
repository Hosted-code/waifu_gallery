cmake_minimum_required(VERSION 3.20)
project(waifu_gallery LANGUAGES CXX)

# setup configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${PROJECT_SOURCE_DIR}/src/ui)

# Qt6 moc fails to parse <filesystem> with GCC 11+ on Linux.
# Workaround: tell moc to skip system include paths.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "11")
    set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
endif()

set(VCPKG_APPLOCAL_DEPS OFF)

# libs
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# Try vcpkg first, fall back to system packages
find_package(unofficial-sqlite3 CONFIG QUIET)
if(NOT unofficial-sqlite3_FOUND)
    find_package(SQLite3 REQUIRED)
    add_library(unofficial::sqlite3::sqlite3 ALIAS SQLite::SQLite3)
endif()

find_package(xxHash CONFIG QUIET)
if(NOT xxHash_FOUND)
    find_library(XXHASH_LIB NAMES xxhash REQUIRED)
    add_library(xxhash_alias STATIC IMPORTED)
    set_target_properties(xxhash_alias PROPERTIES IMPORTED_LOCATION ${XXHASH_LIB})
    add_library(xxHash::xxhash ALIAS xxhash_alias)
endif()

find_package(WebP CONFIG QUIET)
if(NOT WebP_FOUND)
    find_library(WEBP_LIB NAMES webp REQUIRED)
    find_library(WEBPDEMUX_LIB NAMES webpdemux REQUIRED)
    # webpdecoder is included in libwebp on some systems
    find_library(WEBPDECODER_LIB NAMES webpdecoder QUIET)
    add_library(WebP::webp STATIC IMPORTED)
    set_target_properties(WebP::webp PROPERTIES IMPORTED_LOCATION ${WEBP_LIB})
    add_library(WebP::webpdemux STATIC IMPORTED)
    set_target_properties(WebP::webpdemux PROPERTIES IMPORTED_LOCATION ${WEBPDEMUX_LIB})
    if(WEBPDECODER_LIB)
        add_library(WebP::webpdecoder STATIC IMPORTED)
        set_target_properties(WebP::webpdecoder PROPERTIES IMPORTED_LOCATION ${WEBPDECODER_LIB})
    else()
        add_library(WebP::webpdecoder ALIAS WebP::webp)
    endif()
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
endif()

find_package(Stb QUIET)
if(NOT Stb_FOUND)
    find_path(STB_INCLUDE_DIR stb_image.h PATHS /usr/include/stb /usr/include REQUIRED)
endif()

# RapidCSV header-only
find_path(RAPIDCSV_INCLUDE_DIR rapidcsv.h PATHS ${PROJECT_SOURCE_DIR}/external/rapidcsv /usr/include /usr/local/include QUIET)

set(PROJECT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)
if(TARGET xxhash::xxhash)
    list(APPEND PROJECT_LIBS xxhash::xxhash)
elseif(TARGET xxHash::xxhash)
    list(APPEND PROJECT_LIBS xxHash::xxhash)
endif()
if(TARGET WebP::webp)
    list(APPEND PROJECT_LIBS WebP::webp WebP::webpdecoder WebP::webpdemux)
endif()
if(TARGET unofficial::sqlite3::sqlite3)
    list(APPEND PROJECT_LIBS unofficial::sqlite3::sqlite3)
endif()

# src files
set(PROJECT_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/external ${PROJECT_SOURCE_DIR}/src/gui/widgets)
file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp ${PROJECT_SOURCE_DIR}/src/*.h) # remember configure CMake before build
file(GLOB UI_FILES ${PROJECT_SOURCE_DIR}/src/ui/*.ui)
file(GLOB RES_FILES ${PROJECT_SOURCE_DIR}/res/*.qrc)
file(GLOB RC_FILES ${PROJECT_SOURCE_DIR}/res/*.rc)

# target
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(waifu_gallery ${SRC_FILES} ${UI_FILES} ${RES_FILES} ${RC_FILES})
else()
    if(WIN32)
        add_executable(waifu_gallery WIN32 ${SRC_FILES} ${UI_FILES} ${RES_FILES} ${RC_FILES})
    else()
        add_executable(waifu_gallery ${SRC_FILES} ${UI_FILES} ${RES_FILES} ${RC_FILES})
    endif()
endif()
target_include_directories(waifu_gallery PRIVATE ${PROJECT_INCLUDE_DIRS})
if(NLOHMANN_JSON_INCLUDE_DIR)
    target_include_directories(waifu_gallery PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
endif()
if(STB_INCLUDE_DIR)
    target_include_directories(waifu_gallery PRIVATE ${STB_INCLUDE_DIR})
endif()
if(RAPIDCSV_INCLUDE_DIR)
    target_include_directories(waifu_gallery PRIVATE ${RAPIDCSV_INCLUDE_DIR})
endif()
if(COMMAND qt_import_plugins)
qt_import_plugins(waifu_gallery EXCLUDE Qt::QPdfPlugin)
endif()
target_link_libraries(waifu_gallery PRIVATE ${PROJECT_LIBS})

# test
if(BUILD_TEST_TARGET STREQUAL "ON")
    file(GLOB_RECURSE TEST_SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp ${PROJECT_SOURCE_DIR}/src/*.h)
    list(REMOVE_ITEM TEST_SRC_FILES ${PROJECT_SOURCE_DIR}/src/main.cpp) # exclude main.cpp
    file(GLOB TEST_FILES ${PROJECT_SOURCE_DIR}/tests/*.cpp) # include test files
    add_executable(TEST_waifu_gallery
        ${TEST_SRC_FILES}
        ${TEST_FILES}
    )
    target_include_directories(TEST_waifu_gallery PRIVATE ${PROJECT_INCLUDE_DIRS})
    if(COMMAND qt_import_plugins)
    qt_import_plugins(TEST_waifu_gallery EXCLUDE Qt::QPdfPlugin)
    endif()
    target_link_libraries(TEST_waifu_gallery PRIVATE ${PROJECT_LIBS})
endif()