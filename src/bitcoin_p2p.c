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

#include "bitcoin_p2p.h"
#include "logging.h"
#include "util.h"
#include "bitcoin_common.h"

#include <errno.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <endian.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>

#define P2P_HEX_DEBUG 0

#define BTC_MAX_P2P_MSG_SIZE 0x02000000;

/*
    main 	0xD9B4BEF9 	F9 BE B4 D9
    testnet/regtest 	0xDAB5BFFA 	FA BF B5 DA
    testnet3 	0x0709110B 	0B 11 09 07
    signet(default) 	0x40CF030A 	0A 03 CF 40
    namecoin 	0xFEB4BEF9 	F9 BE B4 FE 
*/
#define BTC_PROTOCOL_VERSION 70014 //70014 //70012
#define BTC_NET_MAGIC_MAIN  0xD9B4BEF9
#define BTC_NET_MAGIC_TEST  0xDAB5BFFA

#define MSG_CMD_VERSION "version"
#define MSG_CMD_VERACK "verack"
#define MSG_CMD_GETDATA "getdata"
#define MSG_CMD_NOTFOUND "notfound"

/*
    Services (bit field)
    1 	NODE_NETWORK 	This node can be asked for full blocks instead of just headers.
    2 	NODE_GETUTXO 	See BIP 0064
    4 	NODE_BLOOM 	See BIP 0111
    8 	NODE_WITNESS 	See BIP 0144
    16 	NODE_XTHIN 	Never formally proposed (as a BIP), and discontinued. Was historically sporadically seen on the network.
    64 	NODE_COMPACT_FILTERS 	See BIP 0157
    1024 	NODE_NETWORK_LIMITED 	See BIP 0159
*/
#define ELECTRUMD_SERVICES 0x00

#define MSG_BLOCK_STR "MSG_BLOCK"

PACKED_STRUCT p2p_msg_header {
    uint32_t start_string;
    char command_name[12];
    uint32_t payload_sz;
    char checksum[4]; // 	Added inprotocol version 209. First 4 bytes of SHA256(SHA256(payload)) in internal byte order. If payload is empty, as in verack and “getaddr” messages, the checksum is always 0x5df6e0e2 (SHA256(SHA256(<empty string>))).
    // char *payload....
};

// PACKED_STRUCT p2p_bitcoin_addr {
//     uint32_t timestamp;
//     uint64_t services;
//     uint8_t ipaddr[16];
//     uint16_t port;
// };

PACKED_STRUCT p2p_msg_version {
    int32_t version; // Identifies protocol version being used by the node
    uint64_t services; // bitfield of features to be enabled for this connection
    int64_t timestamp; // standard UNIX timestamp in seconds
    char addr_recv[26]; // The network address of the node receiving this message
    char addr_from[26]; // Field can be ignored. 
    uint64_t nonce; // Node random nonce, randomly generated every time a version packet is sent. 
    char user_agent; // -> aleways 0x00!!!
    int32_t start_height; // The last block received by the emitting node
    uint8_t is_relay; // Whether the remote peer should announce relayed transactions or not, see BIP 0037
};


int p2p_ctx_init(BtcP2pProtoCtx *ctx, const char *addr, int port, const char *chain)
{
    if (!addr)
        return -1;

    ctx->addr = (char*) addr;
    ctx->port = port;
    ctx->chain_magic = IS_MAINNET(chain) ? BTC_NET_MAGIC_MAIN : BTC_NET_MAGIC_TEST;

    return 0;
}

int p2p_send_message(BtcP2pProtoCtx *ctx, const char *command_name, uint8_t *payload, uint32_t payload_sz)
{
    struct p2p_msg_header header;
    header.start_string = htole32(ctx->chain_magic);
    memset(header.command_name, 0, sizeof(header.command_name));
    strcpy(header.command_name, command_name);
    //logdebugf("payload_size = %ld", payload_sz);
    header.payload_sz = htole32(payload_sz);

    unsigned char checksum[SHA256_DIGEST_LENGTH];
    double_sha256(checksum, payload, payload_sz);
    memcpy(header.checksum, checksum, sizeof(header.checksum));

//    print_array_hex("debug send -> msg_header", (uint8_t*) &header, sizeof(header));
//    print_array_hex("debug send -> msg_payload", payload, payload_sz);

    //check for errors
    if (send(ctx->sock_fd, &header, sizeof(header), 0) != sizeof(header)) {
        logdebugf("p2p error sending header: %s", strerror(errno));
        return -errno;
    }
    if (send(ctx->sock_fd, payload, payload_sz, 0) != payload_sz) {
        logdebugf("p2p error sending payload: %s", strerror(errno));
        return -errno;
    }

    return 0;
}

