FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.16.0
    GIT_SHALLOW true
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(ipc_proxy_tests
    test_lifecycle.cpp
    test_metadata.cpp
    test_input.cpp
    test_session.cpp
    test_frame.cpp
    ../src/ipc_server.cpp
    $<TARGET_OBJECTS:ox_ipc_frontend_obj>
)

target_include_directories(ipc_proxy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ipc_proxy_tests PRIVATE spdlog::spdlog ${OX_IPC_NNG_TARGET} GTest::gtest GTest::gtest_main)

set_target_properties(ipc_proxy_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/tests
)

enable_testing()
add_test(NAME ipc_proxy_tests COMMAND ipc_proxy_tests)