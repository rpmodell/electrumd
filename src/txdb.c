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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <pthread.h>

#include "logging.h"

#include "txdb.h"

#define HEIGHT_ITER_START 1

#define DB_MAX_FILE_SIZE (16*1024*1024) //16mb
#define DB_TXS_BLK_SIZE (8*1024) //8kb
#define DB_DEFAULT_BLK_SIZE (4*1024) //4kb

#define HEADERS_DB_FILE_NAME "headers.db"
#define TXHASHES_DB_FILE_NAME "txhashes.db"
#define TXINS_DB_FILE_NAME "txins.db"
#define TXOUTS_DB_FILE_NAME "txouts.db"
#define STATUS_FILE_NAME "status" //rename to dbstat

/*
 * Electrumd Database (TXDB) Structure Description
 * 
 * headers.db
 *   - Key: height (4 bytes)
 *   - Data: block_header (80 bytes)
 *     This database stores block headers, where the key is the block height.
 *     Each entry contains the full block header data, allowing quick access to block information by its height.
 * 
 * txhashes.db
 *   - Key: height (4 bytes) + tx_index (2 bytes, big endian)
 *   - Data: txid (32 bytes)
 *     This database maps a transaction's height and index to its transaction ID (txid).
 *     The key is a combination of the block height and the transaction index within that block, 
 *     facilitating the lookup of a txid based on its position in the blockchain.
 * 
 * txins.db
 *   - Key: prevout_hash_prefix (8 bytes) + prev_out_index (2 bytes)
 *   - Data: height (8 bytes) + tx_index (2 bytes)
 *     This database is used to lookup transaction inputs by the prefix of the previous output hash.
 *     The data includes the block height, transaction index, and the previous output index, providing
 *     details about the source of a transaction input.
 * 
 * txouts.db
 *   - Key: scripthash_prefix (8 bytes) + height (4 bytes)
 *   - Data: value (8 bytes) + tx_index (2 bytes) + tx_pos (2 bytes)
 *     This database maps transaction outputs by a composite key that includes the scripthash prefix.
 *     The data includes the txid prefix, value, height, transaction index, and position, allowing for efficient 
 *     lookup of specific transaction outputs based on the scripthash.
 */

PACKED_STRUCT utxo_key {
	uint8_t scripthash_prefix[8];
	uint32_t height; //<<--- height at which utxo is located used to fetch txhash from db easily
};

PACKED_STRUCT utxo_dbt {
    int64_t value; // the amount
    uint16_t tx_index; // the index of the tx in which holds this output is in the block
    uint16_t tx_pos; // the output position in the transaction outputs vec
};

PACKED_STRUCT txin_key {
	uint8_t txid_prefix[8];
	uint16_t prev_out_index; // the index of the vout used in input
};

PACKED_STRUCT txin_dbt {
    uint32_t height; //<<--- height at which vin is located used to fetch txhash from db easily
    uint16_t tx_index; // the index of the tx in which holds this input is in the block
};

PACKED_STRUCT txhash_key {
	uint32_t height; // block height in which tx is stored
	uint16_t tx_index; // index of the tx inside the block
};

static int db_init_open(struct dbi *db, const char *db_dir, const char *db_name, int compr, size_t blksz, ssize_t cache_buf_size)
{
	/* Create and initialize LevelDB database */
	char db_path[1024];
    sprintf(db_path, "%s/%s", db_dir, db_name);
	
	char *err = NULL;
	db->opts = leveldb_options_create();

	if (cache_buf_size > 0)
    	leveldb_options_set_write_buffer_size(db->opts, cache_buf_size);

	db->filtpol = leveldb_filterpolicy_create_bloom(10);

    leveldb_options_set_filter_policy(db->opts, db->filtpol);
    leveldb_options_set_max_file_size(db->opts, DB_MAX_FILE_SIZE);
    leveldb_options_set_compression(db->opts, compr);
    leveldb_options_set_block_size(db->opts, blksz);
    leveldb_options_set_create_if_missing(db->opts, 1);
    db->db = leveldb_open(db->opts, db_path, &err);

    if (err) {
		logerrf("txdb open %s fail: %s", db_name, err);
		/* reset error var */
		leveldb_free(err); 
		return -1;
    }
    
    // Add read and write options keep normal for now until we know how to configure
    db->wopts = leveldb_writeoptions_create();
    db->ropts = leveldb_readoptions_create(); 
	
	return 0;
}

