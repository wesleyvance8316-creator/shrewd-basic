#include "shrewd-basic/reflect/reflect.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared reflect log module, defined in library/reflect/reflect.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_reflect_log);

#define DUMP_INDENT 2            /* spaces per nesting level */
#define DUMP_MAX_DEPTH 16        /* indentation clamp (defensive) */
#define DUMP_STR_MAX 256         /* raw string bytes before truncation */
#define DUMP_ARRAY_MAX 32        /* elements rendered per array line */
#define DUMP_TEXT_MAX 256        /* ops->render text buffer */
#define DUMP_BODY_MAX (1u << 20) /* hard cap on one dump body (1 MiB) */
#define DUMP_BODY_MIN 1024       /* initial body buffer */

/*
 * The whole dump is one log record: the body is accumulated in a growable
 * buffer and written through a single shrewd_basic_log() call, so only the
 * first line carries the log prefix and the dump itself starts on the next
 * line (unprefixed). The buffer is shared across recursion - a line is
 * always completed before the next is built - and grows on demand; when it
 * cannot grow further the output is truncated with a marker.
 */
typedef struct dump_ctx {
    char *buf;
    size_t cap;
    size_t pos;
    int truncated;
    int nomem;
    struct {
        const void *object;
        const shrewd_basic_reflect_type_t *type;
    } path[DUMP_MAX_DEPTH]; /* objects on the render stack */
} dump_ctx_t;

/*
 * Mark the output as cut and stop appending. The marker is guaranteed
 * to fit: when the buffer is nearly full the tail is rewound to make
 * room for it.
 */
static void dump_truncate(dump_ctx_t *ctx) {
    static const char marker[] = " ...(dump truncated)";
    size_t at = (ctx->cap > sizeof(marker)) ? (ctx->cap - sizeof(marker)) : 0;
    if (at < ctx->pos) {
        ctx->pos = at;
    }
    memcpy(ctx->buf + ctx->pos, marker, sizeof(marker));
    ctx->pos += sizeof(marker) - 1;
    ctx->truncated = 1;
}

static int dump_init(dump_ctx_t *ctx) {
    void *block = NULL;
    ctx->buf = NULL;
    ctx->cap = 0;
    ctx->pos = 0;
    ctx->truncated = 0;
    ctx->nomem = 0;
    /* Zeroed frames: walks may skip levels (union variants), and
       target_seen must not read uninitialised slots. */
    memset(ctx->path, 0, sizeof(ctx->path));
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, DUMP_BODY_MIN)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    ctx->buf = block;
    ctx->cap = DUMP_BODY_MIN;
    ctx->buf[0] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

static void dump_append(dump_ctx_t *ctx, const char *fmt, ...) {
    if (0 != ctx->truncated) {
        return;
    }
    va_list ap;
    va_list copy;
    va_start(ap, fmt);
    va_copy(copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (0 > needed) {
        va_end(copy);
        return;
    }
    size_t want = ctx->pos + (size_t)needed + 1;
    if (want > ctx->cap) {
        size_t cap = (ctx->cap * 2 > want) ? (ctx->cap * 2) : want;
        if (cap > DUMP_BODY_MAX) {
            cap = DUMP_BODY_MAX;
        }
        if (want > cap) {
            /* The hard cap: keep what fits and stop building. */
            dump_truncate(ctx);
            va_end(copy);
            return;
        }
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, ctx->buf, cap)) {
            ctx->nomem = 1;
            dump_truncate(ctx);
            va_end(copy);
            return;
        }
        ctx->buf = block;
        ctx->cap = cap;
    }
    (void)vsnprintf(ctx->buf + ctx->pos, ctx->cap - ctx->pos, fmt, copy);
    va_end(copy);
    ctx->pos += (size_t)needed;
}

static void dump_emit(dump_ctx_t *ctx) {
    dump_append(ctx, "\n");
}

/* Escape @p len raw bytes into the body (shared by strings and buffers). */
static void dump_escaped(dump_ctx_t *ctx, const unsigned char *text, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        switch (text[i]) {
        case '\\':
            dump_append(ctx, "\\\\");
            break;
        case '"':
            dump_append(ctx, "\\\"");
            break;
        case '\n':
            dump_append(ctx, "\\n");
            break;
        case '\r':
            dump_append(ctx, "\\r");
            break;
        case '\t':
            dump_append(ctx, "\\t");
            break;
        default:
            if ((' ' <= text[i]) && ('~' >= text[i])) {
                dump_append(ctx, "%c", text[i]);
            } else {
                dump_append(ctx, "\\x%02x", (unsigned int)text[i]);
            }
            break;
        }
    }
}

