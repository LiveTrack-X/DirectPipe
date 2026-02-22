/**
 * @file adapter.cpp
 * @brief WDM adapter entry point for the Virtual Loop Mic driver.
 *
 * Implements DriverEntry, AddDevice, and PnP dispatch handlers for a
 * capture-only virtual audio adapter. Based on the Microsoft Sysvad
 * sample driver architecture.
 *
 * This file is the kernel-mode entry point. It registers the adapter
 * with PortCls and sets up the miniport for a single WaveRT capture device.
 *
 * Build: Requires WDK, ntddk.h, portcls.h, ksdebug.h
 */

extern "C" {
#include <ntddk.h>
}

#include <portcls.h>
#include <ksdebug.h>
#include <ntstrsafe.h>

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

extern "C" DRIVER_INITIALIZE       DriverEntry;
extern "C" DRIVER_ADD_DEVICE       VirtualLoopAddDevice;
extern "C" DRIVER_UNLOAD           VirtualLoopUnload;

NTSTATUS VirtualLoopStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PRESOURCELIST   ResourceList
);

/**
 * @brief Create the WaveRT miniport for the capture device.
 *
 * Called during device start to instantiate the miniport and register
 * it with PortCls. This function creates both the port and miniport
 * objects, then connects them.
 *
 * @param DeviceObject  The FDO for the audio adapter.
 * @param Irp           The start-device IRP.
 * @param ResourceList  Hardware resources (empty for a virtual device).
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS CreateWaveRTCaptureMiniport(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PRESOURCELIST   ResourceList
);

// ---------------------------------------------------------------------------
// External miniport factory (defined in miniport.cpp)
// ---------------------------------------------------------------------------

/**
 * @brief Factory function to create the Virtual Loop Mic WaveRT miniport.
 *
 * @param Unknown        Outer IUnknown for aggregation (typically NULL).
 * @param UnknownOuter   Controlling IUnknown.
 * @param PoolType       Kernel pool type (NonPagedPoolNx).
 * @return NTSTATUS indicating success or failure. On success, *Unknown
 *         holds the newly created IMiniportWaveRT interface.
 */
extern NTSTATUS CreateVirtualLoopMiniport(
    _Out_ PUNKNOWN* Unknown,
    _In_  REFCLSID,
    _In_  PUNKNOWN  UnknownOuter  OPTIONAL,
    _In_  POOL_TYPE PoolType
);

// ---------------------------------------------------------------------------
// Debug tag for pool allocations
// ---------------------------------------------------------------------------
#define VIRTUALLOOP_POOLTAG 'PLVD'  // "DVLP" = DirectPipe Virtual Loop

// ---------------------------------------------------------------------------
// Device description tables
// ---------------------------------------------------------------------------

/**
 * @brief Topology miniport descriptor.
 *
 * For a simple capture-only virtual device, the topology miniport is
 * minimal: a single capture node with no mixer or volume controls.
 *
 * TODO: Implement a proper topology miniport if volume/mute controls
 *       are needed in the Windows mixer UI.
 */

/**
 * @brief Subdevice descriptor table.
 *
 * Lists all subdevices (miniports) the adapter exposes. In our case,
 * there is only one: the WaveRT capture subdevice.
 */

// ---------------------------------------------------------------------------
// DriverEntry
// ---------------------------------------------------------------------------

/**
 * @brief Main entry point for the Virtual Loop Mic kernel driver.
 *
 * Called by the OS when the driver is loaded. Initializes PortCls and
 * registers the AddDevice callback.
 *
 * @param DriverObject   Pointer to the driver object created by the OS.
 * @param RegistryPath   Registry path for driver-specific configuration.
 * @return STATUS_SUCCESS on success, or an appropriate error code.
 */
#pragma code_seg("INIT")
extern "C"
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    DbgPrint("VirtualLoopMic: DriverEntry called\n");

    // -----------------------------------------------------------------
    // Initialize PortCls — this sets up the standard PnP, power, and
    // IRP dispatch routines for a PortCls audio miniport driver.
    // -----------------------------------------------------------------
    NTSTATUS status = PcInitializeAdapterDriver(
        DriverObject,
        RegistryPath,
        VirtualLoopAddDevice
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: PcInitializeAdapterDriver failed 0x%08X\n", status);
        return status;
    }

    // -----------------------------------------------------------------
    // Override the driver unload routine so we can perform cleanup.
    // PortCls sets its own unload handler; we chain ours after it.
    // -----------------------------------------------------------------
    DriverObject->DriverUnload = VirtualLoopUnload;

    DbgPrint("VirtualLoopMic: DriverEntry succeeded\n");
    return STATUS_SUCCESS;
}
#pragma code_seg()

