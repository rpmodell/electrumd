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

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>
#include <stddef.h>

#define UNIX_MINUTE 60

#define PACKED_STRUCT struct __attribute__((__packed__))

#define JSONRPC_OK 0
#define JSONRPC_PARSE_ERROR -32700
#define JSONRPC_INVALID_REQ -32600
#define JSONRPC_NO_METHOD -32601
#define JSONRPC_INVALID_PARAMS -32602
#define JSONRPC_INTERNAL_ERROR -32603
#define JSONRPC_ELECTRUM_BAD_REQUEST 1
#define JSONRPC_ELECTRUM_DAEMON_ERROR 2
#define JSONRPC_ELECTRUM_UNAVAIL_INDEX 32603

#define FCMP_8(A, B) A[0] == B[0] && A[1] == B[1] && A[2] == B[2] && A[3] == B[3] && A[4] == B[4] && A[5] == B[5] && A[6] == B[6] && A[7] == B[7]

#define SHA256_DIGEST_LEN 32

const char *jsonrpc_strerror(long code);

/**
 * Allocate the memory and copy the string.
 *
 * @param str Pointer to the input string.
 *
 * @return Pointer to the cloned string.
 */
char *str_clone(const char *str);

/**
 * Computes the SHA-256 hash of data input and stores the result in the provided hash buffer.
 *
 * @param hash Pointer to a buffer where the resulting double SHA-256 hash will be stored. The buffer
 *             must be at least 32 bytes long to accommodate the SHA-256 hash output.
 * @param data Pointer to the input data buffer.
 * @param data_sz Size of the input data buffer in bytes.
 *
 * @note The function assumes that the provided hash buffer is sufficiently large to hold the hash output.
 *       No bounds checking is performed on the hash buffer.
 */
int sha256(uint8_t *hash, const void *data, size_t data_sz);

/**
 * Computes the double SHA-256 hash of data input and stores the result in the provided hash buffer.
 *
 * @param hash Pointer to a buffer where the resulting double SHA-256 hash will be stored. The buffer
 *             must be at least 32 bytes long to accommodate the SHA-256 hash output.
 * @param data Pointer to the input data buffer.
 * @param data_sz Size of the input data buffer in bytes.
 *
 * @note The function assumes that the provided hash buffer is sufficiently large to hold the hash output.
 *       No bounds checking is performed on the hash buffer.
 */
int double_sha256(uint8_t *hash, const uint8_t *data, size_t data_sz);

/**
 * Computes the double SHA-256 hash of two concatenated data inputs and stores the result in the provided hash buffer.
 *
 * This function first concatenates the two input data buffers (`data1` and `data2`). It then computes
 * the SHA-256 hash of the concatenated data, and subsequently computes the SHA-256 hash of the resulting
 * hash. The final hash is stored in the `hash` buffer.
 *
 * @param hash Pointer to a buffer where the resulting double SHA-256 hash will be stored. The buffer
 *             must be at least 32 bytes long to accommodate the SHA-256 hash output.
 * @param data1 Pointer to the first input data buffer.
 * @param data1_sz Size of the first input data buffer in bytes.
 * @param data2 Pointer to the second input data buffer.
 * @param data2_sz Size of the second input data buffer in bytes.
 *
 * @note The function assumes that the provided hash buffer is sufficiently large to hold the hash output.
 *       No bounds checking is performed on the hash buffer.
 */
int double_sha256_concat2(uint8_t *hash, const uint8_t *data1, size_t data1_sz, const uint8_t *data2, size_t data2_sz);

/**
 * Computes the double SHA-256 hash of three concatenated data inputs and stores the result in the provided hash buffer.
 *
 * This function first concatenates the two input data buffers (`data1` and `data2`). It then computes
 * the SHA-256 hash of the concatenated data, and subsequently computes the SHA-256 hash of the resulting
 * hash. The final hash is stored in the `hash` buffer.
 *
 * @param hash Pointer to a buffer where the resulting double SHA-256 hash will be stored. The buffer
 *             must be at least 32 bytes long to accommodate the SHA-256 hash output.
 * @param data1 Pointer to the first input data buffer.
 * @param data1_sz Size of the first input data buffer in bytes.
 * @param data2 Pointer to the second input data buffer.
 * @param data2_sz Size of the second input data buffer in bytes.
 * @param data3 Pointer to the second input data buffer.
 * @param data3_sz Size of the second input data buffer in bytes.
 *
 * @note The function assumes that the provided hash buffer is sufficiently large to hold the hash output.
 *       No bounds checking is performed on the hash buffer.
 */
