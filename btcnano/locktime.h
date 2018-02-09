#ifndef LIGHTNING_BTCNANO_LOCKTIME_H
#define LIGHTNING_BTCNANO_LOCKTIME_H
#include "config.h"
#include <ccan/short_types/short_types.h>
#include <stdbool.h>

/* As used by nSequence and OP_CHECKSEQUENCEVERIFY (BIP68) */
struct rel_locktime {
	u32 locktime;
};

bool seconds_to_rel_locktime(u32 seconds, struct rel_locktime *rel);
bool blocks_to_rel_locktime(u32 blocks, struct rel_locktime *rel);
bool rel_locktime_is_seconds(const struct rel_locktime *rel);
u32 rel_locktime_to_seconds(const struct rel_locktime *rel);
u32 rel_locktime_to_blocks(const struct rel_locktime *rel);

u32 btcnano_nsequence(const struct rel_locktime *rel);

/* As used by nLocktime and OP_CHECKLOCKTIMEVERIFY (BIP65) */
struct abs_locktime {
	u32 locktime;
};

bool seconds_to_abs_locktime(u32 seconds, struct abs_locktime *abs);
bool blocks_to_abs_locktime(u32 blocks, struct abs_locktime *abs);
bool abs_locktime_is_seconds(const struct abs_locktime *abs);
u32 abs_locktime_to_seconds(const struct abs_locktime *abs);
u32 abs_locktime_to_blocks(const struct abs_locktime *abs);

#endif /* LIGHTNING_BTCNANO_LOCKTIME_H */
