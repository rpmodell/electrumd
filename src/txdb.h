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

#ifndef __TXDB_H__
#define __TXDB_H__

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef __FreeBSD__
#include <db5/db.h>
#else
#include <db.h>
#endif

#include "util.h"
#include "bitcoin_common.h"
#include "hashes_vec.h"

#define TXDB_UNSPENT 0x00000001
#define TXDB_SPENT   0x00000002

PACKED_STRUCT utxo_dbt {
    int64_t value; // the amount
    uint32_t height; //<<--- height at which utxo is located used to fetch txhash from db easily
    uint16_t tx_index; // the index of the tx in which holds this output is in the block
    uint16_t tx_pos; // the output position in the transaction outputs vec
    uint8_t txid_prefix[8]; // the txid (easy lookup of the confirmed height (if confirmed))
};

PACKED_STRUCT txin_dbt {
    uint32_t height; //<<--- height at which vin is located used to fetch txhash from db easily
    uint16_t tx_index; // the index of the tx in which holds this input is in the block
    uint16_t prev_out_index; // the index of the vout used in input
};

typedef struct {
    int64_t value;
    uint8_t txid_prefix[8];
    uint8_t txhash[32];
    uint32_t height; //confirmed height
    uint16_t tx_index;
    uint16_t tx_pos;
} Utxo;

typedef struct {
    uint8_t txhash[32];
    uint32_t height;

    uint16_t tx_index;
} HistoryItem;

/*
    txDB structure:
    * txs.db:
      | tx_hash prefix (8bytes)      | height (uint32_t) |

    * txins.db
      | prevout_hash_prefix (8bytes) | confirmed height  |

    * txouts.db
      | SHA256(scripthash) (8bytes)  | utxo_dbt          |
*/
typedef struct {
    char db_dir[512];
    long current_height;
    DB_ENV *env_ptr;
    DB *headers_ptr;
    DB *txhashes_ptr;
    DB *txins_ptr; // key txindex (outpoint hash prefix) hash first 8 bytes, value height
    DB *txouts_ptr; // key outpoint first 8 bytes, value utxo_dbt
} TXDB;

/**
 * Opens and initializes a database.
 *
 * @param dbptr Pointer to a TXDB structure that will hold the database handle.
 * @param db_dir Directory path where the database files are located.
 * @param cache_size Database cache size.
 * @param start_height The starting block height for processing transactions.
 * @return 0 on success, non-zero error code on failure.
 */
int txdb_open(TXDB *dbptr, const char *db_dir, unsigned int cache_size, long start_height);

/**
 * Closes the database.
 *
 * @param dbptr Pointer to a TXDB structure that will hold the database handle.
 * @return 0 on success, non-zero error code on failure.
 */
size_t txdb_close(TXDB *dbptr);

/**
 * Flushes the db to disk.
 *
 * @param dbptr Pointer to a TXDB structure that will hold the database handle.
 * @return 0 on success, non-zero error code on failure.
 */
size_t txdb_flush(TXDB *dbptr);

/**
 * Walk through all the utxos with a specific scripthash in the database and
 * returns a list of them.
 *
 * @param dbptr Pointer to a TXDB structure that will hold the database handle.
 * @param scripthash Pointer to the scripthash used to filter the utxos.
 * @param utxosp double pointer to the output. This function will automatically allocate memory,
 *          is caller responsability to free the memory.
 * @param mode addidtional filtering rule for the utxo, default is 0 no filter,
 *          TXDB_UNSPENT only unspent outputs,
 *          TXDB_SPENT only spent outputs
 * @return 0 if no utxos found, otherwise the size of the utxos found.
 */
size_t txdb_lookup_utxos(TXDB *dbptr, const uint8_t *scripthash, Utxo **utxosp, int mode);

/**
 * Walk through all the utxos with a specific scripthash in the database, retrieves
 * all the inputs that were using these utxos, store the result in a list of (txhash, heght)
 * tuple.
 * Returns the sorted list in the blockchain order;
 *
 * @param dbptr Pointer to a TXDB structure that will hold the database handle.
 * @param scripthash Pointer to the scripthash used to filter the utxos.
 * @param historyp double pointer to the output. This function will automatically allocate memory,
 *          is caller responsability to free the memory.
 * @param limit the size limit of the output
 * @return 0 if no utxos found, otherwise the size of the utxos found.
 */
size_t txdb_history(TXDB *dbptr, uint8_t *scripthash, HistoryItem **historyp, size_t limit);

/**
 * Retrieves a transaction hash of a specific block heght at a specific index.
 *
 * @param dbptr Pointer to a TXDB structure that will hold the database handle.
 * @param tx_hash pointer to the output hash, is caller responsability to preallocate 32 bytes
 *          of memory to hold the output.
 * @param height block height.
 * @param tx_index index of the transaction in the block.
 * @return 0 on success, non-zero error code on failure.
 */
int txdb_lookup_txhash(TXDB *dbptr, uint8_t *tx_hash, uint32_t height, uint16_t tx_index);

/**
 * Returns a vector containing all the transaction hashes of a block at a specific heght.
 *
 * @param dbptr Pointer to a TXDB structure that will hold the database handle.
 * @param hashes pointer to the output HashesVec, the hashes vec is expected already initialized,
 *               the hashes will be appended to the tail of the vec.
 * @param height block height.
 * @return 0 on success, non-zero error code on failure.
 */
int txdb_lookup_txhashes_at_height(TXDB *dbptr, HashesVec *hashes, uint32_t height);
int txdb_store_block_header(TXDB *dbptr, const uint8_t *data, uint32_t height);
int txdb_get_block_header(TXDB *dbptr, uint8_t *data, uint32_t height);
int txdb_store_txs(TXDB *dbptr, BtcTx *txs, size_t txs_sz, uint32_t height);
int txdb_bulk_store_txs(TXDB *dbptr, BtcTx *txs, size_t txs_sz, uint32_t height);


/*
 * Test functions for database needs to be removed from header
 * file.
 */

//int db_init_open(DB **dbp, const char *db_path);
//int db_put(DB *dbp, const char *key, size_t key_sz, void *data, size_t data_sz);
int db_get(DB *dbp, const char *key, size_t key_sz, DBT *data_ptr);

#endif
