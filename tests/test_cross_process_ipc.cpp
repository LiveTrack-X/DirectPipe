/**
 * @file test_cross_process_ipc.cpp
 * @brief Cross-process IPC integration test
 *
 * Validates that shared memory and ring buffer work correctly across
 * process boundaries — the actual deployment scenario where DirectPipe
 * host (producer) communicates with the Receiver VST in OBS (consumer).
 *
 * This test spawns ipc-test-child as a separate process and verifies
 * that audio data written by the parent is correctly received by the child.
 */

#include <gtest/gtest.h>
#include "directpipe/SharedMemory.h"
#include "directpipe/RingBuffer.h"
#include "directpipe/Constants.h"
#include "directpipe/Protocol.h"

#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace directpipe;

// Must match values in ipc_test_child.cpp
static constexpr const char* kShmName = "Local\\DirectPipeTestXProc";
static constexpr uint32_t kCapacity = 8192;
static constexpr uint32_t kChannels = 2;
static constexpr uint32_t kSampleRate = 48000;
static constexpr uint32_t kFramesPerBlock = 128;
static constexpr int kBlocks = 50;  // 50 * 128 = 6400 frames < 8192 capacity

#ifdef _WIN32
/// Find ipc-test-child.exe in the same directory as the running test executable
static std::string getChildExePath()
{
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);
    return path + "ipc-test-child.exe";
}
#endif

TEST(CrossProcessIPC, ProducerWriteChildConsumerRead) {
#ifndef _WIN32
    GTEST_SKIP() << "Cross-process IPC test currently Windows-only";
#else
    // 1. Create shared memory and initialize ring buffer
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory shm;
    ASSERT_TRUE(shm.create(kShmName, shmSize))
        << "Failed to create shared memory '" << kShmName << "'";

    RingBuffer producer;
    producer.initAsProducer(shm.getData(), kCapacity, kChannels, kSampleRate);

    // 2. Write all test blocks before spawning child
    //    Block[i] has all samples = float(i + 1)
    std::vector<float> writeBuf(kFramesPerBlock * kChannels);
    for (int i = 0; i < kBlocks; ++i) {
        std::fill(writeBuf.begin(), writeBuf.end(), static_cast<float>(i + 1));
        uint32_t written = producer.write(writeBuf.data(), kFramesPerBlock);
        ASSERT_EQ(written, kFramesPerBlock) << "Write failed at block " << i;
    }

    EXPECT_EQ(producer.availableRead(), kFramesPerBlock * kBlocks);

    // 3. Spawn child process
    std::string childPath = getChildExePath();

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // CreateProcessA needs a mutable string for lpCommandLine
    std::string cmdLine = "\"" + childPath + "\"";
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(
        nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
        0, nullptr, nullptr, &si, &pi);

    if (!created) {
        DWORD err = GetLastError();
        // Child exe might not be built — skip instead of fail
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            shm.close();
            GTEST_SKIP() << "ipc-test-child.exe not found at: " << childPath
                         << " (build it first)";
        }
        FAIL() << "CreateProcess failed with error " << err;
    }

    // 4. Wait for child (10 second timeout)
    DWORD result = WaitForSingleObject(pi.hProcess, 10000);
    ASSERT_EQ(result, WAIT_OBJECT_0)
        << "Child process timed out (possible deadlock or missing child exe)";

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    EXPECT_EQ(exitCode, 0u) << "Child exit code " << exitCode
        << ": 1=SHM open fail, 2=attach fail, 3=inactive, 4=data mismatch, 5=incomplete read";

    // 5. Cleanup
    producer.detach();
    shm.close();
#endif
}

TEST(CrossProcessIPC, ChildSeesHeaderFields) {
#ifndef _WIN32
    GTEST_SKIP() << "Cross-process IPC test currently Windows-only";
#else
    // Verify that protocol header fields (version, SR, channels, capacity)
    // are visible in the child process by checking them in the parent first.
    // The child implicitly verifies this via attachAsConsumer's validation.

    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory shm;
    ASSERT_TRUE(shm.create(kShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(shm.getData(), kCapacity, kChannels, kSampleRate);

    // Verify header is correctly populated
    auto* header = static_cast<DirectPipeHeader*>(shm.getData());
    EXPECT_EQ(header->version, PROTOCOL_VERSION);
    EXPECT_EQ(header->sample_rate, kSampleRate);
    EXPECT_EQ(header->channels, kChannels);
    EXPECT_EQ(header->buffer_frames, kCapacity);
    EXPECT_TRUE(header->producer_active.load(std::memory_order_acquire));

    // Open from "consumer" perspective (same process, different mapping)
    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kShmName, shmSize));

    auto* consumerHeader = static_cast<DirectPipeHeader*>(consumerShm.getData());
    EXPECT_EQ(consumerHeader->version, PROTOCOL_VERSION);
    EXPECT_EQ(consumerHeader->sample_rate, kSampleRate);
    EXPECT_EQ(consumerHeader->channels, kChannels);
    EXPECT_EQ(consumerHeader->buffer_frames, kCapacity);

    consumerShm.close();
    shm.close();
#endif
}
