FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );
FILE_SECBOOT ( PERMITTED );

#include <stdint.h>
#include <ipxe/timer.h>
#include <ipxe/pci.h>
#include <ipxe/pcibackup.h>

static int pci_find_capability_common ( struct pci_device *pci,
					uint8_t pos, int cap ) {
	uint8_t id;
	int ttl = 48;

	while ( ttl-- && pos >= 0x40 ) {
		pos &= ~3;
		pci_read_config_byte ( pci, pos + PCI_CAP_ID, &id );
		DBG ( "PCI Capability: %d\n", id );
		if ( id == 0xff )
			break;
		if ( id == cap )
			return pos;
		pci_read_config_byte ( pci, pos + PCI_CAP_NEXT, &pos );
	}
	return 0;
}

/**
 * Look for a PCI capability
 *
 * @v pci		PCI device to query
 * @v cap		Capability code
 * @ret address		Address of capability, or 0 if not found
 *
 * Determine whether or not a device supports a given PCI capability.
 * Returns the address of the requested capability structure within
 * the device's PCI configuration space, or 0 if the device does not
 * support it.
 */
int pci_find_capability ( struct pci_device *pci, int cap ) {
	uint16_t status;
	uint8_t pos;
	uint8_t hdr_type;

	pci_read_config_word ( pci, PCI_STATUS, &status );
	if ( ! ( status & PCI_STATUS_CAP_LIST ) )
		return 0;

	pci_read_config_byte ( pci, PCI_HEADER_TYPE, &hdr_type );
	switch ( hdr_type & PCI_HEADER_TYPE_MASK ) {
	case PCI_HEADER_TYPE_NORMAL:
	case PCI_HEADER_TYPE_BRIDGE:
	default:
		pci_read_config_byte ( pci, PCI_CAPABILITY_LIST, &pos );
		break;
	case PCI_HEADER_TYPE_CARDBUS:
		pci_read_config_byte ( pci, PCI_CB_CAPABILITY_LIST, &pos );
		break;
	}
	return pci_find_capability_common ( pci, pos, cap );
}

/**
 * Look for another PCI capability
 *
 * @v pci		PCI device to query
 * @v pos		Address of the current capability
 * @v cap		Capability code
 * @ret address		Address of capability, or 0 if not found
 *
 * Determine whether or not a device supports a given PCI capability
 * starting the search at a given address within the device's PCI
 * configuration space. Returns the address of the next capability
 * structure within the device's PCI configuration space, or 0 if the
 * device does not support another such capability.
 */
int pci_find_next_capability ( struct pci_device *pci, int pos, int cap ) {
	uint8_t new_pos;

	pci_read_config_byte ( pci, pos + PCI_CAP_NEXT, &new_pos );
	return pci_find_capability_common ( pci, new_pos, cap );
}

/**
 * Look for a PCI Express extended capability
 *
 * @v pci		PCI device to query
 * @v cap		Extended capability code
 * @ret address		Address of capability, or 0 if not found
 *
 * Walk the PCI Express extended capability list (which begins at
 * configuration space offset 0x100) and return the address of the
 * requested extended capability structure, or 0 if the device does
 * not support it.  This requires access to extended configuration
 * space, which is available via ECAM or the EFI PCI I/O protocols.
 */
int pci_find_ext_capability ( struct pci_device *pci, int cap ) {
	uint32_t header;
	int pos = PCI_EXT_CAPABILITY_LIST;
	/* Each extended capability is at least 8 bytes long, so this
	 * bounds the number of list entries we are willing to follow.
	 */
	int ttl = ( ( 0x1000 - 0x100 ) / 8 );

	/* Read the first extended capability header */
	if ( pci_read_config_dword ( pci, pos, &header ) != 0 )
		return 0;

	/* A header of all-zeroes or all-ones means there is no
	 * extended capability list (or extended config space is not
	 * accessible).
	 */
	if ( ( header == 0 ) || ( header == 0xffffffff ) )
		return 0;

	while ( ttl-- > 0 ) {
		if ( PCI_EXT_CAP_ID ( header ) == ( unsigned int ) cap )
			return pos;
		pos = PCI_EXT_CAP_NEXT ( header );
		if ( pos < PCI_EXT_CAPABILITY_LIST )
			break;
		if ( pci_read_config_dword ( pci, pos, &header ) != 0 )
			break;
	}

	return 0;
}

/**
 * Perform PCI Express function-level reset (FLR)
 *
 * @v pci		PCI device
 * @v exp		PCI Express Capability address
 */
void pci_reset ( struct pci_device *pci, unsigned int exp ) {
	struct pci_config_backup backup;
	uint16_t control;

	/* Back up configuration space */
	pci_backup ( pci, &backup, PCI_CONFIG_BACKUP_STANDARD, NULL );

	/* Perform a PCIe function-level reset */
	pci_read_config_word ( pci, ( exp + PCI_EXP_DEVCTL ), &control );
	control |= PCI_EXP_DEVCTL_FLR;
	pci_write_config_word ( pci, ( exp + PCI_EXP_DEVCTL ), control );

	/* Allow time for reset to complete */
	mdelay ( PCI_EXP_FLR_DELAY_MS );

	/* Restore configuration */
	pci_restore ( pci, &backup, PCI_CONFIG_BACKUP_STANDARD, NULL );
}
