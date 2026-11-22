# reflect 模块功能总览

本文**只介绍和总结 reflect 模块当前已实现的功能**:类型系统、字段形态、API、语义
规则与工程配套。用法细节与完整参考见 [reflect reference](reflect.md)。

---

## 1. 模块定位

为 C 结构体提供**编译期生成的运行时类型元数据**:泛型代码不接触具体类型定义,
即可枚举字段、按名/按路径取值赋值、递归打印、深度比较/拷贝、逐节点遍历。
定位是"尽量完善的反射地基"——序列化(JSON 等)属于其上的应用层,由应用基于本
模块自行实现。

**设计原则**:

| 原则 | 含义 |
|---|---|
| 宏为基座 | 注解宏展开为 `const` 描述符(纯数据,可放只读段/flash);生成器只是可选的编写辅助 |
| 全 const 数据模型 | 描述符表编译期定型,零运行时注册;跨 TU 以 `TYPE_DECLARE` 静态共享 |
| size 不变量 | 一切数据访问走 `field->sb_refl_size`/步长;`type->sb_refl_size` 仅信息性(LLP64 上 `long` 映射 int64 仍安全) |
| DEFINE/DECLARE 纪律 | 每类型在一个 .c 中 DEFINE 一次,兄弟 .c 用 DECLARE 引入(与 log 模块一致) |
| memcpy 访问 | 一律按字节读写后转值,packed/异址对齐良定义,UBSan 干净 |
| 严格 C11 | `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` 零告警,GCC 15 + Clang 21 双验证 |

错误码集中于 `shrewd-basic/platform/error.h`(reflect 号段 -200 起):
`REFLECT_MISMATCH`(-200)、`REFLECT_NOT_SUPPORTED`(-201)。

---

## 2. 类型系统(18 种 kind)

| kind | 说明 | 注册方式 |
|---|---|---|
| `BOOL, CHAR, INT8, UINT8, INT16, UINT16, INT32, UINT32, INT64, UINT64` | 基本整数/字符/布尔(按真实宽度读写) | 内置描述符(`shrewd_basic_reflect_int32` 等 14 个) |
| `ENUM` | 枚举值(int 兼容,顺序在整数族后、浮点前),dump 按常量名渲染,名/值双向查表 | `ENUM_DEFINE` + `ENUM_VALUE` |
| `FLOAT, DOUBLE` | 浮点(数值语义比较) | 内置描述符 |
| `STRING` | `char *` / `const char *` 成员,**指针即值**(get/set 拷指针) | `FIELD_PRIMITIVE` 自动绑定 |
| `STRUCT` | 有字段表的聚合,嵌套/递归(自引用)均可 | `TYPE_DEFINE` / `TYPE_DEFINE_OPS` |
| `ARRAY` | 定长数组值(多维数组的一行),可嵌套(3D+) | `ARRAY_DEFINE(var, elem_ref, elem_type, count)` |
| `UNION` | 带标签联合,变体表按标签选中活动变体 | `UNION_DEFINE` + `UNION_VARIANT` |
| `OPAQUE` | 不可描述的目标(`shrewd_basic_reflect_void` 或应用自定义 + ops) | `OPAQUE_DEFINE` |

`kind_name()` 返回小写名;枚举可扩展,所有 switch 均有 `default` 兜底。

---

## 3. 字段形态(15 种 + 11 种 `_OPS` 变体)

字段形态由正交 flag 组合表达(`F_ARRAY|F_PTR|F_DARRAY|F_SENTINEL|F_FLEX|
F_STRINGBUF|F_STRING_OWNED|F_BITS|F_UNION`):

