# json 模块 — 架构、值模型、结构映射与文本契约

JSON 支持按**分层**组织:第 0 层(值模型 + 解析/序列化)与第 1 层(结构映射)
已落地,本文覆盖两层与全部线格式契约;策略注册(第 2 层)在分层图中标注。

---

## 1. 分层架构

```
┌────────────────────────────────────────────────┐
│ 第 2 层:JSON 策略注册(键名/别名/省略/必填/范围) │  后续落地
├────────────────────────────────────────────────┤
│ 第 1 层:结构映射(struct ↔ DOM,reflect 驱动)    │  已落地
├────────────────────────────────────────────────┤
│ 第 0 层:JSON 值模型(DOM)+ 解析/序列化          │  已落地
└────────────────────────────────────────────────┘
```

第 0 层是一个**独立可用的 JSON 库**(对标 cJSON 的能力):解析文本到值树、按名/按下标
取值、建值/改值/删值、序列化回文本。上层的 struct 映射只是值模型的又一个消费者。

## 2. 值模型(`shrewd_basic_json_value_t`)

六种 kind:`NULL / BOOL / NUMBER / STRING / ARRAY / OBJECT`,节点不透明。

**构造**(创建即拥有,`value_free` 整树释放):

```c
shrewd_basic_json_new(kind, &v)          /* null / array / object */
shrewd_basic_json_new_bool(b, &v)
shrewd_basic_json_new_i64(i, &v)         /* 整数 token */
shrewd_basic_json_new_u64(u, &v)         /* 全 64 位数字 */
shrewd_basic_json_new_f64(d, &v)         /* 最短往返格式;非有限值拒绝 */
shrewd_basic_json_new_string(text, &v) / _new_string_n(text, n, &v)
shrewd_basic_json_value_free(v)          /* 整树释放,NULL 安全 */
```

**读取**:

| 接口 | 语义 |
|---|---|
| `kind(v)` / `count(v)` | 类型 / 成员数(对象或数组) |
| `at(v, i)` / `key_at(v, i)` | 第 i 个成员的值 / 键名 |
| `get(v, key)` | 首个同名成员(**重复键第一个生效**) |
| `to_bool/to_i64/to_u64/to_f64` | 按值读取;`1.0` 读作整数 1,越界/异型 → `JSON_TYPE_MISMATCH` |
| `to_string(v)` | 字符串内容(非字符串返回 NULL) |
| `number_text(v)` | 数字的**原始 token 文本** |

**修改**:

```c
shrewd_basic_json_set(object, key, item)     /* 替换同名成员或追加;接管 item */
shrewd_basic_json_append(array, item)        /* 追加;接管 item */
shrewd_basic_json_remove(v, key) / _remove_at(v, i)   /* 删除并释放该子树 */
```

对象保持**成员的文档顺序**;解析器原样保留重复键(DOM 保真,序列化原样吐回),
`get` 取第一个,`set` 只替换第一个。

## 3. 文本进出

```c
int shrewd_basic_json_parse(text, length, &value, options, &error);
int shrewd_basic_json_serialize(value, &text, options, &error);
void shrewd_basic_json_text_free(text);
```

**解析**默认**严格 RFC 8259**:

- 拒绝:注释、尾逗号、单引号、裸词、`01`/`+1`/`.5`/`1.`/`1e`、串内控制字符、
  坏转义、**不成对代理项**、尾随垃圾(根值之后只允许空白);
- 空白仅 `SP/HT/LF/CR`;数字按文法逐字符校验;
- `\uXXXX` 解码(代理对合成 UTF-8);串内原始字节原样保留(不做 UTF-8 合法性校验);
- 可选宽松:`options.allow_comments`(`//` 与 `/* */`)、
  `options.allow_trailing_commas`;
- `options.max_depth` 限制嵌套(默认 32;按**值的嵌套层级**计数)。

**序列化**:紧凑默认,`options.pretty` + `options.indent`(默认 2)缩进输出;
嵌套超限报 `JSON_DEPTH`。

## 4. 线格式契约

