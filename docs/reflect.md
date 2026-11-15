# reflect 模块 — 机制、接口与用法

`shrewd-basic/reflect` 为 C 结构体提供**编译期生成的运行时类型元数据**:泛型代码可以枚举
字段、按名/按路径取值赋值、递归打印、校验、比较、深拷贝,而不需要知道具体类型。所有元数据是
`const` 数据(可放 flash),注册由宏在编译期完成,零工具链依赖。

- 头文件:`include/shrewd-basic/reflect/reflect.h`
- 实现:`library/reflect/reflect.c`(核心)、`library/reflect/reflect_dump.c`(打印)
- 演示:`programs/reflect_demo.c`(覆盖全部接口)
- 测试:`tests/test_reflect.c`(宏展开回归、全接口语义)

---

## 1. 设计原则

1. **宏为基座**:用户用宏注解结构体,编译期展开成 `const` 描述符;未来的代码生成器产出同一套宏,不改变使用方式。
2. **混合式类型绑定**:基础类型经 C11 `_Generic` 自动推导;聚合(嵌套结构体、数组、指针)显式传入目标描述符。
3. **size 不变量**:**一切数据读写走 `field->sb_refl_size`**(数组步长为 `size/count`),`type->sb_refl_size` 仅信息性。这使 `long` → int64 描述符的映射在 LLP64 平台上依然内存安全。
4. **零所有权**:反射默认不分配、不释放、不深拷贝;需要时由 ops 回调(见 §7)接管。
5. **恒定错误码**:`shrewd-basic/platform/error.h` 集中注册,reflect 占 `-200..-299` 号段
   (`SHREWD_BASIC_ERR_REFLECT_MISMATCH = -200`)。

---

## 2. 快速上手

```c
#include "shrewd-basic/reflect/reflect.h"

typedef struct { int32_t x; int32_t y; } point_t;
typedef struct {
    const char *name;
    int32_t age;
    float scores[3];
    point_t home;
} person_t;

/* 每个类型只 DEFINE 一次(一个 .c);其他 .c 用 TYPE_DECLARE 引入 */
SHREWD_BASIC_REFLECT_TYPE_DEFINE(point_type, point_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(person_type, person_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, name),          /* 字符串:自动 */
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, age),           /* 标量:自动 */
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, scores, shrewd_basic_reflect_float),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(person_t, home, point_type));

void demo(void)
{
    person_t p = { .name = "Alice", .age = 30, .home = {1, 2} };

    /* 递归打印(经 log 模块输出,终端带颜色) */
    shrewd_basic_reflect_dump(&person_type, &p, "person");

    /* 按路径读写 */
    int32_t x = 0;
    shrewd_basic_reflect_path_get(&p, &person_type, "home.x", &x, sizeof(x));
    shrewd_basic_reflect_path_set(&p, &person_type, "home.x", &(int32_t){7}, sizeof(int32_t));
}
```

输出:

```
person (person_t) {
  name = "Alice"
  age = 30
  scores = [ 0, 0, 0 ]
  home = {
    x = 7
    y = 2
  }
}
```

---

## 3. 数据模型

### 3.1 kind(`shrewd_basic_reflect_kind_t`)

`BOOL, CHAR, INT8, UINT8, INT16, UINT16, INT32, UINT32, INT64, UINT64, ENUM,
FLOAT, DOUBLE, STRING, STRUCT, ARRAY, OPAQUE`

- `ENUM`:枚举值(int 兼容,顺序排在整数族之后、浮点之前),`ENUM_DEFINE` 注册名/值表,dump 按常量名渲染,`enum_find_name/enum_find_value` 双向查表;
- `STRING`:`char *` / `const char *` 成员,指针即值;
- `STRUCT`:有字段表的聚合;
- `ARRAY`:定长数组值(多维数组的一行),`ARRAY_DEFINE` 注册元素/计数/步长,行可嵌套(3D+);
- `UNION`:带标签联合,`UNION_DEFINE` + `UNION_VARIANT` 注册变体表,`FIELD_UNION` 联动
  标签成员;遍历按标签选中**活动变体**(dump/equal/hash/copy/destroy/visit),标签无匹配
  时按原始字节比较、copy 只拷存储、destroy 不触碰(**失效槽位的野指针永不释放**);
