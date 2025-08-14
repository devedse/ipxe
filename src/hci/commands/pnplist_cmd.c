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
 * Try to access the underlying PCI device through EFI PCI I/O protocol
 * 
 * @v netdev		Network device
 * @v subsys_vendor	Subsystem vendor ID output
 * @v subsys_device	Subsystem device ID output
 * @v revision		Revision output
 * @ret pci		PCI device with real information, or NULL if not found
 */
static struct pci_device * try_efi_pci_access ( struct net_device *netdev,
						uint16_t *subsys_vendor,
						uint16_t *subsys_device,
						uint8_t *revision ) {
	/* Initialize outputs */
	*subsys_vendor = 0x0000;
	*subsys_device = 0x0000;
	*revision = 0x00;

	/* EFI PCI I/O protocol access temporarily disabled due to compilation issues */
	/* Will be re-enabled once EFI compilation environment is properly configured */
	printf("DEBUG: EFI PCI I/O protocol access not available in this build\n");
	( void ) netdev;
	return NULL;
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
	uint16_t dummy_subsys_vendor, dummy_subsys_device;
	uint8_t dummy_revision;

	/* First, try direct PCI device access */
	if ( dev->desc.bus_type == BUS_TYPE_PCI ) {
		pci = container_of ( dev, struct pci_device, dev );
		
		/* Check if we're getting abstracted device info (common in SNP) */
		if ( ( pci->vendor == 0x0102 ) && ( pci->device == 0x000c ) ) {
			printf("DEBUG: Direct PCI shows abstracted device %04x:%04x (likely SNP), searching for real device\n",
			       pci->vendor, pci->device);
		} else {
			printf("DEBUG: Direct PCI device seems valid: %04x:%04x\n", 
			       pci->vendor, pci->device);
			return pci;
		}
	} else {
		printf("DEBUG: Non-PCI device (%d), trying EFI PCI I/O protocol first\n", 
		       dev->desc.bus_type);
		
		/* Try EFI PCI I/O protocol first for SNP devices */
		pci = try_efi_pci_access ( netdev, &dummy_subsys_vendor, &dummy_subsys_device, &dummy_revision );
		if ( pci ) {
			printf("DEBUG: EFI PCI I/O protocol succeeded\n");
			return pci;
		}
		printf("DEBUG: EFI PCI I/O protocol failed, falling back to enhanced PCI bus scan\n");
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

	/* Enhanced PCI bus scan for network controllers */
	printf("DEBUG: Performing enhanced PCI bus scan for network controllers...\n");
	while ( ( rc = pci_find_next ( &temp_pci, &busdevfn ) ) == 0 ) {
		/* Read class code to check if this is a network device */
		rc = pci_read_config_dword ( &temp_pci, PCI_REVISION, &class );
		if ( rc == 0 ) {
			class >>= 8; /* Remove revision byte */
			if ( ( class >> 8 ) == PCI_CLASS_NETWORK ) {
				printf("DEBUG: Found network PCI device %d: %02x:%02x.%x vendor=0x%04x device=0x%04x class=0x%06x",
				       current_network_device, PCI_BUS(temp_pci.busdevfn), PCI_SLOT(temp_pci.busdevfn), 
				       PCI_FUNC(temp_pci.busdevfn), temp_pci.vendor, temp_pci.device, class);

				/* Special handling for common virtualized network devices */
				if ( temp_pci.vendor == 0x1af4 && temp_pci.device == 0x1000 ) {
					printf(" [VIRTIO-NET]");
				} else if ( temp_pci.vendor == 0x8086 ) {
					printf(" [INTEL]");
				} else if ( temp_pci.vendor == 0x10ec ) {
					printf(" [REALTEK]");
				}
				printf("\n");
				
				/* Enhanced device matching logic */
				int should_use_device = 0;
				
				/* If there's only one network device and one PCI network controller, use it */
				if ( netdev_count == 1 && current_network_device == 0 ) {
					should_use_device = 1;
					printf("DEBUG: Single network device mapping: using only available PCI network controller\n");
				}
				/* If we're specifically looking for a virtio device and found one */
				else if ( temp_pci.vendor == 0x1af4 && temp_pci.device == 0x1000 ) {
					should_use_device = 1;
					printf("DEBUG: Found virtio-net device, likely target for SNP abstraction\n");
				}
				/* Otherwise, match by index */
				else if ( current_network_device == target_netdev_index ) {
					should_use_device = 1;
					printf("DEBUG: Index-based matching: device %d matches netdev index %d\n",
					       current_network_device, target_netdev_index);
				}
				
				if ( should_use_device ) {
					printf("DEBUG: Matched netdev '%s' (index %d) to PCI device %d (vendor=0x%04x device=0x%04x)\n", 
					       netdev->name, target_netdev_index, current_network_device,
					       temp_pci.vendor, temp_pci.device);
					
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
	int got_subsys_from_efi = 0;

	/* Find the actual PCI device */
	pci = find_actual_pci_device ( netdev );
	if ( ! pci ) {
		printf("DEBUG: No PCI device found for %s\n", netdev->name);
		return 0;
	}

	/* Check if we allocated this PCI structure (need to free it later) */
	if ( netdev->dev->desc.bus_type != BUS_TYPE_PCI ) {
		allocated_pci = 1;
		
		/* Try to get EFI subsystem data if this is an EFI device */
		if ( netdev->dev->desc.bus_type == BUS_TYPE_EFI ) {
			struct pci_device *efi_pci = try_efi_pci_access ( netdev, &subsys_vendor, &subsys_device, &revision );
			if ( efi_pci ) {
				got_subsys_from_efi = 1;
				printf("DEBUG: Using subsystem info from EFI: vendor=0x%04x device=0x%04x revision=0x%02x\n",
				       subsys_vendor, subsys_device, revision);
				/* Free the temporary EFI PCI device since we already have the main one */
				if ( efi_pci != pci ) {
					free ( efi_pci );
				}
			}
		}
	} else {
		struct pci_device *direct_pci = container_of ( netdev->dev, struct pci_device, dev );
		if ( pci != direct_pci ) {
			allocated_pci = 1;
		}
	}

	/* Debug: Read vendor/device directly from config space if not from EFI */
	if ( ! got_subsys_from_efi ) {
		rc = pci_read_config_dword ( pci, PCI_VENDOR_ID, &vendor_device );
		if ( rc == 0 ) {
			printf("DEBUG: Raw vendor/device from config: 0x%08x (vendor=0x%04x, device=0x%04x)\n", 
			       vendor_device, vendor_device & 0xffff, vendor_device >> 16);
		}
	}

	printf("DEBUG: PCI device %s:\n", netdev->name);
	printf("DEBUG:   Stored vendor: 0x%04x\n", pci->vendor);
	printf("DEBUG:   Stored device: 0x%04x\n", pci->device);
	printf("DEBUG:   Bus:Dev.Fn: %02x:%02x.%x (busdevfn=0x%08x)\n", 
	       PCI_BUS(pci->busdevfn), PCI_SLOT(pci->busdevfn), PCI_FUNC(pci->busdevfn),
	       pci->busdevfn);

	/* Try to read subsystem vendor ID if not already obtained from EFI */
	if ( ! got_subsys_from_efi ) {
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