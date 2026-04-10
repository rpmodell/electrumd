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
 
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "util.h"
#include "logging.h"

#include "mempool.h"

#define FEE_HIST_BIN_SIZE 25000.0

int fee_hist_rev_sort_comp(const void *a, const void *b)
{
    return ((struct fee_hist_entry*) b)->rate - ((struct fee_hist_entry*) a)->rate;
}

static inline void fee_histogram_init(FeeHistogram *hist, size_t cap)
{
	hist->capacity = cap;
    hist->size = 0;
    hist->hist = (struct fee_hist_entry*) malloc(cap * sizeof(struct fee_hist_entry));

}

static void fee_histogram_append(FeeHistogram *hist, int64_t rate, size_t vsize)
{
    size_t i;
    for (i = 0; i < hist->size; i++) {
        if (hist->hist[i].rate == rate) {
            hist->hist[i].vsize += vsize;
            return;
        }
    }

    if (hist->size + 1 >= hist->capacity) {
        hist->capacity++;
        hist->hist = (struct fee_hist_entry*) realloc(hist->hist, hist->capacity * sizeof(struct fee_hist_entry));
    }
    hist->hist[hist->size].rate = rate;
    hist->hist[hist->size].vsize = vsize;
    hist->size++;
}

static inline void fee_histogram_free(FeeHistogram *hist)
{
	hist->capacity = 0;
	hist->size = 0;
	free(hist->hist);
	hist->hist = NULL;
}

static void fee_histogram_compact(FeeHistogram *dest_hist, FeeHistogram *src_hist)
{
    /*
     * Compact the histogram,
     * refs:
     *
     * The compact histogram is an array of (fee_rate, vsize) values.
     * vsize_n is the cumulative virtual size of mempool
     * transactions with a fee rate in the interval
     * [rate_(n-1), rate_n)], and rate_(n-1) > rate_n.
     * Intervals are chosen to create tranches containing at
     * least a certain cumulative size (bin_size) of transactions.
     */
    qsort(src_hist->hist, src_hist->size, sizeof(struct fee_hist_entry), &fee_hist_rev_sort_comp);
    double bin_size = FEE_HIST_BIN_SIZE;
	size_t i, cm_size = 0, vsize = 0;

    dest_hist->size = 0; // reset the size of the fee hist
    for (i = 0; i < src_hist->size; i++) {
        vsize = src_hist->hist[i].vsize;

        if (i > 0 && vsize > 2 * bin_size && cm_size > 0) {
            // push compact to the top of list
			fee_histogram_append(dest_hist, src_hist->hist[i-1].rate, cm_size);
            bin_size *= 1.1;
            cm_size = 0;
        }

        cm_size += vsize;
        if (cm_size > bin_size) {
            // push compact to the top of list
			fee_histogram_append(dest_hist, src_hist->hist[i].rate, cm_size);
            bin_size *= 1.1;
            cm_size = 0;
        }
    }

}

void mempool_cache_init(MempoolCache *mcp)
{
    mcp->tx_cache.head = NULL;
    mcp->tx_cache.tail = NULL;
    mcp->tx_cache.size = 0;

	fee_histogram_init(&mcp->fee_histogram, 100);
}

void mempool_cache_print(MempoolCache *mc_ptr)
{
    char txhash_str[65];
    struct mc_tx_entry *e;
    for (e = mc_ptr->tx_cache.head; e; e = e->next) {
        bytes_to_hex_reverse(e->tx.txid, 32, txhash_str);
        fprintf(stderr, "%d: tx_id: %s, fee: %lf\n", e->loc, txhash_str, e->fee);
    }
}


long mempool_tx_is_input(MempoolCache *mc_ptr, const uint8_t *txid_prefix)
{
    size_t itx;
    struct mc_tx_entry *e = NULL;
    for (e = mc_ptr->tx_cache.head; e; e = e->next) {
        for (itx = 0; itx < e->tx.tx_in_count; itx++) {
            if (memcmp(e->tx.tx_in[itx].prev_out_hash, txid_prefix, 8) == 0)
                return 1;
        }
    }
	return 0;
}