| 宏 | 形态 | 关键语义 |
|---|---|---|
| `FIELD_PRIMITIVE(t, m)` | 标量 / 字符串 | `_Generic` 自动推导类型 |
| `FIELD_STRUCT(t, m, ref)` | 嵌套结构体值 | 递归展开 |
| `FIELD_ARRAY(t, m, ref)` | 定长数组 | `count/stride` 由 sizeof 推出;元素可为标量/结构体/数组行 |
| `FIELD_PTR(t, m, ref)` | 指针成员 | get/set 拷指针;dump/check/copy/destroy 跟随 |
| `FIELD_PTR_ARRAY(t, m, ref)` | 定长指针数组 | 元素逐个跟随 |
| `FIELD_ENUM(t, m, ref)` | 枚举成员 | 名/值渲染 |
| `FIELD_DARRAY(t, m, count_member, ref)` | 动态值数组 | 指针 + 计数成员联动,计数运行时读取 |
| `FIELD_PTR_DARRAY(t, m, count_member, ref)` | 动态指针表 | `T **` + 计数 |
| `FIELD_SENTINEL(t, m, ref)` | NULL 值终止表 | argv 风格(`char **`) |
| `FIELD_PTR_SENTINEL(t, m, ref)` | NULL 槽终止指针表 | 逐个跟随 |
| `FIELD_FLEX(t, m, count_member, ref)` | 柔性尾部 | C99 FAM 与 GNU `[0]`;计数联动,内联存储 |
| `FIELD_STRING_BUFFER(t, m)` | `char[N]` 文本缓冲 | C 字符串语义(dump 引号转义、equal/diff/hash 到 NUL 为止、visit 单 LEAF);get/set 仍是原始 N 字节 |
| `FIELD_STRING_OWNED(t, m)` | 图**拥有**的 `char *` | copy 克隆内容(共享串保持共享)、destroy 释放;NULL 保持 NULL |
| `FIELD_BITS(t, unit, name, bit_offset, bit_width)` | 整数单元内的位组 | get/set 读改写单元(值零扩展到单元宽度);equal/diff/hash/dump 只看该组的位 |
| `FIELD_UNION(t, m, union_var, tag_member)` | 带标签联合成员 | 标签运行时选变体,只有活动变体参与比较/拷贝/释放/渲染 |
| `FIELD_*_OPS(t, m, ..., ops_ref)` | 上述 11 种的字段级 ops 覆盖 | 优先于类型级 ops |

约束:宏参数名固定为 `var/struct_type/member/ref/...`,与 `sb_refl_` 前缀的描述符
成员完全隔离(designator 替换陷阱);字段列表至少 1 个;bit-field、函数指针无法
描述(见 §7)。

---

## 4. 类型注册宏

```c
SHREWD_BASIC_REFLECT_TYPE_DEFINE(var, struct_type, field, ...)        /* 结构体 */
SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(var, struct_type, ops_ref, ...)  /* 结构体 + 类型级 ops */
SHREWD_BASIC_REFLECT_ARRAY_DEFINE(var, elem_ref, elem_type, count)    /* 数组值(一行) */
SHREWD_BASIC_REFLECT_UNION_DEFINE(var, union_type, variant, ...)      /* 带标签联合 */
SHREWD_BASIC_REFLECT_UNION_VARIANT(tag_value, field, ...)             /* 一个变体(≥1 字段) */
SHREWD_BASIC_REFLECT_ENUM_DEFINE(var, enum_type, value, ...)          /* 枚举 */
SHREWD_BASIC_REFLECT_ENUM_VALUE(value)                                /* 一个常量 */
SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(var, "name", ops_ref)              /* 不透明类型 */
SHREWD_BASIC_REFLECT_TYPE_DECLARE(var)                                /* 跨 TU 引入 */
SHREWD_BASIC_REFLECT_PRIM_TYPE(expr)                                  /* _Generic 映射 */
```

- 描述符对象**外部链接**:`&var` 是地址常量,可出现在其他描述符的初始化中
  (嵌套、自引用递归类型都靠这一点);字段表 `var##_fields` 留在本 TU;
- `TYPE_DEFINE` 会前向声明 `var`,自引用类型可直接写在自己的字段表里。

---

## 5. 运行时 API(22 个函数 + 1 个 dump 宏)

### 元数据遍历 / 查找

| 接口 | 语义 |
|---|---|
| `kind_name(kind)` | kind 的名字 |
| `type_field_count(type)` / `type_field_at(type, i)` | 遍历字段表(不触碰布局) |
| `enum_count(type)` / `enum_at(type, i)` | 遍历枚举常量表 |
| `type_find_field(type, name, &field)` | 扁平按名查字段(排序索引二分加速) |
| `list_register(list, type, name)` + `type_find_list` / `type_find_field_list` / `type_resolve_list` / `path_get_list` / `path_set_list` | 调用方容器(ops 回调)驱动的同族查找,线程安全路径 |
| `enum_find_name(type, value, &name)` / `enum_find_value(type, name, &value)` | 枚举名/值双向查表 |

### 值读写

| 接口 | 语义 |
|---|---|
| `field_get(obj, field, out, n)` / `field_set(...)` | 原始字节读写(`n == field->sb_refl_size`;位组为提取/注入语义) |
| `value_get(obj, field, out, n)` / `value_set(...)` | 经 ops 取/写目标的规范值 |

### 路径访问

| 接口 | 语义 |
|---|---|
| `type_resolve(type, path, &field, &offset, &size)` | 路径→字段+偏移+值尺寸(纯元数据,拒跨指针/union) |
| `path_get(obj, type, path, out, n)` / `path_set(...)` | 沿路径读/写(自动解引用指针段、union 运行时选变体) |

