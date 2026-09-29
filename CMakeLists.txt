# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 freedy79

cmake_minimum_required(VERSION 3.20)
project(stomp_client VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Library definition
add_library(stomp_client STATIC
    src/StompFrame.cpp
    src/StompParser.cpp
    src/StompClient.cpp
    src/CurlTransport.cpp
)

target_include_directories(stomp_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Dependencies (libcurl). Embedded toolchains often ship libcurl without a CMake
# config package, so fall back to the plain library name.
find_package(CURL QUIET)
if(TARGET CURL::libcurl)
    target_link_libraries(stomp_client PUBLIC CURL::libcurl)
else()
    target_link_libraries(stomp_client PUBLIC curl)
endif()

# Optional JSON support for the application layer. The STOMP core itself stays
# payload agnostic; only helpers and examples use nlohmann/json.
option(STOMP_WITH_JSON "Enable nlohmann/json based message helpers" ON)
set(STOMP_JSON_INCLUDE_DIR "" CACHE PATH "Directory containing nlohmann/json.hpp when no package is installed")

if(STOMP_WITH_JSON AND NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.2.0 QUIET)

    if(NOT nlohmann_json_FOUND)
        if(STOMP_JSON_INCLUDE_DIR AND EXISTS "${STOMP_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
            add_library(nlohmann_json INTERFACE)
            add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
            target_include_directories(nlohmann_json SYSTEM INTERFACE "${STOMP_JSON_INCLUDE_DIR}")
        else()
            message(WARNING "nlohmann/json not found - building without JSON helpers. "
                            "Install the package or set STOMP_JSON_INCLUDE_DIR.")
            set(STOMP_WITH_JSON OFF)
        endif()
    endif()
endif()

if(STOMP_WITH_JSON)
    target_link_libraries(stomp_client PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(stomp_client PUBLIC STOMP_WITH_JSON)
endif()

# Tests and examples are only of interest for the standalone build; when embedded
# into a larger project they would collide with its own test registration.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(STOMP_BUILD_TESTS "Build unit tests and examples" ON)
else()
    option(STOMP_BUILD_TESTS "Build unit tests and examples" OFF)
endif()

if(STOMP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
    add_subdirectory(examples)
endif()