// ---------------------------------------------------------------------------
// AddDevice
// ---------------------------------------------------------------------------

/**
 * @brief PnP AddDevice handler.
 *
 * Called by the PnP manager when it detects a device matching this
 * driver's INF. For our software-enumerated virtual device, this is
 * called when the devnode is created via pnputil or devcon.
 *
 * We delegate to PcAddAdapterDevice, which creates the FDO and
 * attaches it to the device stack.
 *
 * @param DriverObject         The driver object.
 * @param PhysicalDeviceObject The PDO created by the bus driver (ROOT bus).
 * @return STATUS_SUCCESS on success.
 */
#pragma code_seg("PAGE")
extern "C"
NTSTATUS
VirtualLoopAddDevice(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PDEVICE_OBJECT  PhysicalDeviceObject
)
{
    PAGED_CODE();

    DbgPrint("VirtualLoopMic: AddDevice called\n");

    // -----------------------------------------------------------------
    // PcAddAdapterDevice creates the FDO, attaches it to the PDO,
    // and sets the StartDevice callback that PortCls will invoke
    // during IRP_MN_START_DEVICE.
    //
    // Parameters:
    //   - DriverObject: our driver
    //   - PhysicalDeviceObject: the PDO from ROOT bus
    //   - VirtualLoopStartDevice: our start-device callback
    //   - MAX_MINIPORTS: max number of subdevices (1 for capture only)
    //   - 0: device extension size (we use PortCls contexts instead)
    // -----------------------------------------------------------------
    constexpr ULONG MAX_MINIPORTS = 1;

    NTSTATUS status = PcAddAdapterDevice(
        DriverObject,
        PhysicalDeviceObject,
        VirtualLoopStartDevice,
        MAX_MINIPORTS,
        0   // DeviceExtensionSize
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: PcAddAdapterDevice failed 0x%08X\n", status);
    }

    return status;
}
#pragma code_seg()

// ---------------------------------------------------------------------------
// StartDevice
// ---------------------------------------------------------------------------

/**
 * @brief PortCls start-device callback.
 *
 * Called by PortCls during IRP_MN_START_DEVICE processing. This is
 * where we create and register our WaveRT capture miniport.
 *
 * @param DeviceObject  The FDO.
 * @param Irp           The start IRP.
 * @param ResourceList  Translated resources (empty for virtual device).
 * @return STATUS_SUCCESS on success.
 */
#pragma code_seg("PAGE")
NTSTATUS
VirtualLoopStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PRESOURCELIST   ResourceList
)
{
    PAGED_CODE();

    DbgPrint("VirtualLoopMic: StartDevice called\n");

    // -----------------------------------------------------------------
    // Create the WaveRT capture subdevice.
    // This instantiates both the port and miniport, connects them,
    // and registers the subdevice with PortCls.
    // -----------------------------------------------------------------
    NTSTATUS status = CreateWaveRTCaptureMiniport(DeviceObject, Irp, ResourceList);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: CreateWaveRTCaptureMiniport failed 0x%08X\n", status);
        return status;
    }

    DbgPrint("VirtualLoopMic: StartDevice succeeded\n");
    return STATUS_SUCCESS;
}
#pragma code_seg()

// ---------------------------------------------------------------------------
// CreateWaveRTCaptureMiniport
// ---------------------------------------------------------------------------

/**
 * @brief Instantiate the WaveRT port/miniport pair and register as a subdevice.
 *
 * Steps:
 *   1. Create a WaveRT port object (IPortWaveRT).
 *   2. Create our custom miniport (IMiniportWaveRT) via factory function.
 *   3. Register the subdevice with PortCls using PcRegisterSubdevice.
 *
 * @param DeviceObject  The FDO.
 * @param Irp           The start IRP.
 * @param ResourceList  Translated resources (empty for virtual device).
 * @return STATUS_SUCCESS on success.
 */
