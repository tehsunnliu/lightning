/* Code for talking to btcnanod.  We use btcnano-cli. */
#include "btcnano/base58.h"
#include "btcnano/block.h"
#include "btcnano/shadouble.h"
#include "btcnanod.h"
#include "lightningd.h"
#include "log.h"
#include <ccan/cast/cast.h>
#include <ccan/io/io.h>
#include <ccan/pipecmd/pipecmd.h>
#include <ccan/str/hex/hex.h>
#include <ccan/take/take.h>
#include <ccan/tal/grab_file/grab_file.h>
#include <ccan/tal/path/path.h>
#include <ccan/tal/str/str.h>
#include <ccan/tal/tal.h>
#include <common/json.h>
#include <common/memleak.h>
#include <common/utils.h>
#include <errno.h>
#include <inttypes.h>
#include <lightningd/chaintopology.h>

#define BTCNANO_CLI "btcnano-cli"

char *btcnano_datadir;

/* Add the n'th arg to *args, incrementing n and keeping args of size n+1 */
static void add_arg(const char ***args, const char *arg)
{
	size_t n = tal_count(*args);
	tal_resize(args, n + 1);
	(*args)[n] = arg;
}

static const char **gather_args(const struct btcnanod *btcnanod,
				const tal_t *ctx, const char *cmd, va_list ap)
{
	const char **args = tal_arr(ctx, const char *, 1);
	const char *arg;

	args[0] = btcnanod->chainparams->cli;
	if (btcnanod->chainparams->cli_args)
		add_arg(&args, btcnanod->chainparams->cli_args);

	if (btcnanod->datadir)
		add_arg(&args, tal_fmt(args, "-datadir=%s", btcnanod->datadir));


	if (btcnanod->rpcconnect)
		add_arg(&args,
			tal_fmt(args, "-rpcconnect=%s", btcnanod->rpcconnect));

	if (btcnanod->rpcuser)
		add_arg(&args, tal_fmt(args, "-rpcuser=%s", btcnanod->rpcuser));

	if (btcnanod->rpcpass)
		add_arg(&args,
			tal_fmt(args, "-rpcpassword=%s", btcnanod->rpcpass));

	add_arg(&args, cmd);

	while ((arg = va_arg(ap, const char *)) != NULL)
		add_arg(&args, tal_strdup(args, arg));

	add_arg(&args, NULL);
	return args;
}

struct btcnano_cli {
	struct list_node list;
	struct btcnanod *btcnanod;
	int fd;
	int *exitstatus;
	pid_t pid;
	const char **args;
	char *output;
	size_t output_bytes;
	size_t new_output;
	void (*process)(struct btcnano_cli *);
	void *cb;
	void *cb_arg;
	struct btcnano_cli **stopper;
};

static struct io_plan *read_more(struct io_conn *conn, struct btcnano_cli *bcli)
{
	bcli->output_bytes += bcli->new_output;
	if (bcli->output_bytes == tal_count(bcli->output))
		tal_resize(&bcli->output, bcli->output_bytes * 2);
	return io_read_partial(conn, bcli->output + bcli->output_bytes,
			       tal_count(bcli->output) - bcli->output_bytes,
			       &bcli->new_output, read_more, bcli);
}

static struct io_plan *output_init(struct io_conn *conn, struct btcnano_cli *bcli)
{
	bcli->output_bytes = bcli->new_output = 0;
	bcli->output = tal_arr(bcli, char, 100);
	return read_more(conn, bcli);
}

static void next_bcli(struct btcnanod *btcnanod);

/* For printing: simple string of args. */
static char *bcli_args(struct btcnano_cli *bcli)
{
	size_t i;
	char *ret = tal_strdup(bcli, bcli->args[0]);

	for (i = 1; bcli->args[i]; i++) {
		ret = tal_strcat(bcli, take(ret), " ");
		ret = tal_strcat(bcli, take(ret), bcli->args[i]);
	}
	return ret;
}

