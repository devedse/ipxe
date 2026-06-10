#ifndef _IPXE_MD5_SHA1_H
#define _IPXE_MD5_SHA1_H

/** @file
 *
 * Hybrid MD5+SHA1 hash as used by TLSv1.1 and earlier
 *
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );
FILE_SECBOOT ( PERMITTED );

#include <stdint.h>
#include <ipxe/crypto.h>
#include <ipxe/md5.h>
#include <ipxe/sha1.h>

/** An MD5+SHA1 context */
struct md5_sha1_context {
	/** MD5 context */
	uint8_t md5[MD5_CTX_SIZE];
	/** SHA-1 context */
	uint8_t sha1[SHA1_CTX_SIZE];
} __attribute__ (( packed ));

/** MD5+SHA1 context size */
#define MD5_SHA1_CTX_SIZE sizeof ( struct md5_sha1_context )

/** An MD5+SHA1 digest */
struct md5_sha1_digest {
	/** MD5 digest */
	uint8_t md5[MD5_DIGEST_SIZE];
	/** SHA-1 digest */
	uint8_t sha1[SHA1_DIGEST_SIZE];
} __attribute__ (( packed ));

/** MD5+SHA1 digest size */
#define MD5_SHA1_DIGEST_SIZE sizeof ( struct md5_sha1_digest )

extern struct digest_algorithm md5_sha1_algorithm;

#endif /* _IPXE_MD5_SHA1_H */
