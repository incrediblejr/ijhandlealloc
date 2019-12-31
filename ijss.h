/* clang-format off */

/*
ijss : IncredibleJunior SparseSet

sparse set [1] for bookkeeping of dense<->sparse index mapping or a
building-block for a simple LIFO index/handle allocator.

This file provides both the interface and the implementation.
The sparse set is implemented as a stb-style header-file library[2]
which means that in *ONE* source file, put:

#define IJSS_IMPLEMENTATION
// if custom assert wanted (and no dependencies on assert.h)
#define IJSS_assert   custom_assert
#include "ijss.h"

Other source files should just include ijss.h

EXAMPLES/UNIT TESTS
   Usage examples+tests is at the bottom of the file in the IJSS_TEST section.
LICENSE
   See end of file for license information

References:
   [1] https://research.swtch.com/sparse
   [2] https://github.com/nothings/stb

*/

#ifndef IJSS_INCLUDED_H
#define IJSS_INCLUDED_H

#ifdef __cplusplus
   extern "C" {
#endif

#if defined(IJSS_STATIC)
   #define IJSS_API static
#else
   #define IJSS_API extern
#endif

struct ijss_pair8 {
   unsigned char sparse_index;
   unsigned char dense_index;
};

struct ijss_pair16 {
   unsigned short sparse_index;
   unsigned short dense_index;
};

struct ijss_pair32 {
   unsigned sparse_index;
   unsigned dense_index;
};

struct ijss {
   void *dense;
   void *sparse;
   unsigned dense_stride;
   unsigned sparse_stride;
   unsigned size;
   unsigned capacity;
   unsigned elementsize; /* size in bytes for _one_ dense/sparse index */
   unsigned reserved32;
};


/* dense: pointer to storage of dense indices
 * dense_stride: how many bytes to advance from index A to A+1
 *
 * sparse+sparse_index: same as dense above but for sparse indices
 *
 * elementsize:
 *    the size in bytes of _one_ sparse/dense _index_
 *    i.e. if using ijss_pairXX for bookkeeping use 'sizeof(struct ijss_pairXX)>>1'
 *
 * capacity: how many dense/sparse pairs to manage
 *
 * please refer to 'ijss_init_from_pairtype_size' or 'ijss_init_from_pairtype'
 * helper macros
 */
IJSS_API void ijss_init(struct ijss *self, void *dense, unsigned dense_stride, void *sparse, unsigned sparse_stride, unsigned elementsize, unsigned capacity);

/* initialize sparse set with a pair (ex struct ijss_pair<NBITS>) size (size of the _pair_) */
#define ijss_init_from_pairtype_size(pairtype_size, self, pairs, stride, capacity) ijss_init((self), (unsigned char*)(pairs), (stride), (unsigned char*)(pairs)+((pairtype_size)>>1), (stride), (pairtype_size)>>1, capacity)

/* initialize sparse set with a 'struct ijss_pair<NBITS>'
 *
 * pairs is the memory location where the pairs start.
 *
 * ex: pairs is inlined in another structure
 *
 *     struct Object {
 *        unsigned char payload[24];
 *        struct ijss_pair32 ss_bookkeeping;
 *     };
 *     struct Object all_objects[16];
 *     unsigned capacity = sizeof objects / sizeof *objects;
 *     struct ijss sparse_set;
 *     ijss_init_from_pairtype(struct ijss_pair32,
 *                             &sparse_set
 *                             (unsigned char*)all_objects + offsetof(struct Object, ss_bookkeeping),
 *                             sizeof(struct Object),
 *                             capacity);
 *
 * ex: pairs just used 'as-is'
 *     struct ijss_pair32 object_ss_pairs[16];
 *     unsigned capacity = sizeof object_ss_pairs / sizeof *object_ss_pairs;
 *     struct ijss sparse_set;
 *     ijss_init_from_pairtype(struct ijss_pair32, &sparse_set, object_ss_pairs, sizeof(struct ijss_pair32), capacity);
 *
 */
#define ijss_init_from_pairtype(pairtype, self, pairs, stride, capacity) ijss_init_from_pairtype_size(sizeof(pairtype), (self), (pairs), (stride), (capacity))

IJSS_API void ijss_reset(struct ijss *self);

/* reset and sets to D[x] = x for x [0, capacity) */
IJSS_API void ijss_reset_identity(struct ijss *self);

/* returns the dense index */
IJSS_API unsigned ijss_add(struct ijss *self, unsigned sparse_index);

/* returns -1 on invalid sparse index else if a move of (external) data is needed
 * stored the indices that should move in move_to_index and move_from_index respectively.
 * ex:
 *    unsigned move_from, move_to;
 *    int do_move_data = ijss_remove(self, idx) > 0;
 *    if (do_move_data)
 *       my_external_data[move_to] = my_external_data[move_from];
 *
 * stores the indices which should be moved in the (external) dense array if move is needed */
IJSS_API int ijss_remove(struct ijss *self, unsigned sparse_index, unsigned *move_to_index, unsigned *move_from_index);

IJSS_API unsigned ijss_dense_index(struct ijss *self, unsigned sparse_index);
IJSS_API unsigned ijss_sparse_index(struct ijss *self, unsigned dense_index);
IJSS_API int ijss_has(struct ijss *self, unsigned sparse_index);

#ifdef __cplusplus
   }
