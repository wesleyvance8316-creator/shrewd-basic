# shrewd-basic

shrewd-basic is a small C11 utility library: a platform layer (error
codes, memory), data structures, a leveled logging module, a
compile-time reflection module, serialization formats and the sb_dbus
data bus.

## Guides

- [reflect reference](reflect.md) - the full reflect guide: data model,
  authoring macros, API, dump output, pointer/ops semantics and the
  optional code generator.
- [reflect API reference](reflect-api.md) - every reflect interface with
  its full name, exact prototype, parameters and return codes, grouped
  by concern (metadata, values, paths, lookup lists, graph algorithms,
  traversal).
- [requirements](requirements.md) - the consolidated requirements and
  boundary decisions per module: delivered scope, explicit non-goals
  and the trigger conditions of demand-driven items.
- [formats API reference](formats-api.md) - the text layer and the
  seven format modules (INI, env/args, CSV, TOML, msgpack, CBOR, bin):
  encode/decode round trips, ownership contracts and limits; every
  interface exercised by programs/formats_demo.c.
- [json API reference](json-api.md) - every json interface: options,
  value model, text I/O, struct mapping, policy rows, tools, pull.
- [annotation guide](annotations.md) - reflect_gen.py annotations per
  target (types, members, unions, wide forms, json policies) plus CLI
  and build wiring.
- [dbus design](dbus-design.md) / [dbus API](dbus-api.md) - the sb_dbus
  data bus: typed namespace with hook chains, inproc and shm data
  planes, batch/coalesce/guard/policy; API overview with usage
  examples, every interface exercised by programs/dbus_demo.c.
- [style guide](style.md) - C coding and formatting rules: initializer
  completeness, mandatory braces, per-clause condition parentheses,
  and the clang-format / clang-tidy gate.
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
| json | `shrewd-basic/formats/json.h` | JSON value model, parser and serializer |
| bin | `shrewd-basic/formats/bin.h` | binary struct serialization over reflect metadata |
| ini | `shrewd-basic/formats/ini.h` | struct <-> INI sections and keys |
| env | `shrewd-basic/formats/env.h` | environment variables / argv -> struct |
| csv | `shrewd-basic/formats/csv.h` | struct arrays <-> RFC 4180 tables |
| toml | `shrewd-basic/formats/toml.h` | struct <-> TOML 1.0 subset |
| msgpack | `shrewd-basic/formats/msgpack.h` | struct <-> MessagePack |
| cbor | `shrewd-basic/formats/cbor.h` | struct <-> CBOR (RFC 8949 subset) |
| dbus | `shrewd-basic/dbus/dbus.h` | sb_dbus typed data bus (hooks, inproc/shm) |

The API reference below is generated from the public headers.
