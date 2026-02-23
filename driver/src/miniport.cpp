/**
 * @file miniport.cpp
 * @brief WaveRT miniport implementation for the Virtual Loop Mic driver.
 *
 * Implements IMiniportWaveRT for a capture-only virtual audio device.
 * The miniport handles format negotiation, pin/stream creation, and
 * reports supported audio formats (48kHz/44.1kHz, mono/stereo,
 * 16-bit PCM and 32-bit float).
 *
 * Based on the Microsoft Sysvad sample's miniport pattern.
 *
 * Build: Requires WDK, portcls.h, ks.h, ksmedia.h
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
// Forward declarations (stream factory, defined in stream.cpp)
// ---------------------------------------------------------------------------

/**
 * @brief Factory to create a VirtualLoopStream instance.
 *
 * Called by the miniport when PortCls opens a new capture stream.
 *
 * @param OutStream        Receives the created IMiniportWaveRTStream.
 * @param OutDmaChannel    Not used for WaveRT (set to NULL).
 * @param Miniport         Backpointer to the miniport.
 * @param PortStream       The port's stream interface (for buffer allocation).
 * @param Pin              Pin ID that was opened.
 * @param Capture          TRUE if capture, FALSE if render.
 * @param DataFormat       Requested audio format.
 * @return STATUS_SUCCESS on success.
 */
extern NTSTATUS CreateVirtualLoopStream(
    _Out_ IMiniportWaveRTStream** OutStream,
    _In_  IPortWaveRTStream*      PortStream,
    _In_  ULONG                   Pin,
    _In_  BOOLEAN                 Capture,
    _In_  PKSDATAFORMAT           DataFormat
);

// ---------------------------------------------------------------------------
// Supported format definitions
// ---------------------------------------------------------------------------

/**
 * Format support matrix for Virtual Loop Mic:
 *
 *   Sample Rate  | Channels | Bit Depth     | Format Tag
 *   -------------|----------|---------------|--------------------
 *   48000 Hz     | 1-2 ch   | 16-bit PCM    | KSDATAFORMAT_SUBTYPE_PCM
 *   48000 Hz     | 1-2 ch   | 24-bit PCM    | KSDATAFORMAT_SUBTYPE_PCM
 *   48000 Hz     | 1-2 ch   | 32-bit float  | KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
 *   44100 Hz     | 1-2 ch   | 16-bit PCM    | KSDATAFORMAT_SUBTYPE_PCM
 *   44100 Hz     | 1-2 ch   | 24-bit PCM    | KSDATAFORMAT_SUBTYPE_PCM
 *   44100 Hz     | 1-2 ch   | 32-bit float  | KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
 */

/// Pin data ranges for the capture pin.
/// Each KSDATARANGE_AUDIO describes one supported format range.
static KSDATARANGE_AUDIO CaptureDataRanges[] =
{
    // ---- 48000 Hz, 16-bit PCM, 1-2 channels ----
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        2,      // MaximumChannels
        16,     // MinimumBitsPerSample
        16,     // MaximumBitsPerSample
        48000,  // MinimumSampleFrequency
        48000   // MaximumSampleFrequency
    },

    // ---- 48000 Hz, 24-bit PCM, 1-2 channels ----
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        2,      // MaximumChannels
        24,     // MinimumBitsPerSample
        24,     // MaximumBitsPerSample
        48000,  // MinimumSampleFrequency
        48000   // MaximumSampleFrequency
    },

    // ---- 48000 Hz, 32-bit float, 1-2 channels ----
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        2,      // MaximumChannels
        32,     // MinimumBitsPerSample
        32,     // MaximumBitsPerSample
        48000,  // MinimumSampleFrequency
        48000   // MaximumSampleFrequency
    },

    // ---- 44100 Hz, 16-bit PCM, 1-2 channels ----
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        2,      // MaximumChannels
        16,     // MinimumBitsPerSample
        16,     // MaximumBitsPerSample
        44100,  // MinimumSampleFrequency
        44100   // MaximumSampleFrequency
    },

    // ---- 44100 Hz, 24-bit PCM, 1-2 channels ----
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        2,      // MaximumChannels
        24,     // MinimumBitsPerSample
        24,     // MaximumBitsPerSample
        44100,  // MinimumSampleFrequency
        44100   // MaximumSampleFrequency
    },

    // ---- 44100 Hz, 32-bit float, 1-2 channels ----
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        2,      // MaximumChannels
        32,     // MinimumBitsPerSample
        32,     // MaximumBitsPerSample
        44100,  // MinimumSampleFrequency
        44100   // MaximumSampleFrequency
    }
};

