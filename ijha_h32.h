/* clang-format off */

/*
ijha_h32 : IncredibleJunior HandleAllocator 32-bit Handles - v1.0

In many situations it's desirable to refer to objects/resources by handles
instead of pointers. In addition to memory safety, like detecting double free's
and reference freed/reallocated memory, it allows the private implementation to
hide its implementation and reorganize data without changing the public API.
Andre Weissflog (@floooh) goes into this in great detail in
'Handles are the better pointers' [1].

ijha_h32 is a runtime configurable thread-safe FIFO/LIFO handle allocator with
handles that have a user configurable number of userflags bits and
variable number of generation bits. Memory usage is 4 bytes per handle.

ijha_h32 and it's many, often unpublished, predecessors was originally inspired
by Niklas Gray's (@niklasfrykholm) blogpost about packed arrays [2],
which is recommended reading.

The following properties for handles holds (all of Niklas' requirements in [2]):

 - 1-1 mapping between a valid object/resource and a handle
 - stale handles can be detected
 - lookup from handle to object/resource is fast (only a mask operation)
 - adding and removing handles should be fast
 - optional userflags per handle (not present in [2])

Handles are 32 bits and can have user configurable number of userflag bits
and variable number generation bits, which depends on number of requested bits
for userflags and the number of bits needed to represent the requested max number
of handles.

The generation part of the handle dictates how many times a handle can be reused
before giving a false positive 'is-valid' answer.

All valid handles are guaranteed to never be 0, which guarantees that the
'clear to zero is initialization' pattern works. In fact a valid handle is
guaranteed to never be [0, capacity mask], where capacity mask is the configured
max number of handles (rounded up to power of 2) minus 1.

Each time a handle is reused the generation part of the handle is increased,
provided >0 generation bits been reserved. How many times a handle can be reused,
before giving a false positive 'is-valid' answer, depends on how many free slots
there are (if a FIFO queue is used) and the number of generation bits.
Once a handle is acquired from the queue, it can be reused
2^(num generation bits)-1 times before returning a false positive.

The _optional_ userflags is stored before the most significant bit (MSB)
of the 32-bit handle by default.

MSB                                                                            LSB
+--------------------------------------------------------------------------------+
| in-use-bit | _optional_ userflags | generation | sparse-index or freelist next |
+--------------------------------------------------------------------------------+

If handle allocator is initialized with the flag 'IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT'
the handle layout changes to the following:

MSB                                                                            LSB
+--------------------------------------------------------------------------------+
| _optional_ userflags | generation | in-use-bit | sparse-index or freelist next |
+--------------------------------------------------------------------------------+

A newly initialized handle allocator starts allocating handles with sparse index
going from [0, max number of handles) (*)

Storing the in-use-bit in the MSB, coupled with the fact that newly allocated
handles starts at (sparse) index 0 (*) enables defining constants that is
independent of how many handles the handle allocator was initialized with, which
would be the case if the in-use-bit would be stored between the sparse index and
generation part of the handle.

(*) iff it's initialized with thread-safe-flag, then it starts at 1,
as 0 is used as end-of-list/sentinel node.

Please refer to the 'ijha_h32_test_constant_handles'-test at the end of the file
which goes into greater detail showing how to setup this and get back the sentinel
node that is 'lost' in the thread-safe version (with some caveats though).

The ijha_h32 is initialized with a memory area and information about the size of
the _optional_ userdata and offsets to handles. This enables both having handles
'external'/'non-inline' to the userdata, 'internal'/'inlined' in the userdata
and with _no_ userdata at all, as it is optional.

Inline in this context means that the handle is 'inlined'/'embedded' in the
userdata, for example:

   struct MyObject {
      unsigned ijha_h32_handle;
      float x, y, z;
   };

Which one, 'inline' or 'non-inline', to choose depends on situation,
and preconditions, and dictates how the resulting memory layout looks like.
Note that if using handles 'non-inline' the user must be wary of alignment
requirements of the userdata as the userdata is interleaved with handles.

H:  Handle
UD: UserData

No userdata:                        [H][H][H][...]
Userdata with 'non-inline' handles: [H][UD][H][UD][H][UD][...]
Userdata with 'inline' handles:     [UD][UD][UD][...]

Please refer to the 'ijha_h32_init_no_inlinehandles' and
'ijha_h32_init_inlinehandles' helper macros when initializing the handle allocator.

This file provides both the interface and the implementation.
The handle allocator is implemented as a stb-style header-file library[3]
which means that in *ONE* source file, put:

   #define IJHA_H32_IMPLEMENTATION
   // if custom assert wanted (and no dependencies on assert.h)
   #define IJHA_H32_assert   custom_assert
   // #define IJHA_H32_NO_THREADSAFE_SUPPORT // to disable the thread-safe versions
   #include "ijha_h32.h"

Other source files should just include ijha_h32.h

EXAMPLES/UNIT TESTS
   Usage examples+tests is at the bottom of the file in the IJHA_H32_TEST section.
LICENSE
   See end of file for license information

REVISIONS
   1.0 (2022-08-12) First version
                    'IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT' flag added
   1.1 (2024-02-29) Added 'ijha_h32_memory_size_allocated'
                    *Breaking Change*
                    Removed unused flags parameter from 'ijha_h32_memory_size_needed'.
                    When upgrading just remove 'ijha_flags' parameter from call (last parameter)

References:
   [1] https://floooh.github.io/2018/06/17/handles-vs-pointers.html
   [2] http://bitsquid.blogspot.se/2011/09/managing-decoupling-part-4-id-lookup.html
   [3] https://github.com/nothings/stb

*/

#ifndef IJHA_H32_INCLUDED_H
#define IJHA_H32_INCLUDED_H

