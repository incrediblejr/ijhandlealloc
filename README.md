# Collection of handle/id allocators

In many situations it is desireable to refer to resources by handles/ids instead of pointers.


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

- [ijha_fifo_h32.h](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_fifo_h32.h) is a FIFO handle allocator with handles that have a user
configurable number of userdata/flags bits and variable number of generation bits. Memory usage: 8bytes / handle.


- [ijha_fifo_ds_h32i32.h](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_fifo_ds_h32i32.h) is a FIFO handle allocator built on top of ijha_fifo_h32 with
added bookkeeping of handle's sparse/dense relationships to be able to keep all (used) data linear in memory while retaining stable handles. Memory usage: 16bytes / handle.


- [ijha_fifo_ds_h32i16.h](https://github.com/incrediblejr/ijhandlealloc/blob/master/ijha_fifo_ds_h32i16.h) is the same as ijha_fifo_ds_h32i32.h but with 16bit indices, saving 4 bytes per handle tracked. Use this if less or equal to 65535 handles is needed. Memory usage: 12bytes / handle.


## License

Dual-licensed under 3-Clause BSD & Unlicense license.