int p2p_recv_header(BtcP2pProtoCtx *ctx, struct p2p_msg_header *header, const char *expected_cmd_name) 
{
    if (recv(ctx->sock_fd, header, sizeof(struct p2p_msg_header), 0) != sizeof(struct p2p_msg_header)) {
        logdebugf("p2p recv header error: %s", strerror(errno));
        return -1;
	}
#if (P2P_HEX_DEBUG == 1)
     print_array_hex("recv -> header", (uint8_t*) header, sizeof(struct p2p_msg_header));
#endif
    //logdebugf("header message payload size == %ld", header->payload_sz);

    header->payload_sz = le32toh(header->payload_sz);
    return expected_cmd_name ? strcmp(header->command_name, expected_cmd_name) : 0;
}

static inline int p2p_payload_recv(BtcP2pProtoCtx *ctx, uint8_t *bufp, size_t to_recv_sz)
{
    ssize_t nrecv = 0;
    while (to_recv_sz) {
        nrecv = recv(ctx->sock_fd, bufp, to_recv_sz > 4096 ? 4096 : to_recv_sz, 0);
        switch (nrecv) {
        case -1:
            logdebugf("%s: %s", __func__, strerror(errno));
            return -errno;
        case 0:
            return -2;
        default:
            bufp += nrecv;
            to_recv_sz -= nrecv;
            if (to_recv_sz == 0)
                return  0;

            break;
        }
    }

    return -2;
}

//https://en.bitcoin.it/wiki/Protocol_documentation#Message_structure
int p2p_connect(BtcP2pProtoCtx *ctx, int32_t height)
{
    struct sockaddr_in sa;
    socklen_t sa_len = sizeof(sa);

    sa.sin_family = AF_INET;
    sa.sin_port = htons(ctx->port);
    if (inet_pton(AF_INET, ctx->addr, &sa.sin_addr) <= 0)
        return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -2;

    struct timeval optval;
    optval.tv_sec = 10;
    optval.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &optval, sizeof(optval))) {
        logdebugf("connection not established: %s", strerror(errno));
        goto p2p_connect_fail;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &optval, sizeof(optval))) {
        logdebugf("connection not established: %s", strerror(errno));
        goto p2p_connect_fail;
    }

    if (connect(fd, (struct sockaddr*) &sa, sa_len)) {
        logdebugf("connection not established!");
        goto p2p_connect_fail;
    }

    /*
        When a node creates an outgoing connection, it will immediately advertise its version. 
        The remote node will respond with its version. No further communication is possible until
        both peers have exchanged their version.
    */
    struct p2p_msg_version verpayload;
    verpayload.version = htole32(BTC_PROTOCOL_VERSION);
    verpayload.services = ELECTRUMD_SERVICES;
    verpayload.timestamp = time(NULL);
    memset(verpayload.addr_recv, 0, sizeof(verpayload.addr_recv)); // lets live like there and then if it not work adjust
    memset(verpayload.addr_from, 0, sizeof(verpayload.addr_from)); // can be ignored left 0 because its better
    verpayload.nonce = time(NULL) / 3;
    verpayload.user_agent = 0;
    verpayload.start_height = 0;
    verpayload.is_relay = 0;

    ctx->sock_fd = fd;
    if (p2p_send_message(ctx, MSG_CMD_VERSION, (uint8_t*) &verpayload, sizeof(verpayload))) {
        logdebugf("fail:p2p->connect->sendmessagew");
        goto p2p_connect_fail;
    }

    // receive verack
    struct p2p_msg_header header;
    if (p2p_recv_header(ctx, &header, MSG_CMD_VERSION)) {
        logdebugf("fail:p2p->connect->recvmessagew");
        goto p2p_connect_fail;
    }

    // since we do not send the user agent message but bitcoincore does we can not receive the
    // version struct directly but we need to allocate memory to receive the full payload
    // this should change in the future is a bad and ugly hack to get things working....
    uint8_t *version_data = (uint8_t*) malloc(header.payload_sz * sizeof(uint8_t));
    if (recv(ctx->sock_fd, version_data, header.payload_sz, 0) != header.payload_sz) {
        free(version_data);
        goto p2p_connect_fail;
    }

    memcpy(&verpayload, version_data, sizeof(verpayload));
    free(version_data);

    if ((verpayload.services & 0x01) == 0) {
        goto p2p_connect_fail;
    }
    
    if (p2p_send_message(ctx, MSG_CMD_VERACK, NULL, 0))
        goto p2p_connect_fail;

    
    if (p2p_recv_header(ctx, &header, MSG_CMD_VERACK))
        goto p2p_connect_fail;

    // version handshake successfull yay!
    
    return 0;

