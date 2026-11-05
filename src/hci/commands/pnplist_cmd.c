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
FILE_SECBOOT ( PERMITTED );

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <ipxe/netdevice.h>
#include <ipxe/pci.h>
#include <ipxe/command.h>
#include <ipxe/parseopt.h>
#include <ipxe/settings.h>
#include <ipxe/vsprintf.h>

/** @file
 *
 * PNP device path listing command
 *
 * Lists each PCI network device the way Windows identifies it, so that
 * a NIC driver can be injected into an offline Windows image for that
 * exact device: the hardware ID, the instance ID taken from the PCIe
 * Device Serial Number (when the NIC has one), and otherwise the chain
 * of PCI bridges from the root bus to the NIC, from which the host
 * computes the location-based instance ID that Windows will use.
 *
 * With --store, the result is stored as a URL-encoded query string
 * (net0_ven=0x8086&net0_dev=...).
 */

/** Maximum number of bridges between the PCI root bus and a NIC */
#define PNP_MAX_HOPS 16

/** A PCI device's Windows hardware-ID fields */
struct pnp_hop {
	/** Vendor ID */
	uint16_t vendor;
	/** Device ID */
	uint16_t device;
	/** Subsystem vendor ID */
	uint16_t subsys_vendor;
	/** Subsystem ID */
	uint16_t subsys_device;
	/** Revision */
	uint8_t revision;
	/** Device and function number: (slot << 3) | func */
	uint8_t devfn;
};

/** What pnplist reports about one network device */
struct pnp_nic {
	/** Network device */
	struct net_device *netdev;
	/** PCI bus:dev.fn address */
	unsigned int busdevfn;
	/** Hardware-ID fields */
	struct pnp_hop id;
	/** Instance ID from the Device Serial Number, or empty */
	char instance[24];
	/** Bridge chain ends at the first PCI root */
	int chain_complete;
	/** Number of bridges in the chain */
	unsigned int nbridges;
	/** Bridges, from the one on the root bus to the NIC's parent */
	struct pnp_hop bridges[PNP_MAX_HOPS];
};

/** "pnplist" options */
struct pnplist_options {
	/** Variable name to store output */
	char *store;
};

/** "pnplist" option list */
static struct option_descriptor pnplist_opts[] = {
	OPTION_DESC ( "store", 's', required_argument,
		      struct pnplist_options, store, parse_string ),
};

/** "pnplist" command descriptor */
static struct command_descriptor pnplist_cmd =
	COMMAND_DESC ( struct pnplist_options, pnplist_opts, 0, 0,
		       "[--store <variable>]" );

/**
 * Get the PCI device underlying a network device
 *
 * The network device's generic device is a struct pci_device only
 * when one of iPXE's own PCI drivers drives the NIC.  When the NIC is
 * driven through the firmware's SNP, NII or MNP protocols, it is a
 * plain struct device that carries a copy of the PCI device's
 * description.  Both record the PCI location, so read the PCI device
 * from there instead of assuming a struct pci_device.
 *
 * @v netdev		Network device
 * @v pci		PCI device to fill in
 * @ret rc		Return status code
 */
static int pnp_get_pci_device ( struct net_device *netdev,
				struct pci_device *pci ) {
	struct device_description *desc = &netdev->dev->desc;
	int rc;

	printf ( "DEBUG: Getting PCI device for netdev '%s'\n", netdev->name );
	if ( desc->bus_type != BUS_TYPE_PCI ) {
		printf ( "DEBUG: Device bus_type is not PCI (bus_type=%d)\n",
			 desc->bus_type );
		return -ENODEV;
	}

	memset ( pci, 0, sizeof ( *pci ) );
	pci_init ( pci, desc->location );
	if ( ( rc = pci_read_config ( pci ) ) != 0 ) {
		printf ( "DEBUG: No PCI device at %04x:%02x:%02x.%x\n",
			 PCI_SEG ( pci->busdevfn ), PCI_BUS ( pci->busdevfn ),
			 PCI_SLOT ( pci->busdevfn ), PCI_FUNC ( pci->busdevfn ) );
		return rc;
	}
	printf ( "DEBUG: PCI device found - vendor=0x%04x device=0x%04x bus=%d slot=%d func=%d\n", pci->vendor, pci->device, PCI_BUS ( pci->busdevfn ), PCI_SLOT ( pci->busdevfn ), PCI_FUNC ( pci->busdevfn ) );

	return 0;
}

