#include "shrewd-basic/reflect/reflect.h"

#include <stdio.h>
#include <string.h>

#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"

/* Shared reflect log module, defined in library/reflect/reflect.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_reflect_log);

#define VISIT_MAX_DEPTH 16 /* defensive recursion cap, like the other walkers */
#define VISIT_PATH_MAX 512 /* path string budget; longer paths truncate */

/*
 * Event-by-event walk of a value graph. The walk allocates nothing: the
 * path string and the (object, type) cycle stack live in the context,
 * which sits on the caller's stack. Paths use the path_get() syntax -
 * "home.x", "pair[2].y" - and are truncated at VISIT_PATH_MAX-1 bytes.
 */
typedef struct visit_frame {
    const void *object;
    const shrewd_basic_reflect_type_t *type;
} visit_frame_t;

typedef struct visit_ctx {
    shrewd_basic_reflect_visit_fn visit;
    void *user;
    char path[VISIT_PATH_MAX];
    size_t path_len;
    visit_frame_t stack[VISIT_MAX_DEPTH];
    int stopped;
} visit_ctx_t;

/* Append text to the path (truncating, always NUL-terminated). */
static void visit_path_append(visit_ctx_t *ctx, const char *text)
{
    size_t room = (VISIT_PATH_MAX - 1) - ctx->path_len;
    size_t used = strlen(text);
    if (used > room) {
        used = room;
    }
    memcpy(ctx->path + ctx->path_len, text, used);
    ctx->path_len += used;
    ctx->path[ctx->path_len] = '\0';
}

/* Append one member segment: "name" at the root, ".name" otherwise. */
static void visit_path_member(visit_ctx_t *ctx, const char *name)
{
    if (0 != ctx->path_len) {
        visit_path_append(ctx, ".");
    }
    visit_path_append(ctx, name);
}

/* Append one index segment: "[7]". */
static void visit_path_index(visit_ctx_t *ctx, size_t index)
{
    char text[24];
    (void)snprintf(text, sizeof(text), "[%zu]", index);
    visit_path_append(ctx, text);
}

/*
 * A re-entry is only a cycle when the same object reappears as the same
 * type (an embedded first member shares its parent's address without
 * being a cycle - same rule as dump).
 */
static int visit_target_seen(const visit_ctx_t *ctx, size_t depth,
                             const void *target,
                             const shrewd_basic_reflect_type_t *type)
{
    for (size_t i = 0; i < depth; ++i) {
        if ((ctx->stack[i].object == target) && (ctx->stack[i].type == type)) {
            return 1;
        }
    }
    return 0;
}

static shrewd_basic_reflect_visit_action_t visit_emit(
        visit_ctx_t *ctx, shrewd_basic_reflect_visit_event_t event,
        const shrewd_basic_reflect_field_t *field, const void *value,
        const shrewd_basic_reflect_type_t *type, size_t count)
{
    shrewd_basic_reflect_visit_node_t node;
    shrewd_basic_reflect_visit_action_t action;
    if (0 != ctx->stopped) {
        return SHREWD_BASIC_REFLECT_VISIT_STOP;
    }
    node.event = event;
    node.path = ctx->path;
    node.field = field;
    node.value = value;
    node.type = type;
    node.count = count;
    action = ctx->visit(ctx->user, &node);
    if (SHREWD_BASIC_REFLECT_VISIT_STOP == action) {
        ctx->stopped = 1;
    }
    return action;
}

static void visit_struct(visit_ctx_t *ctx, const shrewd_basic_reflect_type_t *type,
                         const unsigned char *object,
                         const shrewd_basic_reflect_field_t *field, size_t depth);

/*
 * Report one element / one pointer target. A NULL slot is a LEAF with a
 * NULL value; a struct target is expanded (ENTER ... EXIT); anything
 * else - scalars, strings, opaque targets - is a LEAF whose value points
 * at the target's storage.
 */
static void visit_target(visit_ctx_t *ctx, const shrewd_basic_reflect_type_t *type,
                         const void *target,
                         const shrewd_basic_reflect_field_t *field, size_t depth)
{
    if (NULL == target) {
        (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_LEAF, field, NULL, type, 0);
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        visit_struct(ctx, type, (const unsigned char *)target, field, depth);
        return;
    }
    (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_LEAF, field, target, type, 0);
}

