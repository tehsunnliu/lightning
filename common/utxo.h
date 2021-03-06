#ifndef LIGHTNING_COMMON_UTXO_H
#define LIGHTNING_COMMON_UTXO_H
#include "config.h"
#include <btcnano/pubkey.h>
#include <btcnano/shadouble.h>
#include <btcnano/tx.h>
#include <ccan/short_types/short_types.h>
#include <ccan/tal/tal.h>
#include <stdbool.h>

/* Information needed for their_unilateral/to-us outputs */
struct unilateral_close_info {
	u64 channel_id;
	struct pubkey peer_id;
	struct pubkey commitment_point;
};

struct utxo {
	struct btcnano_txid txid;
	u32 outnum;
	u64 amount;
	u32 keyindex;
	bool is_p2sh;
	u8 status;

	/* Optional unilateral close information, NULL if this is just
	 * a HD key */
	struct unilateral_close_info *close_info;
};

void towire_utxo(u8 **pptr, const struct utxo *utxo);
void fromwire_utxo(const tal_t *ctx, const u8 **ptr, size_t *max, struct utxo *utxo);

/* build_utxos/funding_tx use array of pointers, but marshall code
 * wants arr of structs */
struct utxo *from_utxoptr_arr(const tal_t *ctx, const struct utxo **utxos);
const struct utxo **to_utxoptr_arr(const tal_t *ctx, const struct utxo *utxos);
#endif /* LIGHTNING_COMMON_UTXO_H */