static PKSDATARANGE CaptureDataRangePointers[] =
{
    PKSDATARANGE(&CaptureDataRanges[0]),
    PKSDATARANGE(&CaptureDataRanges[1]),
    PKSDATARANGE(&CaptureDataRanges[2]),
    PKSDATARANGE(&CaptureDataRanges[3]),
    PKSDATARANGE(&CaptureDataRanges[4]),
    PKSDATARANGE(&CaptureDataRanges[5])
};

// ---------------------------------------------------------------------------
// Pin descriptor
// ---------------------------------------------------------------------------

/**
 * @brief KS pin descriptor for the capture pin.
 *
 * We define a single capture (source) pin. The dataflow is KSPIN_DATAFLOW_OUT
 * because from the filter's perspective, capture data flows OUT of the
 * filter towards the audio engine.
 */
static PCPIN_DESCRIPTOR CapturePinDescriptors[] =
{
    // Pin 0: Bridge pin (represents the physical microphone source)
    // DataFlow IN = data enters the filter from the external source
    {
        0, 0, 0,
        nullptr,
        {
            0,
            nullptr,
            0,
            nullptr,
            SIZEOF_ARRAY(CaptureDataRangePointers),
            CaptureDataRangePointers,
            KSPIN_DATAFLOW_IN,              // Data flows IN from "hardware"
            KSPIN_COMMUNICATION_BRIDGE,     // Bridge pin (not client-accessible)
            &KSNODETYPE_MICROPHONE,         // Physical source = microphone
            nullptr,
            0
        }
    },
    // Pin 1: Host pin (audio engine connects here to read captured audio)
    // DataFlow OUT = data flows OUT from filter to audio engine
    {
        1, 1, 0,                // MaxGlobal, MaxPerFilter, MinPerFilter
        nullptr,                // AutomationTable
        {
            0,                  // InterfacesCount (use defaults)
            nullptr,
            0,                  // MediumsCount (use defaults)
            nullptr,
            SIZEOF_ARRAY(CaptureDataRangePointers),
            CaptureDataRangePointers,
            KSPIN_DATAFLOW_OUT,             // Data flows OUT to audio engine
            KSPIN_COMMUNICATION_SINK,       // Clients connect here
            &KSCATEGORY_AUDIO,              // Generic audio category
            nullptr,                         // Name GUID (use default)
            0
        }
    }
};

// ---------------------------------------------------------------------------
// Node descriptor (minimal topology)
// ---------------------------------------------------------------------------

/**
 * @brief Topology nodes.
 *
 * For a basic capture device we define a single ADC (analog-to-digital
 * converter) node. This is a virtual node since there's no real hardware.
 *
 * TODO: Add volume/mute nodes if mixer control is desired.
 */
static PCNODE_DESCRIPTOR CaptureNodeDescriptors[] =
{
    {
        0,                          // Flags
        nullptr,                    // AutomationTable
        &KSNODETYPE_ADC,            // Type: ADC node
        nullptr                     // Name
    }
};

// ---------------------------------------------------------------------------
// Connection descriptors
// ---------------------------------------------------------------------------

/**
 * @brief Connections between pins and nodes.
 *
 *   [Bridge Pin 0] --> [ADC Node 0] --> [Host Pin 1]
 */
static PCCONNECTION_DESCRIPTOR CaptureConnections[] =
{
    { PCFILTER_NODE, 0,   0,             1 },   // Bridge Pin 0 -> Node 0, pin 1
    { 0,             0,   PCFILTER_NODE, 1 }    // Node 0, pin 0 -> Host Pin 1
};

// ---------------------------------------------------------------------------
// Filter descriptor
// ---------------------------------------------------------------------------

/**
 * @brief The filter descriptor for the capture subdevice.
 *
 * Combines pins, nodes, and connections into a complete KS filter
 * description that PortCls uses to build the filter factory.
 */