#endif

#endif /* IJSS_INCLUDED_H */

#if defined(IJSS_IMPLEMENTATION) && !defined(IJSS_IMPLEMENTATION_DEFINED)

#define IJSS_IMPLEMENTATION_DEFINED (1)

#ifndef IJSS_assert
   #include <assert.h>
   #define IJSS_assert assert
#endif

static unsigned ijss__load(const void * const p, unsigned len)
{
   IJSS_assert(len >= 1 && len <= 4);
   switch (len) {
      case 1: return *(unsigned char*)p;
      case 2: return *(unsigned short*)p;
      case 4: return *(unsigned*)p;
      default: return 0;
   }
}

static void ijss__store(void *dst, unsigned len, unsigned value)
{
   IJSS_assert(len >= 1 && len <= 4);
   IJSS_assert((0xffffffffu >> (8 * (4 - len))) >= value);
   switch (len) {
      case 1: *(unsigned char*)dst = (unsigned char)value; break;
      case 2: *(unsigned short*)dst = (unsigned short)value; break;
      case 4: *(unsigned*)dst = (unsigned)value; break;
      default:break;
   }
}

IJSS_API void ijss_init(struct ijss *self, void *dense, unsigned dense_stride, void *sparse, unsigned sparse_stride, unsigned elementsize, unsigned capacity)
{
   IJSS_assert(elementsize >= 1 && elementsize <= 4);
   IJSS_assert((0xffffffffu >> (8 * (4 - elementsize))) >= capacity);

   self->dense = dense;
   self->dense_stride = dense_stride;
   self->sparse = sparse;
   self->sparse_stride = sparse_stride;
   self->size = 0;
   self->capacity = capacity;
   self->elementsize = elementsize;
   self->reserved32 = 0;
   ijss_reset(self);
}

#define ijss__pointer_add(type, p, bytes) ((type)((unsigned char *)(p) + (bytes)))

#define IJSS__STORE(p, stride, elementsize, idx, value) ijss__store(ijss__pointer_add(void*, (p), (stride)*(idx)), (elementsize), (value))

/* D[idx] = value */
#define IJSS__STORE_DENSE(idx, value) IJSS__STORE(self->dense, self->dense_stride, self->elementsize, idx, value)
/* S[idx] = value */
#define IJSS__STORE_SPARSE(idx, value) IJSS__STORE(self->sparse, self->sparse_stride, self->elementsize, idx, value)

#define IJSS__LOAD(p, stride, elementsize, idx) ijss__load(ijss__pointer_add(void*, (p), (stride)*(idx)), (elementsize))
/* idx = D[idx] */
#define IJSS__LOAD_DENSE(idx) IJSS__LOAD(self->dense, self->dense_stride, self->elementsize, idx)
/* idx = S[idx] */
#define IJSS__LOAD_SPARSE(idx) IJSS__LOAD(self->sparse, self->sparse_stride, self->elementsize, idx)

IJSS_API void ijss_reset(struct ijss *self)
{
   self->size = 0;
}

IJSS_API void ijss_reset_identity(struct ijss *self)
{
   unsigned i;
   self->size = 0;
   for (i = 0; i != self->capacity; ++i)
      IJSS__STORE_DENSE(i, i);
}

