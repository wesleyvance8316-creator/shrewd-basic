/**
 * @file reflect_demo.c
 * @brief Walkthrough of every shrewd-basic/reflect interface.
 *
 * Descriptors are authored with macros at compile time (const data);
 * generic code then enumerates fields, reads/writes values, walks paths,
 * prints objects, validates them and deep-copies them. dump output goes
 * through the "reflect" log module - colored on a terminal - while the
 * section banners use plain printf.
 *
 * Covered: TYPE_DEFINE/DECLARE/DEFINE_OPS/OPAQUE_DEFINE/ENUM_DEFINE,
 * FIELD_PRIMITIVE/STRUCT/ARRAY/PTR/PTR_ARRAY/FIELD_ENUM (+ _OPS
 * variants), kind_name, type_find_field, enum_find_name/value,
 * field_get/set, dump (incl. ops->render and cycle markers),
 * type_resolve, path_get/set, value_get/set, check, equal, copy,
 * destroy, dynamic arrays (F_DARRAY/F_SENTINEL), flexible tails
 * (F_FLEX), string buffers (F_STRINGBUF), owned strings
 * (F_STRING_OWNED), bit groups (F_BITS), multi-dimension arrays
 * (ARRAY_DEFINE), tagged unions (UNION_DEFINE/FIELD_UNION),
 * the traversal accessors,
 * visit (event-by-event walk) and diff/hash, plus caller-hosted
 * lookup lists (list_register + the *_list query family).
 * The 1 MiB dump body cap is covered by the test suite
 * instead - its output would flood the terminal here.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"

#include "reflect_demo_ext.h"

static void banner(const char *title) {
    printf("\n=== %s ===\n", title);
    fflush(stdout);
}

static void print_status(const char *what, int status) {
    printf("  %-46s -> %s (%d)\n", what, shrewd_basic_error_name(status), status);
    fflush(stdout);
}

/* --- reflected types -------------------------------------------------- */

typedef struct {
    int32_t x;
    int32_t y;
} point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(point_type,
                                 point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

/* Self-referential type (linked list). */
typedef struct node {
    int32_t data;
    struct node *next;
} node_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(node_type,
                                 node_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(node_t, data),
                                 SHREWD_BASIC_REFLECT_FIELD_PTR(node_t, next, node_type));

/* --- ops fixtures ----------------------------------------------------- */

/* An opaque handle: the application knows the layout (an int32), reflect
 * only sees "opaque + ops". Invariant: value >= 0; set clamps to 100. */
static int handle_get(const void *pointee, void *out_value, size_t value_size) {
    memcpy(out_value, pointee, value_size);
    return SHREWD_BASIC_ERR_OK;
}

static int handle_set(void *pointee, const void *value, size_t value_size) {
    int32_t rounded = 0;
    memcpy(&rounded, value, sizeof(rounded));
    if (100 < rounded) {
        rounded = 100;
    }
    memcpy(pointee, &rounded, sizeof(rounded));
    (void)value_size;
    return SHREWD_BASIC_ERR_OK;
}

static int handle_check(const void *pointee) {
    int32_t value = 0;
    memcpy(&value, pointee, sizeof(value));
    return (0 <= value) ? SHREWD_BASIC_ERR_OK : SHREWD_BASIC_ERR_ARG_INVALID;
}

static int handle_copy(void **out, const void *pointee) {
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(int32_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(block, pointee, sizeof(int32_t));
    *out = block;
    return SHREWD_BASIC_ERR_OK;
}

static void handle_destroy(void *pointee) {
    (void)shrewd_basic_platform_free(&pointee);
}

/* Custom dump text for a followed handle instead of a raw address. */
static int handle_render(const void *pointee, char *buf, size_t buf_size) {
    int32_t value = 0;
    memcpy(&value, pointee, sizeof(value));
    (void)snprintf(buf, buf_size, "handle#%d", value);
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_reflect_ops_t g_handle_ops = {.value_size = sizeof(int32_t),
                                                        .get = handle_get,
                                                        .set = handle_set,
                                                        .check = handle_check,
                                                        .copy = handle_copy,
                                                        .render = handle_render,
                                                        .destroy = handle_destroy};

SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(handle_type, "handle", g_handle_ops);

/* Field-level override: only get differs (x10), the rest reuses the
 * handle behavior - a field ops table replaces the type's per field. */
static int scale_get(const void *pointee, void *out_value, size_t value_size) {
    int32_t value = 0;
    memcpy(&value, pointee, sizeof(value));
    value *= 10;
    memcpy(out_value, &value, sizeof(value));
    (void)value_size;
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_reflect_ops_t g_scale_ops = {.value_size = sizeof(int32_t),
                                                       .get = scale_get,
                                                       .set = handle_set,
                                                       .check = handle_check,
                                                       .copy = handle_copy,
                                                       .destroy = handle_destroy};

/* Dynamic arrays: a pointer table, a value vector and an argv-style
   NULL-terminated string table. */
typedef struct {
    point_t **grid;
    size_t grid_count;
    int32_t *values;
    size_t value_count;
    char **argv;
} dyn_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    dyn_type,
    dyn_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR_DARRAY(dyn_t, grid, grid_count, point_type),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dyn_t, grid_count),
    SHREWD_BASIC_REFLECT_FIELD_DARRAY(dyn_t, values, value_count, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dyn_t, value_count),
    SHREWD_BASIC_REFLECT_FIELD_SENTINEL(dyn_t, argv, shrewd_basic_reflect_string));

/* A flexible tail: inline storage sized at run time by the count
   member (C99 FAM; the GNU zero-length form works the same way). */
typedef struct {
    size_t len;
    int32_t data[];
} packet_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    packet_type,
    packet_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(packet_t, len),
    SHREWD_BASIC_REFLECT_FIELD_FLEX(packet_t, data, len, shrewd_basic_reflect_int32));

/* A char[N] text buffer: C-string semantics on inline storage. */
typedef struct {
    char title[12];
    int32_t id;
} text_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(text_type,
                                 text_t,
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(text_t, title),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(text_t, id));

/* Owned strings: the graph owns the content (copy clones, destroy frees). */
typedef struct {
    char *title;
    const char *borrowed;
} owned_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(owned_type,
                                 owned_t,
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(owned_t, title),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(owned_t, borrowed));

/* A register-style unit: named bit groups share one uint32 word. */
typedef struct {
    uint32_t cfg;
    int32_t value;
} reg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(reg_type,
                                 reg_t,
                                 SHREWD_BASIC_REFLECT_FIELD_BITS(reg_t, cfg, ready, 0, 1),
                                 SHREWD_BASIC_REFLECT_FIELD_BITS(reg_t, cfg, mode, 1, 3),
                                 SHREWD_BASIC_REFLECT_FIELD_BITS(reg_t, cfg, divider, 8, 16),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(reg_t, value));

/* A matrix: int32_t m[2][3] through an array value type. */
typedef struct {
    int32_t m[2][3];
    int32_t id;
} matrix_t;

SHREWD_BASIC_REFLECT_ARRAY_DEFINE(row3_type, shrewd_basic_reflect_int32, int32_t, 3);
SHREWD_BASIC_REFLECT_TYPE_DEFINE(matrix_type,
                                 matrix_t,
                                 SHREWD_BASIC_REFLECT_FIELD_ARRAY(matrix_t, m, row3_type),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(matrix_t, id));

/* A tagged union: the payload shape follows the kind member. */
typedef enum { MSG_TEXT = 0, MSG_POINT = 1, MSG_RAW = 2 } msg_kind_t;

typedef union {
    char *text;
    point_t point;
} msg_body_t;

typedef struct {
    msg_kind_t kind;
    msg_body_t body;
} msg_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(msg_kind_enum,
                                 msg_kind_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MSG_TEXT),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MSG_POINT),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MSG_RAW));

