/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of the  nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/param.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "shared.h"
#include "logging.h"
#include "electrum_rpc.h"
#include "mempool.h"

#include "block_sync.h"

//#define DB_TEST_HEIGHT 400000

/* enable TEST_BLKCMP to test each received block hash matches with the one taken from jsonrpc,
 * this can impact sync speed so use only for debug purposes
*/
// #define TEST_BLKCMP 1

/*
    Gets blocks using Bitcoin Core Rpc Protocol
    deprecated because its slow
    use prefetch_blocks2 instead
    (to remove in future)
*/
int prefetch_blocks(BitcoinRpcCtx *btc_rpc_ctx, TXDB *dbptr, HashesVec *new_scripthashes)
{
    int ret = 0;
    char hash[65]; // hex as string 2*32 + 1
	long last_height = -1;
#ifndef DB_TEST_HEIGHT
    if (getblockcount(btc_rpc_ctx, &last_height)) {
        return -1;
    }
#else
    last_height = DB_TEST_HEIGHT;
#endif

    time_t last_height_check = time(NULL);
    time_t statistic_time = 0;
    while (electrumd_running && dbptr->current_height < last_height) {
#ifndef DB_TEST_HEIGHT
        if ((time(NULL) - last_height_check) >= (20 * UNIX_MINUTE)) {
            long nw_height = -1;
            if (getblockcount(btc_rpc_ctx, &nw_height)) {
                return -1;
            }

            if (nw_height > last_height) {
                last_height = nw_height;
                loginfof("block sync: new_height=%ld, last_check=%ld", last_height, last_height_check);
            }

            last_height_check = time(NULL);
        }
#endif
        size_t txns, i, itx, itxo;
        size_t count = MIN(last_height - dbptr->current_height, 500); //cache is always a fixed value of 100
        uint8_t **blocks = (uint8_t**) malloc(count * sizeof(uint8_t*));
        size_t *blocks_szs = (size_t*) malloc(count * sizeof(size_t));

        statistic_time = time(NULL);

        for (i = 0; i < count; i++) {	//error logging and handling
            if ((ret = getblockhash(btc_rpc_ctx, dbptr->current_height + i + 1, hash))) {
                logdebugf("block sync: getblockhash: fail: err=%d", ret);
                goto sync_round_end;
            }
                      //
            if ((ret = blocks_szs[i] = (size_t) getrawblock(btc_rpc_ctx, hash, &blocks[i])) <= 0) {
                logdebugf("block sync: getrawblock: fail: err=%d", ret);
                goto sync_round_end;
            }
        }

        logdebugf("sync: debug: fetched %d blocks in %lds", count, time(NULL) - statistic_time);
        statistic_time = 0;

        if (count != i) {
            logdebugf("block sync: error: count %d is not %d", count, i);
            goto sync_round_end;
        }
        logdebugf("getblocks -> OK!");

        time_t time1 = 0;
        time_t parse_tot = 0;
        for (i = 0; i < count; i++) {
            long height = dbptr->current_height + 1; //must be at least >= 0

            time1 = time(NULL);
            BtcTx *txs = NULL;
            if ((txns = btc_parse_txs(&txs, blocks[i], blocks_szs[i])) <= 0) {
                logerrf("blocks sync: failed to parse block at height %ld", height);
                goto sync_round_end;
            }
            parse_tot += (time(NULL) - time1);
            time1 = time(NULL);

            if (txdb_bulk_store_txs(dbptr, txs, txns, height)) {
                logerrf("block sync: error storing txs data");
            }
            if (txdb_store_block_header(dbptr, blocks[i], height)) {
                logerrf("block sync: error storing block headers");
            }
            statistic_time += (time(NULL) - time1);

            if (new_scripthashes) {
                for (itx = 0; itx < txns; itx++) {
                    BtcTx *tx = &txs[itx];
                    for (itxo = 0; itxo < tx->tx_out_count; itxo++) {
                        if (hashes_vec_find(new_scripthashes, tx->tx_out[itxo].pk_script_hash) == -1) {
                            hashes_vec_add(new_scripthashes, tx->tx_out[itxo].pk_script_hash);
                        }
                    }
                }
            }

            btc_txs_free(txs, txns);
            dbptr->current_height++;
        }

        logdebugf("sync: debug: parsed %d blocks in %lds, stored in %lds, avg parse per block=%lds, avg store per block=%lds",
                  count, parse_tot, statistic_time, parse_tot/count, statistic_time/count);
        statistic_time = time(NULL);

        loginfof("block sync: round ended: new_blocks=%ld, current_height=%ld, last_height=%ld, progress=%lf%%, tip=%s",
                 count,
                 dbptr->current_height,
                 last_height,
                 ((double) dbptr->current_height / (double) last_height) * 100.0,
                 hash
                 );
sync_round_end:

        txdb_flush(dbptr);
        logdebugf("sync: debug: flush took %lds", time(NULL) - statistic_time);

        while (count--) {
            if (blocks[count])
                free(blocks[count]);
        }
        free(blocks);
        free(blocks_szs);
    }
    return 0;
}

