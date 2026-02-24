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

#include "logging.h"

#include "txdb.h"

#define BDB_PAGE_SIZE (65536)

#define UPDATES_PER_BULK_PUT (1000)
#define BULK_DBT_INIT(dbt, sz, count) \
memset((dbt), 0, sizeof(DBT));\
(dbt)->flags = DB_DBT_USERMEM | DB_DBT_BULK;\
(dbt)->ulen = (uint32_t) sz * count * 1024;\
(dbt)->data = malloc((dbt)->ulen);\
memset((dbt)->data, 0, (dbt)->ulen);\

#define PARTITION_NUMBER (50) // never change this number!
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
 

PACKED_STRUCT utxo_dbt {
	uint8_t txid_prefix[8]; // the txid (easy lookup of the confirmed height (if confirmed))
    int64_t value; // the amount
    uint32_t height; //<<--- height at which utxo is located used to fetch txhash from db easily
    uint16_t tx_index; // the index of the tx in which holds this output is in the block
    uint16_t tx_pos; // the output position in the transaction outputs vec
};

PACKED_STRUCT txin_dbt {
    uint32_t height; //<<--- height at which vin is located used to fetch txhash from db easily
    uint16_t tx_index; // the index of the tx in which holds this input is in the block
    uint16_t prev_out_index; // the index of the vout used in input
};

PACKED_STRUCT txhash_key {
	uint32_t height; // block height in which tx is stored
	uint16_t tx_index; // index of the tx inside the block
};

uint32_t db_partition_fn(DB *db, DBT *key)
{
    /*
        The db_partition_fn is used to know where to store and retrieve the keys
        since we do not know how many entries will be placed in the database we
        redistribute the keys randomly using an hash function of the 8bit hash
        (txid for txins scripthash for txouts), this hash function is just a xor
        of the 4hi bytes of the key with the random magic number 0xfffefdfc

        The partition number will be the return value of this funcion modulo
        the number of partitions.
     */

    if (key && key->size == 8)
        return (*((uint32_t*) key->data + 4)) % 4;

    return 0;
}

int db_init_open(DB **dbp, DB_ENV *env, const char *db_name, int flags, uint32_t nparts, uint32_t (*partition_fn)(DB*, DBT*))
{
	/* Create and initialize database object, open the database. */
	int ret = 0;
    if ((ret = db_create(dbp, env, 0))) {
        logerrf("%s: create", db_name);
        return ret;
    }

    if ((ret = (*dbp)->set_pagesize(*dbp, BDB_PAGE_SIZE))) {
        logerrf("%s: pagesize error: ", db_name, db_strerror(ret));
        return ret;
    }

    if (flags) {
        if ((ret = (*dbp)->set_flags(*dbp, flags)) != 0) {
            logerrf("%s: flags", db_name);
            return ret;
        }
    }

    if (nparts) {
        if ((ret = (*dbp)->set_partition(*dbp, nparts, NULL, partition_fn)) != 0) {
            logerrf("%s: set_partition: %s", db_name, db_strerror(ret));
            return ret;
        }
    }

    if ((ret = (*dbp)->open(*dbp, NULL, db_name, NULL, DB_HASH, DB_CREATE, 0664)) != 0) {
        logerrf("%s: ->open", db_name);
        return ret;
	}
	return 0;
}

int db_put(DB *dbp, void *rawkey, size_t key_sz, void *dp, size_t data_sz)
{
	DBT key, data;
	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

    key.data = rawkey;
    key.size = key_sz;

	data.data = dp;
	data.size = data_sz;

    int ret = dbp->put(dbp, NULL, &key, &data, 0); //Allow duplicates key
    if (ret) {// also error log
        logerrf("txdb: db put fail: %s", db_strerror(ret));
		return -1;
    }
	return 0;
}

int db_get(DB *dbp, const char *rawkey, size_t key_sz, DBT *data_ptr)
{
	DBT key;
	memset(&key, 0, sizeof(DBT));

    key.data = (void*) rawkey;
	key.size = key_sz;

    return dbp->get(dbp, NULL, &key, data_ptr, 0);
}

