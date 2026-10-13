/* **********************************************************
 * Copyright (c) 2026 Chroniton PBC.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* The library that client.hashtab_segment loads.  It is linked (see
 * CMakeLists.txt) with its hash table at 1MB in the image, followed by .dynsym,
 * .dynstr and the rest, while those are near the start of the file: their
 * segments' file offsets differ from their offsets in the image.  The padding
 * makes the file extend past 1MB, so that the loader's initial mapping of the
 * whole file holds the padding at the hash table's address until the loader maps
 * the hash table's segment there.
 */

#define EXPORT __attribute__((visibility("default")))

#define PADDING_SIZE (1536 * 1024)
#define PADDING_LAST (PADDING_SIZE - 1)

/* Read as a hash table header, this has 0x7f7f7f7f buckets. */
EXPORT const unsigned char hashtab_segment_padding[PADDING_SIZE] = {
    [0 ... PADDING_LAST] = 0x7f
};

EXPORT int
hashtab_segment_export(int x)
{
    return x + 1;
}
