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

#ifndef __ELECTRUM_RPC_SRV_H__
#define __ELECTRUM_RPC_SRV_H__

#include "txdb.h"
#include "bitcoin_rpc.h"
#include "mempool.h"
#include "hashes_vec.h"

#define HISTORY_LIMIT 1000

#define MAX_RET_HEADERS 2016
#define MAX_CLIENTS 10

#define ELECTRUMD_SERVER_NAME "electrumd"
#define ELECTRUM_PROTOCOL_MIN "1.4"
#define ELECTRUM_PROTOCOL_MAX "1.4"
#define ELECTRUM_PROTOCOL_MIN_NUMBER 140
#define ELECTRUM_PROTOCOL_MAX_NUMBER 140

int electrum_rpc_srv_status_updated(void);
int electrum_rpc_height_notify(TXDB *dbptr, uint32_t height);
int electrum_rpc_new_scripthashes_notify(TXDB *dbptr, MempoolCache *mc_ptr, const HashesVec *new_scripthashes);

int electrum_server_init(char *pub_addr, int port, char *donation_addr, char *banner);
int electrum_server_start(MempoolCache *mcp, BitcoinRpcCtx *btc_rpc_ctx, TXDB *dbptr, const char *addr, int port);

#endif