#ifdef __cplusplus
   extern "C" {
#endif

#if defined(IJHA_H32_STATIC)
   #define IJHA_H32_API static
#else
   #define IJHA_H32_API extern
#endif

#define IJHA_H32_INVALID_INDEX ((unsigned)-1)

struct ijha_h32;

typedef unsigned ijha_h32_acquire_func(struct ijha_h32 *self, unsigned userflags, unsigned *handle_out);
typedef unsigned ijha_h32_release_func(struct ijha_h32 *self, unsigned handle);

struct ijha_h32 {
   void *handles;

   ijha_h32_acquire_func *acquire_func;
   ijha_h32_release_func *release_func;

   unsigned flags_num_userflag_bits;
   unsigned handles_stride_userdata_offset;

   unsigned size;
   unsigned capacity;

   unsigned capacity_mask;
   unsigned generation_mask;
   unsigned userflags_mask;

   unsigned in_use_bit;

   /* enqueue/add/put items at the back (+dequeue/remove/get items from the front _iff_ LIFO) */
   unsigned freelist_enqueue_index;
   /* dequeue/remove/get items from the front (FIFO) */
   unsigned freelist_dequeue_index;
};

/* max number of handles does _not_ have to be power of two.
 * NB: number of usable handles is only guaranteed to equal max number of handles
 *     if the handle allocator is 'pure LIFO' (i.e not thread-safe (*) or FIFO).
 *     add 1 to max number of handles in the event that this is really needed in
 *     those cases.
 *
 *     (*) the sentinel node in the thread-safe can be, with some caveats, used.
 *
 * NB: size of 'struct ijha_h32' is *NOT* included
 */
IJHA_H32_API unsigned ijha_h32_memory_size_needed(unsigned max_num_handles, unsigned userdata_size_in_bytes_per_item, int inline_handles);

/* returns the number of bytes allocated for instance, the inverse of 'ijha_h32_memory_size_needed'
 * NB: size of 'struct ijha_h32' is *NOT* included */
#define ijha_h32_memory_size_allocated(self) ((self)->capacity * ijha_h32_handle_stride((self)->handles_stride_userdata_offset))

enum ijha_h32_init_res {
   IJHA_H32_INIT_NO_ERROR = 0,
   IJHA_H32_INIT_CONFIGURATION_UNSUPPORTED = 1 << 0, /* the requested userflag bits + num bits needed to represent a handle could not fit into a 32 bit handle */
   IJHA_H32_INIT_THREADSAFE_UNSUPPORTED = 1 << 1,
   IJHA_H32_INIT_USERDATA_TOO_BIG = 1 << 2,
   IJHA_H32_INIT_HANDLE_OFFSET_TOO_BIG = 1 << 3, /* offset to handle is too big */
   IJHA_H32_INIT_HANDLE_NON_INLINE_SIZE_TOO_BIG = 1 << 4,
   IJHA_H32_INIT_INVALID_INPUT_FLAGS = 1 << 5
};

enum ijha_h32_init_flags {
   IJHA_H32_INIT_LIFO = 1 << 6,
   IJHA_H32_INIT_FIFO = 1 << 7,
   IJHA_H32_INIT_LIFOFIFO_MASK = 0xc0,

   /* NB: FIFO is unsupported in thread-safe version, just LIFO */
   IJHA_H32_INIT_THREADSAFE = 1 << 8,

   /* disable default behaviour of storing the "in use"-bit in MSB of handle
      and instead uses the bit after the bits used to represent the sparse index */
   IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT = 1 << 9
};

/* please refer to 'ijha_h32_init_no_inlinehandles' and 'ijha_h32_init_inlinehandles' helper macros */
IJHA_H32_API int ijha_h32_initex(struct ijha_h32 *self, unsigned max_num_handles, unsigned num_userflag_bits, unsigned non_inline_handle_size_bytes, unsigned handle_offset, unsigned userdata_size_in_bytes_per_item, unsigned ijha_flags, void *memory);

/* initialize without 'inlined' handles in _optional_ userdata, handles is
 * external to the _optional_ userdata.
 *
 * resulting in either:
 *
 *    H:  Handle
 *    UD: UserData
 *
 *    #1: [H][UD][H][UD][...] (with userdata) memory-layout
 *
 *    or
 *
 *    #2: [H][H][H][...] (without userdata) memory-layout
 *
 * if the ijha_h32-instance should just be used for allocating handles, and no
 * other interleaved data, then set the userdata_size_in_bytes_per_item to 0,
 * yielding the memory layout in #2
 *
 * NB: if using userdata be wary the alignment requirement of the userdata as it
 *     is interleaved with handles. alignment requirement greater than 4 can not
 *     be serviced and must be handled by user (structure modification or pragma pack)
 *
 * 'num_userflag_bits' is the _optional_ number of bits that will be reserved in a
 * handle for user storage, stored before the most significant bit of the 32-bit handle
 *
 * 'max_num_handles' does _not_ have to be power of two.
 *
 * NB: the max number of handles specified is not the same as usable handles
 *     post initialization as 1 has to be reserved for bookkeeping,
 *     in non-'pure LIFO' configuration.
 *
 * use 'ijha_h32_memory_size_needed' to calculate how much memory is needed.
 *
 * ijha_flags = ORed ijha_h32_init_flags
 *
 * NB: future operations on self is undefined if return value, which is a combination
 *     of ijha_h32_init_res ORed together, is not equal to IJHA_H32_INIT_NO_ERROR.
 */
#define ijha_h32_init_no_inlinehandles(self, max_num_handles, num_userflag_bits, userdata_size_in_bytes_per_item, ijha_flags, memory) ijha_h32_initex((self), (max_num_handles), (num_userflag_bits), sizeof(unsigned), 0, (userdata_size_in_bytes_per_item), (ijha_flags), (void*)(memory))

/* initialize with handles inlined in userdata (UD) resulting in a
 * [UD][UD][UD][...] memory-layout
 *
 * ex:
 * struct UserdataWithInlineHandle {
 *    void *p;
 *    int flags;
 *    unsigned inline_handle;
 * };
 *
 * ijha_h32_init_inlinehandles(self, max_num_handles, num_userflag_bits,
 *                             sizeof(struct UserdataWithInlineHandle),
 *                             offsetof(struct UserdataWithInlineHandle, inline_handle),
 *                             ijha_flags, memory)
 *
 * 'num_userflag_bits' is the _optional_ number of bits that will be reserved in a
 * handle for user storage, stored before the most significant bit of the 32-bit handle
 *
 * 'max_num_handles' does _not_ have to be power of two.
 *
 * NB: the memory that is used when initializing the handle allocator is the
 *     userdata/user defined structures hence the memory must be aligned according
 *     to the memory alignment requirements of the userdata.
 *
 * NB: the max number of handles specified is not the same as usable handles
 *     post initialization as 1 has to be reserved for bookkeeping,
 *     in non-'pure LIFO' configuration.
 *
 * use 'ijha_h32_memory_size_needed' to calculate how much memory is needed.
 *
 * ijha_flags = ORed ijha_h32_init_flags
 *
 * NB: future operations on self is undefined if return value, which is a combination
 *     of ijha_h32_init_res ORed together, is not equal to IJHA_H32_INIT_NO_ERROR.
 */
#define ijha_h32_init_inlinehandles(self, max_num_handles, num_userflag_bits, userdata_size_in_bytes_per_item, byte_offset_to_handle, ijha_flags, memory) ijha_h32_initex((self), (max_num_handles), (num_userflag_bits), 0, (byte_offset_to_handle), (userdata_size_in_bytes_per_item), (ijha_flags), (memory))

/* reset to initial state (as if no handles been used) */
IJHA_H32_API void ijha_h32_reset(struct ijha_h32 *self);

#define ijha_h32_is_fifo(self) (((self)->flags_num_userflag_bits&IJHA_H32_INIT_FIFO)==IJHA_H32_INIT_FIFO)

/* how many handles can be used */
#define ijha_h32_capacity(self) ((self)->capacity - (((self)->flags_num_userflag_bits&(IJHA_H32_INIT_FIFO|IJHA_H32_INIT_THREADSAFE)) ? 1 : 0))

/* acquires a handle, stored in handle_out.
 * returns the index of the handle on success, IJHA_H32_INVALID_INDEX when all
 * handles is used.
 *
 * if no userflags was reserved/requested on initialization, use 'ijha_h32_acquire'
 *
 * if userflags was reserved/requested on initialization, use 'ijha_h32_acquire_userflags'
 *
 * NB: if the handles is inlined in the userdata then extra care has to be taken
 *     when initializing the userdata after successfully acquired a handle, as
 *     the handle contains bookkeeping information
 *     (generation/freelist traversal information/etc).
 *
 *       ex:
 *       struct UserdataWithInlineHandle {
 *          unsigned inlined_handle;
 *          unsigned char payload[60];
 *        };
 *
 *        unsigned handle;
 *        unsigned sparse_index = ijha_h32_acquire(instance, &handle);
 *        struct UserdataWithInlineHandle *userdata = ijha_h32_userdata(struct UserdataWithInlineHandle*, instance, handle);
 *        // NB: extra care has to be taken in order not to overwrite the handle information
 *        memset(ijha_h32_pointer_add(void*, userdata, sizeof handle),
 *               0,
 *               sizeof *userdata-sizeof handle);
 *
 * NB: the userflags is stored before the most significant bit, so user may or
 *     may not need to shift the userflags. use the 'ijha_h32_userflags_from_handle'
 *     and 'ijha_h32_userflags_to_handle' helper macros to transform userflags
 *     back and forth. see notes by the 'ijha_h32_userflags_'-macros.
 *
 *     ex:
 *       enum Color {
 *          RED = 0, GREEN = 1, BLUE = 2, YELLOW = 3
 *       };
 *
 *       initialize the ijha_h32 instance with 2 userflag bits [0,3]
 *       unsigned num_userflag_bits = 2;
 *       unsigned original_userflags = (unsigned)YELLOW;
 *       unsigned handle_userflags = ijha_h32_userflags_to_handle_bits(original_userflags, num_userflag_bits);
 *       unsigned handle;
 *       unsigned sparse_index = ijha_h32_acquire_userflags(instance, handle_userflags, &handle);
 *       unsigned stored_userflags = ijha_h32_userflags(instance, handle);
 *       this now holds (given that the acquire succeeded) :
 *          original_userflags == ijha_h32_userflags_from_handle_bits(handle, num_userflag_bits)
 *          handle_userflags == stored_userflags
 */
#define ijha_h32_acquire_userflags(self, userflags, handle_out) ((self)->acquire_func)((self), (userflags), (handle_out))
#define ijha_h32_acquire(self, handle_out) ijha_h32_acquire_userflags((self), 0, (handle_out))

/* index of the handle (stable, i.e. will not move) */
#define ijha_h32_index(self, handle) ((self)->capacity_mask & (handle))

/* if index or handle is in use
 * NB: 'ijha_h32_in_use' checks the passed in handle, _NOT_ the stored handle */
#define ijha_h32_in_use_bit(self) ((self)->in_use_bit)
#define ijha_h32_in_use(self, handle) ((handle)&ijha_h32_in_use_bit((self)))
#define ijha_h32_in_use_index(self, index) ijha_h32_in_use((self), *ijha_h32_handle_info_at((self), (index)))

#define ijha_h32_in_use_msb(self) (ijha_h32_in_use_bit(self)&0x80000000)

#define ijha_h32_handle_stride(v) ((v)&0x0000ffffu)
#define ijha_h32_handle_offset(v) (((v)&0x00ff0000u) >> 16)
#define ijha_h32_userdata_offset(v) (((v)&0xff000000u) >> 24)

#define ijha_h32_pointer_add(type, p, num_bytes) ((type)((unsigned char *)(p) + (num_bytes)))

/* pointer to handle */
#define ijha_h32_handle_info_at(self, index) ijha_h32_pointer_add(unsigned *, (self)->handles, ijha_h32_handle_offset((self)->handles_stride_userdata_offset) + ijha_h32_handle_stride((self)->handles_stride_userdata_offset) * (index))

#define ijha_h32_valid_mask(self, handle, handlemask) (((self)->capacity > ((handle) & (self)->capacity_mask)) && ijha_h32_in_use((self), (handle)) && ((*ijha_h32_handle_info_at((self), ((handle) & (self)->capacity_mask)) & (handlemask)) == ((handle) & (handlemask))))
/* if handle is valid/active */
#define ijha_h32_valid(self, handle) ijha_h32_valid_mask((self), (handle), (0xffffffffu))

/* retrieve the stored userflags from handle or index (assumes the handle is valid,
 * use 'ijha_h32_valid' beforehand if unsure) */
#define ijha_h32_userflags(self, handle_or_index) *ijha_h32_handle_info_at((self), ijha_h32_index((self), (handle_or_index)))&(self->userflags_mask)
/* returns the old userflags */
IJHA_H32_API unsigned ijha_h32_userflags_set(struct ijha_h32 *self, unsigned handle, unsigned userflags);

/* helper macros for transforming the userflags stored in the handle back and forth.
 * more often than not the userflags stored in handles is 0 based, think enum-types /
 * constants starting from 0 / etc, which user can not, and shall not for the sake
 * of conforming to a handle allocator, change. the '_bits' versions is when you
 * know the number of bits at the call-site, which often is the case, for other
 * times the number of userflags-bits is stored in the instance */
#define ijha_h32_userflags_num_bits(self) ((self)->flags_num_userflag_bits&31)

#define ijha_h32_userflags_to_handle_bits(self, userflags, num_userflag_bits) (((unsigned)(userflags))<<((31+!ijha_h32_in_use_msb(self))-(num_userflag_bits)))
#define ijha_h32_userflags_to_handle(self, userflags) ijha_h32_userflags_to_handle_bits(self, userflags, ijha_h32_userflags_num_bits((self)))

#define ijha_h32_userflags_from_handle_bits(self, handle, num_userflag_bits) ((((handle)&(0xffffffff>>!!ijha_h32_in_use_msb(self)))>>((31+!ijha_h32_in_use_msb(self))-(num_userflag_bits))))
#define ijha_h32_userflags_from_handle(self, handle) ijha_h32_userflags_from_handle_bits(self, handle, ijha_h32_userflags_num_bits((self)))

/* retrieve pointer to userdata of handle (assumes instance was initialized with userdata)
 * NB: 'ijha_h32_userdata' assumes that the handle/index is valid
 *     'ijha_h32_userdata_checked' does a valid check beforehand, but assumes it is passed a handle */
#define ijha_h32_userdata(userdata_type, self, handle_or_index) ijha_h32_pointer_add(userdata_type, (self)->handles, ijha_h32_handle_stride((self)->handles_stride_userdata_offset) * (ijha_h32_index((self), (handle_or_index))) + ijha_h32_userdata_offset((self)->handles_stride_userdata_offset))
#define ijha_h32_userdata_checked(userdata_type, self, handle) (ijha_h32_valid(self, handle) ? ijha_h32_userdata(userdata_type, self, handle) : 0)

/* release the handle back to the pool thus making it invalid.
 * returns the index of the handle if the handle was valid, IJHA_H32_INVALID_INDEX if invalid. */
#define ijha_h32_release(self, handle) ((self)->release_func)((self), handle)

#ifdef __cplusplus
   }