- `OPAQUE`:不可描述的目标(`shrewd_basic_reflect_void` 或应用自定义),只能打地址/走 ops;
- 枚举可扩展(POINTER 等留待未来),所有 switch 都有 `default` 兜底为 `unknown`。

`shrewd_basic_reflect_kind_name()` 返回小写名(`"int32"`、`"struct"` …)。

### 3.2 描述符

```c
typedef struct shrewd_basic_reflect_field {
    const char *sb_refl_name;                        /* 成员名 */
    const shrewd_basic_reflect_type_t *sb_refl_type; /* 值类型 / 数组元素类型 / 指针目标类型 */
    size_t sb_refl_offset;                           /* offsetof(owner, member) */
    size_t sb_refl_size;                             /* sizeof(member);数组为总字节 */
    size_t sb_refl_count;                            /* 元素数,非数组为 1 */
    uint32_t sb_refl_flags;                          /* F_ARRAY / F_PTR,可组合 */
    const shrewd_basic_reflect_ops_t *sb_refl_ops;   /* 字段级 ops 覆盖,NULL = 用 type->sb_refl_ops */
} shrewd_basic_reflect_field_t;

struct shrewd_basic_reflect_type {
    shrewd_basic_reflect_kind_t sb_refl_kind;
    const char *name;
    size_t sb_refl_size;          /* 信息性宽度 */
    size_t sb_refl_field_count;
    const shrewd_basic_reflect_field_t *sb_refl_fields;
    const shrewd_basic_reflect_ops_t *sb_refl_ops;  /* 类型级 ops,NULL = 纯元数据行为 */
};
```

**数组是字段形态不是 kind**:`F_ARRAY` + `sb_refl_count` 包装一个元素类型;指针同理是 `F_PTR`
形态,`type` 指向目标类型。两者正交(`F_PTR|F_ARRAY` = 指针数组)。

### 3.3 基础类型描述符

每种 kind 一个 `extern const` 实例,供宏引用:

```c
shrewd_basic_reflect_bool / _char / _int8 / _uint8 / _int16 / _uint16
shrewd_basic_reflect_int32 / _uint32 / _int64 / _uint64 / _float / _double
shrewd_basic_reflect_string   /* const char * */
shrewd_basic_reflect_void     /* 不透明目标(OPAQUE),size = 0 */
```

---

## 4. 注册宏

### 4.1 字段宏

| 宏 | 用途 |
|---|---|
| `FIELD_PRIMITIVE(struct_type, member)` | 标量 / 字符串,`_Generic` 自动推导类型 |
| `FIELD_STRUCT(struct_type, member, ref)` | 嵌套结构体值成员 |
| `FIELD_ARRAY(struct_type, member, ref)` | 定长数组,`ref` 为元素描述符 |
| `FIELD_PTR(struct_type, member, ref)` | 指针成员,`ref` 为目标描述符 |
| `FIELD_PTR_ARRAY(struct_type, member, ref)` | 定长指针数组 |
| `FIELD_ENUM(struct_type, member, ref)` | 枚举成员 |
| `FIELD_DARRAY(struct_type, member, count_member, ref)` | 动态值数组(指针 + 计数联动) |
| `FIELD_PTR_DARRAY(struct_type, member, count_member, ref)` | 动态指针表(point_t ** + 计数) |
| `FIELD_SENTINEL(struct_type, member, ref)` | NULL 值终止的值表(char **argv) |
| `FIELD_PTR_SENTINEL(struct_type, member, ref)` | NULL 槽终止的指针表 |
| `FIELD_FLEX(struct_type, member, count_member, ref)` | 柔性尾部(尾部内联 + 计数联动) |
| `FIELD_STRING_BUFFER(struct_type, member)` | `char[N]` 文本缓冲:按 C 字符串渲染/比较(dump 引号转义、equal/diff/hash 到 NUL 为止、visit 单 LEAF;get/set 仍是原始 N 字节) |
| `FIELD_STRING_OWNED(struct_type, member)` | 图**拥有**的 `char *`:copy 克隆内容(共享串保持共享)、destroy 释放;NULL 保持 NULL;equal/hash/dump 不变 |
| `FIELD_BITS(struct_type, unit, name, bit_offset, bit_width)` | 真实整数单元内的位组(寄存器位段/协议头):get/set 读改写单元(值零扩展到单元宽度),equal/diff/hash/dump 只看该组的位;**C 位域成员无法描述**(布局实现定义) |
| `FIELD_UNION(struct_type, member, union_var, tag_member)` | 带标签联合成员:标签在运行时选变体,只有活动变体参与比较/拷贝/释放/渲染 |
| `FIELD_*_OPS`(全部形态各有变体) | 同上 + 字段级 ops 覆盖 |

