cmake_minimum_required(VERSION 3.27)

project(ox-ipc-proxy VERSION 0.1.0 LANGUAGES CXX)

if(WIN32 AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT WIN32)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(OX_DRIVER_HEADER_LOCAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ox_driver.h")
if(EXISTS "${OX_DRIVER_HEADER_LOCAL_PATH}")
    set(OX_DRIVER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    FetchContent_Declare(
        ox_driver_header
        URL https://github.com/ox-runtime/ox-runtime/releases/latest/download/ox_driver.h
        DOWNLOAD_NO_EXTRACT TRUE
        DOWNLOAD_NAME ox_driver.h
    )
    FetchContent_GetProperties(ox_driver_header)
    if(NOT ox_driver_header_POPULATED)
        FetchContent_Populate(ox_driver_header)
    endif()
    set(OX_DRIVER_INCLUDE_DIR "${ox_driver_header_SOURCE_DIR}")
endif()

if(NOT TARGET spdlog::spdlog)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog
        GIT_TAG v1.17.0
        GIT_SHALLOW true
    )
    if(NOT WIN32)
        set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(spdlog)
endif()

if(NOT TARGET nng AND NOT TARGET nng::nng)
    FetchContent_Declare(
        nng
        GIT_REPOSITORY https://github.com/nanomsg/nng
        GIT_TAG v1.10.1
        GIT_SHALLOW true
    )
    set(NNG_TESTS OFF CACHE BOOL "" FORCE)
    set(NNG_TOOLS OFF CACHE BOOL "" FORCE)
    set(NNG_ENABLE_STATS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nng)
endif()

if(TARGET nng::nng)
    set(OX_IPC_NNG_TARGET nng::nng)
else()
    set(OX_IPC_NNG_TARGET nng)
endif()

add_library(ox_ipc_frontend_obj OBJECT
    src/ipc_client.cpp
)

target_include_directories(ox_ipc_frontend_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OX_DRIVER_INCLUDE_DIR}
)

target_link_libraries(ox_ipc_frontend_obj PUBLIC spdlog::spdlog ${OX_IPC_NNG_TARGET})

add_library(ox_ipc_frontend SHARED
    $<TARGET_OBJECTS:ox_ipc_frontend_obj>
)

target_link_libraries(ox_ipc_frontend PRIVATE ox_ipc_frontend_obj)

add_library(ox_ipc_backend SHARED
    src/ipc_server.cpp
)

target_include_directories(ox_ipc_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OX_DRIVER_INCLUDE_DIR}
)

target_link_libraries(ox_ipc_backend PRIVATE spdlog::spdlog ${OX_IPC_NNG_TARGET})

function(configure_proxy_target target output_name)
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output_name}"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib
    )

    if(WIN32)
        set_target_properties(${target} PROPERTIES PREFIX "")
    else()
        set_target_properties(${target} PROPERTIES PREFIX "lib")
    endif()
endfunction()

configure_proxy_target(ox_ipc_frontend "ox_ipc_frontend")
configure_proxy_target(ox_ipc_backend "ox_ipc_backend")

add_subdirectory(tests)