#endif

#endif /* IJHA_H32_INCLUDED_H */

#if defined(IJHA_H32_IMPLEMENTATION) && !defined(IJHA_H32_IMPLEMENTATION_DEFINED)

#define IJHA_H32_IMPLEMENTATION_DEFINED (1)

#ifndef IJHA_H32_NO_THREADSAFE_SUPPORT
   #if _WIN32
      #ifdef __cplusplus
         #define IJHA_H32__EXTERNC_DECL_BEGIN extern "C" {
         #define IJHA_H32__EXTERNC_DECL_END }
      #else
         #define IJHA_H32__EXTERNC_DECL_BEGIN
         #define IJHA_H32__EXTERNC_DECL_END
      #endif

      IJHA_H32__EXTERNC_DECL_BEGIN
         long _InterlockedCompareExchange(long volatile *Destination, long Exchange, long Comparand);
      IJHA_H32__EXTERNC_DECL_END

      #pragma intrinsic(_InterlockedCompareExchange)
      #define IJHA_H32_InterlockedCompareExchange(ptr, exch, comp) _InterlockedCompareExchange((long volatile *)(ptr), (exch), (comp))
      #define IJHA_H32_CAS(ptr, new, old) ((old)==(unsigned)IJHA_H32_InterlockedCompareExchange((ptr), (new), (old)))

      IJHA_H32__EXTERNC_DECL_BEGIN
         long _InterlockedIncrement(long volatile *Addend);
         long _InterlockedDecrement(long volatile *Addend);
      IJHA_H32__EXTERNC_DECL_END

      #pragma intrinsic(_InterlockedIncrement)
      #pragma intrinsic(_InterlockedDecrement)
      /* returns the result of the operation */
      #define IJHA_H32_InterlockedIncrement(ptr) _InterlockedIncrement((long volatile*)(ptr))
      #define IJHA_H32_InterlockedDecrement(ptr) _InterlockedDecrement((long volatile*)(ptr))

      #define IJHA_H32_HAS_ATOMICS (1)
   #else
      #define IJHA_H32_CAS(ptr, new, old) __sync_bool_compare_and_swap((ptr), (old), (new))

      /* returns the result of the operation */
      #define IJHA_H32_InterlockedIncrement(ptr) __sync_add_and_fetch((ptr), 1)
      #define IJHA_H32_InterlockedDecrement(ptr) __sync_sub_and_fetch((ptr), 1)

      #define IJHA_H32_HAS_ATOMICS (1)
   #endif
