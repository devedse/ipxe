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
#include <errno.h>
#include <getopt.h>
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
 * Find the actual PCI device for a network device, handling SNP abstraction
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
	int rc;
	int netdev_count;
	int current_network_device = 0;
	int target_netdev_index = 0;
	struct net_device *temp_netdev;

	/* First, try direct PCI device access */
	if ( dev->desc.bus_type == BUS_TYPE_PCI ) {
		pci = container_of ( dev, struct pci_device, dev );
		
		/* Verify this looks like a real network device */
		if ( ( pci->vendor != 0x0102 ) || ( pci->device != 0x000c ) ) {
			printf("DEBUG: Direct PCI device seems valid: %04x:%04x\n", 
			       pci->vendor, pci->device);
			return pci;
		}
		
		printf("DEBUG: Direct PCI shows abstracted device %04x:%04x, searching for real device\n",
		       pci->vendor, pci->device);
	} else {
		printf("DEBUG: Non-PCI device (%d), searching PCI bus for network controllers\n", 
		       dev->desc.bus_type);
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
	
	printf("DEBUG: Target netdev '%s' is at index %d of %d total netdevs\n", 
	       netdev->name, target_netdev_index, netdev_count);

	/* For SNP devices or abstracted devices, search the PCI bus for network controllers */
	printf("DEBUG: Searching PCI bus for network controllers...\n");
	while ( ( rc = pci_find_next ( &temp_pci, &busdevfn ) ) == 0 ) {
		/* Read class code to check if this is a network device */
		rc = pci_read_config_dword ( &temp_pci, PCI_REVISION, &class );
		if ( rc == 0 ) {
			class >>= 8; /* Remove revision byte */
			if ( ( class >> 8 ) == PCI_CLASS_NETWORK ) {
				printf("DEBUG: Found network PCI device %d: %02x:%02x.%x vendor=0x%04x device=0x%04x class=0x%06x\n",
				       current_network_device, PCI_BUS(temp_pci.busdevfn), PCI_SLOT(temp_pci.busdevfn), 
				       PCI_FUNC(temp_pci.busdevfn), temp_pci.vendor, temp_pci.device, class);
				
				/* If there's only one network device and one PCI network controller, use it */
				/* Or match by index if there are multiple */
				if ( ( netdev_count == 1 && current_network_device == 0 ) ||
				     ( current_network_device == target_netdev_index ) ) {
					printf("DEBUG: Matched netdev '%s' (index %d) to PCI device %d\n", 
					       netdev->name, target_netdev_index, current_network_device);
					
					/* Allocate a new PCI device structure to return */
					pci = malloc ( sizeof ( *pci ) );
					if ( pci ) {
						memcpy ( pci, &temp_pci, sizeof ( *pci ) );
						return pci;
					}
				}
				current_network_device++;
			}
		}
		busdevfn++;
	}

	printf("DEBUG: No matching network PCI device found for netdev '%s' (index %d)\n", 
	       netdev->name, target_netdev_index);
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
	uint32_t vendor_device = 0;
	int rc;
	int allocated_pci = 0;

	/* Find the actual PCI device */
	pci = find_actual_pci_device ( netdev );
	if ( ! pci ) {
		printf("DEBUG: No PCI device found for %s\n", netdev->name);
		return 0;
	}

	/* Check if we allocated this PCI structure (need to free it later) */
	if ( netdev->dev->desc.bus_type != BUS_TYPE_PCI ) {
		allocated_pci = 1;
	} else {
		struct pci_device *direct_pci = container_of ( netdev->dev, struct pci_device, dev );
		if ( pci != direct_pci ) {
			allocated_pci = 1;
		}
	}

	/* Debug: Read vendor/device directly from config space */
	rc = pci_read_config_dword ( pci, PCI_VENDOR_ID, &vendor_device );
	if ( rc == 0 ) {
		printf("DEBUG: Raw vendor/device from config: 0x%08x (vendor=0x%04x, device=0x%04x)\n", 
		       vendor_device, vendor_device & 0xffff, vendor_device >> 16);
	}

	printf("DEBUG: PCI device %s:\n", netdev->name);
	printf("DEBUG:   Stored vendor: 0x%04x\n", pci->vendor);
	printf("DEBUG:   Stored device: 0x%04x\n", pci->device);
	printf("DEBUG:   Bus:Dev.Fn: %02x:%02x.%x (busdevfn=0x%08x)\n", 
	       PCI_BUS(pci->busdevfn), PCI_SLOT(pci->busdevfn), PCI_FUNC(pci->busdevfn),
	       pci->busdevfn);

	/* Try to read subsystem vendor ID - ignore errors */
	rc = pci_read_config_word ( pci, PCI_SUBSYSTEM_VENDOR_ID, &subsys_vendor );
	printf("DEBUG:   Subsystem vendor read: rc=%d, value=0x%04x\n", rc, subsys_vendor);
	if ( rc != 0 ) {
		/* Failed to read subsystem vendor ID, use 0x0000 as default */
		subsys_vendor = 0x0000;
	}

	/* Try to read subsystem device ID - ignore errors */
	rc = pci_read_config_word ( pci, PCI_SUBSYSTEM_ID, &subsys_device );
	printf("DEBUG:   Subsystem device read: rc=%d, value=0x%04x\n", rc, subsys_device);
	if ( rc != 0 ) {
		/* Failed to read subsystem device ID, use 0x0000 as default */
		subsys_device = 0x0000;
	}

	/* Try to read revision - ignore errors */
	rc = pci_read_config_byte ( pci, PCI_REVISION, &revision );
	printf("DEBUG:   Revision read: rc=%d, value=0x%02x\n", rc, revision);
	if ( rc != 0 ) {
		/* Failed to read revision, use 0x00 as default */
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
 * Debug function to enumerate all PCI devices and find network controllers
 */
static void debug_enumerate_pci_devices ( void ) {
	struct pci_device pci;
	uint32_t busdevfn = 0;
	int rc;
	int count = 0;

	printf("=== All PCI Devices ===\n");
	
	/* Find all PCI devices */
	while ( ( rc = pci_find_next ( &pci, &busdevfn ) ) == 0 ) {
		count++;
		printf("PCI %02x:%02x.%x: vendor=0x%04x device=0x%04x class=0x%06x",
		       PCI_BUS(pci.busdevfn), PCI_SLOT(pci.busdevfn), PCI_FUNC(pci.busdevfn),
		       pci.vendor, pci.device, pci.class);
		
		/* Check if this is a network device */
		if ( ( pci.class >> 8 ) == PCI_CLASS_NETWORK ) {
			printf(" [NETWORK]");
		}
		printf("\n");
		
		busdevfn++;
	}
	
	printf("Total PCI devices found: %d\n\n", count);
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

	/* Debug: Enumerate all PCI devices first */
	debug_enumerate_pci_devices();

	printf("=== Network Devices ===\n");
	/* Iterate through all network devices */
	for_each_netdev ( netdev ) {
		if ( ( rc = pnplist_show_device ( netdev ) ) != 0 )
			return rc;
	}

	return 0;
}

/** PNP list command */
COMMAND ( pnplist, pnplist_exec );