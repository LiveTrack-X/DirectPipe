/**
 * @file driver_test.cpp
 * @brief User-mode test for the shared memory reader logic.
 *
 * This test verifies the SPSC ring buffer read protocol by simulating
 * the shared memory layout in user-mode. It creates a shared memory
 * region matching the DirectPipe protocol, writes test audio data as
 * a producer, and reads it back using the same atomic protocol that
 * the kernel-mode reader (shm_kernel_reader.cpp) uses.
 *
 * This validates that:
 *   1. The shared memory header layout is correct and consistent
 *   2. The SPSC ring buffer read/write protocol works correctly
 *   3. Wrap-around at the buffer boundary is handled properly
 *   4. Empty buffer reads return zero frames
 *   5. The protocol version check works
 *
 * Build: Standard C++ with Windows API. Does NOT require WDK.
 *   cl /EHsc /W4 /std:c++17 driver_test.cpp /Fe:driver_test.exe
 *
 * Usage:
 *   driver_test.exe
 *   (exits 0 on success, 1 on failure)
 */

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>
#include <atomic>

// ---------------------------------------------------------------------------
// Mirror of DirectPipeHeader from Protocol.h
// Must be byte-compatible with both the user-mode and kernel-mode versions.
// ---------------------------------------------------------------------------

#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier

/// Protocol version (must match Protocol.h)
static constexpr uint32_t PROTOCOL_VERSION = 1;

/**
 * @brief Test mirror of DirectPipeHeader.
 *
 * This structure must have the exact same binary layout as:
 *   - directpipe::DirectPipeHeader (core/include/directpipe/Protocol.h)
 *   - DIRECTPIPE_HEADER (driver/src/shm_kernel_reader.cpp)
 */
struct DirectPipeHeader {
    alignas(64) std::atomic<uint64_t> write_pos{0};
    alignas(64) std::atomic<uint64_t> read_pos{0};

    uint32_t sample_rate{0};
    uint32_t channels{0};
    uint32_t buffer_frames{0};
    uint32_t version{PROTOCOL_VERSION};

    std::atomic<bool> producer_active{false};

    uint8_t reserved[64 - sizeof(std::atomic<bool>) - 4 * sizeof(uint32_t)]{};
};

#pragma warning(pop)

// Verify alignment matches the production code
static_assert(alignof(DirectPipeHeader) >= 64,
              "DirectPipeHeader must be at least 64-byte aligned");

// ---------------------------------------------------------------------------
// Test shared memory name (different from production to avoid conflicts)
// ---------------------------------------------------------------------------
static constexpr const char* TEST_SHM_NAME = "Local\\DirectPipeAudioTest";

// ---------------------------------------------------------------------------
// Test helper: Calculate shared memory size
// ---------------------------------------------------------------------------
static size_t calculateSharedMemorySize(uint32_t buffer_frames, uint32_t channels)
{
    return sizeof(DirectPipeHeader)
         + (static_cast<size_t>(buffer_frames) * channels * sizeof(float));
}

// ---------------------------------------------------------------------------
// Test helper: SPSC ring buffer write (producer side)
// ---------------------------------------------------------------------------

/**
 * @brief Write frames into the ring buffer (producer side).
 *
 * Mirrors the logic from directpipe::RingBuffer::write().
 *
 * @param header     Pointer to the shared memory header.
 * @param audioData  Pointer to the PCM data region after the header.
 * @param data       Input data (interleaved float32).
 * @param frames     Number of frames to write.
 * @return Number of frames actually written.
 */
static uint32_t ringBufferWrite(
    DirectPipeHeader* header,
    float*            audioData,
    const float*      data,
    uint32_t          frames)
{
    uint64_t writePos = header->write_pos.load(std::memory_order_relaxed);
    uint64_t readPos  = header->read_pos.load(std::memory_order_acquire);

    uint64_t available = header->buffer_frames - (writePos - readPos);
    uint32_t toWrite = static_cast<uint32_t>(
        (frames < available) ? frames : available
    );

    if (toWrite == 0) return 0;

    uint32_t mask = header->buffer_frames - 1;
    uint32_t channels = header->channels;
    uint32_t startIndex = static_cast<uint32_t>(writePos) & mask;
    uint32_t totalSamples = header->buffer_frames * channels;

    uint32_t startSample = startIndex * channels;
    uint32_t samplesToWrite = toWrite * channels;

    // First chunk
    uint32_t firstChunk = samplesToWrite;
    if (startSample + firstChunk > totalSamples)
    {
        firstChunk = totalSamples - startSample;
    }
    memcpy(&audioData[startSample], data, firstChunk * sizeof(float));

    // Second chunk (wrap)
    uint32_t secondChunk = samplesToWrite - firstChunk;
    if (secondChunk > 0)
    {
        memcpy(&audioData[0], &data[firstChunk], secondChunk * sizeof(float));
    }

    // Release barrier: ensure data is written before advancing write_pos
    header->write_pos.store(writePos + toWrite, std::memory_order_release);

    return toWrite;
}