static void bcli_finished(struct io_conn *conn, struct btcnano_cli *bcli)
{
	int ret, status;
	struct btcnanod *btcnanod = bcli->btcnanod;

	/* FIXME: If we waited for SIGCHILD, this could never hang! */
	ret = waitpid(bcli->pid, &status, 0);
	if (ret != bcli->pid)
		fatal("%s %s", bcli_args(bcli),
		      ret == 0 ? "not exited?" : strerror(errno));

	if (!WIFEXITED(status))
		fatal("%s died with signal %i",
		      bcli_args(bcli),
		      WTERMSIG(status));

	if (!bcli->exitstatus) {
		if (WEXITSTATUS(status) != 0) {
			/* Allow 60 seconds of spurious errors, eg. reorg. */
			struct timerel t;

			log_unusual(bcli->btcnanod->log,
				    "%s exited with status %u",
				    bcli_args(bcli),
				    WEXITSTATUS(status));

			if (!btcnanod->error_count)
				btcnanod->first_error_time = time_mono();

			t = timemono_between(time_mono(),
					     btcnanod->first_error_time);
			if (time_greater(t, time_from_sec(60)))
				fatal("%s exited %u (after %u other errors) '%.*s'",
				      bcli_args(bcli),
				      WEXITSTATUS(status),
				      btcnanod->error_count,
				      (int)bcli->output_bytes,
				      bcli->output);
			btcnanod->error_count++;
			btcnanod->req_running = false;
			goto done;
		}
	} else
		*bcli->exitstatus = WEXITSTATUS(status);

	if (WEXITSTATUS(status) == 0)
		btcnanod->error_count = 0;

	btcnanod->req_running = false;

	/* Don't continue if were only here because we were freed for shutdown */
	if (btcnanod->shutdown)
		return;

	db_begin_transaction(btcnanod->ld->wallet->db);
	bcli->process(bcli);
	db_commit_transaction(btcnanod->ld->wallet->db);

done:
	tal_free(bcli);

	next_bcli(btcnanod);
}

static void next_bcli(struct btcnanod *btcnanod)
{
	struct btcnano_cli *bcli;
	struct io_conn *conn;

	if (btcnanod->req_running)
		return;

	bcli = list_pop(&btcnanod->pending, struct btcnano_cli, list);
	if (!bcli)
		return;

	bcli->pid = pipecmdarr(&bcli->fd, NULL, &bcli->fd,
			       cast_const2(char **, bcli->args));
	if (bcli->pid < 0)
		fatal("%s exec failed: %s", bcli->args[0], strerror(errno));

	btcnanod->req_running = true;
	/* This lifetime is attached to btcnanod command fd */
	conn = notleak(io_new_conn(btcnanod, bcli->fd, output_init, bcli));
	io_set_finish(conn, bcli_finished, bcli);
}

static void process_donothing(struct btcnano_cli *bcli)
{
}

/* If stopper gets freed first, set process() to a noop. */
static void stop_process_bcli(struct btcnano_cli **stopper)
{
	(*stopper)->process = process_donothing;
	(*stopper)->stopper = NULL;
}

/* It command finishes first, free stopper. */
static void remove_stopper(struct btcnano_cli *bcli)
{
	/* Calls stop_process_bcli, but we don't care. */
	tal_free(bcli->stopper);
}

/* If ctx is non-NULL, and is freed before we return, we don't call process() */
static void start_btcnano_cli(struct btcnanod *btcnanod,
		  const tal_t *ctx,
		  void (*process)(struct btcnano_cli *),
		  bool nonzero_exit_ok,
		  void *cb, void *cb_arg,
		  char *cmd, ...)
{
	va_list ap;
	struct btcnano_cli *bcli = tal(btcnanod, struct btcnano_cli);

	bcli->btcnanod = btcnanod;
	bcli->process = process;
	bcli->cb = cb;
	bcli->cb_arg = cb_arg;
	if (ctx) {
		/* Create child whose destructor will stop us calling */
		bcli->stopper = tal(ctx, struct btcnano_cli *);
		*bcli->stopper = bcli;
		tal_add_destructor(bcli->stopper, stop_process_bcli);
		tal_add_destructor(bcli, remove_stopper);
	} else
		bcli->stopper = NULL;

	if (nonzero_exit_ok)
		bcli->exitstatus = tal(bcli, int);
	else
		bcli->exitstatus = NULL;
	va_start(ap, cmd);
	bcli->args = gather_args(btcnanod, bcli, cmd, ap);
	va_end(ap);

	list_add_tail(&btcnanod->pending, &bcli->list);
	next_bcli(btcnanod);
}

