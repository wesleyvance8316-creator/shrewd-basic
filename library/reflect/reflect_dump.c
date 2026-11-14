#include "shrewd-basic/reflect/reflect.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared reflect log module, defined in library/reflect/reflect.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_reflect_log);

#define DUMP_INDENT 2     /* spaces per nesting level */
#define DUMP_MAX_DEPTH 16 /* indentation clamp (defensive) */
#define DUMP_STR_MAX 256  /* raw string bytes before truncation */
#define DUMP_ARRAY_MAX 32 /* elements rendered per array line */
#define DUMP_BODY_MIN 1024 /* initial body buffer */

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
    const void *path[DUMP_MAX_DEPTH]; /* objects on the render stack */
} dump_ctx_t;

static int dump_init(dump_ctx_t *ctx)
{
    void *block = NULL;
    ctx->buf = NULL;
    ctx->cap = 0;
    ctx->pos = 0;
    ctx->truncated = 0;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, DUMP_BODY_MIN)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    ctx->buf = block;
    ctx->cap = DUMP_BODY_MIN;
    ctx->buf[0] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

static void dump_append(dump_ctx_t *ctx, const char *fmt, ...)
{
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
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, ctx->buf, cap)) {
            /* Out of room: mark what fits and stop building. */
            static const char marker[] = " ...(dump truncated)";
            size_t slack = ctx->cap - ctx->pos;
            if (slack > sizeof(marker)) {
                memcpy(ctx->buf + ctx->pos, marker, sizeof(marker));
                ctx->pos += sizeof(marker) - 1;
            }
            ctx->truncated = 1;
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

static void dump_emit(dump_ctx_t *ctx)
{
    dump_append(ctx, "\n");
}


static void dump_string(dump_ctx_t *ctx, const char *text, size_t str_max)
{
    if (NULL == text) {
        dump_append(ctx, "(null)");
        return;
    }
    dump_append(ctx, "\"");
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)text; '\0' != *p; ++p) {
        if (used >= str_max) {
            dump_append(ctx, "...(truncated)");
            break;
        }
        ++used;
        switch (*p) {
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
            if ((' ' <= *p) && ('~' >= *p)) {
                dump_append(ctx, "%c", *p);
            } else {
                dump_append(ctx, "\\x%02x", (unsigned int)*p);
            }
            break;
        }
    }
    dump_append(ctx, "\"");
}

/* Render one scalar value; str_max bounds string elements inside arrays. */
static void dump_scalar(dump_ctx_t *ctx, const shrewd_basic_reflect_type_t *type,
                        const unsigned char *bytes, size_t nbytes, size_t str_max)
{
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
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
        dump_append(ctx, "%g", reflect_load_f64(bytes, nbytes));
        break;
    case SHREWD_BASIC_REFLECT_KIND_STRING:
        dump_string(ctx, reflect_load_str(bytes), str_max);
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
    default:
        dump_append(ctx, "<kind %d>", (int)type->sb_refl_kind);
        break;
    }
}

/*
 * Render every field of `object`. A F_PTR slot is followed (the pointee
 * is rendered; NULL shows "(null)", opaque targets show their address).
 * Re-entering an object already on the render stack - a cycle in the
 * object graph - stops with " ...(cyclic)"; the recursion is also
 * hard-capped at DUMP_MAX_DEPTH (" ...(max depth)") as a last resort.
 */
static int dump_target_seen(const dump_ctx_t *ctx, size_t depth,
                            const void *target)
{
    for (size_t i = 0; i < depth; ++i) {
        if (ctx->path[i] == target) {
            return 1;
        }
    }
    return 0;
}

