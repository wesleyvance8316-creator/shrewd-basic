#!/usr/bin/env python3
"""Generate shrewd-basic reflect descriptor definitions from a C header.

Emits the very same SHREWD_BASIC_REFLECT_* macros a human would write,
so the generated code stays readable and hand-editable. Multi-dimension
arrays gain ARRAY_DEFINE row types (one per row level, innermost first).

Types generate only when named - strict opt-in. Annotate in the header:

    /* reflect: type=packet_t */
    /* reflect: type=helper_t external */          (referenced, hand-written)
    /* reflect: member=packet_t.name owned */
    /* reflect: member=packet_t.data count=len */

or name them on the command line (--type / --external) for headers you
cannot edit. Member directives: owned, stringbuf, wstringbuf, skip,
count=, ops=, name=, union_tag=, case=. Targets are fully qualified and
annotation position carries zero semantics. Wide strings (char16_t /
char32_t / wchar_t, plus one level of typedef alias) map to the
FIELD_STRING16/32/WSTRING family. Unions generate UNION_DEFINE from
case= directives; union-typed members need union_tag= (or skip=).

Fast-fail facts (never silently wrong): lexical backend rejects
bit-fields and function pointers (--backend dwarf describes bit-fields);
a referenced in-header type must be generated or marked external.

Usage:
    python3 reflect_gen.py input.h [-o stub] [--quiet]
    python3 reflect_gen.py input.h --backend dwarf --layout [--json]

Writes stub.h (TYPE_DECLARE for every generated type) and stub.c (the
DEFINEs); the stub defaults to the input file's base name. The library
itself never needs this tool - it is an optional authoring aid.
"""

import argparse
import os
import re
import sys

PRIMITIVE_TYPES = {
    "bool": "shrewd_basic_reflect_bool",
    "_Bool": "shrewd_basic_reflect_bool",
    "char": "shrewd_basic_reflect_char",
    "signed char": "shrewd_basic_reflect_int8",
    "unsigned char": "shrewd_basic_reflect_uint8",
    "short": "shrewd_basic_reflect_int16",
    "short int": "shrewd_basic_reflect_int16",
    "unsigned short": "shrewd_basic_reflect_uint16",
    "unsigned short int": "shrewd_basic_reflect_uint16",
    "int": "shrewd_basic_reflect_int32",
    "unsigned int": "shrewd_basic_reflect_uint32",
    "long": "shrewd_basic_reflect_int64",
    "long int": "shrewd_basic_reflect_int64",
    "unsigned long": "shrewd_basic_reflect_uint64",
    "unsigned long int": "shrewd_basic_reflect_uint64",
    "long long": "shrewd_basic_reflect_int64",
    "long long int": "shrewd_basic_reflect_int64",
    "unsigned long long": "shrewd_basic_reflect_uint64",
    "unsigned long long int": "shrewd_basic_reflect_uint64",
    "float": "shrewd_basic_reflect_float",
    "double": "shrewd_basic_reflect_double",
    "int8_t": "shrewd_basic_reflect_int8",
    "uint8_t": "shrewd_basic_reflect_uint8",
    "int16_t": "shrewd_basic_reflect_int16",
    "uint16_t": "shrewd_basic_reflect_uint16",
    "int32_t": "shrewd_basic_reflect_int32",
    "uint32_t": "shrewd_basic_reflect_uint32",
    "int64_t": "shrewd_basic_reflect_int64",
    "uint64_t": "shrewd_basic_reflect_uint64",
    "size_t": "shrewd_basic_reflect_uint64",
    "ssize_t": "shrewd_basic_reflect_int64",
    "intptr_t": "shrewd_basic_reflect_int64",
    "uintptr_t": "shrewd_basic_reflect_uint64",
}

ARRAY_PRIMITIVES = {
    "char": "shrewd_basic_reflect_char",
}


class ParseError(Exception):
    pass


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def split_declarations(body):
    """Split a struct body into member declarations (one declarator each)."""
    parts = []
    depth = 0
    current = ""
    for ch in body:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == ";" and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += ch
    if current.strip():
        raise ParseError("member without ';': %s" % current.strip())
    return [p for p in parts if p]


def parse_member(decl):
    """Return (base_type, stars, name, dims, flexible); dims is outermost
    first ([] for scalars, [N] for T name[N], [N, M] for T name[N][M], ...)."""
    decl = " ".join(decl.split())
    dims = []
    flexible = False
    while True:
        match = re.match(r"^(.*\S)\s*\[\s*(\d*)\s*\]$", decl)
        if not match:
            break
        decl = match.group(1).strip()
        digits = match.group(2)
        if digits in ("", "0"):
            flexible = True  # C99 FAM or the GNU zero-length form
        else:
            dims.append(int(digits))
    dims.reverse()  # the first bracket in the source is the outermost
    match = re.match(r"^(.*\S)\s*(\*+)\s*([A-Za-z_]\w*)$", decl)
    if match:
        return match.group(1).strip(), match.group(2), match.group(3), dims, flexible
    match = re.match(r"^(.*\S)\s+([A-Za-z_]\w*)$", decl)
    if match:
        return match.group(1).strip(), "", match.group(2), dims, flexible
    raise ParseError("cannot parse member: %s" % decl)


def normalize(base):
    base = base.replace("const ", " ").replace("volatile ", " ")
    return " ".join(base.split())


def member_parts(member):
    """Unpack a member tuple: (base, stars, name, dims, flexible[, bits]).
    bits is None for lexical members; the DWARF backend attaches
    (unit_ref, unit_offset, unit_size, bit_offset, bit_width) for
    C bit-field members."""
    base, stars, name, dims, flexible = member[:5]
    bits = member[5] if 5 < len(member) else None
    return base, stars, name, dims, flexible, bits


# --- Annotations -------------------------------------------------------------
#
# /* reflect: type=NAME ... */ and /* reflect: member=TYPE.MEMBER ... */
# carry the *intent* the layout cannot express: which types generate,
# ownership, text-buffer semantics, count pairing, union tags. Targets
# are named explicitly (fully qualified) and annotation position carries
# zero semantics - a comment moved by a refactoring tool never changes
# the output. Facts (offsets, sizes, bit placement) come from the
# compiler, never from here.

class AnnotationError(Exception):
    pass


ANNOTATION_RE = re.compile(r"/\*\s*reflect:\s*(.*?)\*/", re.S)
TYPE_FLAGS = {"external"}
TYPE_VALUES = {"name", "ops"}
MEMBER_FLAGS = {"owned", "stringbuf", "wstringbuf", "skip"}
MEMBER_VALUES = {"count", "ops", "union_tag", "case"}
WIDE_NAMES = {"char16_t": "16", "char32_t": "32", "wchar_t": "wchar"}


class Annotations:
    """Fully-qualified directive tables: types[name] and
    members[(type, member)], each a {key: value} map. Repeated
    annotations on one target merge; a conflicting key is a hard error."""

    def __init__(self):
        self.types = {}
        self.members = {}

    def for_type(self, name):
        return self.types.get(name, {})

    def for_member(self, type_name, member):
        return self.members.get((type_name, member), {})

    def desc(self, type_name):
        """Descriptor variable name for a type (name= override wins)."""
        return self.for_type(type_name).get("name") or descriptor_name(type_name)