/*
 * Constructs the block locator vector hashes.
 * To create the block locator hashes, keep pushing hashes until you go back to the genesis block.
 * After pushing 10 hashes back, the step backwards doubles every loop. Further details can be
 * found here: https://en.bitcoin.it/wiki/Protocol_documentation#getblocks
 *
 * Note: is up to the caller initialize and free up the vector, even in case of failure.
*/
static int block_locator_hashes(BitcoinRpcCtx *btc_rpc_ctx, HashesVec *locator, long top_height)
{
    long index, step;
    char hashstr[65]; // hex as string 2*32 + 1
    uint8_t hash[32];

    step = 1;
    // Start at the top of the chain and work backwards.
    for (index = top_height; index > 0; index -= step) {
        // Push top 10 indexes first, then back off exponentially.
        if (locator->size >= 10)
            step *= 2;


        if (getblockhash(btc_rpc_ctx, index, hashstr))
            return -1;

        reverse_hex_to_bytes(hashstr, hash);
        hashes_vec_add(locator, hash);
    }

    // Push the genesis block hash.
    if (getblockhash(btc_rpc_ctx, 0, hashstr))
        return -1;

    reverse_hex_to_bytes(hashstr, hash);
    hashes_vec_add(locator, hash);

    return 0;
}

/*
 * Gets blocks using Bitcoin P2P protocol 2x faster than jsonrpc
 * the speed can be improved by fetching headers using p2p
*/
int prefetch_blocks2(BitcoinRpcCtx *btc_rpc_ctx, BtcP2pProtoCtx *p2p_ctx, TXDB *dbptr, HashesVec *new_scripthashes)
{
    int ret = 0;

    if ((ret = p2p_connect(p2p_ctx, dbptr->current_height))) {
        logerrf("block sync: p2p error: cannot connect with bitcoin client %s:%d", p2p_ctx->addr, p2p_ctx->port);
        return ret;
    }

    char hashstr[65]; // hex as string 2*32 + 1
    long last_height = -1;
#ifndef DB_TEST_HEIGHT
    if (getblockcount(btc_rpc_ctx, &last_height)) {
        return -1;
    }
#else
    last_height = DB_TEST_HEIGHT;
#endif

    if (dbptr->current_height  < 0) {
        uint8_t header[BLOCK_HEADER_SIZE];
        if (getblockhash(btc_rpc_ctx, 0, hashstr))
            return -1;

        if (getblockheader(btc_rpc_ctx, hashstr, header))
            return -1;

        if (txdb_store_block_header(dbptr, header, 0)) {
            logerrf("block sync: error storing genesis block header");
            return -1;
        }

        dbptr->current_height++;
    }

    time_t last_height_check = time(NULL);
    while (electrumd_running && dbptr->current_height < last_height) {
#ifndef DB_TEST_HEIGHT
        if ((time(NULL) - last_height_check) >= (20 * UNIX_MINUTE)) {
            long nw_height = -1;
            if (getblockcount(btc_rpc_ctx, &nw_height)) {
                return -1;
            }

            if (nw_height > last_height) {
                last_height = nw_height;
                loginfof("block sync: new_height=%ld, last_check=%ld", last_height, last_height_check);
            }

            last_height_check = time(NULL);
        }
#endif
        size_t txns, itx, itxo;
        ssize_t i, count, batch; //cache is always a fixed value of 100
        HashesVec locator, block_hashes;
        hashes_vec_init(&locator);
        hashes_vec_init(&block_hashes);

        // create locator vector
        count = 0;
        if (block_locator_hashes(btc_rpc_ctx, &locator, dbptr->current_height)) {
            hashes_vec_free(&locator);
            ret = -1;
            goto sync_round_end;
        }

        // ask new headers using getheaders p2p
        if ((p2p_get_headers_heashes(p2p_ctx, &block_hashes, locator.v, locator.size, NULL)) < 0) {
            logdebugf("p2p_get_headers_heashes fail %s", strerror(errno));
            ret = -2;
            goto sync_round_end;
        }

        hashes_vec_free(&locator);
        while (count < block_hashes.size) {
            // batch is num blocks per getdata (max is 500), since getheaders can return up to 2000 headers
            batch = MIN(block_hashes.size - count, P2P_INV_MAX_SIZE);

            // send getdata for new blocks, max is 500 per getdata
            if ((ret = p2p_get_data(p2p_ctx, block_hashes.v + count, batch, MSG_WITNESS_BLOCK))) {
                logdebugf("p2p_get_data fail %s", strerror(errno));
                ret = -2;
                goto sync_round_end;
            }

            for (i = 0; i < batch; i++, count++) {
                uint8_t *rawblock = NULL;
                size_t block_sz = 0;
                if ((p2p_receive_message(p2p_ctx, &rawblock, &block_sz, MSG_CMD_BLOCK))) {
                    logdebugf("block sync: receive block fail: %s", strerror(errno));
                    ret = -2;
                    goto sync_round_end;
                }

                // logdebugf("received block no=%ld", i);
                long height = dbptr->current_height + 1;
                if (block_sz < BLOCK_HEADER_SIZE) {
                    logerrf("blocks sync: illegal size of block: height=%d, size=%ld", height, block_sz);
                    free(rawblock);
                    assert(0);
                    goto sync_round_end;
                }
#ifdef TEST_BLKCMP
                {
                    char hashstr2[65]; // hex as string 2*32 + 1
                    char hashstr1[65];
                    uint8_t hash[32];
                    double_sha256(hash, rawblock, BLOCK_HEADER_SIZE);
                    if (getblockhash(btc_rpc_ctx, height, hashstr2))
                        return -1;

                    bytes_to_hex_reverse(hash, 32, hashstr1);
                    // logdebugf("%ld:%ld -> hash1 = %s, hash2 = %s", i, height, hashstr1, hashstr2);
                    assert(strcmp(hashstr1, hashstr2) == 0);
                }
#endif

                BtcTx *txs = NULL;
                /* Genesis block has no transaction so we can skip parsing it */
                if (height) {
                    if ((txns = btc_parse_txs(&txs, rawblock, block_sz)) <= 0) {
                        logerrf("blocks sync: failed to parse block at height %ld", height);
                        free(rawblock);
                        assert(0);
                        goto sync_round_end;
                    }

                    if (new_scripthashes) {
                        for (itx = 0; itx < txns; itx++) {
                            BtcTx *tx = &txs[itx];
                            for (itxo = 0; itxo < tx->tx_out_count; itxo++) {
                                hashes_vec_add(new_scripthashes, tx->tx_out[itxo].pk_script_hash);
                            }
                        }
                    }

                    if (txdb_store_txs(dbptr, txs, txns, height)) {
                        logerrf("block sync: error storing txs data");
                        assert(0);
                    }

                    btc_txs_free(txs, txns);
                }

                if (txdb_store_block_header(dbptr, rawblock, height)) {
                    logerrf("block sync: error storing block headers");
                    assert(0);
                }
                // logdebugf("stored block no=%ld", i);

                free(rawblock);
                dbptr->current_height++;
            }
        }

sync_round_end:
        bytes_to_hex_reverse(block_hashes.v[count - 1], 32, hashstr);

        hashes_vec_free(&block_hashes);

        txdb_flush(dbptr);
        if (dbptr->current_height % 100000 <= count) {
            loginfof("block sync: periodic db compaction");
            txdb_compact(dbptr);
        }


        loginfof("block sync: round ended: new_blocks=%ld, current_height=%ld, last_height=%ld, progress=%f%%, tip=%s",
                 count,
                 dbptr->current_height,
                 last_height,
                 ((float) dbptr->current_height / (float) last_height) * 100.0,
                 hashstr
                 );

         if ((ret == -2) && p2p_ping(p2p_ctx)) {
             /*
                If ret is -2 there is an issue communicating with bitcoind, so we ping bitcoind
                and, if the daemon not reply, we close the connection, wait 5s and reconnect.
             */
             logerrf("block sync: p2p error: wait 5 sec and reconnect");
             close(p2p_ctx->sock_fd);
             sleep(5);
             if ((ret = p2p_connect(p2p_ctx, dbptr->current_height))) {
                 logerrf("block sync: p2p error: cannot connect with bitcoin daemon");
                 return ret;
             }
         }
    }
    return 0;
}
