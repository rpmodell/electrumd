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

#ifndef __HASHES_VEC_H__
#define __HASHES_VEC_H__

#include <stdint.h>
#include <sys/types.h>

typedef struct {
    ssize_t capacity;
    ssize_t size;
    uint8_t **v;
} HashesVec;

void hashes_vec_init(HashesVec *vec);

/*
    hashes_vec_reserve increases the capacity of the vector without allocating the memory for
    the new hashes
*/
void hashes_vec_reserve(HashesVec *vec, ssize_t new_sz);

/*
    hashes_vec_add allocates new memory for the new hash, copies the hash value to the allocated memory
    if hash != NULL
*/
ssize_t hashes_vec_add(HashesVec *vec, uint8_t *hash);

/*
    copies the hash value to the vector element at index, does not allocates memory,
    returns -1 if the specified index location does not exists
*/
ssize_t hashes_vec_insert(HashesVec *vec, ssize_t index, uint8_t *hash);

/*
    returns: the index of the hash in the vector, -1 if not found
*/
ssize_t hashes_vec_find(HashesVec *vec, uint8_t *hash);

int hashes_vec_remove(HashesVec *vec, ssize_t index);

void hashes_vec_free(HashesVec *vec);

#endif