/**
 * Read a device's subsystem IDs, honouring the header type
 *
 * Type-0 (normal) devices carry the subsystem IDs at config 0x2c/0x2e.
 * Type-1 (PCI-to-PCI bridge) devices do NOT; Windows takes a bridge's
 * subsystem from the Subsystem ID / Subsystem Vendor ID capability
 * (cap 0x0d), or uses 0x0000/0x0000 when that capability is absent.
 * Getting this right matters: the host hashes the bridge's full hardware
 * id, so a wrong subsystem here silently breaks the child's instance ID.
 *
 * @v pci		PCI device
 * @v ssv		Subsystem vendor ID to fill in
 * @v ssd		Subsystem ID to fill in
 */
static void pnp_read_subsys ( struct pci_device *pci, uint16_t *ssv,
			      uint16_t *ssd ) {
	int cap;

	*ssv = 0x0000;
	*ssd = 0x0000;

	if ( ( pci->hdrtype & PCI_HEADER_TYPE_MASK ) ==
	     PCI_HEADER_TYPE_NORMAL ) {
		pci_read_config_word ( pci, PCI_SUBSYSTEM_VENDOR_ID, ssv );
		pci_read_config_word ( pci, PCI_SUBSYSTEM_ID, ssd );
	} else if ( ( cap = pci_find_capability ( pci,
						  PCI_CAP_ID_SSVID ) ) ) {
		pci_read_config_word ( pci, ( cap + 0x04 ), ssv );
		pci_read_config_word ( pci, ( cap + 0x06 ), ssd );
	}

	/* Normalise "no device" (0xffff) to 0x0000, matching Windows */
	if ( *ssv == 0xffff )
		*ssv = 0x0000;
	if ( *ssd == 0xffff )
		*ssd = 0x0000;
}

/**
 * Read a PCI device's hardware-ID fields
 *
 * @v pci		PCI device
 * @v hop		Hardware-ID fields to fill in
 * @ret rc		Status of reading the revision (0x00 if it failed)
 */
static int pnp_fill_hop ( struct pci_device *pci, struct pnp_hop *hop ) {
	int rc;

	hop->vendor = pci->vendor;
	hop->device = pci->device;
	if ( ( rc = pci_read_config_byte ( pci, PCI_REVISION,
					   &hop->revision ) ) != 0 )
		hop->revision = 0x00;
	pnp_read_subsys ( pci, &hop->subsys_vendor, &hop->subsys_device );
	hop->devfn = ( pci->busdevfn & 0xff );

	return rc;
}

/**
 * Find the PCI-to-PCI bridge whose secondary bus number matches a target bus
 *
 * This is the immediate parent bridge of any device on @c target_bus.
 * Bus numbers are only unique within a PCI segment, so only bridges in
 * the same segment are considered.
 *
 * @v segment		PCI segment of the target bus
 * @v target_bus	Secondary bus number to match
 * @v out		PCI device to fill with the bridge on success
 * @ret found		1 if a parent bridge was found, 0 otherwise
 */
static int pnp_find_parent_bridge ( unsigned int segment,
				    unsigned int target_bus,
				    struct pci_device *out ) {
	uint32_t busdevfn = PCI_BUSDEVFN ( segment, 0, 0, 0 );
	uint8_t sec;

	while ( ( pci_find_next ( out, &busdevfn ) == 0 ) &&
		( PCI_SEG ( busdevfn ) == segment ) ) {
		if ( ( ( out->hdrtype & PCI_HEADER_TYPE_MASK ) ==
		       PCI_HEADER_TYPE_BRIDGE ) &&
		     ( pci_read_config_byte ( out, PCI_SECONDARY, &sec ) == 0 ) &&
		     ( sec == target_bus ) ) {
			return 1;
		}
		busdevfn++;
	}
	return 0;
}

/**
 * Read the instance ID from a NIC's PCIe Device Serial Number
 *
 * When present, Windows uses the DSN as the device instance ID suffix,
 * formatted as the upper dword followed by the lower dword (each as 8
 * uppercase hex digits) followed by "00".  This is the machine-specific
 * value needed for offline driver injection.  When the DSN is absent,
 * Windows falls back to a location-based instance ID, which the host
 * computes from the bridge chain.
 *
 * @v pci		PCI device
 * @v nic		NIC report to fill in
 */
