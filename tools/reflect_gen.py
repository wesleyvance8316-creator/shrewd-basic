#!/usr/bin/env python3
"""Generate shrewd-basic reflect descriptor definitions from a C header.

Parses the supported subset of typedef struct/enum declarations and emits
the very same SHREWD_BASIC_REFLECT_* macros a human would write, so the
generated code stays readable and hand-editable. Multi-dimension arrays
gain ARRAY_DEFINE row types (one per row level, innermost first).
Unsupported constructs (bit-fields, unions, function pointers) are
skipped with a warning.

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


def array_row_defines(struct_name, member, base, dims):
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
                       % (name, ref or primitive_ref(base) or descriptor_name(base),
                          elem_type, count))
        ref = name
        elem_type = "%s[%d]" % (elem_type, count)
    return defines, ref


def field_macro(parser, struct_name, member, members):
    """Return (macro, warning, defines); defines are statements that must
    precede the TYPE_DEFINE of the owning struct."""
    base, stars, name, dims, flexible, bits = member_parts(member)
    if bits is not None:
        unit_ref, unit_offset, unit_size, bit_offset, bit_width = bits
        return ("SHREWD_BASIC_REFLECT_FIELD_BITS_UNIT(%s, %s, %s, %d, %d, %d, %d)"
                % (struct_name, name, unit_ref, unit_offset, unit_size, bit_offset, bit_width),
                None, [])
    base = normalize(base)
    if flexible:
        if stars != "":
            raise ParseError("pointer flexible array not described: %s" % name)
        count_member = find_count_member(members, name)
        if count_member is None:
            return None, ("no count member found for flexible array '%s' "
                          "(expected %s_count/%s_len/count/len) - skipped"
                          % (name, name, name)), []
        ref = primitive_ref(base)
        if ref is None:
            if base in parser.enums or base in parser.structs:
                ref = descriptor_name(base)
            else:
                return None, ("unknown element type '%s' of flexible array "
                              "'%s' - skipped" % (base, name)), []
        return "SHREWD_BASIC_REFLECT_FIELD_FLEX(%s, %s, %s, %s)" % (
            struct_name, name, count_member, ref), None, []
    if 2 <= len(dims):
        # Multi-dimension array: rows become array value types.
        if stars != "":
            raise ParseError("pointer multi-dimension array not described: %s" % name)
        if base not in PRIMITIVE_TYPES and base not in parser.enums \
                and base not in parser.structs:
            return None, ("unknown element type '%s' of multi-dimension array "
                          "'%s' - skipped" % (base, name)), []
        defines, ref = array_row_defines(struct_name, name, base, dims)
        return "SHREWD_BASIC_REFLECT_FIELD_ARRAY(%s, %s, %s)" % (
            struct_name, name, ref), None, defines
    if stars not in ("", "*"):
        raise ParseError("only single pointers are described: %s" % name)
    is_array = (1 == len(dims))
    ref = None
    if base == "void" and stars == "*":
        ref = "shrewd_basic_reflect_void"
    elif base == "char" and stars == "*":
        return "SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(%s, %s)" % (struct_name, name), None, []
    elif base in PRIMITIVE_TYPES:
        ref = PRIMITIVE_TYPES[base]
        if stars == "" and not is_array:
            return "SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(%s, %s)" % (struct_name, name), None, []
    elif base in parser.enums:
        ref = descriptor_name(base)
        if stars == "" and not is_array:
            return "SHREWD_BASIC_REFLECT_FIELD_ENUM(%s, %s, %s)" % (
                struct_name, name, ref), None, []
    else:
        ref = descriptor_name(base)
        if stars == "" and not is_array:
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


def scan_typedef_names(text):
    """Tolerant brace-counting scan for `typedef struct|union|enum {...} name;`
    declarations - unlike HeaderParser it survives bit-fields and nested
    braces. Returns [(kind, name)] in declaration order (kind is
    'struct'/'union'/'enum')."""
    text = strip_comments(text)
    results = []
    depth = 0
    group_kinds = []  # kind per open brace group (None when not a typedef body)
    pending = None
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "{":
            group_kinds.append(pending)
            pending = None
            depth += 1
        elif ch == "}":
            depth -= 1
            kind = group_kinds.pop() if group_kinds else None
            if (0 == depth) and (kind is not None):
                match = re.match(r"\s*([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*;", text[i + 1:])
                if match is not None:
                    for nm in re.split(r"\s*,\s*", match.group(1)):
                        results.append((kind, nm))
                    i += 1 + match.end()
                    continue
        elif 0 == depth:
            match = re.match(r"typedef\s+(struct|union|enum)\b", text[i:])
            if match is not None:
                pending = match.group(1)
                i += match.end()
                continue
            if ch == ";":
                pending = None
        i += 1
    return results


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
        self.enums = {}
        self.order = []
        self.layout = {}      # name -> {"size", "members": [(member, offset, is_bit)]}
        self.reports = {}     # name -> list of report entries (for --layout)
        self.fingerprint = []
        self.warnings = []
        self._ptr_size = 8
        self._canonical = {}  # struct/enum DIE offset -> descriptor base name

    def parse(self):
        wanted = [(kind, nm) for kind, nm in scan_typedef_names(self.text)
                  if kind in ("struct", "enum")]
        if not wanted:
            raise ParseError("no typedef struct/enum found")
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
                self._read_struct(name, leaf)
            elif "DW_TAG_enumeration_type" == leaf.tag:
                self._read_enum(name, leaf)
            else:
                self.warnings.append("typedef %s is not a struct/enum - skipped" % name)
        for warning in self.warnings:
            print("reflect_gen: %s" % warning, file=sys.stderr)

    def _read_struct(self, name, die):
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
        self.structs[name] = members
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
        if "DW_TAG_base_type" == leaf.tag:
            key = DWARF_BASE_KEYS.get(self._attr_name(leaf))
            return key if key in PRIMITIVE_TYPES else None
        if leaf.tag in ("DW_TAG_structure_type", "DW_TAG_enumeration_type"):
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


def generate(parser, input_name, fingerprint=None, layout=None):
    """Emit the stub pair. fingerprint (dwarf backend) is a list of
    comment lines pinning the toolchain; layout is {name: {"size": N,
    "members": [(member, offset, is_bit)]}} for the _Static_assert block
    that fails the build when the described layout drifts."""
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
        for name in parser.order:
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
            pre = []
            for m in parser.structs[name]:
                macro, warning, defines = field_macro(parser, name, m,
                                                      parser.structs[name])
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
            lines_c.append(
                "SHREWD_BASIC_REFLECT_TYPE_DEFINE(%s, %s,\n    %s);"
                % (descriptor_name(name), name, fields))
            lines_h.append("SHREWD_BASIC_REFLECT_TYPE_DECLARE(%s);"
                           % descriptor_name(name))
            # Optional JSON policy skeleton (docs/json.md section 6).
            rows = ",\n    ".join(
                "SHREWD_BASIC_JSON_KEY(%s, \"%s\")" % (m[2], m[2])
                for m in parser.structs[name])
            lines_c.append("")
            lines_c.append("/* Optional JSON policy for %s:" % name)
            lines_c.append("SHREWD_BASIC_JSON_DEFINE(%s_json, %s," % (name, descriptor_name(name)))
            lines_c.append("    %s);" % rows)
            lines_c.append("*/")
        lines_c.append("")
    return "\n".join(lines_c) + "\n", "\n".join(lines_h) + "\n"


def render_layout(reports):
    """pahole-lite: per-field offset/size/align, padding intervals, and
    the gap between total size and the packed footprint."""
    out = []
    for name in reports:
        info = reports[name]
        out.append("%s: %d bytes, align %d" % (name, info["size"], info["align"]))
        covered = []  # byte intervals covered by members (units may overlap)
        cursor = 0
        for entry in info["entries"]:
            start = entry["offset"]
            end = entry["offset"] + entry["size"]
            gap = start - cursor
            if 0 < gap:
                out.append("  padding [%d..%d)  %d byte(s)" % (cursor, start, gap))
            if entry["bits"] is not None:
                out.append("  %-12s off=%-5d bits=[%d..%d)  unit %d bytes"
                           % (entry["name"], entry["offset"], entry["bits"][0],
                              entry["bits"][0] + entry["bits"][1], entry["size"]))
            else:
                out.append("  %-12s off=%-5d size=%-5d align=%-4d %s"
                           % (entry["name"], entry["offset"], entry["size"],
                              entry["align"], entry["type"]))
            covered.append((start, end))
            cursor = max(cursor, end)
        gap = info["size"] - cursor
        if 0 < gap:
            out.append("  padding [%d..%d)  %d byte(s)" % (cursor, info["size"], gap))
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
        out.append("  packed %d bytes, %d byte(s) padding"
                   % (packed, info["size"] - packed))
        out.append("")
    return "\n".join(out) + "\n"


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
    ap.add_argument("--layout", action="store_true",
                    help="dwarf backend: print a pahole-lite layout report "
                         "instead of generating stubs")
    ap.add_argument("--quiet", action="store_true", help="suppress the summary")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8") as handle:
        text = handle.read()
    try:
        if "dwarf" == args.backend:
            parser = DwarfParser(text, args.input, cc=args.cc,
                                 cflags=args.cflags.split() or None).parse()
        else:
            parser = HeaderParser(text).parse()
    except ParseError as err:
        print("reflect_gen: %s: %s" % (args.input, err), file=sys.stderr)
        return 1
    if not parser.order:
        print("reflect_gen: %s: no typedef struct/enum found" % args.input,
              file=sys.stderr)
        return 1

    if args.layout:
        sys.stdout.write(render_layout(parser.reports))
        return 0

    stub = args.output or os.path.splitext(os.path.basename(args.input))[0]
    body_c, body_h = generate(parser, os.path.basename(args.input),
                              fingerprint=getattr(parser, "fingerprint", None) or None,
                              layout=getattr(parser, "layout", None) or None)
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
