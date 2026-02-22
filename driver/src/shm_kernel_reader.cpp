/**
 * @file shm_kernel_reader.cpp
 * @brief Kernel-mode shared memory reader for the Virtual Loop Mic driver.
 *
 * Wraps the Windows kernel APIs (ZwOpenSection, ZwMapViewOfSection) to
 * access the DirectPipe shared memory region created by the user-mode
 * host application.
 *
 * Reads from the SPSC lock-free ring buffer using the same atomic
 * protocol as the user-mode consumer (OBS plugin). The shared memory
 * layout matches directpipe::DirectPipeHeader from Protocol.h.
 *
 * Kernel-mode shared memory access considerations:
 *   - The section object name is "\\BaseNamedObjects\\DirectPipeAudio"
 *     (kernel object namespace, equivalent to "Local\\DirectPipeAudio"
 *     in user-mode).
 *   - ZwOpenSection requires a valid OBJECT_ATTRIBUTES with the section
 *     name in the NT namespace.
 *   - ZwMapViewOfSection maps the section into the system process address
 *     space so it's accessible at any IRQL from kernel mode.
 *   - Atomic operations on the ring buffer header must use InterlockedXxx
 *     intrinsics (kernel equivalent of std::atomic with acquire/release).
 *
 * Build: Requires WDK, ntddk.h
 */

extern "C" {
#include <ntddk.h>
}

#include <ntstrsafe.h>

// ---------------------------------------------------------------------------
// Pool tag for this module
// ---------------------------------------------------------------------------
#define SHM_READER_POOLTAG  'RSVD'  // "DVSR" = DirectPipe Virtual SHM Reader

// ---------------------------------------------------------------------------
// Shared memory protocol constants (must match Protocol.h)
// ---------------------------------------------------------------------------

/// Protocol version — must match directpipe::PROTOCOL_VERSION
#define DIRECTPIPE_PROTOCOL_VERSION   1

/// Section object name in the kernel namespace.
/// User-mode "Local\\DirectPipeAudio" maps to this in the kernel:
#define DIRECTPIPE_SECTION_NAME   L"\\BaseNamedObjects\\DirectPipeAudio"

// ---------------------------------------------------------------------------
// Kernel-mode mirror of DirectPipeHeader (Protocol.h)
// ---------------------------------------------------------------------------

/**
 * @brief Kernel-mode equivalent of directpipe::DirectPipeHeader.
 *
 * This structure mirrors the user-mode shared memory header exactly.
 * The layout must be byte-compatible with the C++ definition in
 * core/include/directpipe/Protocol.h.
 *
 * In kernel mode we use volatile LONG64 + InterlockedXxx instead of
 * std::atomic<uint64_t>, and volatile LONG for the boolean field.
 *
 * IMPORTANT: alignas(64) in user mode translates to
 * __declspec(align(64)) in kernel mode. The offsets must match.
 */
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier

typedef struct _DIRECTPIPE_HEADER {
    /// Write position in frames (producer increments)
    __declspec(align(64)) volatile LONG64 write_pos;

    /// Read position in frames (consumer increments)
    __declspec(align(64)) volatile LONG64 read_pos;

    /// Audio sample rate (e.g., 48000)
    ULONG sample_rate;

    /// Number of audio channels (1 = mono, 2 = stereo)
    ULONG channels;

    /// Ring buffer capacity in frames (must be power of 2)
    ULONG buffer_frames;

    /// Protocol version for compatibility checking
    ULONG version;

    /// Whether the producer (JUCE host) is actively writing
    volatile LONG producer_active;

    /// Reserved padding (matches user-mode struct layout)
    UCHAR reserved[64 - sizeof(LONG) - 4 * sizeof(ULONG)];
} DIRECTPIPE_HEADER, *PDIRECTPIPE_HEADER;

#pragma warning(pop)

// Compile-time verification of header alignment expectations
// The user-mode header has alignas(64) on write_pos and read_pos,
// so the minimum alignment of the struct is 64 bytes.
C_ASSERT(__alignof(DIRECTPIPE_HEADER) >= 64);