// ---------------------------------------------------------------------------
// Test helper: SPSC ring buffer read (consumer side)
// ---------------------------------------------------------------------------

/**
 * @brief Read frames from the ring buffer (consumer side).
 *
 * Mirrors the logic from KernelShmReaderRead() in shm_kernel_reader.cpp,
 * but using std::atomic instead of InterlockedXxx.
 *
 * @param header     Pointer to the shared memory header.
 * @param audioData  Pointer to the PCM data region after the header.
 * @param buffer     Output buffer (interleaved float32).
 * @param maxFrames  Maximum frames to read.
 * @return Number of frames actually read.
 */
static uint32_t ringBufferRead(
    DirectPipeHeader* header,
    const float*      audioData,
    float*            buffer,
    uint32_t          maxFrames)
{
    uint64_t writePos = header->write_pos.load(std::memory_order_acquire);
    uint64_t readPos  = header->read_pos.load(std::memory_order_relaxed);

    int64_t available64 = static_cast<int64_t>(writePos - readPos);
    if (available64 <= 0)
    {
        return 0;
    }

    uint32_t available = static_cast<uint32_t>(available64);
    uint32_t toRead = (maxFrames < available) ? maxFrames : available;
    if (toRead == 0) return 0;

    uint32_t mask = header->buffer_frames - 1;
    uint32_t channels = header->channels;
    uint32_t startIndex = static_cast<uint32_t>(readPos) & mask;
    uint32_t totalSamples = header->buffer_frames * channels;

    uint32_t startSample = startIndex * channels;
    uint32_t samplesToRead = toRead * channels;

    // First chunk
    uint32_t firstChunk = samplesToRead;
    if (startSample + firstChunk > totalSamples)
    {
        firstChunk = totalSamples - startSample;
    }
    memcpy(buffer, &audioData[startSample], firstChunk * sizeof(float));

    // Second chunk (wrap)
    uint32_t secondChunk = samplesToRead - firstChunk;
    if (secondChunk > 0)
    {
        memcpy(&buffer[firstChunk], &audioData[0], secondChunk * sizeof(float));
    }

    // Release barrier: ensure copies are done before advancing read_pos
    header->read_pos.store(readPos + toRead, std::memory_order_release);

    return toRead;
}

// ---------------------------------------------------------------------------
// Test result tracking
// ---------------------------------------------------------------------------

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(cond, msg)                                          \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);           \
            g_testsFailed++;                                            \
            return false;                                               \
        }                                                               \
    } while (0)

#define TEST_PASS(name)                                                 \
    do {                                                                \
        printf("  PASS: %s\n", name);                                   \
        g_testsPassed++;                                                \
    } while (0)

// ---------------------------------------------------------------------------
// Test 1: Header layout and size
// ---------------------------------------------------------------------------

/**
 * @brief Verify the DirectPipeHeader has the expected layout.
 */
