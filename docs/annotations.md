# 生成器注解手册

`tools/reflect_gen.py` 的注解与用法:头文件里写意图,构建期产出
reflect 描述符、json 策略表与 schema 导出程序。语法参考
[reflect.md §10](reflect.md),策略语义见 [json.md §6](json.md),
产出宏的 API 见 [reflect-api](reflect-api.md) / [json-api](json-api.md)。

**分工原则**:事实问编译器(布局/宽度/位域放置),意图问注释
(所有权/联合体标签/键名)。注解只写后者——dwarf 后端把前者钉成
`_Static_assert` 防陈旧。

## 0. 快速上手

```c
/* reflect: type=user_t;
   json:    type=user_t; */
typedef struct {
    /* reflect: member=user_t.name, owned;
       json:    member=user_t.name, key="full_name", required; */
    const char *name;
    /* json: member=user_t.level, default=2, range=0..5; */
    int32_t level;
} user_t;
```

```sh
python3 tools/reflect_gen.py user.h -o gen/user --quiet --schema-dump
# 产出:gen/user.{c,h} 描述符 + gen/user_json.{c,h} 策略表
#      + gen/user_schema_dump.c(schema 导出程序)
```

构建期把生成器挂进规则(产物落构建目录),应用 `#include` 生成的
`.h` 即可——样板见 [demo/](../demo/)(CMake 与 Make 两个最小工程)。

## 1. 语句语法(注释块/语句/项)

- **注解注释** = 首词带冒号的注释块;语句形如
  `namespace: 项, 项, ...;`——`;` 结束语句(必填),`,` 分项,一个
  注释块可混多语句、多命名空间;
- **项** 三类:目标(`type=NAME` / `member=TYPE.MEMBER`,**必须首项、
  每句一次**)、裸指令(`owned`)、键值(`count=len`);
- **值**:裸词、`'...'`/`"..."` 引号文本(**无转义**——两种引号都嵌
  不下就报错)、花括号列表(`case={A,B}`、`keys={"a","b"}`,一层);
- **放置自由(位置零语义)**:按目标名认主,成员/类型上一行(推荐)、
  同行末尾、集中清单块语义完全相同;
- **错误哲学**:未识别命名空间、重复指令、矛盾指令、缺目标、
  散文注释里夹语句——全部**硬错误**,绝不静默猜;
- `/* */` 是唯一注释形态(不做 `//`——被"注释掉"的注解会意外生效)。

## 2. 对象:类型(`type=NAME`)

**只生成点名的类型**(严格点名,未标注不生成):

| 写法 | 语义 |
|---|---|
| `/* reflect: type=user_t; */` | 生成 `user_t_reflect` 描述符 |
| `/* reflect: type=helper_t, external; */` | 被引用但**手写**描述符,不生成(闭包声明) |
| `/* reflect: type=user_t, name=my_user; */` | 描述符变量改名 `my_user` |
| `/* reflect: type=user_t, ops=g_user_ops; */` | 挂类型级 ops(`TYPE_DEFINE_OPS`) |

- **收录契约**:只收 `typedef struct|union|enum {体} 名字;`(带 tag
  的 `typedef struct foo {...} a_t;` 与多名字 `} a_t, b_t;` 均可;
  裸 `struct foo {...};` 不收);
- **命名完全自由**:不校验命名风格,描述符名机械拼接
  `<原名>_reflect`(name= 可改);
- **引用闭包硬错误**:被引用的头内类型必须同点名(或 external),
  两条出口:`type=` / `--type NAME`(生成),`external` /
  `--external NAME`(声明外部)。

## 3. 对象:成员(`member=TYPE.MEMBER`)

全限定目标(零歧义),指令按需选:

| 指令 | 语义 | 产出 |
|---|---|---|
| `owned` | 图**拥有**的 `char *`:copy 克隆、destroy 释放 | `FIELD_STRING_OWNED` |
| `stringbuf` | `char[N]` 文本缓冲(C 字符串语义) | `FIELD_STRING_BUFFER` |
| `wstringbuf` | 宽文本缓冲(按码元到 NUL 单元) | `FIELD_WSTRING_BUFFER` |
| `skip` | 不生成该成员 | — |
| `count=NAME` | 柔性数组配对计数成员 | `FIELD_FLEX` 的 count 参数 |
| `ops=NAME` | 字段级 ops 覆盖 | `FIELD_*_OPS` |
| `name=NEW` | 描述符里的字段改名 | `.sb_refl_name = "NEW"` |