int mempool_tx_has_unconf_inputs(MempoolCache *mcp, BtcTx *tx)
{
    size_t i;
    struct mc_tx_entry *e = NULL;
    for (i = 0; i < tx->tx_in_count; i++) {
        for (e = mcp->tx_cache.head; e; e = e->next) {
            if (memcmp(e->tx.txid, tx->tx_in[i].prev_out_hash, 32) == 0)
                return 1;
        }
    }
    return 0;
}

/* note: change with utxo filtering rules */
size_t mempool_lookup_utxos(MempoolCache *mc_ptr, const uint8_t *scripthash, Utxo **utxos)
{
    size_t outs_sz = 0, outs_capacity = 64;
    (*utxos) = (Utxo*) malloc(outs_capacity * sizeof(Utxo));


    size_t itx;
    struct mc_tx_entry *e = NULL;
    for (e = mc_ptr->tx_cache.head; e; e = e->next) {
        for (itx = 0; itx < e->tx.tx_out_count; itx++) {
            if (memcmp(e->tx.tx_out[itx].pk_script_hash, scripthash, 32) == 0) {
                if (outs_sz >= outs_capacity) {
                    outs_capacity *= 2;
                    (*utxos) = (Utxo*) realloc(*utxos, outs_capacity * sizeof(Utxo));
                }

                Utxo *utxo = &((*utxos)[outs_sz++]);
                utxo->height = 0;
                utxo->tx_index = 0;
                utxo->tx_pos = itx;
                utxo->value = e->tx.tx_out[itx].value;
                memcpy(utxo->txhash, e->tx.txid, 8);
                memcpy(utxo->txid_prefix, e->tx.txid, 8);
            }
        }
	}
	return outs_sz;
}

size_t mempool_lookup_txs(MempoolCache *mc_ptr, const uint8_t *scripthash, MempoolTxInfo **txinfos)
{
    if (!mc_ptr->tx_cache.head)
        return 0;

    size_t outs_sz = 0, outs_capacity = 64;
    (*txinfos) = (MempoolTxInfo*) malloc(outs_capacity * sizeof(MempoolTxInfo));
    size_t itx;
    struct mc_tx_entry *e = NULL;
    for (e = mc_ptr->tx_cache.head; e; e = e->next) {
        for (itx = 0; itx < e->tx.tx_out_count; itx++) {
            if (memcmp(e->tx.tx_out[itx].pk_script_hash, scripthash, 32) == 0) {
                if (outs_sz >= outs_capacity) {
                    outs_capacity *= 2;
                    (*txinfos) = (MempoolTxInfo*) realloc((*txinfos), outs_capacity * sizeof(MempoolTxInfo));
                }

                MempoolTxInfo *info = &((*txinfos)[outs_sz++]);
                info->fee = (int64_t) e->fee;
                info->has_unconf_inputs = mempool_tx_has_unconf_inputs(mc_ptr, &e->tx);
                memcpy(info->tx_hash, e->tx.txid, 32);
                break;
            }
        }
    }
    return outs_sz;
}


struct mc_tx_entry *tx_cache_find_txid(MempoolCache *mcp, const uint8_t *txid)
{
    struct mc_tx_entry *e = mcp->tx_cache.head;
    for (; e; e = e->next) {
        if (memcmp(e->tx.txid, txid, 32) == 0)
            return e;
    }
    return NULL;
}

struct mc_tx_entry *tx_cache_put(MempoolCache *mcp, BtcTx tx)
{
    // struct mc_tx_entry *e = NULL;
    // for (e = mcp->tx_cache.head; e; e = e->next) {
    //     if (memcmp(tx.tx_hash, e->tx.tx_hash, 32) == 0) {
    //         btc_tx_free(e->tx);
    //         e->tx = tx;
    //         return e;
    //     }
    // }

    struct mc_tx_entry *new_entry = (struct mc_tx_entry*) malloc(1 * sizeof(struct mc_tx_entry));
    memset(new_entry, 0, sizeof(struct mc_tx_entry));

