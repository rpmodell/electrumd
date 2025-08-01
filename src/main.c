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
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "shared.h"
#include "config.h"
#include "block_sync.h"
#include "txdb.h"
#include "logging.h"
#include "util.h"
#include "block_sync.h"
#include "mempool.h"
#include "electrum_rpc.h"

#define SYNC_THREAD_SECONDS (30 * CLOCKS_PER_SEC)

struct btc_sync_thread_args {
    BitcoinRpcCtx *core_rpc_ctx;
    BtcP2pProtoCtx *p2p_ctx;
    TXDB *dbptr;
    MempoolCache *mc_ptr;
    ElectrumRpcCtx *electrum_rpc_ctx;
};

static void signal_handler(int signum)
{
    loginfof("electrumd: SIGINT caught: stopping");
    electrumd_running = 0;
}

void *btc_sync_thread_func(void *o)
{
    if (!o)
        return NULL;

    struct btc_sync_thread_args *arg = (struct btc_sync_thread_args*) o;
    clock_t start = clock();

    while (electrumd_running) {
        if (((clock() - start) % SYNC_THREAD_SECONDS) == 0 || arg->electrum_rpc_ctx->status_update) {
            long prev_height, last_height;
            prev_height = last_height = arg->dbptr->current_height;

            HashesVec new_scripthashes;
            HASHES_VEC_INIT(&new_scripthashes);

            mempool_cache_update(arg->mc_ptr, arg->core_rpc_ctx, &new_scripthashes);
//            mempool_cache_update2(arg->mc_ptr, arg->core_rpc_ctx, arg->p2p_ctx, &new_scripthashes);

            if (getblockcount(arg->core_rpc_ctx, &last_height)) {
                logerrf("sync: failed to fetch new height");
                continue;
            }

            if (last_height - prev_height) {
                loginfof("sync: new_height=%ld", last_height);
//                prefetch_blocks(arg->core_rpc_ctx, arg->dbptr, &new_scripthashes);
                if (prefetch_blocks2(arg->core_rpc_ctx, arg->p2p_ctx, arg->dbptr, &new_scripthashes)) {
                    logerrf("sync: block fetch update failed");
                    continue;
                }

                while (prev_height < last_height)
                    electrum_rpc_height_notify(arg->electrum_rpc_ctx, arg->dbptr, (uint32_t) ++prev_height);
            }

            logdebugf("sync: fetch new %ld scriphashes", new_scripthashes.size);

            //notify for scripthash changes
            electrum_rpc_new_scripthashes_notify(arg->electrum_rpc_ctx, arg->dbptr, arg->mc_ptr, &new_scripthashes);

            hashes_vec_free(&new_scripthashes);
        }
    }

    return NULL;
}

