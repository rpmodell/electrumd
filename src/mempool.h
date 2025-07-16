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

#ifndef __MEMPOOL_H__
#define __MEMPOOL_H__

#include "bitcoin_common.h"
#include "txdb.h"
#include "bitcoin_rpc.h"
#include "bitcoin_p2p.h"

struct mc_tx_entry {
    BtcTx tx;
    double fee;
    size_t vsize; //vbytes
    int loc;

    struct mc_tx_entry *prev;
    struct mc_tx_entry *next;
};

struct fee_hist_entry {
    size_t vsize; //vbytes
    int64_t rate;   // sats/vbytes
};

typedef struct {
    uint8_t tx_hash[32];
    int64_t fee;
    int has_unconf_inputs;
} MempoolTxInfo;

typedef struct {
    long height;
    struct {
        struct mc_tx_entry *head;
        struct mc_tx_entry *tail;
        size_t size;
    } tx_cache;

    size_t fee_hist_capacity;
    size_t fee_hist_sz;
    struct fee_hist_entry *fee_hist;
} MempoolCache;

void mempool_cache_init(MempoolCache *mc_ptr);
void mempool_cache_print(MempoolCache *mc_ptr);
int mempool_cache_update(MempoolCache *mc_ptr, BitcoinRpcCtx *btc_rpc_ctx, HashesVec *new_scripthashes);
int mempool_cache_update2(MempoolCache *mc_ptr, BitcoinRpcCtx *btc_rpc_ctx, BtcP2pProtoCtx *p2p_ctx, HashesVec *new_scripthashes);
int mempool_tx_has_unconf_inputs(MempoolCache *mc_ptr, BtcTx *tx);
long mempool_tx_is_input(MempoolCache *mc_ptr, const uint8_t *txid);
size_t mempool_lookup_utxos(MempoolCache *mc_ptr, const uint8_t *scripthash, Utxo **utxos);
size_t mempool_lookup_txs(MempoolCache *mc_ptr, const uint8_t *scripthash, MempoolTxInfo **txinfos);

#endif
