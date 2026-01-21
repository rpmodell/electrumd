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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "hashes_vec.h"

void hashes_vec_init(HashesVec *vec)
{
    vec->capacity = 0;
    vec->size = 0;
    vec->v = NULL;
}

void hashes_vec_reserve(HashesVec *vec, ssize_t new_sz)
{
    if (new_sz >= vec->capacity) {
        if (vec->capacity == 0)
            vec->capacity = new_sz;

        while (vec->capacity < new_sz)
            vec->capacity *= 2;

        vec->v = (uint8_t**) realloc(vec->v, vec->capacity * sizeof(uint8_t*));
    }
}

ssize_t hashes_vec_add(HashesVec *vec, uint8_t *hash)
{
    hashes_vec_reserve(vec, vec->size + 1);

    vec->v[vec->size] = (uint8_t*) malloc(32 * sizeof(uint8_t));
    if (hash) {
        memcpy(vec->v[vec->size], hash, 32);
    }

    return vec->size++;
}

ssize_t hashes_vec_insert(HashesVec *vec, ssize_t index, uint8_t *hash)
{
    if (hash && index < vec->size) {
        memcpy(vec->v[index], hash, 32);
        return index;
    }

    return -1;
}

ssize_t hashes_vec_find(HashesVec *vec, uint8_t *hash)
{
    long i;
    for (i = 0; i < vec->size; i++) {
        if (memcmp(vec->v[i], hash, 32) == 0)
            return i;
    }

    return -1;
}

int hashes_vec_remove(HashesVec *vec, ssize_t index)
{
    if (index <= vec->size) {
        free(vec->v[index]);
        vec->v[index] = NULL;
        vec->size--;

        long i;
        for (i = index; i < vec->size; i++)
            vec->v[i] = vec->v[i+1];

        return 0;
    }

    return -1;
}

void hashes_vec_free(HashesVec *vec)
{
    while (vec->size--) {
        if (vec->v[vec->size])
            free(vec->v[vec->size]);
    }

    if (vec->v)
        free(vec->v);

    vec->v = NULL;
}