SHREWD_BASIC_REFLECT_UNION_DEFINE(
    msg_body_type,
    msg_body_t,
    SHREWD_BASIC_REFLECT_UNION_VARIANT(MSG_TEXT,
                                       SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(msg_body_t, text)),
    SHREWD_BASIC_REFLECT_UNION_VARIANT(
        MSG_POINT, SHREWD_BASIC_REFLECT_FIELD_STRUCT(msg_body_t, point, point_type)));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    msg_type,
    msg_t,
    SHREWD_BASIC_REFLECT_FIELD_ENUM(msg_t, kind, msg_kind_enum),
    SHREWD_BASIC_REFLECT_FIELD_UNION(msg_t, body, msg_body_type, kind));

/* An enum: int-compatible values with a name/value table. */
typedef enum { MODE_OFF = 0, MODE_ON = 1, MODE_AUTO = 2 } mode_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(mode_type,
                                 mode_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MODE_OFF),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MODE_ON),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MODE_AUTO));

/* A rich struct exercising every field flavor. */
typedef struct {
    const char *name;
    char grade;
    int8_t small;
    int32_t age;
    uint64_t big;
    float ratio;
    double precise;
    mode_t mode;
    char tag[4];
    int32_t scores[3];
    point_t home;
    point_t pair[2];
    point_t *ptr;
    const char **p_str;
    void *ctx;
    int32_t *handle;
    int32_t *scaled;
} person_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    person_type,
    person_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, name),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, grade),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, small),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, age),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, big),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, ratio),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, precise),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(person_t, mode, mode_type),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, tag, shrewd_basic_reflect_char),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, scores, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(person_t, home, point_type),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, pair, point_type),
    SHREWD_BASIC_REFLECT_FIELD_PTR(person_t, ptr, point_type),
    SHREWD_BASIC_REFLECT_FIELD_PTR(person_t, p_str, shrewd_basic_reflect_string),
    SHREWD_BASIC_REFLECT_FIELD_PTR(person_t, ctx, shrewd_basic_reflect_void),
    SHREWD_BASIC_REFLECT_FIELD_PTR(person_t, handle, handle_type),
    SHREWD_BASIC_REFLECT_FIELD_PTR_OPS(person_t, scaled, handle_type, g_scale_ops));