#endif /* ifndef IJHA_H32_NO_THREADSAFE_SUPPORT */

#ifndef IJHA_H32_assert
   #include <assert.h>
   #define IJHA_H32_assert assert
#endif

/* handle the runtime-option where the "in use"-bit is stored.
 *    - "in use"-bit is stored in MSB     -> (capacity_mask+1) is the first generation bit
 *    or
 *    - "in use"-bit is (capacity_mask+1) -> (capacity_mask+1)<<1 is the first generation bit */
#define ijha_h32__generation_add(self) (((self)->flags_num_userflag_bits&IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT)?(((self)->capacity_mask+1)<<1):(((self)->capacity_mask+1)))

IJHA_H32_API unsigned ijha_h32_memory_size_needed(unsigned max_num_handles, unsigned userdata_size_in_bytes_per_item, int inline_handles)
{
   return max_num_handles * (sizeof(unsigned)*(inline_handles ? 0 : 1) + userdata_size_in_bytes_per_item);
}

static unsigned ijha_h32__acquire_userflags_lifo_fifo(struct ijha_h32 *self, unsigned userflags, unsigned *handle_out)
{
   unsigned current_cursor = self->freelist_dequeue_index;
   unsigned in_use_bit = ijha_h32_in_use_bit(self);
   unsigned userflags_mask = self->userflags_mask;
   unsigned maxnhandles = self->capacity - ijha_h32_is_fifo(self);
   IJHA_H32_assert((userflags_mask & userflags) == userflags);

   if (self->size == maxnhandles) {
      /* NOTE
       * if only used as a LIFO queue and one don't need/want to keep dense<->sparse mapping,
       * or the need of number acquired handles, then the 'size' bookkeeping could be skipped altogether.
       * a check 'if (*ijha_h32_handle_info_at(current_cursor)&in_use_bit)' will tell if all handles is used */
      *handle_out = 0;
      return IJHA_H32_INVALID_INDEX;
   } else {
      unsigned *handle = ijha_h32_handle_info_at(self, current_cursor);
      unsigned current_handle = *handle;
      unsigned generation_mask = self->generation_mask;
      unsigned generation_to_add = ijha_h32__generation_add(self);

      unsigned new_cursor = current_handle & self->capacity_mask;
      unsigned new_generation = generation_mask & (current_handle + generation_to_add);
      unsigned new_handle = userflags | new_generation | in_use_bit | current_cursor;

      IJHA_H32_assert(!generation_mask || (*handle & generation_mask) != new_generation); /* no generation or has changed generation */

      *handle = *handle_out = new_handle;

      self->freelist_dequeue_index = new_cursor;
      ++self->size;
      return current_cursor;
   }
}

#if IJHA_H32_HAS_ATOMICS

static unsigned ijha_h32__acquire_lifo_ts(struct ijha_h32 *self, unsigned userflags, unsigned *handle_out)
{
   unsigned *current_freelist_index_serial = &self->freelist_dequeue_index;
   unsigned generation_mask = self->generation_mask;
   unsigned capacity_mask = self->capacity_mask;
   unsigned freelist_serial_add = capacity_mask+1;
   unsigned in_use_bit = ijha_h32_in_use_bit(self);
   unsigned handle_generation_add = ijha_h32__generation_add(self);

   IJHA_H32_assert((self->userflags_mask & userflags) == userflags);

   for (;;) {
      unsigned next_freelist_index;
      unsigned new_freelist_index_serial;
      unsigned old_freelist_index_serial = *current_freelist_index_serial;
      unsigned current_index = old_freelist_index_serial&capacity_mask;
      unsigned *handle = ijha_h32_handle_info_at(self, current_index), current_handle = *handle;

      /* first slot is used as a sentinel/end-of-list */
      if (current_index == 0) {
         *handle_out = 0;
         return IJHA_H32_INVALID_INDEX;
      }

      next_freelist_index = current_handle&capacity_mask;

      new_freelist_index_serial = ((old_freelist_index_serial + freelist_serial_add)&~capacity_mask) | next_freelist_index;
      IJHA_H32_assert((old_freelist_index_serial&~capacity_mask) != (new_freelist_index_serial&~capacity_mask));

      if (IJHA_H32_CAS(current_freelist_index_serial, new_freelist_index_serial, old_freelist_index_serial)) {
         unsigned new_generation = generation_mask & (current_handle + handle_generation_add);
         unsigned new_handle = userflags | new_generation | in_use_bit | current_index;

         IJHA_H32_assert(!generation_mask || (current_handle & generation_mask) != new_generation); /* no generation or has changed generation */

         *handle = *handle_out = new_handle;
         IJHA_H32_InterlockedIncrement(&self->size);

         return current_index;
      }
   }
}

#endif /* IJHA_H32_HAS_ATOMICS */

static unsigned ijha_h32__release_fifo(struct ijha_h32 *self, unsigned handle)
{
   unsigned in_use_bit = ijha_h32_in_use_bit(self);
   unsigned idx = handle & self->capacity_mask;
   unsigned *stored_handle = ((self->capacity > idx) && (handle & in_use_bit)) ? ijha_h32_handle_info_at(self, idx) : 0;

   if (stored_handle && *stored_handle == handle) {
      /* clear in_use-bit of current */
      *stored_handle &= ~in_use_bit;

      stored_handle = ijha_h32_handle_info_at(self, self->freelist_enqueue_index);
      IJHA_H32_assert((*stored_handle & in_use_bit) == 0);
      *stored_handle = (*stored_handle & ~self->capacity_mask) | idx;

      self->freelist_enqueue_index = idx;
      --self->size;
      return idx;
   }

   return IJHA_H32_INVALID_INDEX;
}

static unsigned ijha_h32__release_lifo(struct ijha_h32 *self, unsigned handle)
{
   unsigned in_use_bit = ijha_h32_in_use_bit(self);
   unsigned idx = handle & self->capacity_mask;
   unsigned *stored_handle = ((self->capacity > idx) && (handle & in_use_bit)) ? ijha_h32_handle_info_at(self, idx) : 0;

   if (stored_handle && *stored_handle == handle) {
      unsigned current_cursor = self->freelist_dequeue_index;
      /* clear in_use-bit and store current (soon the be old) cursor */
      *stored_handle = ~in_use_bit & ((handle & ~self->capacity_mask) | current_cursor);
      self->freelist_dequeue_index = idx;

      --self->size;
      return idx;
   }

   return IJHA_H32_INVALID_INDEX;
}

#if IJHA_H32_HAS_ATOMICS

static unsigned ijha_h32__release_lifo_ts(struct ijha_h32 *self, unsigned handle)
{
   unsigned *current_freelist_index_serial = &self->freelist_dequeue_index;
   unsigned capacity_mask = self->capacity_mask;
   unsigned in_use_bit = ijha_h32_in_use_bit(self);
   unsigned idx = handle & capacity_mask;
   unsigned *stored_handle = ((self->capacity > idx) && (handle & in_use_bit)) ? ijha_h32_handle_info_at(self, idx) : 0;

   if (stored_handle && *stored_handle == handle) {
      unsigned freelist_serial_add = capacity_mask + 1;
      /* clear in_use_bit and index */
      handle &= ~(capacity_mask | in_use_bit);

      for (;;) {
         unsigned old_freelist_index_serial = *current_freelist_index_serial;
         /* increase serial and change the freelist index to that of the released handle */
         unsigned new_freelist_index_serial = ((old_freelist_index_serial + freelist_serial_add)&~capacity_mask) | idx;

         IJHA_H32_assert((old_freelist_index_serial&~capacity_mask) != (new_freelist_index_serial&~capacity_mask));

         /* store current freelist index at the place of the release handle */
         *stored_handle = handle | (old_freelist_index_serial&capacity_mask);

         /* try to redirect freelist to current index */
         if (IJHA_H32_CAS(current_freelist_index_serial, new_freelist_index_serial, old_freelist_index_serial))
            break;
      }

      IJHA_H32_InterlockedDecrement(&self->size);

      return idx;
   }

   return IJHA_H32_INVALID_INDEX;
}