static void dump_string(dump_ctx_t *ctx, const char *text, size_t str_max) {
    if (NULL == text) {
        dump_append(ctx, "(null)");
        return;
    }
    size_t len = reflect_strn_len(text, str_max);
    dump_append(ctx, "\"");
    dump_escaped(ctx, (const unsigned char *)text, len);
    if ((len >= str_max) && ('\0' != text[len])) {
        dump_append(ctx, "...(truncated)");
    }
    dump_append(ctx, "\"");
}

/*
 * Render a wide string (or wide buffer) by code unit: ASCII escapes as
 * in dump_escaped, every other unit becomes a \uXXXX / \UXXXXXXXX
 * escape. @p max_units bounds the scan (SIZE_MAX for unbounded
 * pointer strings).
 */
static void dump_wide(
    dump_ctx_t *ctx, const void *text, size_t unit, size_t max_units, int bounded) {
    const unsigned char *bytes = (const unsigned char *)text;
    size_t len = 0;
    if (NULL == text) {
        dump_append(ctx, "(null)");
        return;
    }
    len = reflect_wstrn_len(text, unit, max_units);
    dump_append(ctx, "\"");
    for (size_t i = 0; i < len; ++i) {
        uint32_t code = 0;
        memcpy(&code, bytes + (i * unit), unit);
        if ((0x20u <= code) && (0x7eu >= code) && ('\\' != code) && ('"' != code)) {
            dump_append(ctx, "%c", (char)code);
        } else if ('\\' == code) {
            dump_append(ctx, "\\\\");
        } else if ('"' == code) {
            dump_append(ctx, "\\\"");
        } else if ('\n' == code) {
            dump_append(ctx, "\\n");
        } else if ('\r' == code) {
            dump_append(ctx, "\\r");
        } else if ('\t' == code) {
            dump_append(ctx, "\\t");
        } else if (2 == unit) {
            dump_append(ctx, "\\u%04x", (unsigned int)code);
        } else {
            dump_append(ctx, "\\U%08x", (unsigned int)code);
        }
    }
    if ((0 != bounded) && (len >= max_units)
        && (0 != reflect_wstrn_len(bytes + (len * unit), unit, 1))) {
        dump_append(ctx, "...(truncated)");
    }
    dump_append(ctx, "\"");
}

/*
 * Render a string buffer (a F_STRINGBUF char[N] or wide buffer):
 * quoted and escaped up to the first NUL unit, or the whole buffer when
 * unterminated.
 */
static void dump_string_buf(dump_ctx_t *ctx, const void *buf, size_t unit, size_t size) {
    if (1 == unit) {
        size_t len = reflect_strn_len((const char *)buf, size);
        dump_append(ctx, "\"");
        dump_escaped(ctx, (const unsigned char *)buf, len);
        dump_append(ctx, "\"");
        return;
    }
    dump_wide(ctx, buf, unit, size, 0);
}

/* Render one value inline (scalars, array values, struct values). */
static void dump_inline(dump_ctx_t *ctx,
                        const shrewd_basic_reflect_type_t *type,
                        const unsigned char *bytes,
                        size_t nbytes,
                        size_t str_max);

