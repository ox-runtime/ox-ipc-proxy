cmake_minimum_required(VERSION 3.27)

project(ox-ipc-proxy VERSION 0.1.0 LANGUAGES CXX)

if(WIN32 AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if(NOT TARGET spdlog::spdlog)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog
        GIT_TAG v1.17.0
        GIT_SHALLOW true
    )
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

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.16.0
    GIT_SHALLOW true
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(ox_ipc_frontend_obj OBJECT
    src/ipc_client.cpp
)

target_include_directories(ox_ipc_frontend_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ox_ipc_backend PRIVATE spdlog::spdlog ${OX_IPC_NNG_TARGET})

add_executable(ipc_proxy_tests
    tests/test_lifecycle.cpp
    tests/test_metadata.cpp
    tests/test_input.cpp
    tests/test_session.cpp
    tests/test_frame.cpp
    src/ipc_server.cpp
    $<TARGET_OBJECTS:ox_ipc_frontend_obj>
)

target_include_directories(ipc_proxy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(ipc_proxy_tests PRIVATE spdlog::spdlog ${OX_IPC_NNG_TARGET} GTest::gtest GTest::gtest_main)

if(WIN32)
    target_compile_definitions(ox_ipc_backend PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_definitions(ox_ipc_frontend_obj PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_definitions(ipc_proxy_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

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

set_target_properties(ipc_proxy_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/tests
)

enable_testing()
add_test(NAME ipc_proxy_tests COMMAND ipc_proxy_tests)
