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

#include "util.h"

#include "config.h"

#define DEFAULT_LOG_PATH "/var/log/electrumd.log"
#define DEFAULT_PID_FILE_PATH "/var/run/electrumd.pid"
#define DEFAULT_BITCOIND_RPC_HOST "127.0.0.1:8332"
#define DEFAULT_BITCOIND_P2P_ADDR "127.0.0.1"
#define DEFAULT_BITCOIND_P2P_PORT 8333
#define DEFAULT_ELECTRUMD_BIND "127.0.0.1"
#define DEFAULT_ELECTRUMD_BANNER "electrumd :-)"
#define DEFAULT_ELECTRUMD_PORT 50001
#define DEFAULT_CACAHE_SIZE (64*1048576)

static inline char *str_trim(char *str)
{
	int i;
	int v = 0;
    for (i = 0; i < strlen(str); i++) {
		if (str[i] != ' ' && str[i] != '\t')
			str[v++] = str[i];
    }
			
	str[v] = '\0';
	return str;
}

void configs_init(ElectrumdConfigs *configs)
{
    configs->log_file_path = NULL;
    configs->pid_file_path = NULL;
	configs->bitcoin_rpc_auth_cookie = 0;
    configs->bitcoin_rpc_host = NULL;
	configs->bitcoin_rpc_auth = NULL;
    configs->bitcoin_p2p_addr = NULL;
    configs->bitcoin_p2p_port = DEFAULT_BITCOIND_P2P_PORT;
    configs->electrumd_rpc_bind = NULL;
    configs->electrumd_rpc_port = DEFAULT_ELECTRUMD_PORT;
    configs->cache_size = DEFAULT_CACAHE_SIZE;
    configs->db_dir = NULL;
    configs->banner = NULL;
    configs->donation_address = NULL;
}

void configs_free(ElectrumdConfigs *configs)
{
    if (configs->log_file_path)
        free(configs->log_file_path);
        
    if (configs->pid_file_path)
        free(configs->pid_file_path);

    if (configs->bitcoin_p2p_addr)
        free(configs->bitcoin_p2p_addr);

    if (configs->bitcoin_rpc_auth)
        free(configs->bitcoin_rpc_auth);

    if (configs->bitcoin_rpc_host)
        free(configs->bitcoin_rpc_host);

    if (configs->electrumd_rpc_bind)
        free(configs->electrumd_rpc_bind);

    if (configs->db_dir)
        free(configs->db_dir);

    if (configs->banner)
        free(configs->banner);

    if (configs->donation_address)
        free(configs->donation_address);
}

void configs_print(ElectrumdConfigs *configs)
{
    printf("ElectrumdConfigs = {\n");
    printf("\tbitcoin_p2p=%s:%d\n", configs->bitcoin_p2p_addr, configs->bitcoin_p2p_port);
    printf("\tbitcoin_auth=[auth=%s, cookie=%d]\n", configs->bitcoin_rpc_auth, configs->bitcoin_rpc_auth_cookie);
    printf("\tbitcoin_rpc_host=%s\n", configs->bitcoin_rpc_host);
    printf("\tblocks_cache_max=%d\n", configs->cache_size);
    printf("\telectrumd_rpc_bind=%s:%d\n", configs->electrumd_rpc_bind, configs->electrumd_rpc_port);
    printf("\tdb_dir=%s\n", configs->db_dir);
    printf("\tdonation_address=%s\n", configs->donation_address);
    printf("\tbanner=%s\n", configs->banner);
    printf("}\n");
}