#endif /* IJHA_H32_HAS_ATOMICS */

#define ijha_h32__roundup(x) (--(x), (x) |= (x) >> 1, (x) |= (x) >> 2, (x) |= (x) >> 4, (x) |= (x) >> 8, (x) |= (x) >> 16, ++(x))

static unsigned ijha_h32__num_bits(unsigned n)
{
   unsigned res = 0;
   while (n >>= 1)
      res++;
   return res;
}

IJHA_H32_API int ijha_h32_initex(struct ijha_h32 *self, unsigned max_num_handles, unsigned num_userflag_bits, unsigned non_inline_handle_size_bytes, unsigned handle_offset, unsigned userdata_size_in_bytes_per_item, unsigned ijha_flags, void *memory)
{
   int init_res = IJHA_H32_INIT_NO_ERROR;
   unsigned handles_stride;
   unsigned userflags_mask;

   if ((userdata_size_in_bytes_per_item & 0xffff0000) != 0)
      init_res |= IJHA_H32_INIT_USERDATA_TOO_BIG;
   if ((non_inline_handle_size_bytes & 0xffffff00) != 0)
      init_res |= IJHA_H32_INIT_HANDLE_NON_INLINE_SIZE_TOO_BIG;
   if ((handle_offset & 0xffffff00) != 0)
      init_res |= IJHA_H32_INIT_HANDLE_OFFSET_TOO_BIG;

   self->handles = memory;
   self->flags_num_userflag_bits = ijha_flags;
   if ((self->flags_num_userflag_bits & 31) != 0)
      init_res |= IJHA_H32_INIT_INVALID_INPUT_FLAGS; /* erroneous flags passed in */

   self->flags_num_userflag_bits |= num_userflag_bits;

   handles_stride = non_inline_handle_size_bytes + userdata_size_in_bytes_per_item;
   self->handles_stride_userdata_offset = handles_stride | (non_inline_handle_size_bytes << 24) | (handle_offset << 16);

   self->size = 0;
   self->capacity = max_num_handles;
   ijha_h32__roundup(max_num_handles);
   self->capacity_mask = max_num_handles - 1;

   userflags_mask = num_userflag_bits ? (0xffffffffu << (32 - num_userflag_bits)) : 0;

   self->generation_mask = ~(self->capacity_mask | userflags_mask);

  if ((ijha_flags&IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT) == 0) {
      self->in_use_bit = 0x80000000;
      self->generation_mask = (self->generation_mask >> 1) & ~self->capacity_mask; /* in_use-bit is the MSB */

      self->userflags_mask = userflags_mask >> 1;
   } else {
      self->in_use_bit = self->capacity_mask+1;

      self->generation_mask &= self->generation_mask << 1; /* mask out the in_use-bit */

      self->userflags_mask = userflags_mask;
   }

   if ((ijha_h32__num_bits(max_num_handles) - 1) + num_userflag_bits >= 32)
      init_res |= IJHA_H32_INIT_CONFIGURATION_UNSUPPORTED;

   if (ijha_flags&IJHA_H32_INIT_THREADSAFE) {
      #if IJHA_H32_HAS_ATOMICS
         if ((ijha_flags&IJHA_H32_INIT_LIFOFIFO_MASK) == IJHA_H32_INIT_FIFO) {
            init_res |= IJHA_H32_INIT_THREADSAFE_UNSUPPORTED;
            self->acquire_func = 0;
            self->release_func = 0;
         } else {
            self->acquire_func = &ijha_h32__acquire_lifo_ts;
            self->release_func = &ijha_h32__release_lifo_ts;
            self->flags_num_userflag_bits |= IJHA_H32_INIT_LIFO;
         }
      #else
         init_res |= IJHA_H32_INIT_THREADSAFE_UNSUPPORTED;
         self->acquire_func = 0;
         self->release_func = 0;
      #endif
   } else {
      if ((ijha_flags&IJHA_H32_INIT_LIFOFIFO_MASK) == IJHA_H32_INIT_LIFO) {
         self->acquire_func = &ijha_h32__acquire_userflags_lifo_fifo;
         self->release_func = &ijha_h32__release_lifo;
      } else {
         self->acquire_func = &ijha_h32__acquire_userflags_lifo_fifo;
         self->release_func = &ijha_h32__release_fifo;
      }
   }

   if (init_res == IJHA_H32_INIT_NO_ERROR)
      ijha_h32_reset(self);

   return init_res;
}

IJHA_H32_API void ijha_h32_reset(struct ijha_h32 *self)
{
   /* always reset handles with full generation mask as then the first
    * allocation/acquire makes it wrap-around. this guarantees that the
    * handles allocated, barring any releases and handle allocator is not
    * initialized with 'IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT'-flag, becomes:
    *
    * (0x80000000 | 0) -> (0x80000000 | 1) -> (0x80000000 | 2) -> etc
    *
    * this can be useful if you know that there is always allocated N
    * objects/handles at the start and want to guarantee that the handles to
    * these objects always is the same, regardless of the capacity the handle
    * allocator is initialized with.
    *
    * NB : this is just guaranteed for the non-thread safe version, as the
    *      thread-safe version starts with sparse index 1. this can be worked
    *      around but the user has to jump through a few hoops in order to achieve
    *      it.
    */
   unsigned i, generation_mask = self->generation_mask;
   self->size = 0;

   self->freelist_dequeue_index = 0;
   self->freelist_enqueue_index = self->capacity - 1;

   for (i = 0; i != self->capacity; ++i) {
      unsigned *current = ijha_h32_handle_info_at(self, i);
      *current = (i + 1) | generation_mask;
   }

   /* make last handle/slot loop back to 0 */
   *ijha_h32_handle_info_at(self, self->capacity - 1) = 0 | generation_mask;

   if (self->flags_num_userflag_bits & IJHA_H32_INIT_THREADSAFE)
      self->freelist_dequeue_index = 1; /* use the first slot as a sentinel/end-of-list */
}

IJHA_H32_API unsigned ijha_h32_userflags_set(struct ijha_h32 *self, unsigned handle, unsigned userflags)
{
   unsigned ohandle, *p;
   IJHA_H32_assert(((userflags) & (self)->userflags_mask) == (userflags));
   IJHA_H32_assert(ijha_h32_valid_mask(self, handle, ~self->userflags_mask));
   p = ijha_h32_handle_info_at(self, handle & self->capacity_mask), ohandle = *p;
   *p = (ohandle & ~self->userflags_mask) | userflags;

   return ohandle & self->userflags_mask;
}

#if defined(IJHA_H32_TEST) || defined(IJHA_H32_TEST_MAIN)

#ifndef IJHA_H32_memset
   #include <string.h>
   #define IJHA_H32_memset memset
#endif

struct ijha_h32_test_userdata {
   void *p;
   unsigned a, b, c;
   unsigned inline_handle;
};

