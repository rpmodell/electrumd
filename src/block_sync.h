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
 
#ifndef __BLOCK_SYNC_H__
#define __BLOCK_SYNC_H__

#include "txdb.h"
#include "bitcoin_rpc.h"
#include "bitcoin_p2p.h"
#include "mempool.h"

typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    BitcoinRpcCtx *core_rpc_ctx;
    BtcP2pProtoCtx *p2p_ctx;
    TXDB *dbptr;
    MempoolCache *mc_ptr;
    int syncing;
} SyncThreadCtx;

int prefetch_blocks(BitcoinRpcCtx *btc_rpc_ctx, TXDB *dbptr, HashesVec *touched_addresses);
int prefetch_blocks2(BitcoinRpcCtx *btc_rpc_ctx, BtcP2pProtoCtx *p2p_ctx, TXDB *dbptr, HashesVec *new_scripthashes);
int prefetch_blocks3(BitcoinRpcCtx *btc_rpc_ctx, BtcP2pProtoCtx *p2p_ctx, TXDB *dbptr, HashesVec *new_scripthashes);

int sync_thread_start(SyncThreadCtx *sctx, BitcoinRpcCtx *btc_rpc_ctx, BtcP2pProtoCtx *p2p_ctx, TXDB *dbptr, MempoolCache *mc_ptr);
void sync_thread_notify_update(SyncThreadCtx *sctx);
void sync_thread_set_syncing(SyncThreadCtx *sctx, int syncing);
void sync_thread_stop(SyncThreadCtx *sctx);

#endif