`ref`/`ops_ref` 参数**不带 `&`**(宏内部补 `&` 以保持地址常量)。

### 4.2 类型宏

```c
SHREWD_BASIC_REFLECT_TYPE_DEFINE(var, struct_type, field, ...)        /* 结构体类型 */
SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(var, struct_type, ops_ref, ...)  /* 结构体 + 类型级 ops */
SHREWD_BASIC_REFLECT_ARRAY_DEFINE(var, elem_ref, elem_type, count)    /* 数组值(一行) */
SHREWD_BASIC_REFLECT_UNION_DEFINE(var, union_type, variant, ...)      /* 带标签联合 */
SHREWD_BASIC_REFLECT_UNION_VARIANT(tag_value, field, ...)             /* 一个变体(≥1 字段) */
SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(var, "name", ops_ref)              /* 不透明类型 + ops */
SHREWD_BASIC_REFLECT_TYPE_DECLARE(var)                                /* 跨 TU 引入 */
```

纪律与 log 模块一致:**每个类型在一个 .c 中 DEFINE 一次**,兄弟 .c 用 DECLARE 共享
(描述符对象外部链接,`&var` 是地址常量,可自引用——递归指针类型正是靠这一点)。

### 4.3 `_Generic` 绑定规则

`PRIM_TYPE` 只列**基本类型**(`char`、`signed char`、`unsigned char` 是三个独立类型,
各有分支;绝不列 `int8_t` 等 stdint 别名,否则重复关联编译报错),且**没有 default 分支**:
不支持的成员类型(指针、函数指针、`long double` 等)直接编译报错——这是特性。

---

## 5. 接口参考

全部返回 `int` 错误码(`shrewd-basic/platform/error.h`),数据经出参返回。

| 接口 | 语义 | 错误码 |
|---|---|---|
| `kind_name(kind)` | kind 的名字 | — |
| `enum_find_name(type, value, &name)` | 枚举值→常量名 | `KEY_NOTFOUND` / `ARG_INVALID` |
| `enum_find_value(type, name, &value)` | 常量名→枚举值 | `KEY_NOTFOUND` / `ARG_INVALID` |
| `type_find_field(type, name, &field)` | 扁平按名查字段(排序索引加速) | `KEY_NOTFOUND` / `ARG_INVALID` |
| `field_get(obj, field, out, n)` | 原始字节读字段(`n == field->sb_refl_size`) | `REFLECT_MISMATCH` / `ARG_INVALID` |
| `field_set(obj, field, in, n)` | 原始字节写字段 | 同上 |
| `dump(type, obj, label)` 宏 | 递归打印(type 需为 STRUCT):单条 log 记录、记录调用位置 | `ARG_INVALID` / `MEM_NOMEM` |
| `type_resolve(type, path, &field, &offset, &size)` | 路径→字段+偏移+值尺寸(纯元数据) | `KEY_NOTFOUND` / `ARG_INVALID` |
| `path_get(obj, type, path, out, n)` | 沿路径读(自动解引用指针段) | `REFLECT_MISMATCH` / `KEY_NOTFOUND` / `ARG_INVALID` |
| `path_set(obj, type, path, in, n)` | 沿路径写 | 同上 |
| `value_get(obj, field, out, n)` | 经 ops 取目标的规范值 | `REFLECT_MISMATCH` / `KEY_NOTFOUND` / `ARG_INVALID` |
| `value_set(obj, field, in, n)` | 经 ops 写目标的规范值 | 同上 |
| `check(obj, type)` | 图校验(ops->check + 字段递归) | 回调返回值 / `ARG_INVALID` |
| `equal(a, b, type, &same)` | 深度比较(值语义/内容/深目标,环安全) | `ARG_INVALID` |
| `diff(a, b, type, path, n)` | 比较并报告首个差异的路径 | `REFLECT_MISMATCH`(带路径)/ `ARG_INVALID` |
| `hash(obj, type, &out)` | 与 equal 一致的泛型哈希 | `ARG_INVALID` |
| `copy(&out, obj, type)` | 深拷贝(clone-out) | `MEM_NOMEM` / `REFLECT_NOT_SUPPORTED` / `ARG_INVALID` |
| `destroy(obj, type)` | 深释放(copy 的逆) | `REFLECT_NOT_SUPPORTED` / `ARG_INVALID` |
| `visit(obj, type, fn, ctx)` | 事件流遍历(ENTER/LEAF/EXIT/…,跟随指针,环→CYCLE) | `ARG_INVALID` |

