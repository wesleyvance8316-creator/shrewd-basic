#include "test_reflect_ext.h"

#include "shrewd-basic/reflect/reflect.h"

/* Import the type defined in test_reflect.c: same descriptor, same fields. */
SHREWD_BASIC_REFLECT_TYPE_DECLARE(g_point_reflect);

int shrewd_test_reflect_ext_get_x(const void *point, int32_t *out) {
    const shrewd_basic_reflect_field_t *field = NULL;
    int status = shrewd_basic_reflect_type_find_field(&g_point_reflect, "x", &field);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_reflect_field_get(point, field, out, sizeof(*out));
}

int shrewd_test_reflect_ext_set_x(void *point, int32_t value) {
    const shrewd_basic_reflect_field_t *field = NULL;
    int status = shrewd_basic_reflect_type_find_field(&g_point_reflect, "x", &field);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_reflect_field_set(point, field, &value, sizeof(value));
}

int shrewd_test_reflect_ext_field_count(void) {
    return (int)g_point_reflect.sb_refl_field_count;
}
