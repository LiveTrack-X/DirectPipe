# DirectPipe Tests

# Core + ActionDispatcher tests (no JUCE dependency)
add_executable(directpipe-tests
    test_ring_buffer.cpp
    test_shared_memory.cpp
    test_latency.cpp
    test_ipc_integration.cpp
    test_action_dispatcher.cpp
    # ActionDispatcher no longer depends on JUCE, compile it directly
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

include(GoogleTest)
gtest_discover_tests(directpipe-tests)

# Host-dependent tests (require JUCE for StateBroadcaster JSON, etc.)
if(DIRECTPIPE_BUILD_HOST)
    add_executable(directpipe-host-tests
        test_websocket_protocol.cpp
        # Host source files needed by the tests
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

    # Generate JuceHeader.h for this target (needed by StateBroadcaster.cpp)
    juce_generate_juce_header(directpipe-host-tests)

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
