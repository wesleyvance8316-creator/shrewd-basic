#include "reflect_demo_ext.h"

#include "shrewd-basic/reflect/reflect.h"

/* Import the type defined in reflect_demo.c: same descriptor, same fields. */
SHREWD_BASIC_REFLECT_TYPE_DECLARE(point_type);

int32_t shrewd_demo_reflect_ext_sum(const void *point)
{
    const shrewd_basic_reflect_field_t *field = NULL;
    int32_t x = 0;
    int32_t y = 0;
    if ((SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_type_find_field(&point_type, "x", &field))
        || (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_field_get(point, field, &x, sizeof(x)))) {
        return 0;
    }
    if ((SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_type_find_field(&point_type, "y", &field))
        || (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_field_get(point, field, &y, sizeof(y)))) {
        return 0;
    }
    return x + y;
}

void shrewd_demo_reflect_ext_dump(const void *point)
{
    (void)shrewd_basic_reflect_dump(&point_type, point, "point@ext");
}
