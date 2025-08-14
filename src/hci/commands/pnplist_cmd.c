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
	uint16_t subsys_vendor, subsys_device;
	uint8_t revision;
	int rc;

	/* Check if this is a PCI device */
	if ( dev->desc.bus_type != BUS_TYPE_PCI ) {
		/* Skip non-PCI devices */
		return 0;
	}

	/* Get PCI device */
	pci = container_of ( dev, struct pci_device, dev );

	/* Read subsystem vendor ID */
	if ( ( rc = pci_read_config_word ( pci, PCI_SUBSYSTEM_VENDOR_ID,
					   &subsys_vendor ) ) != 0 ) {
		printf ( "Error reading subsystem vendor ID for %s: %s\n",
			 netdev->name, strerror ( rc ) );
		return rc;
	}

	/* Read subsystem device ID */
	if ( ( rc = pci_read_config_word ( pci, PCI_SUBSYSTEM_ID,
					   &subsys_device ) ) != 0 ) {
		printf ( "Error reading subsystem device ID for %s: %s\n",
			 netdev->name, strerror ( rc ) );
		return rc;
	}

	/* Read revision */
	if ( ( rc = pci_read_config_byte ( pci, PCI_REVISION,
					   &revision ) ) != 0 ) {
		printf ( "Error reading revision for %s: %s\n",
			 netdev->name, strerror ( rc ) );
		return rc;
	}

	/* Display PNP device path */
	if ( ( subsys_vendor == 0x0000 ) && ( subsys_device == 0x0000 ) ) {
		/* No subsystem ID - use vendor and device ID as fallback */
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