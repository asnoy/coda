/* BLURB lgpl
			Coda File System
			    Release 6

	    Copyright (c) 2006 Carnegie Mellon University
		  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the  terms of the  GNU  Library General Public Licence  Version 2,  as
shown in the file LICENSE. The technical and financial contributors to
Coda are listed in the file CREDITS.

			Additional copyrights
#*/

/* helpers and private functions */

#ifndef _GRUNT_H_
#define _GRUNT_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>
#include "aes.h"

#define bytes(bits)	((bits)/8)
#define int32(x)	((uint32_t *)(x))
#define dump128(prefix, b) \
    fprintf(stderr, "%c %08x%08x%08x%08x\n", prefix, \
	    htonl(int32(b)[0]), htonl(int32(b)[1]), \
	    htonl(int32(b)[2]), htonl(int32(b)[3]))

static inline void xor128(aes_block out, const aes_block in)
{
    out[0] ^= in[0];
    out[1] ^= in[1];
}

/* private functions */
/* secure_aes.c */
void secure_aes_init(int verbose);
int aes_cbc_encrypt(const uint64_t *in, uint64_t *out, size_t len,
		    const aes_block iv, aes_encrypt_ctx *ctx);
int aes_cbc_decrypt(const uint64_t *in, uint64_t *out, size_t len,
		    const aes_block iv, aes_decrypt_ctx *ctx);

int aes_xcbc_prf_init(void **ctx, const uint8_t *key, size_t len);
#define aes_xcbc_prf_release aes_xcbc_mac_release
#define aes_xcbc_prf_128     aes_xcbc_mac_128

/* auth_aes_xcbc.c */
int aes_xcbc_mac_init(void **ctx, const uint8_t *key, size_t len);
void aes_xcbc_mac_release(void **ctx);
void aes_xcbc_mac_128(void *ctx, const uint8_t *buf, size_t len, aes_block mac);

/* secure_random.c */
void secure_random_init(int verbose);
void secure_random_release(void);

/* secure_init.c */
void secure_audit(const char *event, uint32_t spi, uint32_t seq,
		  const struct sockaddr *src);

/* Sadly we need this because we couldn't pass the version number to the
 * initializers without breaking the ABI */
void aes_ccm_tweak(void *ctx, uint32_t version);

/* secure_pbkdf.c */
void secure_pbkdf_init(int verbose);

#endif /* _GRUNT_H_ */