#ifndef offsetof
   typedef unsigned int ijha_h32_uint32;

   #ifdef _MSC_VER
      typedef unsigned __int64 ijha_h32_uint64;
   #else
      typedef unsigned long long ijha_h32_uint64;
   #endif

   #if defined(__ppc64__) || defined(__aarch64__) || defined(_M_X64) || defined(__x86_64__) || defined(__x86_64)
      typedef ijha_h32_uint64 ijha_h32_uintptr;
   #else
      typedef ijha_h32_uint32 ijha_h32_uintptr;
   #endif

   #define ijha_h32_test_offsetof(st, m) ((ijha_h32_uintptr)&(((st *)0)->m))
#else
   #define ijha_h32_test_offsetof offsetof
#endif

static void ijha_h32_test_inline_noinline_handles(void)
{
#define IJHA_TEST_MAX_NUM_HANDLES 5
#if defined(IJHA_H32_HAS_ATOMICS)
   unsigned LIFO_FIFO_FLAGS[] = {IJHA_H32_INIT_LIFO, IJHA_H32_INIT_FIFO, IJHA_H32_INIT_THREADSAFE | IJHA_H32_INIT_LIFO};
#else
   unsigned LIFO_FIFO_FLAGS[] = {IJHA_H32_INIT_LIFO, IJHA_H32_INIT_FIFO};
#endif
   struct ijha_h32 l, *self = &l;
   unsigned idx, num = sizeof LIFO_FIFO_FLAGS / sizeof *LIFO_FIFO_FLAGS;
   unsigned i, j, maxnhandles, dummy, handles[IJHA_TEST_MAX_NUM_HANDLES];
   int init_res;

   for (idx = 0; idx != num*2; ++idx) {
      unsigned LIFO_FIFO_FLAG = LIFO_FIFO_FLAGS[idx%num];

      unsigned num_userflag_bits = 0;
      unsigned userdata_size_in_bytes_per_item = sizeof(struct ijha_h32_test_userdata);
      struct ijha_h32_test_userdata userdata_inlinehandles[IJHA_TEST_MAX_NUM_HANDLES];

      if (idx >= num)
         LIFO_FIFO_FLAG |= IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT;

      IJHA_H32_assert(sizeof userdata_inlinehandles >= ijha_h32_memory_size_needed(IJHA_TEST_MAX_NUM_HANDLES, userdata_size_in_bytes_per_item, 1));
      init_res = ijha_h32_init_inlinehandles(self, IJHA_TEST_MAX_NUM_HANDLES, num_userflag_bits, sizeof(struct ijha_h32_test_userdata), ijha_h32_test_offsetof(struct ijha_h32_test_userdata, inline_handle), LIFO_FIFO_FLAG, userdata_inlinehandles);
      IJHA_H32_assert(init_res == IJHA_H32_INIT_NO_ERROR);
      IJHA_H32_assert(ijha_h32_memory_size_allocated(self) == ijha_h32_memory_size_needed(IJHA_TEST_MAX_NUM_HANDLES, userdata_size_in_bytes_per_item, 1));
      maxnhandles = ijha_h32_capacity(self);

      for (i = 0; i != maxnhandles; ++i) {
         unsigned si = ijha_h32_acquire_userflags(self, 0, &handles[i]);
         for (j = 0; j != i + 1; ++j)
            IJHA_H32_assert(ijha_h32_valid(self, handles[j]));
         IJHA_H32_assert(si != IJHA_H32_INVALID_INDEX);
      }
      IJHA_H32_assert(ijha_h32_acquire_userflags(self, 0, &dummy) == IJHA_H32_INVALID_INDEX);
      for (i = 0; i != maxnhandles; ++i) {
         unsigned handleidx = ijha_h32_index(self, handles[i]);
         struct ijha_h32_test_userdata *userdata = ijha_h32_userdata(struct ijha_h32_test_userdata*, self, handleidx);
         unsigned *handleinfo = ijha_h32_handle_info_at(self, handleidx);
         IJHA_H32_assert(userdata == &userdata_inlinehandles[handleidx]);
         IJHA_H32_assert(handleinfo == &userdata_inlinehandles[handleidx].inline_handle);
         IJHA_H32_assert(userdata == ijha_h32_userdata_checked(struct ijha_h32_test_userdata*, self, handles[i]));
      }
   }

   for (idx = 0; idx != num*2; ++idx) {
      unsigned LIFO_FIFO_FLAG = LIFO_FIFO_FLAGS[idx%num];

      unsigned num_userflag_bits = 0;
      unsigned userdata_size_in_bytes_per_item = 0;
      unsigned all_handles_memory[IJHA_TEST_MAX_NUM_HANDLES];

      if (idx >= num)
         LIFO_FIFO_FLAG |= IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT;

      IJHA_H32_assert(sizeof all_handles_memory >= ijha_h32_memory_size_needed(IJHA_TEST_MAX_NUM_HANDLES, userdata_size_in_bytes_per_item, 0));
      init_res = ijha_h32_init_no_inlinehandles(self, IJHA_TEST_MAX_NUM_HANDLES, num_userflag_bits, userdata_size_in_bytes_per_item, LIFO_FIFO_FLAG, all_handles_memory);
      IJHA_H32_assert(init_res == IJHA_H32_INIT_NO_ERROR);
      IJHA_H32_assert(ijha_h32_memory_size_allocated(self) == ijha_h32_memory_size_needed(IJHA_TEST_MAX_NUM_HANDLES, userdata_size_in_bytes_per_item, 0));
      maxnhandles = ijha_h32_capacity(self);

      for (i = 0; i != maxnhandles; ++i) {
         unsigned si = ijha_h32_acquire_userflags(self, 0, &handles[i]);
         for (j = 0; j != i + 1; ++j)
            IJHA_H32_assert(ijha_h32_valid(self, handles[j]));
         IJHA_H32_assert(si != IJHA_H32_INVALID_INDEX);
      }
      IJHA_H32_assert(ijha_h32_acquire_userflags(self, 0, &dummy) == IJHA_H32_INVALID_INDEX);
      for (i = 0; i != maxnhandles; ++i) {
         unsigned handleidx = ijha_h32_index(self, handles[i]);
         IJHA_H32_assert(ijha_h32_handle_info_at(self, handleidx) == &all_handles_memory[handleidx]);
      }
   }

   for (idx = 0; idx != num*2; ++idx) {
      unsigned LIFO_FIFO_FLAG = LIFO_FIFO_FLAGS[idx%num];
      unsigned num_userflag_bits = 0;
      unsigned userdata_size_in_bytes_per_item = sizeof(struct ijha_h32_test_userdata);
      unsigned stride = sizeof(struct ijha_h32_test_userdata) + sizeof(unsigned);
      unsigned char memory_for_noinline_handles[(sizeof(struct ijha_h32_test_userdata) + sizeof(unsigned))*IJHA_TEST_MAX_NUM_HANDLES];

      if (idx >= num)
         LIFO_FIFO_FLAG |= IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT;

      IJHA_H32_assert(sizeof memory_for_noinline_handles >= ijha_h32_memory_size_needed(IJHA_TEST_MAX_NUM_HANDLES, userdata_size_in_bytes_per_item, 0));
      init_res = ijha_h32_init_no_inlinehandles(self, IJHA_TEST_MAX_NUM_HANDLES, num_userflag_bits, userdata_size_in_bytes_per_item, LIFO_FIFO_FLAG, memory_for_noinline_handles);
      IJHA_H32_assert(init_res == IJHA_H32_INIT_NO_ERROR);
      IJHA_H32_assert(ijha_h32_memory_size_allocated(self) == ijha_h32_memory_size_needed(IJHA_TEST_MAX_NUM_HANDLES, userdata_size_in_bytes_per_item, 0));
      maxnhandles = ijha_h32_capacity(self);

      for (i = 0; i != maxnhandles; ++i) {
         unsigned si = ijha_h32_acquire_userflags(self, 0, &handles[i]);
         for (j = 0; j != i + 1; ++j)
            IJHA_H32_assert(ijha_h32_valid(self, handles[j]));
         IJHA_H32_assert(si != IJHA_H32_INVALID_INDEX);
      }

      IJHA_H32_assert(ijha_h32_acquire_userflags(self, 0, &dummy) == IJHA_H32_INVALID_INDEX);
      for (i = 0; i != maxnhandles; ++i) {
         unsigned handleidx = ijha_h32_index(self, handles[i]);
         unsigned *handleinfo = ijha_h32_handle_info_at(self, handleidx);
         IJHA_H32_assert(ijha_h32_pointer_add(char *, memory_for_noinline_handles, stride*handleidx) == (char*)handleinfo);
      }
   }
#undef IJHA_TEST_MAX_NUM_HANDLES
}

