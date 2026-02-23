/**
 * @file stream.cpp
 * @brief WaveRT stream implementation for the Virtual Loop Mic driver.
 *
 * Implements IMiniportWaveRTStream for the capture stream. This stream
 * opens the DirectPipe shared memory section from kernel mode, reads
 * PCM data from the SPSC ring buffer, and fills the WaveRT DMA buffer
 * that the Windows audio engine reads from.
 *
 * Key responsibilities:
 *   - Allocate the WaveRT cyclic buffer (DMA buffer)
 *   - Run a timer-driven DPC to periodically copy audio from shared
 *     memory into the WaveRT buffer
 *   - Handle stream state transitions (STOP, ACQUIRE, PAUSE, RUN)
 *   - Fill with silence when no data is available
 *
 * Based on the Microsoft Sysvad sample's WaveRT stream pattern.
 *
 * Build: Requires WDK, portcls.h, ks.h
 */

extern "C" {
#include <ntddk.h>
}

#include <portcls.h>
#include <stdunk.h>
#include <ksdebug.h>
#include <ks.h>
#include <ksmedia.h>
#include <ntstrsafe.h>

// ---------------------------------------------------------------------------
// Forward declaration: Kernel-mode shared memory reader (shm_kernel_reader.cpp)
// ---------------------------------------------------------------------------

/**
 * @brief Opaque handle to the kernel shared memory reader.
 */
typedef struct _KERNEL_SHM_READER KERNEL_SHM_READER, *PKERNEL_SHM_READER;

/**
 * @brief Open the DirectPipe shared memory section from kernel mode.
 *
 * @param OutReader  Receives the reader handle on success.
 * @return STATUS_SUCCESS on success, or an error code.
 */
extern "C" NTSTATUS KernelShmReaderOpen(_Out_ PKERNEL_SHM_READER* OutReader);

/**
 * @brief Close the shared memory reader and release all resources.
 *
 * @param Reader  The reader handle to close.
 */
extern "C" VOID KernelShmReaderClose(_In_opt_ PKERNEL_SHM_READER Reader);

/**
 * @brief Read frames from the shared memory ring buffer.
 *
 * Reads up to MaxFrames of float32 interleaved PCM from the ring buffer.
 * Returns 0 if no data is available.
 *
 * @param Reader     The reader handle.
 * @param Buffer     Output buffer for float32 interleaved PCM.
 * @param MaxFrames  Maximum number of frames to read.
 * @return Number of frames actually read.
 */
extern "C" ULONG KernelShmReaderRead(
    _In_  PKERNEL_SHM_READER Reader,
    _Out_ float*             Buffer,
    _In_  ULONG              MaxFrames
);

/**
 * @brief Get the sample rate from the shared memory header.
 */
extern "C" ULONG KernelShmReaderGetSampleRate(_In_ PKERNEL_SHM_READER Reader);

/**
 * @brief Get the channel count from the shared memory header.
 */
extern "C" ULONG KernelShmReaderGetChannels(_In_ PKERNEL_SHM_READER Reader);

/**
 * @brief Check if the reader is connected and the shared memory is valid.
 */
extern "C" BOOLEAN KernelShmReaderIsConnected(_In_opt_ PKERNEL_SHM_READER Reader);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Timer interval for the DPC that copies audio data (in 100ns units).
/// 5ms = 50000 * 100ns. This is the polling interval for the timer DPC.
/// At 48kHz, 5ms = 240 frames — well within typical buffer sizes.
#define TIMER_INTERVAL_100NS    (-50000LL)  // Relative time: 5ms

/// Pool allocation tag for this module
#define STREAM_POOLTAG          'SLVD'  // "DVLS" = DirectPipe Virtual Loop Stream

/// Maximum temporary read buffer size (frames)
/// Should accommodate one timer period's worth of frames at max sample rate
/// 48000 Hz * 10ms = 480 frames; use 1024 for headroom
#define MAX_TEMP_FRAMES         1024

// ===========================================================================
// CVirtualLoopStream — IMiniportWaveRTStream implementation
// ===========================================================================

/**
 * @class CVirtualLoopStream
 * @brief WaveRT capture stream for the Virtual Loop Mic.
 *
 * Lifecycle:
 *   1. Created by miniport NewStream() when a client opens the capture pin.
 *   2. AllocateBufferWithNotification() allocates the WaveRT cyclic buffer.
 *   3. SetState(KSSTATE_RUN) starts the timer DPC that fills the buffer.
 *   4. Timer DPC fires periodically, reads from shared memory, and writes
 *      into the WaveRT buffer at the current write position.
 *   5. SetState(KSSTATE_STOP) stops the timer. The stream is released.
 */
