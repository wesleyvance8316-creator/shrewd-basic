# reflect API 总览

接口参考速查。所有函数以**全名 + 参数**书写(如
shrewd_basic_reflect_type_find_field(type, name, out)),便于 Doxygen
为每个名字生成跳转;紧随其后的代码块是精确原型。错误码约定:

| 错误码 | 含义 |
|---|---|
| `SHREWD_BASIC_ERR_OK`(0) | 成功 |
| `SHREWD_BASIC_ERR_MEM_NOMEM`(-1) | 分配失败 |
| `SHREWD_BASIC_ERR_ARG_INVALID`(-2) | 参数非法(含 NULL) |
| `SHREWD_BASIC_ERR_KEY_NOTFOUND`(-3) | 查不到(字段/成员/路径/枚举常量) |
| `SHREWD_BASIC_ERR_REFLECT_MISMATCH`(-200) | 值宽与要求不符 |
| `SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED`(-201) | 操作需要 ops 回调 |

**承载性不变量**:一切数据访问走 `field->sb_refl_size` 与
`count/stride`,`type->sb_refl_size` 仅信息性。

## 0. 数据模型

| 类型 | 作用 | 关键成员 |
|---|---|---|
| `shrewd_basic_reflect_type_t` | 类型描述符(全 const,可放 flash) | `sb_refl_kind`、`sb_refl_name`、`sb_refl_size`、`sb_refl_align`(C 类型对齐,0=未知)、`sb_refl_abi`(作者头文件的 `SHREWD_BASIC_REFLECT_ABI` 版本)、`sb_refl_fields`/`sb_refl_field_count`、`sb_refl_enum`、`sb_refl_ops`、`sb_refl_elem`(ARRAY 行)、`sb_refl_variants`(UNION) |
| `shrewd_basic_reflect_field_t` | 字段描述符 | `sb_refl_name`、`sb_refl_type`、`sb_refl_offset`、`sb_refl_size`、`sb_refl_count`、`sb_refl_stride`、`sb_refl_flags`、`sb_refl_ops`、`sb_refl_limit_offset`/`sb_refl_limit_size`、`sb_refl_bit_offset`/`sb_refl_bit_width` |
| `shrewd_basic_reflect_variant_t` | union 变体 | `sb_refl_tag`、`sb_refl_name`、变体字段表 |
| `shrewd_basic_reflect_enum_entry_t` | 枚举常量 | `sb_refl_name`、`sb_refl_value` |
| `shrewd_basic_reflect_ops_t` | 类型/字段级回调 vtable | `value_size`、`get`、`set`、`check`、`equal`、`render`、`copy`、`destroy`、`hash` |

字段形态标志(`sb_refl_flags`):

| 标志宏 | 含义 |
|---|---|
| `SHREWD_BASIC_REFLECT_FIELD_F_ARRAY` | 定长数组 |
| `SHREWD_BASIC_REFLECT_FIELD_F_PTR` | 指针(寻址时解引用) |
| `SHREWD_BASIC_REFLECT_FIELD_F_DARRAY` | 动态数组(计数成员驱动) |
| `SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL` | NULL 结尾数组 |
| `SHREWD_BASIC_REFLECT_FIELD_F_FLEX` | 柔性尾部(FAM) |
| `SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF` | 文本缓冲(`char[N]` 或宽 `char16_t[N]`/`char32_t[N]`,内容到 NUL 码元为止) |
| `SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED` | 自有字符串指针(char 或宽字符) |
| `SHREWD_BASIC_REFLECT_FIELD_F_BITS` | 整数单元内的位组 |
| `SHREWD_BASIC_REFLECT_FIELD_F_UNION` | 标签化 union 字段 |

## 1. 注册宏(编译期)

描述符定义/导入:

| 宏 | 作用 | 参数 |
|---|---|---|
| `SHREWD_BASIC_REFLECT_TYPE_DEFINE(var, struct_type, ...)` | 定义结构体描述符(外部链接,带前置声明可自引用) | `var` 描述符变量名;`struct_type` C 结构体;`...` 字段宏列表 |
| `SHREWD_BASIC_REFLECT_TYPE_DECLARE(var)` | 跨 TU 导入描述符 | `var` 描述符名 |
| `SHREWD_BASIC_REFLECT_ARRAY_DEFINE(var, elem_ref, elem_type, count)` | 多维数组的"行"值类型 | 行描述符名、元素描述符、元素 C 类型、行长 |
| `SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(var, struct_type, ops_ref, ...)` | 带类型级 ops 的描述符 | 同 TYPE_DEFINE + `ops_ref` |
| `SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(var, type_name, ops_ref)` | 不透明类型(值语义由 ops 解释) | 描述符名、类型名串、ops |
| `SHREWD_BASIC_REFLECT_ENUM_DEFINE(var, enum_type, ...)` | 枚举描述符(名↔值双向) | 描述符名、枚举 C 类型、`ENUM_VALUE` 列表 |
| `SHREWD_BASIC_REFLECT_ENUM_VALUE(value)` | 枚举常量条目 | 常量名(值同名) |