int txdb_open(TXDB *dbptr, const char *db_dir, unsigned int cache_size, long start_height)
{
    FILE *status_fp = NULL;
    char db_path[1024];
    sprintf(db_path, "%s/%s", db_dir, STATUS_FILE_NAME);
    status_fp = fopen(db_path, "rb");
    if (status_fp) {
        if (fread(&dbptr->current_height, sizeof(long), 1, status_fp) != 1) {
            logerrf("txdb open: %s: cant read current height, resetting to %d", STATUS_FILE_NAME, start_height);
            dbptr->current_height = start_height;
        } else {
            loginfof("txdb open: height=%ld", dbptr->current_height);
        }
        fclose(status_fp);
    } else {
        logerrf("txdb open: %s: %s: cant read current height, resetting to %d", STATUS_FILE_NAME, strerror(errno), start_height);
        dbptr->current_height = start_height;
    }

    if (db_init_open(&dbptr->headers_ptr, db_dir, HEADERS_DB_FILE_NAME, leveldb_no_compression, DB_DEFAULT_BLK_SIZE, 0))
        return -1;

    if (db_init_open(&dbptr->txhashes_ptr, db_dir, TXHASHES_DB_FILE_NAME, leveldb_no_compression, DB_DEFAULT_BLK_SIZE, cache_size / 3))
        return -1;


    if (db_init_open(&dbptr->txins_ptr, db_dir, TXINS_DB_FILE_NAME, leveldb_snappy_compression, DB_TXS_BLK_SIZE, cache_size / 3))
        return -1;
	
    if (db_init_open(&dbptr->txouts_ptr, db_dir, TXOUTS_DB_FILE_NAME, leveldb_snappy_compression, DB_TXS_BLK_SIZE, cache_size / 3))
		return -1;

    strcpy(dbptr->db_dir, db_dir);
	return 0;
}

static int db_close(struct dbi *db)
{
    leveldb_writeoptions_destroy(db->wopts);
    leveldb_readoptions_destroy(db->ropts);
    leveldb_options_destroy(db->opts);
	leveldb_close(db->db);
	leveldb_filterpolicy_destroy(db->filtpol);
    return 0;
}

int txdb_close(TXDB *dbptr)
{
    if (db_close(&dbptr->headers_ptr)) {
        logerrf("txdb error closing %s db", HEADERS_DB_FILE_NAME);
		return -1;
    }

    if (db_close(&dbptr->txhashes_ptr)) {
        logerrf("txdb error closing %s db", TXHASHES_DB_FILE_NAME);
		return -1;
    }
    
    if (db_close(&dbptr->txins_ptr)) {
        logerrf("txdb error closing %s db", TXINS_DB_FILE_NAME);
		return -1;
    }
    
    if (db_close(&dbptr->txouts_ptr)) {
        logerrf("txdb error closing %s db", TXOUTS_DB_FILE_NAME);
		return -1;
    }

	return 0;
}

size_t txdb_flush(TXDB *dbptr)
{
    FILE *status_fp = NULL;
    char db_path[1024];
    sprintf(db_path, "%s/%s", dbptr->db_dir, STATUS_FILE_NAME);
    status_fp = fopen(db_path, "wb");
    if (!status_fp) {
        logerrf("txdb open: %s: %s", STATUS_FILE_NAME, strerror(errno));
    }
    if (fwrite(&dbptr->current_height, sizeof(long), 1, status_fp) != 1) {
        logerrf("txdb open: %s: cant write current height", STATUS_FILE_NAME);
        fclose(status_fp);
        return -1;
    }

    fclose(status_fp);
	return 0;
}