    if (mcp->tx_cache.tail) {
        new_entry->prev = mcp->tx_cache.tail;
        mcp->tx_cache.tail->next = new_entry;
    }
    new_entry->tx = tx;
    mcp->tx_cache.tail = new_entry;
    if (!mcp->tx_cache.head) {
        mcp->tx_cache.head = mcp->tx_cache.tail;
    }

    mcp->tx_cache.size++;
    return new_entry;
}

// returns the previous element of the removed one
struct mc_tx_entry *tx_cache_remove(MempoolCache *mcp, struct mc_tx_entry *e)
{
    struct mc_tx_entry *prev = e->prev;
    if (e == mcp->tx_cache.tail)
        mcp->tx_cache.tail = prev;

    if (e == mcp->tx_cache.head)
        mcp->tx_cache.head = e->next;

    if (prev)
        prev->next = e->next;

    if (e->next)
        e->next->prev = prev;

    btc_tx_free(e->tx);
    free(e);

    mcp->tx_cache.size--;
    return prev;
}

int mempool_cache_update(MempoolCache *mcp, BitcoinRpcCtx *btc_rpc_ctx, HashesVec *new_scripthashes)
{
    HashesVec new_txs_hashes;
    hashes_vec_init(&new_txs_hashes);

    if (getrawmempool(btc_rpc_ctx, &new_txs_hashes))
        return -1;

    logdebugf("mempool cache update new mempool size = %ld", new_txs_hashes.size);

    // Discard txs not in mempool from bitcoin daemon, keep the others
    ssize_t i, j, rawtx_sz;
    struct mc_tx_entry *e = mcp->tx_cache.head;
    while (e) {
        if (hashes_vec_find(&new_txs_hashes, e->tx.txid) == -1)
            e = tx_cache_remove(mcp, e);

        if (e)
            e = e->next;
        else
            break;
    }

    int ret = 0;

    // Fetch new mempool txs and append to cache, if to_fetch_sz is 0 nothing to be done
    uint8_t *rawtx = NULL;
    size_t new_count;
    BtcTx tx = {0};
    char txid_str[65];
    struct mempool_entry mpe;

    new_count = 0;
    for (i = 0; i < new_txs_hashes.size; i++) {
        if (tx_cache_find_txid(mcp, new_txs_hashes.v[i]))
            continue;

        bytes_to_hex_reverse(new_txs_hashes.v[i], 32, txid_str);
        if ((ret = getmempoolentry(btc_rpc_ctx, txid_str, &mpe))) {
            logerrf("failed getmempool entry");
            if (ret == -5)
                continue;
            else
                goto mempool_cache_update_end;
        }

         if ((rawtx_sz = getrawtransaction(btc_rpc_ctx, txid_str, &rawtx)) <= 0) {
             logerrf("failed fetch mempool tx code=%d", rawtx_sz);
             continue;
         }

         if (btc_parse_tx(&tx, &rawtx, rawtx + rawtx_sz)) {
             logerrf("failed parse mempool tx number %d, %s != %s", i);
             ret = -1;
             goto mempool_cache_update_end;
         }

        // touched_addreses is an optional parameter
        if (new_scripthashes) {
            for (j = 0; j < tx.tx_out_count; j++) {
                if (hashes_vec_find(new_scripthashes, tx.tx_out[j].pk_script_hash) == -1)
                    hashes_vec_add(new_scripthashes, tx.tx_out[j].pk_script_hash);
            }
        }

        e = tx_cache_put(mcp, tx);
        e->fee = SATS(mpe.fee_base);
        e->loc = i;
        e->vsize = mpe.vsize;

        new_count++;
    }

	FeeHistogram mp_hist;
	fee_histogram_init(&mp_hist, 100 + new_count);

    int64_t fee_rate = 0;
    //next construct the full fee histogram
    for (e = mcp->tx_cache.head; e; e = e->next) {
        fee_rate = (int64_t) (floor(e->fee / e->vsize * 10.0) / 10.0);
        fee_histogram_append(&mp_hist, fee_rate, e->vsize);
    }

	fee_histogram_compact(&mcp->fee_histogram, &mp_hist);
	fee_histogram_free(&mp_hist);

    logdebugf("mempool cache: fetched new %ld mempool transactions, current mempool size is %ld", new_count, mcp->tx_cache.size);

mempool_cache_update_end:
    hashes_vec_free(&new_txs_hashes);
    return ret;
}