def _annotation_target(token):
    if "=" not in token:
        raise AnnotationError("target must be type=NAME or member=TYPE.MEMBER: %r" % token)
    key, _, ident = token.partition("=")
    if "type" == key:
        if not re.match(r"^[A-Za-z_]\w*$", ident):
            raise AnnotationError("bad type target: %r" % token)
        return "type", ident
    if "member" == key:
        if "." not in ident:
            raise AnnotationError("member target must be fully qualified TYPE.MEMBER: %r" % token)
        type_name, _, member = ident.rpartition(".")
        if not re.match(r"^[A-Za-z_]\w*$", type_name) or not re.match(r"^[A-Za-z_]\w*$", member):
            raise AnnotationError("bad member target: %r" % token)
        return "member", (type_name, member)
    raise AnnotationError("target must start with type= or member=: %r" % token)


def scan_annotations(text):
    """Parse every /* reflect: ... */ comment into one Annotations table.

    Grammar (whitespace separated): the first token names the target,
    the rest are directives - bare flags (owned, stringbuf, wstringbuf,
    skip, external) or key=value (name=, ops=, count=, union_tag=,
    case=). case= takes a comma-separated list of tag constants."""
    ann = Annotations()
    for match in ANNOTATION_RE.finditer(text):
        tokens = match.group(1).split()
        if not tokens:
            raise AnnotationError("empty reflect annotation")
        kind, target = _annotation_target(tokens[0])
        flags = TYPE_FLAGS if "type" == kind else MEMBER_FLAGS
        values = TYPE_VALUES if "type" == kind else MEMBER_VALUES
        table = ann.types if "type" == kind else ann.members
        store = table.setdefault(target, {})
        for token in tokens[1:]:
            if "=" in token:
                key, _, value = token.partition("=")
                if key not in values:
                    raise AnnotationError(
                        "%s: '%s' is not a %s directive" % (tokens[0], key, kind))
                if not value:
                    raise AnnotationError("%s: %s= needs a value" % (tokens[0], key))
                if key in store:
                    raise AnnotationError("%s: duplicate %s= directive" % (tokens[0], key))
                store[key] = value
            else:
                if token not in flags:
                    raise AnnotationError(
                        "%s: '%s' is not a %s directive" % (tokens[0], token, kind))
                if token in store:
                    raise AnnotationError("%s: duplicate '%s' directive" % (tokens[0], token))
                store[token] = True
    return ann


class HeaderParser:
    def __init__(self, text):
        self.text = text
        self.structs = {}  # typedef name -> [(base, stars, name, dims, flexible)]
        self.unions = {}   # typedef name -> same member shape as structs
        self.enums = {}    # typedef name -> [constant names]
        self.order = []    # declaration order of type names
        self.wide_alias = scan_wide_aliases(text)

    def parse(self):
        for kind, body, names in scan_typedefs(self.text):
            for name in names:
                if "enum" == kind:
                    self.enums[name] = self._parse_enum(body)
                else:
                    members = self._parse_struct(body)
                    if "union" == kind:
                        self.unions[name] = members
                    else:
                        self.structs[name] = members
                self.order.append(name)
        return self

    def _parse_struct(self, body):
        members = []
        for decl in split_declarations(body):
            if ":" in decl:
                raise ParseError("bit-field: %s" % decl)
            if "(" in decl:
                raise ParseError("function pointer: %s" % decl)
            members.append(parse_member(decl))
        return members

    def _parse_enum(self, body):
        names = []
        for entry in body.split(","):
            entry = entry.strip()
            if not entry:
                continue
            name = entry.split("=")[0].strip()
            if not re.match(r"^[A-Za-z_]\w*$", name):
                raise ParseError("cannot parse enum constant: %s" % entry)
            names.append(name)
        return names


def descriptor_name(type_name):
    return "%s_reflect" % type_name


def find_count_member(members, name):
    """Pair a flexible tail with its count member (name_count, name_len,
    count, len - in that order of preference)."""
    names = [m[2] for m in members]
    for candidate in ("%s_count" % name, "%s_len" % name, "count", "len"):
        if candidate in names:
            return candidate
    return None


def primitive_ref(base):
    return PRIMITIVE_TYPES.get(base)


def array_row_defines(struct_name, member, base, dims, desc):
    """ARRAY_DEFINE lines for the row types of a multi-dimension array.

    Returns (defines, outer_ref): the outermost row type is what
    FIELD_ARRAY references. Types are generated innermost-first so each
    one may reference its predecessor (3D+ nests naturally)."""
    defines = []
    ref = None
    elem_type = base
    for level, count in enumerate(dims[1:], start=1):
        name = "%s_%s_row%d" % (struct_name, member, level)
        defines.append("SHREWD_BASIC_REFLECT_ARRAY_DEFINE(%s, %s, %s, %d);"
                       % (name, ref or primitive_ref(base) or desc(base),
                          elem_type, count))
        ref = name
        elem_type = "%s[%d]" % (elem_type, count)
    return defines, ref


def wide_unit(base, wide_alias):
    """Wide-string spelling for a base name ('char16_t'/'char32_t'/'wchar_t')
    or None. Alias typedefs resolve through the header's wide aliases."""
    resolved = wide_alias.get(base, base)
    return resolved if resolved in WIDE_NAMES else None


OPS_CAPABLE = ("SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE",
               "SHREWD_BASIC_REFLECT_FIELD_ARRAY",
               "SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER",
               "SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED",
               "SHREWD_BASIC_REFLECT_FIELD_STRING16",
               "SHREWD_BASIC_REFLECT_FIELD_STRING16_OWNED",
               "SHREWD_BASIC_REFLECT_FIELD_STRING16_BUFFER",
               "SHREWD_BASIC_REFLECT_FIELD_STRING32",
               "SHREWD_BASIC_REFLECT_FIELD_STRING32_OWNED",
               "SHREWD_BASIC_REFLECT_FIELD_STRING32_BUFFER",
               "SHREWD_BASIC_REFLECT_FIELD_WSTRING",
               "SHREWD_BASIC_REFLECT_FIELD_WSTRING_OWNED",
               "SHREWD_BASIC_REFLECT_FIELD_WSTRING_BUFFER")


def apply_ops(macro, ops, label):
    """Suffix a FIELD_* macro into its _OPS twin (ops_ref last). Only the
    forms with a real _OPS variant accept ops= - anything else is an
    error, never a silent drop."""
    for name in OPS_CAPABLE:
        if macro.startswith(name + "("):
            return name + "_OPS(" + macro[len(name) + 1:-1] + ", " + ops + ")"
    raise ParseError("%s: ops= is not supported for this member form" % label)


