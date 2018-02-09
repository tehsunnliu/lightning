#ifndef LIGHTNING_LIGHTNINGD_BTCNANO_H
#define LIGHTNING_LIGHTNINGD_BTCNANO_H
#include "config.h"
#include <btcnano/chainparams.h>
#include <btcnano/tx.h>
#include <ccan/list/list.h>
#include <ccan/short_types/short_types.h>
#include <ccan/tal/tal.h>
#include <ccan/time/time.h>
#include <ccan/typesafe_cb/typesafe_cb.h>
#include <stdbool.h>

struct btcnano_blkid;
struct btcnano_tx_output;
struct block;
struct lightningd;
struct ripemd160;
struct btcnano_tx;
struct peer;
struct btcnano_block;

enum btcnanod_mode {
	BTCNANOD_MAINNET = 1,
	BTCNANOD_TESTNET,
	BTCNANOD_REGTEST
};

struct btcnanod {
	/* -datadir arg for btcnano-cli. */
	char *datadir;

	/* Where to do logging. */
	struct log *log;

	/* Main lightningd structure */
	struct lightningd *ld;

	/* Are we currently running a btcnanod request (it's ratelimited) */
	bool req_running;

	/* Pending requests. */
	struct list_head pending;

	/* What network are we on? */
	const struct chainparams *chainparams;

	/* If non-zero, time we first hit a btcnanod error. */
	unsigned int error_count;
	struct timemono first_error_time;

	/* Ignore results, we're shutting down. */
	bool shutdown;

	/* Passthrough parameters for btcnano-cli */
	char *rpcuser, *rpcpass, *rpcconnect;
};

struct btcnanod *new_btcnanod(const tal_t *ctx,
			      struct lightningd *ld,
			      struct log *log);

void wait_for_btcnanod(struct btcnanod *btcnanod);

void btcnanod_estimate_fees_(struct btcnanod *btcnanod,
			     const u32 blocks[], const char *estmode[],
			     size_t num_estimates,
			     void (*cb)(struct btcnanod *btcnanod,
					const u32 satoshi_per_kw[], void *),
			     void *arg);

#define btcnanod_estimate_fees(btcnanod_, blocks, estmode, num, cb, arg) \
	btcnanod_estimate_fees_((btcnanod_), (blocks), (estmode), (num), \
				typesafe_cb_preargs(void, void *,	\
						    (cb), (arg),	\
						    struct btcnanod *,	\
						    const u32 *),	\
				(arg))

void btcnanod_sendrawtx_(struct btcnanod *btcnanod,
			 const char *hextx,
			 void (*cb)(struct btcnanod *btcnanod,
				    int exitstatus, const char *msg, void *),
			 void *arg);

#define btcnanod_sendrawtx(btcnanod_, hextx, cb, arg)			\
	btcnanod_sendrawtx_((btcnanod_), (hextx),			\
			    typesafe_cb_preargs(void, void *,		\
						(cb), (arg),		\
						struct btcnanod *,	\
						int, const char *),	\
			    (arg))

void btcnanod_getblockcount_(struct btcnanod *btcnanod,
			     void (*cb)(struct btcnanod *btcnanod,
					u32 blockcount,
					void *arg),
			     void *arg);

#define btcnanod_getblockcount(btcnanod_, cb, arg)			\
	btcnanod_getblockcount_((btcnanod_),				\
				typesafe_cb_preargs(void, void *,	\
						    (cb), (arg),	\
						    struct btcnanod *,	\
						    u32 blockcount),	\
				(arg))

/* blkid is NULL if call fails. */
void btcnanod_getblockhash_(struct btcnanod *btcnanod,
			    u32 height,
			    void (*cb)(struct btcnanod *btcnanod,
				       const struct btcnano_blkid *blkid,
				       void *arg),
			    void *arg);
#define btcnanod_getblockhash(btcnanod_, height, cb, arg)		\
	btcnanod_getblockhash_((btcnanod_),				\
			       (height),				\
			       typesafe_cb_preargs(void, void *,	\
						   (cb), (arg),		\
						   struct btcnanod *,	\
						   const struct btcnano_blkid *), \
			       (arg))

void btcnanod_getrawblock_(struct btcnanod *btcnanod,
			   const struct btcnano_blkid *blockid,
			   void (*cb)(struct btcnanod *btcnanod,
				      struct btcnano_block *blk,
				      void *arg),
			   void *arg);
#define btcnanod_getrawblock(btcnanod_, blkid, cb, arg)			\
	btcnanod_getrawblock_((btcnanod_), (blkid),			\
			      typesafe_cb_preargs(void, void *,		\
						  (cb), (arg),		\
						  struct btcnanod *,	\
						  struct btcnano_block *), \
			      (arg))

void btcnanod_getoutput_(struct btcnanod *btcnanod,
			 unsigned int blocknum, unsigned int txnum,
			 unsigned int outnum,
			 void (*cb)(struct btcnanod *btcnanod,
				    const struct btcnano_tx_output *output,
				    void *arg),
			 void *arg);
#define btcnanod_getoutput(btcnanod_, blocknum, txnum, outnum, cb, arg)	\
	btcnanod_getoutput_((btcnanod_), (blocknum), (txnum), (outnum),	\
			    typesafe_cb_preargs(void, void *,		\
						(cb), (arg),		\
						struct btcnanod *,	\
						const struct btcnano_tx_output*), \
			    (arg))

void btcnanod_gettxout(struct btcnanod *btcnanod,
		       const struct btcnano_txid *txid, const u32 outnum,
		       void (*cb)(struct btcnanod *btcnanod,
				  const struct btcnano_tx_output *txout,
				  void *arg),
		       void *arg);

#endif /* LIGHTNING_LIGHTNINGD_BTCNANOD_H */