static void pnp_read_dsn ( struct pci_device *pci, struct pnp_nic *nic ) {
	uint32_t dsn_lo = 0;
	uint32_t dsn_hi = 0;
	int dsn_pos;

	dsn_pos = pci_find_ext_capability ( pci, PCI_EXT_CAP_DSN );
	if ( dsn_pos ) {
		pci_read_config_dword ( pci, ( dsn_pos + PCI_DSN_LOWER ),
					&dsn_lo );
		pci_read_config_dword ( pci, ( dsn_pos + PCI_DSN_UPPER ),
					&dsn_hi );
		/* Treat an all-zero or all-ones serial number as absent */
		if ( ! ( ( ( dsn_lo == 0x00000000 ) &&
			   ( dsn_hi == 0x00000000 ) ) ||
			 ( ( dsn_lo == 0xffffffff ) &&
			   ( dsn_hi == 0xffffffff ) ) ) ) {
			snprintf ( nic->instance, sizeof ( nic->instance ),
				   "%08X%08X00", dsn_hi, dsn_lo );
		}
	}
	printf ( "DEBUG: DSN ext cap @0x%x present=%d hi=0x%08x lo=0x%08x "
		 "instance=%s\n", dsn_pos, ( nic->instance[0] != '\0' ),
		 dsn_hi, dsn_lo, nic->instance );
}

/**
 * Walk the PCI bridges from a NIC up to the root bus
 *
 * The host needs the NIC's bridge chain to reconstruct the no-DSN,
 * location-based Windows instance ID offline.  The walk follows each
 * bus's parent bridge (the bridge whose secondary bus it is) up to bus
 * 0, and records only the bridges: the NIC itself is already described
 * by its own fields.  Each bridge carries its Windows hardware-id fields
 * plus its own devfn byte; the host assembles and hashes these.
 *
 * The chain is complete when it ends at bus 0 of segment 0: the host
 * computes the ID for the first PCI root (ACPI\PNP0A08\0).  A topology
 * gap, or a chain that ends at another PCI root, leaves it incomplete,
 * so that the host keeps the driver package's instance ID rather than
 * computing a wrong one.  A complete chain may have zero bridges, when
 * the NIC sits on the root bus itself.  An incomplete chain still lists
 * the bridges that were found.
 *
 * @v pci		PCI device of the NIC
 * @v nic		NIC report to fill in
 */
static void pnp_walk_chain ( struct pci_device *pci, struct pnp_nic *nic ) {
	struct pnp_hop hops[PNP_MAX_HOPS];
	struct pci_device parent;
	unsigned int segment = PCI_SEG ( pci->busdevfn );
	unsigned int bus = PCI_BUS ( pci->busdevfn );
	unsigned int count = 0;
	unsigned int i;

	/* Walk up, collecting the bridges from the NIC's parent upwards */
	while ( 1 ) {
		if ( bus == 0 ) {
			nic->chain_complete = ( segment == 0 );
			break;
		}
		if ( count >= PNP_MAX_HOPS )
			break;
		if ( ! pnp_find_parent_bridge ( segment, bus, &parent ) )
			break;
		pnp_fill_hop ( &parent, &hops[count++] );
		bus = PCI_BUS ( parent.busdevfn );
	}

	/* Record the chain from the root bus downwards */
	for ( i = 0 ; i < count ; i++ )
		nic->bridges[i] = hops[ count - 1 - i ];
	nic->nbridges = count;
}

/**
 * Read what pnplist reports about a network device
 *
 * @v netdev		Network device
 * @v nic		NIC report to fill in
 * @ret rc		Return status code (-ENODEV if not a PCI device)
 */