p2p_connect_fail:
    logerrf("p2p connect error: %s", strerror(errno));
    ctx->sock_fd = -1;
    close(fd);
    return -1;
}

// after handshake it may happen that the client is sending unuseful messages to us
// such as sendcompact or other things we need to wait until we receive the expected message type
// or in case of ping we need to reply with pong
int p2p_wait_recv_message(BtcP2pProtoCtx *ctx, struct p2p_msg_header *header, const char *expected_command_name)
{
    int ret = 0;
    while ((ret = p2p_recv_header(ctx, header, NULL)) == 0) {
        if (!strcmp(header->command_name, "ping")) {
            uint64_t nonce = 0;
            logdebugf("ping");
            if (recv(ctx->sock_fd, &nonce, sizeof(nonce), 0) != sizeof(nonce)) {
                //node is behaving wrongly
                logdebugf("ping: node is behaving wrongly");
                continue;
            }
            p2p_send_message(ctx, "pong", (uint8_t*) &nonce, sizeof(nonce));
        } else if (!strcmp(header->command_name, expected_command_name)) {
            return 0;
        } else {
            uint8_t *tmpbuf = malloc(header->payload_sz * sizeof(uint8_t));
            p2p_payload_recv(ctx, tmpbuf, header->payload_sz);
            free(tmpbuf);
        }
    }
    logdebugf("recv_first_useful_header: fail");
    return ret;
}

int p2p_ping(BtcP2pProtoCtx *ctx)
{
    int ret = 0;
    uint64_t nonce_a = time(NULL) % 32;
    uint64_t nonce_b = 0;

    struct p2p_msg_header header;

    if (( ret = p2p_send_message(ctx, "ping", (uint8_t*) &nonce_a, sizeof(nonce_a)))) {
        return ret;
    }

    if ((ret = p2p_wait_recv_message(ctx, &header, "pong")) || header.payload_sz != sizeof(nonce_a)) {
        return header.payload_sz != sizeof(nonce_a) ? -1 : ret;
    }

    if ((ret = recv(ctx->sock_fd, &nonce_b, sizeof(nonce_b), 0))) {
        return ret;
    }

    return -(nonce_a != nonce_b);
}

/*
 * getdata
 *
 * getdata is used in response to inv, to retrieve the content of a specific object, and is usually sent after receiving an inv packet, after filtering known elements. It can be used to retrieve transactions, but only if they are in the memory pool or relay set - arbitrary access to transactions in the chain is not allowed to avoid having clients start to depend on nodes having full transaction indexes (which modern nodes do not).
 *
 * Payload (maximum 50,000 entries, which is just over 1.8 megabytes):
 * Field Size 	Description 	Data type 	Comments
 * 1+ 	count 	var_int 	Number of inventory entries
 * 36x? 	inventory 	inv_vect[] 	Inventory vectors
*/
int p2p_get_data(BtcP2pProtoCtx *ctx, uint8_t **blkhashes, size_t hashes_sz, uint32_t ivt)
{
    ivt = htole32(ivt);

    size_t msg_len = MAX_VARINT_SZ + (sizeof(ivt) + 32) * hashes_sz;
    uint8_t *msgbuf = (uint8_t*) malloc(msg_len * sizeof(uint8_t));
    uint8_t *msgbufp = msgbuf;

    msgbufp += btc_write_varint(msgbuf, hashes_sz); // one or more blocks at a time -> decide later for now 1

    size_t i = 0;
    for (i = 0; i < hashes_sz; i++) {
        memcpy(msgbufp, &ivt, sizeof(ivt));
        msgbufp += sizeof(ivt);

        memcpy(msgbufp, blkhashes[i], 32);
        msgbufp += 32;
    }

    int ret = 0;
    if ((ret = p2p_send_message(ctx, MSG_CMD_GETDATA, msgbuf, (msgbufp - msgbuf)))) {
        goto getblocks_end;
    }

getblocks_end:
    free(msgbuf);

    return ret;
}