字段宏(核心参数均为 `struct_type, member`;宏参数名不得与描述符成员名
重合):

| 宏 | 覆盖形态 |
|---|---|
| `SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(struct_type, member)` | 标量/字符串(`_Generic` 自动) |
| `SHREWD_BASIC_REFLECT_FIELD_STRUCT(struct_type, member, ref)` | 嵌套结构体 |
| `SHREWD_BASIC_REFLECT_FIELD_ARRAY(struct_type, member, ref)` | 定长数组 |
| `SHREWD_BASIC_REFLECT_FIELD_PTR(struct_type, member, ref)` | 指针 |
| `SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY(struct_type, member, ref)` | 指针数组 |
| `SHREWD_BASIC_REFLECT_FIELD_ENUM(struct_type, member, ref)` | 枚举成员 |
| `SHREWD_BASIC_REFLECT_FIELD_DARRAY(struct_type, member, count_member, ref)` | 动态数组 |
| `SHREWD_BASIC_REFLECT_FIELD_PTR_DARRAY(struct_type, member, count_member, ref)` | 指针动态数组 |
| `SHREWD_BASIC_REFLECT_FIELD_SENTINEL(struct_type, member, ref)` | NULL 结尾数组 |
| `SHREWD_BASIC_REFLECT_FIELD_PTR_SENTINEL(struct_type, member, ref)` | 指针的 NULL 结尾数组 |
| `SHREWD_BASIC_REFLECT_FIELD_FLEX(struct_type, member, count_member, ref)` | 柔性尾部(FAM) |
| `SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(struct_type, member)` | `char[N]` 文本缓冲 |
| `SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(struct_type, member)` | 自有字符串指针 |
| `SHREWD_BASIC_REFLECT_FIELD_STRING16(struct_type, member)` | `char16_t *` 宽字符串(借用) |
| `SHREWD_BASIC_REFLECT_FIELD_STRING16_OWNED(struct_type, member)` | `char16_t *` 宽字符串(自有) |
| `SHREWD_BASIC_REFLECT_FIELD_STRING16_BUFFER(struct_type, member)` | `char16_t[N]` 宽文本缓冲 |
| `SHREWD_BASIC_REFLECT_FIELD_STRING32(struct_type, member)` | `char32_t *` 宽字符串(借用) |
| `SHREWD_BASIC_REFLECT_FIELD_STRING32_OWNED(struct_type, member)` | `char32_t *` 宽字符串(自有) |
| `SHREWD_BASIC_REFLECT_FIELD_STRING32_BUFFER(struct_type, member)` | `char32_t[N]` 宽文本缓冲 |
| `SHREWD_BASIC_REFLECT_FIELD_WSTRING/_OWNED/_BUFFER(struct_type, member)` | `wchar_t` 形态,单元宽度随 `WCHAR_MAX` 编译期归入 16/32 |
| `SHREWD_BASIC_REFLECT_FIELD_BITS(struct_type, unit, name, bit_offset, bit_width)` | 位组(`name` 为标签) |
| `SHREWD_BASIC_REFLECT_FIELD_BITS_UNIT(struct_type, name, unit_ref, unit_offset, unit_size, bit_offset, bit_width)` | 位组(显式单元放置;C 位域用,`unit_ref` 不带 `&`) |
| `SHREWD_BASIC_REFLECT_UNION_DEFINE(var, union_type, ...)` | union 描述符(变体表) |
| `SHREWD_BASIC_REFLECT_UNION_VARIANT(tag_value, ...)` | union 变体条目 |
| `SHREWD_BASIC_REFLECT_FIELD_UNION(struct_type, member, union_var, tag_member)` | 标签化 union 字段 |
| `*_OPS` 变体(如 `SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE_OPS(struct_type, member, ops_ref)` 等) | 同名形态 + 字段级 ops |
| `FIELD_*` 对匿名成员 | C11 无名 struct/union 成员**平铺**描述:子成员名直接作 `member` 传入(如 `FIELD_PRIMITIVE(S, ax)`),offset/size 穿透匿名父级 |
| `SHREWD_BASIC_REFLECT_REGISTRY_DEFINE(var, capacity)` | 声明一个内置条目数组的注册表(文件域,见第 6 节) |

## 2. 元数据查询

