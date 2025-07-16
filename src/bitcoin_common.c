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

#include <string.h>
#include <endian.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "bitcoin_common.h"

#define TX_FLAGS_IS_WITNESS(F) (flags & 1)

/*
 * Refs https://github.com/jonasschnelli/bitcoincore-indexd/blob/master/src/libbtc/src/serialize.c
 */
int btc_read_varint(uint8_t *buf, uint64_t *n)
{
    uint8_t e = buf[0];
    buf += 1;
    int skip = 1;
    *n = 0;
    if (e == 253) {
        uint16_t v = 0;
        memcpy(&v, buf, sizeof(uint16_t));
        *n = le16toh(v);
        skip += sizeof(uint16_t);
    } else if (e == 254) {
        uint32_t v = 0;
        memcpy(&v, buf, sizeof(uint32_t));
        *n = le32toh(v);
        skip += sizeof(uint32_t);
    } else if (e == 255) {
        uint64_t v = 0;
        memcpy(&v, buf, sizeof(uint64_t));
        *n = le64toh(v);
        skip += sizeof(uint64_t);
    } else {
        *n = (uint64_t) e;
        skip = 1;
    }
    return skip;

}

int btc_write_varint(uint8_t *buf, uint64_t n)
{
    int skip = 1;
    if (n < 253) {
        buf[0] = (uint8_t) n;
    } else if (n < 0x10000) {
        buf[0] = 253;
        uint16_t v = htole16(n);
        memcpy(buf+1, &v, sizeof(uint16_t));
        skip += sizeof(uint16_t);
    } else if (n < 0x100000000) {
        buf[0] = 254;
        uint32_t v = htole32(n);
        memcpy(buf+1, &v, sizeof(uint32_t));
        skip += sizeof(uint32_t);
    } else {
        buf[0] = 255;
        uint64_t v = htole64(n);
        memcpy(buf+1, &v, sizeof(uint64_t));
        skip += sizeof(uint64_t);
    }

    return skip;

}

void btc_tx_free(BtcTx tx)
{
    size_t i;
    for (i = 0; i < tx.tx_in_count; i++) {
        if (tx.tx_in[i].script && tx.tx_in[i].script_len)
            free(tx.tx_in[i].script);
    }

    for (i = 0; i < tx.tx_out_count; i++) {
        if (tx.tx_out[i].pk_script && tx.tx_out[i].pk_script_len)
            free(tx.tx_out[i].pk_script);
    }

    if (tx.tx_in) {
        free(tx.tx_in);
        tx.tx_in = NULL;
    }

    if (tx.tx_out) {
        free(tx.tx_out);
        tx.tx_out = NULL;
    }
}

void btc_txs_free(BtcTx *txs, size_t txs_sz)
{
    if (!txs)
        return;

    size_t i;
    for (i = 0; i < txs_sz; i++) {
        btc_tx_free(txs[i]);
    }
    free(txs);
}

/*
 * Refs https://github.com/jonasschnelli/bitcoincore-indexd/blob/master/src/libbtc/src/tx.c
 */