enum IJHA_H32_TestColor {
   IJHA_H32_TestColor_RED=0, IJHA_H32_TestColor_GREEN=1, IJHA_H32_TestColor_BLUE=2, IJHA_H32_TestColor_YELLOW=3
};

static void ijha_h32_test_basic_operations(void)
{
#define IJHA_TEST_MAX_NUM_HANDLES 5
#if defined(IJHA_H32_HAS_ATOMICS)
   unsigned LIFO_FIFO_FLAGS[] = {IJHA_H32_INIT_LIFO, IJHA_H32_INIT_FIFO, IJHA_H32_INIT_THREADSAFE|IJHA_H32_INIT_LIFO};
#else
   unsigned LIFO_FIFO_FLAGS[] = {IJHA_H32_INIT_LIFO, IJHA_H32_INIT_FIFO};
#endif
   unsigned ijha_h32_memory_area[IJHA_TEST_MAX_NUM_HANDLES];
   unsigned handles[IJHA_TEST_MAX_NUM_HANDLES];
   struct ijha_h32 l, *self = &l;
   unsigned idx, num = sizeof LIFO_FIFO_FLAGS / sizeof *LIFO_FIFO_FLAGS;
   int init_res;

   for (idx = 0; idx != num*2; ++idx) {
      unsigned LIFO_FIFO_FLAG = LIFO_FIFO_FLAGS[idx%num];
      unsigned i, j, user_nbits;

      if (idx >= num)
         LIFO_FIFO_FLAG |= IJHA_H32_INIT_DONT_USE_MSB_AS_IN_USE_BIT;

      for (user_nbits = 0; user_nbits != 29; ++user_nbits) {
         unsigned maxnhandles;
         unsigned dummy;
         unsigned userflags_test;
         unsigned userdata_size_in_bytes_per_item = 0;
         IJHA_H32_memset(ijha_h32_memory_area, 0, sizeof ijha_h32_memory_area);
         IJHA_H32_memset(handles, 0, sizeof handles);

         IJHA_H32_assert(sizeof ijha_h32_memory_area >= ijha_h32_memory_size_needed(IJHA_TEST_MAX_NUM_HANDLES, userdata_size_in_bytes_per_item, 0));
         init_res = ijha_h32_init_no_inlinehandles(self, IJHA_TEST_MAX_NUM_HANDLES, user_nbits, userdata_size_in_bytes_per_item, LIFO_FIFO_FLAG, ijha_h32_memory_area);
         IJHA_H32_assert(init_res == IJHA_H32_INIT_NO_ERROR);
         IJHA_H32_assert(ijha_h32_memory_size_allocated(self) == ijha_h32_memory_size_needed(IJHA_TEST_MAX_NUM_HANDLES, userdata_size_in_bytes_per_item, 0));
         maxnhandles = ijha_h32_capacity(self);

         for (i = 0; i != maxnhandles; ++i) {
            unsigned si, userflags = 0;
            enum IJHA_H32_TestColor testcolor = IJHA_H32_TestColor_RED;
            if (user_nbits > 1) {
               testcolor = (enum IJHA_H32_TestColor)(i%4);
               userflags = ijha_h32_userflags_to_handle(self, testcolor);
               userflags_test = ijha_h32_userflags_to_handle_bits(self, testcolor, user_nbits);
               IJHA_H32_assert(userflags == userflags_test);
               userflags_test = ijha_h32_userflags_from_handle(self, userflags);
               IJHA_H32_assert((unsigned)testcolor == userflags_test);
            }
            si = ijha_h32_acquire_userflags(self, userflags, &handles[i]);
            IJHA_H32_assert(ijha_h32_in_use(self, handles[i]));
            IJHA_H32_assert(ijha_h32_in_use_index(self, si));
            for (j = 0; j != i + 1; ++j)
               IJHA_H32_assert(ijha_h32_valid(self, handles[j]));

            if (user_nbits > 1) {
               unsigned stored_userflags = ijha_h32_userflags(self, handles[i]);
               unsigned userflags_from_handle = ijha_h32_userflags_from_handle(self, handles[i]);

               IJHA_H32_assert(stored_userflags == userflags);
               IJHA_H32_assert(userflags_from_handle == (unsigned)testcolor);
               IJHA_H32_assert(ijha_h32_userflags_from_handle_bits(self, stored_userflags, user_nbits) == (unsigned)testcolor);
               IJHA_H32_assert(ijha_h32_userflags_set(self, handles[i], stored_userflags) == userflags);
               IJHA_H32_assert(ijha_h32_userflags_set(self, handles[i], stored_userflags) == userflags);
            } else {
               /* thread-safe LIFO starts the idx at 1, as the 0 is used as end-of-list/sentinel */
               unsigned idx_add = (LIFO_FIFO_FLAG&IJHA_H32_INIT_THREADSAFE) ? 1 : 0;
               IJHA_H32_assert(handles[i] == (ijha_h32_in_use_bit(self) | (i + idx_add)));
            }
            IJHA_H32_assert(si != IJHA_H32_INVALID_INDEX);
         }
         IJHA_H32_assert(ijha_h32_acquire_userflags(self, 0, &dummy) == IJHA_H32_INVALID_INDEX);

         for (userflags_test = 1; userflags_test < user_nbits; ++userflags_test) {
            for (i = 0; i != maxnhandles; ++i) {
               unsigned ohandle = handles[i];

               unsigned userflag = 1 << (32 - user_nbits + userflags_test - 1);
               unsigned old_userflags = ijha_h32_userflags_set(self, ohandle, userflag);
               IJHA_H32_assert(userflags_test == 1 || old_userflags == (1u << (32 - user_nbits + userflags_test - 1 - 1)));

               handles[i] = (ohandle & ~self->userflags_mask) | userflag;
            }
         }

         for (i = 0; i != maxnhandles; ++i) {
            unsigned si = ijha_h32_release(self, handles[i]);
            for (j = 0; j != i + 1; ++j)
               IJHA_H32_assert(!ijha_h32_valid(self, handles[j]));
            for (j = i + 1; j < maxnhandles; ++j)
               IJHA_H32_assert(ijha_h32_valid(self, handles[j]));
            IJHA_H32_assert(si != IJHA_H32_INVALID_INDEX);
         }

         for (i = 0; i != maxnhandles; ++i) {
            unsigned si = ijha_h32_acquire_userflags(self, 0, &handles[i]);
            for (j = 0; j != i + 1; ++j)
               IJHA_H32_assert(ijha_h32_valid(self, handles[j]));
            IJHA_H32_assert(si != IJHA_H32_INVALID_INDEX);
         }
         IJHA_H32_assert(ijha_h32_acquire_userflags(self, 0, &dummy) == IJHA_H32_INVALID_INDEX);

         for (i = 0; i != maxnhandles; ++i) {
            unsigned si = ijha_h32_release(self, handles[i]);
            for (j = 0; j != i + 1; ++j)
               IJHA_H32_assert(!ijha_h32_valid(self, handles[j]));
            for (j = i + 1; j < maxnhandles; ++j)
               IJHA_H32_assert(ijha_h32_valid(self, handles[j]));
            IJHA_H32_assert(si != IJHA_H32_INVALID_INDEX);
         }

         for (i = 0; i != maxnhandles; ++i) {
            unsigned sia, sir;
            IJHA_H32_assert(!ijha_h32_valid(self, handles[0]));
            sia = ijha_h32_acquire_userflags(self, 0, &handles[0]);
            IJHA_H32_assert(ijha_h32_valid(self, handles[0]));
            sir = ijha_h32_release(self, handles[0]);
            IJHA_H32_assert(!ijha_h32_valid(self, handles[0]));
            IJHA_H32_assert(sir == sia);
         }
         IJHA_H32_assert((IJHA_H32_INIT_THREADSAFE&LIFO_FIFO_FLAG)== 0 || (self->size == 0));
      }
   }
#undef IJHA_TEST_MAX_NUM_HANDLES
}