路径语法:`member([idx])?(\.member([idx])?)*`,多维数组**链式索引**
(`m[1][2]`、`cube[1][0][1]`);语法非法 → `ARG_INVALID`,成员不存在/索引越界/
指针段 NULL → `KEY_NOTFOUND`。

### 打印

| 接口 | 语义 |
|---|---|
| `shrewd_basic_reflect_dump(type, obj, label)` 宏 | 递归 pretty-print:**一条 log 记录**(仅首行带前缀,正文从下一行起独立无前缀),前缀记录 dump 发起位置 |

dump 覆盖全部形态:转义字符串、`[ a, b, c ]` 标量数组(32 个封顶)、嵌套块、
结构体数组逐元素块、指针跟随(`(null)`/地址/值/`handle#42` 式 ops 渲染)、环
`...(cyclic)`、16 层深度钳制、1 MiB 正文封顶(截断标记)、枚举按常量名、位组
按提取值、文本缓冲按引号串、union 按 `(TAG) { ... }` 标签块。

### 图算法

| 接口 | 语义 |
|---|---|
| `check(obj, type)` | 图校验:ops->check + 字段递归(指针目标、活动变体) |
| `copy(&out, obj, type)` | 深拷贝 clone-out(见 §6 所有权) |
| `destroy(obj, type)` | 深释放(copy 的逆) |
| `equal(a, b, type, &same)` | 深度比较(值语义/内容/深目标,环安全) |
| `diff(a, b, type, path, n)` | 比较并报告**首个差异的路径**(`home.y`/`pair[1].x`/`(root)`) |
| `hash(obj, type, &out)` | 与 equal 相容的泛型哈希(相等 ⇒ 哈希相等) |

### 事件遍历

| 接口 | 语义 |
|---|---|
| `visit(obj, type, fn, ctx)` | 逐节点事件流:`ENTER/ARRAY_ENTER/LEAF/CYCLE/ARRAY_EXIT/EXIT`,每节点带路径;回调可 `CONTINUE/PRUNE/STOP`;零分配 |

---

## 6. 核心语义规则

### 指针语义(按操作区分)

| 操作 | 行为 |
|---|---|
| `field_get/set`、路径终点 | **浅层**拷贝指针值 |
| dump / visit | **跟随**解引用(NULL → `(null)`/LEAF 空值;不透明 → 地址;环 → 标记) |
| `path_get/set` | 解引用**寻址**;中途 NULL → `KEY_NOTFOUND`,绝不半途写入 |
| `check/copy/destroy/equal/hash` | 深度图语义(下述) |

### 所有权契约

- `copy` 是 clone-out(经 `platform_alloc` 族):结构体/标量目标递归克隆、
  **共享源映射共享克隆**(环/菱形图健全)、字符串默认**别名**、`F_STRING_OWNED`
  串**克隆内容**、不透明目标需 ops->copy;
- `destroy` 是 `copy` 的逆,只应作用于拥有目标的图(clone 产物或
  platform_alloc 搭建的数据);借用目标永不触碰;
- union:**只碰活动变体**——失效槽位里的陈旧指针位永不跟随、永不释放。

### 环与共享图

- copy:克隆映射(rbtree)登记 src→clone,回边直接取克隆;
- destroy/check:访问集保证每节点只处理一次;
- equal:双模拟(在比的 (a,b) 对再现即判等),循环数据可终止;
- dump/visit/hash:`(object, type)` 对的在路径检测(嵌入成员与父共享地址不算环),
  16 层深度钳制;
- hash 共享/环目标经颜色细化(双模拟不变,自环与多元环同哈希);深度封顶区折标记。

### 字符串的三种形态(× 三种码元宽度)

| 形态 | 存储 | equal/hash | copy/destroy |
|---|---|---|---|
| `STRING`(借用) | 槽内是 `char *` | 按内容 | 别名 / 永不释放 |
| `F_STRING_OWNED`(拥有) | 槽内是 `char *` | 按内容 | 克隆内容 / 释放内容 |
| `F_STRINGBUF` | `char[N]` 内联 | 到 NUL 为止 | 原样字节 / 无 |

宽字符串(`STRING16`/`STRING32`:`char16_t *`/`char32_t *`/`wchar_t *`)
是同一组形态的 **16/32 位码元版**:比较/哈希/截断一律按码元到 NUL 单元
为止,dump 逐码元转义(`\uXXXX`/`\UXXXXXXXX`),owned 克隆按码元内容。
显式宏(`FIELD_STRING16/32` 与 `FIELD_WSTRING` 系列)——刻意不走
`_Generic` 自动映射,避免 `char32_t`/`unsigned int` 别名吞掉整型指针。

### 位组

