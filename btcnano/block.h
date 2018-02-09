#ifndef LIGHTNING_BTCNANO_BLOCK_H
#define LIGHTNING_BTCNANO_BLOCK_H
#include "config.h"
#include "btcnano/shadouble.h"
#include <ccan/endian/endian.h>
#include <ccan/short_types/short_types.h>
#include <ccan/tal/tal.h>
#include <stdbool.h>

struct btcnano_blkid {
	struct sha256_double shad;
};

struct btcnano_block_hdr {
	le32 version;
	struct btcnano_blkid prev_hash;
	struct sha256_double merkle_hash;
	le32 timestamp;
	le32 target;
	le32 nonce;
};

struct btcnano_block {
	struct btcnano_block_hdr hdr;
	/* tal_count shows now many */
	struct btcnano_tx **tx;
};

struct btcnano_block *btcnano_block_from_hex(const tal_t *ctx,
					     const char *hex, size_t hexlen);

/* Parse hex string to get blockid (reversed, a-la btcnanod). */
bool btcnano_blkid_from_hex(const char *hexstr, size_t hexstr_len,
			    struct btcnano_blkid *blockid);

/* Get hex string of blockid (reversed, a-la btcnanod). */
bool btcnano_blkid_to_hex(const struct btcnano_blkid *blockid,
			  char *hexstr, size_t hexstr_len);
#endif /* LIGHTNING_BTCNANO_BLOCK_H */