static void dump_fields(dump_ctx_t *ctx, const shrewd_basic_reflect_type_t *type,
                        const unsigned char *object, size_t depth)
{
    size_t level = (depth < DUMP_MAX_DEPTH) ? depth : DUMP_MAX_DEPTH;
    ctx->path[depth - 1] = object;
    int pad = (int)(level * DUMP_INDENT);
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const unsigned char *at = object + field->sb_refl_offset;
        size_t stride = field->sb_refl_size / field->sb_refl_count;
        int is_array = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY));
        int is_ptr = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR));
        int is_struct = (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind);
        int is_opaque = (SHREWD_BASIC_REFLECT_KIND_OPAQUE == field->sb_refl_type->sb_refl_kind);

        if ((0 != is_array) && (0 != is_struct)) {
            /* Array of structs or of struct pointers: one block each. */
            for (size_t j = 0; j < field->sb_refl_count; ++j) {
                const unsigned char *elem = at + (j * stride);
                const unsigned char *target = elem;
                if (0 != is_ptr) {
                    void *pointer = reflect_load_ptr(elem);
                    if (NULL == pointer) {
                        dump_append(ctx, "%*s%s[%zu] = (null)", pad, "", field->sb_refl_name, j);
                        dump_emit(ctx);
                        continue;
                    }
                    target = (const unsigned char *)pointer;
                }
                if (0 != dump_target_seen(ctx, depth, target)) {
                    dump_append(ctx, "%*s%s[%zu] = ...(cyclic)", pad, "",
                                field->sb_refl_name, j);
                    dump_emit(ctx);
                    continue;
                }
                if (depth >= DUMP_MAX_DEPTH) {
                    dump_append(ctx, "%*s%s[%zu] = ...(max depth)", pad, "", field->sb_refl_name, j);
                    dump_emit(ctx);
                    continue;
                }
                dump_append(ctx, "%*s%s[%zu] = {", pad, "", field->sb_refl_name, j);
                dump_emit(ctx);
                dump_fields(ctx, field->sb_refl_type, target, depth + 1);
                dump_append(ctx, "%*s}", pad, "");
                dump_emit(ctx);
            }
        } else if (0 != is_array) {
            /* Array of scalars/strings, possibly behind pointers. */
            dump_append(ctx, "%*s%s = [ ", pad, "", field->sb_refl_name);
            for (size_t j = 0; j < field->sb_refl_count; ++j) {
                if (DUMP_ARRAY_MAX == j) {
                    dump_append(ctx, "...(%zu more)", field->sb_refl_count - j);
                    break;
                }
                if (0 != j) {
                    dump_append(ctx, ", ");
                }
                const unsigned char *elem = at + (j * stride);
                if (0 != is_ptr) {
                    void *pointer = reflect_load_ptr(elem);
                    if (NULL == pointer) {
                        dump_append(ctx, "(null)");
                    } else if (0 != is_opaque) {
                        dump_append(ctx, "0x%llx",
                                    (unsigned long long)(uintptr_t)pointer);
                    } else {
                        dump_scalar(ctx, field->sb_refl_type,
                                    (const unsigned char *)pointer,
                                    field->sb_refl_type->sb_refl_size, DUMP_STR_MAX / 8);
                    }
                } else {
                    dump_scalar(ctx, field->sb_refl_type, elem, stride, DUMP_STR_MAX / 8);
                }
            }
            dump_append(ctx, " ]");
            dump_emit(ctx);
        } else if (0 != is_ptr) {
            void *pointer = reflect_load_ptr(at);
            if (NULL == pointer) {
                dump_append(ctx, "%*s%s = (null)", pad, "", field->sb_refl_name);
                dump_emit(ctx);
            } else if (0 != is_opaque) {
                dump_append(ctx, "%*s%s = 0x%llx", pad, "", field->sb_refl_name,
                            (unsigned long long)(uintptr_t)pointer);
                dump_emit(ctx);
            } else if (0 != is_struct) {
                if (0 != dump_target_seen(ctx, depth, pointer)) {
                    dump_append(ctx, "%*s%s = ...(cyclic)", pad, "", field->sb_refl_name);
                    dump_emit(ctx);
                } else if (depth >= DUMP_MAX_DEPTH) {
                    dump_append(ctx, "%*s%s = ...(max depth)", pad, "", field->sb_refl_name);
                    dump_emit(ctx);
                } else {
                    dump_append(ctx, "%*s%s = {", pad, "", field->sb_refl_name);
                    dump_emit(ctx);
                    dump_fields(ctx, field->sb_refl_type, (const unsigned char *)pointer, depth + 1);
                    dump_append(ctx, "%*s}", pad, "");
                    dump_emit(ctx);
                }
            } else {
                dump_append(ctx, "%*s%s = ", pad, "", field->sb_refl_name);
                dump_scalar(ctx, field->sb_refl_type, (const unsigned char *)pointer,
                            field->sb_refl_type->sb_refl_size, DUMP_STR_MAX);
                dump_emit(ctx);
            }
        } else if (0 != is_struct) {
            if (0 != dump_target_seen(ctx, depth, at)) {
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
                              const void *object, const char *label,
                              const char *file, int line, const char *func)
{
    if ((NULL == type) || (NULL == object)) {
        shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "dump", file, line, func, "dump: type or object is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "dump", file, line, func, "dump: type \"%s\" is not a struct\n", type->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    dump_ctx_t ctx;
    if (SHREWD_BASIC_ERR_OK != dump_init(&ctx)) {
        shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_reflect_log,
                         "dump", file, line, func, "dump: allocation failed\n");
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
    shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_INFO, shrewd_basic_reflect_log,
                     "dump", file, line, func, "\n%s", ctx.buf);
    int status = (0 != ctx.truncated) ? SHREWD_BASIC_ERR_MEM_NOMEM
                                      : SHREWD_BASIC_ERR_OK;
    (void)shrewd_basic_platform_free((void **)&ctx.buf);
    return status;
}
