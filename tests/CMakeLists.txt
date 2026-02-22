# DirectPipe Tests

add_executable(directpipe-tests
    test_ring_buffer.cpp
    test_shared_memory.cpp
    test_latency.cpp
    test_action_dispatcher.cpp
    test_websocket_protocol.cpp
    test_ipc_integration.cpp
)

target_link_libraries(directpipe-tests PRIVATE
    directpipe-core
    GTest::gtest
    GTest::gtest_main
)

if(WIN32)
    target_link_libraries(directpipe-tests PRIVATE kernel32)
endif()

include(GoogleTest)
gtest_discover_tests(directpipe-tests)