def field_macro(parser, struct_name, member, members, desc, ann):
    """Return (macro, warning, defines); defines are statements that must
    precede the TYPE_DEFINE of the owning struct. ann is the member's
    directive dict; desc(base) resolves descriptor variable names."""
    base, stars, name, dims, flexible, bits = member_parts(member)
    label = "%s.%s" % (struct_name, name)
    if bits is not None:
        if ann:
            raise ParseError("%s: bit-group member takes no member directives" % label)
        unit_ref, unit_offset, unit_size, bit_offset, bit_width = bits
        return ("SHREWD_BASIC_REFLECT_FIELD_BITS_UNIT(%s, %s, %s, %d, %d, %d, %d)"
                % (struct_name, name, unit_ref, unit_offset, unit_size,
                   bit_offset, bit_width),
                None, [])
    base = normalize(base)
    wide = wide_unit(base, parser.wide_alias)
    if wide is not None:
        macro, warning, defines = wide_field_macro(struct_name, name, wide, stars, dims, ann)
    elif flexible:
        if stars != "":
            raise ParseError("pointer flexible array not described: %s" % name)
        for bad in ("stringbuf", "wstringbuf", "owned"):
            if bad in ann:
                raise ParseError("%s: %s= needs a fixed array or pointer member" % (label, bad))
        count_member = ann.get("count") or find_count_member(members, name)
        if ann.get("count") and (ann["count"] not in [m[2] for m in members]):
            raise ParseError("%s: count=%s names no member of %s"
                             % (label, ann["count"], struct_name))
        if count_member is None:
            return None, ("no count member found for flexible array '%s' "
                          "(expected %s_count/%s_len/count/len or a count= "
                          "directive) - skipped" % (name, name, name)), []
        ref = primitive_ref(base)
        if ref is None:
            if base in parser.enums or base in parser.structs or base in parser.unions:
                ref = desc(base)
            else:
                return None, ("unknown element type '%s' of flexible array "
                              "'%s' - skipped" % (base, name)), []
        macro = "SHREWD_BASIC_REFLECT_FIELD_FLEX(%s, %s, %s, %s)" % (
            struct_name, name, count_member, ref)
        warning, defines = None, []
    elif 2 <= len(dims):
        # Multi-dimension array: rows become array value types.
        if stars != "":
            raise ParseError("pointer multi-dimension array not described: %s" % name)
        for bad in ("stringbuf", "wstringbuf", "owned", "count", "union_tag", "case"):
            if bad in ann:
                raise ParseError("%s: %s= does not apply to a multi-dimension array" % (label, bad))
        if base not in PRIMITIVE_TYPES and base not in parser.enums \
                and base not in parser.structs and base not in parser.unions:
            return None, ("unknown element type '%s' of multi-dimension array "
                          "'%s' - skipped" % (base, name)), []
        defines, ref = array_row_defines(struct_name, name, base, dims, desc)
        macro = "SHREWD_BASIC_REFLECT_FIELD_ARRAY(%s, %s, %s)" % (struct_name, name, ref)
        warning = None
    else:
        if "count" in ann:
            raise ParseError("%s: count= only applies to flexible arrays" % label)
        macro, warning, defines = member_macro(parser, struct_name, member, members, desc, ann)
    if "ops" in ann:
        macro = apply_ops(macro, ann["ops"], label)
    return macro, warning, defines


def member_macro(parser, struct_name, member, members, desc, ann):
    """Scalar / pointer / single-darray / fixed-array member forms."""
    base, stars, name, dims, flexible, _bits = member_parts(member)
    label = "%s.%s" % (struct_name, name)
    if stars not in ("", "*"):
        raise ParseError("only single pointers are described: %s" % name)
    # Union-typed member: FIELD_UNION when tagged, otherwise the user must
    # choose (union_tag= or skip=) - never guess.
    if ("" == stars) and (0 == len(dims)) and (normalize(base) in parser.unions):
        for bad in ("owned", "stringbuf", "wstringbuf"):
            if bad in ann:
                raise ParseError("%s: %s= does not apply to a union member" % (label, bad))
        if "union_tag" in ann:
            tag = ann["union_tag"]
            if tag not in [m[2] for m in members]:
                raise ParseError("%s: union_tag=%s names no member of %s"
                                 % (label, tag, struct_name))
            return ("SHREWD_BASIC_REFLECT_FIELD_UNION(%s, %s, %s, %s)"
                    % (struct_name, name, desc(normalize(base)), tag)), None, []
        return None, ("union member '%s' needs a union_tag= directive (or "
                      "skip=) - skipped" % name), []
    if ("union_tag" in ann) or ("case" in ann):
        raise ParseError("%s: union_tag=/case= only apply to union members" % label)
    base = normalize(base)
    is_array = (1 == len(dims))
    ref = None
    if (base == "void") and (stars == "*"):
        if ("owned" in ann) or ("stringbuf" in ann) or ("wstringbuf" in ann):
            raise ParseError("%s: void* takes no string directives" % label)
        ref = "shrewd_basic_reflect_void"
    elif (base == "char") and (stars == "*"):
        if ("stringbuf" in ann) or ("wstringbuf" in ann):
            raise ParseError("%s: text-buffer directives need a fixed array" % label)
        if "owned" in ann:
            return "SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(%s, %s)" % (
                struct_name, name), None, []
        return "SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(%s, %s)" % (
            struct_name, name), None, []
    elif (base == "char") and is_array and ("stringbuf" in ann):
        if "owned" in ann:
            raise ParseError("%s: owned= needs a string pointer member" % label)
        return "SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(%s, %s)" % (
            struct_name, name), None, []
    elif base in PRIMITIVE_TYPES:
        if "owned" in ann:
            raise ParseError("%s: owned= needs a string pointer member" % label)
        if ("stringbuf" in ann) or ("wstringbuf" in ann):
            raise ParseError("%s: text-buffer directives need char[] or wide buffers" % label)
        ref = PRIMITIVE_TYPES[base]
        if (stars == "") and (not is_array):
            return "SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(%s, %s)" % (
                struct_name, name), None, []
    elif base in parser.enums:
        if "owned" in ann:
            raise ParseError("%s: owned= needs a string pointer member" % label)
        ref = desc(base)
        if (stars == "") and (not is_array):
            return "SHREWD_BASIC_REFLECT_FIELD_ENUM(%s, %s, %s)" % (
                struct_name, name, ref), None, []
    else:
        if "owned" in ann:
            raise ParseError("%s: owned= needs a string pointer member" % label)
        ref = desc(base)
        if (stars == "") and (not is_array):
            return "SHREWD_BASIC_REFLECT_FIELD_STRUCT(%s, %s, %s)" % (
                struct_name, name, ref), None, []
    if ref is None:
        raise ParseError("unsupported member type: %s %s" % (base, name))
    if stars == "*":
        macro = "SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY" if is_array \
            else "SHREWD_BASIC_REFLECT_FIELD_PTR"
    else:
        macro = "SHREWD_BASIC_REFLECT_FIELD_ARRAY"
    return "%s(%s, %s, %s)" % (macro, struct_name, name, ref), None, []


