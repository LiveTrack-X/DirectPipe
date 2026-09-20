// SPDX-License-Identifier: GPL-3.0-or-later
// Cross-process fixture only. Parent passes its unique test mapping; "idle" holds
// a live claim until the parent terminates this exact process to test dead-owner
// reclaim. Normal completion destroys the consumer before closing its mapping.
#include "directpipe/FanOut.h"
#include "directpipe/SharedMemory.h"
#include <array>
#include <string>

int main(int argc, char** argv) {
    using namespace directpipe;
    if (argc != 3) return 1;
    const std::string name = argv[1];
    // Test helper must never open either production mapping.
    if (name.find("DirectPipeFanOutTest_") == std::string::npos) return 2;
    SharedMemory memory;
    if (!memory.open(name, 0)) return 3;
    FanOutConsumer consumer;
    if (consumer.claim(memory.getData(), memory.getSize()) != FanOutAttachResult::Waiting) return 4;
    if (consumer.isReady()) return 5;
    NamedEvent proceed;
    if (!proceed.open(name + "_go")) return 6;
#ifdef _WIN32
    // NamedEvent::open intentionally grants SYNCHRONIZE only (production
    // consumers wait). This test child signals back to its parent and therefore
    // needs an explicit EVENT_MODIFY_STATE handle for the test-only ready event.
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, (name + "_ready").c_str());
    if (!ready) return 11;
    const BOOL signaled = SetEvent(ready);
    CloseHandle(ready);
    if (!signaled) return 12;
#else
    NamedEvent ready;
    if (!ready.open(name + "_ready")) return 6;
    ready.signal();
#endif
    if (!proceed.wait(10000)) return 7;
    if (std::string(argv[2]) == "idle") return 0;
    if (!consumer.isReady() || consumer.getSampleRate() != 48000 || consumer.getChannels() != 2) return 8;
    std::array<float, 512> audio{};
    if (consumer.read(audio.data(), 256) != 256) return 9;
    for (size_t i = 0; i < audio.size(); ++i)
        if (audio[i] != static_cast<float>(i + 1)) return 10;
    return 0;
}