/* Render one scalar value; str_max bounds string elements inside arrays. */
static void dump_scalar(dump_ctx_t *ctx,
                        const shrewd_basic_reflect_type_t *type,
                        const unsigned char *bytes,
                        size_t nbytes,
                        size_t str_max) {
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        dump_append(ctx, "%s", (0 != reflect_load_u64(bytes, nbytes)) ? "true" : "false");
        break;
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char c = '\0';
        memcpy(&c, bytes, 1);
        if ((' ' <= (unsigned char)c) && ('~' >= (unsigned char)c)) {
            dump_append(ctx, "'%c'", c);
        } else {
            dump_append(ctx, "'\\x%02x'", (unsigned int)(unsigned char)c);
        }
        break;
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
        dump_append(ctx, "%lld", (long long)reflect_load_i64(bytes, nbytes));
        break;
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        dump_append(ctx, "%llu", (unsigned long long)reflect_load_u64(bytes, nbytes));
        break;
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        /* Render by constant name (inline lookup: no log noise while
           printing); unknown values show their number. */
        int64_t value = reflect_load_i64(bytes, nbytes);
        const char *name = NULL;
        for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
            if (type->sb_refl_enum[i].sb_refl_value == value) {
                name = type->sb_refl_enum[i].sb_refl_name;
                break;
            }
        }
        if (NULL != name) {
            dump_append(ctx, "%s", name);
        } else {
            dump_append(ctx, "%lld", (long long)value);
        }
        break;
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
        dump_append(ctx, "%g", reflect_load_f64(bytes, nbytes));
        break;
    case SHREWD_BASIC_REFLECT_KIND_STRING:
        dump_string(ctx, reflect_load_str(bytes), str_max);
        break;
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
    case SHREWD_BASIC_REFLECT_KIND_STRING32:
        dump_wide(
            ctx, reflect_load_ptr(bytes), reflect_string_unit(type->sb_refl_kind), str_max, 1);
        break;
    case SHREWD_BASIC_REFLECT_KIND_ARRAY: {
        /* Row of a multi-dimension array: [ e, e, ... ] per level. */
        size_t count = type->sb_refl_elem_count;
        size_t stride = type->sb_refl_elem_stride;
        dump_append(ctx, "[ ");
        for (size_t i = 0; i < count; ++i) {
            if (DUMP_ARRAY_MAX == i) {
                dump_append(ctx, "...(%zu more)", count - i);
                break;
            }
            if (0 != i) {
                dump_append(ctx, ", ");
            }
            dump_inline(ctx, type->sb_refl_elem, bytes + (i * stride), stride, str_max);
        }
        dump_append(ctx, " ]");
        break;
    }
    default:
        dump_append(ctx, "<kind %d>", (int)type->sb_refl_kind);
        break;
    }
}

static void dump_inline(dump_ctx_t *ctx,
                        const shrewd_basic_reflect_type_t *type,
                        const unsigned char *bytes,
                        size_t nbytes,
                        size_t str_max) {
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        dump_scalar(ctx, type, bytes, nbytes, str_max);
        return;
    }
    /* Struct value inline: { x = 1, y = 2 }. */
    dump_append(ctx, "{ ");
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        if (0 != i) {
            dump_append(ctx, ", ");
        }
        dump_append(ctx, "%s = ", field->sb_refl_name);
        dump_inline(
            ctx, field->sb_refl_type, bytes + field->sb_refl_offset, field->sb_refl_size, str_max);
    }
    dump_append(ctx, " }");
}

/*
 * Render every field of `object`. A F_PTR slot is followed (the pointee
 * is rendered; NULL shows "(null)", opaque targets show their address).
 * Re-entering an object already on the render stack - a cycle in the
 * object graph - stops with " ...(cyclic)"; the recursion is also
 * hard-capped at DUMP_MAX_DEPTH (" ...(max depth)") as a last resort.
 */
/*
 * A re-entry is only a cycle when the same object reappears as the same
 * type: an embedded first member shares its parent's address (offset 0)
 * without being a cycle.
 */