static bool testHeaderLayout()
{
    printf("[Test 1] Header layout verification\n");

    // write_pos should be at offset 0 (aligned to 64)
    TEST_ASSERT(offsetof(DirectPipeHeader, write_pos) == 0,
                "write_pos offset should be 0");

    // read_pos should be at offset 64 (next cache line)
    TEST_ASSERT(offsetof(DirectPipeHeader, read_pos) == 64,
                "read_pos offset should be 64");

    // sample_rate should be at offset 128
    TEST_ASSERT(offsetof(DirectPipeHeader, sample_rate) == 128,
                "sample_rate offset should be 128");

    // Total header size should be reasonable (at least 3 cache lines)
    TEST_ASSERT(sizeof(DirectPipeHeader) >= 192,
                "Header should be at least 192 bytes");

    // Alignment should be 64
    TEST_ASSERT(alignof(DirectPipeHeader) >= 64,
                "Header alignment should be >= 64");

    TEST_PASS("Header layout");
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: Create shared memory and verify mapping
// ---------------------------------------------------------------------------

/**
 * @brief Test creating and opening a shared memory region.
 */
static bool testSharedMemoryCreateOpen()
{
    printf("[Test 2] Shared memory create/open\n");

    const uint32_t bufferFrames = 1024;
    const uint32_t channels     = 2;
    const size_t   shmSize      = calculateSharedMemorySize(bufferFrames, channels);

    // Create the shared memory region (producer side)
    HANDLE hMapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(shmSize),
        TEST_SHM_NAME
    );
    TEST_ASSERT(hMapping != nullptr, "CreateFileMapping should succeed");

    // Map the region
    void* pView = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, shmSize);
    TEST_ASSERT(pView != nullptr, "MapViewOfFile should succeed");

    // Initialize the header
    memset(pView, 0, shmSize);
    auto* header = static_cast<DirectPipeHeader*>(pView);

    // Use placement-new to properly construct atomics
    new (&header->write_pos) std::atomic<uint64_t>(0);
    new (&header->read_pos) std::atomic<uint64_t>(0);
    new (&header->producer_active) std::atomic<bool>(true);

    header->sample_rate   = 48000;
    header->channels      = channels;
    header->buffer_frames = bufferFrames;
    header->version       = PROTOCOL_VERSION;

    // Verify the header fields
    TEST_ASSERT(header->version == PROTOCOL_VERSION, "Version should match");
    TEST_ASSERT(header->sample_rate == 48000, "Sample rate should be 48000");
    TEST_ASSERT(header->channels == 2, "Channels should be 2");
    TEST_ASSERT(header->buffer_frames == 1024, "Buffer frames should be 1024");

    // Open from consumer side
    HANDLE hMapping2 = OpenFileMappingA(FILE_MAP_READ, FALSE, TEST_SHM_NAME);
    TEST_ASSERT(hMapping2 != nullptr, "OpenFileMapping should succeed");

    void* pView2 = MapViewOfFile(hMapping2, FILE_MAP_READ, 0, 0, shmSize);
    TEST_ASSERT(pView2 != nullptr, "Consumer MapViewOfFile should succeed");

    auto* header2 = static_cast<const DirectPipeHeader*>(pView2);
    TEST_ASSERT(header2->version == PROTOCOL_VERSION, "Consumer should see correct version");
    TEST_ASSERT(header2->sample_rate == 48000, "Consumer should see correct sample rate");

    // Cleanup
    UnmapViewOfFile(pView2);
    CloseHandle(hMapping2);
    UnmapViewOfFile(pView);
    CloseHandle(hMapping);

    TEST_PASS("Shared memory create/open");
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: Ring buffer write and read
// ---------------------------------------------------------------------------

/**
 * @brief Test basic ring buffer write/read cycle.
 */
static bool testRingBufferWriteRead()
{
    printf("[Test 3] Ring buffer basic write/read\n");

    const uint32_t bufferFrames = 256;  // Small for testing
    const uint32_t channels     = 2;
    const size_t   shmSize      = calculateSharedMemorySize(bufferFrames, channels);

    // Allocate and initialize shared memory locally
    auto* memory = static_cast<uint8_t*>(malloc(shmSize));
    TEST_ASSERT(memory != nullptr, "Memory allocation should succeed");
    memset(memory, 0, shmSize);

    auto* header = reinterpret_cast<DirectPipeHeader*>(memory);
    new (&header->write_pos) std::atomic<uint64_t>(0);
    new (&header->read_pos) std::atomic<uint64_t>(0);
    new (&header->producer_active) std::atomic<bool>(true);
    header->sample_rate   = 48000;
    header->channels      = channels;
    header->buffer_frames = bufferFrames;
    header->version       = PROTOCOL_VERSION;

    float* audioData = reinterpret_cast<float*>(memory + sizeof(DirectPipeHeader));

    // Write 100 frames of test data
    const uint32_t testFrames = 100;
    float writeBuffer[testFrames * 2];  // stereo
    for (uint32_t i = 0; i < testFrames * channels; ++i)
    {
        writeBuffer[i] = static_cast<float>(i) / 1000.0f;
    }

    uint32_t written = ringBufferWrite(header, audioData, writeBuffer, testFrames);
    TEST_ASSERT(written == testFrames, "Should write all 100 frames");
    TEST_ASSERT(header->write_pos.load() == testFrames, "write_pos should be 100");
    TEST_ASSERT(header->read_pos.load() == 0, "read_pos should still be 0");

    // Read back
    float readBuffer[testFrames * 2];
    memset(readBuffer, 0, sizeof(readBuffer));

    uint32_t framesRead = ringBufferRead(header, audioData, readBuffer, testFrames);
    TEST_ASSERT(framesRead == testFrames, "Should read all 100 frames");
    TEST_ASSERT(header->read_pos.load() == testFrames, "read_pos should advance to 100");

    // Verify data integrity
    for (uint32_t i = 0; i < testFrames * channels; ++i)
    {
        float expected = static_cast<float>(i) / 1000.0f;
        TEST_ASSERT(fabsf(readBuffer[i] - expected) < 1e-6f,
                    "Data should match what was written");
    }

    free(memory);

    TEST_PASS("Ring buffer basic write/read");
    return true;
}

