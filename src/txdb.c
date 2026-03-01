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

#include "logging.h"

#include "txdb.h"

#define HEIGHT_ITER_START 1

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
 *   - Key: height (4 bytes) + tx_index (2 bytes)
 *   - Data: txid (32 bytes)
 *     This database maps a transaction's height and index to its transaction ID (txid).
 *     The key is a combination of the block height and the transaction index within that block, 
 *     facilitating the lookup of a txid based on its position in the blockchain.
 * 
 * txins.db
 *   - Key: prevout_hash_prefix (8 bytes)
 *   - Data: height (8 bytes) + tx_index (2 bytes) + prev_out_index (2 bytes)
 *     This database is used to lookup transaction inputs by the prefix of the previous output hash.
 *     The data includes the block height, transaction index, and the previous output index, providing
 *     details about the source of a transaction input.
 * 
 * txouts.db
 *   - Key: scripthash_prefix (8 bytes)
 *   - Data: txid_prefix (8 bytes) + value (8 bytes) + height (4 bytes) + tx_index (2 bytes) + tx_pos (2 bytes)
 *     This database maps transaction outputs by a composite key that includes the scripthash prefix.
 *     The data includes the txid prefix, value, height, transaction index, and position, allowing for efficient 
 *     lookup of specific transaction outputs based on the scripthash.
 */

// note iterate through all heights to find the key corresponding to that key
PACKED_STRUCT utxo_key {
	uint8_t scripthash_prefix[8];
	uint32_t height; //<<--- height at which utxo is located used to fetch txhash from db easily
};