static int dump_target_seen(const dump_ctx_t *ctx,
                            size_t depth,
                            const void *target,
                            const shrewd_basic_reflect_type_t *type) {
    for (size_t i = 0; i < depth; ++i) {
        if ((ctx->path[i].object == target) && (ctx->path[i].type == type)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Render a followed pointer target through the effective ops->render
 * (field-level override wins). Returns 1 when a text was produced;
 * otherwise the caller falls back to the default rendering.
 */
static int dump_render_target(const shrewd_basic_reflect_field_t *field,
                              const void *pointer,
                              char *buf,
                              size_t buf_size) {
    const shrewd_basic_reflect_ops_t *ops = reflect_field_ops(field);
    if ((NULL == ops) || (NULL == ops->render)) {
        return 0;
    }
    return (SHREWD_BASIC_ERR_OK == ops->render(pointer, buf, buf_size)) ? 1 : 0;
}

static void dump_field_list(dump_ctx_t *ctx,
                            const shrewd_basic_reflect_field_t *fields,
                            size_t count,
                            const unsigned char *object,
                            size_t depth);

static void dump_fields(dump_ctx_t *ctx,
                        const shrewd_basic_reflect_type_t *type,
                        const unsigned char *object,
                        size_t depth) {
    ctx->path[depth - 1].object = object;
    ctx->path[depth - 1].type = type;
    dump_field_list(ctx, type->sb_refl_fields, type->sb_refl_field_count, object, depth);
}

/* Render one field list - a struct's fields or an active union variant's. */
static void dump_field_list(dump_ctx_t *ctx,
                            const shrewd_basic_reflect_field_t *fields,
                            size_t count,
                            const unsigned char *object,
                            size_t depth) {
    int pad = (int)(((depth < DUMP_MAX_DEPTH) ? depth : DUMP_MAX_DEPTH) * DUMP_INDENT);
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const unsigned char *at = object + field->sb_refl_offset;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            /*
             * Tagged union: the active variant's fields render as a block
             * annotated with the tag constant (raw tag when unknown).
             */
            const shrewd_basic_reflect_variant_t *variant = reflect_union_variant(field, at);
            if (NULL == variant) {
                long long tag = (long long)reflect_load_i64(object + field->sb_refl_limit_offset,
                                                            field->sb_refl_limit_size);
                dump_append(ctx, "%*s%s = (tag %lld: unknown)", pad, "", field->sb_refl_name, tag);
                dump_emit(ctx);
                continue;
            }
            dump_append(ctx, "%*s%s = (%s) {", pad, "", field->sb_refl_name, variant->sb_refl_name);
            dump_emit(ctx);
            dump_field_list(
                ctx, variant->sb_refl_fields, variant->sb_refl_field_count, at, depth + 1);
            dump_append(ctx, "%*s}", pad, "");
            dump_emit(ctx);
            continue;
        }
        int is_array = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY));
        int is_darray = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY));
        int is_ptr = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR));
        int is_struct = (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind);
        int is_opaque = (SHREWD_BASIC_REFLECT_KIND_OPAQUE == field->sb_refl_type->sb_refl_kind);

        if ((0 != is_array)
            && (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))) {
            /* Text buffer: a quoted C string, not an element list. */
            dump_append(ctx, "%*s%s = ", pad, "", field->sb_refl_name);
            dump_string_buf(ctx, at, field->sb_refl_stride, field->sb_refl_count);
            dump_emit(ctx);
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
            /* Bit group: the extracted bits, not the whole unit. */
            uint64_t unit = reflect_load_u64(at, field->sb_refl_size);
            dump_append(ctx,
                        "%*s%s = %llu",
                        pad,
                        "",
                        field->sb_refl_name,
                        (unsigned long long)reflect_bits_extract(
                            unit, field->sb_refl_bit_offset, field->sb_refl_bit_width));
            dump_emit(ctx);
            continue;
        }
        if ((0 != is_array) || (0 != is_darray)) {
            /* Inline or dynamic array of values or of pointers. */
            reflect_array_view_t view = reflect_array_view(object, field);
            if (NULL == view.base) {
                dump_append(ctx, "%*s%s = (null)", pad, "", field->sb_refl_name);
                dump_emit(ctx);
                continue;
            }
            if (0 != is_struct) {
                /* Struct elements (or struct pointers): one block each. */
                for (size_t j = 0; j < view.count; ++j) {
                    const unsigned char *elem = view.base + (j * view.stride);
                    const unsigned char *target = elem;
                    if (0 != is_ptr) {
                        void *pointer = reflect_load_ptr(elem);
                        if (NULL == pointer) {
                            dump_append(
                                ctx, "%*s%s[%zu] = (null)", pad, "", field->sb_refl_name, j);
                            dump_emit(ctx);
                            continue;
                        }
                        char text[DUMP_TEXT_MAX] = {0};
                        if (0 != dump_render_target(field, pointer, text, sizeof(text))) {
                            dump_append(
                                ctx, "%*s%s[%zu] = %s", pad, "", field->sb_refl_name, j, text);
                            dump_emit(ctx);
                            continue;
                        }
                        target = (const unsigned char *)pointer;
                    }
                    if (0 != dump_target_seen(ctx, depth, target, field->sb_refl_type)) {
                        dump_append(
                            ctx, "%*s%s[%zu] = ...(cyclic)", pad, "", field->sb_refl_name, j);
                        dump_emit(ctx);
                        continue;
                    }
                    if (depth >= DUMP_MAX_DEPTH) {
                        dump_append(
                            ctx, "%*s%s[%zu] = ...(max depth)", pad, "", field->sb_refl_name, j);
                        dump_emit(ctx);
                        continue;
                    }
                    dump_append(ctx, "%*s%s[%zu] = {", pad, "", field->sb_refl_name, j);
                    dump_emit(ctx);
                    dump_fields(ctx, field->sb_refl_type, target, depth + 1);
                    dump_append(ctx, "%*s}", pad, "");
                    dump_emit(ctx);
                }
            } else {
                /* Scalar/string elements, possibly behind pointers. */
                dump_append(ctx, "%*s%s = [ ", pad, "", field->sb_refl_name);
                for (size_t j = 0; j < view.count; ++j) {
                    if (DUMP_ARRAY_MAX == j) {
                        dump_append(ctx, "...(%zu more)", view.count - j);
                        break;
                    }
                    if (0 != j) {
                        dump_append(ctx, ", ");
                    }
                    const unsigned char *elem = view.base + (j * view.stride);
                    if (0 != is_ptr) {
                        void *pointer = reflect_load_ptr(elem);
                        if (NULL == pointer) {
                            dump_append(ctx, "(null)");
                        } else {
                            char text[DUMP_TEXT_MAX] = {0};
                            if (0 != dump_render_target(field, pointer, text, sizeof(text))) {
                                dump_append(ctx, "%s", text);
                            } else if (0 != is_opaque) {
                                dump_append(ctx, "0x%llx", (unsigned long long)(uintptr_t)pointer);
                            } else {
                                dump_scalar(ctx,
                                            field->sb_refl_type,
                                            (const unsigned char *)pointer,
                                            field->sb_refl_type->sb_refl_size,
                                            DUMP_STR_MAX / 8);
                            }
                        }
                    } else {
                        dump_scalar(ctx, field->sb_refl_type, elem, view.stride, DUMP_STR_MAX / 8);
                    }
                }
                dump_append(ctx, " ]");
                dump_emit(ctx);
            }
        } else if (0 != is_ptr) {
            void *pointer = reflect_load_ptr(at);
            if (NULL == pointer) {
                dump_append(ctx, "%*s%s = (null)", pad, "", field->sb_refl_name);
                dump_emit(ctx);
            } else {
                char text[DUMP_TEXT_MAX] = {0};
                if (0 != dump_render_target(field, pointer, text, sizeof(text))) {
                    dump_append(ctx, "%*s%s = %s", pad, "", field->sb_refl_name, text);
                    dump_emit(ctx);
                } else if (0 != is_opaque) {
                    dump_append(ctx,
                                "%*s%s = 0x%llx",
                                pad,
                                "",
                                field->sb_refl_name,
                                (unsigned long long)(uintptr_t)pointer);
                    dump_emit(ctx);
                } else if (0 != is_struct) {
                    if (0 != dump_target_seen(ctx, depth, pointer, field->sb_refl_type)) {
                        dump_append(ctx, "%*s%s = ...(cyclic)", pad, "", field->sb_refl_name);
                        dump_emit(ctx);
                    } else if (depth >= DUMP_MAX_DEPTH) {
                        dump_append(ctx, "%*s%s = ...(max depth)", pad, "", field->sb_refl_name);
                        dump_emit(ctx);
                    } else {
                        dump_append(ctx, "%*s%s = {", pad, "", field->sb_refl_name);
                        dump_emit(ctx);
                        dump_fields(
                            ctx, field->sb_refl_type, (const unsigned char *)pointer, depth + 1);
                        dump_append(ctx, "%*s}", pad, "");
                        dump_emit(ctx);
                    }
                } else {
                    dump_append(ctx, "%*s%s = ", pad, "", field->sb_refl_name);
                    dump_scalar(ctx,
                                field->sb_refl_type,
                                (const unsigned char *)pointer,
                                field->sb_refl_type->sb_refl_size,
                                DUMP_STR_MAX);
                    dump_emit(ctx);
                }
            }
        } else if (0 != is_struct) {
            if (0 != dump_target_seen(ctx, depth, at, field->sb_refl_type)) {
                dump_append(ctx, "%*s%s = ...(cyclic)", pad, "", field->sb_refl_name);
                dump_emit(ctx);
            } else if (depth >= DUMP_MAX_DEPTH) {
                dump_append(ctx, "%*s%s = ...(max depth)", pad, "", field->sb_refl_name);
                dump_emit(ctx);
            } else {
                dump_append(ctx, "%*s%s = {", pad, "", field->sb_refl_name);
                dump_emit(ctx);
                dump_fields(ctx, field->sb_refl_type, at, depth + 1);
                dump_append(ctx, "%*s}", pad, "");
                dump_emit(ctx);
            }
        } else {
            dump_append(ctx, "%*s%s = ", pad, "", field->sb_refl_name);
            dump_scalar(ctx, field->sb_refl_type, at, field->sb_refl_size, DUMP_STR_MAX);
            dump_emit(ctx);
        }
    }
}

