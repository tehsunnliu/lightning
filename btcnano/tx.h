#ifndef LIGHTNING_BTCNANO_TX_H
#define LIGHTNING_BTCNANO_TX_H
#include "config.h"
#include "shadouble.h"
#include "signature.h"
#include "varint.h"
#include <ccan/short_types/short_types.h>
#include <ccan/tal/tal.h>

struct btcnano_txid {
	struct sha256_double shad;
};

struct btcnano_tx {
	u32 version;
	struct btcnano_tx_input *input;
	struct btcnano_tx_output *output;
	u32 lock_time;
};

struct btcnano_tx_output {
	u64 amount;
	u8 *script;
};

struct btcnano_tx_input {
	struct btcnano_txid txid;
	u32 index; /* output number referred to by above */
	u8 *script;
	u32 sequence_number;

	/* Value of the output we're spending (NULL if unknown). */
	u64 *amount;

	/* Only if BIP141 used. */
	u8 **witness;
};


/* SHA256^2 the tx: simpler than sha256_tx */
void btcnano_txid(const struct btcnano_tx *tx, struct btcnano_txid *txid);

/* Useful for signature code. */
void sha256_tx_for_sig(struct sha256_double *h, const struct btcnano_tx *tx,
		       unsigned int input_num, const u8 *witness_script);

/* Linear bytes of tx. */
u8 *linearize_tx(const tal_t *ctx, const struct btcnano_tx *tx);

/* Get weight of tx in Sipa. */
size_t measure_tx_weight(const struct btcnano_tx *tx);

/* Allocate a tx: you just need to fill in inputs and outputs (they're
 * zeroed with inputs' sequence_number set to FFFFFFFF) */
struct btcnano_tx *btcnano_tx(const tal_t *ctx, varint_t input_count, varint_t output_count);

/* This takes a raw btcnano tx in hex. */
struct btcnano_tx *btcnano_tx_from_hex(const tal_t *ctx, const char *hex,
				       size_t hexlen);

/* Parse hex string to get txid (reversed, a-la btcnanod). */
bool btcnano_txid_from_hex(const char *hexstr, size_t hexstr_len,
			   struct btcnano_txid *txid);

/* Get hex string of txid (reversed, a-la btcnanod). */
bool btcnano_txid_to_hex(const struct btcnano_txid *txid,
			 char *hexstr, size_t hexstr_len);

/* Internal de-linearization functions. */
struct btcnano_tx *pull_btcnano_tx(const tal_t *ctx,
				   const u8 **cursor, size_t *max);

/**
 * pull_btcnano_tx_onto - De-serialize a btcnano tx into tx
 *
 * Like pull_btcnano_tx, but skips the allocation of tx. Used by the
 * wire implementation where the caller allocates, and the callee only
 * fills in values.
 *
 * @ctx: Allocation context
 * @cursor: buffer to read from
 * @max: Buffer size left to read
 * @tx (out): Destination transaction
 */
struct btcnano_tx *pull_btcnano_tx_onto(const tal_t *ctx, const u8 **cursor,
					size_t *max, struct btcnano_tx *tx);
#endif /* LIGHTNING_BTCNANO_TX_H */