int mempool_cache_update2(MempoolCache *mcp, BitcoinRpcCtx *btc_rpc_ctx, BtcP2pProtoCtx *p2p_ctx, HashesVec *new_scripthashes)
{
    HashesVec new_txs_hashes;
    hashes_vec_init(&new_txs_hashes);

    if (getrawmempool(btc_rpc_ctx, &new_txs_hashes))
        return -1;

    logdebugf("mempool cache update new mempool size = %ld", new_txs_hashes.size);

    // Discard txs not in mempool from bitcoin daemon, keep the others
    ssize_t i, j;
    struct mc_tx_entry *e = mcp->tx_cache.head;
    while (e) {
        if (hashes_vec_find(&new_txs_hashes, e->tx.txid) == -1)
            e = tx_cache_remove(mcp, e);

        if (!e)
            break;

        e = e->next;
    }

    int ret = 0;

    // Fetch new mempool txs and append to cache, if to_fetch_sz is 0 nothing to be done
    uint8_t *rawtx = NULL;
    size_t rawtx_sz, new_count;
    BtcTx tx = {0};
    char txid_str[65];
    struct mempool_entry mpe;

    for (i = 0; i < new_txs_hashes.size; i++) {
        if (tx_cache_find_txid(mcp, new_txs_hashes.v[i]))
            hashes_vec_remove(&new_txs_hashes, i--);
    }

    new_count = 0;
    if ((p2p_get_data(p2p_ctx, new_txs_hashes.v, new_txs_hashes.size, MSG_WITNESS_TX)) < 0) {
        logdebugf("mempool sync: fail request transactions: fail: %s", strerror(errno));
        goto mempool_cache_update_end;
    }

    for (i = 0; i < new_txs_hashes.size; i++) {
        if ((p2p_receive_message(p2p_ctx, &rawtx, &rawtx_sz, MSG_CMD_TX))) {
            logerrf("failed fetch mempool tx: %s", rawtx_sz, strerror(errno));
            continue;
        }

         if (btc_parse_tx(&tx, &rawtx, rawtx + rawtx_sz)) {
             logerrf("failed parse mempool tx number %d, %s != %s", i);
             ret = -1;
             goto mempool_cache_update_end;
         }

         bytes_to_hex_reverse(new_txs_hashes.v[i], 32, txid_str);
         if ((ret = getmempoolentry(btc_rpc_ctx, txid_str, &mpe))) {
             logerrf("failed getmempool entry");
             if (ret == -5)
                 continue;
             else
                 goto mempool_cache_update_end;
         }

        // touched_addreses is an optional parameter
        if (new_scripthashes) {
            for (j = 0; j < tx.tx_out_count; j++) {
                if (hashes_vec_find(new_scripthashes, tx.tx_out[j].pk_script_hash) == -1)
                    hashes_vec_add(new_scripthashes, tx.tx_out[j].pk_script_hash);
            }
        }

        e = tx_cache_put(mcp, tx);
        e->fee = SATS(mpe.fee_base);
        e->loc = i;
        e->vsize = mpe.vsize;

        new_count++;
    }


	FeeHistogram mp_hist;
	fee_histogram_init(&mp_hist, 100 + new_count);

    int64_t fee_rate = 0;
    //next construct the full fee histogram
    for (e = mcp->tx_cache.head; e; e = e->next) {
        fee_rate = (int64_t) (floor(e->fee / e->vsize * 10.0) / 10.0);
        fee_histogram_append(&mp_hist, fee_rate, e->vsize);
    }

	fee_histogram_compact(&mcp->fee_histogram, &mp_hist);
	fee_histogram_free(&mp_hist);

    logdebugf("mempool cache: fetched new %ld mempool transactions, current mempool size is %ld", new_count, mcp->tx_cache.size);

mempool_cache_update_end:
    hashes_vec_free(&new_txs_hashes);
    return ret;
}