/* Emit the events of one array-shaped field (inline, flexible or dynamic). */
static void visit_array(visit_ctx_t *ctx, const shrewd_basic_reflect_field_t *field,
                        const unsigned char *object, size_t depth)
{
    reflect_array_view_t view = reflect_array_view(object, field);
    if (NULL == view.base) {
        /* Dynamic table not allocated: like dump's "(null)". */
        (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_LEAF, field, NULL,
                         field->sb_refl_type, 0);
        return;
    }
    if (SHREWD_BASIC_REFLECT_VISIT_PRUNE
            == visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, field,
                          view.base, field->sb_refl_type, view.count)) {
        return;
    }
    for (size_t i = 0; i < view.count; ++i) {
        const unsigned char *elem = view.base + (i * view.stride);
        size_t mark = ctx->path_len;
        visit_path_index(ctx, i);
        if (0 != ctx->stopped) {
            ctx->path_len = mark;
            ctx->path[mark] = '\0';
            return;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            visit_target(ctx, field->sb_refl_type, reflect_load_ptr(elem),
                         field, depth + 1);
        } else if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
            visit_struct(ctx, field->sb_refl_type, elem, field, depth + 1);
        } else {
            (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_LEAF, field, elem,
                             field->sb_refl_type, 0);
        }
        ctx->path_len = mark;
        ctx->path[mark] = '\0';
        if (0 != ctx->stopped) {
            return;
        }
    }
    (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_ARRAY_EXIT, field, view.base,
                     field->sb_refl_type, view.count);
}

static void visit_field(visit_ctx_t *ctx, const shrewd_basic_reflect_field_t *field,
                        const unsigned char *object, size_t depth)
{
    size_t mark = ctx->path_len;
    visit_path_member(ctx, field->sb_refl_name);
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        /* Text buffer: one string leaf, not an element list. */
        (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_LEAF, field,
                         object + field->sb_refl_offset, field->sb_refl_type, 0);
    } else if (0 != (field->sb_refl_flags
              & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
        visit_array(ctx, field, object, depth);
    } else if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        visit_target(ctx, field->sb_refl_type,
                     reflect_load_ptr(object + field->sb_refl_offset),
                     field, depth + 1);
    } else if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
        visit_struct(ctx, field->sb_refl_type, object + field->sb_refl_offset,
                     field, depth + 1);
    } else {
        (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_LEAF, field,
                         object + field->sb_refl_offset, field->sb_refl_type, 0);
    }
    ctx->path_len = mark;
    ctx->path[mark] = '\0';
}

/*
 * Expand one struct value: ENTER, the fields, EXIT. Cycles and the depth
 * cap report a CYCLE event at the offending path instead. A STOP from
 * below aborts everything, pending EXIT events included.
 */
static void visit_struct(visit_ctx_t *ctx, const shrewd_basic_reflect_type_t *type,
                         const unsigned char *object,
                         const shrewd_basic_reflect_field_t *field, size_t depth)
{
    if ((VISIT_MAX_DEPTH <= depth)
            || (0 != visit_target_seen(ctx, depth, object, type))) {
        (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_CYCLE, field, object, type, 0);
        return;
    }
    ctx->stack[depth].object = object;
    ctx->stack[depth].type = type;
    if (SHREWD_BASIC_REFLECT_VISIT_PRUNE
            == visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_ENTER, field, object, type, 0)) {
        return;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        if (0 != ctx->stopped) {
            return;
        }
        visit_field(ctx, &type->sb_refl_fields[i], object, depth);
    }
    (void)visit_emit(ctx, SHREWD_BASIC_REFLECT_VISIT_EXIT, field, object, type, 0);
}

int shrewd_basic_reflect_visit(const void *object,
                               const shrewd_basic_reflect_type_t *type,
                               shrewd_basic_reflect_visit_fn visit, void *ctx)
{
    visit_ctx_t walk;
    if ((NULL == object) || (NULL == type) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "visit", "visit: object, type or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "visit", "visit: type \"%s\" is not a struct\n",
                         type->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&walk, 0, sizeof(walk));
    walk.visit = visit;
    walk.user = ctx;
    visit_struct(&walk, type, (const unsigned char *)object, NULL, 0);
    return SHREWD_BASIC_ERR_OK;
}