自描述配对(`name_count`/`name_len`/`count`/`len`)自动识别,配不到
告警跳过;多维数组自动逐层 `ARRAY_DEFINE` 行类型。

## 4. 对象:联合体(`union_tag=` / `case=`)

联合体标签是**设计意图**,两个后端都无法从布局推断——注解显式声明:

```c
/* reflect: type=payload_u_t; */
typedef union {
    /* reflect: member=payload_u_t.i, case=PAYLOAD_INT; */
    int32_t i;
    /* reflect: member=payload_u_t.f, case={PAYLOAD_REAL, PAYLOAD_ALT}; */
    float f;
    /* reflect: member=payload_u_t.raw, case=*; */
    uint8_t raw[4];
} payload_u_t;

/* reflect: type=event_t; */
typedef struct {
    int32_t kind;
    /* reflect: member=event_t.payload, union_tag=kind; */
    payload_u_t payload;
} event_t;
```

- `case=CONST` / `case={A,B}`(变体归属)、`case=*`(全部变体);
- `union_tag=MEMBER` 挂在**联合体类型的成员**上,指向标签成员;
- 未给 `union_tag=`(或 `skip`)的联合体成员 → 硬错误,不猜。

## 5. 对象:宽字符串与宽标量

- **宽字符串**(`char16_t`/`char32_t`/`wchar_t` 及一层 typedef 别名)
  自动识别 → `FIELD_STRING16/32/WSTRING` 家族(借用/`owned`/`wstringbuf`
  三形态);**刻意不走 `_Generic`**(char32_t 别名会吞整型指针);
- **宽标量**:`long double` 自动映射(`_Generic`);复数与
  `_BitInt(N)` 生成器产出显式宏 `FIELD_COMPLEX`/`FIELD_BITINT`
  (值语义/字节语义见 [reflect.md §3.1](reflect.md));
- 位域成员走 `--backend dwarf`(真实放置 → `FIELD_BITS_UNIT`)。

## 6. 对象:JSON 策略(`json:` 命名空间)

`json: type=NAME` 点名生成 `NAME_json` 稀疏表(仅含带指令成员,
`name=` 可改表名;`--json-type NAME` 同效);成员指令**一指令生成
一行宏**([json.md §6](json.md) 的行宏):

| 指令 | 生成行 | 要点 |
|---|---|---|
| `key="x"` | `KEY(m, "x")` | 改规范编码键;decode 额外**永远**认成员名 |
| `keys={"a","a1","a2"}` | `KEY(m, "a", "a1", "a2")` | 首名规范 + 别名;与 `key=` 互斥 |
| `omit_empty` / `required` / `hidden` / `borrowed` | 同名行 | 标志 |
| `default=18` / `default_str="s"` / `default_json='{"a":1}'` | `DEFAULT*` | 三者择一 |
| `range=0..150` | `RANGE(m, 0, 150)` | 数值范围 |
| `ops=NAME` / `ops_elem=NAME` | `OPS` / `OPS_ELEM` | 编解码钩子 |
| `format="date-time"` / `pattern="^\d+$"` | `FORMAT` / `PATTERN` | 仅 schema 标注 |
| `union_style=nested\|flat\|unwrap` | `UNION_STYLE` | union 线格式风格 |

- **闭包硬错误**:成员指令必须有 `json: type=`(或 `--json-type`);
  type= 必须有 reflect 描述符(生成或 external);空表硬错误;
- 值里的引号文本**原样捕获**,C 转义由生成器负责
  (`pattern="^\d+$"` 生成 `"^\\d+$"`)。

### 6.5 对象:dbus 挂载(`dbus:` 命名空间)

构建期生成 sb_dbus 挂载表(`db<stub>_dbus.{c,h}`):

```c
/* reflect: type=user_t;
   dbus:    type=user_t, path=/config/user; */
/* dbus: type=s_t, path=/hw/adc0, ops=adc_ops; */
/* dbus: path=/event/alarm, flags={event}; */
```