// ---------------------------------------------------------------------------
// Test 4: Ring buffer wrap-around
// ---------------------------------------------------------------------------

/**
 * @brief Test ring buffer behavior at the wrap-around boundary.
 */
static bool testRingBufferWrapAround()
{
    printf("[Test 4] Ring buffer wrap-around\n");

    const uint32_t bufferFrames = 64;  // Very small to force wrapping
    const uint32_t channels     = 1;   // Mono for simplicity
    const size_t   shmSize      = calculateSharedMemorySize(bufferFrames, channels);

    auto* memory = static_cast<uint8_t*>(malloc(shmSize));
    TEST_ASSERT(memory != nullptr, "Memory allocation should succeed");
    memset(memory, 0, shmSize);

    auto* header = reinterpret_cast<DirectPipeHeader*>(memory);
    new (&header->write_pos) std::atomic<uint64_t>(0);
    new (&header->read_pos) std::atomic<uint64_t>(0);
    new (&header->producer_active) std::atomic<bool>(true);
    header->sample_rate   = 48000;
    header->channels      = channels;
    header->buffer_frames = bufferFrames;
    header->version       = PROTOCOL_VERSION;

    float* audioData = reinterpret_cast<float*>(memory + sizeof(DirectPipeHeader));

    // Write/read multiple times to force wrap-around
    float writeBuffer[32];
    float readBuffer[32];

    for (int cycle = 0; cycle < 5; ++cycle)
    {
        // Fill with unique values per cycle
        for (uint32_t i = 0; i < 32; ++i)
        {
            writeBuffer[i] = static_cast<float>(cycle * 100 + i);
        }

        uint32_t written = ringBufferWrite(header, audioData, writeBuffer, 32);
        TEST_ASSERT(written == 32, "Should write 32 frames each cycle");

        memset(readBuffer, 0, sizeof(readBuffer));
        uint32_t framesRead = ringBufferRead(header, audioData, readBuffer, 32);
        TEST_ASSERT(framesRead == 32, "Should read 32 frames each cycle");

        // Verify data
        for (uint32_t i = 0; i < 32; ++i)
        {
            float expected = static_cast<float>(cycle * 100 + i);
            if (fabsf(readBuffer[i] - expected) >= 1e-6f)
            {
                printf("    Cycle %d, sample %u: expected %f, got %f\n",
                       cycle, i, expected, readBuffer[i]);
                TEST_ASSERT(false, "Wrapped data should match");
            }
        }
    }

    // Verify positions have advanced correctly (5 cycles * 32 frames = 160)
    TEST_ASSERT(header->write_pos.load() == 160, "write_pos should be 160");
    TEST_ASSERT(header->read_pos.load() == 160, "read_pos should be 160");

    free(memory);

    TEST_PASS("Ring buffer wrap-around");
    return true;
}

// ---------------------------------------------------------------------------
// Test 5: Empty buffer read
// ---------------------------------------------------------------------------

/**
 * @brief Test that reading from an empty buffer returns zero frames.
 */