static bool extract_feerate(struct btcnano_cli *bcli,
			    const char *output, size_t output_bytes,
			    double *feerate)
{
	const jsmntok_t *tokens, *feeratetok;
	bool valid;

	tokens = json_parse_input(output, output_bytes, &valid);
	if (!tokens)
		fatal("%s: %s response",
		      bcli_args(bcli),
		      valid ? "partial" : "invalid");

	if (tokens[0].type != JSMN_OBJECT) {
		log_unusual(bcli->btcnanod->log,
			    "%s: gave non-object (%.*s)?",
			    bcli_args(bcli),
			    (int)output_bytes, output);
		return false;
	}

	feeratetok = json_get_member(output, tokens, "feerate");
	if (!feeratetok)
		return false;

	return json_tok_double(output, feeratetok, feerate);
}

struct estimatefee {
	size_t i;
	const u32 *blocks;
	const char **estmode;

	void (*cb)(struct btcnanod *btcnanod, const u32 satoshi_per_kw[],
		   void *);
	void *arg;
	u32 *satoshi_per_kw;
};

static void do_one_estimatefee(struct btcnanod *btcnanod,
			       struct estimatefee *efee);

static void process_estimatefee(struct btcnano_cli *bcli)
{
	double feerate;
	struct estimatefee *efee = bcli->cb_arg;

	/* FIXME: We could trawl recent blocks for median fee... */
	if (!extract_feerate(bcli, bcli->output, bcli->output_bytes, &feerate)) {
		log_unusual(bcli->btcnanod->log, "Unable to estimate %s/%u fee",
			    efee->estmode[efee->i], efee->blocks[efee->i]);
		efee->satoshi_per_kw[efee->i] = 0;
	} else
		/* Rate in satoshi per kw. */
		efee->satoshi_per_kw[efee->i] = feerate * 100000000 / 4;

	efee->i++;
	if (efee->i == tal_count(efee->satoshi_per_kw)) {
		efee->cb(bcli->btcnanod, efee->satoshi_per_kw, efee->arg);
		tal_free(efee);
	} else {
		/* Next */
		do_one_estimatefee(bcli->btcnanod, efee);
	}
}

static void do_one_estimatefee(struct btcnanod *btcnanod,
			       struct estimatefee *efee)
{
	char blockstr[STR_MAX_CHARS(u32)];

	sprintf(blockstr, "%u", efee->blocks[efee->i]);
	start_btcnano_cli(btcnanod, NULL, process_estimatefee, false, NULL, efee,
			  "estimatesmartfee", blockstr, efee->estmode[efee->i],
			  NULL);
}

void btcnanod_estimate_fees_(struct btcnanod *btcnanod,
			     const u32 blocks[], const char *estmode[],
			     size_t num_estimates,
			     void (*cb)(struct btcnanod *btcnanod,
					const u32 satoshi_per_kw[], void *),
			     void *arg)
{
	struct estimatefee *efee = tal(btcnanod, struct estimatefee);

	efee->i = 0;
	efee->blocks = tal_dup_arr(efee, u32, blocks, num_estimates, 0);
	efee->estmode = tal_dup_arr(efee, const char *, estmode, num_estimates,
				    0);
	efee->cb = cb;
	efee->arg = arg;
	efee->satoshi_per_kw = tal_arr(efee, u32, num_estimates);

	do_one_estimatefee(btcnanod, efee);
}

static void process_sendrawtx(struct btcnano_cli *bcli)
{
	void (*cb)(struct btcnanod *btcnanod,
		   int, const char *msg, void *) = bcli->cb;
	const char *msg = tal_strndup(bcli, (char *)bcli->output,
				      bcli->output_bytes);

	log_debug(bcli->btcnanod->log, "sendrawtx exit %u, gave %s",
		  *bcli->exitstatus, msg);

	cb(bcli->btcnanod, *bcli->exitstatus, msg, bcli->cb_arg);
}

void btcnanod_sendrawtx_(struct btcnanod *btcnanod,
			 const char *hextx,
			 void (*cb)(struct btcnanod *btcnanod,
				    int exitstatus, const char *msg, void *),
			 void *arg)
{
	log_debug(btcnanod->log, "sendrawtransaction: %s", hextx);
	start_btcnano_cli(btcnanod, NULL, process_sendrawtx, true, cb, arg,
			  "sendrawtransaction", hextx, NULL);
}