// ---------------------------------------------------------------------------
// KERNEL_SHM_READER structure
// ---------------------------------------------------------------------------

/**
 * @brief Internal state for the kernel shared memory reader.
 *
 * Holds all handles and pointers needed to access the shared memory
 * section and read from the ring buffer.
 */
typedef struct _KERNEL_SHM_READER {
    /// Handle to the section object (from ZwOpenSection)
    HANDLE          sectionHandle;

    /// Mapped base address of the shared memory region
    PVOID           mappedBase;

    /// Size of the mapped view
    SIZE_T          viewSize;

    /// Pointer to the header at the start of the mapped region
    PDIRECTPIPE_HEADER header;

    /// Pointer to the PCM audio data (immediately after the header)
    float*          audioData;

    /// Cached capacity mask (buffer_frames - 1, for power-of-2 modulo)
    ULONG           capacityMask;

    /// Whether this reader is successfully connected
    BOOLEAN         connected;
} KERNEL_SHM_READER, *PKERNEL_SHM_READER;

// ---------------------------------------------------------------------------
// KernelShmReaderOpen
// ---------------------------------------------------------------------------

/**
 * @brief Open the DirectPipe shared memory section from kernel mode.
 *
 * Opens the named section "\\BaseNamedObjects\\DirectPipeAudio" created
 * by the user-mode host application, maps it into the system process
 * address space, validates the protocol version, and prepares for reading.
 *
 * @param OutReader  Receives the reader handle on success. Caller must
 *                   eventually call KernelShmReaderClose().
 * @return STATUS_SUCCESS on success.
 *         STATUS_OBJECT_NAME_NOT_FOUND if the section doesn't exist
 *         (host app not running).
 *         STATUS_REVISION_MISMATCH if protocol version doesn't match.
 */