def wide_field_macro(struct_type, name, wide, stars, dims, ann):
    """Wide-string member forms. Pointer -> FIELD_STRING16/32(_OWNED) or
    FIELD_WSTRING(_OWNED); fixed array -> the _BUFFER macro under
    wstringbuf=, a raw code-unit FIELD_ARRAY otherwise."""
    suffix = {"char16_t": "STRING16", "char32_t": "STRING32", "wchar_t": "WSTRING"}[wide]
    label = "%s.%s" % (struct_type, name)
    for bad in ("count", "union_tag", "case"):
        if bad in ann:
            raise ParseError("%s: %s= does not apply to a wide string member" % (label, bad))
    if 2 <= len(dims):
        raise ParseError("multi-dimension wide arrays not described: %s" % name)
    if "stringbuf" in ann:
        raise ParseError("%s: stringbuf is for char[N]; use wstringbuf" % label)
    if 1 == len(dims):
        if "owned" in ann:
            raise ParseError("%s.%s: owned needs a string pointer member" % (struct_type, name))
        if "wstringbuf" in ann:
            return ("SHREWD_BASIC_REFLECT_FIELD_%s_BUFFER(%s, %s)"
                    % (suffix, struct_type, name)), None, []
        unit_ref = {"char16_t": "shrewd_basic_reflect_uint16",
                    "char32_t": "shrewd_basic_reflect_uint32",
                    "wchar_t": "SHREWD_BASIC_REFLECT_WCHAR_UNIT"}[wide]
        return ("SHREWD_BASIC_REFLECT_FIELD_ARRAY(%s, %s, %s)"
                % (struct_type, name, unit_ref)), None, []
    if stars != "*":
        raise ParseError("unsupported wide member shape: %s %s" % (wide, name))
    if "wstringbuf" in ann:
        raise ParseError("%s.%s: wstringbuf needs a fixed array" % (struct_type, name))
    if "owned" in ann:
        return ("SHREWD_BASIC_REFLECT_FIELD_%s_OWNED(%s, %s)"
                % (suffix, struct_type, name)), None, []
    return ("SHREWD_BASIC_REFLECT_FIELD_%s(%s, %s)" % (suffix, struct_type, name)), None, []


def collect_references(parser, selected, desc):
    """TYPE_DECLARE targets: bases that are neither described here nor
    primitive-wide. Externally-marked in-header types count as refs."""
    refs = []
    seen = set()
    for struct_name in selected:
        members = parser.structs.get(struct_name) or parser.unions.get(struct_name) or []
        for member in members:
            base, stars, _name, dims, _flexible, bits = member_parts(member)
            if bits is not None:
                continue
            base = normalize(base)
            if (base in PRIMITIVE_TYPES) or ("void" == base) or (base in parser.wide_alias) \
                    or (base in WIDE_NAMES):
                continue
            if (base in parser.structs) or (base in parser.unions) or (base in parser.enums):
                if base in selected:
                    continue
            var = desc(base)
            if (var, base) not in seen:
                seen.add((var, base))
                refs.append((var, base))
    return refs


# --- DWARF backend (requires pyelftools + a C compiler) -------------------
#
# Compiles a probe TU including the input header, then reads the *actual*
# layout (offsets, sizes, bit-field placement) from the object's DWARF.
# This is the only honest source for C bit-field placement and the only
# way to pin layout facts into _Static_assert guards.

DWARF_BASE_KEYS = {
    "_Bool": "bool",
    "bool": "bool",
    "char": "char",
    "signed char": "signed char",
    "unsigned char": "unsigned char",
    "short int": "short",
    "short unsigned int": "unsigned short",
    "int": "int",
    "unsigned int": "unsigned int",
    "long int": "long int",
    "long unsigned int": "unsigned long int",
    "long long int": "long long int",
    "long long unsigned int": "unsigned long long int",
    "float": "float",
    "double": "double",
}

UNIT_SIZES = (1, 2, 4, 8)


def scan_typedefs(text):
    """Brace-counting scan for `typedef struct|union|enum {body} names;`.
    Survives nested braces, bit-fields and multi-name typedefs (the
    lexical member parser cannot). Yields (kind, body, [names]) in
    declaration order; kind is 'struct'/'union'/'enum'."""
    text = strip_comments(text)
    results = []
    opener = re.compile(r"typedef\s+(struct|union|enum)\b")
    closer = re.compile(r"\s*([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*;")
    i = 0
    while True:
        match = opener.search(text, i)
        if match is None:
            break
        j = match.end()
        head = re.match(r"\s*([A-Za-z_]\w*)?\s*\{", text[j:])
        if head is None:
            i = match.end()
            continue
        body_start = j + head.end()
        depth = 1
        k = body_start
        while (k < len(text)) and (0 < depth):
            if "{" == text[k]:
                depth += 1
            elif "}" == text[k]:
                depth -= 1
            k += 1
        if 0 != depth:
            raise ParseError("unterminated typedef body near offset %d" % body_start)
        names = closer.match(text, k)
        if names is None:
            i = k
            continue
        results.append((match.group(1),
                        text[body_start:k - 1],
                        [nm.strip() for nm in names.group(1).split(",")]))
        i = names.end()
    return results


def scan_typedef_names(text):
    """[(kind, name)] in declaration order - the name view of
    scan_typedefs() (kind is 'struct'/'union'/'enum')."""
    return [(kind, nm) for kind, _body, names in scan_typedefs(text) for nm in names]


def scan_wide_aliases(text):
    """`typedef wchar_t my_wstr;` style aliases -> {alias: wide name}.
    Lets the lexical backend keep wide-string semantics through one
    level of typedef sugar (the dwarf backend walks real chains)."""
    text = strip_comments(text)
    out = {}
    for match in re.finditer(r"typedef\s+(char16_t|char32_t|wchar_t)\s+([A-Za-z_]\w*)\s*;", text):
        out[match.group(2)] = match.group(1)
    return out


