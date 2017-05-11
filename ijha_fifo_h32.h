/*
   : HandleAllocator FIFO 32bit Handles

ijha_fifo_h32 is a FIFO handle allocator with handles that have a user
configurable number of userdata/flags bits and variable number of generation bits.

This was inspired by Niklas Gray's (@niklasfrykholm) blogpost about packed arrays [1],
which is recommended reading (along with everything else he ever wrote).

Handles are 32 bits and can have user configurable number of userdata/flags bits
and variable number generation bits (how many depends on number of requested bits
for userflags and the number of bits needed to represent the requested max number
of handles).

The generation part of the handle dictates how many times a handle can be reused
before giving a false positive 'is_valid' answer.

If the number generation bits is >= 2 then all handles returned are guaranteed
to never be 0 or 0xffffffffu.

This guarantees that the 'clear to zero is initialization' pattern work and
0xffffffffu can be used for other purposes.

Handles is kept in a FIFO queue in order to limit handle reuse. Every time a handle
is reused the generation part of the handle is increased (given >0 generation bits
have been reserved). How many times a handle can be reused, before giving a
false positive 'is_valid' answer depends on how many free slots there are
(given it's a FIFO queue) and the number of generation bits.

Each individual slot (once assigned from the FIFO queue) can be reused N times,
where N depends on number of generation bits.

num generation bits == 0 -> N: 0 times
num generation bits == 1 -> N: 1 time
num generation bits >= 2 -> N: 2^(num generation bits)-3 times

The (optional) userdata/flags bits is stored in the most significant bits of
the 32 bit handle.

Handle layout / overview:
MSB                                            LSB
+ -----------------------------------------------+
| userdata(optional) | generation | sparse_index |
+ -----------------------------------------------+

Usage examples+tests is at the bottom of the file in the IJHA_FIFO_H32_TEST section.

This file provides both the interface and the implementation.

The handle allocator is implemented as a [stb-style header-file library][2]
which means that in *ONE* source file, put:

   #define IJHA_FIFO_H32_IMPLEMENTATION
   // if custom assert wanted (and no dependencies on assert.h)
    #define IJHA_FIFO_H32_assert   custom_assert
    #include "ijha_fifo_h32.h"

Other source files should just include ijha_fifo_h32.h

References:

[1] http://bitsquid.blogspot.se/2011/09/managing-decoupling-part-4-id-lookup.html
[2] https://github.com/nothings/stb

LICENSE
   See end of file for license information
*/
#ifndef IJHA_FIFO_H32_INCLUDED_H
#define IJHA_FIFO_H32_INCLUDED_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(IJHA_FIFO_H32_STATIC)
   #define IJHA_FIFO_H32_API static
#else
   #define IJHA_FIFO_H32_API extern
#endif

#define IJHA_FIFO_H32_INVALID_INDEX ((unsigned)-1)

struct ijha_fifo_h32_index {
   /* masked with capacity mask gives the _sparse_ index (into self->handles) */
   unsigned handle;
   unsigned next_index;
};

struct ijha_fifo_h32 {
   void *handles;

   unsigned handles_stride;

   unsigned num_handles; /* current num used handles */

   unsigned capacity_mask; /* num allocated is capacity_mask+1 */
   unsigned generation_mask;

   unsigned freelist_enqueue_index; /* enqueue/add/put items at the back */
   unsigned freelist_dequeue_index; /* dequeue/remove/get items from the front (FIFO order) */
};

/* storage size needed for max_num_handles (max_num_handles will be rounded
   up to next power of 2)
   NB: the size of 'struct ijha_fifo_h32' is _not_ included */
IJHA_FIFO_H32_API unsigned ijha_fifo_h32_memory_size_needed(unsigned max_num_handles);

/*
max_num_handles:
   will be rounded up to next power of two (if not already)
   max_num_handles (power of two) must be less or equal to 0x80000000u

NB:
   num _usable_ handles will be equal max_num_handles-1 ('ijha_fifo_h32_capacity' / self->capacity_mask)

num_userflag_bits:
   num bits for 'userflags'.
   userflags is stored in the most significant bits of the 32 bit handle

memory:
   the allocated memory (use 'ijha_fifo_h32_memory_size_needed' to calculate the storage size)

memory_size:
   the size of the memory block (checks is disabled if passed memory_size==0)

handles_stride:
   the stride of each handle (if the handles are interleaved in another data structure for example)
*/
IJHA_FIFO_H32_API void ijha_fifo_h32_init_stride(struct ijha_fifo_h32 *self, unsigned max_num_handles,
   unsigned num_userflag_bits, void *memory, unsigned memory_size, unsigned handles_stride);