IJSS_API unsigned ijss_add(struct ijss *self, unsigned sparse_index)
{
   unsigned dense_index = self->size++;
   IJSS_assert((0xffffffffu >> (8 * (4 - self->elementsize))) >= dense_index);
   IJSS_assert((0xffffffffu >> (8 * (4 - self->elementsize))) >= sparse_index);

   IJSS_assert(self->capacity > dense_index);
   IJSS_assert(self->capacity > sparse_index);

   IJSS__STORE_DENSE(dense_index, sparse_index);
   IJSS__STORE_SPARSE(sparse_index, dense_index);

   return dense_index;
}

IJSS_API int ijss_remove(struct ijss *self, unsigned sparse_index, unsigned *move_to_index, unsigned *move_from_index)
{
   if (!ijss_has(self, sparse_index))
      return -1;
   else {
      unsigned size_now = self->size-1;
      unsigned dense_index_of_removed, sparse_index_of_back;
      IJSS_assert(self->capacity > size_now);

      dense_index_of_removed = IJSS__LOAD_SPARSE(sparse_index);
      IJSS_assert(self->capacity > dense_index_of_removed);
      IJSS_assert(size_now >= dense_index_of_removed);
      sparse_index_of_back = IJSS__LOAD_DENSE(size_now);

      /* #1 is not strictly necessary, but together with 'ijss_reset_identity'
       * we can make a LIFO index/handle allocator */
      IJSS__STORE_DENSE(size_now, sparse_index); /* #1 */
      IJSS__STORE_DENSE(dense_index_of_removed, sparse_index_of_back);
      IJSS__STORE_SPARSE(sparse_index_of_back, dense_index_of_removed);

      *move_from_index = size_now;
      *move_to_index = dense_index_of_removed;
      --self->size;

      return dense_index_of_removed != size_now;
   }
}

IJSS_API int ijss_has(struct ijss *self, unsigned sparse_index)
{
   if (sparse_index >= self->capacity)
      return 0;
   else {
      unsigned dense_index = IJSS__LOAD_SPARSE(sparse_index);
      return self->size > dense_index && IJSS__LOAD_DENSE(dense_index) == sparse_index;
   }
}

IJSS_API unsigned ijss_dense_index(struct ijss *self, unsigned sparse_index)
{
   IJSS_assert(self->capacity > sparse_index);
   return IJSS__LOAD_SPARSE(sparse_index);
}

IJSS_API unsigned ijss_sparse_index(struct ijss *self, unsigned dense_index)
{
   IJSS_assert(self->capacity > dense_index);
   return IJSS__LOAD_DENSE(dense_index);
}

#if defined(IJSS_TEST) || defined(IJSS_TEST_MAIN)

typedef unsigned int ijss_uint32;

#ifdef _MSC_VER
   typedef unsigned __int64 ijss_uint64;
#else
   typedef unsigned long long ijss_uint64;
#endif

#if defined(__ppc64__) || defined(__aarch64__) || defined(_M_X64) || defined(__x86_64__) || defined(__x86_64)
   typedef ijss_uint64 ijss_uintptr;
#else
   typedef ijss_uint32 ijss_uintptr;
#endif

#ifndef offsetof
   #define ijss_test_offsetof(st, m) ((ijss_uintptr)&(((st *)0)->m))
#else
   #define ijss_test_offsetof offsetof
#endif

#define SSHA_INVALID_HANDLE (unsigned)-1
static unsigned ijss_alloc_handle(struct ijss *self, unsigned *dense)
{
   unsigned h;
   if (self->capacity == self->size)
      return SSHA_INVALID_HANDLE;

   /* the sparse indices does not move on adds or removes so we leverage this
    * fact to use them as handles */
   h = ijss_sparse_index(self, self->size);
   *dense = ijss_add(self, h);
   IJSS_assert(*dense == ijss_dense_index(self, h));
   IJSS_assert(*dense == self->size-1);
   return h;
}

static unsigned ijss_handle_valid(struct ijss *self, unsigned handle)
{
   return ijss_has(self, handle);
}