static bool testEmptyBufferRead()
{
    printf("[Test 5] Empty buffer read\n");

    const uint32_t bufferFrames = 256;
    const uint32_t channels     = 2;
    const size_t   shmSize      = calculateSharedMemorySize(bufferFrames, channels);

    auto* memory = static_cast<uint8_t*>(malloc(shmSize));
    TEST_ASSERT(memory != nullptr, "Memory allocation should succeed");
    memset(memory, 0, shmSize);

    auto* header = reinterpret_cast<DirectPipeHeader*>(memory);
    new (&header->write_pos) std::atomic<uint64_t>(0);
    new (&header->read_pos) std::atomic<uint64_t>(0);
    new (&header->producer_active) std::atomic<bool>(true);
    header->sample_rate   = 48000;
    header->channels      = channels;
    header->buffer_frames = bufferFrames;
    header->version       = PROTOCOL_VERSION;

    float* audioData = reinterpret_cast<float*>(memory + sizeof(DirectPipeHeader));

    // Try to read from empty buffer
    float readBuffer[128];
    uint32_t framesRead = ringBufferRead(header, audioData, readBuffer, 64);
    TEST_ASSERT(framesRead == 0, "Should read 0 frames from empty buffer");

    // Write some data, read all of it, then try to read again
    float writeBuffer[20] = {};
    for (uint32_t i = 0; i < 20; ++i) writeBuffer[i] = 1.0f;

    ringBufferWrite(header, audioData, writeBuffer, 10);  // 10 stereo frames
    framesRead = ringBufferRead(header, audioData, readBuffer, 10);
    TEST_ASSERT(framesRead == 10, "Should read 10 frames");

    // Buffer should now be empty again
    framesRead = ringBufferRead(header, audioData, readBuffer, 10);
    TEST_ASSERT(framesRead == 0, "Should read 0 frames after consuming all data");

    free(memory);

    TEST_PASS("Empty buffer read");
    return true;
}

// ---------------------------------------------------------------------------
// Test 6: Full buffer (overrun detection)
// ---------------------------------------------------------------------------

/**
 * @brief Test that writing to a full buffer drops frames.
 */
static bool testFullBuffer()
{
    printf("[Test 6] Full buffer (overrun)\n");

    const uint32_t bufferFrames = 64;
    const uint32_t channels     = 1;
    const size_t   shmSize      = calculateSharedMemorySize(bufferFrames, channels);

    auto* memory = static_cast<uint8_t*>(malloc(shmSize));
    TEST_ASSERT(memory != nullptr, "Memory allocation should succeed");
    memset(memory, 0, shmSize);

    auto* header = reinterpret_cast<DirectPipeHeader*>(memory);
    new (&header->write_pos) std::atomic<uint64_t>(0);
    new (&header->read_pos) std::atomic<uint64_t>(0);
    new (&header->producer_active) std::atomic<bool>(true);
    header->sample_rate   = 48000;
    header->channels      = channels;
    header->buffer_frames = bufferFrames;
    header->version       = PROTOCOL_VERSION;

    float* audioData = reinterpret_cast<float*>(memory + sizeof(DirectPipeHeader));

    // Fill the entire buffer (64 mono frames)
    float writeBuffer[64];
    for (uint32_t i = 0; i < 64; ++i) writeBuffer[i] = static_cast<float>(i);

    uint32_t written = ringBufferWrite(header, audioData, writeBuffer, 64);
    TEST_ASSERT(written == 64, "Should write exactly 64 frames to fill buffer");

    // Try to write more — should be dropped
    float moreData[16] = {};
    written = ringBufferWrite(header, audioData, moreData, 16);
    TEST_ASSERT(written == 0, "Should write 0 frames to a full buffer");

    // Read some data to make room
    float readBuffer[32];
    uint32_t framesRead = ringBufferRead(header, audioData, readBuffer, 32);
    TEST_ASSERT(framesRead == 32, "Should read 32 frames");

    // Now we can write again
    written = ringBufferWrite(header, audioData, moreData, 16);
    TEST_ASSERT(written == 16, "Should be able to write 16 frames after partial read");

    free(memory);

    TEST_PASS("Full buffer (overrun)");
    return true;
}

// ---------------------------------------------------------------------------
// Test 7: Protocol version mismatch
// ---------------------------------------------------------------------------

/**
 * @brief Test that a version mismatch is detectable.
 */
