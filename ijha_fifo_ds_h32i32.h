/* clang-format off */
/*
ijha_fifo_ds_h32i32 : IncredibleJunior HandleAllocator FIFO DenseSparse-bookkeeping 32-bit Handles 32-bit Indices

ijha_fifo_ds_h32i32 is a FIFO handle allocator built on top of ijha_fifo_h32 with
added bookkeeping of handles' sparse/dense relationships to be able to keep
all (used) data linear in memory while retaining stable handles.

This was inspired by Niklas Gray's (@niklasfrykholm) blogpost about packed arrays [1],
which is recommended reading (along with everything else he ever wrote).

In order to keep the ijha_fifo_h32's handles map to data that is kept linear
in memory, extra bookkeeping is needed to track the handles (sparse) mapping
into the linear (dense/packed) array.

If we have an array of 4 objects and were to use ijha_fifo_h32's sparse handles
to map to slots in the array our array would look (potentially) like:

F == Free
U == Used

+---------------+
| F | U | F | U |
+---------------+

Iterating over the array would iterate over a lot of free/unoccupied/unused space
(and special logic has to be implemented to check if current object is valid or not).

We would like to be able to return a handle that does not invalidate on
(internal) data movement and still be able to keep all data linear in memory.

By storing sparse to dense mappings we can keep all our data linear in memory,
rearrange it on deletes and still keep the outstanding handles stable. This comes
with a cost of a extra indirection when looking up a handle to object as we now have
to map the sparse handle to its dense index but to quote @niklasfrykholm:

"...in most cases more items are touched internally than are referenced externally."

(The extra indirection comes when you need to access your handles dense array(s)
data. There is is nothing that keeps you from having multiple sparse / dense arrays
that is indexed by the handle for different purposes.)

This gives us an array that looks like
+---------------+
| U | U | F | F |
+---------------+

This is good for cache utilization and the soul.

ijha_fifo_ds_h32i32 is built on top of ijha_fifo_h32 all functions of ijha_fifo_h32
are available to ijha_fifo_ds_h32i32 (but not vice-versa).

ijha_fifo_ds_h32i32 used ijha_fifo_h32 for handle allocation with added dense/sparse
mapping layered on top, which means that it shares all ijha_fifo_h32 limitations/features.

Usage examples+tests is at the bottom of the file in the IJHA_FIFO_DS_H32I32_TEST section.

This file provides both the interface and the implementation.

The handle allocator is implemented as a stb-style header-file library[2]
which means that in *ONE* source file, put:

   #define IJHA_FIFO_DS_H32I32_IMPLEMENTATION
   // if custom assert wanted (and no dependencies on assert.h)
   #define IJHA_FIFO_DS_H32I32_assert      custom_assert
   #include "ijha_fifo_ds_h32i32.h"

Other source files should just include ijha_fifo_ds_h32i32.h

NB: ijha_fifo_ds_h32i32 depends on ijha_fifo_h32 (that implementation also has to be instantiated)

References:

[1] http://bitsquid.blogspot.se/2011/09/managing-decoupling-part-4-id-lookup.html
[2] https://github.com/nothings/stb

LICENSE
   See end of file for license information
*/
#ifndef IJHA_FIFO_DS_H32I32_INCLUDED_H
#define IJHA_FIFO_DS_H32I32_INCLUDED_H

#include "ijha_fifo_h32.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(IJHA_FIFO_DS_H32I32_STATIC)
   #define IJHA_FIFO_DS_H32I32_API static
#else
   #define IJHA_FIFO_DS_H32I32_API extern
#endif

/* storage size needed for max_num_handles with _optional_ userdata size in bytes per item.
   if the handles will _not_ be interleaved with userdata, then the userdata size is 0.

   max_num_handles will be rounded up to next power of 2.

   NB: the size of 'struct ijha_fifo_h32' is not included */
