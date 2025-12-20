/*
 * Copyright (C) 2025 iPXE Project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * You can also choose to distribute this program under the terms of
 * the Unmodified Binary Distribution Licence (as given in the file
 * COPYING.UBDL), provided that you have satisfied its requirements.
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ipxe/netdevice.h>
#include <ipxe/pci.h>
#include <ipxe/command.h>
#include <ipxe/parseopt.h>
#include <ipxe/malloc.h>
#ifdef PLATFORM_efi
#include <ipxe/efi/efi.h>
#include <ipxe/efi/efi_pci.h>
#include <ipxe/efi/efi_snp.h>
#include <ipxe/efi/Protocol/PciIo.h>
#endif

/** @file
 *
 * PNP device path listing command
 *
 */

/** "pnplist" options */
struct pnplist_options {};

/** "pnplist" option list */
static struct option_descriptor pnplist_opts[] = {};

/** "pnplist" command descriptor */
static struct command_descriptor pnplist_cmd =
	COMMAND_DESC ( struct pnplist_options, pnplist_opts, 0, 0, "" );

/**
 * Detect if device info appears to be abstracted by SNP or virtualization
 *
 * @v vendor		Vendor ID to check
 * @v device		Device ID to check  
 * @ret abstracted	True if this appears to be abstracted device info
 */
static int is_snp_abstracted ( uint16_t vendor, uint16_t device ) {
	/* SNP commonly uses these abstracted IDs */
	return ( vendor == 0x0102 && device == 0x000c );
}

/**
 * Try to get real PCI device information using EFI PCI I/O protocol
 *
 * This bypasses SNP abstraction by accessing the EFI handle associated with
 * the SNP device and using the EFI PCI I/O protocol to read real vendor/device IDs.
 *
 * @v netdev		Network device
 * @ret pci		PCI device, or NULL if not found
 * @ret need_free	Set to 1 if returned PCI device should be freed
 */
static struct pci_device * try_efi_pci_access ( struct net_device *netdev, int *need_free ) {
#ifdef PLATFORM_efi
	struct efi_snp_device *snpdev;
	struct efi_pci_device *efipci;
	struct pci_device *pci;
	EFI_HANDLE device_handle;
	int rc;

	/* Find SNP device for this network device */
	snpdev = find_snpdev_by_netdev ( netdev );
	if ( snpdev ) {
		
		/* Get the EFI device handle - this should be the PCI device */
		device_handle = snpdev->handle;
		
		/* Try to open PCI I/O protocol on the EFI device handle */
		efipci = malloc ( sizeof ( *efipci ) );
		if ( ! efipci ) {
			return NULL;
		}

		rc = efipci_info ( device_handle, efipci );
		if ( rc == 0 ) {
			
			/* Allocate a regular PCI device structure and copy the information */
			pci = malloc ( sizeof ( *pci ) );
			if ( pci ) {
				memcpy ( pci, &efipci->pci, sizeof ( *pci ) );
				*need_free = 1;
				free ( efipci );
				return pci;
			}
		} else {
			free ( efipci );
	} else {
		*need_free = 0;
#else
	/* Suppress unused parameter warning on non-EFI platforms */
	( void ) netdev;
#endif
	*need_free = 0;
	return NULL;
}

/**
 * Get the actual PCI device information for a network device
 *
 * In SNP builds, network devices often present abstracted vendor/device IDs 
 * instead of the real hardware. This function detects abstraction and uses
 * EFI PCI I/O protocol to find the actual underlying network controller.
 *
 * @v netdev		Network device
 * @v device_index	Index of this device in the network device list
 * @ret pci		PCI device, or NULL if not found
 * @ret need_free	Set to 1 if returned PCI device should be freed
 */
static struct pci_device * get_real_pci_device ( struct net_device *netdev, int *need_free ) {
	struct device *dev = netdev->dev;
	struct pci_device *pci = NULL;
	
	*need_free = 0;
	
	/* First, try EFI PCI I/O protocol access to bypass SNP abstraction */
	pci = try_efi_pci_access ( netdev, need_free );
	if ( pci ) {
		return pci;
	}
	
	/* Fall back to direct PCI device access */
	if ( dev->desc.bus_type == BUS_TYPE_PCI ) {
		pci = container_of ( dev, struct pci_device, dev );
		
		/* Check if we have abstracted device information */
		if ( is_snp_abstracted ( pci->vendor, pci->device ) ) {
		}
		
		return pci;
	}

	return NULL;
}

/**
 * Display Windows-style PNP device path for a network device
 *
 * @v netdev		Network device
 * @ret rc		Return status code
 */
static int pnplist_show_device ( struct net_device *netdev ) {
	struct pci_device *pci;
	uint16_t subsys_vendor = 0x0000, subsys_device = 0x0000;
	uint8_t revision = 0x00;
	int rc;
	int need_free = 0;

	/* Get the real PCI device information */
	pci = get_real_pci_device ( netdev, &need_free );
	if ( ! pci ) {
		return 0; /* Skip devices we can't identify */
	}

	/* Try to read subsystem vendor ID */
	rc = pci_read_config_word ( pci, PCI_SUBSYSTEM_VENDOR_ID, &subsys_vendor );
	if ( rc != 0 ) {
		subsys_vendor = 0x0000;
	}

	/* Try to read subsystem device ID */
	rc = pci_read_config_word ( pci, PCI_SUBSYSTEM_ID, &subsys_device );
	if ( rc != 0 ) {
		subsys_device = 0x0000;
	}

	/* Try to read revision */
	rc = pci_read_config_byte ( pci, PCI_REVISION, &revision );
	if ( rc != 0 ) {
		revision = 0x00;
	}

	/* Display PNP device path in Windows format */
	if ( ( subsys_vendor == 0x0000 ) || ( subsys_device == 0x0000 ) ||
	     ( subsys_vendor == 0xFFFF ) || ( subsys_device == 0xFFFF ) ) {
		/* No valid subsystem ID - use vendor and device ID as fallback */
		printf ( "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X\\%X&%X&%X&%X\n",
			 pci->vendor, pci->device, pci->device, pci->vendor,
			 revision, PCI_BUS ( pci->busdevfn ), PCI_SLOT ( pci->busdevfn ),
			 PCI_FUNC ( pci->busdevfn ), pci->busdevfn );
	} else {
		/* Use actual subsystem IDs */
		printf ( "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X\\%X&%X&%X&%X\n",
			 pci->vendor, pci->device, subsys_device, subsys_vendor,
			 revision, PCI_BUS ( pci->busdevfn ), PCI_SLOT ( pci->busdevfn ),
			 PCI_FUNC ( pci->busdevfn ), pci->busdevfn );
	}

	/* Free allocated PCI device structure if needed */
	if ( need_free ) {
		free ( pci );
	}

	return 0;
}

/**
 * "pnplist" command
 *
 * @v argc		Argument count
 * @v argv		Argument list
 * @ret rc		Return status code
 */
static int pnplist_exec ( int argc, char **argv ) {
	struct pnplist_options opts;
	struct net_device *netdev;
	int rc;

	/* Parse options */
	if ( ( rc = parse_options ( argc, argv, &pnplist_cmd, &opts ) ) != 0 )
		return rc;

	/* Iterate through all network devices */
	for_each_netdev ( netdev ) {
		if ( ( rc = pnplist_show_device ( netdev ) ) != 0 )
			return rc;
	}

	return 0;
}

/** PNP list command */
COMMAND ( pnplist, pnplist_exec );