#pragma code_seg("PAGE")
NTSTATUS
CreateWaveRTCaptureMiniport(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PRESOURCELIST   ResourceList
)
{
    PAGED_CODE();

    NTSTATUS  status      = STATUS_SUCCESS;
    PUNKNOWN  unknownPort = nullptr;
    PUNKNOWN  unknownMiniport = nullptr;

    // -----------------------------------------------------------------
    // Step 1: Create the WaveRT port driver instance.
    //
    // The port driver handles most of the KS (Kernel Streaming) protocol:
    // pin creation, state transitions, clock negotiation, etc.
    // -----------------------------------------------------------------
    status = PcNewPort(&unknownPort, CLSID_PortWaveRT);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: PcNewPort(WaveRT) failed 0x%08X\n", status);
        goto Exit;
    }

    // -----------------------------------------------------------------
    // Step 2: Create our custom miniport.
    //
    // The miniport implements the device-specific behavior: format
    // negotiation, stream creation, and the actual audio data transfer
    // from shared memory into the WaveRT buffer.
    // -----------------------------------------------------------------
    status = CreateVirtualLoopMiniport(
        &unknownMiniport,
        CLSID_PortWaveRT,      // Reference class (not actually used for CoCreate)
        nullptr,                // No aggregation
        NonPagedPoolNx
    );
    if (!NT_SUCCESS(status))
    {
        DbgPrint("VirtualLoopMic: CreateVirtualLoopMiniport failed 0x%08X\n", status);
        goto Exit;
    }

    // -----------------------------------------------------------------
    // Step 3: Bind the port and miniport, then register.
    //
    // PcRegisterSubdevice calls port->Init(miniport, ...) internally,
    // which triggers our miniport's Init() method. The subdevice name
    // "VirtualLoopCapture" becomes the KS filter factory name.
    // -----------------------------------------------------------------
    {
        IPortWaveRT* portWaveRT = nullptr;
        status = unknownPort->QueryInterface(
            IID_IPortWaveRT,
            reinterpret_cast<PVOID*>(&portWaveRT)
        );
        if (!NT_SUCCESS(status))
        {
            DbgPrint("VirtualLoopMic: QI for IPortWaveRT failed 0x%08X\n", status);
            goto Exit;
        }

        IMiniportWaveRT* miniportWaveRT = nullptr;
        status = unknownMiniport->QueryInterface(
            IID_IMiniportWaveRT,
            reinterpret_cast<PVOID*>(&miniportWaveRT)
        );
        if (!NT_SUCCESS(status))
        {
            DbgPrint("VirtualLoopMic: QI for IMiniportWaveRT failed 0x%08X\n", status);
            portWaveRT->Release();
            goto Exit;
        }

        // Initialize the port with our miniport
        status = portWaveRT->Init(
            DeviceObject,
            miniportWaveRT,
            ResourceList
        );

        if (NT_SUCCESS(status))
        {
            // Register the subdevice under a well-known name
            status = PcRegisterSubdevice(
                DeviceObject,
                L"VirtualLoopCapture",
                unknownPort
            );
        }

        miniportWaveRT->Release();
        portWaveRT->Release();
    }

Exit:
    if (unknownPort)
    {
        unknownPort->Release();
    }
    if (unknownMiniport)
    {
        unknownMiniport->Release();
    }

    return status;
}
#pragma code_seg()

// ---------------------------------------------------------------------------
// DriverUnload
// ---------------------------------------------------------------------------

/**
 * @brief Driver unload handler.
 *
 * Called when the driver is being unloaded. We perform any global
 * cleanup here. PortCls handles most cleanup automatically via
 * reference counting on the port/miniport objects.
 *
 * @param DriverObject  The driver object being unloaded.
 */
#pragma code_seg("PAGE")
extern "C"
VOID
VirtualLoopUnload(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    PAGED_CODE();

    DbgPrint("VirtualLoopMic: DriverUnload called\n");

    // -----------------------------------------------------------------
    // PortCls handles most unload work. If we had global resources
    // (e.g., WPP tracing, lookaside lists), we'd clean them up here.
    // -----------------------------------------------------------------

    // TODO: If WPP tracing is enabled, call WPP_CLEANUP(DriverObject) here.

    UNREFERENCED_PARAMETER(DriverObject);
}
#pragma code_seg()