static void process_rawblock(struct btcnano_cli *bcli)
{
	struct btcnano_block *blk;
	void (*cb)(struct btcnanod *btcnanod, struct btcnano_block *blk, void *arg) = bcli->cb;

	/* FIXME: Just get header if we can't get full block. */
	blk = btcnano_block_from_hex(bcli, bcli->output, bcli->output_bytes);
	if (!blk)
		fatal("%s: bad block '%.*s'?", bcli_args(bcli), (int)bcli->output_bytes, (char *)bcli->output);

	cb(bcli->btcnanod, blk, bcli->cb_arg);
}

void btcnanod_getrawblock_(struct btcnanod *btcnanod,
			   const struct btcnano_blkid *blockid,
			   void (*cb)(struct btcnanod *btcnanod,
				      struct btcnano_block *blk,
				      void *arg),
			   void *arg)
{
	char hex[hex_str_size(sizeof(*blockid))];

	btcnano_blkid_to_hex(blockid, hex, sizeof(hex));
	start_btcnano_cli(btcnanod, NULL, process_rawblock, false, cb, arg,
			  "getblock", hex, "false", NULL);
}

static void process_getblockcount(struct btcnano_cli *bcli)
{
	u32 blockcount;
	char *p, *end;
	void (*cb)(struct btcnanod *btcnanod,
		   u32 blockcount,
		   void *arg) = bcli->cb;

	p = tal_strndup(bcli, bcli->output, bcli->output_bytes);
	blockcount = strtol(p, &end, 10);
	if (end == p || *end != '\n')
		fatal("%s: gave non-numeric blockcount %s",
		      bcli_args(bcli), p);

	cb(bcli->btcnanod, blockcount, bcli->cb_arg);
}

void btcnanod_getblockcount_(struct btcnanod *btcnanod,
			      void (*cb)(struct btcnanod *btcnanod,
					 u32 blockcount,
					 void *arg),
			      void *arg)
{
	start_btcnano_cli(btcnanod, NULL, process_getblockcount, false, cb, arg,
			  "getblockcount", NULL);
}

struct get_output {
	unsigned int blocknum, txnum, outnum;

	/* The real callback */
	void (*cb)(struct btcnanod *btcnanod, const struct btcnano_tx_output *txout, void *arg);

	/* The real callback arg */
	void *cbarg;
};

static void process_get_output(struct btcnanod *btcnanod, const struct btcnano_tx_output *txout, void *arg)
{
	struct get_output *go = arg;
	go->cb(btcnanod, txout, go->cbarg);
}

static void process_gettxout(struct btcnano_cli *bcli)
{
	void (*cb)(struct btcnanod *btcnanod,
		   const struct btcnano_tx_output *output,
		   void *arg) = bcli->cb;
	const jsmntok_t *tokens, *valuetok, *scriptpubkeytok, *hextok;
	struct btcnano_tx_output out;
	bool valid;

	/* As of at least v0.15.1.0, btcnanod returns "success" but an empty
	   string on a spent gettxout */
	if (*bcli->exitstatus != 0 || bcli->output_bytes == 0) {
		log_debug(bcli->btcnanod->log, "%s: not unspent output?",
			  bcli_args(bcli));
		cb(bcli->btcnanod, NULL, bcli->cb_arg);
		return;
	}

	tokens = json_parse_input(bcli->output, bcli->output_bytes, &valid);
	if (!tokens)
		fatal("%s: %s response",
		      bcli_args(bcli), valid ? "partial" : "invalid");

	if (tokens[0].type != JSMN_OBJECT)
		fatal("%s: gave non-object (%.*s)?",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);

	valuetok = json_get_member(bcli->output, tokens, "value");
	if (!valuetok)
		fatal("%s: had no value member (%.*s)?",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);

	if (!json_tok_btcnano_amount(bcli->output, valuetok, &out.amount))
		fatal("%s: had bad value (%.*s)?",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);

	scriptpubkeytok = json_get_member(bcli->output, tokens, "scriptPubKey");
	if (!scriptpubkeytok)
		fatal("%s: had no scriptPubKey member (%.*s)?",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);
	hextok = json_get_member(bcli->output, scriptpubkeytok, "hex");
	if (!hextok)
		fatal("%s: had no scriptPubKey->hex member (%.*s)?",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);

	out.script = tal_hexdata(bcli, bcli->output + hextok->start,
				 hextok->end - hextok->start);
	if (!out.script)
		fatal("%s: scriptPubKey->hex invalid hex (%.*s)?",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);

	cb(bcli->btcnanod, &out, bcli->cb_arg);
}