class DwarfParser:
    """Compile a probe TU and describe the requested typedefs from DWARF.
    Produces the HeaderParser model (structs/enums/order, member tuples
    with an extra bits tuple for C bit-fields) plus layout facts."""

    def __init__(self, text, input_path, cc=None, cflags=None):
        self.text = text
        self.input_path = input_path
        self.cc = cc or os.environ.get("CC") or "cc"
        self.cflags = list(cflags) if cflags else []
        self.structs = {}
        self.unions = {}
        self.enums = {}
        self.order = []
        self.wide_alias = scan_wide_aliases(text)
        self.layout = {}      # name -> {"size", "members": [(member, offset, is_bit)]}
        self.reports = {}     # name -> list of report entries (for --layout)
        self.fingerprint = []
        self.warnings = []
        self._ptr_size = 8
        self._canonical = {}  # struct/union/enum DIE offset -> descriptor base name

    def parse(self):
        wanted = [(kind, nm) for kind, nm in scan_typedef_names(self.text)
                  if kind in ("struct", "union", "enum")]
        if not wanted:
            raise ParseError("no typedef struct/union/enum found")
        obj_path = self._compile_probe([nm for _kind, nm in wanted])
        try:
            self._read_dwarf(obj_path, wanted)
        finally:
            for victim in (obj_path, os.path.splitext(obj_path)[0] + ".c"):
                if os.path.exists(victim):
                    os.unlink(victim)
        return self

    def _compile_probe(self, names):
        import subprocess
        import tempfile
        work = tempfile.mkdtemp(prefix="reflect_gen_")
        probe_c = os.path.join(work, "probe.c")
        probe_o = os.path.join(work, "probe.o")
        with open(probe_c, "w", encoding="utf-8") as handle:
            handle.write("/* reflect_gen probe: force DIEs for every target type. */\n")
            handle.write("#include \"%s\"\n" % os.path.abspath(self.input_path))
            for nm in names:
                handle.write("%s *volatile shrewd_gen_keep_%s = 0;\n" % (nm, nm))
        cmd = [self.cc, "-g", "-std=c11", "-c", probe_c, "-o", probe_o] + self.cflags
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if 0 != proc.returncode:
            raise ParseError("probe compile failed (%s): %s"
                             % (" ".join(cmd), proc.stderr.strip()))
        self.fingerprint = [
            " * Backend: dwarf (probe: %s)" % " ".join(cmd[1:-3] + ["probe.c", "-o", "probe.o"]),
            " * Compiler: %s / target %s" % (self._compiler_id(), self._target_triple()),
        ]
        return probe_o

    def _compiler_id(self):
        import subprocess
        proc = subprocess.run([self.cc, "--version"], capture_output=True, text=True)
        line = proc.stdout.splitlines()
        return line[0].strip() if line else self.cc

    def _target_triple(self):
        import subprocess
        proc = subprocess.run([self.cc, "-dumpmachine"], capture_output=True, text=True)
        return proc.stdout.strip() or "unknown"

    def _read_dwarf(self, obj_path, wanted):
        from elftools.elf.elffile import ELFFile
        typedefs = {}
        with open(obj_path, "rb") as handle:
            elf = ELFFile(handle)
            dwarf = elf.get_dwarf_info()
            for cu in dwarf.iter_CUs():
                for die in cu.iter_DIEs():
                    if "DW_TAG_typedef" == die.tag:
                        name = self._attr_name(die)
                        if (name is not None) and (name not in typedefs):
                            typedefs[name] = die
        for kind, name in wanted:
            die = typedefs.get(name)
            if die is None:
                self.warnings.append("no DWARF entry for typedef %s - skipped" % name)
                continue
            leaf = self._leaf(self._type_of(die))
            if "DW_TAG_structure_type" == leaf.tag:
                self._read_struct(name, leaf, self.structs)
            elif "DW_TAG_union_type" == leaf.tag:
                self._read_struct(name, leaf, self.unions)
            elif "DW_TAG_enumeration_type" == leaf.tag:
                self._read_enum(name, leaf)
            else:
                self.warnings.append("typedef %s is not a struct/union/enum - skipped" % name)
        for warning in self.warnings:
            print("reflect_gen: %s" % warning, file=sys.stderr)

    def _read_struct(self, name, die, target):
        members = []
        layout_members = []
        report = []
        self._canonical[die.offset] = name
        struct_size = self._attr_value(die, "DW_AT_byte_size")
        if struct_size is None:
            self.warnings.append("struct %s has no size (incomplete) - skipped" % name)
            return
        struct_align = self._type_align(die, struct_size)
        for child in die.iter_children():
            if "DW_TAG_member" != child.tag:
                continue
            for member, layout_entry, report_entry in self._read_member(name, child, struct_size):
                members.append(member)
                layout_members.append(layout_entry)
                report.append(report_entry)
        if not members:
            self.warnings.append("struct %s: no describable fields - skipped" % name)
            return
        target[name] = members
        self.order.append(name)
        self.layout[name] = {"size": struct_size, "members": layout_members}
        self.reports[name] = {"size": struct_size, "align": struct_align, "entries": report}

    def _read_member(self, owner, die, struct_size):
        """Return a list of (member_tuple, layout_entry, report_entry).
        Anonymous struct members flatten (B2 style); anonymous unions and
        unsupported shapes warn and skip."""
        member_name = self._attr_name(die)
        offset = self._attr_value(die, "DW_AT_data_member_location") or 0
        type_die = self._type_of(die)
        leaf, spelling, stars, dims, flexible = self._classify(type_die)
        if member_name is None:
            if ("DW_TAG_structure_type" == leaf.tag) and (0 == stars) and (not dims):
                return self._flatten(owner, leaf, struct_size)
            self.warnings.append("%s: unnamed member of unsupported type - skipped" % owner)
            return []
        bit_size = self._attr_value(die, "DW_AT_bit_size")
        if bit_size is not None:
            return self._read_bit(owner, member_name, die, leaf, struct_size, offset)
        base = self._base_name(leaf, spelling)
        if base is None:
            self.warnings.append("%s.%s: unsupported member type - skipped" % (owner, member_name))
            return []
        report_type = spelling + ("*" * stars) if stars else spelling
        if dims:
            report_type += "".join("[%d]" % d for d in dims)
        if flexible:
            report_type += "[]"
        entry = (base, ("*" * stars), member_name, dims, flexible)
        size = self._entry_size(leaf, stars, dims, flexible, offset, struct_size)
        layout_entry = (member_name, offset, False)
        report_entry = {"name": member_name, "offset": offset, "size": size,
                        "align": self._member_align(leaf, stars), "type": report_type,
                        "bits": None}
        return [(entry, layout_entry, report_entry)]

    def _read_bit(self, owner, member_name, die, leaf, struct_size, offset):
        bit_width = self._attr_value(die, "DW_AT_bit_size")
        bit_start = self._attr_value(die, "DW_AT_data_bit_offset")
        if bit_start is None:
            self.warnings.append("%s.%s: pre-DWARF4 bit placement - skipped"
                                 % (owner, member_name))
            return []
        declared = self._base_size(leaf)
        unit_key = self._base_name(leaf, None)
        unit_ref = self._base_ref(leaf)
        if (declared is None) or (unit_ref is None):
            self.warnings.append("%s.%s: unsupported bit-field unit type - skipped"
                                 % (owner, member_name))
            return []
        # Unit window: prefer the declared type's slot, shrink to fit.
        candidates = [declared] + [s for s in UNIT_SIZES if s != declared]
        unit = None
        for size in candidates:
            if size not in UNIT_SIZES:
                continue
            window = (bit_start // (size * 8)) * size
            if (window + size) > struct_size:
                continue
            if (bit_start + bit_width) <= ((window + size) * 8):
                unit = (window, size)
                break
        if unit is None:
            self.warnings.append("%s.%s: no fitting unit window - skipped" % (owner, member_name))
            return []
        unit_offset, unit_size = unit
        bit_offset = bit_start - (unit_offset * 8)
        entry = (unit_key, "", member_name, [], False,
                 (unit_ref, unit_offset, unit_size, bit_offset, bit_width))
        layout_entry = (member_name, offset, True)
        report_entry = {"name": member_name, "offset": unit_offset, "size": unit_size,
                        "align": unit_size, "type": "bits", "bits": (bit_start, bit_width)}
        return [(entry, layout_entry, report_entry)]

    def _flatten(self, owner, die, struct_size):
        """Anonymous struct member: its fields are the owner's fields (B2).
        Offsets come from DWARF as member facts; macros re-derive them."""
        entries = []
        for child in die.iter_children():
            if "DW_TAG_member" != child.tag:
                continue
            entries.extend(self._read_member(owner, child, struct_size))
        return entries

    def _read_enum(self, name, die):
        constants = []
        for child in die.iter_children():
            if "DW_TAG_enumerator" != child.tag:
                continue
            const = self._attr_name(child)
            if const is not None:
                constants.append(const)
        if not constants:
            self.warnings.append("enum %s: no constants - skipped" % name)
            return
        self.enums[name] = constants
        self.order.append(name)
        self._canonical[die.offset] = name

    # -- type resolution helpers ------------------------------------------

    @staticmethod
    def _attr_value(die, key):
        attr = die.attributes.get(key)
        return None if attr is None else attr.value

    @staticmethod
    def _attr_name(die):
        value = DwarfParser._attr_value(die, "DW_AT_name")
        return None if value is None else value.decode(errors="replace")

    def _type_of(self, die):
        try:
            return die.get_DIE_from_attribute("DW_AT_type")
        except (KeyError, AttributeError):
            return None

    def _leaf(self, die):
        seen = set()
        while die is not None and die.offset not in seen:
            seen.add(die.offset)
            if die.tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type",
                           "DW_TAG_restrict_type", "DW_TAG_pointer_type", "DW_TAG_array_type"):
                nxt = self._type_of(die)
                if nxt is None:
                    return die  # pointer to void
                die = nxt
                continue
            break
        return die

    def _classify(self, die):
        """Walk a type DIE to (leaf, spelling, stars, dims, flexible).
        spelling is the outermost typedef name (or the leaf's own name)."""
        stars = 0
        dims = []
        flexible = False
        spelling = None
        seen = set()
        while die is not None and die.offset not in seen:
            seen.add(die.offset)
            tag = die.tag
            if "DW_TAG_typedef" == tag:
                if spelling is None:
                    spelling = self._attr_name(die)
                die = self._type_of(die)
            elif tag in ("DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type"):
                die = self._type_of(die)
            elif "DW_TAG_pointer_type" == tag:
                stars += 1
                width = self._attr_value(die, "DW_AT_byte_size")
                if width is not None:
                    self._ptr_size = width
                die = self._type_of(die)
                if die is None:
                    return (None, spelling or "void", stars, dims, flexible)
            elif "DW_TAG_array_type" == tag:
                counts = []
                for child in die.iter_children():
                    if "DW_TAG_subrange_type" != child.tag:
                        continue
                    bound = self._attr_value(child, "DW_AT_upper_bound")
                    count = self._attr_value(child, "DW_AT_count")
                    if bound is None and count is None:
                        flexible = True
                    elif (bound is not None) and (bound < 0):
                        flexible = True
                    else:
                        counts.append((count if count is not None else bound + 1))
                if not flexible:
                    dims = dims + counts  # subranges list outermost-first; nested
                                          # array types arrive outer-first too
                die = self._type_of(die)
            else:
                break
        if spelling is None:
            spelling = self._attr_name(die) if die is not None else None
        return (die, spelling, stars, dims, flexible)

    def _base_name(self, leaf, spelling):
        if leaf is None:
            return "void"
        # Spelling first: a typedef'd scalar keeps its written identity
        # ("size_t" -> uint64 everywhere), so both backends agree on the
        # descriptor reference regardless of the platform's leaf width.
        # Wide spellings survive too - they select the string macros.
        if (spelling is not None) and ((spelling in PRIMITIVE_TYPES)
                                       or (spelling in WIDE_NAMES)
                                       or (spelling in self.wide_alias)):
            return spelling
        if "DW_TAG_base_type" == leaf.tag:
            key = DWARF_BASE_KEYS.get(self._attr_name(leaf))
            return key if key in PRIMITIVE_TYPES else None
        if leaf.tag in ("DW_TAG_structure_type", "DW_TAG_union_type",
                        "DW_TAG_enumeration_type"):
            return self._canonical.get(leaf.offset) or spelling or self._attr_name(leaf)
        return None

    def _base_ref(self, leaf):
        key = self._base_name(leaf, None)
        return PRIMITIVE_TYPES.get(key) if key is not None else None

    def _base_size(self, leaf):
        if leaf is None:
            return None
        return self._attr_value(leaf, "DW_AT_byte_size")

    def _entry_size(self, leaf, stars, dims, flexible, offset, struct_size):
        if stars:
            return self._ptr_size
        if dims:
            total = 1
            for count in dims:
                total *= count
            elem = self._base_size(leaf) or 0
            return total * elem
        if flexible:
            return 0
        return (self._base_size(leaf) or 0)

    def _member_align(self, leaf, stars):
        if stars:
            return self._ptr_size
        return self._die_align(leaf)

    def _die_align(self, die):
        """Natural alignment: scalars/arrays by element, structs by their
        widest member, enums by the underlying width. Pointers handled by
        the caller (stars)."""
        if die is None:
            return 1
        tag = die.tag
        if tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type",
                   "DW_TAG_restrict_type", "DW_TAG_array_type"):
            return self._die_align(self._type_of(die))
        if "DW_TAG_pointer_type" == tag:
            return self._ptr_size
        if "DW_TAG_base_type" == tag:
            return self._attr_value(die, "DW_AT_byte_size") or 1
        if "DW_TAG_enumeration_type" == tag:
            return self._attr_value(die, "DW_AT_byte_size") or 1
        if "DW_TAG_structure_type" == tag:
            widest = 1
            for child in die.iter_children():
                if "DW_TAG_member" != child.tag:
                    continue
                _leaf, _spelling, stars, _dims, _flex = self._classify(self._type_of(child))
                widest = max(widest, self._member_align(_leaf, stars))
            return widest
        return 1

    def _type_align(self, type_die, size):
        return self._die_align(type_die) or size or 1


