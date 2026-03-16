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

# ─── Cross-process IPC test child process ─────────────────────
add_executable(ipc-test-child ipc_test_child.cpp)

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
        # Slice 3: Control Handlers
        test_midi_handler.cpp
        test_action_handler.cpp
        # Slice 5: Safety Limiter
        test_safety_limiter.cpp
        # Slice 6: Built-in Processors
        test_builtin_processors.cpp
        # Slice 4: Platform
        test_platform.cpp
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
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/PluginPreloadCache.cpp
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
        juce::juce_data_structures
        juce::juce_events
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
    )

    if(WIN32)
        target_link_libraries(directpipe-host-tests PRIVATE kernel32)
    endif()

    gtest_discover_tests(directpipe-host-tests)
endif()