static void ijss_as_handlealloc_test_suite(void)
{
#define SSHA_NUM_OBJECTS (4)
   int r, do_move_data;
   unsigned i, h, dense, move_from, move_to;
   struct ijss_pair32 ssdata[SSHA_NUM_OBJECTS];
   unsigned handles[SSHA_NUM_OBJECTS];
   struct ijss ss, *self = &ss;

   ijss_init_from_pairtype_size(sizeof *ssdata, self, ssdata, sizeof *ssdata, SSHA_NUM_OBJECTS);
   ijss_reset_identity(self);

   for (i = 0; i != SSHA_NUM_OBJECTS; ++i) {
      h = ijss_alloc_handle(self, &dense);
      handles[i] = h;
      IJSS_assert(ijss_handle_valid(self, h));
   }

   for (i = 0; i != SSHA_NUM_OBJECTS; ++i) {
      IJSS_assert(ijss_handle_valid(self, handles[i]));
      if (i % 2)
         continue;

      r = ijss_remove(self, i, &move_to, &move_from);
      IJSS_assert(r >= 0);
      do_move_data = r > 0;
      handles[i] = SSHA_INVALID_HANDLE;
   }

   for (i = 0; i != SSHA_NUM_OBJECTS; ++i) {
      if (handles[i] == SSHA_INVALID_HANDLE)
         IJSS_assert(!ijss_handle_valid(self, handles[i]));
      else
         IJSS_assert(ijss_handle_valid(self, handles[i]));
   }

   for (i = 0; i != 2; ++i) {
      h = ijss_alloc_handle(self, &dense);
      IJSS_assert(handles[h] == SSHA_INVALID_HANDLE);
      handles[h] = h;
   }

   r = sizeof do_move_data; /* squashing [-Wunused-but-set-variable] */

#undef SSHA_NUM_OBJECTS
}

struct ijss_test_orientation {
   int a;
   unsigned sparse_owner;
};

struct ijss_test_position {
   int x, y;
   unsigned sparse_owner;
};

struct ijss_test_object {
   struct ijss_pair32 bookkeeping_position_array;
   unsigned char somepayload[20];
   struct ijss_pair8 bookkeeping_orientation_array;
};

