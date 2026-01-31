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
#include <zlib.h>

#include "logging.h"
#include "txdb.h"

#define BDB_DEFAULT_FLAGS (DB_TXN_WRITE_NOSYNC | DB_TXN_NOSYNC)
#define BDB_PAGE_SIZE (65536)

/*
  Need to account for proper buffer size,
  The buffer must be at least as large as the page size of
  the underlying database, aligned for unsigned integer
  access, and be a multiple of 1024 bytes in size.
 */
#define BULK_DBT_INIT(ptr, dbt, ksz) \
memset(dbt, 0, sizeof(DBT));\
(dbt)->ulen = (uint32_t) (ksz * 1024 >= BDB_PAGE_SIZE) ? ksz * 1024 : BDB_PAGE_SIZE;\
(dbt)->flags = DB_DBT_USERMEM | DB_DBT_BULK;\
(dbt)->data = malloc((dbt)->ulen);\
memset((dbt)->data, 0, (dbt)->ulen);\


#define HEADERS_DB_FILE_NAME "headers.db"
#define TXHASHES_DB_FILE_NAME "txhashes.db"
#define TXINS_DB_FILE_NAME "txins.db"
#define TXOUTS_DB_FILE_NAME "txouts.db"
#define STATUS_FILE_NAME "status" //rename to dbstat

int txhashes_comp(DB* db, const DBT *a, const DBT *b)
{
	/* We need to ensure that the txhashes are ordered by DUPSORT in the order of insertion */
	return 1;
}