/* calls 'ijha_fifo_h32_init_stride' with default stride (sizeof(struct ijha_fifo_h32_index)) */
IJHA_FIFO_H32_API void ijha_fifo_h32_init(struct ijha_fifo_h32 *self, unsigned max_num_handles,
   unsigned num_userflag_bits, void *memory, unsigned memory_size);

/* reset to initial state (as if no handles has been allocated) */
IJHA_FIFO_H32_API void ijha_fifo_h32_reset(struct ijha_fifo_h32 *self);

/* retrieve the memory that was used to initialize self */
IJHA_FIFO_H32_API void *ijha_fifo_h32_memory(struct ijha_fifo_h32 *self);

/* number of usable handles (this is less than 'max_num_handles' used in 'ijha_fifo_h32_init' */
IJHA_FIFO_H32_API unsigned ijha_fifo_h32_capacity(struct ijha_fifo_h32 *self);

/* check if handle is valid. returns non zero if handle is valid, 0 (zero) if invalid */
IJHA_FIFO_H32_API int ijha_fifo_h32_valid(struct ijha_fifo_h32 *self, unsigned handle);

/* returns the index of the newly allocated handle on success
   IJHA_FIFO_H32_INVALID_INDEX on failure (all handles are used i.e. full) */
IJHA_FIFO_H32_API unsigned ijha_fifo_h32_alloc(struct ijha_fifo_h32 *self, unsigned *handle_out);
IJHA_FIFO_H32_API unsigned ijha_fifo_h32_alloc_mask(struct ijha_fifo_h32 *self,
   unsigned userflags, unsigned *handle_out);

/* 'deallocate'/'return' the handle
   NB: assumes that the handle is valid (if uncertain use 'ijha_fifo_h32_valid' beforehand) */
IJHA_FIFO_H32_API void ijha_fifo_h32_dealloc(struct ijha_fifo_h32 *self, unsigned handle);

#ifdef __cplusplus
}
#endif

#endif /* IJHA_FIFO_H32_INCLUDED_H */

#if defined(IJHA_FIFO_H32_IMPLEMENTATION)

/*
Implementation notes

In order not to use extra storage and/or 'borrow' bits from userflags/generation
to indicate handle validity, we invalidate a handle by setting the most
significant bit (MSB) in the handle's next_index.

We can do this because we guarantee that we do not use/need the full bits for
next_index by forcing that the max number of handles that can be allocated does
not use the MSB.

(handles_next_index&MSB) == 0   -> the handle is used
(handles_next_index&MSB) == MSB -> the handle is _not_ used
*/

#define ijha_fifo_h32__join2(x, y) x ## y
#define ijha_fifo_h32__join(x, y) ijha_fifo_h32__join2(x, y)
#define ijha_fifo_h32__static_assert(exp) typedef char ijha_fifo_h32__join(static_assert, __LINE__) [(exp) ? 1 : -1]

ijha_fifo_h32__static_assert(sizeof(unsigned) == 4);
ijha_fifo_h32__static_assert(sizeof(unsigned short) == 2);

#define IJHA_FIFO_H32_NEXT_INDEX_FREE_BIT (0x80000000u)
#define IJHA_FIFO_H32_NEXT_INDEX_USED_MASK (0x7fffffffu)

#define ijha_fifo_h32__roundup(x) (--(x), (x)|=(x)>>1, (x)|=(x)>>2, (x)|=(x)>>4, (x)|=(x)>>8, (x)|=(x)>>16, ++(x))

#define ijha_fifo_h32__pointer_add(p, bytes) ((unsigned char*)(p)+(bytes))
#define ijha_fifo_h32__cast(t, exp) ((t) (exp))

#define ijha_fifo_h32__handle_info_at(index) ijha_fifo_h32__cast(struct ijha_fifo_h32_index*, ijha_fifo_h32__pointer_add(self->handles, self->handles_stride*(index)))

#ifndef IJHA_FIFO_H32_assert
   #include <assert.h>
   #define IJHA_FIFO_H32_assert assert
#endif

static unsigned ijha_fifo_h32__num_bits(unsigned n) { unsigned res=0; while (n >>= 1) res++; return res; }

IJHA_FIFO_H32_API unsigned ijha_fifo_h32_memory_size_needed(unsigned max_num_handles)
{
   ijha_fifo_h32__roundup(max_num_handles);

   return max_num_handles * sizeof(struct ijha_fifo_h32_index);
}