static void *db_compact(void *uo)
{
    leveldb_compact_range(((struct dbi*) uo)->db, NULL, 0, NULL, 0);
    return NULL;
}

int txdb_compact(TXDB *dbptr)
{
    if (!dbptr)
        return -1;

    pthread_t txouts_cmcpt_thread;
    pthread_create(&txouts_cmcpt_thread, NULL, &db_compact, &dbptr->txouts_ptr);

    pthread_t txins_cmcpt_thread;
    pthread_create(&txins_cmcpt_thread, NULL, &db_compact, &dbptr->txins_ptr);

    pthread_t txhash_cmcpt_thread;
    pthread_create(&txhash_cmcpt_thread, NULL, &db_compact, &dbptr->txhashes_ptr);

    pthread_join(txouts_cmcpt_thread, NULL);
    pthread_join(txins_cmcpt_thread, NULL);
    pthread_join(txhash_cmcpt_thread, NULL);

    return 0;
}

// DB put / get
static int db_put(struct dbi *db, void *key, size_t key_sz, void *dp, size_t data_sz)
{
	char *err = NULL;
	leveldb_put(db->db, db->wopts, (char*) key, key_sz, (char*) dp, data_sz, &err);

    if (err) {
		logerrf("db put error %s", err);
		leveldb_free(err); 
		return -1;
    }
    
	return 0;
}

int db_get(struct dbi *db, const void *key, size_t key_sz, void *data_ptr, size_t data_sz)
{
	char *err = NULL;
	size_t read_sz = 0;
	char *read_ptr = leveldb_get(db->db, db->ropts, (char*) key, key_sz, &read_sz, &err);

	//FIXME specify errors!
    if (err) {
		logerrf("db get error %s", err);
		leveldb_free(err); 
		return -1;
    }
    
    if (!read_ptr)
		return -1; // key not found
    
    if (read_sz != data_sz) {
		leveldb_free(read_ptr); 
		return -1; // size mismatch
	}
	
	// if size matches then ok we can copy data!
	if (data_ptr)
		memcpy(data_ptr, read_ptr, data_sz);
    
    leveldb_free(read_ptr);
    return 0;
}

static leveldb_iterator_t *create_iterator_at(struct dbi *db, const void *key, size_t key_sz)
{
    char *err = NULL;
    leveldb_iterator_t *iter = leveldb_create_iterator(db->db, db->ropts);
    leveldb_iter_seek(iter, (char*) key, key_sz);
    leveldb_iter_get_error(iter, &err);

    if (err) {
        logdebugf("txdb create iter error: %s", err);
        leveldb_free(err);
        leveldb_iter_destroy(iter);
        return NULL;
    }

    if (!leveldb_iter_valid(iter)) {
        leveldb_iter_destroy(iter);
        return NULL;
    }

    return iter;
}

static int txdb_get_txin(TXDB *dbptr, uint8_t *txid_prefix, uint16_t prev_out_index, struct txin_dbt *in_dbt)
{
    struct txin_key txink;
    txink.prev_out_index = prev_out_index;
    memcpy(txink.txid_prefix, txid_prefix, 8);
    
    return db_get(&dbptr->txins_ptr, &txink, sizeof(txink), in_dbt, sizeof(struct txin_dbt));
}

