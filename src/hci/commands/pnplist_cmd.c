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
 * Scan PCI bus for network controllers and return the one at specified index
 *
 * @v target_index	Index of network controller to return (0-based)
 * @ret pci		PCI device structure, or NULL if not found
 */
static struct pci_device * scan_pci_network_controllers ( int target_index ) {
	struct pci_device temp_pci;
	struct pci_device *result_pci;
	uint32_t busdevfn = 0;
	uint32_t class;
	uint32_t vendor_device;
	int current_index = 0;
	int rc;

	/* Scan all PCI devices looking for network controllers */
	while ( ( rc = pci_find_next ( &temp_pci, &busdevfn ) ) == 0 ) {
		/* Read class code to check if this is a network device */
		rc = pci_read_config_dword ( &temp_pci, PCI_REVISION, &class );
		if ( rc == 0 ) {
			class >>= 8; /* Remove revision byte */
			if ( ( class >> 8 ) == PCI_CLASS_NETWORK ) {
				/* Read vendor/device from config space */
				rc = pci_read_config_dword ( &temp_pci, PCI_VENDOR_ID, &vendor_device );
				if ( rc == 0 ) {
					temp_pci.vendor = vendor_device & 0xffff;
					temp_pci.device = vendor_device >> 16;
					
					/* Skip invalid device IDs */
					if ( temp_pci.vendor == 0x0000 || temp_pci.vendor == 0xffff ||
					     temp_pci.device == 0x0000 || temp_pci.device == 0xffff ) {
						busdevfn++;
						continue;
					}
					
					printf ( "DEBUG: Found network controller #%d: vendor=0x%04x device=0x%04x at %02x:%02x.%x\n", 
						 current_index, temp_pci.vendor, temp_pci.device,
						 PCI_BUS ( temp_pci.busdevfn ), PCI_SLOT ( temp_pci.busdevfn ), 
						 PCI_FUNC ( temp_pci.busdevfn ) );
					
					if ( current_index == target_index ) {
						/* Allocate and return this device */
						result_pci = malloc ( sizeof ( *result_pci ) );
						if ( result_pci ) {
							memcpy ( result_pci, &temp_pci, sizeof ( *result_pci ) );
							printf ( "DEBUG: Returning PCI device at index %d\n", target_index );
							return result_pci;
						}
					}
					current_index++;
				}
			}
		}
		busdevfn++;
	}
	
	return NULL;
}

/**
 * Get the actual PCI device information for a network device
 *
 * In SNP builds, network devices often present abstracted vendor/device IDs 
 * instead of the real hardware. This function detects abstraction and uses
 * PCI bus scanning to find the actual underlying network controller.
 *
 * @v netdev		Network device
 * @v device_index	Index of this device in the network device list
 * @ret pci		PCI device, or NULL if not found
 * @ret need_free	Set to 1 if returned PCI device should be freed
 */
static struct pci_device * get_real_pci_device ( struct net_device *netdev, 
						  int device_index, int *need_free ) {
	struct device *dev = netdev->dev;
	struct pci_device *pci = NULL;
	
	*need_free = 0;
	
	/* Try direct PCI device access first */
	if ( dev->desc.bus_type == BUS_TYPE_PCI ) {
		pci = container_of ( dev, struct pci_device, dev );
		
		printf ( "DEBUG: Direct PCI access - vendor=0x%04x device=0x%04x\n", 
			 pci->vendor, pci->device );
		
		/* Check if we have abstracted device information */
		if ( is_snp_abstracted ( pci->vendor, pci->device ) ) {
			printf ( "DEBUG: Detected SNP abstraction, scanning PCI bus\n" );
			
			/* Use PCI bus scan to find real device */
			pci = scan_pci_network_controllers ( device_index );
			if ( pci ) {
				*need_free = 1;
				printf ( "DEBUG: Found real device via PCI scan: vendor=0x%04x device=0x%04x\n",
					 pci->vendor, pci->device );
				return pci;
			} else {
				printf ( "DEBUG: PCI scan failed, falling back to abstracted values\n" );
				/* Fall back to original device */
				pci = container_of ( dev, struct pci_device, dev );
				*need_free = 0;
			}
		}
		
		return pci;
	}
	
	/* Non-PCI device - try PCI bus scan */
	printf ( "DEBUG: Non-PCI device, attempting PCI bus scan\n" );
	pci = scan_pci_network_controllers ( device_index );
	if ( pci ) {
		*need_free = 1;
		printf ( "DEBUG: Found device via PCI scan: vendor=0x%04x device=0x%04x\n",
			 pci->vendor, pci->device );
	}
	
	return pci;
}

/**
 * Display Windows-style PNP device path for a network device
 *
 * @v netdev		Network device
 * @v device_index	Index of this network device
 * @ret rc		Return status code
 */
static int pnplist_show_device ( struct net_device *netdev, int device_index ) {
	struct pci_device *pci;
	uint16_t subsys_vendor = 0x0000, subsys_device = 0x0000;
	uint8_t revision = 0x00;
	int rc;
	int need_free = 0;

	/* Get the real PCI device information */
	pci = get_real_pci_device ( netdev, device_index, &need_free );
	if ( ! pci ) {
		return 0; /* Skip devices we can't identify */
	}

	printf ( "DEBUG: Using PCI device vendor=0x%04x device=0x%04x (need_free=%d)\n", 
		 pci->vendor, pci->device, need_free );

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
	int device_index = 0;
	int rc;

	/* Parse options */
	if ( ( rc = parse_options ( argc, argv, &pnplist_cmd, &opts ) ) != 0 )
		return rc;

	/* Iterate through all network devices */
	for_each_netdev ( netdev ) {
		if ( ( rc = pnplist_show_device ( netdev, device_index ) ) != 0 )
			return rc;
		device_index++;
	}

	return 0;
}

/** PNP list command */
COMMAND ( pnplist, pnplist_exec );