static void describe_field(const char *name) {
    const shrewd_basic_reflect_field_t *field = NULL;
    int status = shrewd_basic_reflect_type_find_field(&person_type, name, &field);
    if (SHREWD_BASIC_ERR_OK != status) {
        printf("  %-10s -> %s\n", name, shrewd_basic_error_name(status));
        return;
    }
    printf("  %-10s kind=%-6s offset=%-3zu size=%-3zu count=%zu%s%s\n",
           name,
           shrewd_basic_reflect_kind_name(field->sb_refl_type->sb_refl_kind),
           field->sb_refl_offset,
           field->sb_refl_size,
           field->sb_refl_count,
           (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) ? " ARRAY" : "",
           (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) ? " PTR" : "");
}

/* Visit callback: one printed line per traversal event. */
static shrewd_basic_reflect_visit_action_t demo_visit_print(
    void *ctx, const shrewd_basic_reflect_visit_node_t *node) {
    static const char *const event_name[]
        = {"ENTER", "ARRAY_ENTER", "LEAF", "CYCLE", "ARRAY_EXIT", "EXIT"};
    (void)ctx;
    if ((SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER == node->event)
        || (SHREWD_BASIC_REFLECT_VISIT_ARRAY_EXIT == node->event)) {
        printf("  %-11s %s (count %zu)\n", event_name[node->event], node->path, node->count);
    } else {
        printf("  %-11s %s%s\n",
               event_name[node->event],
               ('\0' != node->path[0]) ? node->path : "(root)",
               (NULL == node->value) ? " (null)" : "");
    }
    return SHREWD_BASIC_REFLECT_VISIT_CONTINUE;
}

/* Section 19: a caller-hosted lookup list - plain array, linear ops. */
typedef struct {
    shrewd_basic_reflect_lookup_t entries[4];
    size_t count;
} demo_reg_t;