static PCFILTER_DESCRIPTOR CaptureFilterDescriptor =
{
    0,                                          // Version
    nullptr,                                    // AutomationTable
    sizeof(PCPIN_DESCRIPTOR),                   // PinSize
    SIZEOF_ARRAY(CapturePinDescriptors),        // PinCount
    CapturePinDescriptors,                      // Pins
    sizeof(PCNODE_DESCRIPTOR),                  // NodeSize
    SIZEOF_ARRAY(CaptureNodeDescriptors),       // NodeCount
    CaptureNodeDescriptors,                     // Nodes
    SIZEOF_ARRAY(CaptureConnections),           // ConnectionCount
    CaptureConnections,                         // Connections
    0,                                          // CategoryCount
    nullptr                                     // Categories
};

// ===========================================================================
// CVirtualLoopMiniport — IMiniportWaveRT implementation
// ===========================================================================

/**
 * @class CVirtualLoopMiniport
 * @brief WaveRT miniport for the Virtual Loop Mic capture device.
 *
 * Implements the IMiniportWaveRT interface. PortCls calls into this
 * miniport for:
 *   - Init(): one-time initialization
 *   - NewStream(): creating capture streams
 *   - GetDescription(): returning the filter descriptor
 *
 * This is a capture-only miniport. NewStream() rejects render requests.
 */
class CVirtualLoopMiniport : public IMiniportWaveRT,
                              public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CVirtualLoopMiniport);

    ~CVirtualLoopMiniport();

    // ---- IMiniportWaveRT ----

    /**
     * @brief Initialize the miniport.
     *
     * Called by PortCls after the port and miniport are connected.
     * We store the port interface for later use and perform any
     * one-time initialization.
     *
     * @param UnknownAdapter  Not used (adapter context).
     * @param ResourceList    Hardware resources (empty for virtual device).
     * @param Port_           The port driver interface.
     * @return STATUS_SUCCESS.
     */
    STDMETHODIMP_(NTSTATUS) Init(
        _In_ PUNKNOWN      UnknownAdapter,
        _In_ PRESOURCELIST  ResourceList,
        _In_ PPORTWAVERT    Port_
    );

    /**
     * @brief Create a new WaveRT stream.
     *
     * Called when a client opens the capture pin. We instantiate a
     * VirtualLoopStream object that handles the actual audio data
     * transfer from shared memory.
     *
     * @param OutStream        Receives the new stream interface.
     * @param OutDmaChannel    Not used for WaveRT (set to NULL).
     * @param PortStream       Port stream interface (for buffer allocation).
     * @param Pin              Pin number being opened.
     * @param Capture          TRUE for capture, FALSE for render.
     * @param DataFormat       Requested audio data format.
     * @return STATUS_SUCCESS on success.
     */
    STDMETHODIMP_(NTSTATUS) NewStream(
        _Out_ PMINIPORTWAVERTSTREAM* OutStream,
        _In_  PPORTWAVERTSTREAM      PortStream,
        _In_  ULONG                  Pin,
        _In_  BOOLEAN                Capture,
        _In_  PKSDATAFORMAT          DataFormat
    );

    /**
     * @brief Return the filter descriptor for this miniport.
     *
     * PortCls uses this to build the KS filter factory with the
     * correct pin types, data ranges, and topology.
     *
     * @param OutFilterDescriptor  Receives pointer to the descriptor.
     * @return STATUS_SUCCESS.
     */
    STDMETHODIMP_(NTSTATUS) GetDescription(
        _Out_ PPCFILTER_DESCRIPTOR* OutFilterDescriptor
    );

    STDMETHODIMP_(NTSTATUS) DataRangeIntersection(
        _In_  ULONG       PinId,
        _In_  PKSDATARANGE DataRange,
        _In_  PKSDATARANGE MatchingDataRange,
        _In_  ULONG       OutputBufferLength,
        _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
              PVOID       ResultantFormat,
        _Out_ PULONG      ResultantFormatLength
    );

    STDMETHODIMP_(NTSTATUS) GetDeviceDescription(
        _Out_ PDEVICE_DESCRIPTION DeviceDescription
    );

private:
    /// Back-pointer to the WaveRT port driver.
    PPORTWAVERT port_ = nullptr;
};

// ---------------------------------------------------------------------------
// IUnknown / CUnknown boilerplate
// ---------------------------------------------------------------------------