IJHA_FIFO_DS_H32I32_API unsigned ijha_fifo_ds_h32i32_memory_size_needed(unsigned max_num_handles,
   unsigned userdata_size_in_bytes_per_item);

/* this just forwards to 'ijha_fifo_h32_init_stride' with a set handles_stride so the
   same limitations apply. */
IJHA_FIFO_DS_H32I32_API void ijha_fifo_ds_h32i32_init(struct ijha_fifo_h32 *self, unsigned max_num_handles,
   unsigned num_userflag_bits, unsigned userdata_size_in_bytes_per_item, void *memory, unsigned memory_size);

/* returns the packed/dense index of the newly acquired handle on success
   or IJHA_FIFO_H32_INVALID_INDEX on failure (all handles are used i.e. full) */
IJHA_FIFO_DS_H32I32_API unsigned ijha_fifo_ds_h32i32_acquire_mask(struct ijha_fifo_h32 *self,
   unsigned userflag_mask, unsigned *handle_out);
#define ijha_fifo_ds_h32i32_acquire(self, handle_out) ijha_fifo_ds_h32i32_acquire_mask((self), 0, (handle_out))

/*
release the _valid_ handle and returns if it was the last index in the packed/dense array.

returns:
   1 if the handle was the last/back handle
   0 if it was _not_ the last handle

examples:
   unsigned move_from_index, move_to_index;
   ijha_fifo_ds_h32i32_release(self, handle, &move_from_index, &move_to_index);
   my_objects[move_to_index] = my_objects[move_from_index]
   ...
   unsigned handle_0, handle_1, handle_2;
   ijha_fifo_ds_h32i32_acquire(self, &handle_0);
   ijha_fifo_ds_h32i32_acquire(self, &handle_1);
   ijha_fifo_ds_h32i32_acquire(self, &handle_2);
   assert(ijha_fifo_ds_h32i32_release(self, &handle_2));
   assert(!ijha_fifo_ds_h32i32_release(self, &handle_0));

NB:
   assumes the handle is valid. if uncertain, use 'ijha_fifo_h32_valid(self, handle)'
   beforehand to check handle validity */
IJHA_FIFO_DS_H32I32_API int ijha_fifo_ds_h32i32_release(struct ijha_fifo_h32 *self, unsigned handle,
   unsigned *move_from_index, unsigned *move_to_index);

/* returns the packed/dense index of the handle or IJHA_FIFO_H32_INVALID_INDEX if handle is invalid */
IJHA_FIFO_DS_H32I32_API unsigned ijha_fifo_ds_h32i32_dense_index(struct ijha_fifo_h32 *self, unsigned handle);

#ifdef __cplusplus
}
#endif

#endif /* IJHA_FIFO_DS_H32I32_INCLUDED_H */

#if defined(IJHA_FIFO_DS_H32I32_IMPLEMENTATION)

#ifndef IJHA_FIFO_DS_H32I32_assert
   #include <assert.h>
   #define IJHA_FIFO_DS_H32I32_assert assert
#endif

#define ijha_fifo_ds_h32i32__roundup(x) (--(x), (x)|=(x)>>1, (x)|=(x)>>2, (x)|=(x)>>4, (x)|=(x)>>8, (x)|=(x)>>16, ++(x))

#define ijha_fifo_ds_h32i32__pointer_add(type, p, bytes) ((type)((unsigned char*)(p)+(bytes)))

#define ijha_fifo_ds_h32i32__handle_info_at(index) ijha_fifo_ds_h32i32__pointer_add(struct ijha_fifo_ds_h32i32_indexhandle*, self->handles, ijha_fifo_h32_handle_stride(self->handles_stride_userdata_offset)*(index))

static unsigned ijha_fifo_ds_h32i32__num_bits(unsigned n) { unsigned res=0; while (n >>= 1) res++; return res; }

