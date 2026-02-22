# DirectPipe Tests

include(GoogleTest)

# ─── Core IPC Tests (no JUCE dependency) ────────────────────────
add_executable(directpipe-tests
    test_ring_buffer.cpp
    test_shared_memory.cpp
    test_latency.cpp
    test_action_dispatcher.cpp
    test_ipc_integration.cpp
    # ActionDispatcher source compiled directly (no JUCE dependency)
    ${CMAKE_SOURCE_DIR}/host/Source/Control/ActionDispatcher.cpp
)

target_include_directories(directpipe-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/host/Source
)

target_link_libraries(directpipe-tests PRIVATE
    directpipe-core
    GTest::gtest
    GTest::gtest_main
)

if(WIN32)
    target_link_libraries(directpipe-tests PRIVATE kernel32)
endif()

gtest_discover_tests(directpipe-tests)

# ─── Host Tests (requires JUCE) ────────────────────────────────
if(DIRECTPIPE_BUILD_HOST)
    juce_add_console_app(directpipe-host-tests
        PRODUCT_NAME "DirectPipeHostTests"
    )

    target_sources(directpipe-host-tests PRIVATE
        test_websocket_protocol.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/ActionDispatcher.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/StateBroadcaster.cpp
    )

    target_include_directories(directpipe-host-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/host/Source
    )

    target_compile_definitions(directpipe-host-tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(directpipe-host-tests PRIVATE
        directpipe-core
        GTest::gtest
        GTest::gtest_main
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_recommended_config_flags
    )

    if(WIN32)
        target_link_libraries(directpipe-host-tests PRIVATE kernel32)
    endif()

    gtest_discover_tests(directpipe-host-tests)
endif()