int btc_parse_tx(BtcTx *tx, uint8_t **block_ptr, uint8_t *block_end_ptr)
{
    memset(tx, 0, sizeof(BtcTx));

    uint64_t tx_in_count = 0, tx_out_count = 0, script_len = 0;
    uint8_t flags = 0;

    uint8_t *tx_start_ptr = *block_ptr;
    uint8_t *tx_nowit_start_ptr = tx_start_ptr;
    uint8_t *tx_nowit_end_ptr = NULL;
    size_t itx, toread_sz = sizeof(int32_t);

    if (*block_ptr + toread_sz > block_end_ptr)
        goto tx_parse_fail;

    memcpy(&tx->version, *block_ptr, toread_sz);
    *block_ptr += toread_sz;

    if (*block_ptr + MIN_VARINT_SZ > block_end_ptr)
        goto tx_parse_fail;

    *block_ptr += btc_read_varint(*block_ptr, &tx_in_count);
    if (tx_in_count == 0) {
        /* We read a dummy or an empty vin. */
        flags = **block_ptr;
        *block_ptr += 1;
        if (flags != 0) {
            // contains witness, deser the vin len
            if (*block_ptr + MIN_VARINT_SZ > block_end_ptr)
                goto tx_parse_fail;

            /* If witness we start the tx_legacy_start_ptr from the legacy start to calculate txid */
            tx_nowit_start_ptr = *block_ptr;
            *block_ptr += btc_read_varint(*block_ptr, &tx_in_count);
        }
    }

    if (tx_in_count > 0) {
        tx->tx_in = (BtcTxIn*) malloc(tx_in_count * sizeof(BtcTxIn));
        for (itx = 0; itx < tx_in_count; itx++) {
            toread_sz = 32;
            if (*block_ptr + toread_sz > block_end_ptr)
                goto tx_parse_fail;

            memcpy(tx->tx_in[itx].prev_out_hash, *block_ptr, toread_sz);
            *block_ptr += toread_sz;

            toread_sz = sizeof(uint32_t);
            if (*block_ptr + toread_sz > block_end_ptr)
                goto tx_parse_fail;

            memcpy(&tx->tx_in[itx].prev_out_index, *block_ptr, toread_sz);
            *block_ptr += toread_sz;

            if (*block_ptr + MIN_VARINT_SZ > block_end_ptr)
                goto tx_parse_fail;

            *block_ptr += btc_read_varint(*block_ptr, &script_len);

            if (*block_ptr + script_len > block_end_ptr)
                goto tx_parse_fail;

            tx->tx_in[itx].script = NULL;
            if (script_len > 0) {
                tx->tx_in[itx].script = (uint8_t*) malloc(script_len * sizeof(uint8_t));
                memcpy(tx->tx_in[itx].script, *block_ptr, script_len);
                *block_ptr += script_len;
            }
            tx->tx_in[itx].script_len = script_len;

            toread_sz = sizeof(uint32_t);
            if (*block_ptr + toread_sz > block_end_ptr)
                goto tx_parse_fail;

            memcpy(&tx->tx_in[itx].sequence, *block_ptr, toread_sz);
            *block_ptr += toread_sz;
        }
        tx->tx_in_count = tx_in_count;
    }

    if (*block_ptr + MIN_VARINT_SZ > block_end_ptr)
            goto tx_parse_fail;

    *block_ptr += btc_read_varint((*block_ptr), &tx_out_count);
    if (tx_out_count > 0) {
        tx->tx_out = (BtcTxOut*) malloc(tx_out_count * sizeof(BtcTxOut));
        for (itx = 0; itx < tx_out_count; itx++) {
            toread_sz = sizeof(int64_t);
            if (*block_ptr + toread_sz > block_end_ptr)
                goto tx_parse_fail;

            memcpy(&tx->tx_out[itx].value, *block_ptr, toread_sz);
            *block_ptr += toread_sz;

            if (*block_ptr + MIN_VARINT_SZ > block_end_ptr)
                goto tx_parse_fail;

            *block_ptr += btc_read_varint(*block_ptr, &script_len);

            if (*block_ptr + script_len > block_end_ptr)
                goto tx_parse_fail;

            tx->tx_out[itx].pk_script = NULL;
            if (script_len > 0) {
                tx->tx_out[itx].pk_script = (uint8_t*) malloc(script_len * sizeof(uint8_t));
                memcpy(tx->tx_out[itx].pk_script, *block_ptr, script_len);

                /* Pre-calculate the script hash */
                sha256(tx->tx_out[itx].pk_script_hash, *block_ptr, script_len);
                *block_ptr += script_len;
            }
            tx->tx_out[itx].pk_script_len = script_len;
        }
        tx->tx_out_count = tx_out_count;
    }

    tx_nowit_end_ptr = *block_ptr;

    if (TX_FLAGS_IS_WITNESS(flags)) {
        uint64_t j, vlen = 0, skip = 0;
        /* The witness flag is present, and we support witnesses. */
        // flags ^= 1;
        for (itx = 0; itx < tx_in_count; itx++) {
            if (*block_ptr + MIN_VARINT_SZ > block_end_ptr)
                    goto tx_parse_fail;

            *block_ptr += btc_read_varint(*block_ptr, &vlen);


            for (j = 0; j < vlen; j++) {
                /* For now we just skip the witness */
                if (*block_ptr + MIN_VARINT_SZ > block_end_ptr)
                        goto tx_parse_fail;

                *block_ptr += btc_read_varint(*block_ptr, &skip);
                if (*block_ptr + skip > block_end_ptr)
                    goto tx_parse_fail;

                *block_ptr += skip;
            }
        }
    }

    toread_sz = sizeof(uint32_t);
    if (*block_ptr + toread_sz > block_end_ptr)
        goto tx_parse_fail;

    memcpy(&tx->lock_time, *block_ptr, toread_sz);
    *block_ptr += toread_sz;

    if (TX_FLAGS_IS_WITNESS(flags)) {
        /* Calculate txid if tx has witness */
        double_sha256_concat3(
            tx->txid,
            &tx->version, sizeof(tx->version),      //version
            tx_nowit_start_ptr, tx_nowit_end_ptr - tx_nowit_start_ptr, //tx data without witness
            &tx->lock_time, sizeof(tx->lock_time)   // lock time
        );
    } else {
        /* Tx don't use witness so txid==txhash*/
        double_sha256(tx->txid, tx_start_ptr, *block_ptr - tx_start_ptr);
    }

    return 0;

tx_parse_fail:
    return -2;
}

size_t btc_parse_txs(BtcTx **txs_ptr, uint8_t *block, size_t block_sz)
{
    uint8_t *block_ptr = block;
    uint8_t *block_end_ptr = block + block_sz;
    block_ptr += BLOCK_HEADER_SIZE; //skip block header
    if (block_ptr + 1 > block_end_ptr)
        return 0;

    uint64_t txns = 0;
    block_ptr += btc_read_varint(block_ptr, &txns);

    *txs_ptr = (BtcTx*) malloc(txns * sizeof(BtcTx));

    size_t i;
    for (i = 0; i < txns; i++) {
        if (btc_parse_tx(*txs_ptr + i, &block_ptr, block_end_ptr)) {
            btc_txs_free(*txs_ptr, i+1);

            return 0;
        }
    }

    return (size_t) txns;
}