int shrewd_basic_reflect_dump_write(const shrewd_basic_reflect_type_t *type,
                                    const void *object,
                                    const char *label,
                                    const char *file,
                                    int line,
                                    const char *func) {
    if ((NULL == type) || (NULL == object)) {
        shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_WARN,
                               shrewd_basic_reflect_log,
                               "dump",
                               file,
                               line,
                               func,
                               "dump: type or object is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_WARN,
                               shrewd_basic_reflect_log,
                               "dump",
                               file,
                               line,
                               func,
                               "dump: type \"%s\" is not a struct\n",
                               type->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    dump_ctx_t ctx = {0};
    if (SHREWD_BASIC_ERR_OK != dump_init(&ctx)) {
        shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_ERROR,
                               shrewd_basic_reflect_log,
                               "dump",
                               file,
                               line,
                               func,
                               "dump: allocation failed\n");
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if (NULL != label) {
        dump_append(&ctx, "%s (%s) {", label, type->sb_refl_name);
    } else {
        dump_append(&ctx, "%s {", type->sb_refl_name);
    }
    dump_emit(&ctx);
    dump_fields(&ctx, type, (const unsigned char *)object, 1);
    dump_append(&ctx, "}");
    dump_emit(&ctx);
    /* One log record: the prefix line, then the dump body from the next
       line on - unprefixed. */
    shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_INFO,
                           shrewd_basic_reflect_log,
                           "dump",
                           file,
                           line,
                           func,
                           "\n%s",
                           ctx.buf);
    int status = (0 != ctx.nomem) ? SHREWD_BASIC_ERR_MEM_NOMEM : SHREWD_BASIC_ERR_OK;
    (void)shrewd_basic_platform_free((void **)&ctx.buf);
    return status;
}