def offset_or_zero(die):
    value = DwarfParser._attr_value(die, "DW_AT_data_member_location")
    return 0 if value is None else value


def check_closure(parser, ann, selected, external):
    """Reference closure: every in-header type named by a generated
    member must itself be generated or explicitly marked external.
    Forgetting is a hard error - two exits, never a guess."""
    chosen = set(selected)
    for name in selected:
        members = parser.structs.get(name) or parser.unions.get(name) or []
        for member in members:
            base, _stars, _mname, _dims, _flexible, bits = member_parts(member)
            if bits is not None:
                continue
            base = normalize(base)
            if (base in PRIMITIVE_TYPES) or ("void" == base) or (base in WIDE_NAMES) \
                    or (base in parser.wide_alias):
                continue
            in_header = (base in parser.structs) or (base in parser.unions) \
                        or (base in parser.enums)
            if in_header and (base not in chosen) and (base not in external) \
                    and (not ann.for_type(base).get("external")):
                raise ParseError(
                    "%s references %s, but %s is neither generated nor external\n"
                    "  - to generate it:  /* reflect: type=%s */ or --type %s\n"
                    "  - to keep it external:  /* reflect: type=%s external */ or "
                    "--external %s" % (name, base, base, base, base, base, base))
    for name in selected:
        if name in parser.unions:
            for member in parser.unions[name]:
                man = ann.for_member(name, member_parts(member)[2])
                if man.get("skip") or man.get("case"):
                    continue
                raise ParseError("union %s: member '%s' needs a case= directive"
                                 % (name, member_parts(member)[2]))