size_t txdb_lookup_utxos(TXDB *dbptr, const uint8_t *scripthash, Utxo **utxosp, int mode)
{
	int ret = 0;
    size_t utxo_sz = 0;
    size_t utxos_capacity = 0;
    
    struct utxo_key seek_key;
    seek_key.height = 0;
    memcpy(seek_key.scripthash_prefix, scripthash, 8);

    leveldb_iterator_t *iter = create_iterator_at(&dbptr->txouts_ptr, &seek_key, sizeof(seek_key));
    if (!iter) {
        logdebugf("utxo cannot be found %ul", *((uint64_t*)scripthash));
        return 0; //notfound
    }

	char *data = NULL;
    uint8_t txhash[32];
    size_t data_len;
    struct utxo_key ukey;
    struct utxo_dbt udbt;
    for (leveldb_iter_next(iter); leveldb_iter_valid(iter); leveldb_iter_next(iter)) {
        data = (char*) leveldb_iter_key(iter, &data_len);
        assert(data_len == sizeof(ukey));

		memcpy(&ukey, data, sizeof(ukey));
        if (memcmp(ukey.scripthash_prefix, scripthash, 8))
            break;

        data = (char*) leveldb_iter_value(iter, &data_len);
        assert(data_len == sizeof(udbt));

		memcpy(&udbt, data, sizeof(udbt));
        if ((ret = txdb_lookup_txhash(dbptr, txhash, ukey.height, udbt.tx_index))) {
            break;
        }

        //Is there a better way?
        if (mode & TXDB_UNSPENT) {
            if (!txdb_get_txin(dbptr, txhash, udbt.tx_pos, NULL)) { // this way we save ~ 14gb of space
                continue;
            }
        }

        if (utxo_sz >= utxos_capacity) {
            utxos_capacity = MAX(utxos_capacity, 1) * 2;
            (*utxosp) = (Utxo*) realloc((*utxosp), utxos_capacity * sizeof(Utxo));
        }

        Utxo *utxo = *utxosp + utxo_sz++;
        utxo->height = ukey.height;
        utxo->value = udbt.value;
        utxo->tx_index = udbt.tx_index;
        utxo->tx_pos = udbt.tx_pos;
        memcpy(utxo->txhash, txhash, sizeof(txhash));
    }

    leveldb_iter_destroy(iter);

    if (ret) {
        logerrf("txdb utxo lookup error %d", ret);
        return 0;
    }

	return utxo_sz;
}

int history_item_comp(const void *a, const void *b)
{
    /* If history items are in the same block then sort using the tx index */
    int diff = ((HistoryItem*) a)->height - ((HistoryItem*) b)->height;
    if (diff == 0)
        diff = ((HistoryItem*) a)->tx_index - ((HistoryItem*) b)->tx_index;

    return diff;
}

size_t txdb_history(TXDB *dbptr, uint8_t *scripthash, HistoryItem **historyp, size_t limit)
{
    int ret = 0;
    int input_found;
    size_t hist_sz = 0;
    size_t hist_capacity = 0;
    
    struct utxo_key seek_key;
    seek_key.height = 0;
    memcpy(seek_key.scripthash_prefix, scripthash, 8);

    leveldb_iterator_t *iter = create_iterator_at(&dbptr->txouts_ptr, &seek_key, sizeof(seek_key));
    if (!iter) {
        logdebugf("utxo cannot be found %ul", *((uint64_t*)scripthash));
        return 0; //notfound
    }

	char *data = NULL;
    size_t data_len, i;
    struct txin_dbt in_dbt;
    struct utxo_key ukey;
    struct utxo_dbt udbt;
    for (leveldb_iter_next(iter); leveldb_iter_valid(iter) && hist_sz < limit; leveldb_iter_next(iter)) {
        data = (char*) leveldb_iter_key(iter, &data_len);
        assert(data_len == sizeof(ukey));

		memcpy(&ukey, data, sizeof(ukey));
        if (memcmp(ukey.scripthash_prefix, scripthash, 8))
            break;

        data = (char*) leveldb_iter_value(iter, &data_len);
        assert(data_len == sizeof(udbt));

		memcpy(&udbt, data, sizeof(udbt));
        if (hist_sz + 2 > hist_capacity) {
            hist_capacity = MAX(hist_capacity, 1) * 2;
            (*historyp) = (HistoryItem*) realloc((*historyp), hist_capacity * sizeof(HistoryItem));
        }

        HistoryItem *hi = *historyp + hist_sz++;
        hi->height = ukey.height;
        hi->tx_index = udbt.tx_index;
        if ((ret = txdb_lookup_txhash(dbptr, hi->txhash, ukey.height, udbt.tx_index))) {
            break;
        }

        /*
            If there is an input using this utxo
            Get the height and txHash
        */
        if (txdb_get_txin(dbptr, hi->txhash, udbt.tx_pos, &in_dbt) == 0) {
            /*
                A transaction can have multiple provouts from different blocks, so
                we need to check if the output transaction is already added to the history
                to avoid duplicates.
            */
            input_found = 0;
            for (i = 0; i < hist_sz; i++) {
                hi = *historyp + i;
                input_found = in_dbt.height == hi->height && in_dbt.tx_index == hi->tx_index;
                if (input_found)
                    break;
            }

            if (!input_found) {
                hi = *historyp + hist_sz++;
                hi->height = in_dbt.height;
                hi->tx_index = in_dbt.tx_index;
                if ((ret = txdb_lookup_txhash(dbptr, hi->txhash, in_dbt.height, in_dbt.tx_index))) {
                    break;
                }
            }
        }
		
	}

    leveldb_iter_destroy(iter);

    if (ret) {
        logerrf("txdb history error %d", ret);
        return 0;
    }

	// sorts the result in blockchain order
    if (hist_sz)
		qsort(*historyp, hist_sz, sizeof(HistoryItem), &history_item_comp);

    return hist_sz;
}

