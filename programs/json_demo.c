/**
 * @file json_demo.c
 * @brief Walkthrough of the json module: value model and struct mapping.
 *
 * Three sections: (1) encode a reflect-described struct to JSON text,
 * (2) decode text back into a struct and release it, (3) use the DOM
 * directly - parse an arbitrary document, query and modify it, write it
 * back. The struct mapping covers scalars, enums, strings, text buffers
 * and a tagged union (F1 form).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/json/json.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* --- fixture ---------------------------------------------------------- */

typedef enum {
    MSG_TEXT = 0,
    MSG_POINT = 1
} msg_kind_t;

typedef struct {
    int32_t x;
    int32_t y;
} point_t;

typedef union {
    char *text;
    point_t point;
} msg_body_t;

typedef struct {
    msg_kind_t kind;
    int32_t id;
    char tag[8];
    const char *name;
    msg_body_t body;
} msg_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(msg_kind_reflect, msg_kind_t,
    SHREWD_BASIC_REFLECT_ENUM_VALUE(MSG_TEXT),
    SHREWD_BASIC_REFLECT_ENUM_VALUE(MSG_POINT));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(point_reflect, point_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

SHREWD_BASIC_REFLECT_UNION_DEFINE(msg_body_reflect, msg_body_t,
    SHREWD_BASIC_REFLECT_UNION_VARIANT(MSG_TEXT,
        SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(msg_body_t, text)),
    SHREWD_BASIC_REFLECT_UNION_VARIANT(MSG_POINT,
        SHREWD_BASIC_REFLECT_FIELD_STRUCT(msg_body_t, point, point_reflect)));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(msg_reflect, msg_t,
    SHREWD_BASIC_REFLECT_FIELD_ENUM(msg_t, kind, msg_kind_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(msg_t, id),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(msg_t, tag),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(msg_t, name),
    SHREWD_BASIC_REFLECT_FIELD_UNION(msg_t, body, msg_body_reflect, kind));

/* Policy: a key alias for id and a compact unwrapped union body. */
SHREWD_BASIC_JSON_DEFINE(msg_json, msg_reflect,
    SHREWD_BASIC_JSON_KEY(id, "msg_id"),
    SHREWD_BASIC_JSON_OMIT_EMPTY(name),
    SHREWD_BASIC_JSON_UNION_STYLE(body, SHREWD_BASIC_JSON_UNION_UNWRAP));

static void print_status(const char *what, int status)
{
    printf("  %-36s -> %s (%d)\n", what, shrewd_basic_error_name(status),
           status);
}

int main(void)
{
    printf("=== 1. encode: struct -> JSON text ===\n");
    msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.kind = MSG_POINT;
    msg.id = 42;
    memcpy(msg.tag, "hi", 3);
    msg.name = "demo";
    msg.body.point.x = 1;
    msg.body.point.y = 2;
    char *text = NULL;
    print_status("encode(msg)", shrewd_basic_json_encode(&msg, &msg_reflect, NULL,
                                                         &text, NULL, NULL));
    printf("  compact: %s\n", text);
    shrewd_basic_json_text_free(text);
    {
        shrewd_basic_json_options_t options;
        memset(&options, 0, sizeof(options));
        options.pretty = 1;
        print_status("encode(msg, pretty)",
                     shrewd_basic_json_encode(&msg, &msg_reflect, NULL, &text,
                                              &options, NULL));
        printf("%s\n", text);
        shrewd_basic_json_text_free(text);
    }

    printf("\n=== 2. decode: JSON text -> struct ===\n");
    {
        const char *incoming =
                "{\"kind\":\"MSG_TEXT\",\"id\":7,\"tag\":\"ok\","
                "\"name\":\"n\",\"body\":{\"text\":\"hello\"}}";
        shrewd_basic_json_error_t error;
        void *block = NULL;
        (void)shrewd_basic_platform_calloc(&block, 1, sizeof(msg_t));
        print_status("decode(text)",
                     shrewd_basic_json_decode(incoming, strlen(incoming), block,
                                              &msg_reflect, NULL, NULL, &error));
        msg_t *decoded = block;
        printf("  kind=%d id=%d tag=\"%s\" name=\"%s\" body.text=\"%s\"\n",
               (int)decoded->kind, decoded->id, decoded->tag, decoded->name,
               decoded->body.text);
        print_status("json_free", shrewd_basic_json_free(decoded, &msg_reflect));
        (void)shrewd_basic_platform_free(&block);
    }

    printf("\n=== 3. value model: parse, query, modify, write back ===\n");
    {
        const char *doc = "{\"user\":{\"name\":\"wes\",\"roles\":[\"dev\"]},"
                          "\"n\":3}";
        shrewd_basic_json_value_t *root = NULL;
        shrewd_basic_json_value_t *roles = NULL;
        shrewd_basic_json_value_t *user = NULL;
        print_status("parse(doc)",
                     shrewd_basic_json_parse(doc, strlen(doc), &root, NULL,
                                             NULL));
        user = shrewd_basic_json_get(root, "user");
        printf("  user.name = %s\n",
               shrewd_basic_json_to_string(shrewd_basic_json_get(user, "name")));
        roles = shrewd_basic_json_get(user, "roles");
        printf("  roles has %zu entries\n", shrewd_basic_json_count(roles));
        {
            shrewd_basic_json_value_t *extra = NULL;
            (void)shrewd_basic_json_new_string("ops", &extra);
            (void)shrewd_basic_json_append(roles, extra);
        }
        {
            shrewd_basic_json_value_t *n = NULL;
            (void)shrewd_basic_json_new_i64(4, &n);
            (void)shrewd_basic_json_set(root, "n", n);
        }
        print_status("serialize(root)",
                     shrewd_basic_json_serialize(root, &text, NULL, NULL));
        printf("  -> %s\n", text);
        shrewd_basic_json_text_free(text);
        shrewd_basic_json_value_free(root);
    }

    printf("\n=== 4. policy: key aliases, omission, union styles ===\n");
    {
        char *policed = NULL;
        msg_t tagged;
        memset(&tagged, 0, sizeof(tagged));
        tagged.kind = MSG_TEXT;
        tagged.id = 9;
        tagged.name = NULL; /* OMIT_EMPTY */
        memcpy(tagged.tag, "ok", 3);
        tagged.body.text = "hi";
        print_status("encode(msg, policy)",
                     shrewd_basic_json_encode(&tagged, &msg_reflect, &msg_json,
                                              &policed, NULL, NULL));
        printf("  -> %s\n", policed);
        shrewd_basic_json_text_free(policed);
        {
            const char *aliased =
                    "{\"msg_id\":5,\"kind\":\"MSG_TEXT\",\"body\":\"hello\"}";
            void *block = NULL;
            (void)shrewd_basic_platform_calloc(&block, 1, sizeof(msg_t));
            print_status("decode(aliased)",
                         shrewd_basic_json_decode(aliased, strlen(aliased), block,
                                                  &msg_reflect, &msg_json, NULL,
                                                  NULL));
            msg_t *decoded = block;
            printf("  msg_id -> id=%d body.text=\"%s\"\n", decoded->id,
                   decoded->body.text);
            print_status("json_free",
                         shrewd_basic_json_free(decoded, &msg_reflect));
            (void)shrewd_basic_platform_free(&block);
        }
    }

    printf("\nDemo done. See docs/json.md for the full reference.\n");
    return 0;
}