### 5.1 查找加速与 get/set 的尺寸规则

字段查找(`find_field` 以及 `resolve`/`path_get`/`path_set` 的路径段查找)经过按类型缓存的
**排序索引**二分加速:描述符保持纯 const,索引是惰性构建的运行时侧表(静态、零分配,
8 槽 × 64 字段,槽满后轮转回收)。超过 64 字段的类型回落线性扫描(结果不受影响);
与 log 模块注册一样**不支持多线程同时首次使用**。

#### get/set 的尺寸规则

`value_size` 必须等于目标的字节宽度(`field->sb_refl_size` 或 `ops->value_size`),否则
`REFLECT_MISMATCH`;数组整体读写时 `n` 为总字节,路径以索引结尾时为单元素宽度
(`resolve` 的 `out_size` 明确给出,无需猜测)。

### 5.2 路径语法

```
member ( '[' index ']' )? ( '.' member ( '[' index ']' )? )*
```

- 成员名为 C 标识符,索引为十进制数字;数组值(多维数组)可**链式索引**:`m[1][2]`、
  `cube[1][0][1]`,每层一个索引,越界 → `KEY_NOTFOUND`;
- `.` 由字段形态驱动:值成员直进,`F_PTR` 段自动解引用(与 `->` 不区分);
- 语法非法 → `ARG_INVALID`;成员不存在 / 索引越界 / **指针段为 NULL** → `KEY_NOTFOUND`;
- `resolve` 是纯元数据零解引用,**跨指针段的路径被拒绝**(`ARG_INVALID`),动态寻址用
  `path_get/set`;路径终点是指针字段时返回指针值本身(与 `field_get` 一致)。

### 5.3 visit 事件遍历

`shrewd_basic_reflect_visit(obj, type, fn, ctx)` 把值图逐节点报给回调,应用(序列化器、
校验器、导出器)无需自己写递归。零分配,路径串随事件给出(5.2 的语法,根为 `""`)。

| 事件 | 含义 | 节点内容 |
|---|---|---|
| `ENTER` / `EXIT` | 结构体值展开前后 | `type` = 该结构体类型,`field` = 到达它的字段(根为 NULL) |
| `ARRAY_ENTER` / `ARRAY_EXIT` | 数组字段前后 | `count` = 运行时元素数,`value` = 元素区基址 |
| `LEAF` | 标量 / 字符串 / 不透明值,**或 NULL 指针槽**(`value == NULL`) | `value` = 值存储地址;STRING 槽内是 `char *` 本身 |
| `CYCLE` | 不再展开的目标 | 环回边(同对象同类型再现)或超出 16 层深度钳制 |

- 指针语义与 dump 一致:**跟随**(NULL → `LEAF` + `value==NULL`,不透明 → `LEAF`,结构体 →
  `ENTER…EXIT`);动态数组表未分配时按 `LEAF(value==NULL)` 上报(同 dump 的 `(null)`);