IJHA_FIFO_H32_API void ijha_fifo_h32_reset(struct ijha_fifo_h32 *self)
{
   unsigned i, max_num_handles = self->capacity_mask+1;
   self->num_handles = 0;
   self->freelist_dequeue_index = 0;
   self->freelist_enqueue_index = (unsigned)self->capacity_mask;

   for (i = 0; i != max_num_handles; ++i) {
      struct ijha_fifo_h32_index *current = ijha_fifo_h32__handle_info_at(i);

      current->handle = i;
      current->next_index = ((unsigned)i+1)|IJHA_FIFO_H32_NEXT_INDEX_FREE_BIT;
   }
   ijha_fifo_h32__handle_info_at(self->capacity_mask)->next_index = IJHA_FIFO_H32_NEXT_INDEX_FREE_BIT;
}

IJHA_FIFO_H32_API unsigned ijha_fifo_h32_capacity(struct ijha_fifo_h32 *self)
{
   return self->capacity_mask;
}

IJHA_FIFO_H32_API void ijha_fifo_h32_init_stride(struct ijha_fifo_h32 *self, unsigned max_num_handles,
   unsigned num_userflag_bits, void *memory, unsigned memory_size, unsigned handles_stride)
{
   unsigned userflags_mask;
   ijha_fifo_h32__roundup(max_num_handles);
   IJHA_FIFO_H32_assert(!memory_size || (ijha_fifo_h32_memory_size_needed(max_num_handles) <= memory_size));
   IJHA_FIFO_H32_assert(max_num_handles > 0 && !(max_num_handles & (max_num_handles-1))); /* ensure power of 2 */
   IJHA_FIFO_H32_assert((unsigned)-1 >= max_num_handles-1);
   IJHA_FIFO_H32_assert(IJHA_FIFO_H32_NEXT_INDEX_FREE_BIT >= max_num_handles);
   IJHA_FIFO_H32_assert((ijha_fifo_h32__num_bits(max_num_handles)-1)+num_userflag_bits < 32);

   userflags_mask = num_userflag_bits ? (0xffffffffu << (32-num_userflag_bits)) : 0;
   self->capacity_mask = max_num_handles-1;
   self->generation_mask = ~(self->capacity_mask|userflags_mask);

   self->handles = memory;
   self->handles_stride = handles_stride;

   ijha_fifo_h32_reset(self);
}

IJHA_FIFO_H32_API void ijha_fifo_h32_init(struct ijha_fifo_h32 *self,
   unsigned max_num_handles, unsigned num_userflag_bits, void *memory,
   unsigned memory_size)
{
   ijha_fifo_h32_init_stride(self, max_num_handles, num_userflag_bits, memory,
      memory_size, sizeof(struct ijha_fifo_h32_index));
}

IJHA_FIFO_H32_API void *ijha_fifo_h32_memory(struct ijha_fifo_h32 *self) { return self->handles; }

IJHA_FIFO_H32_API int ijha_fifo_h32_valid(struct ijha_fifo_h32 *self, unsigned handle)
{
   struct ijha_fifo_h32_index *stored = ijha_fifo_h32__handle_info_at(handle&self->capacity_mask);
   return stored->handle == handle && (stored->next_index&IJHA_FIFO_H32_NEXT_INDEX_FREE_BIT)==0;
}

IJHA_FIFO_H32_API unsigned ijha_fifo_h32_alloc_mask(struct ijha_fifo_h32 *self,
   unsigned userflags, unsigned *handle_out)
{
   unsigned userflags_mask = ~(self->capacity_mask|self->generation_mask);
   IJHA_FIFO_H32_assert((userflags_mask&userflags) == userflags);

   if (self->num_handles == self->capacity_mask) {
      *handle_out = 0;
      return IJHA_FIFO_H32_INVALID_INDEX;
   } else {
      unsigned object_index = (unsigned)self->num_handles++;
      unsigned generation_mask = self->generation_mask;
      unsigned generation_to_add = self->capacity_mask+1;
      unsigned next_to_last_generation_mask = (generation_mask<<1)&generation_mask;
      unsigned index_handle;
      struct ijha_fifo_h32_index *index = ijha_fifo_h32__handle_info_at(self->freelist_dequeue_index);
      IJHA_FIFO_H32_assert(index->next_index&IJHA_FIFO_H32_NEXT_INDEX_FREE_BIT);
      index->next_index &= IJHA_FIFO_H32_NEXT_INDEX_USED_MASK;
      IJHA_FIFO_H32_assert(self->freelist_dequeue_index == (index->handle&self->capacity_mask));
      self->freelist_dequeue_index = index->next_index;

      index_handle = index->handle;

      if (next_to_last_generation_mask) {
         /* ensure that the handle is not 0 (zero) or 0xffffffffu */
         unsigned current_generation = index_handle&generation_mask;
         unsigned new_generation = (current_generation==next_to_last_generation_mask) ? generation_to_add : (generation_mask & (index_handle+generation_to_add));

         index->handle = (index_handle&self->capacity_mask) | new_generation | userflags;

         IJHA_FIFO_H32_assert(index->handle&generation_mask); /* non zero */
         IJHA_FIFO_H32_assert((index->handle&generation_mask)!=generation_mask); /* not full generation */
         IJHA_FIFO_H32_assert((index->handle&generation_mask)!=(index_handle&generation_mask)); /* has changed generation */
      } else if (generation_mask) {
         /* 1 bit generation */
         unsigned new_generation = generation_mask&(index_handle+generation_to_add);
         index->handle = (index_handle&self->capacity_mask) | new_generation | userflags;
         IJHA_FIFO_H32_assert((index->handle&generation_mask)!=(index_handle&generation_mask)); /* has changed generation */
      } else {
         /* 0 bit generation i.e. no generation, just replace the userflags */
         index->handle = (~userflags_mask&index_handle) | userflags;
      }

      IJHA_FIFO_H32_assert((index_handle&self->capacity_mask) == (index->handle&self->capacity_mask));

      *handle_out = index->handle;
      return object_index;
   }
}

