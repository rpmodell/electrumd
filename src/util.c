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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/sha.h>

#include "util.h"

const char *jsonrpc_strerror(long code)
{
    switch (code) {
    case JSONRPC_PARSE_ERROR:
           return "Parse error";
    case JSONRPC_INVALID_REQ:
           return "Invalid request";
    case JSONRPC_NO_METHOD:
           return "Method not found";
    case JSONRPC_INVALID_PARAMS:
           return "invalid params";
    case JSONRPC_INTERNAL_ERROR:
           return "Internal error";
    case JSONRPC_ELECTRUM_BAD_REQUEST:
            return "bad request";
    case JSONRPC_ELECTRUM_DAEMON_ERROR:
            return "daemon error";
    case JSONRPC_ELECTRUM_UNAVAIL_INDEX:
        return "unavailable index";
    default:
           return "";
    }
}

char *str_clone(const char *str)
{
    if (!str)
        return NULL;
    long str_sz = strlen(str);
    char *clone = (char*) malloc((str_sz + 1) * sizeof(char));
    if (clone)
        strcpy(clone, str);

    return clone;
}

int sha256(uint8_t *hash, const void *data, size_t data_sz)
{
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, data, data_sz);
    return SHA256_Final(hash, &sha256);
}

int double_sha256(uint8_t *hash, const uint8_t *data, size_t data_sz)
{
    uint8_t tmp[32];
    sha256(tmp, data, data_sz);
    return sha256(hash, tmp, 32);
}

int double_sha256_concat2(uint8_t *hash, const uint8_t *data1, size_t data1_sz, const uint8_t *data2, size_t data2_sz)
{
    uint8_t tmp[32];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data1, data1_sz);
    SHA256_Update(&sha256, data2, data2_sz);
    SHA256_Final(tmp, &sha256);

    SHA256_Init(&sha256);
    SHA256_Update(&sha256, tmp, sizeof(tmp));
    return SHA256_Final(hash, &sha256);
}

int double_sha256_concat3(uint8_t *hash, const void *data1, size_t data1_sz, const void *data2, size_t data2_sz, const void *data3, size_t data3_sz)
{
    uint8_t tmp[32];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data1, data1_sz);
    SHA256_Update(&sha256, data2, data2_sz);
    SHA256_Update(&sha256, data3, data3_sz);
    SHA256_Final(tmp, &sha256);

    SHA256_Init(&sha256);
    SHA256_Update(&sha256, tmp, sizeof(tmp));
    return SHA256_Final(hash, &sha256);
}

uint8_t *bytesinv(uint8_t *arr, size_t sz)
{
    size_t i;
    uint8_t tmp = 0;
    for (i = 0; i < sz / 2; i++) {
        tmp = arr[sz - i - 1];
        arr[sz - i - 1] = arr[i];
        arr[i] = tmp;
    }
    return arr;
}

int hex_to_bytes(const char *hexstr, uint8_t *arr)
{
    size_t hexlen = strlen(hexstr);
    size_t stri = 0, i = 0;
    char a = 0;
    uint8_t hi = 0, lo = 0;
    while (stri < hexlen) {
        lo = 0;
        a = hexstr[stri++];
        if (a >= '0' && a <= '9')
            hi = a - '0';
        else if (a >= 'a' && a <= 'f')
            hi = a - 'a' + 10;
        else
            return -1;

        if (stri >= hexlen)
            goto hex_to_bytes_end;

        a = hexstr[stri++];
        if (a >= '0' && a <= '9')
            lo = a - '0';
        else if (a >= 'a' && a <= 'f')
            lo = a - 'a' + 10;
        else
            return -1;

hex_to_bytes_end:
        arr[i++] = ((hi << 4) & 0xf0) | (lo & 0x0f);
    }
    	
    return  i;
}

int reverse_hex_to_bytes(const char *hexstr, uint8_t *arr)
{
    int hexli = strlen(hexstr) - 1;
    int i = 0;
    char a = 0;
    uint8_t hi = 0, lo = 0;
    while (hexli > 0) {
        a = hexstr[hexli--];
        if (a >= '0' && a <= '9')
            lo = a - '0';
        else if (a >= 'a' && a <= 'f')
            lo = a - 'a' + 10;
        else
            return -1;

        a = hexstr[hexli--];
        if (a >= '0' && a <= '9')
            hi = a - '0';
        else if (a >= 'a' && a <= 'f')
            hi = a - 'a' + 10;
        else
            return -1;

        arr[i++] = ((hi << 4) & 0xf0) | (lo & 0x0f);
    }
    return  i;
}

int bytes_to_hex(const uint8_t *bytes, size_t bytes_sz, char *hex)
{
    size_t i = 0, sz = 0;
    uint8_t a = 0;
    for (i = 0; i < bytes_sz; i++) {
        a = (bytes[i] & 0xf0) >> 4;
        hex[sz++] = a > 9 ? a - 10 + 'a' : a + '0';
        a = bytes[i] & 0x0f;
        hex[sz++] = a > 9 ? a - 10 + 'a' : a + '0';
    }
    hex[sz++] = '\0';
    return sz;
}

size_t bytes_to_hex_reverse(const uint8_t *bytes, size_t bytes_sz, char *hex)
{
    size_t sz = 0;
    uint8_t a = 0;
    while (bytes_sz--) {
        a = (bytes[bytes_sz] & 0xf0) >> 4;
        hex[sz++] = a > 9 ? a - 10 + 'a' : a + '0';
        a = bytes[bytes_sz] & 0x0f;
        hex[sz++] = a > 9 ? a - 10 + 'a' : a + '0';
    }
    hex[sz++] = '\0';
    return sz;
}

void print_array_hex(const char *label, uint8_t *s, int sz)
{
    fprintf(stderr, "=========== %s =========== \n", label);
    int i, j;
    uint8_t *linep = s;
    for (j = 0; j < sz; j++) {
        fprintf(stderr, " %02hhx ", s[j]);
        if ((j+1) % 16 == 0) {
            fprintf(stderr, "\t");
            
            for (i = 0; i < 16; i++)
                fprintf(stderr, "%c", isprint(linep[i]) ? linep[i] : '.');

            fprintf(stderr, "\n");
            linep = s + j +1;
        }
    }
    fprintf(stderr, "\n");
}
