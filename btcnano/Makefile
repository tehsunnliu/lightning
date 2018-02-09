# Included for one dir up.

BTCNANO_SRC :=					\
	btcnano/base58.c			\
	btcnano/block.c				\
	btcnano/chainparams.c		\
	btcnano/locktime.c			\
	btcnano/pubkey.c			\
	btcnano/pullpush.c			\
	btcnano/script.c			\
	btcnano/shadouble.c			\
	btcnano/short_channel_id.c	\
	btcnano/signature.c			\
	btcnano/tx.c				\
	btcnano/varint.c

BTCNANO_OBJS := $(BTCNANO_SRC:.c=.o)

BTCNANO_HEADERS := btcnano/address.h			\
	btcnano/base58.h			\
	btcnano/block.h				\
	btcnano/chainparams.h		\
	btcnano/locktime.h			\
	btcnano/preimage.h			\
	btcnano/privkey.h			\
	btcnano/pubkey.h			\
	btcnano/pullpush.h			\
	btcnano/script.h			\
	btcnano/shadouble.h			\
	btcnano/short_channel_id.h	\
	btcnano/signature.h			\
	btcnano/tx.h				\
	btcnano/varint.h

check-source: $(BTCNANO_SRC:%=check-src-include-order/%)		\
	$(BTCNANO_HEADERS:%=check-hdr-include-order/%)

# Btcnano objects depends on btcnano/ external/ and ccan
$(BTCNANO_OBJS): $(CCAN_HEADERS) $(BTCNANO_HEADERS) $(EXTERNAL_HEADERS)

check-source-bolt: $(BTCNANO_SRC:%=bolt-check/%) $(BTCNANO_HEADERS:%=bolt-check/%)

check-makefile: check-btcnano-makefile

check-btcnano-makefile:
	@if [ "`echo btcnano/*.h`" != "$(BTCNANO_HEADERS)" ]; then echo BTCNANO_HEADERS incorrect; exit 1; fi

check-whitespace: $(BTCNANO_SRC:%=check-whitespace/%) $(BTCNANO_HEADERS:%=check-whitespace/%) check-whitespace/btcnano/Makefile

clean: btcnano-clean

btcnano-clean:
	$(RM) $(BTCNANO_OBJS) $(BTCNANO_TEST_PROGRAMS) $(BTCNANO_TEST_OBJS)

# Uncomment the below line for running test
#include btcnano/test/Makefile