int db_init_open(DB **dbp, const char *db_dir, const char *db_name, int flags, int (*dup_compare_fcn)(DB*, const DBT*, const DBT*))
{
	/* Create and initialize database object, open the database. */
	int ret = 0;
	char path_buf[1024];
	snprintf(path_buf, 1024, "%s/%s", db_dir, db_name);

    if ((ret = db_create(dbp, NULL, 0))) {
        logerrf("%s: create", db_name);
        return ret;
    }

    if ((ret = (*dbp)->set_pagesize(*dbp, BDB_PAGE_SIZE))) {
        logerrf("%s: pagesize error: ", db_name, db_strerror(ret));
        return ret;
    }

    if ((ret = (*dbp)->set_cachesize(*dbp, 0, 64*1024*1024, 1))) {
        logerrf("%s: pagesize error: ", db_name, db_strerror(ret));
        return ret;
    }
	if (dup_compare_fcn) {
		if ((ret = (*dbp)->set_dup_compare(*dbp, dup_compare_fcn))) {
			logerrf("%d, failed to set duplicate compare function: %s", db_name, db_strerror(ret));
			return ret;
		}
	}

    if (flags) {
        if ((ret = (*dbp)->set_flags(*dbp, flags)) != 0) {
            logerrf("%s: flags", db_name);
            return ret;
        }
    }

    if ((ret = (*dbp)->open(*dbp, NULL, path_buf, NULL, DB_HASH, DB_CREATE, 0664)) != 0) {
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

    if ((ret = db_init_open(&dbptr->headers_ptr, db_dir, HEADERS_DB_FILE_NAME, 0, NULL))) {
        logerrf("txdb open: %s: %s", HEADERS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }

    if ((ret = db_init_open(&dbptr->txhashes_ptr, db_dir, TXHASHES_DB_FILE_NAME, DB_DUP | DB_DUPSORT, &txhashes_comp))) {
        logerrf("txdb open: %s: %s", TXHASHES_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }


    if ((ret = db_init_open(&dbptr->txins_ptr, db_dir, TXINS_DB_FILE_NAME, DB_DUP, NULL))) {
        logerrf("txdb open: %s: %s", TXINS_DB_FILE_NAME, db_strerror(ret));
        return -1;
    }
	
    if ((ret = db_init_open(&dbptr->txouts_ptr, db_dir, TXOUTS_DB_FILE_NAME, DB_DUP, NULL))) {
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
 * We store a single txid_prefix key for all transaction inputs, allowing them 
 * to be recognized using the prev_out_index. This approach saves disk space. 
 * For example, a transaction with 10 inputs would only require 88 bytes 
 * (calculated as 8 + (10 * (4 + 2 + 2))), whereas the same transaction stored 
 * with a unique key (txid + prevout_index) would require 160 bytes (calculated as 
 * 10 * (8 + 2 + 4 + 2)).
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

	uint16_t itx = 0;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = &height;
    key.size = sizeof(height);

    DBC *dbcp; //Database cursor pointer used to iterate through the db
    if ((ret = dbptr->txhashes_ptr->cursor(dbptr->txhashes_ptr, NULL, &dbcp, 0)) != 0) {
        logerrf("failed init dbcursor %s", db_strerror(ret));
        return -1;
    }

    if ((ret = dbcp->c_get(dbcp, &key, &data, DB_SET))) {
        //logdebugf("db err txlook %s", db_strerror(ret));
        goto close_cur;
    }

    do {
		if (itx == tx_index) {
			memcpy(tx_hash, data.data, data.size);
			goto close_cur;
		}
		itx++;
    } while ((ret = dbcp->c_get(dbcp, &key, &data, DB_NEXT_DUP)) == 0);

close_cur:
    if (dbcp->close(dbcp)) {
        logerrf("dbcp_close err!");
    }

    return ret;
}

int txdb_lookup_txhashes_at_height(TXDB *dbptr, HashesVec *hashes, uint32_t height)
{
    int ret = 0;
    DBT key, data;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = &height;
    key.size = sizeof(height);

    DBC *dbcp; //Database cursor pointer used to iterate through the db
    if ((ret = dbptr->txhashes_ptr->cursor(dbptr->txhashes_ptr, NULL, &dbcp, 0)) != 0) {
        logerrf("failed init dbcursor %s", db_strerror(ret));
        return -1;
    }

    if ((ret = dbcp->c_get(dbcp, &key, &data, DB_SET))) {
        //logdebugf("db err txlook %s", db_strerror(ret));
        goto close_cur;
    }

    do {
		hashes_vec_add(hashes, (uint8_t*) data.data);
    } while ((ret = dbcp->c_get(dbcp, &key, &data, DB_NEXT_DUP)) == 0);

close_cur:
    if (dbcp->close(dbcp)) {
        logerrf("dbcp_close err!");
    }

    return ret;
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
    
    BtcTx *tx;
    
    
    for (itx = 0; itx < txs_sz; itx++) {
        assert(itx < USHRT_MAX);

		tx = &txs[itx];
        if (itx > 0) {
            /* We do not store coinbase tx inputs because a coinbase tx has dummy inputs */
            for (i = 0; i < tx->tx_in_count; i++) {
                assert(tx->tx_in[i].prev_out_index < USHRT_MAX);

                memset(&in_dbt, 0, sizeof(struct txin_dbt));
                in_dbt.height = height;
                in_dbt.tx_index = (uint16_t) itx;
                in_dbt.prev_out_index = (uint16_t) tx->tx_in[i].prev_out_index;

                if ((ret = db_put(dbptr->txins_ptr, tx->tx_in[i].prev_out_hash, 8, &in_dbt, sizeof(in_dbt)))) {
                    logerrf("txdb: error storing txins");
                    goto txdb_store_txs_end;
                }
            }
        }
    }
    
    for (itx = 0; itx < txs_sz; itx++) {
		tx = &txs[itx];
		
		for (i = 0; i < tx->tx_out_count; i++) {
            assert(i < USHRT_MAX);

            memset(&udbt, 0, sizeof(struct utxo_dbt));
            udbt.value = tx->tx_out[i].value;
            udbt.height = height;
            udbt.tx_pos = (uint16_t) i;
            udbt.tx_index = (uint16_t) itx;
            memcpy(udbt.txid_prefix, tx->txid, sizeof(udbt.txid_prefix));

            if (tx->tx_out[i].pk_script_len > 0) {
                if ((ret = db_put(dbptr->txouts_ptr, tx->tx_out[i].pk_script_hash, 8, &udbt, sizeof(udbt)))) {
                    logerrf("txdb: error storing txouts");
                    goto txdb_store_txs_end;
                }
            }
        }
	}

	/*
	 * If a block contains 2,000+ transactions, storing all txids in a single byte array can produce a 
	 * DBT with a variable data size. This size can easily exceed the maximum page size of the database. 
	 * When the data size surpasses the page size, BerkeleyDB creates overflow pages, which can dramatically 
	 * impact write performance. To avoid this, each txid is stored as a duplicate record (DB_DUP|DB_DUPSORT) 
	 * in the format [height, txid(32)] in sorted order. This sorting is enforced by the txhashes_comp sort 
	 * function to ensure that txids are stored in the same order as the transactions in the block.
	 * Upon retrieval, the txids will be fetched in the exact same order.
	 */
	for (itx = 0; itx < txs_sz; itx++) {
    	if ((ret = db_put(dbptr->txhashes_ptr, &height, sizeof(height), txs[itx].txid, 32))) {
			goto txdb_store_txs_end;
		}
    }

txdb_store_txs_end:
    return ret;
}

int txdb_bulk_store_txs(TXDB *dbptr, BtcTx *txs, size_t txs_sz, uint32_t height)
{
    if (!txs_sz) {
        return -1;
    }

    int ret = 0;
    size_t itx, i;

    size_t bulk_txins_len = 0;
    size_t bulk_txouts_len = 0;

    DB_TXN *txn = NULL;

    void *txins_key_ptr = NULL;
    void *txins_data_ptr = NULL;
    DBT txins_key;
    DBT txins_data;

    void *txouts_key_ptr = NULL;
    void *txouts_data_ptr = NULL;
    DBT txouts_key;
    DBT txouts_data;

    //compute initial size for bulk data store
    for (itx = 0; itx < txs_sz; itx++) {
        bulk_txins_len += txs[itx].tx_in_count;
        bulk_txouts_len += txs[itx].tx_out_count;
    }

    /*
     * Need to account for proper buffer size,
     * The buffer must be at least as large as the page size of
     * the underlying database, aligned for unsigned integer
     * access, and be a multiple of 1024 bytes in size.
     */
    BULK_DBT_INIT(txins_key_ptr, &txins_key, bulk_txins_len * 8);
    BULK_DBT_INIT(txins_data_ptr, &txins_data, bulk_txins_len * sizeof(struct txin_dbt));

    BULK_DBT_INIT(txouts_key_ptr, &txouts_key, bulk_txouts_len * 8);
    BULK_DBT_INIT(txouts_data_ptr, &txouts_data, bulk_txouts_len * sizeof(struct utxo_dbt));

    DB_MULTIPLE_WRITE_INIT(txins_key_ptr, &txins_key);
    DB_MULTIPLE_WRITE_INIT(txins_data_ptr, &txins_data);

    DB_MULTIPLE_WRITE_INIT(txouts_key_ptr, &txouts_key);
    DB_MULTIPLE_WRITE_INIT(txouts_data_ptr, &txouts_data);

    struct utxo_dbt udbt;
    struct txin_dbt in_dbt;
    for (itx = 0; itx < txs_sz; itx++) {
        BtcTx tx = txs[itx];

        assert(itx < USHRT_MAX);
        if (itx > 0) {
            /* We do not store coinbase tx inputs because a coinbase tx has dummy inputs */
            for (i = 0; i < tx.tx_in_count; i++) {
                assert(tx.tx_in[i].prev_out_index < USHRT_MAX);

                DB_MULTIPLE_WRITE_NEXT(txins_key_ptr, &txins_key, tx.tx_in[i].prev_out_hash, 8);

                memset(&in_dbt, 0, sizeof(struct txin_dbt));

                in_dbt.height = height;
                in_dbt.tx_index = (uint16_t) itx;
                in_dbt.prev_out_index = (uint16_t) tx.tx_in[i].prev_out_index;
                DB_MULTIPLE_WRITE_NEXT(txins_data_ptr, &txins_data, &in_dbt, sizeof(in_dbt));
            }
        }

        for (i = 0; i < tx.tx_out_count; i++) {
            assert(i < USHRT_MAX);

            memset(&udbt, 0, sizeof(struct utxo_dbt));
            udbt.value = tx.tx_out[i].value;
            udbt.height = height;
            udbt.tx_pos = (uint16_t) i;
            udbt.tx_index = (uint16_t) itx;
            memcpy(udbt.txid_prefix, tx.txid, sizeof(udbt.txid_prefix));

            if (tx.tx_out[i].pk_script_len > 0) {
                DB_MULTIPLE_WRITE_NEXT(txouts_key_ptr, &txouts_key, tx.tx_out[i].pk_script_hash, 8);
                DB_MULTIPLE_WRITE_NEXT(txouts_data_ptr, &txouts_data, &udbt, sizeof(udbt));
            }
        }
    }

    if ((ret = dbptr->txins_ptr->put(dbptr->txins_ptr, txn, &txins_key, &txins_data, DB_MULTIPLE))) {
        logerrf("txdb: error storing txins");
        goto txdb_store_txs_end;
    }
    if ((ret = dbptr->txouts_ptr->put(dbptr->txouts_ptr, txn, &txouts_key, &txouts_data, DB_MULTIPLE))) {
        logerrf("txdb: error storing txouts");
        goto txdb_store_txs_end;
    }

    for (itx = 0; itx < txs_sz; itx++) {
    	if ((ret = db_put(dbptr->txhashes_ptr, &height, sizeof(height), txs[itx].txid, 32))) {
			goto txdb_store_txs_end;
		}
    }

txdb_store_txs_end:
    free(txins_key.data);
    free(txins_data.data);
    free(txouts_key.data);
    free(txouts_data.data);

    return ret;
}