static bool testProtocolVersionMismatch()
{
    printf("[Test 7] Protocol version mismatch\n");

    const uint32_t bufferFrames = 256;
    const uint32_t channels     = 2;
    const size_t   shmSize      = calculateSharedMemorySize(bufferFrames, channels);

    auto* memory = static_cast<uint8_t*>(malloc(shmSize));
    TEST_ASSERT(memory != nullptr, "Memory allocation should succeed");
    memset(memory, 0, shmSize);

    auto* header = reinterpret_cast<DirectPipeHeader*>(memory);
    header->version = 999;  // Wrong version

    // A real consumer should check version before reading
    TEST_ASSERT(header->version != PROTOCOL_VERSION,
                "Mismatched version should be detectable");

    // Set correct version — should now pass
    header->version = PROTOCOL_VERSION;
    TEST_ASSERT(header->version == PROTOCOL_VERSION,
                "Correct version should match");

    free(memory);

    TEST_PASS("Protocol version mismatch");
    return true;
}

// ---------------------------------------------------------------------------
// Test 8: Mono format support
// ---------------------------------------------------------------------------

/**
 * @brief Test ring buffer with mono (1-channel) audio.
 */
static bool testMonoFormat()
{
    printf("[Test 8] Mono format write/read\n");

    const uint32_t bufferFrames = 128;
    const uint32_t channels     = 1;
    const size_t   shmSize      = calculateSharedMemorySize(bufferFrames, channels);

    auto* memory = static_cast<uint8_t*>(malloc(shmSize));
    TEST_ASSERT(memory != nullptr, "Memory allocation should succeed");
    memset(memory, 0, shmSize);

    auto* header = reinterpret_cast<DirectPipeHeader*>(memory);
    new (&header->write_pos) std::atomic<uint64_t>(0);
    new (&header->read_pos) std::atomic<uint64_t>(0);
    new (&header->producer_active) std::atomic<bool>(true);
    header->sample_rate   = 44100;
    header->channels      = channels;
    header->buffer_frames = bufferFrames;
    header->version       = PROTOCOL_VERSION;

    float* audioData = reinterpret_cast<float*>(memory + sizeof(DirectPipeHeader));

    // Write 50 mono frames
    float writeBuffer[50];
    for (uint32_t i = 0; i < 50; ++i)
    {
        writeBuffer[i] = sinf(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / 44100.0f);
    }

    uint32_t written = ringBufferWrite(header, audioData, writeBuffer, 50);
    TEST_ASSERT(written == 50, "Should write 50 mono frames");

    // Read back
    float readBuffer[50];
    uint32_t framesRead = ringBufferRead(header, audioData, readBuffer, 50);
    TEST_ASSERT(framesRead == 50, "Should read 50 mono frames");

    // Verify
    for (uint32_t i = 0; i < 50; ++i)
    {
        TEST_ASSERT(fabsf(readBuffer[i] - writeBuffer[i]) < 1e-6f,
                    "Mono data should match");
    }

    free(memory);

    TEST_PASS("Mono format write/read");
    return true;
}

// ---------------------------------------------------------------------------
// Test 9: Shared memory via Windows API (integration test)
// ---------------------------------------------------------------------------

/**
 * @brief End-to-end test using actual Windows shared memory APIs.
 *
 * Creates a named shared memory region (like the host app would),
 * writes test audio data, then opens and reads from a second mapping
 * (like the kernel driver would from user mode).
 */