int txdb_lookup_txhash(TXDB *dbptr, uint8_t *tx_hash, uint32_t height, uint16_t tx_index)
{
	if (height > dbptr->current_height)
		return -1;
	
    struct txhash_key thkey;
    thkey.height = height;
    thkey.tx_index = htobe16(tx_index);

    return db_get(&dbptr->txhashes_ptr, &thkey, sizeof(thkey), tx_hash, 32);
}

int txdb_lookup_txhashes_at_height(TXDB *dbptr, HashesVec *hashes, uint32_t height)
{
	if (height > dbptr->current_height)
		return -1;

	struct txhash_key thkey;
	thkey.height = height;
    thkey.tx_index = 0;

    leveldb_iterator_t *iter = create_iterator_at(&dbptr->txhashes_ptr, &thkey, sizeof(thkey));

    size_t data_len;
	char *data = NULL;
    for (; leveldb_iter_valid(iter); leveldb_iter_next(iter)) {
        data = (char*) leveldb_iter_key(iter, &data_len);
        assert(data_len == sizeof(thkey));

		memcpy(&thkey, data, sizeof(thkey));
        if (thkey.height != height)
            break;

        data = (char*) leveldb_iter_value(iter, &data_len);
        assert(data_len == 32);

        /*
         * the txhash order is enforced by leveldb itself thanks to the lexographical comparator
         * since the indexes are stored in big-endian byte order the hashes are always sorted
         */
        hashes_vec_add(hashes, (uint8_t*) data);
    }

    leveldb_iter_destroy(iter);
	
	// not found tx at index 0 this means that this height does not exists! NOTE this should never happen!
    if (hashes->size == 0)
		return -1;

    return 0;
}

int txdb_store_block_header(TXDB *dbptr, const uint8_t *data, uint32_t height)
{
    return db_put(&dbptr->headers_ptr, &height, sizeof(height), (uint8_t*) data, BLOCK_HEADER_SIZE);
}

int txdb_get_block_header(TXDB *dbptr, uint8_t *header, uint32_t height)
{
	if (height > dbptr->current_height)
		return -1;
		
    return db_get(&dbptr->headers_ptr, &height, sizeof(height), header, BLOCK_HEADER_SIZE);
}