static int pnp_read_nic ( struct net_device *netdev, struct pnp_nic *nic ) {
	struct pci_device pci;
	int rc;

	memset ( nic, 0, sizeof ( *nic ) );
	nic->netdev = netdev;

	printf ( "\nDEBUG: ==== Processing device '%s' ====\n", netdev->name );

	/* Get the underlying PCI device */
	if ( ( rc = pnp_get_pci_device ( netdev, &pci ) ) != 0 ) {
		printf ( "DEBUG: Could not identify PCI device for '%s', skipping\n" , netdev->name );
		return rc;
	}
	nic->busdevfn = pci.busdevfn;

	/* Read the hardware-ID fields */
	rc = pnp_fill_hop ( &pci, &nic->id );
	printf ( "DEBUG: Read subsystem IDs: subsys_vendor=0x%04x subsys_device=0x%04x\n",
		 nic->id.subsys_vendor, nic->id.subsys_device );
	printf ( "DEBUG: Read revision: rc=%d value=0x%02x\n", rc, nic->id.revision );

	/* Read the instance ID, if the NIC has a serial number */
	pnp_read_dsn ( &pci, nic );

	printf ( "DEBUG: Final values - vendor=0x%04x device=0x%04x subsys_vendor=0x%04x subsys_device=0x%04x rev=0x%02x\n", nic->id.vendor, nic->id.device, nic->id.subsys_vendor, nic->id.subsys_device, nic->id.revision );
	if ( ( nic->id.subsys_vendor == 0x0000 ) ||
	     ( nic->id.subsys_device == 0x0000 ) ) {
		/* No valid subsystem ID - use vendor and device ID as
		 * fallback.
		 */
		printf ( "DEBUG: Using fallback subsystem IDs (invalid subsystem)\n" );
		nic->id.subsys_vendor = nic->id.vendor;
		nic->id.subsys_device = nic->id.device;
	} else {
		printf ( "DEBUG: Using actual subsystem IDs\n" );
	}

	/* Read the bridge chain */
	pnp_walk_chain ( &pci, nic );

	return 0;
}

/**
 * Print a NIC report to the console
 *
 * Windows only takes the instance ID from the device itself when the
 * NIC has a PCIe Device Serial Number.  Otherwise it derives the ID
 * from the NIC's location (see the bridge chain), which cannot be
 * shown here.
 *
 * @v nic		NIC report
 */
static void pnp_print_nic ( struct pnp_nic *nic ) {
	struct pnp_hop *hop;
	unsigned int i;

	printf ( "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X",
		 nic->id.vendor, nic->id.device, nic->id.subsys_device,
		 nic->id.subsys_vendor, nic->id.revision );
	if ( nic->instance[0] ) {
		printf ( "\\%s\n", nic->instance );
	} else {
		printf ( " at %02x:%02x.%x (no serial number, so the instance "
			 "ID depends on the PCI location)\n",
			 PCI_BUS ( nic->busdevfn ), PCI_SLOT ( nic->busdevfn ),
			 PCI_FUNC ( nic->busdevfn ) );
	}

	printf ( "DEBUG: %s instance-id chain: complete=%d, %d bridge(s) "
		 "root->parent (NIC excluded)\n", nic->netdev->name,
		 nic->chain_complete, nic->nbridges );
	for ( i = 0 ; i < nic->nbridges ; i++ ) {
		hop = &nic->bridges[i];
		printf ( "  bridge[%d] PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X devfn=%02X\n",
			 i, hop->vendor, hop->device, hop->subsys_device,
			 hop->subsys_vendor, hop->revision, hop->devfn );
	}
}

/**
 * Format NIC reports as a URL-encoded query string
 *
 * Per NIC: netN_ven, _dev, _subsys_ven, _subsys_dev, _rev, _bus_loc,
 * _dsn_present, _instance (the ready-to-use instance ID suffix when a
 * DSN is present), _chain_complete, and per bridge K (0 = the bridge on
 * the root bus) netN_chainK_ven, _dev, _subsys_ven, _subsys_dev, _rev
 * and _devfn.
 *
 * As with ssnprintf(), call this with a NULL buffer and zero size to
 * find the length needed, then again to fill in the buffer.
 *
 * @v buf		Buffer to fill in
 * @v size		Size of buffer
 * @v nics		NIC reports
 * @v count		Number of NIC reports
 * @ret len		Length of formatted string
 */