int p2p_receive_message(BtcP2pProtoCtx *ctx, uint8_t **rawpayload, size_t *payload_sz, const char *cmd)
{
    struct p2p_msg_header header;
    if (p2p_wait_recv_message(ctx, &header, cmd)) {
        return -1;
    }

    *payload_sz = 0;
    *rawpayload = (uint8_t*) malloc(header.payload_sz * sizeof(uint8_t));
    memset(*rawpayload, 0, header.payload_sz);
    if (p2p_payload_recv(ctx, *rawpayload, header.payload_sz)) {
        free(*rawpayload);
        return -1;
    }

    //verify payload chksum
    unsigned char checksum[SHA256_DIGEST_LENGTH];
    double_sha256(checksum, *rawpayload, header.payload_sz);

    assert(memcmp(checksum, header.checksum, 4) == 0);

#if (P2P_HEX_DEBUG == 1)
    print_array_hex(cmd, *rawpayload, header.payload_sz);
#endif

    *payload_sz = header.payload_sz;
    return 0;
}

/*
 * getheaders
 *
 * Return a headers packet containing the headers of blocks starting right after the last known hash in the block
 * locator object, up to hash_stop or 2000 blocks, whichever comes first. To receive the next block headers, one needs
 * to issue getheaders again with a new block locator object. Keep in mind that some clients may provide headers of blocks
 * which are invalid if the block locator object contains a hash on the invalid branch.
 * Payload:
 * Field Size 	Description 	Data type 	Comments
 * 4 	version 	uint32_t 	the protocol version
 * 1+ 	hash count 	var_int 	number of block locator hash entries
 * 32+ 	block locator hashes 	char[32] 	block locator object; newest back to genesis block (dense to start, but then sparse)
 * 32 	hash_stop 	char[32] 	hash of the last desired block header; set to zero to get as many blocks as possible (2000)
*/
int p2p_get_headers_heashes(BtcP2pProtoCtx *ctx, HashesVec *out_hashes, uint8_t **blkhash, size_t blkhash_sz, const uint8_t *stophash)
{
    size_t i;
    ssize_t ret = 0;

    uint32_t version = htole32(BTC_PROTOCOL_VERSION);

    size_t msg_len = MAX_VARINT_SZ + sizeof(version) + 32 + blkhash_sz * 32;
    uint8_t *msgbuf = (uint8_t*) malloc(msg_len * sizeof(uint8_t));
    uint8_t *msgbufp = msgbuf;

    memcpy(msgbufp, &version, sizeof(version));
    msgbufp += sizeof(version);

    msgbufp += btc_write_varint(msgbufp, blkhash_sz); // one or more blocks at a time -> decide later for now 1
    for (i = 0; i < blkhash_sz; i++) {
        memcpy(msgbufp, blkhash[i], 32);
        msgbufp += 32;
    }

    if (stophash) {
       memcpy(msgbufp, stophash, 32);
    } else {
        memset(msgbufp, 0, 32);
    }

    msgbufp += 32;
    if ((ret = p2p_send_message(ctx, "getheaders", msgbuf, msgbufp - msgbuf))) {
        goto get_headers_heashes_end;
    }

    //receive the message
    struct p2p_msg_header header;
    if ((ret = p2p_wait_recv_message(ctx, &header, "headers"))) {
        goto get_headers_heashes_end;
    }

    free(msgbuf);
    msgbuf = (uint8_t*) malloc(header.payload_sz * sizeof(uint8_t));
    msgbufp = msgbuf;

    if (p2p_payload_recv(ctx, msgbuf, header.payload_sz)) {
        goto get_headers_heashes_end;
    }

    uint64_t tx_count, headers_sz = 0;
    msgbufp += btc_read_varint(msgbufp, &headers_sz);

    hashes_vec_reserve(out_hashes, headers_sz);
    for (i = 0; i < headers_sz; i++) {
        long pos = hashes_vec_add(out_hashes, NULL);
        double_sha256(out_hashes->v[pos], msgbufp, BLOCK_HEADER_SIZE);
        msgbufp += BLOCK_HEADER_SIZE;

        // do not care about tx_count readed but not used
        msgbufp += btc_read_varint(msgbufp, &tx_count);
    }

get_headers_heashes_end:
    free(msgbuf);
    return ret;
}