/* --- type self-print -------------------------------------------------- */

static void describe_field(dump_ctx_t *ctx,
                           const char *indent,
                           const shrewd_basic_reflect_field_t *field) {
    char shape[64] = {0};
    const char *base = (NULL != field->sb_refl_type) ? field->sb_refl_type->sb_refl_name : "?";
    const char *ptr = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) ? "*" : "";
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL))) {
        snprintf(shape, sizeof(shape), "%s%s[]", ptr, base);
    } else if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
        snprintf(shape, sizeof(shape), "%s%s[%zu]", ptr, base, field->sb_refl_count);
    } else {
        snprintf(shape, sizeof(shape), "%s%s", ptr, base);
    }
    dump_append(ctx,
                "%s%-12s %-12s off=%-4zu size=%-4zu align=%zu",
                indent,
                field->sb_refl_name,
                shape,
                field->sb_refl_offset,
                field->sb_refl_size,
                shrewd_basic_reflect_field_align(field));
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED)) {
        dump_append(ctx, " owned");
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        dump_append(ctx, " stringbuf");
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        dump_append(ctx, " bits(%zu@%zu)", field->sb_refl_bit_width, field->sb_refl_bit_offset);
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
        dump_append(ctx, " union");
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
        dump_append(ctx, " flex");
    }
    dump_emit(ctx);
}