/// COM interface map for CVirtualLoopMiniport.
/// Routes QI calls to IMiniportWaveRT and IUnknown.
NTSTATUS CVirtualLoopMiniport_QueryInterface(
    _In_  PUNKNOWN  pUnknown,
    _In_  REFIID    Interface,
    _Out_ PVOID*    Object
);

#pragma code_seg("PAGE")

/**
 * @brief Destructor — releases the port reference.
 */
CVirtualLoopMiniport::~CVirtualLoopMiniport()
{
    PAGED_CODE();

    DbgPrint("VirtualLoopMic: Miniport destructor\n");

    if (port_)
    {
        port_->Release();
        port_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// IMiniportWaveRT::Init
// ---------------------------------------------------------------------------

STDMETHODIMP_(NTSTATUS)
CVirtualLoopMiniport::Init(
    _In_ PUNKNOWN      UnknownAdapter,
    _In_ PRESOURCELIST  ResourceList,
    _In_ PPORTWAVERT    Port_
)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    DbgPrint("VirtualLoopMic: Miniport::Init\n");

    // Store port reference (AddRef)
    port_ = Port_;
    port_->AddRef();

    // -----------------------------------------------------------------
    // TODO: Additional initialization:
    //   - Read registry settings (default sample rate, channel count)
    //   - Initialize WPP tracing
    //   - Pre-validate shared memory availability (optional)
    // -----------------------------------------------------------------

    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// IMiniportWaveRT::NewStream
// ---------------------------------------------------------------------------

STDMETHODIMP_(NTSTATUS)
CVirtualLoopMiniport::NewStream(
    _Out_ PMINIPORTWAVERTSTREAM* OutStream,
    _In_  PPORTWAVERTSTREAM      PortStream,
    _In_  ULONG                  Pin,
    _In_  BOOLEAN                Capture,
    _In_  PKSDATAFORMAT          DataFormat
)
{
    PAGED_CODE();

    DbgPrint("VirtualLoopMic: Miniport::NewStream (Pin=%lu, Capture=%d)\n", Pin, Capture);

    // -----------------------------------------------------------------
    // Validate: we only support capture (not render).
    // -----------------------------------------------------------------
    if (!Capture)
    {
        DbgPrint("VirtualLoopMic: Render not supported\n");
        return STATUS_NOT_SUPPORTED;
    }

    // -----------------------------------------------------------------
    // Validate the pin number. Pin 1 is the host capture pin.
    // -----------------------------------------------------------------
    if (Pin != 1)
    {
        DbgPrint("VirtualLoopMic: Invalid pin %lu\n", Pin);
        return STATUS_INVALID_PARAMETER;
    }

    // -----------------------------------------------------------------
    // Validate the requested data format.
    //
    // TODO: Full format intersection/validation against our supported
    //       ranges. For now, accept any WAVEFORMATEX-based audio format
    //       that matches our data ranges.
    // -----------------------------------------------------------------
    if (DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX))
    {
        DbgPrint("VirtualLoopMic: DataFormat too small\n");
        return STATUS_INVALID_PARAMETER;
    }

    PKSDATAFORMAT_WAVEFORMATEX waveFormat =
        reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX>(DataFormat);
    WAVEFORMATEX* wfx = &waveFormat->WaveFormatEx;

    // Verify sample rate is one we support
    if (wfx->nSamplesPerSec != 48000 && wfx->nSamplesPerSec != 44100)
    {
        DbgPrint("VirtualLoopMic: Unsupported sample rate %lu\n", wfx->nSamplesPerSec);
        return STATUS_NOT_SUPPORTED;
    }

    // Verify channel count (1 or 2)
    if (wfx->nChannels < 1 || wfx->nChannels > 2)
    {
        DbgPrint("VirtualLoopMic: Unsupported channel count %u\n", wfx->nChannels);
        return STATUS_NOT_SUPPORTED;
    }

    // Verify bit depth (16-bit PCM, 24-bit PCM, or 32-bit float)
    if (wfx->wBitsPerSample != 16 && wfx->wBitsPerSample != 24 && wfx->wBitsPerSample != 32)
    {
        DbgPrint("VirtualLoopMic: Unsupported bit depth %u\n", wfx->wBitsPerSample);
        return STATUS_NOT_SUPPORTED;
    }

    // -----------------------------------------------------------------
    // Create the stream object.
    //
    // The stream handles the actual audio transfer: reading from
    // DirectPipe shared memory and filling the WaveRT DMA buffer.
    // -----------------------------------------------------------------
    IMiniportWaveRTStream* stream = nullptr;

    NTSTATUS status = CreateVirtualLoopStream(
        &stream,
        PortStream,
        Pin,
        Capture,
        DataFormat
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: CreateVirtualLoopStream failed 0x%08X\n", status);
        return status;
    }

    *OutStream = stream;
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// IMiniportWaveRT::GetDescription
// ---------------------------------------------------------------------------

STDMETHODIMP_(NTSTATUS)
CVirtualLoopMiniport::GetDescription(
    _Out_ PPCFILTER_DESCRIPTOR* OutFilterDescriptor
)
{
    PAGED_CODE();

    *OutFilterDescriptor = &CaptureFilterDescriptor;
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// IMiniport::DataRangeIntersection
// ---------------------------------------------------------------------------

STDMETHODIMP_(NTSTATUS)
CVirtualLoopMiniport::DataRangeIntersection(
    _In_  ULONG       PinId,
    _In_  PKSDATARANGE DataRange,
    _In_  PKSDATARANGE MatchingDataRange,
    _In_  ULONG       OutputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
          PVOID       ResultantFormat,
    _Out_ PULONG      ResultantFormatLength
)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);

    // Return STATUS_NOT_IMPLEMENTED to let PortCls handle
    // the default data range intersection logic.
    return STATUS_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// IMiniportWaveRT::GetDeviceDescription
// ---------------------------------------------------------------------------

STDMETHODIMP_(NTSTATUS)
CVirtualLoopMiniport::GetDeviceDescription(
    _Out_ PDEVICE_DESCRIPTION DeviceDescription
)
{
    PAGED_CODE();

    RtlZeroMemory(DeviceDescription, sizeof(DEVICE_DESCRIPTION));
    DeviceDescription->Version  = DEVICE_DESCRIPTION_VERSION;
    DeviceDescription->Master   = TRUE;
    DeviceDescription->Dma32BitAddresses = TRUE;
    DeviceDescription->MaximumLength = 0x10000;  // 64KB

    return STATUS_SUCCESS;
}

#pragma code_seg()

// ---------------------------------------------------------------------------
// IUnknown support
// ---------------------------------------------------------------------------

/**
 * @brief Non-delegating QueryInterface for CVirtualLoopMiniport.
 */
STDMETHODIMP_(NTSTATUS)
CVirtualLoopMiniport::NonDelegatingQueryInterface(
    _In_ REFIID Interface,
    _Out_ PVOID* Object
)
{
    PAGED_CODE();

    ASSERT(Object);

    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
    {
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERT(this)));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniport))
    {
        *Object = PVOID(PMINIPORT(this));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRT))
    {
        *Object = PVOID(PMINIPORTWAVERT(this));
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
// Factory function (called from adapter.cpp)
// ---------------------------------------------------------------------------

/**
 * @brief Create a new CVirtualLoopMiniport instance.
 *
 * This is the factory function registered with the adapter and called
 * from CreateWaveRTCaptureMiniport() in adapter.cpp.
 *
 * @param Unknown        Receives the IUnknown of the new miniport.
 * @param ClassId        Not used (would be for COM class factory).
 * @param UnknownOuter   Outer unknown for COM aggregation.
 * @param PoolType       Kernel pool type.
 * @return STATUS_SUCCESS on success.
 */
#pragma code_seg("PAGE")
NTSTATUS
CreateVirtualLoopMiniport(
    _Out_ PUNKNOWN*  Unknown,
    _In_  REFCLSID   ClassId,
    _In_  PUNKNOWN   UnknownOuter  OPTIONAL,
    _In_  POOL_TYPE  PoolType
)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ClassId);

    CVirtualLoopMiniport* miniport = new(PoolType, 'PLVD')
        CVirtualLoopMiniport(UnknownOuter);

    if (!miniport)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    miniport->AddRef();
    *Unknown = reinterpret_cast<PUNKNOWN>(miniport);

    return STATUS_SUCCESS;
}
#pragma code_seg()