### shrewd_basic_reflect_kind_name(kind)

```c
const char *shrewd_basic_reflect_kind_name(shrewd_basic_reflect_kind_t kind);
```

shrewd_basic_reflect_kind_name() 返回 kind 的名字。

- **传参**:`kind` — 类型种类枚举。
- **返回**:`const char *` 静态串(`"int32"`、`"bool"` 等),未知 kind
  返回兜底名。不返回错误码。

### shrewd_basic_reflect_type_field_count(type)

```c
size_t shrewd_basic_reflect_type_field_count(
        const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_type_field_count() 报告类型描述的字段数。

- **传参**:`type` — 类型描述符。
- **返回**:`size_t` 字段数;标量/枚举/`type == NULL` 时为 0。

### shrewd_basic_reflect_type_field_at(type, index)

```c
const shrewd_basic_reflect_field_t *shrewd_basic_reflect_type_field_at(
        const shrewd_basic_reflect_type_t *type, size_t index);
```

shrewd_basic_reflect_type_field_at() 取下标处的字段;与
shrewd_basic_reflect_type_field_count() 组成不触碰描述符布局的字段遍历。

- **传参**:`type` — 类型描述符;`index` — 0 起下标。
- **返回**:`const shrewd_basic_reflect_field_t *` 指向静态字段描述符;
  越界或 `type == NULL` 返回 NULL。

### shrewd_basic_reflect_enum_count(type)

```c
size_t shrewd_basic_reflect_enum_count(const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_enum_count() 报告 ENUM 描述符的常量个数。

- **传参**:`type` — 类型描述符。
- **返回**:`size_t` 常量数;非枚举/NULL 时为 0。

### shrewd_basic_reflect_enum_at(type, index)

```c
const shrewd_basic_reflect_enum_entry_t *shrewd_basic_reflect_enum_at(
        const shrewd_basic_reflect_type_t *type, size_t index);
```

shrewd_basic_reflect_enum_at() 取下标处的枚举常量。

- **传参**:`type` — 类型描述符;`index` — 0 起下标。
- **返回**:`const shrewd_basic_reflect_enum_entry_t *`;越界或非枚举返回
  NULL。

### shrewd_basic_reflect_enum_find_name(type, value, out_name)

```c
int shrewd_basic_reflect_enum_find_name(const shrewd_basic_reflect_type_t *type,
                                        int64_t value,
                                        const char **out_name);
```

shrewd_basic_reflect_enum_find_name() 枚举值→常量名。

- **传参**:`type` — ENUM 描述符;`value` — 枚举值;`out_name` — 出参,
  命中时常量名(静态串)。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_KEY_NOTFOUND` 无此值;
  `SHREWD_BASIC_ERR_ARG_INVALID` 参数 NULL 或非枚举。

### shrewd_basic_reflect_enum_find_value(type, name, out_value)

```c
int shrewd_basic_reflect_enum_find_value(const shrewd_basic_reflect_type_t *type,
                                         const char *name,
                                         int64_t *out_value);
```

shrewd_basic_reflect_enum_find_value() 常量名→枚举值。

- **传参**:`type` — ENUM 描述符;`name` — 常量名;`out_value` — 出参。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_KEY_NOTFOUND` 无此名;
  `SHREWD_BASIC_ERR_ARG_INVALID` 参数 NULL 或非枚举。

### shrewd_basic_reflect_type_find_field(type, name, out)

```c
int shrewd_basic_reflect_type_find_field(const shrewd_basic_reflect_type_t *type,
                                         const char *name,
                                         const shrewd_basic_reflect_field_t **out);
```

shrewd_basic_reflect_type_find_field() 按名扁平查字段(单层)。经
`_Thread_local` 排序索引二分加速,多线程安全。

- **传参**:`type` — 类型描述符;`name` — 字段名(C 串);`out` — 出参,
  命中时指向 `type->sb_refl_fields` 里的静态字段描述符。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_KEY_NOTFOUND` 无匹配;
  `SHREWD_BASIC_ERR_ARG_INVALID` 参数 NULL。

### shrewd_basic_reflect_field_align(field)

```c
size_t shrewd_basic_reflect_field_align(
        const shrewd_basic_reflect_field_t *field);
```

shrewd_basic_reflect_field_align() 给出字段存储的有效对齐:指针形态
(`F_PTR`、`F_DARRAY` 表槽)为 `alignof(void *)`,其余取字段类型的
`sb_refl_align`。

- **传参**:`field` — 字段描述符。
- **返回**:`size_t` 对齐;未知或参数 NULL 返回 0。不返回错误码。

### shrewd_basic_reflect_check_type(type)

```c
int shrewd_basic_reflect_check_type(const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_check_type() 校验描述符合法性(一层深):ABI 版本
(`SHREWD_BASIC_REFLECT_ABI`)、kind 合法、字段表自洽(名字/类型引用/偏移
与尺寸不越界/stride 非零)、位组不越单元、枚举/数组/union 引用形态。
用于测试与启动期,抓过期或手改坏的描述符。

- **传参**:`type` — 类型描述符。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_REFLECT_MISMATCH`
  不变量破坏(违规名记入日志);`SHREWD_BASIC_ERR_ARG_INVALID` 参数 NULL。

## 3. 值读写(原始字节)

### shrewd_basic_reflect_field_get(object, field, out_value, value_size)

```c
int shrewd_basic_reflect_field_get(const void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   void *out_value, size_t value_size);
```

shrewd_basic_reflect_field_get() 把字段的原始字节拷贝出来。

- **传参**:`object` — 声明 `field` 的结构体实例;`field` — 字段描述符;
  `out_value` — 接收缓冲;`value_size` — 必须等于
  `field->sb_refl_size`(数组为总字节)。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_REFLECT_MISMATCH` 尺寸
  不符;`SHREWD_BASIC_ERR_ARG_INVALID` 参数 NULL。位组为**提取**语义
  (值已移出位段);字符串字段读出的是 `char *` 指针本身。

### shrewd_basic_reflect_field_set(object, field, value, value_size)

```c
int shrewd_basic_reflect_field_set(void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   const void *value, size_t value_size);
```

shrewd_basic_reflect_field_set() 把原始字节写入字段。传参与返回同
shrewd_basic_reflect_field_get();位组为**注入**语义(保留单元其余位)。

### shrewd_basic_reflect_value_get(object, field, out_value, value_size)

```c
int shrewd_basic_reflect_value_get(const void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   void *out_value, size_t value_size);
```

shrewd_basic_reflect_value_get() 经 `ops->get` 读目标的规范值。

- **传参**:`object`、`field` 同上;`out_value` — 接收缓冲;
  `value_size` — 必须等于 `ops->value_size`。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_REFLECT_MISMATCH` 尺寸
  不符;`SHREWD_BASIC_ERR_KEY_NOTFOUND` 指针字段为 NULL;
  `SHREWD_BASIC_ERR_ARG_INVALID` 无 `ops->get` 或数组字段。指针字段读
  指目标,值字段读字段本身。

### shrewd_basic_reflect_value_set(object, field, value, value_size)

```c
int shrewd_basic_reflect_value_set(void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   const void *value, size_t value_size);
```

shrewd_basic_reflect_value_set() 经 `ops->set` 写目标的规范值。传参与
返回同 shrewd_basic_reflect_value_get()。

## 4. 打印

### shrewd_basic_reflect_describe(type)

```c
int shrewd_basic_reflect_describe(const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_describe() 打印**描述符本身**(不是对象)的自述,
走 "reflect" log 模块单条 INFO 记录:头行(名/kind/尺寸/对齐)+ 每字段
一行(名、形态标记如 `*point_t`/`int32[3]`/`*T[]`、偏移、尺寸、对齐、
标志注记 owned/stringbuf/bits/union/flex);枚举列常量、数组行示元素形态、
union 按变体分组。注册调试与文档素材用。

- **传参**:`type` — 类型描述符。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_MEM_NOMEM`;
  `SHREWD_BASIC_ERR_ARG_INVALID`。

### shrewd_basic_reflect_layout(type, fn, ctx)

```c
int shrewd_basic_reflect_layout(const shrewd_basic_reflect_type_t *type,
                                shrewd_basic_reflect_layout_fn fn, void *ctx);
```

shrewd_basic_reflect_layout() 报告 STRUCT 的内存布局(pahole-lite):按偏移升序
逐字段回调 `shrewd_basic_reflect_layout_entry_t`(字段、偏移、尺寸、有效对齐、
前置填充)。位组同址重叠报 pad_before 0;尾部填充 = 类型尺寸 - 末字段结束;嵌套
结构体作单条目(用其描述符再调一层)。零分配,回调返回非 OK 即中止并透传。

- **传参**:`type` — STRUCT 描述符;`fn` — `shrewd_basic_reflect_layout_fn`
  回调;`ctx` — 回调上下文。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_ARG_INVALID`;回调的
  中止码。

### shrewd_basic_reflect_dump(type, object, label) 宏 / shrewd_basic_reflect_dump_write(type, object, label, file, line, func)

```c
int shrewd_basic_reflect_dump_write(const shrewd_basic_reflect_type_t *type,
                                    const void *object, const char *label,
                                    const char *file, int line,
                                    const char *func);

#define shrewd_basic_reflect_dump(type, object, label) ...
```

shrewd_basic_reflect_dump() 递归美化打印对象,走 "reflect" log 模块的
**单条 INFO 记录**(宏自动带调用位置);实现入口是
shrewd_basic_reflect_dump_write()。

- **传参**:`type` — 需 STRUCT 描述符;`object` — 实例;`label` — 标签,
  可 NULL;shrewd_basic_reflect_dump_write() 的后三参由宏从调用点捕获。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_ARG_INVALID`;
  `SHREWD_BASIC_ERR_MEM_NOMEM`。嵌套缩进、数组单行、字符串转义、环标
  `(cycle)`、深度与体量封顶。

## 5. 路径访问

路径语法:`member([index])?(.member([index])?)*`(如 `"home.x"`、
`"pair[2].y"`、多维链式 `"m[1][2]"`)。

### shrewd_basic_reflect_type_resolve(type, path, out_field, out_offset, out_size)

```c
int shrewd_basic_reflect_type_resolve(const shrewd_basic_reflect_type_t *type,
                                      const char *path,
                                      const shrewd_basic_reflect_field_t **out_field,
                                      size_t *out_offset, size_t *out_size);
```

shrewd_basic_reflect_type_resolve() 把路径解析为字段+偏移+值尺寸(纯
元数据,零解引用)。

- **传参**:`type` — 根类型;`path` — 路径串;`out_field` — 终点字段;
  `out_offset` — 根内字节偏移;`out_size` — 值宽(以索引结尾时为单元素宽)。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_KEY_NOTFOUND` 成员缺失
  /越界;`SHREWD_BASIC_ERR_ARG_INVALID` 语法错或跨指针段(静态无法解析,
  动态寻址用 path_get/path_set)。

### shrewd_basic_reflect_path_get(object, type, path, out_value, value_size)

```c
int shrewd_basic_reflect_path_get(const void *object,
                                  const shrewd_basic_reflect_type_t *type,
                                  const char *path, void *out_value,
                                  size_t value_size);
```

shrewd_basic_reflect_path_get() 沿路径读值,自动解引用指针段。

- **传参**:`object` — 根实例;`type` — 根类型;`path` — 路径串;
  `out_value` — 接收缓冲;`value_size` — 必须等于解析值宽。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_REFLECT_MISMATCH`;
  `SHREWD_BASIC_ERR_KEY_NOTFOUND`(含中途 NULL 指针);
  `SHREWD_BASIC_ERR_ARG_INVALID`。终点是指针字段时返回指针值本身。

### shrewd_basic_reflect_path_set(object, type, path, value, value_size)

```c
int shrewd_basic_reflect_path_set(void *object,
                                  const shrewd_basic_reflect_type_t *type,
                                  const char *path, const void *value,
                                  size_t value_size);
```

shrewd_basic_reflect_path_set() 沿路径写值。传参与返回同
shrewd_basic_reflect_path_get();寻址时解引用但绝不分配/释放,中途 NULL
指针不写入(`SHREWD_BASIC_ERR_KEY_NOTFOUND`)。

## 6. 调用方查找容器(`*_list` 家族)

容器形态、存储、锁策略全归应用;库零全局、零加锁。相关类型:

- `shrewd_basic_reflect_lookup_t` — 每类型一条目:注册名、类型指针、
  按名排序的字段指针索引(`sb_refl_sorted`,上限
  `SHREWD_BASIC_REFLECT_LOOKUP_FIELDS` 64,`sb_refl_count == 0` 表示无索引);
- `shrewd_basic_reflect_list_ops_t` — 容器回调:`insert(ctx, name, type,
  out_slot)` 两段式插入、`find_by_type(ctx, type)`、`find_by_name(ctx, name)`;
- `shrewd_basic_reflect_list_t` — `sb_refl_ops` 回调表 + `sb_refl_ctx`
  容器句柄;
- `shrewd_basic_reflect_registry_t` — 开箱即用的默认容器(条目数组 +
  容量 + 计数),由 `SHREWD_BASIC_REFLECT_REGISTRY_DEFINE(var, capacity)`
  在文件域声明(内联条目存储,零堆分配)。

### shrewd_basic_reflect_registry_bind(registry, out_list)

```c
int shrewd_basic_reflect_registry_bind(
        shrewd_basic_reflect_registry_t *registry,
        shrewd_basic_reflect_list_t *out_list);
```

shrewd_basic_reflect_registry_bind() 把内置数组 ops 挂到 `out_list`,
此后 shrewd_basic_reflect_list_register() 与全部 `_list` 查询原样可用
(注册名有序存放、二分查找;重名 `KEY_EXISTS`;满容 `MEM_NOMEM`;
条目指针在下次注册前有效)。

- **传参**:`registry` — `SHREWD_BASIC_REFLECT_REGISTRY_DEFINE` 声明的
  注册表;`out_list` — 出参,list 视图。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_ARG_INVALID`。

### shrewd_basic_reflect_registry_ops()

```c
const shrewd_basic_reflect_list_ops_t *shrewd_basic_reflect_registry_ops(void);
```

shrewd_basic_reflect_registry_ops() 返回内置数组 ops(手工组合自定义
容器形状时用)。无参数;不返回错误码。

### shrewd_basic_reflect_lookup_init(look, type, name)

```c
int shrewd_basic_reflect_lookup_init(shrewd_basic_reflect_lookup_t *look,
                                     const shrewd_basic_reflect_type_t *type,
                                     const char *name);
```

shrewd_basic_reflect_lookup_init() 在调用方存储里建按名排序索引。

- **传参**:`look` — 调用方条目;`type` — 类型;`name` — 注册名
  (NULL 用 `type->sb_refl_name`,指针按原样保存,须比条目活得长)。
- **返回**:`SHREWD_BASIC_ERR_OK`(超 64 字段或非 struct 也成功,但
  `sb_refl_count == 0` 无索引);`SHREWD_BASIC_ERR_ARG_INVALID`。

### shrewd_basic_reflect_lookup_find(look, name, name_len)

```c
const shrewd_basic_reflect_field_t *shrewd_basic_reflect_lookup_find(
        const shrewd_basic_reflect_lookup_t *look, const char *name,
        size_t name_len);
```

shrewd_basic_reflect_lookup_find() 条目内二分查字段(名可不带 NUL)。

- **传参**:`look` — 条目;`name` — 字段名;`name_len` — 名长度。
- **返回**:`const shrewd_basic_reflect_field_t *`;未命中、`look` 无索引
  (`sb_refl_count == 0`)或参数 NULL 返回 NULL。不返回错误码。

### shrewd_basic_reflect_list_register(list, type, name)

```c
int shrewd_basic_reflect_list_register(const shrewd_basic_reflect_list_t *list,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *name);
```

shrewd_basic_reflect_list_register() 注册类型:`ops->insert` 把条目放进
容器并经 `out_slot` 报告位置,库就地建索引。别名=同类型的多个条目。

- **传参**:`list` — ops + 容器句柄;`type` — 类型;`name` — 注册名
  (NULL 用类型名)。
- **返回**:`SHREWD_BASIC_ERR_OK`;插入回调的返回值(重名惯例
  `SHREWD_BASIC_ERR_KEY_EXISTS`);`SHREWD_BASIC_ERR_ARG_INVALID`(含回调
  未给出条目槽位)。

### shrewd_basic_reflect_type_find_list(list, name, out)

```c
int shrewd_basic_reflect_type_find_list(const shrewd_basic_reflect_list_t *list,
                                        const char *name,
                                        const shrewd_basic_reflect_type_t **out);
```

shrewd_basic_reflect_type_find_list() 注册名→类型。

- **传参**:`list` — 容器;`name` — 注册名;`out` — 出参,类型指针。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_KEY_NOTFOUND` 未注册;
  `SHREWD_BASIC_ERR_ARG_INVALID`。

### shrewd_basic_reflect_type_find_field_list(list, type, name, out)

```c
int shrewd_basic_reflect_type_find_field_list(
        const shrewd_basic_reflect_list_t *list,
        const shrewd_basic_reflect_type_t *type, const char *name,
        const shrewd_basic_reflect_field_t **out);
```

shrewd_basic_reflect_type_find_field_list() 经调用方容器按名查字段;容器
不持有的类型(以及无索引条目)回落线性扫描。传参与返回同
shrewd_basic_reflect_type_find_field(),首参加 `list`。

### shrewd_basic_reflect_type_resolve_list(list, type, path, out_field, out_offset, out_size)

```c
int shrewd_basic_reflect_type_resolve_list(
        const shrewd_basic_reflect_list_t *list,
        const shrewd_basic_reflect_type_t *type, const char *path,
        const shrewd_basic_reflect_field_t **out_field, size_t *out_offset,
        size_t *out_size);
```

shrewd_basic_reflect_type_resolve_list() 是
shrewd_basic_reflect_type_resolve() 的孪生:路径每段经容器解析,首参加
`list`。传参与返回同其孪生接口。

### shrewd_basic_reflect_path_get_list(list, object, type, path, out_value, value_size)

```c
int shrewd_basic_reflect_path_get_list(const shrewd_basic_reflect_list_t *list,
                                       const void *object,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *path, void *out_value,
                                       size_t value_size);
```

shrewd_basic_reflect_path_get_list() 是 shrewd_basic_reflect_path_get()
的孪生。传参与返回同其孪生接口。

### shrewd_basic_reflect_path_set_list(list, object, type, path, value, value_size)

```c
int shrewd_basic_reflect_path_set_list(const shrewd_basic_reflect_list_t *list,
                                       void *object,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *path, const void *value,
                                       size_t value_size);
```

shrewd_basic_reflect_path_set_list() 是 shrewd_basic_reflect_path_set()
的孪生。传参与返回同其孪生接口。

并发契约:不带 list 的查询接口经 `_Thread_local` 缓存加速,多线程安全;
`*_list` 家族无共享状态,init 期单线程注册后只读查询任意线程并发安全。

## 7. 图算法(深度递归,环安全)

### shrewd_basic_reflect_check(object, type)

```c
int shrewd_basic_reflect_check(const void *object,
                               const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_check() 校验对象图:每个有效 `ops->check` 逐节点
调用,结构体字段递归(含指针目标,NULL 跳过)。

- **传参**:`object` — 实例;`type` — 类型。
- **返回**:`SHREWD_BASIC_ERR_OK`;首个非 OK 的回调码;
  `SHREWD_BASIC_ERR_ARG_INVALID`。深度封顶。

### shrewd_basic_reflect_copy(out, object, type)

```c
int shrewd_basic_reflect_copy(void **out, const void *object,
                              const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_copy() 深拷贝(clone-out):指针目标克隆,F_STRING_OWNED
克隆内容;`ops->copy` 可接管整段克隆。

- **传参**:`out` — 出参,接收克隆根(分配);`object` — 源实例;`type` —
  类型。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_MEM_NOMEM`;
  `SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED`;`SHREWD_BASIC_ERR_ARG_INVALID`。

### shrewd_basic_reflect_copy_with(alloc, out, object, type)

```c
int shrewd_basic_reflect_copy_with(const shrewd_basic_reflect_allocator_t *alloc,
                                   void **out, const void *object,
                                   const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_copy_with() 带分配器注入的深拷贝:克隆块、owned 串拷贝、
动态表与 copy 的记账节点全部经 `shrewd_basic_reflect_allocator_t`(alloc 需返回
清零内存)进出,与 shrewd_basic_reflect_destroy_with() 严格配对——池/自定义
分配器无需替换平台层。NULL = 平台分配器(即 shrewd_basic_reflect_copy())。
传参与返回同 shrewd_basic_reflect_copy()。

### shrewd_basic_reflect_destroy(object, type)

```c
int shrewd_basic_reflect_destroy(void *object,
                                 const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_destroy() 是 shrewd_basic_reflect_copy() 的逆:释放
对象与克隆会复制的一切。

- **传参**:`object` — 待释放实例(**根对象也释放**,栈对象勿整体调用);
  `type` — 类型。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED`;
  `SHREWD_BASIC_ERR_ARG_INVALID`。借用目标与无 `ops->destroy` 的 opaque
  目标不碰。

### shrewd_basic_reflect_destroy_with(alloc, object, type)

```c
int shrewd_basic_reflect_destroy_with(
        const shrewd_basic_reflect_allocator_t *alloc, void *object,
        const shrewd_basic_reflect_type_t *type);
```

shrewd_basic_reflect_destroy_with() 以同一分配器释放 copy_with 的产物。传参与
返回同 shrewd_basic_reflect_destroy()。

### shrewd_basic_reflect_equal(a, b, type, out_equal)

```c
int shrewd_basic_reflect_equal(const void *a, const void *b,
                               const shrewd_basic_reflect_type_t *type,
                               bool *out_equal);
```

shrewd_basic_reflect_equal() 深度相等比较:整数/枚举按数值、浮点按数值、
字符串按内容,指针字段递归比较目标(环终止);`ops->equal` 逐节点覆盖;
无 ops 的 opaque 按地址。

- **传参**:`a`、`b` — 两实例;`type` — 类型;`out_equal` — 出参。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_ARG_INVALID`。

### shrewd_basic_reflect_diff(a, b, type, path_out, path_out_size)

```c
int shrewd_basic_reflect_diff(const void *a, const void *b,
                              const shrewd_basic_reflect_type_t *type,
                              char *path_out, size_t path_out_size);
```

shrewd_basic_reflect_diff() 比较并报告首个差异的路径。

- **传参**:`a`、`b` — 两实例;`type` — 类型;`path_out` — 路径缓冲
  (可 NULL 仅判相等);`path_out_size` — 缓冲长。
- **返回**:**相等** `SHREWD_BASIC_ERR_OK`;**不等**
  `SHREWD_BASIC_ERR_REFLECT_MISMATCH`,且首差路径写入 `path_out`
  (`"home.y"`/`"pair[2].x"`,根为 `"(root)"`;数组变长报字段名);
  `SHREWD_BASIC_ERR_ARG_INVALID`。缓冲总是 NUL 结尾,过小则截断。

### shrewd_basic_reflect_diff_all(a, b, type, fn, ctx)

```c
int shrewd_basic_reflect_diff_all(const void *a, const void *b,
                                  const shrewd_basic_reflect_type_t *type,
                                  shrewd_basic_reflect_diff_fn fn, void *ctx);
```

shrewd_basic_reflect_diff_all() 报告**全部**差异(不为首差停步):每处不符经
`shrewd_basic_reflect_diff_fn` 回调给出同 diff 语法的路径与类别
(`SHREWD_BASIC_REFLECT_DIFF_VALUE` 叶值/ops 不符,`SHREWD_BASIC_REFLECT_DIFF_SHAPE`
指针空/数组长度/union 标签)。语义镜像 equal 位组/字符串缓冲/活动变体/环对只访
一次;回调返回 OK 继续,其他码中止并透传。

- **传参**:`a`、`b` — 两实例;`type` — 类型;`fn` — 回调;`ctx` — 回调上下文。
- **返回**:`SHREWD_BASIC_ERR_OK`(走完;相等图零报告);回调中止码;
  `SHREWD_BASIC_ERR_ARG_INVALID`。

### shrewd_basic_reflect_hash(object, type, out_hash)

```c
int shrewd_basic_reflect_hash(const void *object,
                              const shrewd_basic_reflect_type_t *type,
                              size_t *out_hash);
```

shrewd_basic_reflect_hash() 与 equal 一致的 64 位 FNV-1a 泛型哈希:整数
按值、浮点折叠 -0.0、字符串按内容、opaque 按地址,跨端序/宽度稳定;
`ops->hash` 覆盖单节点。共享/环目标经**迭代颜色细化**折叠(双模拟不变:自环、
二元环、长环、菱形共享尾哈希一致);超深度封顶区两侧折标记(与 equal 封顶一致)。

- **传参**:`object` — 实例;`type` — 类型;`out_hash` — 出参。
- **返回**:`SHREWD_BASIC_ERR_OK`;`SHREWD_BASIC_ERR_ARG_INVALID`。

## 8. 遍历(事件流)

事件与回调类型:

- `shrewd_basic_reflect_visit_event_t` — `ENTER`/`EXIT`(结构体)、
  `ARRAY_ENTER`/`ARRAY_EXIT`(数组)、`LEAF`(标量/字符串/opaque/NULL 槽)、
  `CYCLE`(环或深度超限);
- `shrewd_basic_reflect_visit_node_t` — 事件载荷:`event`、`path`
  (路径语法,根为 `""`)、`field`(根为 NULL)、`value`、`type`、
  `count`(数组元素数);
- `shrewd_basic_reflect_visit_fn` — 回调,返回 `CONTINUE` 继续、`PRUNE`
  跳过子树(仅 ENTER/ARRAY_ENTER,连带取消配对 EXIT)、`STOP` 立即中止
  (不再报任何事件)。

### shrewd_basic_reflect_visit(object, type, visit, ctx)

```c
int shrewd_basic_reflect_visit(const void *object,
                               const shrewd_basic_reflect_type_t *type,
                               shrewd_basic_reflect_visit_fn visit, void *ctx);
```

shrewd_basic_reflect_visit() 把值图逐节点报给回调(序列化器、校验器、
导出器免写递归),零分配。指针跟随规则同
shrewd_basic_reflect_dump()。

- **传参**:`object` — 实例;`type` — 需 STRUCT 描述符;`visit` — 回调;
  `ctx` — 回调上下文。
- **返回**:`SHREWD_BASIC_ERR_OK`(走完或被 STOP);
  `SHREWD_BASIC_ERR_ARG_INVALID`。

## 9. 全局特性

- 所有接口收 `const` 描述符指针;描述符全 const、永不析构,索引/缓存永不过期;
- 除 shrewd_basic_reflect_copy() 与字符串克隆外零堆分配;数据访问
  memcpy 化(对齐/packed/UBSan 安全);
- 并发:shrewd_basic_reflect_type_find_field() 与不带 list 的路径接口经
  `_Thread_local` 缓存,多线程安全;`*_list` 家族无共享状态;其余接口只
  读描述符或写调用方内存;
- 用法指南(数据模型详解、宏约束、路径语法细节)见
  [reflect reference](reflect.md);支持面总结见
  [reflect overview](reflect-overview.md)。
