# DirectPipe Architecture

## System Overview

```
USB Mic → WASAPI Exclusive (~2.7ms) → VST Chain (0ms added)
  ├──→ [OBS Plugin] Shared Memory direct (3~6ms total latency)
  └──→ [Virtual Mic] Bundled driver (8~12ms, for Discord/Zoom)
```

## Components

### 1. Core Library (`core/`)
- **RingBuffer**: SPSC lock-free ring buffer using `std::atomic` with
  acquire/release semantics. Cache-line aligned to prevent false sharing.
- **SharedMemory**: Windows `CreateFileMapping`/`MapViewOfFile` wrapper
  with POSIX `shm_open`/`mmap` fallback for development.
- **NamedEvent**: Windows Named Event for data-ready signaling.

### 2. Host Application (`host/`)
- **AudioEngine**: WASAPI Exclusive mode input, manages the audio callback.
- **VSTChain**: `AudioProcessorGraph`-based VST2/VST3 plugin chain.
- **OutputRouter**: Distributes processed audio to 3 simultaneous outputs.
- **SharedMemWriter**: Producer-side IPC, writes to ring buffer.
- **GUI**: JUCE-based UI with device selector, plugin chain editor, meters.

### 3. OBS Plugin (`obs-plugin/`)
- **directpipe-source**: OBS audio source that reads from shared memory.
- **shm-reader**: Consumer-side C API for the shared ring buffer.

## Data Flow (Real-time Audio Thread)

1. WASAPI Exclusive callback fires (128 samples @ 48kHz = 2.67ms period)
2. Copy input PCM float32 to working buffer
3. Process through VST chain (inline `processBlock` calls)
4. Route to outputs:
   - a. Write to shared memory ring buffer → signal Named Event
   - b. WASAPI output → virtual microphone driver
   - c. Copy to callback output → local monitoring headphones

## IPC Protocol

### Shared Memory Layout
```
[DirectPipeHeader - 64-byte aligned]
  write_pos:  atomic<uint64_t>  (cache line 1)
  read_pos:   atomic<uint64_t>  (cache line 2)
  sample_rate, channels, buffer_frames, version, active
[Ring Buffer Data]
  float32 PCM, interleaved
  4096 frames × 2ch × 4 bytes = 32KB
```

### Synchronization
- Named Event `DirectPipeDataReady` signals new data
- Producer: write → `SetEvent()`
- Consumer: `WaitForSingleObject()` → read
- Timeout 500ms → connection lost detection