PACKED_STRUCT utxo_dbt {
	uint8_t txid_prefix[8]; // the txid (easy lookup of the confirmed height (if confirmed))
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

static int db_init_open(struct dbi *db, const char *db_dir, const char *db_name, int flags)
{
	/* Create and initialize LevelDB database */
	char db_path[1024];
    sprintf(db_path, "%s/%s", db_dir, db_name);
	
	char *err = NULL;
	db->opts = leveldb_options_create();
    leveldb_options_set_create_if_missing(db->opts, 1);
    db->db = leveldb_open(db->opts, db_path, &err);

    if (err != NULL) {
		logerrf("txdb open %s fail: %s", db_name, err);
		/* reset error var */
		leveldb_free(err); 
		return -1;
    }
    
    // Add read and write options keep normal for now until we know how to configure
    db->wopts = leveldb_writeoptions_create();
    db->ropts = leveldb_readoptions_create();

    /* reset error var */
    leveldb_free(err); 
	
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

    if (db_init_open(&dbptr->headers_ptr, db_dir, HEADERS_DB_FILE_NAME, 0))
        return -1;

    if (db_init_open(&dbptr->txhashes_ptr, db_dir, TXHASHES_DB_FILE_NAME, 0))
        return -1;


    if (db_init_open(&dbptr->txins_ptr, db_dir, TXINS_DB_FILE_NAME, 0))
        return -1;
	
    if (db_init_open(&dbptr->txouts_ptr, db_dir, TXOUTS_DB_FILE_NAME, 0))
		return -1;

    strcpy(dbptr->db_dir, db_dir);
	return 0;
}

static int db_close(struct dbi *db, const char *name)
{
	char *err = NULL;
	
	leveldb_close(db->db);
    leveldb_destroy_db(db->opts, "testdb", &err);
    if (err != NULL) {
		logerrf("txdb close: %s: %s", name, err);
		leveldb_free(err);
		return -1;
    }

    leveldb_free(err);
    return 0;
}

int txdb_close(TXDB *dbptr)
{
	if (db_close(&dbptr->headers_ptr, HEADERS_DB_FILE_NAME)) 
		return -1;

	if (db_close(&dbptr->txhashes_ptr, TXHASHES_DB_FILE_NAME))
		return -1;
    
	if (db_close(&dbptr->txins_ptr, TXINS_DB_FILE_NAME))
		return -1;
    
	if (db_close(&dbptr->txouts_ptr, TXOUTS_DB_FILE_NAME))
		return -1;

	return 0;
}

size_t txdb_flush(TXDB *dbptr)
{
    FILE *status_fp = NULL;
    char db_path[1024];
    int ret = 0;
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

/*
    if ((ret = dbptr->headers_ptr->sync(dbptr->headers_ptr, 0))) {
        logerrf("txdb flush: %s: %s", HEADERS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }
    if ((ret = dbptr->txhashes_ptr->sync(dbptr->txhashes_ptr, 0))) {
        logerrf("txdb flush: %s: %s", TXHASHES_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }
    if ((ret = dbptr->txins_ptr->sync(dbptr->txins_ptr, 0))) {
        logerrf("txdb flush: %s: %s", TXINS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }
	if ((ret = dbptr->txouts_ptr->sync(dbptr->txouts_ptr, 0))) {
        logerrf("txdb flush: %s: %s", TXOUTS_DB_FILE_NAME, db_strerror(ret));
		return -1;
	}*/

	return 0;
}

// DB put / get
static int db_put(struct dbi *db, void *key, size_t key_sz, void *dp, size_t data_sz)
{
	char *err = NULL;
	leveldb_put(db->db, db->wopts, (char*) key, key_sz, (char*) dp, data_sz, &err);

    if (err != NULL) {
		logerrf("db put error %s", err);
		/* reset error var */
		leveldb_free(err); 
		return -1;
    }
    
    leveldb_free(err);
	return 0;
}

int db_get(struct dbi *db, const void *key, size_t key_sz, void *data_ptr, size_t data_sz)
{
	char *err = NULL;
	size_t read_sz = 0;
	char *read_ptr = leveldb_get(db->db, db->ropts, (char*) key, key_sz, &read_sz, &err);

	//FIXME specify errors!
    if (err != NULL) {
		logerrf("db get error %s", err);
		leveldb_free(err); 
		return -1;
    }
    
    leveldb_free(err);
    
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

/*
    We store a single txid_prefix key for all tx inputs so that can be recognized using the prev_out_index.
    This way we can save few disk space e.g. a tx that has 10 inputs weiges only 8+(10*(4+2+2)) = 88 bytes, while
    the same tx stored with an unique key (txid+prevout_index) weiges 10*(8+2+4+2) = 160

    NOTE: storing of the prev_out index can be avoided theoretically and this can save, considering the
    example above, about 10*2 = 20 bytes because the index is equal to the position of the
    duplicate in the db (if using DUPSORT option), however, for extra safety (we are talking about money afterall)
    we prefer to store prevout index in the txin_dbt instead of relying on the database. If the space becomes a
    problem this can be changed in the future.
*/

//FIXME integrate txin_key into utxo_dbt structure to avoid calling this function!
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
    
    struct utxo_key ukey;
    ukey.height = 0;
    memcpy(ukey.scripthash_prefix, scripthash, 8);
    
    struct utxo_dbt udbt;
	
    for (ukey.height = 0; ukey.height <= dbptr->current_height; ukey.height++) {
		if ((ret = db_get(&dbptr->txouts_ptr, &ukey, sizeof(ukey), &udbt, sizeof(udbt))))
			continue;

        //Is there a better way?
        if (mode & TXDB_UNSPENT) {
            if (!txdb_get_txin(dbptr, udbt.txid_prefix, udbt.tx_pos, NULL)) { // this way we save ~ 14gb of space
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

        if (txdb_lookup_txhash(dbptr, utxo->txhash, ukey.height, udbt.tx_index)) {
            break;
        }
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
    size_t i;
    size_t hist_capacity = 0;
    
    struct utxo_key ukey;
    ukey.height = 0;
    memcpy(ukey.scripthash_prefix, scripthash, 8);
    
    struct utxo_dbt udbt;
    struct txin_dbt in_dbt;

    for (ukey.height = 0; ukey.height <= dbptr->current_height; ukey.height++) {
		if ((ret = db_get(&dbptr->txouts_ptr, &ukey, sizeof(ukey), &udbt, sizeof(udbt))))
			continue;

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
        if ((ret = txdb_get_txin(dbptr, udbt.txid_prefix, udbt.tx_pos, &in_dbt)) == 0) {
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
    thkey.tx_index = tx_index;

    return db_get(&dbptr->txhashes_ptr, &thkey, sizeof(thkey), tx_hash, 32);
}

int txdb_lookup_txhashes_at_height(TXDB *dbptr, HashesVec *hashes, uint32_t height)
{
	if (height > dbptr->current_height)
		return -1;
	
    uint8_t hash[32];
    memset(hash, 0, 32);

	struct txhash_key thkey;
	thkey.height = height;

	/* FIXME for now using a simple get until we setup a proper comparator, that ensures irdering and 
	 * provides efficiency on retrieval. 
	 * PROVIDE A COMPARATOR AND USE ITERATOR!!!!
	*/
	for (thkey.tx_index = 0; thkey.tx_index < USHRT_MAX; thkey.tx_index++) {
        if (db_get(&dbptr->txhashes_ptr, &thkey, sizeof(thkey), hash, 32))
            break;

        hashes_vec_add(hashes, hash);
	}
	
	// not found tx at index 0 this means that this height does not exists! NOTE this should never happen!
	if (thkey.tx_index == 0)
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
            assert(i < USHRT_MAX);
            
            memcpy(ukey.scripthash_prefix, txs[itx].tx_out[i].pk_script_hash, sizeof(ukey.scripthash_prefix));
            ukey.height = height;
            
            logdebugf("store %lu scripthash at height %d", *((uint64_t*) ukey.scripthash_prefix), height);

            memset(&udbt, 0, sizeof(struct utxo_dbt));
            udbt.value = txs[itx].tx_out[i].value;
            udbt.tx_pos = (uint16_t) i;
            udbt.tx_index = (uint16_t) itx;
            memcpy(udbt.txid_prefix, txs[itx].txid, sizeof(udbt.txid_prefix));

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
    for (thkey.tx_index = 0; thkey.tx_index < txs_sz; thkey.tx_index++) {
		if ((ret = db_put(&dbptr->txhashes_ptr, &thkey, sizeof(thkey), txs[thkey.tx_index].txid, 32))) {
			return ret;
		}
	}
	
    return ret;
}

/*
 * references to https://github.com/berkeleydb/libdb/blob/master/examples/c/ex_bulk.c
 * Do not use bulk for now, using the UPDATES_PER_BULK_PUT makes it slower than not using it and
 * preallocating the memory and putting alltogether makes the memory usage impact very high
 * */
int txdb_bulk_store_txs(TXDB *dbptr, BtcTx *txs, size_t txs_sz, uint32_t height)
{
    //if (!txs_sz) {
        //return -1;
    //}

    //int ret = 0;
    //size_t itx, i, bulkn;

    //void *ptrk = NULL;
    //void *ptrd = NULL;
    //DBT key;
    //DBT data;
    
    //struct utxo_dbt udbt;
    //struct txin_dbt in_dbt;
    //struct txhash_key thkey;
    
    //BULK_DBT_INIT(&key, 8, UPDATES_PER_BULK_PUT);
    //BULK_DBT_INIT(&data, sizeof(in_dbt), UPDATES_PER_BULK_PUT);

	//DB_MULTIPLE_WRITE_INIT(ptrk, &key);
	//DB_MULTIPLE_WRITE_INIT(ptrd, &data);

    
    //for (itx = 0; itx < txs_sz; itx++) {
        //assert(itx < USHRT_MAX);
        //if (itx > 0) {
            ///* We do not store coinbase tx inputs because a coinbase tx has dummy inputs */
            //for (i = 0; i < txs[itx].tx_in_count; i++) {
                //assert(txs[itx].tx_in[i].prev_out_index < USHRT_MAX);

                //DB_MULTIPLE_WRITE_NEXT(ptrk, &key, txs[itx].tx_in[i].prev_out_hash, 8);

                //in_dbt.height = height;
                //in_dbt.tx_index = (uint16_t) itx;
                //in_dbt.prev_out_index = (uint16_t) txs[itx].tx_in[i].prev_out_index;
                //DB_MULTIPLE_WRITE_NEXT(ptrd, &data, &in_dbt, sizeof(in_dbt));
                
                //bulkn++;
                //if (bulkn % UPDATES_PER_BULK_PUT == 0) {
					//if ((ret = dbptr->txins_ptr->put(dbptr->txins_ptr, NULL, &key, &data, DB_MULTIPLE))) {
						//logerrf("txdb: error storing txins");
						//goto txdb_store_txs_end;
					//}
					
					//DB_MULTIPLE_WRITE_INIT(ptrk, &key);
					//DB_MULTIPLE_WRITE_INIT(ptrd, &data);
					//bulkn = 0;
				//}
            //}
        //}
    //}
    
    //if (bulkn % UPDATES_PER_BULK_PUT) {
		//if ((ret = dbptr->txins_ptr->put(dbptr->txins_ptr, NULL, &key, &data, DB_MULTIPLE))) {
			//logerrf("txdb: error storing txins");
			//goto txdb_store_txs_end;
		//}
	//}
    
    //free(key.data);
    //free(data.data);
    
    //BULK_DBT_INIT(&key, 8, UPDATES_PER_BULK_PUT);
    //BULK_DBT_INIT(&data, sizeof(udbt), UPDATES_PER_BULK_PUT);

	//DB_MULTIPLE_WRITE_INIT(ptrk, &key);
	//DB_MULTIPLE_WRITE_INIT(ptrd, &data);

    //bulkn = 0;
    //for (itx = 0; itx < txs_sz; itx++) {
		//for (i = 0; i < txs[itx].tx_out_count; i++) {
            //assert(i < USHRT_MAX);

            //udbt.value = txs[itx].tx_out[i].value;
            //udbt.height = height;
            //udbt.tx_pos = (uint16_t) i;
            //udbt.tx_index = (uint16_t) itx;
            //memcpy(udbt.txid_prefix, txs[itx].txid, sizeof(udbt.txid_prefix));

            //if (txs[itx].tx_out[i].pk_script_len > 0) {
                //DB_MULTIPLE_WRITE_NEXT(ptrk, &key, txs[itx].tx_out[i].pk_script_hash, 8);
                //DB_MULTIPLE_WRITE_NEXT(ptrd, &data, &udbt, sizeof(udbt));
                
                //bulkn++;
            //}
            
			//if (bulkn % UPDATES_PER_BULK_PUT == 0) {
				//if ((ret = dbptr->txouts_ptr->put(dbptr->txouts_ptr, NULL, &key, &data, DB_MULTIPLE))) {
					//logerrf("txdb: error storing txouts");
					//goto txdb_store_txs_end;
				//}
				
				//DB_MULTIPLE_WRITE_INIT(ptrk, &key);
				//DB_MULTIPLE_WRITE_INIT(ptrd, &data);
				//bulkn = 0;
			//}
        //}
	//}

    //if (bulkn % UPDATES_PER_BULK_PUT) {
		//if ((ret = dbptr->txouts_ptr->put(dbptr->txouts_ptr, NULL, &key, &data, DB_MULTIPLE))) {
			//logerrf("txdb: error storing txouts");
			//goto txdb_store_txs_end;
		//}
    //}
    
    
    //free(key.data);
    //free(data.data);
    
    //BULK_DBT_INIT(&key, sizeof(thkey), UPDATES_PER_BULK_PUT);
    //BULK_DBT_INIT(&data, 32, UPDATES_PER_BULK_PUT);

 	//DB_MULTIPLE_WRITE_INIT(ptrk, &key);
	//DB_MULTIPLE_WRITE_INIT(ptrd, &data);   

    //thkey.height = height;
    //bulkn = 0;
    //for (thkey.tx_index = 0; thkey.tx_index < txs_sz; thkey.tx_index++) {
		//DB_MULTIPLE_WRITE_NEXT(ptrk, &key, &thkey, sizeof(thkey));
		//DB_MULTIPLE_WRITE_NEXT(ptrd, &data, txs[thkey.tx_index].txid, 32);
		
		//bulkn++;
		//if (bulkn % UPDATES_PER_BULK_PUT == 0) {
			//if ((ret = dbptr->txhashes_ptr->put(dbptr->txhashes_ptr, NULL, &key, &data, DB_MULTIPLE))) {
				//logerrf("txdb: error storing txhashes");
				//goto txdb_store_txs_end;
			//}
			
			//DB_MULTIPLE_WRITE_INIT(ptrk, &key);
			//DB_MULTIPLE_WRITE_INIT(ptrd, &data);
			//bulkn = 0;
		//}
	//}
	
	//if (bulkn % UPDATES_PER_BULK_PUT) {
		//if ((ret = dbptr->txhashes_ptr->put(dbptr->txhashes_ptr, NULL, &key, &data, DB_MULTIPLE))) {
			//logerrf("txdb: error storing txhashes");
			//goto txdb_store_txs_end;
		//}
	//}

//txdb_store_txs_end:
    //free(key.data);
    //free(data.data);

    //return ret;
}