static size_t pnp_format_nics ( char *buf, size_t size,
				struct pnp_nic *nics, unsigned int count ) {
	struct pnp_nic *nic;
	struct pnp_hop *hop;
	const char *name;
	size_t len = 0;
	unsigned int i;
	unsigned int j;

	for ( i = 0 ; i < count ; i++ ) {
		nic = &nics[i];
		name = nic->netdev->name;
		len += ssnprintf ( ( buf + len ), ( size - len ),
				   "%s%s_ven=0x%04x&%s_dev=0x%04x"
				   "&%s_subsys_ven=0x%04x&%s_subsys_dev=0x%04x"
				   "&%s_rev=0x%02x&%s_bus_loc=%02x:%02x.%x"
				   "&%s_dsn_present=%d&%s_instance=%s"
				   "&%s_chain_complete=%d",
				   ( i ? "&" : "" ),
				   name, nic->id.vendor, name, nic->id.device,
				   name, nic->id.subsys_vendor,
				   name, nic->id.subsys_device,
				   name, nic->id.revision,
				   name, PCI_BUS ( nic->busdevfn ),
				   PCI_SLOT ( nic->busdevfn ),
				   PCI_FUNC ( nic->busdevfn ),
				   name, ( nic->instance[0] != '\0' ),
				   name, nic->instance,
				   name, nic->chain_complete );
		for ( j = 0 ; j < nic->nbridges ; j++ ) {
			hop = &nic->bridges[j];
			len += ssnprintf ( ( buf + len ), ( size - len ),
					   "&%s_chain%d_ven=0x%04x"
					   "&%s_chain%d_dev=0x%04x"
					   "&%s_chain%d_subsys_ven=0x%04x"
					   "&%s_chain%d_subsys_dev=0x%04x"
					   "&%s_chain%d_rev=0x%02x"
					   "&%s_chain%d_devfn=%02x",
					   name, j, hop->vendor,
					   name, j, hop->device,
					   name, j, hop->subsys_vendor,
					   name, j, hop->subsys_device,
					   name, j, hop->revision,
					   name, j, hop->devfn );
		}
	}
	return len;
}

/**
 * Store NIC reports in a setting
 *
 * @v name		Setting name
 * @v nics		NIC reports
 * @v count		Number of NIC reports
 * @ret rc		Return status code
 */
static int pnp_store_nics ( char *name, struct pnp_nic *nics,
			    unsigned int count ) {
	struct named_setting setting;
	size_t len;
	char *output;
	int rc;

	/* Parse setting name */
	if ( ( rc = parse_autovivified_setting ( name, &setting ) ) != 0 ) {
		printf ( "Could not parse setting name \"%s\": %s\n",
			 name, strerror ( rc ) );
		goto err_parse;
	}

	/* Apply default type if necessary */
	if ( ! setting.setting.type )
		setting.setting.type = &setting_type_string;

	/* Format the reports */
	len = pnp_format_nics ( NULL, 0, nics, count );
	output = malloc ( len + 1 /* NUL */ );
	if ( ! output ) {
		rc = -ENOMEM;
		goto err_alloc;
	}
	pnp_format_nics ( output, ( len + 1 ), nics, count );

	/* Store setting */
	printf ( "\nDEBUG: Storing %d bytes to variable '%s'\n",
		 ( ( int ) len ), name );
	if ( ( rc = storef_setting ( setting.settings, &setting.setting,
				     output ) ) != 0 ) {
		printf ( "Could not store to variable \"%s\": %s\n",
			 name, strerror ( rc ) );
		goto err_store;
	}

 err_store:
	free ( output );
 err_alloc:
 err_parse:
	return rc;
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
	struct pnp_nic *nics;
	unsigned int max = 0;
	unsigned int count = 0;
	int rc;

	/* Parse options */
	if ( ( rc = parse_options ( argc, argv, &pnplist_cmd, &opts ) ) != 0 )
		goto err_parse;

	/* Allocate a report for every network device */
	for_each_netdev ( netdev )
		max++;
	nics = calloc ( ( max ? max : 1 ), sizeof ( nics[0] ) );
	if ( ! nics ) {
		rc = -ENOMEM;
		goto err_alloc;
	}

	/* Read every PCI network device, skipping all others */
	for_each_netdev ( netdev ) {
		if ( pnp_read_nic ( netdev, &nics[count] ) != 0 )
			continue;
		if ( ! opts.store )
			pnp_print_nic ( &nics[count] );
		count++;
	}

	/* Store the reports if requested */
	if ( opts.store &&
	     ( ( rc = pnp_store_nics ( opts.store, nics, count ) ) != 0 ) )
		goto err_store;

 err_store:
	free ( nics );
 err_alloc:
 err_parse:
	return rc;
}

/** PNP list command */
COMMAND ( pnplist, pnplist_exec );
