# iPXE Boot Hang Investigation - MSI MAG B850M MORTAR WIFI

## Issue Summary
iPXE boots successfully on one computer but hangs on a newly built PC with MSI MAG B850M MORTAR WIFI motherboard. The system gets stuck after displaying "autoexec.ipxe... ok".

## Hardware Details
- **Motherboard**: MSI MAG B850M MORTAR WIFI
- **Network Card**: Marvell AQtion AQC13 (PCI ID: 1d6a:94c0, class 020000)
- **Network Driver**: Marvell AQtion Driver 2.1.7.0 EFIx64

## Debug Findings

### Hang Location
The system hangs during the EFI driver connection phase, specifically when processing handle 483/665.

### Last Successful Operations
1. `autoexec.ipxe` loads successfully (12711 bytes)
2. Autoexec script loaded (or not found)
3. Drivers vetoed successfully
4. Driver connection begins (`efi_driver_connect_all`)
5. Processes handles 0-483 successfully

### Hang Point Details
The system hangs while processing:
```
PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)
```

This is the Marvell AQC13 network card.

### Debug Output at Hang
```
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) connecting new drivers
DEBUG: About to call ConnectController for PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)
[HANGS HERE]
```

### Detailed Trace Analysis (from screenshot)
The log shows activity *before* the final hang on the main PCI handle:

1.  **Child Device Activity**:
    ```
    DEBUG: efi_driver_start entered for .../MAC(CC28AA68E1CA,0x0)
    EFIDRV ... DRIVER_START
    EFIDRV ... refusing to start during disconnection
    DEBUG: efi_driver_start failed with status 8000000000000006 (EFI_NOT_READY)
    ```
    This confirms iPXE correctly refuses to start drivers while it's in the middle of disconnecting existing ones.

2.  **Vendor Hardware Device Check**:
    ```
    DEBUG: efi_driver_supported entered for .../VenHw(D79DF6B0-EF44-43BD-9797-43E93BCF5FA8)
    DEBUG: checking support for driver PCI
    DEBUG: efipci_supported entered for ...
    DEBUG: efipci_info failed for ... Error 0x7f2f2083
    ```
    iPXE checks a `VenHw` device. `efipci_info` fails, which is expected for non-PCI devices.
    It then checks NII, SNP, MNP drivers, none support it.
    `DEBUG: no driver found for ...` - This is normal.

3.  **Main PCI Device Connection (The Hang)**:
    ```
    EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) connecting new drivers
    DEBUG: About to call ConnectController for PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)
    ```
    - `ConnectController` is called.
    - **CRITICAL**: We do **NOT** see `DEBUG: efi_driver_supported entered` for this handle after `ConnectController` is called.
    - This implies the firmware hangs **before** calling back into iPXE's driver binding protocol for this device, OR it calls another driver first which hangs.

### Hypothesis
The firmware is likely attempting to reconnect the **native Marvell AQtion driver** (which we just disconnected) because it has a higher priority or version than iPXE's driver. The native driver then hangs during re-initialization.

### Next Steps
1.  Investigate `efi_driver.c` to see if we can force `ConnectController` to only consider iPXE's driver.
2.  Alternatively, try to prevent the native driver from binding (veto).

```
Processing handle 483/665: PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)
EFIPCI 0000:05:00.0 (1d6a:94c0 class 020000) has driver "AQC13"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) has driver "PCI"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) disconnecting existing drivers
EFIPCI 0000:05:00.0 (1d6a:94c0 class 020000) has driver "AQC13"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) disconnecting PciIo drivers
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) disconnecting PciIo driver Marvell AQtion Driver 2.1.7.0 EFIx64
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)/MAC(8c20bed8e1ca,0x0) has driver "NII"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)/MAC(8c20bed8e1ca,0x0) has driver "NP"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)/MAC(8c20bed8e1ca,0x0) has driver "MN"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)/MAC(8c20bed8e1ca,0x0) DRIVER_START
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)/MAC(8c20bed8e1ca,0x0) refusing to start during disconnection
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) has driver "NII"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) has driver "SNP"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) has driver "MNP"
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) DRIVER_START
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) refusing to start during disconnection
EFIDRV PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0) connecting new drivers
DEBUG: About to call ConnectController for PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)
[HANGS HERE]
```

### Analysis
- The hang occurs **inside** the `bs->ConnectController` call in `efi_connect.c`.
- The `atl_probe` function in `aqc1xx.c` is **never reached** (no "DEBUG: atl_probe entered" message).
- This indicates the issue lies within the firmware's `ConnectController` implementation or in the early stages of iPXE's EFI driver binding callbacks (`efi_driver_supported` or `efi_driver_start`) before the specific hardware driver is invoked.

## Related GitHub Discussion

Original issue: https://github.com/ipxe/ipxe/discussions/1498