def union_variants(parser, name, members, ann, desc):
    """Group a union's members into UNION_VARIANT lists by their case=
    directives. case=* lands in every variant; variants appear in order
    of first mention."""
    order = []
    by_tag = {}
    star = []
    for member in members:
        base, stars, mname, dims, flexible, bits = member_parts(member)
        man = dict(ann.for_member(name, mname))
        tags = man.pop("case", None)
        if man.pop("skip", False):
            continue
        if tags is None:
            continue  # check_closure already rejected this
        macro, warning, defines = field_macro(parser, name, member, members, desc, man)
        if warning is not None:
            print("reflect_gen: %s: %s" % (name, warning), file=sys.stderr)
        if macro is None:
            continue
        for tag in tags.split(","):
            tag = tag.strip()
            if not tag:
                raise ParseError("union %s.%s: empty case= entry" % (name, mname))
            if "*" == tag:
                star.append(macro)
            else:
                if tag not in by_tag:
                    by_tag[tag] = []
                    order.append(tag)
                by_tag[tag].append(macro)
    if not order:
        raise ParseError("union %s: no case= directives, no variants" % name)
    out = []
    for tag in order:
        fields = by_tag[tag] + star
        out.append("SHREWD_BASIC_REFLECT_UNION_VARIANT(%s,\n    %s)"
                   % (tag, ",\n    ".join(fields)))
    return out


def generate(parser, input_name, ann, selected, fingerprint=None, layout=None):
    """Emit the stub pair for the selected types (strict opt-in).
    fingerprint (dwarf backend) pins the toolchain in the header;
    layout feeds the _Static_assert drift guards."""
    header = ["/* Generated by tools/reflect_gen.py from %s - do not edit." % input_name]
    if fingerprint is not None:
        header.extend(fingerprint)
    header.append(" */")
    lines_c = header + [
        "",
        '#include "%s"' % input_name,
        "",
        "#include <stddef.h>",
        "",
        "#include \"shrewd-basic/reflect/reflect.h\"",
        "",
    ]
    lines_h = header + [
        "",
        "#include \"shrewd-basic/reflect/reflect.h\"",
        "",
    ]
    if layout is not None:
        lines_c.append("/* Layout guards: regenerate with the same toolchain,")
        lines_c.append("   or update the numbers after an intentional change. */")
        for name in selected:
            info = layout.get(name)
            if info is None:
                continue
            lines_c.append("_Static_assert(sizeof(%s) == %d, \"reflect_gen: %s size drift\");"
                           % (name, info["size"], name))
            for member, offset, is_bit in info["members"]:
                if is_bit:
                    continue  # offsetof is undefined for bit-fields
                lines_c.append(
                    "_Static_assert(offsetof(%s, %s) == %d, \"reflect_gen: %s.%s offset drift\");"
                    % (name, member, offset, name, member))
        lines_c.append("")
    refs = collect_references(parser, selected, ann.desc)
    for var, base in refs:
        lines_c.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);" % var)
        lines_h.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);" % var)
    if refs:
        lines_c.append("")
    for name in selected:
        if ann.for_type(name).get("external"):
            continue
        var = ann.desc(name)
        if name in parser.enums:
            entries = ",\n    ".join(
                "SHREWD_BASIC_REFLECT_ENUM_VALUE(%s)" % c for c in parser.enums[name])
            lines_c.append(
                "SHREWD_BASIC_REFLECT_ENUM_DEFINE(%s, %s,\n    %s);"
                % (var, name, entries))
            lines_h.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);" % var)
            lines_c.append("")
            continue
        is_union = name in parser.unions
        members = parser.unions[name] if is_union else parser.structs[name]
        if is_union:
            variants = union_variants(parser, name, members, ann, ann.desc)
            if "ops" in ann.for_type(name):
                raise ParseError("union %s: ops= has no UNION_DEFINE form" % name)
            lines_c.append(
                "SHREWD_BASIC_REFLECT_UNION_DEFINE(%s, %s,\n    %s);"
                % (var, name, ",\n    ".join(variants)))
            lines_h.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);" % var)
            lines_c.append("")
            continue
        entries = []
        pre = []
        for member in members:
            man = dict(ann.for_member(name, member_parts(member)[2]))
            if man.pop("skip", False):
                continue
            macro, warning, defines = field_macro(parser, name, member, members,
                                                  ann.desc, man)
            if warning is not None:
                print("reflect_gen: %s: %s" % (name, warning), file=sys.stderr)
            if macro is not None:
                entries.append(macro)
                pre.extend(defines)
        if not entries:
            print("reflect_gen: %s: no describable fields - skipped" % name,
                  file=sys.stderr)
            continue
        for define in pre:
            lines_c.append(define)
        if pre:
            lines_c.append("")
        fields = ",\n    ".join(entries)
        type_ann = ann.for_type(name)
        if "ops" in type_ann:
            lines_c.append(
                "SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(%s, %s, %s,\n    %s);"
                % (var, name, type_ann["ops"], fields))
        else:
            lines_c.append(
                "SHREWD_BASIC_REFLECT_TYPE_DEFINE(%s, %s,\n    %s);"
                % (var, name, fields))
        lines_h.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);" % var)
        # Optional JSON policy skeleton (docs/json.md section 6).
        rows = ",\n    ".join(
            "SHREWD_BASIC_JSON_KEY(%s, \"%s\")" % (member_parts(m)[2], member_parts(m)[2])
            for m in members if not ann.for_member(name, member_parts(m)[2]).get("skip"))
        lines_c.append("")
        lines_c.append("/* Optional JSON policy for %s:" % name)
        lines_c.append("SHREWD_BASIC_JSON_DEFINE(%s_json, %s," % (name, var))
        lines_c.append("    %s);" % rows)
        lines_c.append("*/")
        lines_c.append("")
    return "\n".join(lines_c) + "\n", "\n".join(lines_h) + "\n"


def layout_analysis(reports):
    """Per-type pahole facts: entries, padding intervals, packed bytes.
    Shared by the text and JSON renderers."""
    out = {}
    for name in reports:
        info = reports[name]
        entries = []
        covered = []  # byte intervals covered by members (units may overlap)
        gaps = []
        cursor = 0
        for entry in info["entries"]:
            start = entry["offset"]
            end = entry["offset"] + entry["size"]
            if cursor < start:
                gaps.append((cursor, start))
            record = {"name": entry["name"], "offset": start, "size": entry["size"],
                      "align": entry["align"], "type": entry["type"]}
            if entry["bits"] is not None:
                record["bits"] = [entry["bits"][0], entry["bits"][0] + entry["bits"][1]]
            entries.append(record)
            covered.append((start, end))
            cursor = max(cursor, end)
        if cursor < info["size"]:
            gaps.append((cursor, info["size"]))
        covered.sort()
        packed = 0
        lo = hi = None
        for start, end in covered:
            if (lo is None) or (hi < start):
                if lo is not None:
                    packed += hi - lo
                lo, hi = start, end
            else:
                hi = max(hi, end)
        if lo is not None:
            packed += hi - lo
        out[name] = {"size": info["size"], "align": info["align"], "entries": entries,
                     "gaps": gaps, "packed": packed, "padding": info["size"] - packed}
    return out


