CC = cc
CFLAGS = -g -std=c99 -D_DEFAULT_SOURCE -O0 -Wall -pedantic -I/usr/include/db5.3/ -I/usr/local/include/

LDFLAGS = -lcurl -lpthread -ldb-5.3 -lcrypto -lm -L/usr/local/lib/

SOURCES_DIR=src/

OBJS=shared.o ujson.o hashes_vec.o block_sync.o bitcoin_rpc.o bitcoin_common.o bitcoin_p2p.o txdb.o merkle.o util.o mempool.o logging.o config.o electrum_rpc.o main.o
TARGET=electrumd

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

shared.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/shared.c

ujson.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/ujson.c

hashes_vec.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/hashes_vec.c

block_sync.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/block_sync.c

bitcoin_common.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/bitcoin_common.c

bitcoin_rpc.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/bitcoin_rpc.c

bitcoin_p2p.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/bitcoin_p2p.c

txdb.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/txdb.c

merkle.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/merkle.c

util.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/util.c

mempool.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/mempool.c

logging.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/logging.c

config.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/config.c

electrum_rpc.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/electrum_rpc.c

main.o:
	$(CC) $(CFLAGS) -c -o $@ $(SOURCES_DIR)/main.c

.PHONY: clean
clean:
	@rm -f $(TARGET) $(OBJS)

.PHONY: install
install:
	install -m 0755 ./$(TARGET) /usr/local/bin/ 
	install -m 0664 -b ./etc/electrumd.conf /usr/local/etc/
	install -m 0664 ./electrumd.1 /usr/local/share/man/man1/
	install -m 0664 ./electrumd.conf.1 /usr/local/share/man/man1/