class CVirtualLoopStream : public IMiniportWaveRTStreamNotification,
                            public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CVirtualLoopStream);

    ~CVirtualLoopStream();

    NTSTATUS Init(
        _In_ IPortWaveRTStream* PortStream,
        _In_ ULONG              Pin,
        _In_ BOOLEAN            Capture,
        _In_ PKSDATAFORMAT      DataFormat
    );

    // ---- IMiniportWaveRTStream (base interface) ----

    STDMETHODIMP_(NTSTATUS) SetFormat(
        _In_ PKSDATAFORMAT DataFormat
    );

    STDMETHODIMP_(NTSTATUS) SetState(
        _In_ KSSTATE State
    );

    STDMETHODIMP_(NTSTATUS) GetPosition(
        _Out_ KSAUDIO_POSITION* Position
    );

    STDMETHODIMP_(NTSTATUS) AllocateAudioBuffer(
        _In_  ULONG              RequestedSize,
        _Out_ PMDL*              AudioBufferMdl,
        _Out_ ULONG*             ActualSize,
        _Out_ ULONG*             OffsetFromFirstPage,
        _Out_ MEMORY_CACHING_TYPE* CacheType
    );

    STDMETHODIMP_(VOID) FreeAudioBuffer(
        _In_opt_ PMDL AudioBufferMdl,
        _In_ ULONG BufferSize
    );

    STDMETHODIMP_(VOID) GetHWLatency(
        _Out_ KSRTAUDIO_HWLATENCY* hwLatency
    );

    STDMETHODIMP_(NTSTATUS) GetPositionRegister(
        _Out_ PKSRTAUDIO_HWREGISTER Register
    );

    STDMETHODIMP_(NTSTATUS) GetClockRegister(
        _Out_ PKSRTAUDIO_HWREGISTER Register
    );

    // ---- IMiniportWaveRTStreamNotification ----

    STDMETHODIMP_(NTSTATUS) AllocateBufferWithNotification(
        _In_  ULONG              NotificationCount,
        _In_  ULONG              RequestedSize,
        _Out_ PMDL*              AudioBufferMdl,
        _Out_ ULONG*             ActualSize,
        _Out_ ULONG*             OffsetFromFirstPage,
        _Out_ MEMORY_CACHING_TYPE* CacheType
    );

    STDMETHODIMP_(VOID) FreeBufferWithNotification(
        _In_ PMDL AudioBufferMdl,
        _In_ ULONG BufferSize
    );

    STDMETHODIMP_(NTSTATUS) RegisterNotificationEvent(
        _In_ PKEVENT NotificationEvent
    );

    STDMETHODIMP_(NTSTATUS) UnregisterNotificationEvent(
        _In_ PKEVENT NotificationEvent
    );