- 回调返回 `CONTINUE` 继续、`PRUNE` 跳过子树(仅对 `ENTER`/`ARRAY_ENTER` 有效,并连带
  抑制配对的 `EXIT`)、`STOP` 立即终止(后续事件含待发的 `EXIT` 一概不再报告);
- 事件顺序:`ENTER 根 → 字段序(数组为 ARRAY_ENTER → 元素 → ARRAY_EXIT)→ EXIT 根`,
  结构体数组逐元素 `ENTER/EXIT`(路径 `pair[1]`)。

### 5.4 diff 与 hash

- `shrewd_basic_reflect_diff(a, b, type, path, n)`:按 `equal` 的语义比较,在首个差异处
  停止并把路径写入 `path`(5.2 语法,如 `home.y`、`pair[1].x`;根级 `ops->equal` 不等时为
  `(root)`;动态数组长度不同报字段名本身,如 `values`)。相等返回 `OK`,不等返回
  `REFLECT_MISMATCH`;`path` 可传 NULL 跳过报告,非 NULL 时保证 NUL 结尾(过小则截断)。
- `shrewd_basic_reflect_hash(obj, type, &out)`:与 equal 相容的泛型哈希——**相等的对象
  必然哈希相等**。叶子按规范编码折入 FNV-1a 流(整数按值、浮点折叠 -0.0/NaN 标记、
  字符串按内容、不透明按地址),与字节序和宽度无关。限制:环图按形状折叠(回边记标记
  常量),双模拟但形状不同的环可能哈希不同;深度钳制区两侧同样折叠标记,与 equal 的
  钳制行为一致。自定义节点用 `ops->hash`。

---

## 6. dump 输出格式

```
person (person_t) {
  name = "Alice"            字符串:转义 \\ \" \n \r \t 与 \xHH,NULL → (null),256 字节截断
  grade = 'A'               char:可打印字符,否则 '\x07'
  small = -7                有符号整数(按真实宽度读)
  big = 5                   无符号整数
  ratio = 1.5               浮点 %g
  tag = [ 't', 'a', 'g' ]   标量数组单行,32 个封顶 ...(N more)
  scores = [ 1, 2, 3 ]
  home = {                  嵌套结构体缩进块(2 空格/层,16 层深度钳制)
    x = 1
    y = 2
  }
  pair[0] = { ... }         结构体数组逐元素块
  ptr = { ... }             指针跟随(NULL → (null))
  ctx = 0x7ffd1234          不透明目标打地址
}
```

经共享 `"reflect"` log 模块输出:**一次 dump 一条记录**——仅首行带日志前缀,正文从下一行
开始独立、无前缀。`shrewd_basic_reflect_dump` 是宏,`__FILE__/__LINE__/__func__` 注入调用
位置,前缀显示的是 dump 的**发起处**(而非 reflect_dump.c 内部)。彩色前缀、输出重定向
(`set_file`/全局输出)全部免费获得;捕获测试用 `shrewd_basic_log_module_find("reflect", &m)`
 + `set_stream(tmpfile())`。

---

## 7. 指针语义与 ops 回调

### 7.1 指针规则

| 场景 | 行为 |
|---|---|
| `field_get/set` / 路径终点 | **浅层**拷贝指针值 |
| dump | **跟随**解引用(`(null)` / 地址 / 值 / 块,环输出 `...(cyclic)`,深链钳制) |
| `path_get/set` | 解引用**寻址**;中途 NULL → `KEY_NOTFOUND`,绝不半途写入 |
| `check/copy/destroy` | 见下 |

自引用(`struct node *next`)、环形与共享图均安全:环渲染 `...(cyclic)`,深链钳制 `...(max depth)`;`copy`/`destroy`/`check` 经克隆映射与访问集对每个目标只处理一次(clone 保环、共享保持共享)。

### 7.2 ops vtable