/* sparse/dense index notes for struct ijha_fifo_ds_h32i32_indexhandle

sparse_index = handle&capacity_mask

handles[sparse_index].dense_index is the dense/packed index corresponding to handle
handles[dense_index].sparse_index is the sparse_index of dense_index.

the handle of the dense_index is handles[ handles[dense_index].sparse_index ].handle
*/
struct ijha_fifo_ds_h32i32_indexhandle {
   struct ijha_fifo_h32_indexhandle handle_alloc_index;

   unsigned dense_index; /* index into the 'packed array'/'dense part' */
   unsigned sparse_index; /* the sparse index of indices[dense_index] */
};

IJHA_FIFO_DS_H32I32_API unsigned ijha_fifo_ds_h32i32_memory_size_needed(unsigned max_num_handles,
   unsigned userdata_size_in_bytes_per_item)
{
   ijha_fifo_ds_h32i32__roundup(max_num_handles);
   return max_num_handles * (sizeof(struct ijha_fifo_ds_h32i32_indexhandle) + userdata_size_in_bytes_per_item);
}

IJHA_FIFO_DS_H32I32_API void ijha_fifo_ds_h32i32_init(struct ijha_fifo_h32 *self, unsigned max_num_handles,
   unsigned num_userflag_bits, unsigned userdata_size_in_bytes_per_item, void *memory, unsigned memory_size)
{
   ijha_fifo_ds_h32i32__roundup(max_num_handles);
   IJHA_FIFO_DS_H32I32_assert(!memory_size || (ijha_fifo_ds_h32i32_memory_size_needed(max_num_handles, userdata_size_in_bytes_per_item) <= memory_size));
   IJHA_FIFO_DS_H32I32_assert((unsigned)(unsigned)-1 >= max_num_handles);
   ijha_fifo_h32_init_stride(self, max_num_handles, num_userflag_bits, userdata_size_in_bytes_per_item,  sizeof(struct ijha_fifo_ds_h32i32_indexhandle), memory, memory_size);

   ijha_fifo_h32_reset(self);
}

IJHA_FIFO_DS_H32I32_API unsigned ijha_fifo_ds_h32i32_acquire_mask(struct ijha_fifo_h32 *self,
   unsigned userflags, unsigned *handle_out)
{
   unsigned dense_index = self->num_handles;
   unsigned sparse_index = ijha_fifo_h32_acquire_mask(self, userflags, handle_out);
   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_userdata_offset(self->handles_stride_userdata_offset) == sizeof(struct ijha_fifo_ds_h32i32_indexhandle));
   if (sparse_index == IJHA_FIFO_H32_INVALID_INDEX) {
      return IJHA_FIFO_H32_INVALID_INDEX;
   } else {
      ijha_fifo_ds_h32i32__handle_info_at(sparse_index)->dense_index = dense_index;
      ijha_fifo_ds_h32i32__handle_info_at(dense_index)->sparse_index = sparse_index;
      return dense_index;
   }
}

IJHA_FIFO_DS_H32I32_API int ijha_fifo_ds_h32i32_release(struct ijha_fifo_h32 *self,
   unsigned handle, unsigned *move_from_index, unsigned *move_to_index)
{
   unsigned sparse_index_of_removed = ijha_fifo_h32_release(self, handle);
   unsigned dense_index_of_removed = ijha_fifo_ds_h32i32__handle_info_at(sparse_index_of_removed)->dense_index;
   int is_back_index = dense_index_of_removed == self->num_handles;
   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_userdata_offset(self->handles_stride_userdata_offset) == sizeof(struct ijha_fifo_ds_h32i32_indexhandle));
   IJHA_FIFO_DS_H32I32_assert(sparse_index_of_removed != IJHA_FIFO_H32_INVALID_INDEX);

   /* the item @ 'back_index' in indices have to be updated to point to 'dense_index_of_removed' */
   if (!is_back_index) {
      struct ijha_fifo_ds_h32i32_indexhandle *back_info = ijha_fifo_ds_h32i32__handle_info_at(self->num_handles);
      unsigned sparse_index_of_back = back_info->sparse_index;
      ijha_fifo_ds_h32i32__handle_info_at(sparse_index_of_back)->dense_index = (unsigned) dense_index_of_removed;
      ijha_fifo_ds_h32i32__handle_info_at(dense_index_of_removed)->sparse_index = sparse_index_of_back;
   }

   ijha_fifo_ds_h32i32__handle_info_at(sparse_index_of_removed)->dense_index = (unsigned)-1;

   *move_from_index = self->num_handles;
   *move_to_index = dense_index_of_removed;

   return is_back_index;
}