private:
    // -----------------------------------------------------------------
    // Timer DPC callback (static + instance method)
    // -----------------------------------------------------------------

    /**
     * @brief Timer DPC routine.
     *
     * Called at DISPATCH_LEVEL every TIMER_INTERVAL_100NS. Reads audio
     * from shared memory and copies it into the WaveRT buffer.
     */
    static KDEFERRED_ROUTINE TimerDpcRoutine;

    /**
     * @brief Instance method called from TimerDpcRoutine.
     *
     * Performs the actual data transfer:
     *   1. Read available frames from shared memory via KernelShmReader
     *   2. Convert format if needed (float32 -> int16, etc.)
     *   3. Copy into the WaveRT buffer at the current write position
     *   4. Advance the write position (wrapping around the cyclic buffer)
     *   5. If no data available, write silence
     *   6. Signal notification events
     */
    void OnTimerDpc();

    /**
     * @brief Convert float32 PCM samples to int16 PCM.
     *
     * Used when the client requests 16-bit PCM but the shared memory
     * contains float32 data.
     *
     * @param dst    Destination buffer (int16 samples).
     * @param src    Source buffer (float32 samples).
     * @param count  Number of samples (frames * channels).
     */
    static void ConvertFloat32ToInt16(
        _Out_ INT16*       dst,
        _In_  const float* src,
        _In_  ULONG        count
    );

    /**
     * @brief Convert float32 PCM samples to 24-bit PCM (packed 3 bytes).
     *
     * Used when the client requests 24-bit PCM but the shared memory
     * contains float32 data. Outputs little-endian packed 24-bit.
     *
     * @param dst    Destination buffer (3 bytes per sample).
     * @param src    Source buffer (float32 samples).
     * @param count  Number of samples (frames * channels).
     */
    static void ConvertFloat32ToInt24(
        _Out_ BYTE*        dst,
        _In_  const float* src,
        _In_  ULONG        count
    );

    // -----------------------------------------------------------------
    // Member variables
    // -----------------------------------------------------------------

    /// Port stream interface (for buffer allocation)
    IPortWaveRTStream*     portStream_       = nullptr;

    /// Shared memory reader
    PKERNEL_SHM_READER     shmReader_        = nullptr;

    /// Current stream state
    KSSTATE                state_            = KSSTATE_STOP;

    /// Audio format parameters (from the negotiated DataFormat)
    ULONG                  sampleRate_       = 48000;
    ULONG                  channels_         = 2;
    ULONG                  bitsPerSample_    = 32;
    BOOLEAN                isFloat_          = TRUE;
    ULONG                  bytesPerFrame_    = 8;   // channels * (bitsPerSample / 8)

    /// WaveRT cyclic buffer
    PBYTE                  dmaBuffer_        = nullptr;
    ULONG                  dmaBufferSize_    = 0;   // Total buffer size in bytes
    PMDL                   dmaBufferMdl_     = nullptr;

    /// Current write position in the DMA buffer (in bytes, wraps around)
    volatile ULONG         writePosition_    = 0;

    /// Number of bytes written since stream start (monotonically increasing)
    volatile ULONGLONG     bytesTransferred_ = 0;

    /// Timer and DPC for periodic audio transfer
    KTIMER                 timer_;
    KDPC                   timerDpc_;
    BOOLEAN                timerActive_      = FALSE;

    /// Notification event for buffer completion signaling
    /// (Used by WASAPI event-driven mode)
    PKEVENT                notificationEvent_ = nullptr;
    ULONG                  notificationCount_ = 0;

    /// Temporary buffer for reading float32 data from shared memory
    /// Allocated once during Init(), sized for MAX_TEMP_FRAMES.
    float*                 tempBuffer_       = nullptr;
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

#pragma code_seg("PAGE")