```c
struct shrewd_basic_reflect_ops {
    size_t value_size;                                  /* get/set 规范值宽度 */
    int  (*get)(const void *pointee, void *out, size_t value_size);
    int  (*set)(void *pointee, const void *value, size_t value_size);
    int  (*check)(const void *pointee);                 /* 不变量校验 */
    int  (*equal)(const void *a, const void *b, bool *out_equal);     /* 比较两个目标 */
    int  (*render)(const void *pointee, char *buf, size_t buf_size); /* dump 自定义渲染 */
    int  (*copy)(void **out, const void *pointee);      /* clone-out 深拷贝 */
    void (*destroy)(void *pointee);                     /* 释放 copy 产物 */
    int  (*hash)(const void *value, size_t *out_hash);  /* 与 equal 一致的哈希折叠 */
};
```

- **两级挂载**:`field->sb_refl_ops` 优先,缺省回落 `type->sb_refl_ops`;
- 动态数组(`F_DARRAY`):表本身是堆块,`copy` 会克隆表并登记克隆映射(共享表保持共享、失败清理可追踪),元素各自深拷;`destroy` 访问集保证表只释放一次;**计数成员建议也注册为字段**(未注册时 copy 会把解析出的计数写回,保持克隆自洽);
- `render` 用于 dump 跟随指针目标时的自定义文本(如不透明句柄打 `handle#42` 而非地址);缺失或返回失败则回退默认渲染;
- `hash` 供 `reflect_hash()` 覆盖单节点折叠(与 `equal` 同层生效):自定义了
  `equal` 的节点应同步提供 `hash`,维持"相等则哈希相等"的契约;
- **逐回调解退**:缺哪个回调就用哪个元数据默认(只写 `check` 也能复用通用拷贝);
- `get/set/check` 对值和指针目标同样适用;`copy/destroy` 是**对象语义**(指针目标与
  `copy/destroy` 的根对象)。

### 7.3 通用深拷贝/释放矩阵(无 ops 时)

| 目标 | copy | destroy |
|---|---|---|
| 结构体 | `platform_calloc` + 字段递归 | 递归 + `platform_free` |
| 标量 | `platform_alloc` + 拷值(规范宽度) | `platform_free` |
| 字符串(槽) | **别名**(借用语义) | 永不触碰 |
| 字符串(`F_STRING_OWNED`) | **克隆内容**(共享串保持共享) | 释放内容(访问集保证一次) |
| 不透明 | 拒绝(`ARG_INVALID`) | 跳过(非我方分配) |
| 超深度钳制 | 别名(防御) | 跳过 |

**所有权契约**:`destroy` 是 `copy` 的逆操作,只对"图自持所有权"的图运行(copy 产物或
`platform_alloc` 构建的数据)。指针指向调用方栈/静态存储的图必须自供 ops(空
`destroy`)或不调用 `destroy`。`copy` 失败路径会清理部分克隆,不泄漏。

### 7.4 ops 用法示例

```c
static int handle_get(const void *h, void *out, size_t n) { memcpy(out, h, n); return 0; }
static int handle_check(const void *h) { return (*(const int32_t *)h >= 0) ? 0 : SHREWD_BASIC_ERR_ARG_INVALID; }
static int handle_copy(void **out, const void *h);
static void handle_destroy(void *h);

static const shrewd_basic_reflect_ops_t handle_ops = {
    .value_size = sizeof(int32_t),
    .get = handle_get, .set = handle_set, .check = handle_check,
    .copy = handle_copy, .destroy = handle_destroy };

SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(handle_type, "handle", handle_ops);

SHREWD_BASIC_REFLECT_TYPE_DEFINE(device_type, device_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR(device_t, h, handle_type));   /* 或 FIELD_PTR_OPS 覆盖 */
```

---

## 8. 约束与陷阱

1. **描述符成员统一带 `sb_refl_` 前缀**(`sb_refl_name` / `sb_refl_type` / `sb_refl_size` ...):C 宏替换会改写 designated initializer 的设计器(未加前缀时,参数 `type` 会毁掉 `.type =`),项目缩写 + `refl_` 双重前缀使设计器与 `type`/`name`/`var`/`size` 等常见标识符彻底隔离——这些词如今可安全用作宏参数或用户包装宏的参数;宏参数仅需避开 `sb_refl_*` 本身。
2. **`char buf[N]` 必须用 `FIELD_ARRAY(t, buf, shrewd_basic_reflect_char)`**:
   `FIELD_PRIMITIVE` 会因数组退化把 `char[N]` 当 `char *` 误判为字符串(编译器抓不到)。
