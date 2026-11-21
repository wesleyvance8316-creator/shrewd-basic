# shrewd-basic

shrewd-basic is a small C11 utility library: a platform layer (error
codes, memory), data structures, a leveled logging module and a
compile-time reflection module.

## Guides

- [reflect reference](reflect.md) - the full reflect guide: data model,
  authoring macros, API, dump output, pointer/ops semantics and the
  optional code generator.
- [reflect overview](reflect-overview.md) - a summary of everything the
  reflect module supports today: type system, field shapes, API list,
  semantics and tooling.
- [reflect API reference](reflect-api.md) - every reflect interface with
  its full name, exact prototype, parameters and return codes, grouped
  by concern (metadata, values, paths, lookup lists, graph algorithms,
  traversal).
- [json reference](json.md) - the json module: layered architecture, the
  value model (DOM), the strict RFC 8259 parser/serializer and the wire
  format contract.

## Modules

| Module | Header | Purpose |
|---|---|---|
| platform | `shrewd-basic/platform/error.h`, `memory.h` | error codes and allocation wrappers |
| ds | `shrewd-basic/ds/*.h` | slist, dlist, queue, stack, btree, rbtree, trie, radix tree |
| log | `shrewd-basic/log/log.h` | leveled logging with per-module masks and outputs |
| reflect | `shrewd-basic/reflect/reflect.h` | compile-time type metadata for generic code |
| json | `shrewd-basic/json/json.h` | JSON value model, parser and serializer |

The API reference below is generated from the public headers.