CVirtualLoopStream::~CVirtualLoopStream()
{
    PAGED_CODE();

    DbgPrint("VirtualLoopMic: Stream destructor\n");

    // Ensure timer is stopped
    if (timerActive_)
    {
        KeCancelTimer(&timer_);
        timerActive_ = FALSE;
    }

    // Close shared memory reader
    if (shmReader_)
    {
        KernelShmReaderClose(shmReader_);
        shmReader_ = nullptr;
    }

    // Free temporary buffer
    if (tempBuffer_)
    {
        ExFreePoolWithTag(tempBuffer_, STREAM_POOLTAG);
        tempBuffer_ = nullptr;
    }

    // Release port stream
    if (portStream_)
    {
        portStream_->Release();
        portStream_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

NTSTATUS
CVirtualLoopStream::Init(
    _In_ IPortWaveRTStream* PortStream,
    _In_ ULONG              Pin,
    _In_ BOOLEAN            Capture,
    _In_ PKSDATAFORMAT      DataFormat
)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Pin);
    UNREFERENCED_PARAMETER(Capture);

    DbgPrint("VirtualLoopMic: Stream::Init\n");

    // Store port stream reference
    portStream_ = PortStream;
    portStream_->AddRef();

    // -----------------------------------------------------------------
    // Parse the data format to extract audio parameters
    // -----------------------------------------------------------------
    PKSDATAFORMAT_WAVEFORMATEX waveFormat =
        reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX>(DataFormat);
    WAVEFORMATEX* wfx = &waveFormat->WaveFormatEx;

    sampleRate_    = wfx->nSamplesPerSec;
    channels_      = wfx->nChannels;
    bitsPerSample_ = wfx->wBitsPerSample;
    bytesPerFrame_ = channels_ * (bitsPerSample_ / 8);

    // Determine if the format is float or integer PCM
    isFloat_ = (IsEqualGUIDAligned(DataFormat->SubFormat,
                                    KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) ? TRUE : FALSE;

    DbgPrint("VirtualLoopMic: Stream format: %luHz, %lu ch, %lu bit, float=%d\n",
             sampleRate_, channels_, bitsPerSample_, isFloat_);

    // -----------------------------------------------------------------
    // Allocate temporary buffer for reading from shared memory.
    // Shared memory always contains float32; we may need to convert.
    // -----------------------------------------------------------------
    tempBuffer_ = static_cast<float*>(
        ExAllocatePoolWithTag(
            NonPagedPoolNx,
            MAX_TEMP_FRAMES * channels_ * sizeof(float),
            STREAM_POOLTAG
        )
    );

    if (!tempBuffer_)
    {
        DbgPrint("VirtualLoopMic: Failed to allocate temp buffer\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // -----------------------------------------------------------------
    // Open the DirectPipe shared memory section.
    //
    // This may fail if the host app hasn't started yet. That's OK;
    // we'll produce silence until the shared memory becomes available.
    // The reader can be retried in the timer DPC.
    // -----------------------------------------------------------------
    NTSTATUS status = KernelShmReaderOpen(&shmReader_);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: Shared memory not available (host not running?), "
                 "will produce silence. Status=0x%08X\n", status);
        shmReader_ = nullptr;
        // Not a fatal error — we'll retry or produce silence
    }

    // -----------------------------------------------------------------
    // Initialize the timer and DPC for periodic audio transfer.
    // The timer is not started until SetState(KSSTATE_RUN).
    // -----------------------------------------------------------------
    KeInitializeTimer(&timer_);
    KeInitializeDpc(&timerDpc_, TimerDpcRoutine, this);

    return STATUS_SUCCESS;
}

#pragma code_seg()

// ---------------------------------------------------------------------------
// AllocateBufferWithNotification
// ---------------------------------------------------------------------------

/**
 * @brief Allocate the WaveRT cyclic buffer.
 *
 * Called by PortCls to allocate the DMA buffer that the audio engine
 * will read from. For a virtual device, there's no real DMA — we
 * allocate a contiguous memory region and fill it via timer DPC.
 *
 * @param NotificationCount     Number of notifications per buffer cycle.
 * @param RequestedSize         Requested buffer size in bytes.
 * @param AudioBufferMdl        Receives MDL describing the buffer.
 * @param ActualSize            Receives actual allocated size.
 * @param OffsetFromFirstPage   Receives offset into the first page.
 * @param CacheType             Receives the caching type.
 * @return STATUS_SUCCESS on success.
 */
#pragma code_seg("PAGE")
STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::AllocateBufferWithNotification(
    _In_  ULONG              NotificationCount,
    _In_  ULONG              RequestedSize,
    _Out_ PMDL*              AudioBufferMdl,
    _Out_ ULONG*             ActualSize,
    _Out_ ULONG*             OffsetFromFirstPage,
    _Out_ MEMORY_CACHING_TYPE* CacheType
)
{
    PAGED_CODE();

    DbgPrint("VirtualLoopMic: AllocateBufferWithNotification "
             "(NotifCount=%lu, ReqSize=%lu)\n",
             NotificationCount, RequestedSize);

    notificationCount_ = NotificationCount;

    // -----------------------------------------------------------------
    // Allocate a contiguous, non-paged memory region for the WaveRT
    // cyclic buffer. The audio engine maps this into user mode.
    //
    // We use the port stream's AllocateContiguousPagesForMdl or
    // AllocatePagesForMdl to get a suitable buffer.
    // -----------------------------------------------------------------

    // Round up to page-aligned size
    ULONG allocSize = RequestedSize;
    if (allocSize == 0)
    {
        // Default: 10ms worth of audio
        allocSize = (sampleRate_ / 100) * bytesPerFrame_;
    }

    // Allocate pages for the buffer
    PHYSICAL_ADDRESS highAddr = { 0 };
    highAddr.QuadPart = (LONGLONG)0x7FFFFFFFFFFFFFFF;

    dmaBufferMdl_ = portStream_->AllocatePagesForMdl(
        highAddr,
        allocSize
    );

    if (!dmaBufferMdl_)
    {
        DbgPrint("VirtualLoopMic: AllocatePagesForMdl failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Map the MDL to get a kernel-mode virtual address
    dmaBuffer_ = static_cast<PBYTE>(
        portStream_->MapAllocatedPages(
            dmaBufferMdl_,
            MmCached
        )
    );

    if (!dmaBuffer_)
    {
        DbgPrint("VirtualLoopMic: MapAllocatedPages failed\n");
        // TODO: Free the MDL
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    dmaBufferSize_ = MmGetMdlByteCount(dmaBufferMdl_);

    // Zero-fill the buffer initially (silence)
    RtlZeroMemory(dmaBuffer_, dmaBufferSize_);

    // Return results
    *AudioBufferMdl      = dmaBufferMdl_;
    *ActualSize          = dmaBufferSize_;
    *OffsetFromFirstPage = MmGetMdlByteOffset(dmaBufferMdl_);
    *CacheType           = MmCached;

    DbgPrint("VirtualLoopMic: Allocated WaveRT buffer: %lu bytes\n", dmaBufferSize_);

    return STATUS_SUCCESS;
}
#pragma code_seg()

// ---------------------------------------------------------------------------
// FreeBufferWithNotification
// ---------------------------------------------------------------------------

#pragma code_seg("PAGE")
STDMETHODIMP_(VOID)
CVirtualLoopStream::FreeBufferWithNotification(
    _In_ PMDL  AudioBufferMdl,
    _In_ ULONG BufferSize
)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(BufferSize);

    DbgPrint("VirtualLoopMic: FreeBufferWithNotification\n");

    if (dmaBuffer_ && dmaBufferMdl_)
    {
        portStream_->UnmapAllocatedPages(dmaBuffer_, dmaBufferMdl_);
        dmaBuffer_ = nullptr;
    }

    if (AudioBufferMdl)
    {
        portStream_->FreePagesFromMdl(AudioBufferMdl);
        dmaBufferMdl_ = nullptr;
    }

    dmaBufferSize_ = 0;
}
#pragma code_seg()

// ---------------------------------------------------------------------------
// GetClockRegister / GetPositionRegister
// ---------------------------------------------------------------------------

/**
 * @brief Return a hardware clock register for low-latency position queries.
 *
 * For a virtual device, we don't have a hardware register. Return
 * STATUS_NOT_SUPPORTED so PortCls uses software position tracking.
 */
STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::GetClockRegister(
    _Out_ PKSRTAUDIO_HWREGISTER Register
)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Return a hardware position register.
 *
 * Not supported for virtual device — PortCls falls back to GetPosition().
 */
STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::GetPositionRegister(
    _Out_ PKSRTAUDIO_HWREGISTER Register
)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

// ---------------------------------------------------------------------------
// SetFormat
// ---------------------------------------------------------------------------

STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::SetFormat(
    _In_ PKSDATAFORMAT DataFormat
)
{
    UNREFERENCED_PARAMETER(DataFormat);
    // Format changes after stream creation are not supported.
    return STATUS_NOT_SUPPORTED;
}

// ---------------------------------------------------------------------------
// AllocateAudioBuffer (non-notification version)
// ---------------------------------------------------------------------------

#pragma code_seg("PAGE")
STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::AllocateAudioBuffer(
    _In_  ULONG              RequestedSize,
    _Out_ PMDL*              AudioBufferMdl,
    _Out_ ULONG*             ActualSize,
    _Out_ ULONG*             OffsetFromFirstPage,
    _Out_ MEMORY_CACHING_TYPE* CacheType
)
{
    PAGED_CODE();

    // Delegate to the notification version with notification count = 0
    return AllocateBufferWithNotification(
        0, RequestedSize, AudioBufferMdl, ActualSize, OffsetFromFirstPage, CacheType);
}
#pragma code_seg()

// ---------------------------------------------------------------------------
// FreeAudioBuffer (non-notification version)
// ---------------------------------------------------------------------------

#pragma code_seg("PAGE")
STDMETHODIMP_(VOID)
CVirtualLoopStream::FreeAudioBuffer(
    _In_opt_ PMDL AudioBufferMdl,
    _In_ ULONG BufferSize
)
{
    PAGED_CODE();

    FreeBufferWithNotification(AudioBufferMdl, BufferSize);
}
#pragma code_seg()

// ---------------------------------------------------------------------------
// GetHWLatency
// ---------------------------------------------------------------------------

STDMETHODIMP_(VOID)
CVirtualLoopStream::GetHWLatency(
    _Out_ KSRTAUDIO_HWLATENCY* hwLatency
)
{
    // Virtual device has no hardware latency
    hwLatency->ChipsetDelay = 0;
    hwLatency->CodecDelay   = 0;
    hwLatency->FifoSize     = 0;
}

// ---------------------------------------------------------------------------
// SetState
// ---------------------------------------------------------------------------

/**
 * @brief Handle stream state transitions.
 *
 * The KS stream state machine:
 *   STOP -> ACQUIRE -> PAUSE -> RUN
 *   RUN  -> PAUSE   -> ACQUIRE -> STOP
 *
 * We start the timer DPC on RUN and stop it on anything else.
 */
STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::SetState(
    _In_ KSSTATE State
)
{
    DbgPrint("VirtualLoopMic: SetState %lu -> %lu\n", state_, State);

    switch (State)
    {
    case KSSTATE_RUN:
        // ---------------------------------------------------------
        // Start the periodic timer to copy audio from shared memory.
        // ---------------------------------------------------------
        if (!timerActive_)
        {
            writePosition_    = 0;
            bytesTransferred_ = 0;

            // Try to open shared memory if not already open
            if (!shmReader_)
            {
                NTSTATUS status = KernelShmReaderOpen(&shmReader_);
                if (!NT_SUCCESS(status))
                {
                    DbgPrint("VirtualLoopMic: SHM still not available, "
                             "will produce silence\n");
                    shmReader_ = nullptr;
                }
            }

            // Set periodic timer: fires every 5ms
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = TIMER_INTERVAL_100NS;

            KeSetTimerEx(
                &timer_,
                dueTime,
                5,          // Period in ms (must match TIMER_INTERVAL_100NS)
                &timerDpc_
            );

            timerActive_ = TRUE;
            DbgPrint("VirtualLoopMic: Timer started (5ms period)\n");
        }
        break;

    case KSSTATE_PAUSE:
        // Timer keeps running in PAUSE to maintain position,
        // but we could optionally write silence only.
        // TODO: Decide pause behavior — for now, keep filling.
        break;

    case KSSTATE_ACQUIRE:
        // Pre-roll: nothing to do for capture
        break;

    case KSSTATE_STOP:
        // ---------------------------------------------------------
        // Stop the timer DPC.
        // ---------------------------------------------------------
        if (timerActive_)
        {
            KeCancelTimer(&timer_);
            timerActive_ = FALSE;
            DbgPrint("VirtualLoopMic: Timer stopped\n");
        }
        break;
    }

    state_ = State;
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// GetPosition
// ---------------------------------------------------------------------------

/**
 * @brief Report the current play/write position.
 *
 * For capture:
 *   - PlayOffset: read position (where the audio engine is reading)
 *   - WriteOffset: write position (where the driver is writing)
 *
 * The audio engine uses this to know how much unread data is in the buffer.
 */
STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::GetPosition(
    _Out_ KSAUDIO_POSITION* Position
)
{
    if (!Position)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // For capture, WriteOffset is where we last wrote data.
    // PlayOffset is where the audio engine has consumed up to.
    // Since we're a virtual device, we report our write position.
    Position->PlayOffset  = bytesTransferred_;
    Position->WriteOffset = bytesTransferred_;

    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// RegisterNotificationEvent / UnregisterNotificationEvent
// ---------------------------------------------------------------------------

STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::RegisterNotificationEvent(
    _In_ PKEVENT NotificationEvent
)
{
    DbgPrint("VirtualLoopMic: RegisterNotificationEvent\n");

    notificationEvent_ = NotificationEvent;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::UnregisterNotificationEvent(
    _In_ PKEVENT NotificationEvent
)
{
    DbgPrint("VirtualLoopMic: UnregisterNotificationEvent\n");

    if (notificationEvent_ == NotificationEvent)
    {
        notificationEvent_ = nullptr;
    }
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

STDMETHODIMP_(NTSTATUS)
CVirtualLoopStream::NonDelegatingQueryInterface(
    _In_  REFIID  Interface,
    _Out_ PVOID*  Object
)
{
    PAGED_CODE();

    ASSERT(Object);

    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
    {
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERTSTREAM(this)));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStream))
    {
        *Object = PVOID(PMINIPORTWAVERTSTREAM(this));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStreamNotification))
    {
        *Object = PVOID(static_cast<IMiniportWaveRTStreamNotification*>(this));
    }
    else
    {
        *Object = nullptr;
        return STATUS_INVALID_PARAMETER;
    }

    PUNKNOWN(*Object)->AddRef();
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Timer DPC — Audio data transfer
// ---------------------------------------------------------------------------

/**
 * @brief Static DPC callback. Dispatches to the instance method.
 *
 * Runs at DISPATCH_LEVEL. Must not call any paged functions, allocate
 * memory, or block.
 */
_Use_decl_annotations_
VOID
CVirtualLoopStream::TimerDpcRoutine(
    _In_     PKDPC  Dpc,
    _In_opt_ PVOID  DeferredContext,
    _In_opt_ PVOID  SystemArgument1,
    _In_opt_ PVOID  SystemArgument2
)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (DeferredContext)
    {
        auto* stream = static_cast<CVirtualLoopStream*>(DeferredContext);
        stream->OnTimerDpc();
    }
}

/**
 * @brief Core audio transfer logic (runs at DISPATCH_LEVEL).
 *
 * This method is called every 5ms by the timer DPC. It:
 *   1. Calculates how many frames to produce for this period
 *   2. Reads from the DirectPipe shared memory ring buffer
 *   3. Converts format if needed (float32 -> int16)
 *   4. Copies into the WaveRT DMA buffer at the current write position
 *   5. Fills with silence if insufficient data
 *   6. Advances the write position (wrapping around)
 *   7. Signals the notification event
 *
 * IMPORTANT: This runs at DISPATCH_LEVEL. No paged memory access,
 * no allocations, no blocking calls.
 */
void
CVirtualLoopStream::OnTimerDpc()
{
    if (state_ != KSSTATE_RUN || !dmaBuffer_ || dmaBufferSize_ == 0)
    {
        return;
    }

    // -----------------------------------------------------------------
    // Calculate how many frames to produce for this timer period.
    //
    // At 48000 Hz with a 5ms timer, that's 240 frames per period.
    // We use a fixed calculation based on sample rate and timer interval.
    // -----------------------------------------------------------------
    const ULONG framesToProduce = (sampleRate_ * 5) / 1000;  // 5ms worth
    const ULONG bytesToProduce  = framesToProduce * bytesPerFrame_;

    // Ensure we don't exceed our temporary buffer
    ULONG actualFrames = min(framesToProduce, MAX_TEMP_FRAMES);

    // -----------------------------------------------------------------
    // Read from shared memory (if connected)
    // -----------------------------------------------------------------
    ULONG framesRead = 0;

    if (shmReader_ && KernelShmReaderIsConnected(shmReader_) && tempBuffer_)
    {
        framesRead = KernelShmReaderRead(shmReader_, tempBuffer_, actualFrames);
    }

    // -----------------------------------------------------------------
    // Write into the WaveRT DMA buffer at the current position.
    //
    // The DMA buffer is cyclic: when we reach the end, we wrap to the
    // beginning. We may need to split the write across the wrap point.
    // -----------------------------------------------------------------
    ULONG bytesWritten = 0;
    ULONG pos = writePosition_;

    if (framesRead > 0)
    {
        // We have audio data from shared memory
        if (isFloat_ && bitsPerSample_ == 32)
        {
            // Float32 format matches shared memory — direct copy
            const ULONG copyBytes = framesRead * bytesPerFrame_;
            const ULONG firstChunk = min(copyBytes, dmaBufferSize_ - pos);
            const ULONG secondChunk = copyBytes - firstChunk;

            RtlCopyMemory(dmaBuffer_ + pos, tempBuffer_, firstChunk);
            if (secondChunk > 0)
            {
                RtlCopyMemory(dmaBuffer_, reinterpret_cast<PBYTE>(tempBuffer_) + firstChunk, secondChunk);
            }

            bytesWritten = copyBytes;
        }
        else if (!isFloat_ && bitsPerSample_ == 16)
        {
            // Need to convert float32 -> int16
            const ULONG sampleCount = framesRead * channels_;
            const ULONG copyBytes = framesRead * bytesPerFrame_;  // int16 size

            // Convert directly into the DMA buffer, handle cyclic wrap
            const ULONG firstChunkBytes = min(copyBytes, dmaBufferSize_ - pos);
            const ULONG firstChunkSamples = firstChunkBytes / sizeof(INT16);

            ConvertFloat32ToInt16(
                reinterpret_cast<INT16*>(dmaBuffer_ + pos),
                tempBuffer_,
                firstChunkSamples
            );

            if (firstChunkBytes < copyBytes)
            {
                // Wrap: convert remaining samples into the start of the buffer
                const ULONG remainingSamples = sampleCount - firstChunkSamples;
                ConvertFloat32ToInt16(
                    reinterpret_cast<INT16*>(dmaBuffer_),
                    tempBuffer_ + firstChunkSamples,
                    remainingSamples
                );
            }

            bytesWritten = copyBytes;
        }
        else if (!isFloat_ && bitsPerSample_ == 24)
        {
            // Need to convert float32 -> 24-bit packed PCM (3 bytes/sample)
            const ULONG sampleCount = framesRead * channels_;
            const ULONG copyBytes = framesRead * bytesPerFrame_;  // 3 bytes per sample

            // Convert directly into the DMA buffer, handle cyclic wrap
            const ULONG firstChunkBytes = min(copyBytes, dmaBufferSize_ - pos);
            const ULONG firstChunkSamples = firstChunkBytes / 3;

            ConvertFloat32ToInt24(
                dmaBuffer_ + pos,
                tempBuffer_,
                firstChunkSamples
            );

            if (firstChunkBytes < copyBytes)
            {
                // Wrap: convert remaining samples into the start of the buffer
                const ULONG remainingSamples = sampleCount - firstChunkSamples;
                ConvertFloat32ToInt24(
                    dmaBuffer_,
                    tempBuffer_ + firstChunkSamples,
                    remainingSamples
                );
            }

            bytesWritten = copyBytes;
        }
        else
        {
            // Unsupported format combination — fill with silence
            framesRead = 0;
        }
    }

    // -----------------------------------------------------------------
    // Fill remaining frames with silence if we didn't get enough data
    // -----------------------------------------------------------------
    if (framesRead < actualFrames)
    {
        const ULONG silenceFrames = actualFrames - framesRead;
        const ULONG silenceBytes  = silenceFrames * bytesPerFrame_;
        const ULONG silenceStart  = (pos + bytesWritten) % dmaBufferSize_;

        const ULONG firstChunk = min(silenceBytes, dmaBufferSize_ - silenceStart);
        RtlZeroMemory(dmaBuffer_ + silenceStart, firstChunk);

        if (firstChunk < silenceBytes)
        {
            RtlZeroMemory(dmaBuffer_, silenceBytes - firstChunk);
        }

        bytesWritten += silenceBytes;
    }

    // -----------------------------------------------------------------
    // Advance write position (cyclic wrap)
    // -----------------------------------------------------------------
    writePosition_     = (pos + bytesWritten) % dmaBufferSize_;
    bytesTransferred_ += bytesWritten;

    // -----------------------------------------------------------------
    // Signal the notification event so the audio engine knows new data
    // is available. PortCls/WASAPI uses this for event-driven mode.
    // -----------------------------------------------------------------
    if (notificationEvent_)
    {
        KeSetEvent(notificationEvent_, 0, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Format conversion helpers
// ---------------------------------------------------------------------------

/**
 * @brief Convert float32 samples to int16 with clamping.
 *
 * Clamps to [-1.0, 1.0] range before scaling to int16 range.
 * No SIMD optimization here — this is a scaffold implementation.
 *
 * @param dst    Output int16 samples.
 * @param src    Input float32 samples.
 * @param count  Number of individual samples to convert.
 */
void
CVirtualLoopStream::ConvertFloat32ToInt16(
    _Out_ INT16*       dst,
    _In_  const float* src,
    _In_  ULONG        count
)
{
    for (ULONG i = 0; i < count; ++i)
    {
        float sample = src[i];

        // Clamp to [-1.0, 1.0]
        if (sample > 1.0f)  sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;

        // Scale to int16 range
        dst[i] = static_cast<INT16>(sample * 32767.0f);
    }
}

/**
 * @brief Convert float32 samples to 24-bit packed PCM with clamping.
 *
 * Outputs 3 bytes per sample in little-endian order.
 * Clamps to [-1.0, 1.0] range before scaling to 24-bit range.
 *
 * @param dst    Output buffer (3 bytes per sample).
 * @param src    Input float32 samples.
 * @param count  Number of individual samples to convert.
 */
void
CVirtualLoopStream::ConvertFloat32ToInt24(
    _Out_ BYTE*        dst,
    _In_  const float* src,
    _In_  ULONG        count
)
{
    for (ULONG i = 0; i < count; ++i)
    {
        float sample = src[i];

        // Clamp to [-1.0, 1.0]
        if (sample > 1.0f)  sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;

        // Scale to 24-bit range (-8388608 to 8388607)
        INT32 val = static_cast<INT32>(sample * 8388607.0f);

        // Write 3 bytes little-endian
        dst[i * 3 + 0] = static_cast<BYTE>(val & 0xFF);
        dst[i * 3 + 1] = static_cast<BYTE>((val >> 8) & 0xFF);
        dst[i * 3 + 2] = static_cast<BYTE>((val >> 16) & 0xFF);
    }
}

// ---------------------------------------------------------------------------
// Factory function (called from miniport.cpp)
// ---------------------------------------------------------------------------

/**
 * @brief Create and initialize a new CVirtualLoopStream.
 *
 * @param OutStream    Receives the IMiniportWaveRTStream interface.
 * @param PortStream   The port's stream interface.
 * @param Pin          Pin number.
 * @param Capture      TRUE for capture.
 * @param DataFormat   Requested audio format.
 * @return STATUS_SUCCESS on success.
 */
#pragma code_seg("PAGE")
NTSTATUS
CreateVirtualLoopStream(
    _Out_ IMiniportWaveRTStream** OutStream,
    _In_  IPortWaveRTStream*      PortStream,
    _In_  ULONG                   Pin,
    _In_  BOOLEAN                 Capture,
    _In_  PKSDATAFORMAT           DataFormat
)
{
    PAGED_CODE();

    CVirtualLoopStream* stream = new(NonPagedPoolNx, STREAM_POOLTAG)
        CVirtualLoopStream(nullptr);

    if (!stream)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NTSTATUS status = stream->Init(PortStream, Pin, Capture, DataFormat);
    if (!NT_SUCCESS(status))
    {
        stream->Release();
        return status;
    }

    stream->AddRef();
    *OutStream = static_cast<IMiniportWaveRTStream*>(stream);

    return STATUS_SUCCESS;
}
#pragma code_seg()