static int demo_reg_insert(void *ctx,
                           const char *name,
                           const shrewd_basic_reflect_type_t *type,
                           shrewd_basic_reflect_lookup_t **out_slot) {
    demo_reg_t *reg = (demo_reg_t *)ctx;
    (void)type;
    for (size_t i = 0; i < reg->count; ++i) {
        if (0 == strcmp(reg->entries[i].sb_refl_name, name)) {
            return SHREWD_BASIC_ERR_KEY_EXISTS;
        }
    }
    *out_slot = &reg->entries[reg->count];
    ++reg->count;
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_reflect_lookup_t *demo_reg_find_type(
    void *ctx, const shrewd_basic_reflect_type_t *type) {
    demo_reg_t *reg = (demo_reg_t *)ctx;
    for (size_t i = 0; i < reg->count; ++i) {
        if (reg->entries[i].sb_refl_type == type) {
            return &reg->entries[i];
        }
    }
    return NULL;
}

static const shrewd_basic_reflect_lookup_t *demo_reg_find_name(void *ctx, const char *name) {
    demo_reg_t *reg = (demo_reg_t *)ctx;
    for (size_t i = 0; i < reg->count; ++i) {
        if (0 == strcmp(reg->entries[i].sb_refl_name, name)) {
            return &reg->entries[i];
        }
    }
    return NULL;
}

int main(void) {
    /* Line-buffer stdout so printf output interleaves cleanly with the
       log module's stderr output. */
    (void)setvbuf(stdout, NULL, _IOLBF, 0);

    /* The reflect module logs every operation at TRACE through the shared
       "reflect" log module (see docs/reflect.md). The module joins the
       registry on first use, so trigger one lookup miss first (its TRACE
       line is the only unmasked one), then mask to INFO: dump lines are
       INFO and still show. */
    const shrewd_basic_reflect_field_t *probe = NULL;
    (void)shrewd_basic_reflect_type_find_field(&person_type, "warmup", &probe);
    shrewd_basic_log_module_t *log_module = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find("reflect", &log_module)) {
        (void)shrewd_basic_log_module_set_level(log_module, SHREWD_BASIC_LOG_LEVEL_INFO);
    }

    /* The clone map of copy rides on the ds rbtree: silence that module
       too so the demo stays readable (a tiny warm-up copy registers it
       before the lookup). */
    dyn_t warmup = {0};
    dyn_t *warmup_clone = NULL;
    memset(&warmup, 0, sizeof(warmup));
    (void)shrewd_basic_reflect_copy((void **)&warmup_clone, &warmup, &dyn_type);
    (void)shrewd_basic_reflect_destroy(warmup_clone, &dyn_type);
    shrewd_basic_log_module_t *ds_module = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find("ds", &ds_module)) {
        (void)shrewd_basic_log_module_set_level(ds_module, SHREWD_BASIC_LOG_LEVEL_NONE);
    }

    person_t person = {0};
    memset(&person, 0, sizeof(person));
    person.name = "Alice";
    person.grade = 'A';
    person.small = -7;
    person.age = 30;
    person.big = UINT64_C(0x1122334455667788);
    person.ratio = 1.5f;
    person.precise = 2.25;
    memcpy(person.tag, "tag", 4);
    person.scores[0] = 1;
    person.scores[1] = 2;
    person.scores[2] = 3;
    person.home.x = 4;
    person.home.y = 5;
    person.pair[0].x = 6;
    person.pair[0].y = 7;
    person.pair[1].x = 8;
    person.pair[1].y = 9;

    banner("1. type description: find_field + kind_name");
    printf("  person_t has %zu fields, size %zu bytes, kind %s\n",
           person_type.sb_refl_field_count,
           person_type.sb_refl_size,
           shrewd_basic_reflect_kind_name(person_type.sb_refl_kind));
    describe_field("age");
    describe_field("name");
    describe_field("tag");
    describe_field("scores");
    describe_field("home");
    describe_field("ptr");
    describe_field("nope");

    printf("  iterating the %zu fields with type_field_at:",
           shrewd_basic_reflect_type_field_count(&person_type));
    for (size_t i = 0; i < shrewd_basic_reflect_type_field_count(&person_type); ++i) {
        printf(" %s", shrewd_basic_reflect_type_field_at(&person_type, i)->sb_refl_name);
    }
    printf("\n");

    banner("2. field_get/field_set: raw-byte access");
    const shrewd_basic_reflect_field_t *age_field = NULL;
    (void)shrewd_basic_reflect_type_find_field(&person_type, "age", &age_field);
    int32_t age = 0;
    (void)shrewd_basic_reflect_field_get(&person, age_field, &age, sizeof(age));
    printf("  get 'age' -> %d\n", age);
    int32_t updated = 31;
    print_status("set 'age' to 31",
                 shrewd_basic_reflect_field_set(&person, age_field, &updated, sizeof(updated)));
    (void)shrewd_basic_reflect_field_get(&person, age_field, &age, sizeof(age));
    printf("  get 'age' -> %d\n", age);
    print_status("set 'age' with wrong size (2)",
                 shrewd_basic_reflect_field_set(&person, age_field, &updated, (size_t)2));

    banner("3. dump: every field kind, nested and arrays");
    (void)shrewd_basic_reflect_dump(&person_type, &person, "person");

    banner("4. pointers: dump follows them");
    point_t point = {1, 2};
    const char *text = "hi";
    int32_t marker = 0;
    person.ptr = &point;
    person.p_str = &text;
    person.ctx = &marker; /* opaque: dump shows the address */
    int32_t handle_value = 42;
    person.handle = &handle_value; /* ops->render: dump shows handle#42 */
    (void)shrewd_basic_reflect_dump(&person_type, &person, "person");

    node_t tail = {0};
    memset(&tail, 0, sizeof(tail));
    tail.data = 2;
    node_t head = {0};
    memset(&head, 0, sizeof(head));
    head.data = 1;
    head.next = &tail;
    printf("\n  self-referential list (node_t *next):\n");
    (void)shrewd_basic_reflect_dump(&node_type, &head, "node");

    node_t loop = {0};
    memset(&loop, 0, sizeof(loop));
    loop.next = &loop;
    printf("\n  self-cycle terminates at the depth cap:\n");
    (void)shrewd_basic_reflect_dump(&node_type, &loop, "node");
    person.ptr = NULL;
    (void)shrewd_basic_reflect_dump(&person_type, &person, "person (ptr = NULL)");

    banner("5. paths: resolve / path_get / path_set");
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t offset = 0;
    size_t size = 0;
    print_status("resolve \"home.x\"",
                 shrewd_basic_reflect_type_resolve(&person_type, "home.x", &field, &offset, &size));
    printf("  -> field \"%s\", offset %zu, value size %zu\n", field->sb_refl_name, offset, size);
    int32_t value = 77;
    print_status(
        "path_set \"home.x\" = 77",
        shrewd_basic_reflect_path_set(&person, &person_type, "home.x", &value, sizeof(value)));
    value = 0;
    print_status(
        "path_get \"home.x\"",
        shrewd_basic_reflect_path_get(&person, &person_type, "home.x", &value, sizeof(value)));
    printf("  -> home.x = %d\n", value);
    value = 88;
    print_status(
        "path_set \"pair[1].y\" = 88",
        shrewd_basic_reflect_path_set(&person, &person_type, "pair[1].y", &value, sizeof(value)));
    printf("  -> pair[1].y = %d\n", person.pair[1].y);
    person.ptr = &point;
    value = 0;
    print_status(
        "path_get \"ptr.y\" (through a pointer)",
        shrewd_basic_reflect_path_get(&person, &person_type, "ptr.y", &value, sizeof(value)));
    printf("  -> ptr->y = %d\n", value);
    person.ptr = NULL;
    print_status(
        "path_get \"ptr.y\" with NULL pointer",
        shrewd_basic_reflect_path_get(&person, &person_type, "ptr.y", &value, sizeof(value)));
    print_status("resolve \"ptr.y\" (crosses a pointer)",
                 shrewd_basic_reflect_type_resolve(&person_type, "ptr.y", &field, &offset, &size));
    print_status(
        "path_get \"home..x\" (bad syntax)",
        shrewd_basic_reflect_path_get(&person, &person_type, "home..x", &value, sizeof(value)));

    banner("6. ops: value_get/set, check, copy/destroy");
    int32_t handle = 42;
    int32_t scaled = 21;
    person.handle = &handle;
    person.scaled = &scaled;
    person.ptr = &point;
    const shrewd_basic_reflect_field_t *handle_field = NULL;
    const shrewd_basic_reflect_field_t *scaled_field = NULL;
    (void)shrewd_basic_reflect_type_find_field(&person_type, "handle", &handle_field);
    (void)shrewd_basic_reflect_type_find_field(&person_type, "scaled", &scaled_field);
    value = 0;
    print_status("value_get 'handle' (type ops)",
                 shrewd_basic_reflect_value_get(&person, handle_field, &value, sizeof(value)));
    printf("  -> handle = %d\n", value);
    value = 777;
    print_status("value_set 'handle' = 777 (clamped)",
                 shrewd_basic_reflect_value_set(&person, handle_field, &value, sizeof(value)));
    printf("  -> handle = %d (set clamps to 100)\n", handle);
    value = 0;
    print_status("value_get 'scaled' (field ops, x10)",
                 shrewd_basic_reflect_value_get(&person, scaled_field, &value, sizeof(value)));
    printf("  -> scaled = %d (stored %d)\n", value, scaled);

    print_status("check (invariants hold)", shrewd_basic_reflect_check(&person, &person_type));
    handle = -5;
    print_status("check (handle invariant violated)",
                 shrewd_basic_reflect_check(&person, &person_type));
    handle = 100;
    person.ctx = NULL; /* an opaque ctx without ops would block the copy */

    void *raw = NULL;
    print_status("copy (deep clone)", shrewd_basic_reflect_copy(&raw, &person, &person_type));
    person_t *clone = raw;
    printf(
        "  clone: home.x=%d handle=%d scaled=%d\n", clone->home.x, *clone->handle, *clone->scaled);
    printf("  clone->ptr is a new object: %s\n", (clone->ptr != person.ptr) ? "yes" : "no");
    printf("  clone->p_str is aliased (strings borrowed): %s\n",
           (clone->p_str == person.p_str) ? "yes" : "no");
    point.x = 999;
    printf("  after mutating the original: clone->ptr->x = %d (independent)\n", clone->ptr->x);
    point.x = 1;
    print_status("check the clone", shrewd_basic_reflect_check(clone, &person_type));
    print_status("destroy the clone", shrewd_basic_reflect_destroy(clone, &person_type));

    person.ctx = &marker;
    raw = NULL;
    print_status("copy with an opaque ctx (no ops)",
                 shrewd_basic_reflect_copy(&raw, &person, &person_type));
    person.ctx = NULL;

    banner("7. cross-TU sharing: TYPE_DECLARE in another .c");
    printf("  reflect_demo_ext.c imports point_type and never sees point_t:\n");
    point.x = 3;
    point.y = 4;
    printf("  shrewd_demo_reflect_ext_sum(&point) -> %d\n", shrewd_demo_reflect_ext_sum(&point));
    shrewd_demo_reflect_ext_dump(&point);

    banner("8. enums and deep comparison");
    const char *mode_name = NULL;
    print_status("enum_find_name(mode_type, 1)",
                 shrewd_basic_reflect_enum_find_name(&mode_type, 1, &mode_name));
    printf("  -> %s\n", mode_name);
    printf("  the %zu constants via enum_at:", shrewd_basic_reflect_enum_count(&mode_type));
    for (size_t i = 0; i < shrewd_basic_reflect_enum_count(&mode_type); ++i) {
        printf(" %s=%lld",
               shrewd_basic_reflect_enum_at(&mode_type, i)->sb_refl_name,
               (long long)shrewd_basic_reflect_enum_at(&mode_type, i)->sb_refl_value);
    }
    printf("\n");
    int64_t mode_value = 0;
    print_status("enum_find_value(\"MODE_AUTO\")",
                 shrewd_basic_reflect_enum_find_value(&mode_type, "MODE_AUTO", &mode_value));
    printf("  -> %lld\n", (long long)mode_value);

    person_t other = {0};
    memset(&other, 0, sizeof(other));
    other = person;
    other.name = "Alice"; /* same content, different pointer */
    bool same = false;
    print_status("equal(person, other)",
                 shrewd_basic_reflect_equal(&person, &other, &person_type, &same));
    printf("  -> %s (strings compare by content)\n", same ? "equal" : "different");
    other.age = 0;
    person.mode = MODE_AUTO;
    other.mode = MODE_ON;
    print_status("equal(after changes)",
                 shrewd_basic_reflect_equal(&person, &other, &person_type, &same));
    printf("  -> %s\n", same ? "equal" : "different");

    banner("9. cyclic graphs: dump, check, copy, destroy");
    loop.data = 1; /* the self-cycle node from section 4, reused here */
    printf("  self-cycle dump (note the ...(cyclic) marker):\n");
    (void)shrewd_basic_reflect_dump(&node_type, &loop, "node");
    node_t *cycle = NULL;
    print_status("copy(self-cycle)", shrewd_basic_reflect_copy((void **)&cycle, &loop, &node_type));
    printf("  clone keeps the cycle: %s\n",
           ((cycle != &loop) && (cycle->next == cycle)) ? "yes" : "no");
    print_status("check(clone)", shrewd_basic_reflect_check(cycle, &node_type));
    print_status("destroy(clone)", shrewd_basic_reflect_destroy(cycle, &node_type));

    banner("10. dynamic arrays: vectors, pointer tables, argv-style");
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_calloc(&block, 1, sizeof(dyn_t))) {
        dyn_t *dyn = block;
        for (size_t i = 0; i < 2; ++i) {
            void *cell = NULL;
            if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_alloc(&cell, sizeof(point_t))) {
                ((point_t *)cell)->x = (int32_t)(i + 1);
                ((point_t *)cell)->y = (int32_t)(i + 2);
            }
            if (0 == i) {
                void *table = NULL;
                (void)shrewd_basic_platform_alloc(&table, 2 * sizeof(point_t *));
                dyn->grid = table;
            }
            dyn->grid[i] = cell;
        }
        dyn->grid_count = 2;
        (void)shrewd_basic_platform_alloc(&block, 3 * sizeof(int32_t));
        dyn->values = block;
        dyn->values[0] = 7;
        dyn->values[1] = 8;
        dyn->values[2] = 9;
        dyn->value_count = 3;
        (void)shrewd_basic_platform_alloc(&block, 3 * sizeof(char *));
        dyn->argv = block;
        dyn->argv[0] = (char *)"ls";
        dyn->argv[1] = (char *)"-l";
        dyn->argv[2] = NULL;
        (void)shrewd_basic_reflect_dump(&dyn_type, dyn, "dyn");

        dyn_t *dyn_clone = NULL;
        print_status("copy(dyn)", shrewd_basic_reflect_copy((void **)&dyn_clone, dyn, &dyn_type));
        bool dyn_same = false;
        print_status("equal(dyn, clone)",
                     shrewd_basic_reflect_equal(dyn, dyn_clone, &dyn_type, &dyn_same));
        printf("  -> %s (tables cloned, strings borrowed)\n", dyn_same ? "equal" : "different");
        print_status("destroy(clone)", shrewd_basic_reflect_destroy(dyn_clone, &dyn_type));
        print_status("destroy(dyn)", shrewd_basic_reflect_destroy(dyn, &dyn_type));
    }

    banner("11. flexible tails: FAM with a linked count member");
    /* A FAM struct must be allocated with room for its tail - sizeof()
       covers the header only. */
    void *tail_block = NULL;
    if (SHREWD_BASIC_ERR_OK
        == shrewd_basic_platform_calloc(&tail_block, 1, sizeof(packet_t) + (3 * sizeof(int32_t)))) {
        packet_t *packet = tail_block;
        packet->len = 3;
        packet->data[0] = 7;
        packet->data[1] = 8;
        packet->data[2] = 9;
        (void)shrewd_basic_reflect_dump(&packet_type, packet, "packet");

        int32_t at = 0;
        print_status(
            "path_get \"data[2]\"",
            shrewd_basic_reflect_path_get(packet, &packet_type, "data[2]", &at, sizeof(at)));
        printf("  -> data[2] = %d\n", at);

        packet_t *packet_clone = NULL;
        print_status("copy(packet)",
                     shrewd_basic_reflect_copy((void **)&packet_clone, packet, &packet_type));
        bool packet_same = false;
        print_status("equal(packet, clone)",
                     shrewd_basic_reflect_equal(packet, packet_clone, &packet_type, &packet_same));
        printf("  -> %s (clone owns its own tail)\n", packet_same ? "equal" : "different");
        print_status("destroy(clone)", shrewd_basic_reflect_destroy(packet_clone, &packet_type));
        print_status("destroy(packet)", shrewd_basic_reflect_destroy(packet, &packet_type));
    }

    banner("12. traversal: event-by-event visit");
    printf("  walking person (ENTER/LEAF/EXIT, arrays bracketed):\n");
    (void)shrewd_basic_reflect_visit(&person, &person_type, demo_visit_print, NULL);
    printf("  walking the self-cycle node (CYCLE instead of expanding):\n");
    (void)shrewd_basic_reflect_visit(&loop, &node_type, demo_visit_print, NULL);

    banner("13. diff and hash: locate differences, fold values");
    char diff_path[64] = {0};
    print_status(
        "diff(person, other)",
        shrewd_basic_reflect_diff(&person, &other, &person_type, diff_path, sizeof(diff_path)));
    printf("  first difference at \"%s\"\n", diff_path);
    other = person;
    print_status(
        "diff(person, copy)",
        shrewd_basic_reflect_diff(&person, &other, &person_type, diff_path, sizeof(diff_path)));
    size_t person_hash = 0;
    size_t other_hash = 0;
    (void)shrewd_basic_reflect_hash(&person, &person_type, &person_hash);
    (void)shrewd_basic_reflect_hash(&other, &person_type, &other_hash);
    printf("  person=0x%zx other=0x%zx -> %s\n",
           person_hash,
           other_hash,
           (person_hash == other_hash) ? "same (equal objects hash equal)" : "different");
    other.name = "Alicia";
    (void)shrewd_basic_reflect_hash(&other, &person_type, &other_hash);
    printf("  after changing one string: person=0x%zx other=0x%zx\n", person_hash, other_hash);

    banner("14. string buffers: char[N] with C-string semantics");
    text_t text_a = {0};
    text_t text_b = {0};
    memset(&text_a, 0, sizeof(text_a));
    memset(&text_b, 0, sizeof(text_b));
    memcpy(text_a.title, "hello", 6);
    memcpy(text_b.title, "hello", 6);
    text_b.title[6] = 'x'; /* bytes past the NUL are ignored */
    text_a.id = 1;
    text_b.id = 1;
    printf("  text_a and text_b differ only past the NUL:\n");
    (void)shrewd_basic_reflect_dump(&text_type, &text_a, "text_a");
    (void)shrewd_basic_reflect_dump(&text_type, &text_b, "text_b");
    bool text_same = false;
    print_status("equal(text_a, text_b)",
                 shrewd_basic_reflect_equal(&text_a, &text_b, &text_type, &text_same));
    printf("  -> %s\n", text_same ? "equal" : "different");
    memcpy(text_b.title, "world", 6);
    print_status(
        "diff(text_a, text_b)",
        shrewd_basic_reflect_diff(&text_a, &text_b, &text_type, diff_path, sizeof(diff_path)));
    printf("  first difference at \"%s\"\n", diff_path);

    banner("15. owned strings: copy clones content, destroy frees it");
    void *text_block = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_alloc(&text_block, 6)) {
        owned_t *owned = NULL;
        void *owned_block = NULL;
        (void)shrewd_basic_platform_calloc(&owned_block, 1, sizeof(owned_t));
        owned = owned_block;
        memcpy(text_block, "hello", 6);
        owned->title = text_block;
        owned->borrowed = "static literal";
        (void)shrewd_basic_reflect_dump(&owned_type, owned, "owned");
        owned_t *owned_clone = NULL;
        print_status("copy(owned)",
                     shrewd_basic_reflect_copy((void **)&owned_clone, owned, &owned_type));
        printf("  clone title=\"%s\" at %p (source at %p) -> deep copy\n",
               owned_clone->title,
               (void *)owned_clone->title,
               (void *)owned->title);
        printf("  clone borrowed %s -> aliased\n",
               (owned_clone->borrowed == owned->borrowed) ? "is the same pointer"
                                                          : "differs (unexpected)");
        bool owned_same = false;
        print_status("equal(owned, clone)",
                     shrewd_basic_reflect_equal(owned, owned_clone, &owned_type, &owned_same));
        printf("  -> %s (strings compare by content)\n", owned_same ? "equal" : "different");
        print_status("destroy(clone)", shrewd_basic_reflect_destroy(owned_clone, &owned_type));
        print_status("destroy(owned)", shrewd_basic_reflect_destroy(owned, &owned_type));
    }

    banner("16. bit groups: named bits inside a register word");
    reg_t reg = {0};
    memset(&reg, 0, sizeof(reg));
    reg.cfg = (1u << 0) | (5u << 1) | (100u << 8) | 0xf0u | 0xab000000u;
    reg.value = 7;
    printf("  raw unit cfg = 0x%08x (unused bits carry garbage)\n", reg.cfg);
    (void)shrewd_basic_reflect_dump(&reg_type, &reg, "reg");
    uint32_t mode = 0;
    print_status("field_get \"mode\"",
                 shrewd_basic_reflect_field_get(
                     &reg, shrewd_basic_reflect_type_field_at(&reg_type, 1), &mode, sizeof(mode)));
    printf("  -> mode = %u\n", mode);
    mode = 3;
    print_status("field_set \"mode\" = 3",
                 shrewd_basic_reflect_field_set(
                     &reg, shrewd_basic_reflect_type_field_at(&reg_type, 1), &mode, sizeof(mode)));
    printf("  cfg after write = 0x%08x (only bits 1-3 changed)\n", reg.cfg);
    reg_t reg_other = {0};
    memset(&reg_other, 0, sizeof(reg_other));
    reg_other.cfg = (reg.cfg & 0xffffu) | 0x07000000u; /* same groups, other garbage */
    reg_other.value = 7;
    bool reg_same = false;
    print_status("equal(reg, other)",
                 shrewd_basic_reflect_equal(&reg, &reg_other, &reg_type, &reg_same));
    printf("  -> %s (unused unit bits are invisible)\n", reg_same ? "equal" : "different");

    banner("17. multi-dimension arrays: rows as array value types");
    matrix_t mx = {0};
    memset(&mx, 0, sizeof(mx));
    for (size_t r = 0; r < 2; ++r) {
        for (size_t c = 0; c < 3; ++c) {
            mx.m[r][c] = (int32_t)((r * 3) + c) + 1;
        }
    }
    mx.id = 7;
    (void)shrewd_basic_reflect_dump(&matrix_type, &mx, "matrix");
    int32_t cell = 0;
    print_status("path_get \"m[1][2]\"",
                 shrewd_basic_reflect_path_get(&mx, &matrix_type, "m[1][2]", &cell, sizeof(cell)));
    printf("  -> m[1][2] = %d\n", cell);
    matrix_t mx_clone = mx;
    mx_clone.m[0][0] = 99;
    print_status(
        "diff(matrix, clone)",
        shrewd_basic_reflect_diff(&mx, &mx_clone, &matrix_type, diff_path, sizeof(diff_path)));
    printf("  first difference at \"%s\"\n", diff_path);

    banner("18. tagged unions: one variant active per tag");
    void *msg_block = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_calloc(&msg_block, 1, sizeof(msg_t))) {
        msg_t *msg = msg_block;
        msg->kind = MSG_POINT;
        msg->body.point.x = 1;
        msg->body.point.y = 2;
        printf("  active variant MSG_POINT:\n");
        (void)shrewd_basic_reflect_dump(&msg_type, msg, "msg");
        msg->kind = MSG_RAW; /* no variant describes this tag */
        printf("  a tag without a variant:\n");
        (void)shrewd_basic_reflect_dump(&msg_type, msg, "msg");
        msg->kind = MSG_TEXT;
        void *body_text = NULL;
        (void)shrewd_basic_platform_alloc(&body_text, 6);
        memcpy(body_text, "hello", 6);
        msg->body.text = body_text;
        printf("  active variant MSG_TEXT (owned string):\n");
        (void)shrewd_basic_reflect_dump(&msg_type, msg, "msg");
        msg_t *msg_clone = NULL;
        print_status("copy(msg)", shrewd_basic_reflect_copy((void **)&msg_clone, msg, &msg_type));
        printf("  clone text=\"%s\" at %p (source at %p) -> deep copy\n",
               msg_clone->body.text,
               (void *)msg_clone->body.text,
               (void *)msg->body.text);
        bool msg_same = false;
        print_status("equal(msg, clone)",
                     shrewd_basic_reflect_equal(msg, msg_clone, &msg_type, &msg_same));
        printf("  -> %s (only the active variant compares)\n", msg_same ? "equal" : "different");
        print_status("destroy(clone)", shrewd_basic_reflect_destroy(msg_clone, &msg_type));
        print_status("destroy(msg)", shrewd_basic_reflect_destroy(msg, &msg_type));
    }

    banner("19. caller-hosted lookup lists: register + *_list queries");
    {
        static const shrewd_basic_reflect_list_ops_t ops
            = {demo_reg_insert, demo_reg_find_type, demo_reg_find_name};
        demo_reg_t registry = {0};
        shrewd_basic_reflect_list_t list = {0};
        const shrewd_basic_reflect_type_t *by_name = NULL;
        const shrewd_basic_reflect_field_t *by_field = NULL;
        point_t origin = {0};
        int32_t x = 0;

        memset(&registry, 0, sizeof(registry));
        origin.x = 3;
        origin.y = 4;
        list.sb_refl_ops = &ops;
        list.sb_refl_ctx = &registry;
        print_status("list_register(point_type, \"point\")",
                     shrewd_basic_reflect_list_register(&list, &point_type, "point"));
        print_status("list_register(point_type, \"vec2\") (alias)",
                     shrewd_basic_reflect_list_register(&list, &point_type, "vec2"));
        print_status("type_find_list(\"vec2\")",
                     shrewd_basic_reflect_type_find_list(&list, "vec2", &by_name));
        printf("  \"vec2\" -> %s (same descriptor as \"point\")\n", by_name->sb_refl_name);
        print_status("find_field_list(point, \"y\")",
                     shrewd_basic_reflect_type_find_field_list(&list, &point_type, "y", &by_field));
        printf("  y at offset %zu (index built in the caller's entries)\n",
               by_field->sb_refl_offset);
        print_status(
            "path_get_list(point, \"x\")",
            shrewd_basic_reflect_path_get_list(&list, &origin, &point_type, "x", &x, sizeof(x)));
        printf("  origin.x = %d\n", x);
        print_status("find_field_list(point, \"z\") misses",
                     shrewd_basic_reflect_type_find_field_list(&list, &point_type, "z", &by_field));
    }

    banner("20. registry container and describe: name -> type out of the box");
    {
        SHREWD_BASIC_REFLECT_REGISTRY_DEFINE(demo_registry, 4);
        shrewd_basic_reflect_list_t reg_list = {0};
        const shrewd_basic_reflect_type_t *by_name = NULL;

        print_status("registry_bind(&registry, &list)",
                     shrewd_basic_reflect_registry_bind(&demo_registry, &reg_list));
        print_status("list_register(point_type, \"vec\")",
                     shrewd_basic_reflect_list_register(&reg_list, &point_type, "vec"));
        print_status("type_find_list(\"vec\")",
                     shrewd_basic_reflect_type_find_list(&reg_list, "vec", &by_name));
        printf("  \"vec\" -> %s\n", by_name->sb_refl_name);
        printf("  describe(point_type):\n");
        print_status("describe(point_type)", shrewd_basic_reflect_describe(&point_type));
    }

    printf("\nDemo done. See docs/reflect.md for the full reference.\n");
    return 0;
}