- 值 = 位组**零扩展**到单元宽度;get/set 对单元读改写;
- equal/diff/hash/dump 只看该组的位,单元其余位不可见;
- 非法位段(越界)定义行为:提取 0 / 写入不动。

### union 变体

- 标签成员是 owner 的整数成员(`limit_offset` 通道联动),运行时读取;
- dump `(TAG) { ... }`;equal 先比标签再比变体;未知标签按原始字节比较、
  copy 只拷存储;
- 路径 `u.x` 运行时下降;`resolve`(静态)拒绝。

### ops vtable(两级挂载、逐回调解退)

```c
struct shrewd_basic_reflect_ops {
    size_t value_size;
    int (*get)(...); int (*set)(...); int (*check)(...);
    int (*equal)(...); int (*render)(...);
    int (*copy)(...); void (*destroy)(...); int (*hash)(...);
};
```

- `field->sb_refl_ops` 优先,缺省回落 `type->sb_refl_ops`;缺哪个回调就用哪个
  元数据默认;
- `get/set/check` 对值和指针目标同样适用;`copy/destroy` 是对象语义;
  `render` 供 dump 自定义文本;`equal/hash` 成对自定义以维持哈希契约。

---

## 7. 使用约束与陷阱(现状)

- `_Generic` 只列基本类型(不列 stdint 别名防重复关联),无 `default`:不支持的
  成员类型**编译期报错**是特性;
- **匿名 struct/union 成员**(C11 无名成员)按**平铺**描述:子成员直接
  列进外层字段表(`FIELD_PRIMITIVE(S, ax)` 即可,offsetof/sizeof 穿透
  匿名父级);同址的匿名 union 子成员互为别名,语义由应用自理;
- 字符缓冲二选一:`FIELD_STRING_BUFFER`(文本语义)或 `FIELD_ARRAY(t, buf,
  shrewd_basic_reflect_char)`(字节精确);裸 `FIELD_PRIMITIVE` 会把数组退化成
  字符串指针;
- **C 位域成员用 `FIELD_BITS_UNIT`**(显式单元放置)——布局实现定义、宏无法
  内省,放置参数由 `reflect_gen --backend dwarf` 从编译器实际布局产出;
  `FIELD_BITS` 覆盖"真实单元 + 作者给定位段"(寄存器位段/协议头);
- 多维数组的行元素是**值类型**(标量/结构体/嵌套行);生成器对
  `T m[N][M]` 自动生成行类型,3D+ 同理;
- 柔性尾部(FAM)的分配契约:`sizeof` 只覆盖头部,须
  `sizeof(T) + n × sizeof(elem)`;
- union 变体切换标签前,应用须自行处置旧变体资源(库只保证不误触失效槽位);
- find_field 排序索引是惰性构建的 `_Thread_local` 侧表(8 槽 × 64 字段,超限回退
  线性扫描),多线程各自预热、无共享竞态;跨线程共享一份索引用调用方容器
  (`list_register` + `*_list` 接口,库零加锁,并发契约归应用);
- `destroy` 会释放根对象——栈上对象不能整体 destroy(释放目标可用 ops 或手工);
- 宏的参数名不得与描述符成员名重合(已由 `sb_refl_` 前缀隔离);`FIELD_BITS`
  的 `name` 是标签,不要与真实成员重名。

---

## 8. 工程配套

| 配套 | 内容 |
|---|---|
| 测试 | `shrewd-basic-reflect-tests`(2000+ 行,含跨 TU 共享验证);验证纪律:4 套件 ctest、GCC 15 + Clang 21 严格告警零告警、valgrind "All heap blocks were freed"、ASan/UBSan 干净 |
| demo | `programs/reflect_demo.c` 18 节,覆盖全部接口与形态(类型描述/读写/dump/指针/路径/ops/跨 TU/枚举/环图/动态数组/柔性尾部/字符串缓冲/拥有串/位组/多维数组/union/visit/diff+hash) |
| 代码生成器 | `tools/reflect_gen.py`(纯 Python):解析 `typedef struct/enum`,产出同一套宏;支持多维数组行类型、FAM 计数配对;`tools/reflect_gen_check.sh` 一键自测 |
| 基准 | `tools/reflect_bench.sh`:find/resolve/path_get/field_get/equal/copy+destroy/dump 的 ns/op 基线(查找索引优化后 find ~25ns、path_get ~28ns) |
| 文档 | `docs/reflect.md`(完整参考)、本文(总览)、`docs/index.md`(Doxygen 主页);`docs` 目标输出 HTML |
| 构建 | 源码自动 glob;静态/动态库独立开关;支持作为子项目(`add_subdirectory`)零污染集成 |

---

*本文随功能同步维护;设计取舍与演进过程见 git 历史。*