| 项 | 约定 |
|---|---|
| 数字 | **token 原样保真**:`parse("1.25e2")` 再 `serialize` 得 `"1.25e2"`;创建的数用规范形(浮点取最短往返格式,`-0` 保留,u64 全位数) |
| 字符串 | 存储为解码后的 UTF-8;输出转义 `\" \\ \b \f \n \r \t`,其余控制字符 `\u00XX`,非 ASCII 原样 |
| 键序 | 对象成员按**文档顺序**(创建的按插入顺序);不排序 |
| 重复键 | 解析保留(文本保真);`get` 首个生效;`set` 替换首个 |
| 布尔/空 | `true`/`false`/`null` |

(结构映射层的格式约定——union 保形嵌套、键名/别名、位组、枚举名等——随第 1、
2 层落地时归入本文。)

## 5. 结构映射(第 1 层)

```c
int shrewd_basic_json_encode_value(object, type, &value, opt, &err);   /* struct -> DOM */
int shrewd_basic_json_encode(object, type, &text, opt, &err);          /* struct -> 文本 */
int shrewd_basic_json_decode_value(value, object, type, opt, &err);    /* DOM -> struct */
int shrewd_basic_json_decode(text, len, object, type, opt, &err);      /* 文本 -> struct */
int shrewd_basic_json_free(object, type);                              /* decode 的逆 */
```

### 映射规则(C ↔ JSON)

| C 形态 | JSON |
|---|---|
| 标量 / 整数 / 浮点 | number(非有限值默认报错,`opt->allow_nan` → `null`) |
| `ENUM` | 常量名字符串(未知值 → number);decode 名称/数字都收 |
| `STRING` | string(NULL → `null`) |
| `STRINGBUF` | string(内容到 NUL 为止;decode 至多 count 字符,恰满可不带 NUL) |
| `CHAR` | 单字符 string(decode 也接受数字) |
| `FIELD_BITS` | number(提取值;decode 读改写单元) |
| 数组族(定长/动态/哨兵/FAM/多维行) | array,多维逐层嵌套 |
| `F_PTR` | 跟随(NULL → `null`);**环图 encode 报错**(JSON 无法表达) |
| `FIELD_UNION` | **F1 保形嵌套**(见下) |
| 不透明 | `ops->render` → string;否则报错(decode 不支持) |

**键 = 字段名,按描述符字段序输出**(策略层落地前无改名/省略)。

### union 的 F1 形态

```json
{"kind": "MSG_POINT", "body": {"point": {"x": 1, "y": 2}}}
```

标签成员是普通字段(自己的键),union 成员是**活动变体字段**为键的对象;
标签键在 JSON 中的位置无关(decode 先填标签再解 body)。未知标签:
encode 默认报错(`opt->allow_unknown_union` → body 输出 `null`);
decode 对 `null` body 放行、非 null 报错。

### decode 规则

- 目标**先清零**(除非 `opt->keep_defaults`:缺失键保留**预置默认值**,
  REQUIRED 也由默认值满足——配置合并场景);
- **计数成员由数组长度驱动**(JSON 里的计数键被忽略);
- 数组可**短于**槽位(尾部保持零),不可更长;字符串缓冲恰满可无 NUL,超长报错;
- 未知键忽略;整数/浮点按值宽窄校验(越界报 `JSON_TYPE_MISMATCH`);
- 每个错误都带 JSON 路径(`err->path`,如 `body.point.x`);
- FAM 根的分配由调用方按 JSON 尾长预留:`sizeof(T) + n * sizeof(elem)`。

### 映射层所有权

decode 的每个分配都记入**按根对象索引的登记表**;`shrewd_basic_json_free()`
只释放登记在册的块(含普通字符串、指针目标、动态表)——**预置默认值
(静态字面量或调用方分配)永远不会被误释放**,重复键/二次 merge 覆盖旧值
时也只回收 decode 自己的分配。`ops->decode` 产出的目标由 `ops->destroy`
释放。**根存储归调用方**(decode 不分配它)。登记表为静态侧表,与查找
缓存一样**不支持多线程同时使用**。

### 键匹配策略

`opt->key_match = SHREWD_BASIC_JSON_KEY_MATCH_LOOSE`:decode 忽略大小写与
下划线(`name_id`/`nameID`/`NameID` 等价);若两个字段在宽松规则下会撞名,
decode 直接报 `JSON_UNSUPPORTED`(注册即拒绝)。

## 6. 策略注册(第 2 层)

Go 结构体标签的 C 等价物——**只管文本外观,结构仍以 reflect 描述符为唯一来源**:

```c
SHREWD_BASIC_JSON_DEFINE(cfg_json, cfg_reflect,
    SHREWD_BASIC_JSON_KEY(name, "name_id", "nameID", "NameID"),  /* 规键 + 别名 */
    SHREWD_BASIC_JSON_REQUIRED(name),                            /* decode 必填 */
    SHREWD_BASIC_JSON_HIDDEN(secret),                            /* 双向隐藏 */
    SHREWD_BASIC_JSON_OMIT_EMPTY(score),                         /* encode 省略零值 */
    SHREWD_BASIC_JSON_RANGE(age, 0, 150),                        /* 数值范围 */
    SHREWD_BASIC_JSON_OPS(score, g_score_ops),                   /* 自定义编解码/校验 */
    SHREWD_BASIC_JSON_UNION_STYLE(body, SHREWD_BASIC_JSON_UNION_FLAT));
```

encode/decode 的 `info` 参数携带策略(NULL = 纯元数据,行为不变)。

| 策略 | 语义 |
|---|---|
| `KEY(field, 规键, 别名...)` | encode 永远输出**规键**(第 1 个);decode 接受任意别名,**成员名本身始终可接受** |
| `REQUIRED` | decode 时键必须出现(别名任一命中即可),缺失 → `KEY_NOTFOUND` 带路径 |
| `HIDDEN` | 双向跳过 |
| `OMIT_EMPTY` | encode 省略零值(false/0/""/NULL/空容器,按 **C 值**判断,不受 ops 变换影响) |
| `RANGE(lo, hi)` | encode/decode 双向校验数值范围 |
| `OPS(field, json_ops)` | `encode/decode/check` 三钩子替换该字段的映射(值语义按目标,指针字段取指目标;NULL 指针直接映射 null) |
| `UNION_STYLE(field, s)` | `NESTED`(F1,默认)/ `FLAT`(F4,变体字段平铺到父对象,body 键消失)/ `UNWRAP`(F2,单字段变体直出其值) |

- 同一字段的多行策略**按方面合并**(后行覆盖同行方面);
- 别名冲突:**后者覆盖**(默认);`opt->strict` 下报错,且未知键也报错;
- `shrewd_basic_json_keys(type, info, cb, ctx)` 按字段序导出合并视图(键清单/别名/
  标志/范围)——JSON Schema、文档、前端类型的生成都从这里走;
- 策略表是 const 数据(DEFINE/DECLARE 纪律,同 reflect 描述符)。

## 7. 所有权(值模型层)

- 解析/创建的值树归调用方,`shrewd_basic_json_value_free()` 释放整树;
- `set`/`append` **接管** item;被替换/删除的子树由库释放;
- `serialize` 分配的文本用 `shrewd_basic_json_text_free()` 释放(经
  `platform_alloc` 族分配)。

## 8. 错误与日志

错误码(`shrewd-basic/platform/error.h`):

| 码 | 含义 |
|---|---|
| `JSON_SYNTAX = -300` | 文本不合文法(`error.offset` 指向出错字节,`message` 简述) |
| `JSON_TYPE_MISMATCH = -301` | 值与目标形态不符(如非数字读数字) |
| `JSON_DEPTH = -302` | 嵌套超限 |
| `JSON_UNSUPPORTED = -303` | 无 JSON 表示的构造(上层使用) |

日志走共享 `"json"` 模块:WARN 参数错、TRACE 解析/序列化进度。

## 9. 工程配套

- 测试 `shrewd-basic-json-tests`:文法严格性与宽松开关、数字保真、转义/代理对、
  DOM 访问与修改、紧凑/pretty 输出、文本级往返;
- 测试 `shrewd-basic-json-map-tests`:全形态 encode/decode 往返(含 F1 union、
  位组、多维行、FAM、动态数组、指针图)、decode 规则、错误路径与 json_free;
- 测试 `shrewd-basic-json-policy-tests`:键别名/必填/隐藏/省略/范围/自定义钩子/
  union 形态覆盖/键清单导出/严格模式;
- demo `programs/json_demo.c`:encode(紧凑/pretty)、decode、DOM 查询修改、策略;
- 构建随库源码自动 glob,子项目集成零污染;验证纪律同全库
  (GCC/Clang 严格告警、valgrind、ASan/UBSan)。