int double_sha256_concat3(uint8_t *hash, const void *data1, size_t data1_sz, const void *data2, size_t data2_sz, const void *data3, size_t data3_sz);

uint8_t *bytesinv(uint8_t *arr, size_t sz);

/**
 * Converts a hexadecimal string to a byte array.
 *
 * This function takes a null-terminated hexadecimal string and converts it into a byte array.
 * Each pair of hexadecimal characters is converted to a single byte. The resulting byte array
 * is stored in the provided `arr` buffer.
 *
 * @param hexstr Pointer to the input hexadecimal string.
 * @param arr Pointer to a buffer where the resulting byte array will be stored.
 *            The buffer must be at least `strlen(hexstr) / 2` bytes long to accommodate
 *            the byte array.
 *
 * @return The lenght of the bytes array on success, a negative value on error.
 *
 * @note The function assumes that the provided byte array buffer is sufficiently large to hold
 *       the byte array output. No bounds checking is performed on the byte array buffer.
 *       The input hexadecimal string must have an even length.
 */
int hex_to_bytes(const char *hexstr, uint8_t *arr);

/**
 * Converts a hexadecimal string in reversed byte order to a byte array.
 *
 * This function takes a null-terminated hexadecimal string and converts it into a byte array.
 * Each pair of hexadecimal characters is converted to a single byte. The resulting byte array
 * is stored in the provided `arr` buffer.
 *
 * @param hexstr Pointer to the input hexadecimal string.
 * @param arr Pointer to a buffer where the resulting byte array will be stored.
 *            The buffer must be at least `strlen(hexstr) / 2` bytes long to accommodate
 *            the byte array.
 *
 * @return The lenght of the bytes array on success, a negative value on error.
 *
 * @note The function assumes that the provided byte array buffer is sufficiently large to hold
 *       the byte array output. No bounds checking is performed on the byte array buffer.
 *       The input hexadecimal string must have an even length.
 */
int reverse_hex_to_bytes(const char *hexstr, uint8_t *arr);

/**
 * Converts a byte array to its hexadecimal string representation.
 *
 * This function takes an array of bytes and converts each byte to its corresponding
 * two-character hexadecimal representation. The resulting hexadecimal string is
 * stored in the provided `hex` buffer. The hexadecimal string will be null-terminated.
 *
 * @param bytes Pointer to the input byte array.
 * @param bytes_sz Size of the input byte array in bytes.
 * @param hex Pointer to a buffer where the resulting hexadecimal string will be stored.
 *            The buffer must be at least `2 * bytes_sz + 1` bytes long to accommodate
 *            the hex string and the null terminator.
 *
 * @return 0 on success, a negative value on error.
 *
 * @note The function assumes that the provided hex buffer is sufficiently large to hold
 *       the hex string output. No bounds checking is performed on the hex buffer.
 */
int bytes_to_hex(const uint8_t *bytes, size_t bytes_sz, char *hex);

/**
 * Converts a byte array to its hexadecimal string representation in reversed byteorder.
 *
 * This function takes an array of bytes and converts each byte to its corresponding
 * two-character hexadecimal representation. The resulting hexadecimal string is
 * stored in the provided `hex` buffer. The hexadecimal string will be null-terminated.
 *
 * @param bytes Pointer to the input byte array.
 * @param bytes_sz Size of the input byte array in bytes.
 * @param hex Pointer to a buffer where the resulting hexadecimal string will be stored.
 *            The buffer must be at least `2 * bytes_sz + 1` bytes long to accommodate
 *            the hex string and the null terminator.
 *
 * @return 0 on success, a negative value on error.
 *
 * @note The function assumes that the provided hex buffer is sufficiently large to hold
 *       the hex string output. No bounds checking is performed on the hex buffer.
 */
size_t bytes_to_hex_reverse(const uint8_t *bytes, size_t bytes_sz, char *hex);

void print_array_hex(const char *label, uint8_t *s, int sz);
#endif