IJHA_FIFO_H32_API unsigned ijha_fifo_h32_alloc(struct ijha_fifo_h32 *self, unsigned *handle_out)
{
   return ijha_fifo_h32_alloc_mask(self, 0, handle_out);
}

IJHA_FIFO_H32_API void ijha_fifo_h32_dealloc(struct ijha_fifo_h32 *self, unsigned handle)
{
   unsigned sparse_index = handle&self->capacity_mask;
   --self->num_handles;
   IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handle));
   ijha_fifo_h32__handle_info_at(sparse_index)->next_index |= IJHA_FIFO_H32_NEXT_INDEX_FREE_BIT;

   ijha_fifo_h32__handle_info_at(self->freelist_enqueue_index)->next_index = (unsigned)sparse_index | IJHA_FIFO_H32_NEXT_INDEX_FREE_BIT;
   self->freelist_enqueue_index = sparse_index;
}

#endif /* IJHA_FIFO_H32_IMPLEMENTATION */

#if defined (IJHA_FIFO_H32_TEST) || defined (IJHA_FIFO_H32_TEST_MAIN)

#ifndef IJHA_FIFO_H32_memset
   #include <string.h>
   #define IJHA_FIFO_H32_memset memset
#endif

static void ijha_fifo_h32_test_instance(struct ijha_fifo_h32 *self,
   unsigned alloc_userflags, unsigned *handles, int *valids)
{
   unsigned i, j, num_valids, num_useable_handles = self->capacity_mask;
   unsigned last_index = num_useable_handles-1;
   unsigned next_to_last_index = num_useable_handles-2;

   /* alloc all */
   for (i=0; i!=num_useable_handles; ++i) {
      ijha_fifo_h32_alloc_mask(self, alloc_userflags, &handles[i]);
      for (j=0; j<i+1; ++j)
         IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[j]) && (handles[j]&alloc_userflags)==alloc_userflags);
   }
   {
      num_valids = 0;
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == num_useable_handles);
   }

   /* dealloc all */
   for (i=0; i!=num_useable_handles; ++i) {
      ijha_fifo_h32_dealloc(self, handles[i]);
      IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[i]));
      for (j=num_useable_handles-1; j!=i; --j)
         IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[j]) && (handles[j]&alloc_userflags)==alloc_userflags);
   }
   {
      num_valids = 0;
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == 0);
   }

   /* alloc all */
   for (i=0; i!=num_useable_handles; ++i) {
      ijha_fifo_h32_alloc_mask(self, alloc_userflags, &handles[i]);
      for (j=0; j<i+1; ++j)
         IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[j]) && (handles[j]&alloc_userflags)==alloc_userflags);
   }
   {
      num_valids = 0;
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == num_useable_handles);
   }

   /* dealloc last */
   {
      num_valids = 0;
      IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[last_index]) && (handles[last_index]&alloc_userflags)==alloc_userflags);
      ijha_fifo_h32_dealloc(self, handles[last_index]);
      IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[last_index]));
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == num_useable_handles-1);
   }

   /* dealloc next to last */
   {
      num_valids = 0;
      IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[next_to_last_index]) && (handles[next_to_last_index]&alloc_userflags)==alloc_userflags);
      ijha_fifo_h32_dealloc(self, handles[next_to_last_index]);
      IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[next_to_last_index]));
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == num_useable_handles-2);
   }

   IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[last_index]));
   ijha_fifo_h32_alloc_mask(self, alloc_userflags, &handles[last_index]);
   IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[last_index]) && (handles[last_index]&alloc_userflags)==alloc_userflags);

   {
      num_valids = 0;
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == num_useable_handles-1);
   }

   IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[next_to_last_index]));
   ijha_fifo_h32_alloc_mask(self, alloc_userflags, &handles[next_to_last_index]);
   IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[next_to_last_index]));
   IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[next_to_last_index]) && (handles[next_to_last_index]&alloc_userflags)==alloc_userflags);

   {
      num_valids = 0;
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == num_useable_handles);
   }

   for (i=0; i!=num_useable_handles; ++i) {
      ijha_fifo_h32_dealloc(self, handles[i]);
      IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[i]));
      for (j=num_useable_handles-1; j!=i; --j)
         IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[j]) && (handles[j]&alloc_userflags)==alloc_userflags);
   }

   {
      num_valids = 0;
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == 0);
   }

   /* alloc all */
   for (i=0; i!=num_useable_handles; ++i) {
      ijha_fifo_h32_alloc_mask(self, alloc_userflags, &handles[i]);
      for (j=0; j<i+1; ++j)
         IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[j]) && (handles[j]&alloc_userflags)==alloc_userflags);
   }
   {
      num_valids = 0;
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == num_useable_handles);
   }

   /* dealloc next to last */
   {
      num_valids = 0;
      IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[next_to_last_index]) && (handles[next_to_last_index]&alloc_userflags)==alloc_userflags);
      ijha_fifo_h32_dealloc(self, handles[next_to_last_index]);
      IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[next_to_last_index]));
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == num_useable_handles-1);
   }

   IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[next_to_last_index]));
   ijha_fifo_h32_alloc_mask(self, alloc_userflags, &handles[next_to_last_index]);
   IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[next_to_last_index]) && (handles[next_to_last_index]&alloc_userflags)==alloc_userflags);

   /* dealloc all */
   for (i=0; i!=num_useable_handles; ++i) {
      IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[i]) && (handles[i]&alloc_userflags)==alloc_userflags);
      ijha_fifo_h32_dealloc(self, handles[i]);
      IJHA_FIFO_H32_assert(!ijha_fifo_h32_valid(self, handles[i]));
      for (j=num_useable_handles-1; j!=i; --j)
         IJHA_FIFO_H32_assert(ijha_fifo_h32_valid(self, handles[j]));
   }
   {
      num_valids = 0;
      for (j=0; j!=num_useable_handles; ++j) {
         valids[j] = ijha_fifo_h32_valid(self, handles[j]);
         num_valids += valids[j] ? 1 : 0;
      }
      IJHA_FIFO_H32_assert(num_valids == 0);
   }
}