def render_layout(reports, as_json=False):
    """pahole-lite: per-field offset/size/align, padding intervals, and
    the gap between total size and the packed footprint."""
    facts = layout_analysis(reports)
    if as_json:
        import json
        return json.dumps(facts, indent=2, sort_keys=True) + "\n"
    out = []
    for name in facts:
        info = facts[name]
        out.append("%s: %d bytes, align %d" % (name, info["size"], info["align"]))
        pending = list(info["gaps"])
        for entry in info["entries"]:
            while pending and (pending[0][1] <= entry["offset"]):
                lo, hi = pending.pop(0)
                out.append("  padding [%d..%d)  %d byte(s)" % (lo, hi, hi - lo))
            if "bits" in entry:
                out.append("  %-12s off=%-5d bits=[%d..%d)" % (entry["name"], entry["offset"],
                                                               entry["bits"][0], entry["bits"][1]))
            else:
                out.append("  %-12s off=%-5d size=%-5d align=%-4d %s"
                           % (entry["name"], entry["offset"], entry["size"],
                              entry["align"], entry["type"]))
        for lo, hi in pending:
            out.append("  padding [%d..%d)  %d byte(s)" % (lo, hi, hi - lo))
        out.append("  packed %d bytes, %d byte(s) padding" % (info["packed"], info["padding"]))
        out.append("")
    return "\n".join(out) + "\n"


def select_types(parser, ann, cli_types, cli_external):
    """Strict opt-in: generate exactly the type=-annotated names plus
    --type additions. --external / external flag marks in-header types
    that stay hand-written (satisfies the closure check)."""
    selected = []
    external = set(cli_external)
    for name in parser.order:
        if name not in ann.types:
            continue
        if ann.for_type(name).get("external"):
            external.add(name)
        else:
            selected.append(name)
    known = set(parser.order)
    for name in cli_types:
        if name not in known:
            raise ParseError("--type %s: no such typedef in the header" % name)
        if name in external:
            raise ParseError("%s: marked external and requested with --type" % name)
        if name not in selected:
            selected.append(name)
    for name in cli_external:
        if name not in known:
            raise ParseError("--external %s: no such typedef in the header" % name)
    for name in ann.types:
        if name not in known:
            raise ParseError("type=%s: no such typedef in the header" % name)
    for type_name, member in ann.members:
        if type_name not in known:
            raise ParseError("member=%s.%s: no such typedef in the header"
                             % (type_name, member))
        if type_name not in selected:
            print("reflect_gen: member=%s.%s: %s is not generated - annotation ignored"
                  % (type_name, member, type_name), file=sys.stderr)
    for name in selected:
        for member in (parser.structs.get(name) or parser.unions.get(name) or []):
            man = ann.for_member(name, member_parts(member)[2])
            if man.get("skip") and (1 < len(man)):
                raise ParseError("%s.%s: skip= cannot combine with other directives"
                                 % (name, member_parts(member)[2]))
    return selected, external


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input", help="header to parse")
    ap.add_argument("-o", "--output", help="output stub (writes STUB.h and STUB.c)")
    ap.add_argument("--backend", choices=("lex", "dwarf"), default="lex",
                    help="lex: header text scan (default); dwarf: compile a "
                         "probe and read the real layout (needs pyelftools)")
    ap.add_argument("--cc", help="compiler for the dwarf probe (default: $CC or cc)")
    ap.add_argument("--cflags", default="",
                    help="extra flags for the dwarf probe compile")
    ap.add_argument("--type", action="append", default=[], metavar="NAME",
                    help="also generate NAME (repeatable; for headers you "
                         "cannot annotate)")
    ap.add_argument("--external", action="append", default=[], metavar="NAME",
                    help="NAME stays hand-written (repeatable; same effect "
                         "as a 'type=NAME external' annotation)")
    ap.add_argument("--layout", action="store_true",
                    help="dwarf backend: print a pahole-lite layout report "
                         "instead of generating stubs")
    ap.add_argument("--json", action="store_true",
                    help="with --layout: emit the report as JSON")
    ap.add_argument("--quiet", action="store_true", help="suppress the summary")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8") as handle:
        text = handle.read()
    try:
        ann = scan_annotations(text)
        if "dwarf" == args.backend:
            parser = DwarfParser(text, args.input, cc=args.cc,
                                 cflags=args.cflags.split() or None).parse()
        else:
            parser = HeaderParser(text).parse()
    except (ParseError, AnnotationError) as err:
        print("reflect_gen: %s: %s" % (args.input, err), file=sys.stderr)
        return 1
    if not parser.order:
        print("reflect_gen: %s: no typedef struct/union/enum found" % args.input,
              file=sys.stderr)
        return 1

    if args.layout:
        wanted = args.type or sorted(parser.reports)
        missing = [name for name in wanted if name not in parser.reports]
        if missing:
            print("reflect_gen: --layout: no layout for %s" % ", ".join(missing),
                  file=sys.stderr)
            return 1
        sys.stdout.write(render_layout({n: parser.reports[n] for n in wanted}, as_json=args.json))
        return 0

    try:
        selected, external = select_types(parser, ann, args.type, args.external)
    except ParseError as err:
        print("reflect_gen: %s: %s" % (args.input, err), file=sys.stderr)
        return 1
    if not selected:
        print("reflect_gen: %s: nothing selected - annotate types with "
              "/* reflect: type=NAME */ or pass --type NAME" % args.input,
              file=sys.stderr)
        return 1
    try:
        check_closure(parser, ann, selected, external)
        body_c, body_h = generate(parser, os.path.basename(args.input), ann, selected,
                                  fingerprint=getattr(parser, "fingerprint", None) or None,
                                  layout=getattr(parser, "layout", None) or None)
    except ParseError as err:
        print("reflect_gen: %s: %s" % (args.input, err), file=sys.stderr)
        return 1
    stub = args.output or (os.path.splitext(args.input)[0] + "_reflect")
    for path in (stub + ".c", stub + ".h"):
        if os.path.abspath(path) == os.path.abspath(args.input):
            print("reflect_gen: output %s would overwrite the input header" % path,
                  file=sys.stderr)
            return 1
    with open(stub + ".c", "w", encoding="utf-8") as handle:
        handle.write(body_c)
    with open(stub + ".h", "w", encoding="utf-8") as handle:
        handle.write(body_h)
    if not args.quiet:
        note = "" if not external else " (%d external)" % len(external)
        print("reflect_gen: %d type(s) -> %s.h, %s.c%s"
              % (len(selected), stub, stub, note), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
