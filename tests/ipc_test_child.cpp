/**
 * @file ipc_test_child.cpp
 * @brief Cross-process IPC test child process
 *
 * Spawned by test_cross_process_ipc.cpp as a separate process.
 * Opens shared memory created by the parent, reads audio data,
 * verifies data integrity, and returns exit code 0 on success.
 *
 * Exit codes:
 *   0 = success (all blocks verified)
 *   1 = failed to open shared memory
 *   2 = failed to attach ring buffer
 *   3 = producer not active
 *   4 = data verification failed
 *   5 = incomplete read (fewer blocks than expected)
 */

#include "directpipe/SharedMemory.h"
#include "directpipe/RingBuffer.h"
#include "directpipe/Constants.h"
#include "directpipe/Protocol.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Must match values in test_cross_process_ipc.cpp
static constexpr const char* kShmName = "Local\\DirectPipeTestXProc";
static constexpr uint32_t kCapacity = 8192;
static constexpr uint32_t kChannels = 2;
static constexpr uint32_t kSampleRate = 48000;
static constexpr uint32_t kFramesPerBlock = 128;
static constexpr int kExpectedBlocks = 50;

int main()
{
    using namespace directpipe;

    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory shm;
    if (!shm.open(kShmName, shmSize)) {
        fprintf(stderr, "[child] Failed to open shared memory '%s'\n", kShmName);
        return 1;
    }

    RingBuffer consumer;
    if (!consumer.attachAsConsumer(shm.getData())) {
        fprintf(stderr, "[child] Failed to attach ring buffer\n");
        return 2;
    }

    auto* header = static_cast<DirectPipeHeader*>(shm.getData());
    if (!header->producer_active.load(std::memory_order_acquire)) {
        fprintf(stderr, "[child] Producer not active\n");
        return 3;
    }

    // Read and verify blocks
    // Parent wrote blocks where block[i] has all samples = float(i + 1)
    std::vector<float> readBuf(kFramesPerBlock * kChannels);
    int blocksRead = 0;
    int retries = 0;
    constexpr int kMaxRetries = 200;

    while (blocksRead < kExpectedBlocks && retries < kMaxRetries) {
        uint32_t read = consumer.read(readBuf.data(), kFramesPerBlock);
        if (read == 0) {
            ++retries;
#ifdef _WIN32
            Sleep(5);
#else
            usleep(5000);
#endif
            continue;
        }
        retries = 0;

        // Verify: all samples in block should equal float(blocksRead + 1)
        float expected = static_cast<float>(blocksRead + 1);
        for (uint32_t s = 0; s < read * kChannels; ++s) {
            if (readBuf[s] != expected) {
                fprintf(stderr, "[child] Data mismatch at block %d sample %u: got %f expected %f\n",
                        blocksRead, s, static_cast<double>(readBuf[s]), static_cast<double>(expected));
                return 4;
            }
        }
        ++blocksRead;
    }

    if (blocksRead != kExpectedBlocks) {
        fprintf(stderr, "[child] Only read %d of %d blocks\n", blocksRead, kExpectedBlocks);
        return 5;
    }

    consumer.detach();
    shm.close();

    fprintf(stdout, "[child] Cross-process IPC verified: %d blocks OK\n", blocksRead);
    return 0;
}