static void ijha_fifo_h32_test(void)
{
#define IJHA_unit_test_num_objects_to_alloc (4)
   unsigned char test_memory[1024*16];
   int valids[IJHA_unit_test_num_objects_to_alloc];
   unsigned index, handles[IJHA_unit_test_num_objects_to_alloc];

   struct ijha_fifo_h32 pa, *self = &pa;

   for (index = 0; index != 4; ++index) {
      unsigned max_num_handles = IJHA_unit_test_num_objects_to_alloc;
      unsigned num_generation_bits = index;
      unsigned num_userflag_bits = 32-ijha_fifo_h32__num_bits(IJHA_unit_test_num_objects_to_alloc)-num_generation_bits;
      unsigned alloc_userflags = (1u<<(32-num_userflag_bits));

      ijha_fifo_h32_init(self, max_num_handles, num_userflag_bits, test_memory, sizeof(test_memory));

      ijha_fifo_h32_test_instance(self, alloc_userflags, handles, valids);
   }

#undef IJHA_unit_test_num_objects_to_alloc
}

#if defined (IJHA_FIFO_H32_TEST_MAIN)
#include <stdio.h>

int main(int args, char **argc)
{
   (void)(args, argc);
   printf("running tests for IJHA_FIFO_H32.\n");
   ijha_fifo_h32_test();
   printf("tests for IJHA_FIFO_H32. succeeded\n");
   return 0;
}
#endif /* IJHA_FIFO_H32_TEST_MAIN */

#endif /* IJHA_FIFO_H32_TEST || IJHA_FIFO_H32_TEST_MAIN */

/*
LICENSE
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - 3-Clause BSD License
Copyright (c) 2017, Fredrik Engkvist
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