/**
 * process_getblock -- Retrieve a block from btcnanod
 *
 * Used to resolve a `txoutput` after identifying the blockhash, and
 * before extracting the outpoint from the UTXO.
 */
static void process_getblock(struct btcnano_cli *bcli)
{
	void (*cb)(struct btcnanod *btcnanod,
		   const struct btcnano_tx_output *output,
		   void *arg) = bcli->cb;
	struct get_output *go = bcli->cb_arg;
	void *cbarg = go->cbarg;
	const jsmntok_t *tokens, *txstok, *txidtok;
	struct btcnano_txid txid;
	bool valid;

	tokens = json_parse_input(bcli->output, bcli->output_bytes, &valid);
	if (!tokens) {
		/* Most likely we are running on a pruned node, call
		 * the callback with NULL to indicate failure */
		log_debug(bcli->btcnanod->log,
			  "%s: returned invalid block, is this a pruned node?",
			  bcli_args(bcli));
		cb(bcli->btcnanod, NULL, cbarg);
		tal_free(go);
		return;
	}

	if (tokens[0].type != JSMN_OBJECT)
		fatal("%s: gave non-object (%.*s)?",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);

	/*  "tx": [
	    "1a7bb0f58a5d235d232deb61d9e2208dabe69848883677abe78e9291a00638e8",
	    "56a7e3468c16a4e21a4722370b41f522ad9dd8006c0e4e73c7d1c47f80eced94",
	    ...
	*/
	txstok = json_get_member(bcli->output, tokens, "tx");
	if (!txstok)
		fatal("%s: had no tx member (%.*s)?",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);

	/* Now, this can certainly happen, if txnum too large. */
	txidtok = json_get_arr(txstok, go->txnum);
	if (!txidtok) {
		log_debug(bcli->btcnanod->log, "%s: no txnum %u",
			  bcli_args(bcli), go->txnum);
		cb(bcli->btcnanod, NULL, cbarg);
		tal_free(go);
		return;
	}

	if (!btcnano_txid_from_hex(bcli->output + txidtok->start,
				   txidtok->end - txidtok->start,
				   &txid))
		fatal("%s: had bad txid (%.*s)?",
		      bcli_args(bcli),
		      txidtok->end - txidtok->start,
		      bcli->output + txidtok->start);

	go->cb = cb;
	/* Now get the raw tx output. */
	btcnanod_gettxout(bcli->btcnanod, &txid, go->outnum, process_get_output, go);
}

static void process_getblockhash_for_txout(struct btcnano_cli *bcli)
{
	void (*cb)(struct btcnanod *btcnanod,
		   const struct btcnano_tx_output *output,
		   void *arg) = bcli->cb;
	struct get_output *go = bcli->cb_arg;

	if (*bcli->exitstatus != 0) {
		void *cbarg = go->cbarg;
		log_debug(bcli->btcnanod->log, "%s: invalid blocknum?",
			  bcli_args(bcli));
		tal_free(go);
		cb(bcli->btcnanod, NULL, cbarg);
		return;
	}

	start_btcnano_cli(bcli->btcnanod, NULL, process_getblock, false, cb, go,
			  "getblock",
			  take(tal_strndup(go, bcli->output,bcli->output_bytes)),
			  NULL);
}

void btcnanod_getoutput_(struct btcnanod *btcnanod,
			 unsigned int blocknum, unsigned int txnum,
			 unsigned int outnum,
			 void (*cb)(struct btcnanod *btcnanod,
				    const struct btcnano_tx_output *output,
				    void *arg),
			 void *arg)
{
	struct get_output *go = tal(btcnanod, struct get_output);
	go->blocknum = blocknum;
	go->txnum = txnum;
	go->outnum = outnum;
	go->cbarg = arg;

	/* We may not have topology ourselves that far back, so ask btcnanod */
	start_btcnano_cli(btcnanod, NULL, process_getblockhash_for_txout,
			  true, cb, go,
			  "getblockhash", take(tal_fmt(go, "%u", blocknum)),
			  NULL);

	/* Looks like a leak, but we free it in process_getblock */
	notleak(go);
}

