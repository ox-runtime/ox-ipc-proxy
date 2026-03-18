cmake_minimum_required(VERSION 3.27)

project(ox-ipc-proxy VERSION 0.1.0 LANGUAGES CXX)

if(WIN32 AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET spdlog::spdlog)
    include(FetchContent)

    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog
        GIT_TAG v1.17.0
        GIT_SHALLOW true
    )
    FetchContent_MakeAvailable(spdlog)
endif()

if(WIN32)
    set(IPC_PROXY_LIBS)
elseif(APPLE)
    set(IPC_PROXY_LIBS pthread)
else()
    set(IPC_PROXY_LIBS pthread rt dl)
endif()

set(COMMON_SOURCES)

add_library(ox_ipc_frontend SHARED
    ${COMMON_SOURCES}
    src/frontend/ipc_client.cpp
    src/frontend/driver_frontend.cpp
)

target_include_directories(ox_ipc_frontend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend
)

target_link_libraries(ox_ipc_frontend PRIVATE spdlog::spdlog ${IPC_PROXY_LIBS})

add_library(ox_ipc_backend SHARED
    ${COMMON_SOURCES}
    src/backend/ipc_server.cpp
    src/backend/backend_exports.cpp
)

target_include_directories(ox_ipc_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backend
)

target_link_libraries(ox_ipc_backend PRIVATE spdlog::spdlog ${IPC_PROXY_LIBS})

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