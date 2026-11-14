#include "shrewd-basic/reflect/reflect.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/log/log.h"

/* Shared reflect log module, defined in library/reflect/reflect.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_reflect_log);

#define DUMP_INDENT 2      /* spaces per nesting level */
#define DUMP_MAX_DEPTH 16  /* indentation clamp (defensive) */
#define DUMP_LINE_MAX 1536 /* one output line */
#define DUMP_STR_MAX 256   /* raw string bytes before truncation */
#define DUMP_ARRAY_MAX 32  /* elements rendered per array line */

/*
 * One shared line buffer per dump() call: a line is always completed and
 * emitted before the next one is built (also across recursion), so the
 * buffer is reused rather than stacked per depth.
 */
typedef struct dump_ctx {
    char buf[DUMP_LINE_MAX];
    size_t pos;
} dump_ctx_t;

static void dump_begin(dump_ctx_t *ctx)
{
    ctx->pos = 0;
    ctx->buf[0] = '\0';
}

static void dump_append(dump_ctx_t *ctx, const char *fmt, ...)
{
    if (ctx->pos >= sizeof(ctx->buf)) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(ctx->buf + ctx->pos, sizeof(ctx->buf) - ctx->pos, fmt, ap);
    va_end(ap);
    if (0 > written) {
        return;
    }
    if ((size_t)written >= sizeof(ctx->buf) - ctx->pos) {
        ctx->pos = sizeof(ctx->buf); /* truncated: block further appends */
    } else {
        ctx->pos += (size_t)written;
    }
}

static void dump_emit(dump_ctx_t *ctx)
{
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, shrewd_basic_reflect_log, "dump",
                     "%s\n", ctx->buf);
    dump_begin(ctx);
}

/*
 * Loaders: memcpy into a width-matched temporary (alignment-safe, packed
 * safe, endian-safe) then widen. The byte count comes from field->size
 * (the size invariant), so a long mapped to the int64 descriptor is read
 * with its real width.
 */
static int64_t dump_load_i64(const unsigned char *bytes, size_t nbytes)
{
    switch (nbytes) {
    case 1: {
        int8_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (int64_t)value;
    }
    case 2: {
        int16_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (int64_t)value;
    }
    case 4: {
        int32_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (int64_t)value;
    }
    case 8: {
        int64_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

static uint64_t dump_load_u64(const unsigned char *bytes, size_t nbytes)
{
    switch (nbytes) {
    case 1: {
        uint8_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (uint64_t)value;
    }
    case 2: {
        uint16_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (uint64_t)value;
    }
    case 4: {
        uint32_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (uint64_t)value;
    }
    case 8: {
        uint64_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

static double dump_load_f64(const unsigned char *bytes, size_t nbytes)
{
    if (4 == nbytes) {
        float value = 0.0f;
        memcpy(&value, bytes, sizeof(value));
        return (double)value;
    }
    if (8 == nbytes) {
        double value = 0.0;
        memcpy(&value, bytes, sizeof(value));
        return value;
    }
    return 0.0;
}

static const char *dump_load_str(const unsigned char *bytes)
{
    const char *text = NULL;
    memcpy(&text, bytes, sizeof(text));
    return text;
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
static void dump_scalar(dump_ctx_t *ctx, shrewd_basic_reflect_kind_t kind,
                        const unsigned char *bytes, size_t nbytes, size_t str_max)
{
    switch (kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        dump_append(ctx, "%s", (0 != dump_load_u64(bytes, nbytes)) ? "true" : "false");
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
        dump_append(ctx, "%lld", (long long)dump_load_i64(bytes, nbytes));
        break;
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        dump_append(ctx, "%llu", (unsigned long long)dump_load_u64(bytes, nbytes));
        break;
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
        dump_append(ctx, "%g", dump_load_f64(bytes, nbytes));
        break;
    case SHREWD_BASIC_REFLECT_KIND_STRING:
        dump_string(ctx, dump_load_str(bytes), str_max);
        break;
    default:
        dump_append(ctx, "<kind %d>", (int)kind);
        break;
    }
}

static void dump_fields(dump_ctx_t *ctx, const shrewd_basic_reflect_type_t *type,
                        const unsigned char *object, size_t depth)
{
    size_t level = (depth < DUMP_MAX_DEPTH) ? depth : DUMP_MAX_DEPTH;
    int pad = (int)(level * DUMP_INDENT);
    for (size_t i = 0; i < type->field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->fields[i];
        const unsigned char *at = object + field->offset;
        size_t stride = field->size / field->count;
        if (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
            if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->type->kind) {
                for (size_t j = 0; j < field->count; ++j) {
                    dump_append(ctx, "%*s%s[%zu] = {", pad, "", field->name, j);
                    dump_emit(ctx);
                    dump_fields(ctx, field->type, at + (j * stride), depth + 1);
                    dump_append(ctx, "%*s}", pad, "");
                    dump_emit(ctx);
                }
            } else {
                dump_append(ctx, "%*s%s = [ ", pad, "", field->name);
                for (size_t j = 0; j < field->count; ++j) {
                    if (DUMP_ARRAY_MAX == j) {
                        dump_append(ctx, "...(%zu more)", field->count - j);
                        break;
                    }
                    if (0 != j) {
                        dump_append(ctx, ", ");
                    }
                    dump_scalar(ctx, field->type->kind, at + (j * stride), stride,
                                DUMP_STR_MAX / 8);
                }
                dump_append(ctx, " ]");
                dump_emit(ctx);
            }
        } else if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->type->kind) {
            dump_append(ctx, "%*s%s = {", pad, "", field->name);
            dump_emit(ctx);
            dump_fields(ctx, field->type, at, depth + 1);
            dump_append(ctx, "%*s}", pad, "");
            dump_emit(ctx);
        } else {
            dump_append(ctx, "%*s%s = ", pad, "", field->name);
            dump_scalar(ctx, field->type->kind, at, field->size, DUMP_STR_MAX);
            dump_emit(ctx);
        }
    }
}

int shrewd_basic_reflect_dump(const shrewd_basic_reflect_type_t *type,
                              const void *object, const char *label)
{
    if ((NULL == type) || (NULL == object)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "dump", "dump: type or object is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->kind) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "dump", "dump: type \"%s\" is not a struct\n", type->name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    dump_ctx_t ctx;
    dump_begin(&ctx);
    if (NULL != label) {
        dump_append(&ctx, "%s (%s) {", label, type->name);
    } else {
        dump_append(&ctx, "%s {", type->name);
    }
    dump_emit(&ctx);
    dump_fields(&ctx, type, (const unsigned char *)object, 1);
    dump_append(&ctx, "}");
    dump_emit(&ctx);
    return SHREWD_BASIC_ERR_OK;
}