| 指令 | 语义 |
|---|---|
| `path=/a/b` | 必填,`/` 开头;重复挂载路径硬错误 |
| `store=owned` | 默认;`store=linked` 硬错误(data 指针生成器拿不到,手写) |
| `flags={event,readonly}` | 挂载标志(花括号列表可并存) |
| `ops=NAME` | 虚拟/拦截:ops vtable 符号 |
| `name=` | 表名改名(默认回退首个 type) |

`type=` 可省(纯事件 topic);闭包硬错误(type 须 reflect 点名或
external)。产出 `<x>_dbus_mounts` 表 + `<x>_dbus_mounts_count` +
`<x>_dbus_mount_all`(全有或全无挂载,失败回滚)。

### 6.6 对象:csv / ini 策略(`csv:` / `ini:` 等价入口)

与 `json:` **同表合并**(指令集 `key=`/`keys=`/`required`/`hidden`),
产出相同的策略行——csv 列名、ini 键名由同一张表驱动:

    /* csv: member=row_t.a, key="A";
       ini:  member=cfg_t.name, required; */

### 6.7 对象:xml 策略(`xml:` 命名空间)

```c
/* reflect: type=cfg_t;
   xml:     type=cfg_t; */
/* xml: member=cfg_t.name, name="id", attr, required; */
/* xml: member=cfg_t.secret, hidden; */
```

指令:`name=`(标签/属性名)、`attr`(→ 父元素属性)、`required`、
`hidden`;产出 `<x>_policy`(`shrewd_basic_xml_policy_t` 行表)+
`_count`,`name=` 改表名。闭包与硬错误同族。

## 7. 命令行与后端

```sh
reflect_gen.py INPUT [-o STUB] [--quiet]
    [--backend lex|dwarf] [--cc CC] [--cflags "..."]
    [--type NAME]... [--external NAME]...
    [--json-type NAME]... [--schema-dump]
    [--layout [--json]]        # dwarf: pahole-lite 布局报告
```

| 选项 | 语义 |
|---|---|
| `-o STUB` | 产出基名(默认 `<input>_reflect`);**拒绝覆盖输入本身** |
| `--backend lex` | 文本扫描,零依赖(默认) |
| `--backend dwarf` | 编译探针读真实布局(需 `python3-pyelftools` + C 编译器):位域、`_Static_assert` 指纹 |
| `--type/--external` | 命令行点名(不可注解的第三方头) |
| `--json-type` | 命令行点名 JSON 策略表 |
| `--schema-dump` | 加出 `<STUB>_schema_dump.c`(`dump TYPE [FILE]`) |
| `--layout [--json]` | 不生成桩,打布局报告 |

## 8. 产出物与构建接线

| 文件 | 内容 |
|---|---|
| `STUB.h` / `STUB.c` | `TYPE_DECLARE` 清单 / `TYPE_DEFINE` 宏(手写同款) |
| `STUB_json.h` / `STUB_json.c` | `JSON_DECLARE` / `JSON_DEFINE` 策略表 |
| `STUB_schema_dump.c` | schema 导出程序(链接产物+库即可运行) |

接线模式:构建规则跑生成器(产物落构建目录,源树干净),应用目标
编译 `STUB.c`(+ `STUB_json.c`)+ include 构建目录。可直接抄
[demo/consumer-cmake](../demo/consumer-cmake/) 与
[demo/consumer-make](../demo/consumer-make/)。

## 9. 硬错误速查

| 报错 | 处置 |
|---|---|
| `unknown namespace 'X'` | 拼写错/工具版本旧;散文注释别写 `word:` 开头 |
| `duplicate 'key' directive` | 同一指令只许一次;列表写 `keys={...}` |
| `key= and keys= are mutually exclusive` | 二选一 |
| `statement must lead with type= or member=` | 目标项必须首项 |
| `references X, but X is neither generated nor external` | 闭包:给 X 加 `type=` 或 `external` |
| `member=T.m names no member` | 目标名打错 |
| `union ... needs union_tag= (or skip)` | 联合体成员显式声明标签 |
| `would generate an empty policy table` | `json: type=` 要有成员指令 |
| `output ... would overwrite the input header` | 换 `-o` 基名 |

自检:`tools/reflect_gen_check.sh` 一键全轮(双后端、注解、json 策略、
schema 导出、golden 全文对账);已接 ctest(`shrewd-basic.gen-check`,
缺 pyelftools 时响亮跳过)。