3. **多维数组不支持**(元素类型模型是单层),bit-field / 联合体 / 函数指针 /
   匿名成员不支持。柔性尾部(`T a[]` 与 GNU `T a[0]`)用 `FIELD_FLEX`:成员必须是
   最后一个、含它的结构体不能再内嵌(C 约束);**分配必须含尾部**
   (`platform_calloc(1, sizeof(T) + n * sizeof(T0))`),`sizeof` 只覆盖头部;
   copy 克隆时会按运行时计数补足尾部。
4. **`long` → int64 在 LLP64 上 kind 与真实宽度不符**:安全靠 size 不变量,但 dump 的
   指针目标标量按 `type->sb_refl_size` 规范宽度读——`long *` 这类目标建议自供 ops。
5. **宏需要 C11(`_Generic`)且展开为声明**,必须放文件作用域;类型至少 1 个字段。
6. **destroy 契约**(§7.3):别对栈上目标的图调用 `destroy`。

---

## 9. 与其他模块的集成

- **error**:错误码集中在 `platform/error.h`(reflect = -200..-299),
  `shrewd_basic_error_name()` 可打印任意返回码;
- **log**:dump 与操作日志走共享 `"reflect"` 模块;策略 WARN 参数误用、ERROR 分配失败、
  TRACE 操作结果/路径未命中、dump 每行 INFO;可 `find("reflect")` 后 `set_level` /
  `set_stream` / `set_file` / 全局输出(见 `docs/` 外的 log.h 与 log demo);
- **ds**:图遍历的克隆映射与访问集复用 ds 的 rbtree(跨模块复用)。

## 10. 代码生成器(可选)

`tools/reflect_gen.py`(纯 Python3,零依赖)解析头文件的受支持子集,
产出与手写完全一致的宏注解,作为可选的编写辅助——**库本身永远不需要它**,
生成物可读、可手改、可与手写混用:

    python3 tools/reflect_gen.py input.h -o stub   # 生成 stub.h + stub.c

- 收录:`typedef struct/enum`;标量/字符串(`FIELD_PRIMITIVE`)、嵌套结构体
  (`FIELD_STRUCT`)、定长数组(`FIELD_ARRAY`)、柔性尾部(`FIELD_FLEX`,含 GNU
  `[0]` 形态,自动配对 `name_count`/`name_len`/`count`/`len` 计数成员,配不到则
  告警跳过)、指针(`FIELD_PTR`/`FIELD_PTR_ARRAY`)、枚举(`FIELD_ENUM`/`ENUM_DEFINE`),
  引用但未定义的类型自动补 `TYPE_DECLARE`;
- 多维数组 `T m[N][M]`(任意维):逐层生成 `ARRAY_DEFINE` 行类型(内层在前,
  3D+ 自然嵌套)+ `FIELD_ARRAY`;`char[N]` 默认生成 `FIELD_ARRAY`
  (字节精确);文本缓冲可手工改为 `FIELD_STRING_BUFFER`(C 字符串语义);
- 描述符命名约定 `<类型名>_reflect`(如 `shape_t_reflect`);
- 跳过并告警:bit-field、联合体、函数指针;
- 验证:`tools/reflect_gen_check.sh` 一键自测(生成 → 编译 → 运行 dump/equal/enum 冒烟),样例输入 `tools/reflect_gen_sample.h`;
- 基准:`tools/reflect_bench.sh` 输出关键路径的 ns/op 基线(find/resolve/path_get/field_get/equal/copy+destroy/dump),供优化工作对照。

## 11. 路线图

- 一、二期(已交付):核心地基、指针、路径、ops 回调集;
- 三期(已交付):枚举、环形/共享图支持、泛型比较(`reflect_equal`)、代码生成器。
- 后续按需:JSON 等序列化属于 reflect 的应用层,基于本模块自行实现。