int txdb_store_txs(TXDB *dbptr, BtcTx *txs, size_t txs_sz, uint32_t height)
{
    int ret = 0;
    size_t itx, i;
    
    struct utxo_key ukey;
    struct utxo_dbt udbt;
    
    struct txin_key in_key;
    struct txin_dbt in_dbt;
    for (itx = 0; itx < txs_sz; itx++) {
        assert(itx < USHRT_MAX);

        if (itx > 0) {
            /* We do not store coinbase tx inputs because a coinbase tx has dummy inputs */
            for (i = 0; i < txs[itx].tx_in_count; i++) {
                assert(txs[itx].tx_in[i].prev_out_index < USHRT_MAX);
                
                memcpy(in_key.txid_prefix, txs[itx].tx_in[i].prev_out_hash, sizeof(in_key.txid_prefix));
                in_key.prev_out_index = (uint16_t) txs[itx].tx_in[i].prev_out_index;

                memset(&in_dbt, 0, sizeof(struct txin_dbt));
                in_dbt.height = height;
                in_dbt.tx_index = (uint16_t) itx;

                if ((ret = db_put(&dbptr->txins_ptr, &in_key, sizeof(in_key), &in_dbt, sizeof(in_dbt)))) {
                    logerrf("txdb: error storing txins");
                    return ret;
                }
            }
        }
    }
    
    for (itx = 0; itx < txs_sz; itx++) {
		for (i = 0; i < txs[itx].tx_out_count; i++) {
            if (IS_SCRIPT_OP_RETURN(txs[itx].tx_out[i].pk_script, txs[itx].tx_out[i].pk_script_len))
                continue;

            assert(i < USHRT_MAX);
            
            memcpy(ukey.scripthash_prefix, txs[itx].tx_out[i].pk_script_hash, sizeof(ukey.scripthash_prefix));
            ukey.height = 0;
            
            // store the zero scripthash key, this is used as a seek start point, empty value
            if ((ret = db_put(&dbptr->txouts_ptr, &ukey, sizeof(ukey), "", 0))) {
                logerrf("txdb: error storing txouts");
                return ret;
            }

            ukey.height = height;

            memset(&udbt, 0, sizeof(struct utxo_dbt));
            udbt.value = txs[itx].tx_out[i].value;
            udbt.tx_pos = (uint16_t) i;
            udbt.tx_index = (uint16_t) itx;

            if (txs[itx].tx_out[i].pk_script_len > 0) {
                if ((ret = db_put(&dbptr->txouts_ptr, &ukey, sizeof(ukey), &udbt, sizeof(udbt)))) {
                    logerrf("txdb: error storing txouts");
                    return ret;
                }
            }
        }
	}

    struct txhash_key thkey;
    thkey.height = height;
    for (itx = 0; itx < txs_sz; itx++) {
        // tx index is stored in big endian byte order this enforces key ordering
        thkey.tx_index = htobe16((uint16_t) itx);
        if ((ret = db_put(&dbptr->txhashes_ptr, &thkey, sizeof(thkey), txs[itx].txid, 32))) {
			return ret;
		}
	}
	
    return ret;
}

struct write_thread_args {
	struct dbi *dbi;
	leveldb_writebatch_t *batch;
	int error;
};

// NOTE if write gives an error the writebatch destroy is not handled by this function!
static void *db_batch_put(void *ud)
{
	struct write_thread_args *args = (struct write_thread_args*) ud;
    char *err = NULL;
    leveldb_write(args->dbi->db, args->dbi->wopts, args->batch, &err);
    if (err) {
        logdebugf("txdb: bulk store error %s", err);
        leveldb_free(err);
		args->error = -1;
        return NULL;
    }

	args->error = 0;
    return NULL;
}

