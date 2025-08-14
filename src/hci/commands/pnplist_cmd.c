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
 * Display Windows-style PNP device path for a network device
 *
 * @v netdev		Network device
 * @ret rc		Return status code
 */
static int pnplist_show_device ( struct net_device *netdev ) {
	struct device *dev = netdev->dev;
	struct pci_device *pci;
	uint16_t subsys_vendor = 0x0000, subsys_device = 0x0000;
	uint8_t revision = 0x00;
	uint32_t vendor_device = 0;
	int rc;

	/* Check if this is a PCI device */
	if ( dev->desc.bus_type != BUS_TYPE_PCI ) {
		/* Skip non-PCI devices */
		return 0;
	}

	/* Get PCI device */
	pci = container_of ( dev, struct pci_device, dev );

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