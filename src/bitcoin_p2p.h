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

#ifndef BITCOIN_P2P_H
#define BITCOIN_P2P_H

#include "hashes_vec.h"

#include <stdint.h>
#include <stddef.h>

#define MSG_BLOCK 2
#define MSG_WITNESS_BLOCK 0x40000002

#define MSG_TX 1
#define MSG_WITNESS_TX 0x40000001

#define MSG_CMD_INV "inv"
#define MSG_CMD_BLOCK "block"
#define MSG_CMD_TX "tx"

#define P2P_INV_MAX_SIZE 500

typedef struct {
    char *addr;
    int port;
    uint32_t chain_magic;

    int sock_fd;
} BtcP2pProtoCtx;

int p2p_ctx_init(BtcP2pProtoCtx *ctx, const char *addr, int port, const char *chain);
int p2p_connect(BtcP2pProtoCtx *ctx, int32_t height);

int p2p_ping(BtcP2pProtoCtx *ctx);
int p2p_get_data(BtcP2pProtoCtx *ctx, uint8_t **blkhashes, size_t hashes_sz, uint32_t ivt);
int p2p_receive_message(BtcP2pProtoCtx *ctx, uint8_t **rawblock, size_t *block_sz, const char *cmd);

/* Can return up to 2000 hashes */
int p2p_get_headers_heashes(BtcP2pProtoCtx *ctx, HashesVec *out_hashes, uint8_t **blkhash, size_t blkhash_sz, const uint8_t *stophash);

#endif
