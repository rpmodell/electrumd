CC = cc
CFLAGS = -g -std=c99 -D_DEFAULT_SOURCE -O0 -Wall -pedantic -I/usr/include/db5.3/ -I/usr/local/include/

LDFLAGS = -lcurl -lpthread -lleveldb -lcrypto -lm -L/usr/local/lib/

SOURCES_DIR=src/

OBJS=shared.o ujson.o hashes_vec.o block_sync.o bitcoin_rpc.o bitcoin_common.o bitcoin_p2p.o txdb.o merkle.o util.o mempool.o logging.o config.o electrum_rpc.o main.o
TARGET=electrumd

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

shared.o: $(SOURCES_DIR)/shared.c
	$(CC) $(CFLAGS) -c -o $@ $<

ujson.o: $(SOURCES_DIR)/ujson.c
	$(CC) $(CFLAGS) -c -o $@ $<

hashes_vec.o: $(SOURCES_DIR)/hashes_vec.c
	$(CC) $(CFLAGS) -c -o $@ $<

block_sync.o: $(SOURCES_DIR)/block_sync.c
	$(CC) $(CFLAGS) -c -o $@ $<

bitcoin_common.o: $(SOURCES_DIR)/bitcoin_common.c
	$(CC) $(CFLAGS) -c -o $@ $<

bitcoin_rpc.o: $(SOURCES_DIR)/bitcoin_rpc.c
	$(CC) $(CFLAGS) -c -o $@ $<

bitcoin_p2p.o: $(SOURCES_DIR)/bitcoin_p2p.c
	$(CC) $(CFLAGS) -c -o $@ $<

txdb.o: $(SOURCES_DIR)/txdb.c
	$(CC) $(CFLAGS) -c -o $@ $<

merkle.o: $(SOURCES_DIR)/merkle.c
	$(CC) $(CFLAGS) -c -o $@ $<

util.o: $(SOURCES_DIR)/util.c
	$(CC) $(CFLAGS) -c -o $@ $<

mempool.o: $(SOURCES_DIR)/mempool.c
	$(CC) $(CFLAGS) -c -o $@ $<

logging.o: $(SOURCES_DIR)/logging.c
	$(CC) $(CFLAGS) -c -o $@ $<

config.o: $(SOURCES_DIR)/config.c
	$(CC) $(CFLAGS) -c -o $@ $<

electrum_rpc.o: $(SOURCES_DIR)/electrum_rpc.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o: $(SOURCES_DIR)/main.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	@rm -f $(TARGET) $(OBJS)

.PHONY: install
install:
	install -m 0755 ./$(TARGET) /usr/local/bin/ 
	install -m 0664 -b ./etc/electrumd.conf /usr/local/etc/
	install -m 0664 ./electrumd.1 /usr/local/share/man/man1/
	install -m 0664 ./electrumd.conf.1 /usr/local/share/man/man1/
