#!/usr/bin/env python3
"""Generate shrewd-basic reflect descriptor definitions from a C header.

Parses the supported subset of typedef struct/enum declarations and emits
the very same SHREWD_BASIC_REFLECT_* macros a human would write, so the
generated code stays readable and hand-editable. Unsupported constructs
(bit-fields, unions, function pointers, multi-dimension arrays, flexible
arrays) are skipped with a warning.

Usage:
    python3 reflect_gen.py input.h [-o stub] [--quiet]

Writes stub.h (TYPE_DECLARE for every type) and stub.c (the DEFINEs);
the stub defaults to the input file's base name. The library itself never
needs this tool - it is an optional authoring aid.
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
    """Return (base_type, stars, name, array_len or None, flexible)."""
    decl = " ".join(decl.split())
    array_len = None
    flexible = False
    match = re.match(r"^(.*\S)\s*\[\s*(\d*)\s*\]$", decl)
    if match:
        decl = match.group(1).strip()
        digits = match.group(2)
        if digits in ("", "0"):
            flexible = True  # C99 FAM or the GNU zero-length form
        else:
            array_len = int(digits)
    match = re.match(r"^(.*\S)\s*(\*+)\s*([A-Za-z_]\w*)$", decl)
    if match:
        return match.group(1).strip(), match.group(2), match.group(3), array_len, flexible
    match = re.match(r"^(.*\S)\s+([A-Za-z_]\w*)$", decl)
    if match:
        return match.group(1).strip(), "", match.group(2), array_len, flexible
    raise ParseError("cannot parse member: %s" % decl)


def normalize(base):
    base = base.replace("const ", " ").replace("volatile ", " ")
    return " ".join(base.split())


class HeaderParser:
    def __init__(self, text):
        self.text = strip_comments(text)
        self.structs = {}  # typedef name -> [(base, stars, name, array, flexible)]
        self.enums = {}    # typedef name -> [constant names]
        self.order = []    # declaration order of type names

    def parse(self):
        pattern = re.compile(
            r"typedef\s+(struct|enum)\s*(\w+)?\s*\{(.*?)\}\s*(\w+)\s*;",
            re.S)
        for match in pattern.finditer(self.text):
            kind, _tag, body, name = match.groups()
            if kind == "struct":
                self.structs[name] = self._parse_struct(body)
            else:
                self.enums[name] = self._parse_enum(body)
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


def field_macro(parser, struct_name, member, members):
    base, stars, name, array_len, flexible = member
    base = normalize(base)
    if flexible:
        if stars != "":
            raise ParseError("pointer flexible array not described: %s" % name)
        count_member = find_count_member(members, name)
        if count_member is None:
            return None, ("no count member found for flexible array '%s' "
                          "(expected %s_count/%s_len/count/len) - skipped"
                          % (name, name, name))
        ref = primitive_ref(base)
        if ref is None:
            if base in parser.enums or base in parser.structs:
                ref = descriptor_name(base)
            else:
                return None, ("unknown element type '%s' of flexible array "
                              "'%s' - skipped" % (base, name))
        return "SHREWD_BASIC_REFLECT_FIELD_FLEX(%s, %s, %s, %s)" % (
            struct_name, name, count_member, ref), None
    if stars not in ("", "*"):
        raise ParseError("only single pointers are described: %s" % name)
    ref = None
    if base == "void" and stars == "*":
        ref = "shrewd_basic_reflect_void"
    elif base == "char" and stars == "*":
        return "SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(%s, %s)" % (struct_name, name), None
    elif base in PRIMITIVE_TYPES:
        ref = PRIMITIVE_TYPES[base]
        if stars == "" and array_len is None:
            return "SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(%s, %s)" % (struct_name, name), None
    elif base in parser.enums:
        ref = descriptor_name(base)
        if stars == "" and array_len is None:
            return "SHREWD_BASIC_REFLECT_FIELD_ENUM(%s, %s, %s)" % (
                struct_name, name, ref), None
    else:
        ref = descriptor_name(base)
        if stars == "" and array_len is None:
            return "SHREWD_BASIC_REFLECT_FIELD_STRUCT(%s, %s, %s)" % (
                struct_name, name, ref), None
    if ref is None:
        raise ParseError("unsupported member type: %s %s" % (base, name))
    if stars == "*":
        macro = "SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY" if array_len is not None \
            else "SHREWD_BASIC_REFLECT_FIELD_PTR"
    else:
        macro = "SHREWD_BASIC_REFLECT_FIELD_ARRAY"
    return "%s(%s, %s, %s)" % (macro, struct_name, name, ref), None


def collect_references(parser):
    refs = []
    for struct_name, members in parser.structs.items():
        for member in members:
            base = normalize(member[0])
            if (base not in PRIMITIVE_TYPES and base != "void"
                    and base not in parser.structs and base not in parser.enums
                    and (descriptor_name(base), base) not in refs):
                refs.append((descriptor_name(base), base))
    return refs


def generate(parser, input_name):
    lines_c = [
        "/* Generated by tools/reflect_gen.py from %s - do not edit. */" % input_name,
        "",
        '#include "%s"' % input_name,
        "",
        "#include \"shrewd-basic/reflect/reflect.h\"",
        "",
    ]
    lines_h = [
        "/* Generated by tools/reflect_gen.py from %s - do not edit. */" % input_name,
        "",
        "#include \"shrewd-basic/reflect/reflect.h\"",
        "",
    ]
    for var, base in collect_references(parser):
        lines_c.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);" % var)
        lines_h.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);" % var)
    if collect_references(parser):
        lines_c.append("")
    for name in parser.order:
        if name in parser.enums:
            entries = ",\n    ".join(
                "SHREWD_BASIC_REFLECT_ENUM_VALUE(%s)" % c for c in parser.enums[name])
            lines_c.append(
                "SHREWD_BASIC_REFLECT_ENUM_DEFINE(%s, %s,\n    %s);"
                % (descriptor_name(name), name, entries))
            lines_h.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);"
                           % descriptor_name(name))
        else:
            entries = []
            for m in parser.structs[name]:
                macro, warning = field_macro(parser, name, m, parser.structs[name])
                if warning is not None:
                    print("reflect_gen: %s: %s" % (name, warning), file=sys.stderr)
                if macro is not None:
                    entries.append(macro)
            if not entries:
                print("reflect_gen: %s: no describable fields - skipped" % name,
                      file=sys.stderr)
                continue
            fields = ",\n    ".join(entries)
            lines_c.append(
                "SHREWD_BASIC_REFLECT_TYPE_DEFINE(%s, %s,\n    %s);"
                % (descriptor_name(name), name, fields))
            lines_h.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);"
                           % descriptor_name(name))
        lines_c.append("")
    return "\n".join(lines_c) + "\n", "\n".join(lines_h) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input", help="header to parse")
    ap.add_argument("-o", "--output", help="output stub (writes STUB.h and STUB.c)")
    ap.add_argument("--quiet", action="store_true", help="suppress the summary")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8") as handle:
        text = handle.read()
    try:
        parser = HeaderParser(text).parse()
    except ParseError as err:
        print("reflect_gen: %s: %s" % (args.input, err), file=sys.stderr)
        return 1
    if not parser.order:
        print("reflect_gen: %s: no typedef struct/enum found" % args.input,
              file=sys.stderr)
        return 1

    stub = args.output or os.path.splitext(os.path.basename(args.input))[0]
    body_c, body_h = generate(parser, os.path.basename(args.input))
    with open(stub + ".c", "w", encoding="utf-8") as handle:
        handle.write(body_c)
    with open(stub + ".h", "w", encoding="utf-8") as handle:
        handle.write(body_h)
    if not args.quiet:
        print("reflect_gen: %d type(s) -> %s.h, %s.c"
              % (len(parser.order), stub, stub), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
