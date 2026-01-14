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

#include "merkle.h"
#include "util.h"

#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>

int merkle_branch_and_root(uint8_t *root, HashesVec *branches, HashesVec *hashesv, int index, long length)
{
    long hvsz = hashesv->size;
    if (!hvsz || index >= hvsz) {
        return -1;
    }

    if (!length)
        length = (long) ceil(log2(hvsz));

    hashes_vec_reserve(branches, length);

    long hashesv_cpy_sz = hvsz;
    HashesVec hashesv_cpy;
    HASHES_VEC_INIT(&hashesv_cpy);
    hashes_vec_reserve(&hashesv_cpy, hvsz + 1);

    long i, j, k;
    for (i = 0; i < hvsz; i++) {
        hashes_vec_add(&hashesv_cpy, hashesv->v[i]);
    }
    hashes_vec_add(&hashesv_cpy, NULL);

    for (i = 0; i < length; i++) {
        if (hashesv_cpy_sz % 2)
            hashes_vec_insert(&hashesv_cpy, hashesv_cpy_sz, hashesv_cpy.v[hashesv_cpy_sz-1]);

        assert((index^1) < hashesv_cpy.size);
        hashes_vec_add(branches, hashesv_cpy.v[index^1]);
        index >>= 1;

        hashesv_cpy_sz /= 2;
        for (j = 0, k = 0; j < hashesv_cpy_sz; j += 2) {
            double_sha256_concat2(hashesv_cpy.v[k++], hashesv_cpy.v[j], 32, hashesv_cpy.v[j+1], 32);
        }
    }

    if (root) {
        memcpy(root, hashesv_cpy.v[0], 32);
    }

    hashes_vec_free(&hashesv_cpy);

    return 0;
}