static void ijha_h32_test_constant_handles(void)
{
#define IJHA_TEST_MAX_NUM_HANDLES (9)
#if defined(IJHA_H32_HAS_ATOMICS)
   unsigned LIFO_FIFO_FLAGS[] = { IJHA_H32_INIT_LIFO, IJHA_H32_INIT_FIFO, IJHA_H32_INIT_THREADSAFE | IJHA_H32_INIT_LIFO };
#else
   unsigned LIFO_FIFO_FLAGS[] = { IJHA_H32_INIT_LIFO, IJHA_H32_INIT_FIFO };
#endif

   /* some public API constant to to refer to resources that is always created/valid
    * if using userflags then these have to be present here also (no userflags in
    * this example)
    *
    * NB: that the constants have the in_use-bit set (ijha_h32_in_use_bit / 0x80000000)
    *     so they will pass the 'ijha_h32_valid' checks when used
    */
   #define PUBLIC_API_MAIN_WINDOW_HANDLE        (0x80000000)
   #define PUBLIC_API_SECONDARY_WINDOW_HANDLE   (0x80000001)

   struct ijha_h32 l, *self = &l;
   unsigned cap, idx, num = sizeof LIFO_FIFO_FLAGS / sizeof *LIFO_FIFO_FLAGS;
   unsigned i, n, j, maxnhandles, dummy, handles[IJHA_TEST_MAX_NUM_HANDLES];
   struct ijha_h32_test_userdata userdata_inlinehandles[IJHA_TEST_MAX_NUM_HANDLES];
   int init_res;

   /* increase the capacity to verify that the first (two) handles we defined
    * in our public API will not change when we increase the capacity */

   for (cap = 3; cap < IJHA_TEST_MAX_NUM_HANDLES; ++cap) {
      for (idx = 0; idx != num; ++idx) {
         /* first do initial setup of the handle allocator */
         unsigned LIFO_FIFO_FLAG = LIFO_FIFO_FLAGS[idx];
         unsigned num_userflag_bits = 0;
         unsigned ijha_flags = LIFO_FIFO_FLAG;
         unsigned userdata_size_in_bytes_per_item = sizeof(struct ijha_h32_test_userdata);
         IJHA_H32_assert(sizeof userdata_inlinehandles >= ijha_h32_memory_size_needed(cap, userdata_size_in_bytes_per_item, 1));
         init_res = ijha_h32_init_inlinehandles(self, cap, num_userflag_bits, sizeof(struct ijha_h32_test_userdata), ijha_h32_test_offsetof(struct ijha_h32_test_userdata, inline_handle), ijha_flags, userdata_inlinehandles);
         IJHA_H32_assert(init_res == IJHA_H32_INIT_NO_ERROR);
         /* setup finished */

         if (ijha_flags&IJHA_H32_INIT_THREADSAFE) {
            /* as this is the setup phase we have most likely not finished setting
             * up all other resources. we can therefore take for granted that the
             * handle allocator is not accessed concurrently at this point. which is
             * great because we can get "back" our resource at index 0, which is
             * used as a sentinel node.
             *
             * NB: if (ab-)using it like this it is of utmost importance that this
             *     handle _IS NOT_ released back into the pool at any time. it
             *     should only be used for resources/data that have the same
             *     lifetime as the handle allocator itself.
             */
            unsigned *handleinfo = ijha_h32_handle_info_at(self, 0);
            struct ijha_h32_test_userdata *userdata = ijha_h32_userdata(struct ijha_h32_test_userdata*, self, 0);
            /* as it is a freelist it points to next node */
            IJHA_H32_assert(ijha_h32_index(self, *handleinfo) == 1);
            IJHA_H32_assert(ijha_h32_in_use(self, *handleinfo) == 0);
            *handleinfo = PUBLIC_API_MAIN_WINDOW_HANDLE;
            self->size++;
            handles[0] = *handleinfo;
            /* here you would initialize the userdata */
            i = sizeof userdata; /* squash warnings of unused variable */
         }
         maxnhandles = ijha_h32_capacity(self);
         IJHA_H32_assert(maxnhandles >= 2);
         if (ijha_flags&IJHA_H32_INIT_THREADSAFE)
            i = 1, n = maxnhandles + 1;
         else
            i = 0, n = maxnhandles;

         for (; i != n; ++i) {
            unsigned si = ijha_h32_acquire_userflags(self, 0, &handles[i]);
            /* here you would initialize the userdata */
            for (j = 0; j != i + 1; ++j)
               IJHA_H32_assert(ijha_h32_valid(self, handles[j]));
            IJHA_H32_assert(si != IJHA_H32_INVALID_INDEX);
         }
         IJHA_H32_assert(ijha_h32_acquire_userflags(self, 0, &dummy) == IJHA_H32_INVALID_INDEX);
         IJHA_H32_assert(handles[0] == PUBLIC_API_MAIN_WINDOW_HANDLE);
         IJHA_H32_assert(handles[1] == PUBLIC_API_SECONDARY_WINDOW_HANDLE);

         if (ijha_flags&IJHA_H32_INIT_THREADSAFE)
            n = maxnhandles + 1; /* as we (ab-)use the fact that we can 'steal' the node at zero */
         else
            n = maxnhandles;

         IJHA_H32_assert(n == self->size);
         for (i = 0; i != n; ++i) {
            unsigned handleidx = ijha_h32_index(self, handles[i]);
            struct ijha_h32_test_userdata *userdata = ijha_h32_userdata(struct ijha_h32_test_userdata*, self, handleidx);
            unsigned *handleinfo = ijha_h32_handle_info_at(self, handleidx);
            IJHA_H32_assert(userdata == &userdata_inlinehandles[handleidx]);
            IJHA_H32_assert(handleinfo == &userdata_inlinehandles[handleidx].inline_handle);
            IJHA_H32_assert(ijha_h32_valid(self, handles[i]));
         }
      }
   }
#undef IJHA_TEST_MAX_NUM_HANDLES
#undef PUBLIC_API_MAIN_WINDOW_HANDLE
#undef PUBLIC_API_SECONDARY_WINDOW_HANDLE
}

static void ijha_h32_test_suite(void)
{
   ijha_h32_test_basic_operations();
   ijha_h32_test_inline_noinline_handles();
   ijha_h32_test_constant_handles();
}

#if defined(IJHA_H32_TEST_MAIN)

#include <stdio.h>

int main(int args, char **argc)
{
   (void)args;
   (void)argc;
   ijha_h32_test_suite();
   printf("ijha_h32: all tests done.\n");
   return 0;
}
#endif

#endif /* defined(IJHA_H32_TEST) || defined(IJHA_H32_TEST_MAIN) */
#endif /* defined(IJHA_H32_IMPLEMENTATION) */

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