IJHA_FIFO_DS_H32I32_API unsigned ijha_fifo_ds_h32i32_dense_index(struct ijha_fifo_h32 *self, unsigned handle)
{
   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_userdata_offset(self->handles_stride_userdata_offset) == sizeof(struct ijha_fifo_ds_h32i32_indexhandle));
   if (ijha_fifo_h32_valid(self, handle))
      return ijha_fifo_ds_h32i32__handle_info_at(handle&self->capacity_mask)->dense_index;

   return IJHA_FIFO_H32_INVALID_INDEX;
}

#endif /* IJHA_FIFO_DS_H32I32_IMPLEMENTATION */

#if defined (IJHA_FIFO_DS_H32I32_TEST) || defined (IJHA_FIFO_DS_H32I32_TEST_MAIN)

#ifndef IJHA_FIFO_DS_H32I32_memset
   #include <string.h>
   #define IJHA_FIFO_DS_H32I32_memset memset
#endif

#if defined (IJHA_FIFO_DS_H32I32_TEST_MAIN)
   #if !defined (IJHA_FIFO_H32_TEST) || !defined(IJHA_FIFO_H32_IMPLEMENTATION)

      #ifndef IJHA_FIFO_H32_memset
         #define IJHA_FIFO_H32_memset IJHA_FIFO_DS_H32I32_memset
      #endif

      #ifndef IJHA_FIFO_H32_assert
         #define IJHA_FIFO_H32_assert IJHA_FIFO_DS_H32I32_assert
      #endif

      #if !defined(IJHA_FIFO_H32_IMPLEMENTATION)
         #define IJHA_FIFO_H32_IMPLEMENTATION
      #endif

      #if !defined(IJHA_FIFO_H32_TEST)
         #define IJHA_FIFO_H32_TEST
      #endif

      #include "ijha_fifo_h32.h"
   #endif
#endif

typedef struct ijha_fifo_ds_h32i32_test_object {
   int valid;
   unsigned verify_handle_a, verify_handle_b;
   unsigned some_other_data;
   unsigned test_object_index; /* index into array of struct ijha_fifo_ds_h32i32_alive_object */
} ijha_fifo_ds_h32i32_test_object;

typedef struct ijha_fifo_ds_h32i32_alive_object {
   unsigned test_object_handle; /* handle of 'owner' */
   unsigned data;
} ijha_fifo_ds_h32i32_alive_object;

static void ijha_fifo_ds_h32i32_verify_handles(struct ijha_fifo_h32 *self,
   unsigned *handles, unsigned num_handles, ijha_fifo_ds_h32i32_test_object *test_objects)
{
   unsigned loop_index;
   for (loop_index=0; loop_index != num_handles; ++loop_index) {
      unsigned handle = handles[loop_index];
      if (!handle)
         continue; /* invalid handle */
      else {
         unsigned index = ijha_fifo_ds_h32i32_dense_index(self, handle);
         ijha_fifo_ds_h32i32_test_object *to = test_objects+index;

         IJHA_FIFO_DS_H32I32_assert(handle == to->verify_handle_a && handle == to->verify_handle_b);
      }
   }
}

