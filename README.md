# Collection of handle/id allocators

In many situations it's desirable to refer to objects/resources by handles instead of pointers. In addition to memory safety, like detecting double free's and reference freed/reallocated memory, it allows the private implementation to hide its implementation and reorganize data without changing the public API. [Andre Weissflog](https://github.com/floooh) goes into this in great detail in [Handles are the better pointers](https://floooh.github.io/2018/06/17/handles-vs-pointers.html).

### Pros
- detect stale handles ('use after free').
- sizeof(pointer) > sizeof(handle_type) (on 64bit architectures with 32 bit handles).
- system is free to arrange memory of resource refered by handle (ex. keep data linear in memory).
- more userflag bits available (if needed). pointers would only have a bottom bits (due to alignment) and with extra care added to mask out those bits before usage.

### Cons
- one (atleast) extra indirection when using the resource externally (_but_ in most cases more resources are touched internally than are referenced externally).
- trickier to debug/inspect individual resources.

## Allocators

All allocators are implemented as a [stb-style header-file library](https://github.com/nothings/stb) and comes with unittest and usage examples.

- [ijha_h32.h](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_h32.h) is a runtime configurable thread-safe FIFO/LIFO handle allocator with handles that have a user configurable number of userflags bits and variable number of generation bits. Memory usage: 4bytes / handle.

- [ijss.h](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijss.h) sparse set for bookkeeping of dense<->sparse index mapping or a building-block for a simple LIFO index/handle allocator.

*Legacy allocators*, as [ijha_fifo_h32](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_fifo_h32.h) has been superseded by [ijha_h32](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_h32.h), optionally used in combination with [ijss](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijss.h) to keep the dense<->sparse mappings.

- [ijha_fifo_h32.h](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_fifo_h32.h) is a FIFO handle allocator with handles that have a user configurable number of userdata/flags bits and variable number of generation bits. Memory usage: 8bytes / handle.

- [ijha_fifo_ds_h32i32.h](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_fifo_ds_h32i32.h) is a FIFO handle allocator built on top of ijha_fifo_h32 with added bookkeeping of handle's sparse/dense relationships to be able to keep all (used) data linear in memory while retaining stable handles. Memory usage: 16bytes / handle.

- [ijha_fifo_ds_h32i16.h](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_fifo_ds_h32i16.h) is the same as ijha_fifo_ds_h32i32.h but with 16bit indices, saving 4 bytes per handle tracked. Use this if less or equal to 65535 handles is needed. Memory usage: 12bytes / handle.

## License

Dual-licensed under 3-Clause BSD & Unlicense license.