int txdb_open(TXDB *dbptr, const char *db_dir, unsigned int cache_size, long start_height)
{
    FILE *status_fp = NULL;
    char db_path[1024];
    int ret = 0;

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

    if ((ret = db_env_create(&dbptr->env_ptr, 0)) != 0) {
        logerrf("txdb open: %s: %s", db_dir, db_strerror(ret));
        return -1;
    }
//    an experimental flag to try to increase write performance, off for now needs to be tested
//    dbptr->env_ptr->set_lk_max_objects(dbptr->env_ptr, 10000); // Increase the maximum number of locked objects

    if ((ret = dbptr->env_ptr->set_cachesize(dbptr->env_ptr, 0, 64 * 1024 * 1024, 0)) != 0) {
        return ret;
    }
    /* Open the environment with full transactional support. */
    if ((ret = dbptr->env_ptr->open(dbptr->env_ptr, db_dir,
        DB_CREATE | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_TXN | DB_INIT_MPOOL, 0644)) != 0) {
        logerrf("txdb open: %s: %s", db_dir, db_strerror(ret));
        dbptr->env_ptr->close(dbptr->env_ptr, 0);
        return -1;
    }

    if ((ret = db_init_open(&dbptr->headers_ptr, dbptr->env_ptr, HEADERS_DB_FILE_NAME, 0, 0, NULL))) {
        logerrf("txdb open: %s: %s", HEADERS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }

    if ((ret = db_init_open(&dbptr->txhashes_ptr, dbptr->env_ptr, TXHASHES_DB_FILE_NAME, 0, 0, NULL))) {
        logerrf("txdb open: %s: %s", TXHASHES_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }


    if ((ret = db_init_open(&dbptr->txins_ptr, dbptr->env_ptr, TXINS_DB_FILE_NAME, DB_DUP | DB_AUTO_COMMIT, 0, &db_partition_fn))) {
        logerrf("txdb open: %s: %s", TXINS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }
	
    if ((ret = db_init_open(&dbptr->txouts_ptr, dbptr->env_ptr, TXOUTS_DB_FILE_NAME, DB_DUP | DB_AUTO_COMMIT, 0, &db_partition_fn))) {
        logerrf("txdb open: %s: %s", TXOUTS_DB_FILE_NAME, db_strerror(ret));
		return -1;
    }

    strcpy(dbptr->db_dir, db_dir);
		
	return 0;
}

size_t txdb_close(TXDB *dbptr)
{
	int ret = 0;
    if ((ret = dbptr->headers_ptr->close(dbptr->headers_ptr, 0))) {
        logerrf("txdb close: %s: %s", HEADERS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }
    if ((ret = dbptr->txhashes_ptr->close(dbptr->txhashes_ptr, 0))) {
        logerrf("txdb close: %s: %s", TXHASHES_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }
    if ((ret = dbptr->txins_ptr->close(dbptr->txins_ptr, 0))) {
        logerrf("txdb close: %s: %s", TXINS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }
	if ((ret = dbptr->txouts_ptr->close(dbptr->txouts_ptr, 0))) {
        logerrf("txdb close: %s: %s", TXOUTS_DB_FILE_NAME, db_strerror(ret));
		return -1;
	}

    if ((ret = dbptr->env_ptr->close(dbptr->env_ptr, 0))) {
        logerrf("txdb close: %s", db_strerror(ret));
        return -1;
    }
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
	}

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
int txdb_get_txin(TXDB *dbptr, uint8_t *txid_prefix, uint16_t prev_out_index, struct txin_dbt *in_dbt)
{
    int ret = 0;
    DBT key, data;
    DBC *dbcp; //Database cursor pointer used to iterate through the db
    if ((ret = dbptr->txins_ptr->cursor(dbptr->txins_ptr, NULL, &dbcp, 0)) != 0) {
        logerrf("failed init dbcursor %s", db_strerror(ret));
        return -1;
    }

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = txid_prefix;
    key.size = 8;

    if ((ret = dbcp->c_get(dbcp, &key, &data, DB_SET))) {
        //logdebugf("db err txlook %s", db_strerror(ret));
        goto close_cur;
    }

    do {
        assert(data.size == sizeof(struct txin_dbt));
        if (((struct txin_dbt*) data.data)->prev_out_index == prev_out_index) {
            if (in_dbt)
                memcpy(in_dbt, data.data, data.size);

            break;
        }

    } while ((ret = dbcp->c_get(dbcp, &key, &data, DB_NEXT_DUP)) == 0);

close_cur:
    if (dbcp->close(dbcp)) {
        logerrf("dbcp_close err!");
    }

    return ret;
}

size_t txdb_lookup_utxos(TXDB *dbptr, const uint8_t *scripthash, Utxo **utxosp, int mode)
{
	int ret = 0;
    size_t utxo_sz = 0;
    size_t utxos_capacity = 10;
    DBT key, data;
	DBC *dbcp; //Database cursor pointer used to iterate through the db
	if ((ret = dbptr->txouts_ptr->cursor(dbptr->txouts_ptr, NULL, &dbcp, 0)) != 0) {
        logerrf("failed init dbcursor %s", db_strerror(ret));
		return 0;	
	}
	
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

    key.data = (uint8_t*) scripthash;
    key.size = 8;

    if ((ret = dbcp->c_get(dbcp, &key, &data, DB_SET))) {
        logdebugf("db err txlook %s", db_strerror(ret));
        goto close_cur;
    }
	
    (*utxosp) = (Utxo*) malloc(utxos_capacity * sizeof(Utxo));
    do {
        assert(data.size == sizeof(struct utxo_dbt));

        struct utxo_dbt *udbt = (struct utxo_dbt*) data.data;

        //Is there a better way?
        if (mode & TXDB_UNSPENT) {
            if (!txdb_get_txin(dbptr, udbt->txid_prefix, udbt->tx_pos, NULL)) { // this way we save ~ 14gb of space
                continue;
            }
        }

        if (utxo_sz >= utxos_capacity) {
            utxos_capacity *= 2;
            (*utxosp) = (Utxo*) realloc((*utxosp), utxos_capacity * sizeof(Utxo));
        }

        Utxo *utxo = *utxosp + utxo_sz++;
        utxo->value = udbt->value;
        utxo->height = udbt->height;
        utxo->tx_index = udbt->tx_index;
        utxo->tx_pos = udbt->tx_pos;

        if ((ret = txdb_lookup_txhash(dbptr, utxo->txhash, udbt->height, udbt->tx_index))) {
            goto close_cur;
        }
    } while ((ret = dbcp->get(dbcp, &key, &data, DB_NEXT_DUP)) == 0);

	if (ret != DB_NOTFOUND) {
		utxo_sz = 0;
		goto close_cur;
    }

close_cur:
    logdebugf("%s:%d: db return %d: %s", __FILE__, __LINE__, ret, db_strerror(ret));
	if ((ret = dbcp->close(dbcp)) != 0) {
		logerrf("dbcp_close err!");
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
    size_t hist_capacity = 10;
    struct txin_dbt in_dbt;
    DBT key, data;
    DBC *dbcp; //Database cursor pointer used to iterate through the db
    if ((ret = dbptr->txouts_ptr->cursor(dbptr->txouts_ptr, NULL, &dbcp, 0)) != 0) {
        logerrf("failed init dbcursor %s", db_strerror(ret));
        return 0;
    }

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    memset(&in_dbt, 0, sizeof(in_dbt));

    key.data = scripthash;
    key.size = 8;

    if ((ret = dbcp->c_get(dbcp, &key, &data, DB_SET))) {
        logdebugf("db err txlook %s", db_strerror(ret));
        goto close_cur;
    }

    (*historyp) = (HistoryItem*) malloc(hist_capacity * sizeof(HistoryItem));
    do {
        assert(data.size == sizeof(struct utxo_dbt));

        struct utxo_dbt *udbt = (struct utxo_dbt*) data.data;
        if (hist_sz + 2 > hist_capacity) {
            hist_capacity *= 2;
            (*historyp) = (HistoryItem*) realloc((*historyp), hist_capacity * sizeof(HistoryItem));
        }

        HistoryItem *hi = *historyp + hist_sz++;
        hi->height = udbt->height;
        hi->tx_index = udbt->tx_index;
        if ((ret = txdb_lookup_txhash(dbptr, hi->txhash, udbt->height, udbt->tx_index))) {
            goto close_cur;
        }

        /*
            If there is an input using this utxo
            Get the height and txHash
        */
        if ((ret = txdb_get_txin(dbptr, udbt->txid_prefix, udbt->tx_pos, &in_dbt)) == 0) {
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
                    goto close_cur;
                }
            }
        }
    } while ((ret = dbcp->c_get(dbcp, &key, &data, DB_NEXT_DUP)) == 0 && hist_sz < limit);

    // sorts the result in blockchain order
    qsort(*historyp, hist_sz, sizeof(HistoryItem), &history_item_comp);

    if (ret != DB_NOTFOUND) {
        hist_sz = 0;
        goto close_cur;
    }

close_cur:
    if ((ret = dbcp->close(dbcp)) != 0) {
        logerrf("dbcp_close error: %s", db_strerror(ret));
    }

    return hist_sz;
}

int txdb_lookup_txhash(TXDB *dbptr, uint8_t *tx_hash, uint32_t height, uint16_t tx_index)
{
    int ret = 0;
    DBT key, data;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    struct txhash_key thkey;
    thkey.height = height;
    thkey.tx_index = tx_index;
    key.data = &thkey;
    key.size = sizeof(thkey);

    ret = dbptr->txhashes_ptr->get(dbptr->txhashes_ptr, NULL, &key, &data, 0);
    if (ret)
        return ret;

    memcpy(tx_hash, data.data, 32);
    return 0;
}

int txdb_lookup_txhashes_at_height(TXDB *dbptr, HashesVec *hashes, uint32_t height)
{
    int ret = 0;
    DBT key, data;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

	struct txhash_key thkey;
	thkey.height = height;
    key.data = &thkey;
    key.size = sizeof(thkey);

	for (thkey.tx_index = 0; thkey.tx_index < USHRT_MAX; thkey.tx_index++) {
		ret = dbptr->txhashes_ptr->get(dbptr->txhashes_ptr, NULL, &key, &data, 0);
        if (ret)
            break;

        hashes_vec_add(hashes, (uint8_t*) data.data);
	}

    if (ret != DB_NOTFOUND)
        return ret;

    return 0;
}

int txdb_store_block_header(TXDB *dbptr, const uint8_t *data, uint32_t height)
{
    //is not compressed for now
    return db_put(dbptr->headers_ptr, &height, sizeof(height), (uint8_t*) data, BLOCK_HEADER_SIZE);
}

int txdb_get_block_header(TXDB *dbptr, uint8_t *header, uint32_t height)
{
    DBT key, data;
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = &height;
    key.size = sizeof(height);

    int ret = dbptr->headers_ptr->get(dbptr->headers_ptr, NULL, &key, &data, 0);
    if (ret) {
        logerrf("db error: %s: %s", HEADERS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }

    //assert(data.size == BLOCK_HEADER_SZ);
    memcpy(header, data.data, data.size);

    return 0;
}

int txdb_store_txs(TXDB *dbptr, BtcTx *txs, size_t txs_sz, uint32_t height)
{
    int ret = 0;
    size_t itx, i;
    struct utxo_dbt udbt;
    struct txin_dbt in_dbt;
    for (itx = 0; itx < txs_sz; itx++) {
        assert(itx < USHRT_MAX);

        if (itx > 0) {
            /* We do not store coinbase tx inputs because a coinbase tx has dummy inputs */
            for (i = 0; i < txs[itx].tx_in_count; i++) {
                assert(txs[itx].tx_in[i].prev_out_index < USHRT_MAX);

                memset(&in_dbt, 0, sizeof(struct txin_dbt));
                in_dbt.height = height;
                in_dbt.tx_index = (uint16_t) itx;
                in_dbt.prev_out_index = (uint16_t) txs[itx].tx_in[i].prev_out_index;

                if ((ret = db_put(dbptr->txins_ptr, txs[itx].tx_in[i].prev_out_hash, 8, &in_dbt, sizeof(in_dbt)))) {
                    logerrf("txdb: error storing txins");
                    goto txdb_store_txs_end;
                }
            }
        }
    }
    
    for (itx = 0; itx < txs_sz; itx++) {
		for (i = 0; i < txs[itx].tx_out_count; i++) {
            assert(i < USHRT_MAX);

            memset(&udbt, 0, sizeof(struct utxo_dbt));
            udbt.value = txs[itx].tx_out[i].value;
            udbt.height = height;
            udbt.tx_pos = (uint16_t) i;
            udbt.tx_index = (uint16_t) itx;
            memcpy(udbt.txid_prefix, txs[itx].txid, sizeof(udbt.txid_prefix));

            if (txs[itx].tx_out[i].pk_script_len > 0) {
                if ((ret = db_put(dbptr->txouts_ptr, txs[itx].tx_out[i].pk_script_hash, 8, &udbt, sizeof(udbt)))) {
                    logerrf("txdb: error storing txouts");
                    goto txdb_store_txs_end;
                }
            }
        }
	}

    struct txhash_key thkey;
    thkey.height = height;
    for (thkey.tx_index = 0; thkey.tx_index < txs_sz; thkey.tx_index++) {
		if ((ret = db_put(dbptr->txhashes_ptr, &thkey, sizeof(thkey), txs[thkey.tx_index].txid, 32))) {
			goto txdb_store_txs_end;
		}
	}

txdb_store_txs_end:
    return ret;
}

/*
 * Need to account for proper buffer size,
 * The buffer must be at least as large as the page size of
 * the underlying database, aligned for unsigned integer
 * access, and be a multiple of 1024 bytes in size.
 */

/*
 * references to https://github.com/berkeleydb/libdb/blob/master/examples/c/ex_bulk.c
 * Do not use bulk for now, using the UPDATES_PER_BULK_PUT makes it slower than not using it and
 * preallocating the memory and putting alltogether makes the memory usage impact very high
 * */
int txdb_bulk_store_txs(TXDB *dbptr, BtcTx *txs, size_t txs_sz, uint32_t height)
{
    if (!txs_sz) {
        return -1;
    }

    int ret = 0;
    size_t itx, i, bulkn;

    void *ptrk = NULL;
    void *ptrd = NULL;
    DBT key;
    DBT data;
    
    struct utxo_dbt udbt;
    struct txin_dbt in_dbt;
    struct txhash_key thkey;
    
    BULK_DBT_INIT(&key, 8, UPDATES_PER_BULK_PUT);
    BULK_DBT_INIT(&data, sizeof(in_dbt), UPDATES_PER_BULK_PUT);

	DB_MULTIPLE_WRITE_INIT(ptrk, &key);
	DB_MULTIPLE_WRITE_INIT(ptrd, &data);

    
    for (itx = 0; itx < txs_sz; itx++) {
        assert(itx < USHRT_MAX);
        if (itx > 0) {
            /* We do not store coinbase tx inputs because a coinbase tx has dummy inputs */
            for (i = 0; i < txs[itx].tx_in_count; i++) {
                assert(txs[itx].tx_in[i].prev_out_index < USHRT_MAX);

                DB_MULTIPLE_WRITE_NEXT(ptrk, &key, txs[itx].tx_in[i].prev_out_hash, 8);

                in_dbt.height = height;
                in_dbt.tx_index = (uint16_t) itx;
                in_dbt.prev_out_index = (uint16_t) txs[itx].tx_in[i].prev_out_index;
                DB_MULTIPLE_WRITE_NEXT(ptrd, &data, &in_dbt, sizeof(in_dbt));
                
                bulkn++;
                if (bulkn % UPDATES_PER_BULK_PUT == 0) {
					if ((ret = dbptr->txins_ptr->put(dbptr->txins_ptr, NULL, &key, &data, DB_MULTIPLE))) {
						logerrf("txdb: error storing txins");
						goto txdb_store_txs_end;
					}
					
					DB_MULTIPLE_WRITE_INIT(ptrk, &key);
					DB_MULTIPLE_WRITE_INIT(ptrd, &data);
					bulkn = 0;
				}
            }
        }
    }
    
    if (bulkn % UPDATES_PER_BULK_PUT) {
		if ((ret = dbptr->txins_ptr->put(dbptr->txins_ptr, NULL, &key, &data, DB_MULTIPLE))) {
			logerrf("txdb: error storing txins");
			goto txdb_store_txs_end;
		}
	}
    
    free(key.data);
    free(data.data);
    
    BULK_DBT_INIT(&key, 8, UPDATES_PER_BULK_PUT);
    BULK_DBT_INIT(&data, sizeof(udbt), UPDATES_PER_BULK_PUT);

	DB_MULTIPLE_WRITE_INIT(ptrk, &key);
	DB_MULTIPLE_WRITE_INIT(ptrd, &data);

    bulkn = 0;
    for (itx = 0; itx < txs_sz; itx++) {
		for (i = 0; i < txs[itx].tx_out_count; i++) {
            assert(i < USHRT_MAX);

            udbt.value = txs[itx].tx_out[i].value;
            udbt.height = height;
            udbt.tx_pos = (uint16_t) i;
            udbt.tx_index = (uint16_t) itx;
            memcpy(udbt.txid_prefix, txs[itx].txid, sizeof(udbt.txid_prefix));

            if (txs[itx].tx_out[i].pk_script_len > 0) {
                DB_MULTIPLE_WRITE_NEXT(ptrk, &key, txs[itx].tx_out[i].pk_script_hash, 8);
                DB_MULTIPLE_WRITE_NEXT(ptrd, &data, &udbt, sizeof(udbt));
                
                bulkn++;
            }
            
			if (bulkn % UPDATES_PER_BULK_PUT == 0) {
				if ((ret = dbptr->txouts_ptr->put(dbptr->txouts_ptr, NULL, &key, &data, DB_MULTIPLE))) {
					logerrf("txdb: error storing txouts");
					goto txdb_store_txs_end;
				}
				
				DB_MULTIPLE_WRITE_INIT(ptrk, &key);
				DB_MULTIPLE_WRITE_INIT(ptrd, &data);
				bulkn = 0;
			}
        }
	}

    if (bulkn % UPDATES_PER_BULK_PUT) {
		if ((ret = dbptr->txouts_ptr->put(dbptr->txouts_ptr, NULL, &key, &data, DB_MULTIPLE))) {
			logerrf("txdb: error storing txouts");
			goto txdb_store_txs_end;
		}
    }
    
    
    free(key.data);
    free(data.data);
    
    BULK_DBT_INIT(&key, sizeof(thkey), UPDATES_PER_BULK_PUT);
    BULK_DBT_INIT(&data, 32, UPDATES_PER_BULK_PUT);

 	DB_MULTIPLE_WRITE_INIT(ptrk, &key);
	DB_MULTIPLE_WRITE_INIT(ptrd, &data);   

    thkey.height = height;
    bulkn = 0;
    for (thkey.tx_index = 0; thkey.tx_index < txs_sz; thkey.tx_index++) {
		DB_MULTIPLE_WRITE_NEXT(ptrk, &key, &thkey, sizeof(thkey));
		DB_MULTIPLE_WRITE_NEXT(ptrd, &data, txs[thkey.tx_index].txid, 32);
		
		bulkn++;
		if (bulkn % UPDATES_PER_BULK_PUT == 0) {
			if ((ret = dbptr->txhashes_ptr->put(dbptr->txhashes_ptr, NULL, &key, &data, DB_MULTIPLE))) {
				logerrf("txdb: error storing txhashes");
				goto txdb_store_txs_end;
			}
			
			DB_MULTIPLE_WRITE_INIT(ptrk, &key);
			DB_MULTIPLE_WRITE_INIT(ptrd, &data);
			bulkn = 0;
		}
	}
	
	if (bulkn % UPDATES_PER_BULK_PUT) {
		if ((ret = dbptr->txhashes_ptr->put(dbptr->txhashes_ptr, NULL, &key, &data, DB_MULTIPLE))) {
			logerrf("txdb: error storing txhashes");
			goto txdb_store_txs_end;
		}
	}

txdb_store_txs_end:
    free(key.data);
    free(data.data);

    return ret;
}