static void ijss_keep_active_external_data_linear(void)
{
#define SSHA_NUM_OBJECTS (16)
   unsigned i;
   int loop;
   struct ijss ss_positions;
   struct ijss ss_orientations;
   struct ijss_test_object all_test_objects[SSHA_NUM_OBJECTS] = {0};
   struct ijss_test_orientation all_orientations_array[SSHA_NUM_OBJECTS];
   struct ijss_test_position all_positions_array[SSHA_NUM_OBJECTS];

   ijss_init_from_pairtype(struct ijss_pair32,
                           &ss_positions,
                           (unsigned char*)all_test_objects + ijss_test_offsetof(struct ijss_test_object, bookkeeping_position_array),
                           sizeof(struct ijss_test_object),
                           SSHA_NUM_OBJECTS);

   ijss_init_from_pairtype(struct ijss_pair8,
                           &ss_orientations,
                           (unsigned char*)all_test_objects + ijss_test_offsetof(struct ijss_test_object, bookkeeping_orientation_array),
                           sizeof(struct ijss_test_object),
                           SSHA_NUM_OBJECTS);

   for (i = 0; i != SSHA_NUM_OBJECTS; ++i) {
      IJSS_assert(!ijss_has(&ss_positions, i));
      IJSS_assert(!ijss_has(&ss_orientations, i));
   }

   for (i = 0; i != SSHA_NUM_OBJECTS; ++i) {
      unsigned dense;
      if (i & 1) {
         IJSS_assert(!ijss_has(&ss_positions, i));
         dense = ijss_add(&ss_positions, i);
         all_positions_array[dense].sparse_owner = i;
         all_positions_array[dense].x = all_positions_array[dense].y = 0;
         IJSS_assert(ijss_has(&ss_positions, i));
      } else {
         IJSS_assert(!ijss_has(&ss_orientations, i));
         dense = ijss_add(&ss_orientations, i);
         all_orientations_array[dense].sparse_owner = i;
         all_orientations_array[dense].a = 0;
         IJSS_assert(ijss_has(&ss_orientations, i));
      }
   }

   for (i = 0; i != SSHA_NUM_OBJECTS; ++i) {
      if (i & 1) {
         IJSS_assert(!ijss_has(&ss_orientations, i));
         IJSS_assert(ijss_has(&ss_positions, i));
      } else {
         IJSS_assert(!ijss_has(&ss_positions, i));
         IJSS_assert(ijss_has(&ss_orientations, i));
      }
   }

   /* now we have added the objects into either the position or orientation arrays */
   loop = 0;

   while (ss_orientations.size) {

      for (i = 0; i != ss_orientations.size; ++i) {
         struct ijss_test_orientation *current = all_orientations_array + i;
         IJSS_assert(ijss_has(&ss_orientations, current->sparse_owner));
         IJSS_assert(ijss_dense_index(&ss_orientations, current->sparse_owner) == i);
         IJSS_assert(ijss_sparse_index(&ss_orientations, i) == current->sparse_owner);
         IJSS_assert(current->a == loop);
      }

      for (i = 0; i != ss_positions.size; ++i) {
         struct ijss_test_position *current = all_positions_array + i;
         IJSS_assert(ijss_has(&ss_positions, current->sparse_owner));
         IJSS_assert(ijss_dense_index(&ss_positions, current->sparse_owner) == i);
         IJSS_assert(ijss_sparse_index(&ss_positions, i) == current->sparse_owner);
         IJSS_assert(current->x == loop);
         IJSS_assert(current->y == loop);
      }

      /* now remove the first of each array */
      {
         int r, do_move_data;
         unsigned move_from, move_to;
         unsigned sparse_indices[2];
         sparse_indices[0] = all_orientations_array[0].sparse_owner;
         sparse_indices[1] = all_positions_array[0].sparse_owner;
         for (i = 0; i != 2; ++i) {
            unsigned sparse_index = sparse_indices[i];
            if (i & 1) {
               IJSS_assert(!ijss_has(&ss_orientations, sparse_index));
               IJSS_assert(ijss_has(&ss_positions, sparse_index));
               r = ijss_remove(&ss_positions, sparse_index, &move_to, &move_from);
               IJSS_assert(r >= 0);
               do_move_data = r > 0;
               IJSS_assert(do_move_data || ss_positions.size == 0); /* otherwise the test is setup incorrectly */
               all_positions_array[move_to] = all_positions_array[move_from];
            } else {
               IJSS_assert(!ijss_has(&ss_positions, sparse_index));
               IJSS_assert(ijss_has(&ss_orientations, sparse_index));
               r = ijss_remove(&ss_orientations, sparse_index, &move_to, &move_from);
               IJSS_assert(r >= 0);
               do_move_data = r > 0;
               IJSS_assert(do_move_data || ss_orientations.size == 0); /* otherwise the test is setup incorrectly */
               all_orientations_array[move_to] = all_orientations_array[move_from];
            }
         }
      }

      /* now loop the linear data again and verify */
      for (i = 0; i != ss_orientations.size; ++i) {
         struct ijss_test_orientation *current = all_orientations_array + i;
         IJSS_assert(ijss_has(&ss_orientations, current->sparse_owner));
         IJSS_assert(ijss_dense_index(&ss_orientations, current->sparse_owner) == i);
         IJSS_assert(ijss_sparse_index(&ss_orientations, i) == current->sparse_owner);
         IJSS_assert(current->a == loop);
         current->a++;
      }

      for (i = 0; i != ss_positions.size; ++i) {
         struct ijss_test_position *current = all_positions_array + i;
         IJSS_assert(ijss_has(&ss_positions, current->sparse_owner));
         IJSS_assert(ijss_dense_index(&ss_positions, current->sparse_owner) == i);
         IJSS_assert(ijss_sparse_index(&ss_positions, i) == current->sparse_owner);
         IJSS_assert(current->x == loop);
         IJSS_assert(current->y == loop);
         current->x++;
         current->y++;
      }
      ++loop;
   }
#undef SSHA_NUM_OBJECTS

}
static void ijss_test_suite(void)
{
   ijss_as_handlealloc_test_suite();
   ijss_keep_active_external_data_linear();
}

#if defined(IJSS_TEST_MAIN)

#include <stdio.h>

int main(int args, char **argc)
{
   (void)args;
   (void)argc;
   ijss_test_suite();
   printf("ijss: all tests done.\n");
   return 0;
}
#endif /* defined(IJSS_TEST_MAIN) */
#endif /* defined(IJSS_TEST) || defined(IJSS_TEST_MAIN) */

#endif /* IJSS_IMPLEMENTATION */

/*
LICENSE
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - 3-Clause BSD License
Copyright (c) 2019-, Fredrik Engkvist
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
* Neither the name of the copyright holder nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
/* clang-format on */
