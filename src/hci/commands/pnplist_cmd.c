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
 * Count the number of network devices
 *
 * @ret count		Number of network devices
 */
static int count_netdevs ( void ) {
	struct net_device *netdev;
	int count = 0;
	
	for_each_netdev ( netdev ) {
		count++;
	}
	
	return count;
}

/**
 * Detect if a PCI device has abstracted/virtualized vendor/device IDs
 * Common in SNP (Simple Network Protocol) and other virtualization environments
 * 
 * @v vendor		Vendor ID to check
 * @v device		Device ID to check  
 * @ret abstracted	True if this appears to be abstracted device info
 */
static int is_abstracted_device ( uint16_t vendor, uint16_t device ) {
	/* SNP commonly uses these abstracted IDs */
	if ( vendor == 0x0102 && device == 0x000c ) {
		return 1;
	}
	
	/* Other common virtualization abstraction patterns */
	if ( vendor == 0x1414 && device == 0x008e ) { /* Microsoft Hyper-V */
		return 1;
	}
	
	if ( vendor == 0x1af4 && device == 0x1041 ) { /* VirtIO network device (modern) */
		return 1;
	}
	
	/* Generic patterns that suggest abstraction */
	if ( vendor == 0x0001 || vendor == 0x0002 || vendor == 0x0100 ) {
		return 1;
	}
	
	return 0;
}

/**
 * Find the actual PCI device for a network device, handling SNP abstraction
 *
 * In SNP (UEFI Simple Network Protocol) environments, network devices often
 * present abstracted vendor/device IDs (like 0x0102:0x000c) instead of the
 * real underlying hardware IDs. This function detects such abstraction and
 * performs a comprehensive PCI bus scan to find the actual network controller.
 *
 * @v netdev		Network device
 * @ret pci		PCI device, or NULL if not found
 */
static struct pci_device * find_actual_pci_device ( struct net_device *netdev ) {
	struct device *dev = netdev->dev;
	struct pci_device *pci;
	struct pci_device temp_pci;
	uint32_t busdevfn = 0;
	uint32_t class;
	uint32_t vendor_device;
	int rc;
	int netdev_count;
	int current_network_device = 0;
	int target_netdev_index = 0;
	struct net_device *temp_netdev;
	int requires_enhanced_scan = 0;

	/* First, try direct PCI device access */
	if ( dev->desc.bus_type == BUS_TYPE_PCI ) {
		pci = container_of ( dev, struct pci_device, dev );
		
		printf ( "DEBUG: Direct PCI device vendor=0x%04x device=0x%04x\n", pci->vendor, pci->device );
		
		/* Check if we're getting abstracted device info (common in SNP) */
		if ( is_abstracted_device( pci->vendor, pci->device ) ) {
			printf ( "DEBUG: Detected abstracted device, will scan PCI bus\n" );
			requires_enhanced_scan = 1;
		} else {
			/* Verify the stored values match config space */
			rc = pci_read_config_dword ( pci, PCI_VENDOR_ID, &vendor_device );
			if ( rc == 0 ) {
				uint16_t config_vendor = vendor_device & 0xffff;
				uint16_t config_device = vendor_device >> 16;
				if ( config_vendor == pci->vendor && config_device == pci->device ) {
					printf ( "DEBUG: Direct device verified, using it\n" );
					return pci; /* Values match, device is valid */
				}
			}
			printf ( "DEBUG: Direct device verification failed, will scan PCI bus\n" );
			requires_enhanced_scan = 1;
		}
	} else {
		printf ( "DEBUG: Non-PCI device, will scan PCI bus\n" );
		requires_enhanced_scan = 1;
	}

	if ( ! requires_enhanced_scan ) {
		return pci;
	}

	/* Find the index of this netdev in the list */
	for_each_netdev ( temp_netdev ) {
		if ( temp_netdev == netdev ) {
			break;
		}
		target_netdev_index++;
	}
	
	/* Count total network devices */
	netdev_count = count_netdevs();
	
	printf ( "DEBUG: Enhanced scan - netdev_count=%d, target_index=%d\n", netdev_count, target_netdev_index );

	/* Enhanced PCI bus scan for network controllers */
	while ( ( rc = pci_find_next ( &temp_pci, &busdevfn ) ) == 0 ) {
		/* Read class code to check if this is a network device */
		rc = pci_read_config_dword ( &temp_pci, PCI_REVISION, &class );
		if ( rc == 0 ) {
			class >>= 8; /* Remove revision byte */
			if ( ( class >> 8 ) == PCI_CLASS_NETWORK ) {
				/* Read vendor/device from config space to ensure accuracy */
				rc = pci_read_config_dword ( &temp_pci, PCI_VENDOR_ID, &vendor_device );
				if ( rc == 0 ) {
					temp_pci.vendor = vendor_device & 0xffff;
					temp_pci.device = vendor_device >> 16;
				}
				
				printf ( "DEBUG: Found network controller #%d: vendor=0x%04x device=0x%04x\n", 
					 current_network_device, temp_pci.vendor, temp_pci.device );
				
				/* Device matching logic: for single device, use first found; for multiple, use index */
				int should_use_device = 0;
				
				if ( netdev_count == 1 ) {
					/* Single network device: use the first real network controller found */
					should_use_device = 1;
					printf ( "DEBUG: Single device - using this one\n" );
				} else {
					/* Multiple devices: match by index */
					if ( current_network_device == target_netdev_index ) {
						should_use_device = 1;
						printf ( "DEBUG: Multiple devices - index match, using this one\n" );
					}
				}
				
				if ( should_use_device ) {
					/* Allocate a new PCI device structure to return */
					pci = malloc ( sizeof ( *pci ) );
					if ( pci ) {
						memcpy ( pci, &temp_pci, sizeof ( *pci ) );
						printf ( "DEBUG: Returning enhanced scan result: vendor=0x%04x device=0x%04x\n", 
							 pci->vendor, pci->device );
						return pci;
					}
				}
				current_network_device++;
			}
		}
		busdevfn++;
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
	int allocated_pci = 0;

	/* Find the actual PCI device */
	pci = find_actual_pci_device ( netdev );
	if ( ! pci ) {
		return 0; /* Skip non-PCI devices */
	}

	/* Debug: Show what we found */
	printf ( "DEBUG: Found PCI device vendor=0x%04x device=0x%04x for netdev\n", 
		 pci->vendor, pci->device );

	/* Check if we allocated this PCI structure (need to free it later) */
	if ( netdev->dev->desc.bus_type != BUS_TYPE_PCI ) {
		allocated_pci = 1;
	} else {
		struct pci_device *direct_pci = container_of ( netdev->dev, struct pci_device, dev );
		if ( pci != direct_pci ) {
			allocated_pci = 1;
		}
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

	/* Display PNP device path */
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
	if ( allocated_pci ) {
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