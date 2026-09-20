# DirectPipe Tests

include(GoogleTest)

# ─── Core IPC Tests (no JUCE dependency) ────────────────────────
add_executable(directpipe-tests
    test_ring_buffer.cpp
    test_shared_memory.cpp
    test_latency.cpp
    test_ipc_integration.cpp
    test_receiver_simulation.cpp
    test_cross_process_ipc.cpp
    test_fanout.cpp
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

# Actual Receiver callback coverage, using isolated in-memory test connections.
if(DIRECTPIPE_BUILD_RECEIVER)
    juce_add_console_app(directpipe-receiver-tests PRODUCT_NAME "DirectPipeReceiverTests")
    target_sources(directpipe-receiver-tests PRIVATE
        test_receiver_processor.cpp
        ${CMAKE_SOURCE_DIR}/plugins/receiver/Source/PluginProcessor.cpp
        ${CMAKE_SOURCE_DIR}/plugins/receiver/Source/PluginEditor.cpp
    )
    juce_generate_juce_header(directpipe-receiver-tests)
    target_compile_definitions(directpipe-receiver-tests PRIVATE
        DIRECTPIPE_ENABLE_TEST_ACCESS=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JucePlugin_VersionString="${PROJECT_VERSION}"
    )
    target_link_libraries(directpipe-receiver-tests PRIVATE
        directpipe-core GTest::gtest GTest::gtest_main
        juce::juce_audio_utils juce::juce_recommended_config_flags
    )
    if(WIN32)
        target_link_libraries(directpipe-receiver-tests PRIVATE kernel32 ole32 uuid)
    endif()
    gtest_discover_tests(directpipe-receiver-tests)
endif()

# ─── Cross-process IPC test child process ─────────────────────
add_executable(ipc-test-child ipc_test_child.cpp)

add_executable(fanout-test-child fanout_test_child.cpp)
target_link_libraries(fanout-test-child PRIVATE directpipe-core)
add_dependencies(directpipe-tests fanout-test-child)

target_link_libraries(ipc-test-child PRIVATE directpipe-core)

if(WIN32)
    target_link_libraries(ipc-test-child PRIVATE kernel32)
endif()

# ─── Host Tests (requires JUCE) ────────────────────────────────
if(DIRECTPIPE_BUILD_HOST)
    juce_add_console_app(directpipe-host-tests
        PRODUCT_NAME "DirectPipeHostTests"
    )

    target_sources(directpipe-host-tests PRIVATE
        # Existing tests
        test_websocket_protocol.cpp
        test_action_dispatcher.cpp
        test_action_result.cpp
        test_control_mapping.cpp
        test_notification_queue.cpp
        # Slice 1: Settings Persistence
        test_preset_manager.cpp
        test_settings_exporter.cpp
        test_settings_autosaver.cpp
        # Slice 2: Audio Engine
        test_output_router.cpp
        test_audio_engine.cpp
        test_audio_recorder.cpp
        test_monitor_output.cpp
        # Slice 3: Control Handlers
        test_midi_handler.cpp
        test_action_handler.cpp
        # Slice 5: Safety Guard (legacy SafetyLimiter class/API names)
        test_safety_limiter.cpp
        # Slice 6: Built-in Processors
        test_builtin_processors.cpp
        test_builtin_noise_removal.cpp
        test_builtin_auto_gain.cpp
        # Slice 7: VSTChain
        test_vst_chain.cpp
        # Slice 4: Platform
        test_platform.cpp
        test_shared_mem_fanout.cpp
        test_endpoint_change_watcher.cpp
        test_update_script.cpp
        test_receiver_update.cpp
        # Host source files needed by tests
        ${CMAKE_SOURCE_DIR}/host/Source/Control/ActionDispatcher.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/StateBroadcaster.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/ControlMapping.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/SettingsAutosaver.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/ActionHandler.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/MidiHandler.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/HotkeyHandler.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/ControlManager.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/WebSocketServer.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/HttpApiServer.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Control/Log.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/PresetManager.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/SettingsExporter.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/PresetSlotBar.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/PluginChainEditor.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/PluginScanner.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/AudioEngine.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/VSTChain.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/OutputRouter.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/MonitorOutput.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/LatencyMonitor.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/AudioRecorder.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/SafetyLimiter.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/BuiltinFilter.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/BuiltinAutoGain.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/BuiltinNoiseRemoval.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/PluginPreloadCache.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Platform/EndpointChangeWatcher.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/UpdateChecker.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/UpdateScript.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/ReceiverUpdateSupport.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/FilterEditPanel.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/NoiseRemovalEditPanel.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/UI/AGCEditPanel.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/IPC/SharedMemWriter.cpp
    )

    # Platform-specific sources
    if(WIN32)
        target_sources(directpipe-host-tests PRIVATE
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/Windows/WindowsAutoStart.cpp
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/Windows/WindowsProcessPriority.cpp
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/Windows/WindowsMultiInstanceLock.cpp
        )
    elseif(APPLE)
        target_sources(directpipe-host-tests PRIVATE
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/macOS/MacAutoStart.cpp
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/macOS/MacProcessPriority.cpp
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/macOS/MacMultiInstanceLock.cpp
        )
    else()
        target_sources(directpipe-host-tests PRIVATE
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/Linux/LinuxAutoStart.cpp
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/Linux/LinuxProcessPriority.cpp
            ${CMAKE_SOURCE_DIR}/host/Source/Platform/Linux/LinuxMultiInstanceLock.cpp
        )
    endif()

    juce_generate_juce_header(directpipe-host-tests)

    target_include_directories(directpipe-host-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/host/Source
    )

    target_compile_definitions(directpipe-host-tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        DIRECTPIPE_ENABLE_TEST_ACCESS=1
    )

    target_link_libraries(directpipe-host-tests PRIVATE
        directpipe-core
        rnnoise
        GTest::gtest
        GTest::gtest_main
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_events
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
    )

    if(WIN32)
        target_link_libraries(directpipe-host-tests PRIVATE kernel32 ole32 uuid Rstrtmgr Version Advapi32)
        if(TARGET DirectPipeReceiver_VST AND TARGET DirectPipeReceiver_VST3)
            add_dependencies(directpipe-host-tests DirectPipe DirectPipeReceiver_VST DirectPipeReceiver_VST3)
            target_compile_definitions(directpipe-host-tests PRIVATE
                DIRECTPIPE_TEST_HOST_PATH="$<TARGET_FILE:DirectPipe>"
                DIRECTPIPE_TEST_RECEIVER_VST2_PATH="$<TARGET_FILE:DirectPipeReceiver_VST>"
                DIRECTPIPE_TEST_RECEIVER_VST3_PATH="$<TARGET_FILE:DirectPipeReceiver_VST3>"
            )
        endif()
    endif()

    gtest_discover_tests(directpipe-host-tests)

    # Focused endpoint-notification tests remain buildable even when unrelated
    # host sources are temporarily under active development.
    juce_add_console_app(directpipe-endpoint-watcher-tests
        PRODUCT_NAME "DirectPipeEndpointWatcherTests"
    )

    target_sources(directpipe-endpoint-watcher-tests PRIVATE
        test_endpoint_change_watcher.cpp
        ${CMAKE_SOURCE_DIR}/host/Source/Platform/EndpointChangeWatcher.cpp
    )

    juce_generate_juce_header(directpipe-endpoint-watcher-tests)

    target_include_directories(directpipe-endpoint-watcher-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/host/Source
    )

    target_compile_definitions(directpipe-endpoint-watcher-tests PRIVATE
        DIRECTPIPE_ENABLE_TEST_ACCESS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(directpipe-endpoint-watcher-tests PRIVATE
        GTest::gtest
        GTest::gtest_main
        juce::juce_core
        juce::juce_events
        juce::juce_recommended_config_flags
    )

    if(WIN32)
        target_link_libraries(directpipe-endpoint-watcher-tests PRIVATE ole32 uuid)
    endif()

    gtest_discover_tests(directpipe-endpoint-watcher-tests
        TEST_PREFIX "EndpointWatcher."
    )
endif()