int shrewd_basic_reflect_describe(const shrewd_basic_reflect_type_t *type) {
    dump_ctx_t ctx = {0};
    if (NULL == type) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_reflect_log,
                         "describe",
                         "describe: type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_ERR_OK != dump_init(&ctx)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_reflect_log,
                         "describe",
                         "describe: allocation failed\n");
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    dump_append(&ctx,
                "%s (%s, %zu bytes, align %zu)",
                type->sb_refl_name,
                shrewd_basic_reflect_kind_name(type->sb_refl_kind),
                type->sb_refl_size,
                type->sb_refl_align);
    dump_emit(&ctx);
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_STRUCT:
        for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
            describe_field(&ctx, "  ", &type->sb_refl_fields[i]);
        }
        break;
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
            dump_append(&ctx,
                        "  %s = %lld",
                        type->sb_refl_enum[i].sb_refl_name,
                        (long long)type->sb_refl_enum[i].sb_refl_value);
            dump_emit(&ctx);
        }
        break;
    case SHREWD_BASIC_REFLECT_KIND_ARRAY:
        dump_append(&ctx,
                    "  row of %zu x %s (stride %zu)",
                    type->sb_refl_elem_count,
                    (NULL != type->sb_refl_elem) ? type->sb_refl_elem->sb_refl_name : "?",
                    type->sb_refl_elem_stride);
        dump_emit(&ctx);
        break;
    case SHREWD_BASIC_REFLECT_KIND_UNION:
        for (size_t i = 0; i < type->sb_refl_variant_count; ++i) {
            const shrewd_basic_reflect_variant_t *variant = &type->sb_refl_variants[i];
            dump_append(
                &ctx, "  case %lld (%s):", (long long)variant->sb_refl_tag, variant->sb_refl_name);
            dump_emit(&ctx);
            for (size_t j = 0; j < variant->sb_refl_field_count; ++j) {
                describe_field(&ctx, "    ", &variant->sb_refl_fields[j]);
            }
        }
        break;
    default:
        break; /* primitives and opaque types: header line only */
    }
    /* One log record: the prefix line, then the body unprefixed. */
    shrewd_basic_log(
        SHREWD_BASIC_LOG_LEVEL_INFO, shrewd_basic_reflect_log, "describe", "\n%s", ctx.buf);
    int status = (0 != ctx.nomem) ? SHREWD_BASIC_ERR_MEM_NOMEM : SHREWD_BASIC_ERR_OK;
    (void)shrewd_basic_platform_free((void **)&ctx.buf);
    return status;
}

/* --- layout report ---------------------------------------------------- */

int shrewd_basic_reflect_layout(const shrewd_basic_reflect_type_t *type,
                                shrewd_basic_reflect_layout_fn fn,
                                void *ctx) {
    size_t cursor = 0;
    if ((NULL == type) || (NULL == fn)
        || (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_reflect_log,
                         "layout",
                         "layout: type or fn is NULL, or not a struct\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    /* Emit in ascending (offset, declaration index) order by selecting,
       per rank, the field with exactly `rank` predecessors - no scratch
       allocation needed for a diagnostic walk. */
    for (size_t rank = 0; rank < type->sb_refl_field_count; ++rank) {
        const shrewd_basic_reflect_field_t *pick = NULL;
        for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
            const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
            size_t before = 0;
            for (size_t j = 0; j < type->sb_refl_field_count; ++j) {
                const shrewd_basic_reflect_field_t *other = &type->sb_refl_fields[j];
                if (j == i) {
                    continue;
                }
                if ((other->sb_refl_offset < field->sb_refl_offset)
                    || ((other->sb_refl_offset == field->sb_refl_offset) && (j < i))) {
                    ++before;
                }
            }
            if (before == rank) {
                pick = field;
                break;
            }
        }
        if (NULL == pick) {
            break; /* unreachable: every rank has a holder */
        }
        shrewd_basic_reflect_layout_entry_t entry = {0};
        entry.field = pick;
        entry.offset = pick->sb_refl_offset;
        entry.size = pick->sb_refl_size;
        entry.align = shrewd_basic_reflect_field_align(pick);
        entry.pad_before = (pick->sb_refl_offset > cursor) ? (pick->sb_refl_offset - cursor) : 0;
        if (cursor < (pick->sb_refl_offset + pick->sb_refl_size)) {
            cursor = pick->sb_refl_offset + pick->sb_refl_size;
        }
        int status = fn(ctx, &entry);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}
