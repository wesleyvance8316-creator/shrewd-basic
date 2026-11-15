# shrewd-basic

shrewd-basic is a small C11 utility library: a platform layer (error
codes, memory), data structures, a leveled logging module and a
compile-time reflection module.

## Guides

- [reflect reference](reflect.md) - the full reflect guide: data model,
  authoring macros, API, dump output, pointer/ops semantics and the
  optional code generator.

## Modules

| Module | Header | Purpose |
|---|---|---|
| platform | `shrewd-basic/platform/error.h`, `memory.h` | error codes and allocation wrappers |
| ds | `shrewd-basic/ds/*.h` | slist, dlist, queue, stack, btree, rbtree, trie, radix tree |
| log | `shrewd-basic/log/log.h` | leveled logging with per-module masks and outputs |
| reflect | `shrewd-basic/reflect/reflect.h` | compile-time type metadata for generic code |

The API reference below is generated from the public headers.