static void process_getblockhash(struct btcnano_cli *bcli)
{
	struct btcnano_blkid blkid;
	void (*cb)(struct btcnanod *btcnanod,
		   const struct btcnano_blkid *blkid,
		   void *arg) = bcli->cb;

	/* If it failed, call with NULL block. */
	if (*bcli->exitstatus != 0) {
		cb(bcli->btcnanod, NULL, bcli->cb_arg);
		return;
	}

	if (bcli->output_bytes == 0
	    || !btcnano_blkid_from_hex(bcli->output, bcli->output_bytes-1,
				       &blkid)) {
		fatal("%s: bad blockid '%.*s'",
		      bcli_args(bcli), (int)bcli->output_bytes, bcli->output);
	}

	cb(bcli->btcnanod, &blkid, bcli->cb_arg);
}

void btcnanod_getblockhash_(struct btcnanod *btcnanod,
			    u32 height,
			    void (*cb)(struct btcnanod *btcnanod,
				       const struct btcnano_blkid *blkid,
				       void *arg),
			    void *arg)
{
	char str[STR_MAX_CHARS(height)];
	sprintf(str, "%u", height);

	start_btcnano_cli(btcnanod, NULL, process_getblockhash, true, cb, arg,
			  "getblockhash", str, NULL);
}

void btcnanod_gettxout(struct btcnanod *btcnanod,
		       const struct btcnano_txid *txid, const u32 outnum,
		       void (*cb)(struct btcnanod *btcnanod,
				  const struct btcnano_tx_output *txout,
				  void *arg),
		       void *arg)
{
	start_btcnano_cli(btcnanod, NULL,
			  process_gettxout, true, cb, arg,
			  "gettxout",
			  take(type_to_string(NULL, struct btcnano_txid, txid)),
			  take(tal_fmt(NULL, "%u", outnum)),
			  NULL);
}

static void destroy_btcnanod(struct btcnanod *btcnanod)
{
	/* Suppresses the callbacks from bcli_finished as we free conns. */
	btcnanod->shutdown = true;
}

static const char **cmdarr(const tal_t *ctx, const struct btcnanod *btcnanod,
			   const char *cmd, ...)
{
	va_list ap;
	const char **args;

	va_start(ap, cmd);
	args = gather_args(btcnanod, ctx, cmd, ap);
	va_end(ap);
	return args;
}

void wait_for_btcnanod(struct btcnanod *btcnanod)
{
	int from, ret, status;
	pid_t child;
	const char **cmd = cmdarr(btcnanod, btcnanod, "echo", NULL);
	char *output;
	bool printed = false;

	for (;;) {
		child = pipecmdarr(&from, NULL, &from, cast_const2(char **,cmd));
		if (child < 0)
			fatal("%s exec failed: %s", cmd[0], strerror(errno));

		output = grab_fd(cmd, from);
		if (!output)
			fatal("Reading from %s failed: %s",
			      cmd[0], strerror(errno));

		ret = waitpid(child, &status, 0);
		if (ret != child)
			fatal("Waiting for %s: %s", cmd[0], strerror(errno));
		if (!WIFEXITED(status))
			fatal("Death of %s: signal %i",
			      cmd[0], WTERMSIG(status));

		if (WEXITSTATUS(status) == 0)
			break;

		/* btcnano/src/rpc/protocol.h:
		 *	RPC_IN_WARMUP = -28, //!< Client still warming up
		 */
		if (WEXITSTATUS(status) != 28)
			fatal("%s exited with code %i: %s",
			      cmd[0], WEXITSTATUS(status), output);

		if (!printed) {
			log_unusual(btcnanod->log,
				    "Waiting for btcnanod to warm up...");
			printed = true;
		}
		sleep(1);
	}
	tal_free(cmd);
}

struct btcnanod *new_btcnanod(const tal_t *ctx,
			      struct lightningd *ld,
			      struct log *log)
{
	struct btcnanod *btcnanod = tal(ctx, struct btcnanod);

	/* Use testnet by default, change later if we want another network */
	btcnanod->chainparams = chainparams_for_network("testnet");
	btcnanod->datadir = NULL;
	btcnanod->ld = ld;
	btcnanod->log = log;
	btcnanod->req_running = false;
	btcnanod->shutdown = false;
	btcnanod->error_count = 0;
	btcnanod->rpcuser = NULL;
	btcnanod->rpcpass = NULL;
	btcnanod->rpcconnect = NULL;
	list_head_init(&btcnanod->pending);
	tal_add_destructor(btcnanod, destroy_btcnanod);

	return btcnanod;
}