static void ijha_fifo_ds_h32i32_test(void)
{
#define IJHA_unit_test_num_handles_to_acquire (4)
   unsigned char test_memory[1024*16];
   unsigned handles[IJHA_unit_test_num_handles_to_acquire];
   int valids[IJHA_unit_test_num_handles_to_acquire];
   ijha_fifo_ds_h32i32_test_object test_objects[IJHA_unit_test_num_handles_to_acquire];
   ijha_fifo_ds_h32i32_alive_object active_objects[IJHA_unit_test_num_handles_to_acquire];
   unsigned userdata_size_base = sizeof(unsigned);
   struct ijha_fifo_h32 pa, *self = &pa;
   unsigned index, inner;
   unsigned IJHA_TO_INVALID_INDEX = (unsigned)-1;

   for (index = 0; index != 8; ++index) {
      unsigned max_num_handles = IJHA_unit_test_num_handles_to_acquire;
      unsigned num_generation_bits = index;
      unsigned num_userflag_bits = 32-ijha_fifo_ds_h32i32__num_bits(IJHA_unit_test_num_handles_to_acquire)-num_generation_bits;
      unsigned alloc_userflags = (1u<<(32-num_userflag_bits));
      unsigned capacity;
      unsigned userdata_size = 0;
      int has_unsigned_userdata = index >= 4;
      if (has_unsigned_userdata)
         userdata_size = userdata_size_base*(index-3);

      ijha_fifo_ds_h32i32_init(self, max_num_handles, num_userflag_bits, userdata_size, test_memory, sizeof(test_memory));
      capacity = self->capacity_mask;
      ijha_fifo_h32_test_instance(self, alloc_userflags, has_unsigned_userdata, handles, valids);
      IJHA_FIFO_H32_memset(test_objects, 0, sizeof test_objects);
      for (inner = 0; inner != 2; ++inner) {
         unsigned loop_index, packedi;
         unsigned num_active_objects = 0;
         IJHA_FIFO_DS_H32I32_memset(handles, 0, sizeof(handles));
         IJHA_FIFO_DS_H32I32_memset(valids, 0, sizeof(valids));

         for (loop_index=0; loop_index != capacity; ++loop_index) {
            ijha_fifo_ds_h32i32_test_object *object;
            unsigned dense_index = ijha_fifo_ds_h32i32_acquire_mask(self, alloc_userflags, &handles[loop_index]);
            IJHA_FIFO_DS_H32I32_assert(dense_index != IJHA_FIFO_H32_INVALID_INDEX);
            IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_valid(self, handles[loop_index]) && (handles[loop_index]&alloc_userflags)==alloc_userflags);
            object = test_objects+dense_index;
            object->verify_handle_a = object->verify_handle_b = handles[loop_index];
            object->valid = 1;
            if (loop_index % 2 == 0) {
               ijha_fifo_ds_h32i32_alive_object *alive_object = active_objects+num_active_objects;
               alive_object->data = handles[loop_index];
               alive_object->test_object_handle = handles[loop_index];
               object->test_object_index = num_active_objects++;
            }
            else
               object->test_object_index = IJHA_TO_INVALID_INDEX;
         }

         for (loop_index=0; loop_index != capacity; ++loop_index) {
            IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_valid(self, handles[loop_index]) && (handles[loop_index]&alloc_userflags)==alloc_userflags);
            IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, handles[loop_index]) == loop_index);
         }

         IJHA_FIFO_DS_H32I32_assert(self->num_handles == capacity);

         /* iterate over the packed array */
         for (packedi=0; packedi != self->num_handles; ++packedi) {
            IJHA_FIFO_DS_H32I32_assert(test_objects[packedi].valid);   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, test_objects[packedi].verify_handle_a) == packedi);
         }
         for (packedi=self->num_handles; packedi < capacity; ++packedi) {
            IJHA_FIFO_DS_H32I32_assert(!test_objects[packedi].valid);
         }

         {
            unsigned testi;
            for (testi=0; testi!=num_active_objects; ++testi) {
               ijha_fifo_ds_h32i32_alive_object *ao = active_objects + testi;
               unsigned ao_index = ijha_fifo_ds_h32i32_dense_index(self, ao->test_object_handle);
               ijha_fifo_ds_h32i32_test_object *to = test_objects + ao_index;
               IJHA_FIFO_DS_H32I32_assert(to->test_object_index == testi);
            }
         }

         {
            unsigned move_from, move_to;
            for (loop_index = 0; loop_index != capacity; ++loop_index) {
               int last_handle;
               unsigned removed_test_index;
               ijha_fifo_ds_h32i32_test_object *removed_o;
               unsigned handle_to_remove = handles[loop_index];
               IJHA_FIFO_DS_H32I32_assert(handle_to_remove);

               last_handle = ijha_fifo_ds_h32i32_release(self, handle_to_remove, &move_from, &move_to);
               IJHA_FIFO_DS_H32I32_assert(!last_handle || move_to == move_from);
               removed_o = test_objects + move_to;
               IJHA_FIFO_DS_H32I32_assert(removed_o->verify_handle_a == handle_to_remove && removed_o->verify_handle_b == handle_to_remove);
               removed_test_index = removed_o->test_object_index;
               removed_o = 0;

               /* swap the data (has to be done before active object below) */
               test_objects[move_to] = test_objects[move_from];
               test_objects[move_from].valid = 0;

               if (removed_test_index != IJHA_TO_INVALID_INDEX) {
                  if (1 == num_active_objects) {
                     num_active_objects = 0; /* removing the only/last object */
                  } else {
                     ijha_fifo_ds_h32i32_alive_object *last_active_object = active_objects + (num_active_objects - 1);
                     IJHA_FIFO_DS_H32I32_assert(num_active_objects);
                     if (last_active_object->test_object_handle != handle_to_remove) {
                        /* update the reference */
                        unsigned last_object_index = ijha_fifo_ds_h32i32_dense_index(self, last_active_object->test_object_handle);
                        ijha_fifo_ds_h32i32_test_object *last_object_ref = test_objects + last_object_index;
                        last_object_ref->test_object_index = removed_test_index;
                     }

                     active_objects[removed_test_index] = active_objects[--num_active_objects];
                  }
               }

               IJHA_FIFO_DS_H32I32_assert(!ijha_fifo_h32_valid(self, handles[loop_index]));
               handles[loop_index] = 0;

               /* iterate over the packed array */
               for (packedi=0; packedi != self->num_handles; ++packedi) {
                  IJHA_FIFO_DS_H32I32_assert(test_objects[packedi].valid);   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, test_objects[packedi].verify_handle_a) == packedi);
               }
               for (packedi=self->num_handles; packedi < capacity; ++packedi) {
                  IJHA_FIFO_DS_H32I32_assert(!test_objects[packedi].valid);
               }

               ijha_fifo_ds_h32i32_verify_handles(self, handles,
                  capacity, test_objects);
               {
                  unsigned testi;
                  for (testi=0; testi!=num_active_objects; ++testi) {
                     ijha_fifo_ds_h32i32_alive_object *ao = active_objects + testi;
                     unsigned ao_index = ijha_fifo_ds_h32i32_dense_index(self, ao->test_object_handle);
                     ijha_fifo_ds_h32i32_test_object *to = test_objects + ao_index;

                     IJHA_FIFO_DS_H32I32_assert(to->valid && to->test_object_index == testi);
                  }
               }
            }
         }

         IJHA_FIFO_DS_H32I32_assert(self->num_handles == 0);

         for (loop_index=0; loop_index != capacity; ++loop_index) {
            IJHA_FIFO_DS_H32I32_assert(!ijha_fifo_h32_valid(self, handles[loop_index]));
         }

         /* iterate over the packed array */
         for (packedi=0; packedi != self->num_handles; ++packedi) {
            IJHA_FIFO_DS_H32I32_assert(test_objects[packedi].valid);   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, test_objects[packedi].verify_handle_a) == packedi);
         }
         for (packedi=self->num_handles; packedi < capacity; ++packedi) {
            IJHA_FIFO_DS_H32I32_assert(!test_objects[packedi].valid);
         }

         for (loop_index=0; loop_index != capacity; ++loop_index) {
            unsigned test_index = ijha_fifo_ds_h32i32_acquire_mask(self, alloc_userflags, &handles[loop_index]);
            IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_valid(self, handles[loop_index]));
            ijha_fifo_ds_h32i32_test_object *to = test_objects+test_index;
            to->verify_handle_a = to->verify_handle_b = handles[loop_index];
            to->valid = 1;
            to->test_object_index = IJHA_TO_INVALID_INDEX;

            /* iterate over the packed array */
            for (packedi=0; packedi != self->num_handles; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(test_objects[packedi].valid);   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, test_objects[packedi].verify_handle_a) == packedi);
            }
            for (packedi=self->num_handles; packedi < capacity; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(!test_objects[packedi].valid);
            }
         }

         IJHA_FIFO_DS_H32I32_assert(self->num_handles == capacity);

         for (loop_index=0; loop_index != capacity; ++loop_index) {
            IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_valid(self, handles[loop_index]));
         }

         ijha_fifo_ds_h32i32_verify_handles(self, handles,
            capacity, test_objects);

         for (loop_index=0; loop_index != capacity; ++loop_index) {
            unsigned move_from, move_to;
            if (ijha_fifo_ds_h32i32_release(self, handles[loop_index], &move_from, &move_to)) {
               IJHA_FIFO_DS_H32I32_assert(move_to == move_from);
            } else {
               IJHA_FIFO_DS_H32I32_assert(move_to != move_from);
			   IJHA_FIFO_DS_H32I32_assert(capacity > move_to);
			   IJHA_FIFO_DS_H32I32_assert(capacity > move_from);
               test_objects[move_to] = test_objects[move_from]; /* clang complains here */
            }
            test_objects[move_from].valid = 0;
            IJHA_FIFO_DS_H32I32_assert(!ijha_fifo_h32_valid(self, handles[loop_index]));
            handles[loop_index] = 0;
            ijha_fifo_ds_h32i32_verify_handles(self, handles,
               capacity, test_objects);

            /* iterate over the packed array */
            for (packedi=0; packedi != self->num_handles; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(test_objects[packedi].valid);   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, test_objects[packedi].verify_handle_a) == packedi);
            }
            for (packedi=self->num_handles; packedi < capacity; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(!test_objects[packedi].valid);
            }
         }

         IJHA_FIFO_DS_H32I32_assert(self->num_handles == 0);

         for (loop_index=0; loop_index != capacity; ++loop_index) {
            unsigned test_index = ijha_fifo_ds_h32i32_acquire_mask(self, alloc_userflags, &handles[loop_index]);
            IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_valid(self, handles[loop_index]));
            ijha_fifo_ds_h32i32_test_object *to = test_objects+test_index;
            IJHA_FIFO_DS_H32I32_assert(test_index == loop_index);
            IJHA_FIFO_DS_H32I32_assert(test_index == ijha_fifo_ds_h32i32_dense_index(self, handles[loop_index]));
            to->verify_handle_a = to->verify_handle_b = handles[loop_index];
            to->valid = 1;
            to->test_object_index = IJHA_TO_INVALID_INDEX;

            ijha_fifo_ds_h32i32_verify_handles(self, handles,
               capacity, test_objects);

            /* iterate over the packed array */
            for (packedi=0; packedi != self->num_handles; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(test_objects[packedi].valid);   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, test_objects[packedi].verify_handle_a) == packedi);
            }
            for (packedi=self->num_handles; packedi < capacity; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(!test_objects[packedi].valid);
            }
         }

         IJHA_FIFO_DS_H32I32_assert(self->num_handles == capacity);

         {
            unsigned move_from, move_to;
            IJHA_FIFO_DS_H32I32_assert(handles[0]);
            IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_valid(self, handles[0]));
            if (ijha_fifo_ds_h32i32_release(self, handles[0], &move_from, &move_to)) {
               IJHA_FIFO_DS_H32I32_assert(move_to == move_from);
            } else {
               IJHA_FIFO_DS_H32I32_assert(move_to != move_from);
               test_objects[move_to] = test_objects[move_from];
            }
            test_objects[move_from].valid = 0;
            IJHA_FIFO_DS_H32I32_assert(!ijha_fifo_h32_valid(self, handles[0]));
            handles[0] = 0;
            ijha_fifo_ds_h32i32_verify_handles(self, handles, capacity, test_objects);

            /* iterate over the packed array */
            for (packedi=0; packedi != self->num_handles; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(test_objects[packedi].valid);   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, test_objects[packedi].verify_handle_a) == packedi);
            }
            for (packedi=self->num_handles; packedi < capacity; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(!test_objects[packedi].valid);
            }
         }

         loop_index = capacity;

         while (loop_index--) {
            unsigned move_from, move_to;
            if (!handles[loop_index])
               continue;

            IJHA_FIFO_DS_H32I32_assert(ijha_fifo_h32_valid(self, handles[loop_index]));
            if (ijha_fifo_ds_h32i32_release(self, handles[loop_index], &move_from, &move_to)) {
               IJHA_FIFO_DS_H32I32_assert(move_to == move_from);
            } else {
               IJHA_FIFO_DS_H32I32_assert(move_to != move_from);
               test_objects[move_to] = test_objects[move_from];
            }
            test_objects[move_from].valid = 0;

            IJHA_FIFO_DS_H32I32_assert(!ijha_fifo_h32_valid(self, handles[loop_index]));
            handles[loop_index] = 0;

            ijha_fifo_ds_h32i32_verify_handles(self, handles,
               capacity, test_objects);

            /* iterate over the packed array */
            for (packedi=0; packedi != self->num_handles; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(test_objects[packedi].valid);   IJHA_FIFO_DS_H32I32_assert(ijha_fifo_ds_h32i32_dense_index(self, test_objects[packedi].verify_handle_a) == packedi);
            }
            for (packedi=self->num_handles; packedi < capacity; ++packedi) {
               IJHA_FIFO_DS_H32I32_assert(!test_objects[packedi].valid);
            }

         }
         IJHA_FIFO_DS_H32I32_assert(self->num_handles == 0);
      }
   }

