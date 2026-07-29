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
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "shared.h"
#include "config.h"
#include "block_sync.h"
#include "txdb.h"
#include "logging.h"
#include "util.h"
#include "block_sync.h"
#include "mempool.h"
#include "electrum_rpc.h"


static void signal_handler(int signum)
{
    loginfof("electrumd: SIGINT caught: stopping");
    electrumd_running = 0;
}

static void ssl_init(void)
{
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
}

static void ssl_shutdown(void)
{
    ERR_free_strings();
    EVP_cleanup();
}

int main(int argc, char **argv)
{
    char confing_path[512];
    strcpy(confing_path, "/usr/local/etc/electrumd.conf");

    logging_init();
    logging_set_level(LOGGING_LEVEL_DEBUG);

    struct sigaction sa;
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

#ifdef __OpenBSD__
    pledge("stdio rpath wpath cpath flock fattr inet fork", NULL);
#endif
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
#ifdef __OpenBSD__
	unveil(configs.log_file_path, "rwc");
#endif
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
        logerrf("error: cannot initialize: empty bitcoind rpc password or address");
        return EXIT_FAILURE;
    }

    BitcoinNetworkInfo network_info;
    if (getnetworkinfo(&rpc_ctx, &network_info)) {
        logerrf("error: unable to comminucate with bitcoin daemon");
        return EXIT_FAILURE;
    }

    char chain[9];
    memset(chain, 0, sizeof(chain));
    if (getblockchaininfo(&rpc_ctx, chain)) {
        logerrf("error: unable to comminucate with bitcoin daemon");
        return EXIT_FAILURE;
    }

    loginfof("successfully connected to bitcoin daemon: version=%d.%s, chain=%s, protocolversion=%d, relayfee=%lf",
           network_info.version,
           network_info.subversion,
           chain,
           network_info.protocolversion,
           network_info.relayfee
           );

    //compare status with bitcoind blockhaight to check if an initial sync round is needed
    // move the code to skip genesis block in block_sync and block_sync2
    TXDB txdb;
    if (txdb_open(&txdb, configs.db_dir, configs.cache_size, -1)) {
        logerrf("txdb: error opening db");
        return EXIT_FAILURE;
    }

    /* For if daemon is set */
    if (daemon) {
        switch (fork()) {
        case -1:
            logerrf("electrumd: fork failed: %s", strerror(errno));
            return EXIT_FAILURE;
        case 0:
            if (setsid() < 0) {
                logerrf("electrumd: fork failed: %s", strerror(errno));
                return EXIT_FAILURE;
            }

            switch (fork()) {
            case -1:
                logerrf("electrumd: fork failed: %s", strerror(errno));
                return EXIT_FAILURE;
                break;
            case 0:
                break;
            default:
                return EXIT_SUCCESS;
            }
            break;
        default:
			return EXIT_SUCCESS;
        }
    }

    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) == -1)
        return EXIT_FAILURE;

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, NULL) == -1)
        return EXIT_FAILURE;

    /* Start Sync process*/
    electrumd_running = 1;


    FILE *pid_fp = fopen(configs.pid_file_path, "w");
    if (!pid_fp) {
        logerrf("electrumd: unable to write pid file: %s: %s", configs.pid_file_path, strerror(errno));
        return EXIT_FAILURE;
    }
    fprintf(pid_fp, "%d", getpid());
    fclose(pid_fp);

#ifdef __OpenBSD__
    unveil(configs.db_dir, "rwcx");
    unveil(configs.pid_file_path, "rwc");
    if (configs.electrumd_rpc_listen_ssl) {
    	unveil(configs.electrumd_rpc_ssl_cert_file, "r");
    	unveil(configs.electrumd_rpc_ssl_priv_key_file, "r");
    }

    /*
     * Restrict file system access only to bare minimum fs locations 
     * needed for electrumd to work properly
     */
    unveil(NULL, NULL);

    // Restrict further fork calls from now on
    pledge("stdio rpath wpath cpath flock fattr inet", NULL);
#endif

    BtcP2pProtoCtx p2p_ctx;
    p2p_ctx_init(&p2p_ctx, configs.bitcoin_p2p_addr, configs.bitcoin_p2p_port, chain);

    int ret = EXIT_SUCCESS;
    if (prefetch_blocks2(&rpc_ctx, &p2p_ctx, &txdb, NULL)) {
        ret = EXIT_FAILURE;
        goto shutdown;
    }

    if (!electrumd_running) {
        goto shutdown;
    }

    MempoolCache mcp;
    mempool_cache_init(&mcp);
    if (mempool_cache_update(&mcp, &rpc_ctx, NULL)) {
        ret = EXIT_FAILURE;
        goto shutdown;
    }

    SyncThreadCtx sync_thread_ctx;

    electrum_server_init(configs.electrumd_rpc_bind, configs.electrumd_rpc_port, configs.donation_address, configs.banner);

    if (sync_thread_start(&sync_thread_ctx, &rpc_ctx, &p2p_ctx, &txdb, &mcp)) {
        ret = EXIT_FAILURE;
        goto shutdown;
    }

    if (configs.electrumd_rpc_listen_ssl)
        ssl_init();

    if (electrum_server_start(&mcp, &rpc_ctx, &txdb, &sync_thread_ctx, &configs)) {
        ret = EXIT_FAILURE;
        goto shutdown;
    }

    sync_thread_stop(&sync_thread_ctx);

shutdown:
    loginfof("electrumd: exited");
    txdb_close(&txdb);

    if (configs.electrumd_rpc_listen_ssl)
        ssl_shutdown();

    remove(configs.pid_file_path);
    configs_free(&configs);

    return ret;
}