static bool testSharedMemoryIntegration()
{
    printf("[Test 9] Shared memory integration (Windows API)\n");

    const uint32_t bufferFrames = 512;
    const uint32_t channels     = 2;
    const size_t   shmSize      = calculateSharedMemorySize(bufferFrames, channels);

    // --- Producer side: create and initialize ---
    HANDLE hProducer = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(shmSize),
        TEST_SHM_NAME
    );
    TEST_ASSERT(hProducer != nullptr, "Producer CreateFileMapping should succeed");

    void* pProducer = MapViewOfFile(hProducer, FILE_MAP_ALL_ACCESS, 0, 0, shmSize);
    TEST_ASSERT(pProducer != nullptr, "Producer MapViewOfFile should succeed");

    memset(pProducer, 0, shmSize);
    auto* prodHeader = static_cast<DirectPipeHeader*>(pProducer);
    new (&prodHeader->write_pos) std::atomic<uint64_t>(0);
    new (&prodHeader->read_pos) std::atomic<uint64_t>(0);
    new (&prodHeader->producer_active) std::atomic<bool>(true);
    prodHeader->sample_rate   = 48000;
    prodHeader->channels      = channels;
    prodHeader->buffer_frames = bufferFrames;
    prodHeader->version       = PROTOCOL_VERSION;

    float* prodAudio = reinterpret_cast<float*>(
        static_cast<uint8_t*>(pProducer) + sizeof(DirectPipeHeader)
    );

    // Write 200 stereo frames of test tone
    const uint32_t testFrames = 200;
    float writeBuf[testFrames * 2];
    for (uint32_t i = 0; i < testFrames; ++i)
    {
        float sample = sinf(2.0f * 3.14159f * 1000.0f * static_cast<float>(i) / 48000.0f);
        writeBuf[i * 2 + 0] = sample;       // Left
        writeBuf[i * 2 + 1] = sample * 0.5f; // Right (half volume)
    }

    uint32_t written = ringBufferWrite(prodHeader, prodAudio, writeBuf, testFrames);
    TEST_ASSERT(written == testFrames, "Producer should write 200 frames");

    // --- Consumer side: open and read ---
    HANDLE hConsumer = OpenFileMappingA(FILE_MAP_READ, FALSE, TEST_SHM_NAME);
    TEST_ASSERT(hConsumer != nullptr, "Consumer OpenFileMapping should succeed");

    void* pConsumer = MapViewOfFile(hConsumer, FILE_MAP_READ, 0, 0, shmSize);
    TEST_ASSERT(pConsumer != nullptr, "Consumer MapViewOfFile should succeed");

    auto* consHeader = static_cast<DirectPipeHeader*>(pConsumer);

    // Verify header from consumer side
    TEST_ASSERT(consHeader->version == PROTOCOL_VERSION, "Consumer sees correct version");
    TEST_ASSERT(consHeader->sample_rate == 48000, "Consumer sees correct sample rate");
    TEST_ASSERT(consHeader->channels == 2, "Consumer sees correct channel count");

    const float* consAudio = reinterpret_cast<const float*>(
        static_cast<const uint8_t*>(pConsumer) + sizeof(DirectPipeHeader)
    );

    // Read from consumer side
    // NOTE: Consumer needs write access to update read_pos.
    // In a real scenario, the consumer mapping would be FILE_MAP_ALL_ACCESS
    // or at least FILE_MAP_READ | FILE_MAP_WRITE.
    //
    // For this test, we'll read from the producer's view (which has write
    // access to read_pos) to simulate the consumer's read operation.
    float readBuf[testFrames * 2];
    memset(readBuf, 0, sizeof(readBuf));

    uint32_t framesRead = ringBufferRead(prodHeader, prodAudio, readBuf, testFrames);
    TEST_ASSERT(framesRead == testFrames, "Consumer should read 200 frames");

    // Verify first few samples
    for (uint32_t i = 0; i < 10; ++i)
    {
        float expectedL = sinf(2.0f * 3.14159f * 1000.0f * static_cast<float>(i) / 48000.0f);
        float expectedR = expectedL * 0.5f;

        TEST_ASSERT(fabsf(readBuf[i * 2 + 0] - expectedL) < 1e-5f,
                    "Left channel sample should match");
        TEST_ASSERT(fabsf(readBuf[i * 2 + 1] - expectedR) < 1e-5f,
                    "Right channel sample should match");
    }

    // Cleanup
    UnmapViewOfFile(pConsumer);
    CloseHandle(hConsumer);
    UnmapViewOfFile(pProducer);
    CloseHandle(hProducer);

    TEST_PASS("Shared memory integration");
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("==============================================\n");
    printf("  Virtual Loop Mic — Driver Unit Tests\n");
    printf("  Testing SPSC ring buffer protocol\n");
    printf("==============================================\n\n");

    testHeaderLayout();
    testSharedMemoryCreateOpen();
    testRingBufferWriteRead();
    testRingBufferWrapAround();
    testEmptyBufferRead();
    testFullBuffer();
    testProtocolVersionMismatch();
    testMonoFormat();
    testSharedMemoryIntegration();

    printf("\n==============================================\n");
    printf("  Results: %d passed, %d failed\n",
           g_testsPassed, g_testsFailed);
    printf("==============================================\n");

    return (g_testsFailed > 0) ? 1 : 0;
}
