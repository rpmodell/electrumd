CC = cc
CFLAGS = -g -std=c99 -D_DEFAULT_SOURCE -O0 -Wall -pedantic -I/usr/include/db5.3/

LDFLAGS = -lcurl -lpthread -ldb-5.3 -lcrypto -lm
vpath src/

VPATH=src/
SOURCES=shared.c ujson.c hashes_vec.c block_sync.c bitcoin_rpc.c bitcoin_common.c bitcoin_p2p.c txdb.c merkle.c util.c mempool.c logging.c config.c electrum_rpc.c main.c
OBJECTS=$(SOURCES:.c=.o)
TARGET=electrumd

$(TARGET) : $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	@rm -f $(TARGET) $(OBJECTS)

.PHONY: install
install:
	install -m 0755 -D -t /usr/local/bin/ ./electrumd
	install -m 0664 -D -t /usr/local/etc/ ./etc/electrumd.conf
	install -m 0664 -D -t /usr/local/share/man/man1/ ./electrumd.1
	install -m 0664 -D -t /usr/local/share/man/man1/ ./electrumd.conf.1
