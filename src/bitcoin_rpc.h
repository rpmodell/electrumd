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

#ifndef __CORE_RPC_H__
#define __CORE_RPC_H__

#include "ujson.h"
#include "hashes_vec.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *userpw;
    char *rpchost;
} BitcoinRpcCtx;

typedef struct {
    int version;
    char subversion[32];
    uint32_t protocolversion;
    int networkactive;
    double relayfee;
} BitcoinNetworkInfo;

typedef struct {
    size_t vsize;
    double fee_base;
    double fee_mod;
    double fee_ancestor;
    double fee_descendant;
} MempoolEntry;

int bitcoin_rpc_init(BitcoinRpcCtx *ctx, const char *host, const char *auth);
int getblockchaininfo(BitcoinRpcCtx *ctx, char *chain);
int getnetworkinfo(BitcoinRpcCtx *ctx, BitcoinNetworkInfo *info);
int getblockcount(BitcoinRpcCtx *ctx, long *height);
int getblockhash(BitcoinRpcCtx *ctx, long blkno, char *hash);
int getblockheader(BitcoinRpcCtx *ctx, const char *blkhash, uint8_t *header);
int getrawblock(BitcoinRpcCtx *ctx, const char *blkhash, uint8_t **rawblock);
int getrawtransaction(BitcoinRpcCtx *ctx, const char *txid, uint8_t **rawtx);
int getrawtransaction_json(BitcoinRpcCtx *ctx, const char *txid, int verbose, jsonobj *result);
int getrawmempool(BitcoinRpcCtx *ctx, HashesVec *new_txs_hashes);
int getmempoolentry(BitcoinRpcCtx *ctx, char *txid_str, MempoolEntry *mpe);
int sendrawtransaction(BitcoinRpcCtx *ctx, const char *rawtx, double feerate, char *txhash);
double estimatesmartfee(BitcoinRpcCtx *ctx, int conf_target);

#endif