int configs_check(ElectrumdConfigs *configs, int opt_daemon)
{	
    if (!configs->bitcoin_rpc_auth) {
        return -1;
    }
    if (!configs->db_dir) {
        return -2;
    }

    /*
        Fill with reasonable defaults
    */
    if (opt_daemon && !configs->log_file_path)
        configs->log_file_path = str_clone(DEFAULT_LOG_PATH);
    
    if (!configs->pid_file_path)
		configs->pid_file_path = str_clone(DEFAULT_PID_FILE_PATH);
		
    if (!configs->bitcoin_rpc_host)
        configs->bitcoin_rpc_host = str_clone(DEFAULT_BITCOIND_RPC_HOST);
    
    if (!configs->bitcoin_p2p_addr)
        configs->bitcoin_p2p_addr = str_clone(DEFAULT_BITCOIND_P2P_ADDR);
    
    if (!configs->electrumd_rpc_bind)
        configs->electrumd_rpc_bind = str_clone(DEFAULT_ELECTRUMD_BIND);
    
    if (!configs->donation_address)
        configs->donation_address = str_clone("");
    
    if (!configs->banner)
        configs->banner = str_clone(DEFAULT_ELECTRUMD_BANNER);

    return 0;
}

/*
 * On success returns 0, on error returns the number of the line 
 * where error is occurred, and -1 if can't open file
 */
int configs_parse_file(ElectrumdConfigs *configs, const char *fpath)
{
	FILE *fp = fopen(fpath, "r");
	if (!fp)
		return -1;
		
	int lineno = 0;
    char line_buf[1024];
	while (fgets(line_buf, sizeof(line_buf), fp) != NULL) {
		lineno++;
		char *endl_ptr = strchr(line_buf, '\n');
		if (!endl_ptr) goto parse_fail;
		
		(*endl_ptr) = '\0';
		
		str_trim(line_buf);
		
		endl_ptr = strchr(line_buf, '#');
		if (endl_ptr)
			(*endl_ptr) = '\0';
			
		if (strlen(line_buf)) {
			char *key = strtok(line_buf, "=");
			if (!key) goto parse_fail;
				
			char *value = strtok(NULL, "=");
			if (!key) goto parse_fail;

            if (!strcmp(key, "log_file_path")) {
                configs->log_file_path = str_clone(value);
            } else if (!strcmp(key, "pid_file_path")) {
                configs->pid_file_path = str_clone(value);
            } else if (!strcmp(key, "bitcoind_rpc_cookie_file")) {
				configs->bitcoin_rpc_auth_cookie = 1;
                configs->bitcoin_rpc_auth = str_clone(value);
            } else if (!strcmp(key, "bitcoind_rpc_auth")) {
				configs->bitcoin_rpc_auth_cookie = 0;
                configs->bitcoin_rpc_auth = str_clone(value);
            } else if (!strcmp(key, "bitcoind_rpc_host")) {
                configs->bitcoin_rpc_host = str_clone(value);
            } else if (!strcmp(key, "bitcoind_p2p_addr")) {
                configs->bitcoin_p2p_addr = str_clone(value);
            } else if (!strcmp(key, "bitcoind_p2p_port")) {
                configs->bitcoin_p2p_port = atoi(value);
            } else if (!strcmp(key, "electrumd_rpc_bind")) {
                configs->electrumd_rpc_bind = str_clone(value);
            } else if (!strcmp(key, "electrumd_rpc_port")) {
                configs->electrumd_rpc_port = atoi(value);
            } else if (!strcmp(key, "cache_size")) {
                char unit;
                switch(sscanf(value, "%u%c", &configs->cache_size, &unit)) {
                case 1:
                    break;
                case 2:
                    switch (unit) {
                    case 'K':
                        configs->cache_size *= 1024; //KiloBytes
                        break;
                    case 'M':
                        configs->cache_size *= 1048576; // (1024*1024) MegaBytes
                        break;
                    case 'G':
                        configs->cache_size *= 1073741824; // (1024*1024*1024) GigaBytes
                        break;
                    default:
                        goto parse_fail;
                    }
                    break;
                default:
                    goto parse_fail;
                }
            } else if (!strcmp(key, "db_dir")) {
                configs->db_dir = str_clone(value);
            } else if (!strcmp(key, "donation_address")) {
                configs->donation_address = str_clone(value);
            } else if (!strcmp(key, "banner")) {
                configs->banner = str_clone(value);
			} else {
				 goto parse_fail;
			}
		}		
	}

	lineno = 0;
	
parse_fail:
	fclose(fp);
	return lineno;
}