#undef IJHA_unit_test_num_handles_to_acquire
}

#if defined (IJHA_FIFO_DS_H32I32_TEST_MAIN)

#if !defined (IJHA_FIFO_H32_TEST) || !defined(IJHA_FIFO_H32_IMPLEMENTATION)

   #if !defined(IJHA_FIFO_H32_IMPLEMENTATION)
      #define IJHA_FIFO_H32_IMPLEMENTATION
   #endif

   #if !defined(IJHA_FIFO_H32_TEST)
      #define IJHA_FIFO_H32_TEST
   #endif

   #include "ijha_fifo_h32.h"
#endif

#include <stdio.h>

int main(int args, char **argc)
{
   (void)args;
   (void)argc;
   printf("running tests for IJHA_FIFO_DS_H32I32.\n");
   ijha_fifo_h32_test();
   ijha_fifo_ds_h32i32_test();
   printf("tests for IJHA_FIFO_DS_H32I32. succeeded\n");
   return 0;
}
#endif /* IJHA_FIFO_DS_H32I32_TEST_MAIN */

#endif /* IJHA_FIFO_DS_H32I32_TEST || IJHA_FIFO_DS_H32I32_TEST_MAIN */

/*
LICENSE
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - 3-Clause BSD License
Copyright (c) 2017-2018, Fredrik Engkvist
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