int main(int argc, char **argv)
{
    char confing_path[512];
    strcpy(confing_path, "/usr/local/etc/electrumd.conf");

    logging_init();
    logging_set_level(LOGGING_LEVEL_DEBUG);

    struct sigaction sa;
    pthread_t sync_thread;
    ElectrumdConfigs configs;
    configs_init(&configs);

    struct stat pid_stat;

    char c = -1;
    int daemon = 0;
    while ((c = getopt(argc, argv, "c:n:l:dh")) != -1) {
        switch (c) {
        case 'c':
            strcpy(confing_path, optarg);
            break;
        case 'l':
            if (!strcmp(optarg, "debug")) {
                logging_set_level(LOGGING_LEVEL_DEBUG);
            } else if (!strcmp(optarg, "error")) {
                    logging_set_level(LOGGING_LEVEL_ERROR);
            } else if (!strcmp(optarg, "info")) {
                    logging_set_level(LOGGING_LEVEL_INFO);
            } else {
                    logerrf("error: invalid logging level %s", optarg);
                    return EXIT_FAILURE;
            }
            break;
        case 'd':
            daemon = 1;
            break;
        case 'h':
            printf("usage %s [-c<file> -l<level> -d -h]\n", argv[0]);
            return EXIT_SUCCESS;
        }
    }

    int err = configs_parse_file(&configs, confing_path);
    if (err) {
        logerrf("error: parsing configuration failed: %s at line %d", confing_path, err);
        return EXIT_FAILURE;
    }

    switch (configs_check(&configs, daemon)) {
    case -1:
        logerrf("error: invalid configuration detected: bitcoind_rpc_auth or bitcoind_rpc_cookie_file option is mandatory");
        configs_free(&configs);
        return EXIT_FAILURE;
    case -2:
        logerrf("error: invalid configuration detected: db_dir option is mandatory");
        configs_free(&configs);
        return EXIT_FAILURE;
    }
    
	if (!stat(configs.pid_file_path, &pid_stat)) {
        logerrf("electrumd: electrumd is already running, exiting now");
        return EXIT_FAILURE;
    }

	
    if (configs.log_file_path) {
        if (logging_set_file(configs.log_file_path)) {
            logerrf("electrumd: cannot log to file %s: %s", configs.log_file_path, strerror(errno));
            return EXIT_FAILURE;
        }
    }

    char *btc_auth = NULL;
    if (configs.bitcoin_rpc_auth_cookie) {
        FILE *cookie_fp = fopen(configs.bitcoin_rpc_auth, "r");
        if (!cookie_fp) {
            logerrf("electrumd: cannot read cookie file %s: %s", configs.bitcoin_rpc_auth, strerror(errno));
            return EXIT_FAILURE;
        }

        btc_auth = (char*) malloc(2048 * sizeof(char));
        memset(btc_auth, 0, 2048);
        fgets(btc_auth, 2048, cookie_fp);
        char *endp = strchr(btc_auth, '\n');
        if (endp)
            *endp = '\0';
    } else {
        btc_auth = configs.bitcoin_rpc_auth;
    }

    BitcoinRpcCtx rpc_ctx;
    if (bitcoin_rpc_init(&rpc_ctx, configs.bitcoin_rpc_host, btc_auth)) {
        logerrf("error: unable to comminucate with bitcoin daemon");
        return EXIT_FAILURE;
    }

    loginfof("successfully connected to bitcoin daemon: version=%d.%s, chain=%s, protocolversion=%d, relayfee=%lf",
           rpc_ctx.network_info.version,
           rpc_ctx.network_info.subversion,
           rpc_ctx.chain,
           rpc_ctx.network_info.protocolversion,
           rpc_ctx.network_info.relayfee
           );

    //compare status with bitcoind blockhaight to check if an initial sync round is needed
    // move the code to skip genesis block in block_sync and block_sync2
    TXDB txdb;
    if (txdb_open(&txdb, configs.db_dir, configs.cache_size, -1)) {
        logerrf("txdb: error opening db");
        return EXIT_FAILURE;
    }

    // fork process for daemon option here
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) == -1)
        return EXIT_FAILURE;

    /* Start Sync process*/
    electrumd_running = 1;

    /* For if daemon is set */
    if (daemon) {
        switch (fork()) {
        case -1:
            logerrf("electrumd: fork failed: %s", strerror(errno));
            return EXIT_FAILURE;
        case 0:
            break;
        default:
			return EXIT_SUCCESS;
        }
    }

    FILE *pid_fp = fopen(configs.pid_file_path, "w");
    if (!pid_fp) {
        logerrf("electrumd: unable to write pid file: %s: %s", configs.pid_file_path, strerror(errno));
        return EXIT_FAILURE;
    }
    fprintf(pid_fp, "%d", getpid());
    fclose(pid_fp);

    BtcP2pProtoCtx p2p_ctx;
    p2p_ctx_init(&p2p_ctx, configs.bitcoin_p2p_addr, configs.bitcoin_p2p_port, rpc_ctx.chain);

//    if (prefetch_blocks(&rpc_ctx, &txdb, NULL)) {
    if (prefetch_blocks2(&rpc_ctx, &p2p_ctx, &txdb, NULL)) {
        goto shutdown;
    }

    if (!electrumd_running) {
      goto shutdown;
    }

    MempoolCache mcp;
    mempool_cache_init(&mcp);
    mempool_cache_update(&mcp, &rpc_ctx, NULL);
//    mempool_cache_update2(&mcp, &rpc_ctx, &p2p_ctx, NULL);

    ElectrumRpcCtx server_ctx;
    electrum_server_init(&server_ctx, "0.0.0.0", configs.electrumd_rpc_port, configs.donation_address, configs.banner);

    struct btc_sync_thread_args sync_thread_args;
    sync_thread_args.core_rpc_ctx = &rpc_ctx;
    sync_thread_args.p2p_ctx = &p2p_ctx;
    sync_thread_args.dbptr = &txdb;
    sync_thread_args.electrum_rpc_ctx = &server_ctx;
    sync_thread_args.mc_ptr = &mcp;

    pthread_create(&sync_thread, NULL, &btc_sync_thread_func, &sync_thread_args);

    electrum_server_start(&server_ctx, &mcp, &rpc_ctx, &txdb, "0.0.0.0", configs.electrumd_rpc_port);

    pthread_join(sync_thread, NULL);

shutdown:
    loginfof("electrumd: exited");
    txdb_close(&txdb);
    configs_free(&configs);

    remove(configs.pid_file_path);
}
