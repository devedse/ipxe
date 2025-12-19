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