int txdb_bulk_store_txs(TXDB *dbptr, BtcTx *txs, size_t txs_sz, uint32_t height)
{
    size_t itx, i;

    struct utxo_key ukey;
    struct utxo_dbt udbt;

    struct txin_key in_key;
    struct txin_dbt in_dbt;

    leveldb_writebatch_t* utxo_batch = leveldb_writebatch_create();
    leveldb_writebatch_t* txin_batch = leveldb_writebatch_create();
    leveldb_writebatch_t* hash_batch = leveldb_writebatch_create();
    for (itx = 0; itx < txs_sz; itx++) {
        assert(itx < USHRT_MAX);

        if (itx > 0) {
            /* We do not store coinbase tx inputs because a coinbase tx has dummy inputs */
            for (i = 0; i < txs[itx].tx_in_count; i++) {
                assert(txs[itx].tx_in[i].prev_out_index < USHRT_MAX);

                memcpy(in_key.txid_prefix, txs[itx].tx_in[i].prev_out_hash, sizeof(in_key.txid_prefix));
                in_key.prev_out_index = (uint16_t) txs[itx].tx_in[i].prev_out_index;

                memset(&in_dbt, 0, sizeof(struct txin_dbt));
                in_dbt.height = height;
                in_dbt.tx_index = (uint16_t) itx;

                leveldb_writebatch_put(txin_batch, (char*) &in_key, sizeof(in_key), (char*) &in_dbt, sizeof(in_dbt));
            }
        }
    }

    for (itx = 0; itx < txs_sz; itx++) {
        for (i = 0; i < txs[itx].tx_out_count; i++) {
            if (IS_SCRIPT_OP_RETURN(txs[itx].tx_out[i].pk_script, txs[itx].tx_out[i].pk_script_len))
                continue;

            assert(i < USHRT_MAX);

            memcpy(ukey.scripthash_prefix, txs[itx].tx_out[i].pk_script_hash, sizeof(ukey.scripthash_prefix));
            ukey.height = 0;

            // store the zero scripthash key, this is used as a seek start point, empty value
            leveldb_writebatch_put(utxo_batch, (char*) &ukey, sizeof(ukey), "", 0);

            ukey.height = height;

            memset(&udbt, 0, sizeof(struct utxo_dbt));
            udbt.value = txs[itx].tx_out[i].value;
            udbt.tx_pos = (uint16_t) i;
            udbt.tx_index = (uint16_t) itx;

            if (txs[itx].tx_out[i].pk_script_len > 0)
                leveldb_writebatch_put(utxo_batch, (char*) &ukey, sizeof(ukey), (char*) &udbt, sizeof(udbt));
        }
    }

    struct txhash_key thkey;
    thkey.height = height;

    for (itx = 0; itx < txs_sz; itx++) {
        // tx index is stored in big endian byte order this enforces key ordering
        thkey.tx_index = htobe16((uint16_t) itx);
        leveldb_writebatch_put(hash_batch, (char*) &thkey, sizeof(thkey), (char*) txs[itx].txid, 32);
    }

	pthread_t txin_write_thread;
	struct write_thread_args txin_wta = {.dbi=&dbptr->txins_ptr, .batch=txin_batch, .error=0};
	pthread_create(&txin_write_thread, NULL, &db_batch_put, &txin_wta);
	
	pthread_t utxo_write_thread;
	struct write_thread_args utxo_wta = {.dbi=&dbptr->txouts_ptr, .batch=utxo_batch, .error=0};
	pthread_create(&utxo_write_thread, NULL, &db_batch_put, &utxo_wta);

	pthread_t hash_write_thread;
	struct write_thread_args hash_wta = {.dbi=&dbptr->txhashes_ptr, .batch=hash_batch, .error=0};
	pthread_create(&hash_write_thread, NULL, &db_batch_put, &hash_wta);

	pthread_join(txin_write_thread, NULL);
	pthread_join(utxo_write_thread, NULL);
	pthread_join(hash_write_thread, NULL);

    leveldb_writebatch_destroy(utxo_batch);
    leveldb_writebatch_destroy(txin_batch);
    leveldb_writebatch_destroy(hash_batch);

	if (txin_wta.error) {
        logerrf("txdb: error storing txins");
		return -1;
	}

	if (utxo_wta.error) {
        logerrf("txdb: error storing txouts");
		return -1;
	}

	if (hash_wta.error) {
        logerrf("txdb: error storing txhashes");
		return -1;
	}

    return 0;
}