extern "C"
NTSTATUS
KernelShmReaderOpen(_Out_ PKERNEL_SHM_READER* OutReader)
{
    NTSTATUS status;

    *OutReader = nullptr;

    // -----------------------------------------------------------------
    // Allocate the reader context structure
    // -----------------------------------------------------------------
    PKERNEL_SHM_READER reader = static_cast<PKERNEL_SHM_READER>(
        ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(KERNEL_SHM_READER),
            SHM_READER_POOLTAG
        )
    );

    if (!reader)
    {
        DbgPrint("VirtualLoopMic: ShmReader: Failed to allocate reader context\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(reader, sizeof(KERNEL_SHM_READER));

    // -----------------------------------------------------------------
    // Step 1: Open the named section object.
    //
    // The section was created by the host app using CreateFileMapping()
    // with the name "Local\\DirectPipeAudio". In the kernel namespace,
    // this corresponds to "\\BaseNamedObjects\\DirectPipeAudio" (for
    // session 0) or "\\Sessions\\<id>\\BaseNamedObjects\\DirectPipeAudio"
    // for other sessions.
    //
    // TODO: Handle session-aware naming. For now, we try the global
    // BaseNamedObjects path which works for services and console session 0.
    // For interactive user sessions, the path needs to include the
    // session ID. Consider using ZwOpenSession or building the path
    // dynamically based on the session the audio device is in.
    // -----------------------------------------------------------------
    UNICODE_STRING sectionName;
    RtlInitUnicodeString(&sectionName, DIRECTPIPE_SECTION_NAME);

    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(
        &objAttr,
        &sectionName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        nullptr,    // RootDirectory
        nullptr     // SecurityDescriptor
    );

    status = ZwOpenSection(
        &reader->sectionHandle,
        SECTION_MAP_READ,       // We only need read access
        &objAttr
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: ShmReader: ZwOpenSection failed 0x%08X "
                 "(host app not running?)\n", status);
        ExFreePoolWithTag(reader, SHM_READER_POOLTAG);
        return status;
    }

    // -----------------------------------------------------------------
    // Step 2: Map the section into kernel address space.
    //
    // We map with ViewShare = ViewUnmap so the mapping is in the system
    // process address space and accessible from any thread context.
    // -----------------------------------------------------------------
    reader->viewSize = 0;  // Map entire section
    reader->mappedBase = nullptr;

    status = ZwMapViewOfSection(
        reader->sectionHandle,
        ZwCurrentProcess(),         // Map into current (system) process
        &reader->mappedBase,
        0,                          // ZeroBits (no address preference)
        0,                          // CommitSize (map entire view)
        nullptr,                    // SectionOffset (from beginning)
        &reader->viewSize,
        ViewUnmap,                  // Unmappable view
        0,                          // AllocationType
        PAGE_READONLY               // Read-only access
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: ShmReader: ZwMapViewOfSection failed 0x%08X\n", status);
        ZwClose(reader->sectionHandle);
        ExFreePoolWithTag(reader, SHM_READER_POOLTAG);
        return status;
    }

    // -----------------------------------------------------------------
    // Step 3: Validate the shared memory layout.
    //
    // The mapped region must be at least sizeof(DIRECTPIPE_HEADER) and
    // the protocol version must match.
    // -----------------------------------------------------------------
    if (reader->viewSize < sizeof(DIRECTPIPE_HEADER))
    {
        DbgPrint("VirtualLoopMic: ShmReader: Mapped view too small (%llu bytes)\n",
                 (ULONGLONG)reader->viewSize);
        ZwUnmapViewOfSection(ZwCurrentProcess(), reader->mappedBase);
        ZwClose(reader->sectionHandle);
        ExFreePoolWithTag(reader, SHM_READER_POOLTAG);
        return STATUS_BUFFER_TOO_SMALL;
    }

    reader->header = static_cast<PDIRECTPIPE_HEADER>(reader->mappedBase);

    // Check protocol version
    if (reader->header->version != DIRECTPIPE_PROTOCOL_VERSION)
    {
        DbgPrint("VirtualLoopMic: ShmReader: Protocol version mismatch "
                 "(expected %u, got %u)\n",
                 DIRECTPIPE_PROTOCOL_VERSION, reader->header->version);
        ZwUnmapViewOfSection(ZwCurrentProcess(), reader->mappedBase);
        ZwClose(reader->sectionHandle);
        ExFreePoolWithTag(reader, SHM_READER_POOLTAG);
        return STATUS_REVISION_MISMATCH;
    }

    // Validate buffer_frames is a power of 2
    ULONG bufFrames = reader->header->buffer_frames;
    if (bufFrames == 0 || (bufFrames & (bufFrames - 1)) != 0)
    {
        DbgPrint("VirtualLoopMic: ShmReader: buffer_frames (%lu) is not power of 2\n",
                 bufFrames);
        ZwUnmapViewOfSection(ZwCurrentProcess(), reader->mappedBase);
        ZwClose(reader->sectionHandle);
        ExFreePoolWithTag(reader, SHM_READER_POOLTAG);
        return STATUS_INVALID_PARAMETER;
    }

    // Validate total size
    ULONG channels = reader->header->channels;
    SIZE_T expectedDataSize = (SIZE_T)bufFrames * channels * sizeof(float);
    SIZE_T expectedTotalSize = sizeof(DIRECTPIPE_HEADER) + expectedDataSize;

    if (reader->viewSize < expectedTotalSize)
    {
        DbgPrint("VirtualLoopMic: ShmReader: View too small for declared buffer "
                 "(%llu < %llu)\n",
                 (ULONGLONG)reader->viewSize, (ULONGLONG)expectedTotalSize);
        ZwUnmapViewOfSection(ZwCurrentProcess(), reader->mappedBase);
        ZwClose(reader->sectionHandle);
        ExFreePoolWithTag(reader, SHM_READER_POOLTAG);
        return STATUS_BUFFER_TOO_SMALL;
    }

    // -----------------------------------------------------------------
    // Step 4: Set up the audio data pointer and capacity mask.
    // -----------------------------------------------------------------
    reader->audioData = reinterpret_cast<float*>(
        static_cast<PUCHAR>(reader->mappedBase) + sizeof(DIRECTPIPE_HEADER)
    );

    reader->capacityMask = bufFrames - 1;
    reader->connected = TRUE;

    DbgPrint("VirtualLoopMic: ShmReader: Connected successfully. "
             "Rate=%lu, Ch=%lu, BufferFrames=%lu\n",
             reader->header->sample_rate,
             reader->header->channels,
             reader->header->buffer_frames);

    *OutReader = reader;
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// KernelShmReaderClose
// ---------------------------------------------------------------------------

/**
 * @brief Close the shared memory reader and release all kernel resources.
 *
 * Unmaps the section view, closes the section handle, and frees the
 * reader context. Safe to call with a NULL reader.
 *
 * @param Reader  The reader handle to close (may be NULL).
 */
extern "C"
VOID
KernelShmReaderClose(_In_opt_ PKERNEL_SHM_READER Reader)
{
    if (!Reader)
    {
        return;
    }

    DbgPrint("VirtualLoopMic: ShmReader: Closing\n");

    Reader->connected = FALSE;

    // Unmap the section view
    if (Reader->mappedBase)
    {
        ZwUnmapViewOfSection(ZwCurrentProcess(), Reader->mappedBase);
        Reader->mappedBase = nullptr;
        Reader->header = nullptr;
        Reader->audioData = nullptr;
    }

    // Close the section handle
    if (Reader->sectionHandle)
    {
        ZwClose(Reader->sectionHandle);
        Reader->sectionHandle = nullptr;
    }

    // Free the reader context
    ExFreePoolWithTag(Reader, SHM_READER_POOLTAG);
}

// ---------------------------------------------------------------------------
// KernelShmReaderRead
// ---------------------------------------------------------------------------

/**
 * @brief Read frames from the SPSC ring buffer in shared memory.
 *
 * Implements the consumer side of the SPSC ring buffer protocol:
 *   1. Read write_pos with acquire semantics (see what the producer wrote)
 *   2. Calculate available frames: write_pos - read_pos
 *   3. Copy the audio data from the ring buffer
 *   4. Update read_pos with release semantics (tell producer we consumed)
 *
 * The ring buffer uses power-of-2 modulo arithmetic via capacityMask.
 * Positions are monotonically increasing 64-bit values; the actual
 * buffer index is (position & mask).
 *
 * This function is safe to call at DISPATCH_LEVEL (from the timer DPC).
 * It does not allocate memory or call any paged functions.
 *
 * @param Reader     The reader handle.
 * @param Buffer     Output buffer for float32 interleaved PCM.
 * @param MaxFrames  Maximum number of frames to read.
 * @return Number of frames actually read (0 if no data available).
 */
extern "C"
ULONG
KernelShmReaderRead(
    _In_  PKERNEL_SHM_READER Reader,
    _Out_ float*             Buffer,
    _In_  ULONG              MaxFrames
)
{
    if (!Reader || !Reader->connected || !Reader->header || !Reader->audioData)
    {
        return 0;
    }

    PDIRECTPIPE_HEADER hdr = Reader->header;
    ULONG channels = hdr->channels;
    ULONG mask = Reader->capacityMask;

    // -----------------------------------------------------------------
    // Step 1: Read positions with acquire/release semantics.
    //
    // In kernel mode, we use InterlockedCompareExchange64 with value=0
    // as a read-with-acquire, or simply use a volatile read followed by
    // a KeMemoryBarrier(). The C volatile qualifier provides compiler
    // barrier; KeMemoryBarrier() provides a hardware fence.
    //
    // For x86/x64, aligned 64-bit reads are atomic on 64-bit platforms.
    // -----------------------------------------------------------------
    LONG64 writePos = InterlockedCompareExchange64(
        const_cast<LONG64*>(&hdr->write_pos), 0, 0
    );
    // Acquire barrier: ensures we see the data the producer wrote
    // before updating write_pos
    KeMemoryBarrier();

    LONG64 readPos = hdr->read_pos;  // We own read_pos

    // -----------------------------------------------------------------
    // Step 2: Calculate available frames.
    // -----------------------------------------------------------------
    LONG64 available64 = writePos - readPos;
    if (available64 <= 0)
    {
        return 0;  // No data available
    }

    ULONG available = static_cast<ULONG>(available64);
    ULONG framesToRead = min(available, MaxFrames);

    if (framesToRead == 0)
    {
        return 0;
    }

    // -----------------------------------------------------------------
    // Step 3: Copy audio data from the ring buffer.
    //
    // The ring buffer is indexed by (position & mask) where mask =
    // (buffer_frames - 1). Since buffer_frames is a power of 2,
    // this gives correct cyclic indexing.
    //
    // We may need to split the copy across the wrap point.
    // -----------------------------------------------------------------
    ULONG startIndex = static_cast<ULONG>(readPos) & mask;
    ULONG samplesToRead = framesToRead * channels;
    ULONG startSample = startIndex * channels;
    ULONG totalSamples = (mask + 1) * channels;  // Total samples in buffer

    // First chunk: from startSample to end of buffer (or less)
    ULONG firstChunkSamples = min(samplesToRead, totalSamples - startSample);
    ULONG secondChunkSamples = samplesToRead - firstChunkSamples;

    // Copy first chunk
    RtlCopyMemory(
        Buffer,
        &Reader->audioData[startSample],
        firstChunkSamples * sizeof(float)
    );

    // Copy second chunk (after wrap)
    if (secondChunkSamples > 0)
    {
        RtlCopyMemory(
            &Buffer[firstChunkSamples],
            &Reader->audioData[0],
            secondChunkSamples * sizeof(float)
        );
    }

    // -----------------------------------------------------------------
    // Step 4: Update read_pos with release semantics.
    //
    // This tells the producer that we've consumed these frames and
    // the buffer space can be reused.
    // -----------------------------------------------------------------
    KeMemoryBarrier();  // Release barrier: ensure copies are visible before advancing
    InterlockedExchange64(
        const_cast<LONG64*>(&hdr->read_pos),
        readPos + framesToRead
    );

    return framesToRead;
}

// ---------------------------------------------------------------------------
// KernelShmReaderGetSampleRate
// ---------------------------------------------------------------------------

/**
 * @brief Get the sample rate from the shared memory header.
 *
 * @param Reader  The reader handle.
 * @return Sample rate in Hz, or 0 if not connected.
 */
extern "C"
ULONG
KernelShmReaderGetSampleRate(_In_ PKERNEL_SHM_READER Reader)
{
    if (!Reader || !Reader->connected || !Reader->header)
    {
        return 0;
    }
    return Reader->header->sample_rate;
}

// ---------------------------------------------------------------------------
// KernelShmReaderGetChannels
// ---------------------------------------------------------------------------

/**
 * @brief Get the channel count from the shared memory header.
 *
 * @param Reader  The reader handle.
 * @return Number of channels, or 0 if not connected.
 */
extern "C"
ULONG
KernelShmReaderGetChannels(_In_ PKERNEL_SHM_READER Reader)
{
    if (!Reader || !Reader->connected || !Reader->header)
    {
        return 0;
    }
    return Reader->header->channels;
}

// ---------------------------------------------------------------------------
// KernelShmReaderIsConnected
// ---------------------------------------------------------------------------

/**
 * @brief Check if the reader is connected and the producer is active.
 *
 * @param Reader  The reader handle (may be NULL).
 * @return TRUE if connected and producer is writing, FALSE otherwise.
 */
extern "C"
BOOLEAN
KernelShmReaderIsConnected(_In_opt_ PKERNEL_SHM_READER Reader)
{
    if (!Reader || !Reader->connected || !Reader->header)
    {
        return FALSE;
    }

    // Check if the producer is still active
    LONG active = InterlockedCompareExchange(
        const_cast<LONG*>(&Reader->header->producer_active),
        0, 0
    );

    return (active != 0) ? TRUE : FALSE;
}
