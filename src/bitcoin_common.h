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

#ifndef __BITCOIN_COMMON__
#define __BITCOIN_COMMON__

#include <stdint.h>
#include <stddef.h>

#include "util.h"

#define SATS(B) (B*100000000)

#define IS_MAINNET(S) (strcmp(S, "main") == 0)
#define IS_TESTNET(S) (strcmp(S, "test") == 0)
#define IS_REGTEST(S) (strcmp(S, "regtest") == 0)

//refs: https://github.com/bitcoin/bitcoin/blob/master/src/consensus/consensus.h
#define MAX_BLOCK_SERIALIZED_SIZE 4000000
#define BLOCK_HEADER_SIZE 80

#define MIN_VARINT_SZ sizeof(uint8_t)
#define MAX_VARINT_SZ sizeof(uint64_t)

typedef PACKED_STRUCT {
    int32_t version;
    char prev_hash[32];
    char markle_root_hash[32];
    uint32_t time;
    uint32_t nbits;
    uint32_t nonce;
} BtcBlockHeader;

typedef struct {
    uint8_t prev_out_hash[32]; // outpoint prev out txhash!
    uint32_t prev_out_index; // outpoint previous output index
    uint64_t script_len;
    uint8_t *script;
    uint32_t sequence;
} BtcTxIn;

typedef struct {
    int64_t value;
    uint64_t pk_script_len;
    uint8_t *pk_script;
    uint8_t pk_script_hash[32]; //pre-calculate
} BtcTxOut;

typedef struct {
    uint8_t txid[32];
    int32_t version;
    uint64_t tx_in_count;
    BtcTxIn *tx_in;
    uint64_t tx_out_count;
    BtcTxOut *tx_out;
    uint32_t lock_time;
} BtcTx;

// typedef struct {
//     BtcBlockHeader header;
//     size_t txs_sz;
//     BtcTx *txs;
// } BtcBlock;

int btc_read_varint(uint8_t *buf, uint64_t *n);
int btc_write_varint(uint8_t *buf, uint64_t n);

int btc_parse_tx(BtcTx *tx, uint8_t **block_ptr, uint8_t *block_end_ptr);
size_t btc_parse_txs(BtcTx **txs_ptr, uint8_t *block, size_t block_sz);
void btc_tx_free(BtcTx tx);
void btc_txs_free(BtcTx *txs, size_t txs_sz);

#endif
