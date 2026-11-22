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
`options.escape_unicode` 把非 ASCII 全部转义为 `\uXXXX`(BMP 外出代理对);
嵌套超限报 `JSON_DEPTH`。`options.validate_utf8` 让解析拒绝串内非法 UTF-8
(默认原样透传)。

**拉取解析器(pull tokenizer)**:文法的唯一实现在 `shrewd_basic_json_pull_*`
——流式分词器(完整缓冲源与 reader 回调源),应用可直接驱动令牌流
(`OBJECT_BEGIN/KEY/VALUE/ARRAY_END...`)处理超大文档;`pull_capture` 把当前位置的
子树抓成 DOM(`{header:{...}, items:[百万级]}` 可只抓 header 与单个 item);
`json_parse` = pull + DOM 构建,同一份文法。缓冲源的无转义串与数字 token
**零拷贝**直指源文本;令牌文本存活到下一次 `pull_next`/`pull_capture`。

**输出 sink**:序列化的核心是 `shrewd_basic_json_write_fn` 回调——
`json_serialize_to` / `json_encode_to` 流式写文件/socket/收集器,不落全量文本;
`json_write_file` 是 FILE* 的现成实现,`serialize`(返回文本)是收集器包装。

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
| `STRING16` / `STRING32`(宽字符串) | **暂不支持**:encode/decode 报 `JSON_UNSUPPORTED`(UTF-8 互转是二期);free 路径已识别,不泄漏 |
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

- 目标**先清零**;`opt->use_defaults` 开启时,缺失键由策略 `DEFAULT` 行
  **物化默认值**(decode 分配,与正常值同源),`REQUIRED` 由"键出现
  **或**默认值物化"满足;
- **计数成员由数组长度驱动**(JSON 里的计数键被忽略);
- 数组可**短于**槽位(尾部保持零),不可更长;字符串缓冲恰满可无 NUL,超长报错;
- 未知键忽略;整数/浮点按值宽窄校验(越界报 `JSON_TYPE_MISMATCH`);
- 每个错误都带 JSON 路径(`err->path`,如 `body.point.x`);
- FAM 根的分配由调用方按 JSON 尾长预留:`sizeof(T) + n * sizeof(elem)`。

### 映射层所有权(契约模型,无登记表)

所有权由三条契约静态确定,`shrewd_basic_json_free()` 递归释放**一切非空
指针**即精确无误——没有任何运行时记账:

1. **decode 目标先清零**:decode 写入的每个指针都是 decode 分配的;
2. **默认值由策略声明、decode 物化**(`use_defaults` + `DEFAULT` 行),
   不是结构体里的预置字节——来源与正常值统一;
3. **`BORROWED` 字段是应用自己的指针**:decode 不写、free 不碰。decode 会
   先清零整个目标,故在 **decode 之后**给它赋值(静态字面量、借用缓冲、
   自有分配皆可,生命周期自理)。

由契约派生的使用规则:

- **不得在仍持有 decode 产物的对象上再次 decode**(先 `json_free`;
  清零会让旧块变孤儿);decode 失败后用 `json_free` 清理半成品;
- 不得往解码对象里手装外来指针(BORROWED 字段除外);
- 文档内重复键/别名的 last-wins 会先释放前值(契约保证那必是本次
  decode 的分配);
- `ops->decode` 产出的目标由 `ops->destroy` 释放(第四类来源,按字段
  静态已知,不需运行时判定)。**根存储归调用方**(decode 不分配它)。

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
| `REQUIRED` | decode 时键必须出现(别名任一命中即可),缺失 → `KEY_NOTFOUND` 带路径;`use_defaults` 下**默认值物化也可满足** |
| `DEFAULT(field, v)` / `DEFAULT_STR(field, "s")` / `DEFAULT_JSON(field, text)` | 缺失键的默认值(`use_defaults` 下由 decode 物化):数值含整数/枚举/bool/char/浮点,字符串为拷贝分配;`DEFAULT_JSON` 收一段 JSON 文本,**任意形状**(struct/数组/嵌套/ops 字段),内部走正常解码通路,产物与解码值同源同销(FAM 字段除外——根分配按现存 JSON 定尺寸) |
| `BORROWED(field)` | 指针字段归应用:decode 不写、`json_free` 不碰(decode 后赋值,任意来源、任意生命周期) |
| `HIDDEN` | 双向跳过 |
| `OMIT_EMPTY` | encode 省略零值(false/0/""/NULL/空容器,按 **C 值**判断,不受 ops 变换影响) |
| `RANGE(lo, hi)` | encode/decode 双向校验数值范围 |
| `OPS(field, json_ops)` | `encode/decode/check` 三钩子替换该字段的映射(值语义按目标,指针字段取指目标;NULL 指针直接映射 null) |
| `OPS_ELEM(field, json_ops)` | 同一套钩子按**元素**作用于数组字段(逐元素编解码/校验/释放,指针元素的槽位语义同字段级) |
| `FORMAT(field, "date-time")` / `PATTERN(field, "^")` | Schema 的 `format`/`pattern` 标注(**仅标注**,不参与 decode 校验——C11 无正则,校验请走 `OPS->check`) |
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

## 9. 工具集

| 工具 | 语义 |
|---|---|
| `json_pointer(root, "/a/b/0", &out)` | RFC 6901 JSON Pointer 查询(`~0`/`~1` 转义;数组段下标、对象段键名) |
| `json_pointer_set(root, "/a/b/-", item)` | Pointer 写入:末段替换成员/元素或 `"-"` 数组追加;失败时 `item` 仍归调用方 |
| `json_decode_pointer(value, "/a/b", obj, type, ...)` | 局部解码:把指针指向的子文档映射进对象(根不必是 struct,标量亦可) |
| `json_replace_at(value, i, item)` | 原位替换数组元素/对象成员(释放旧值) |
| `json_schema_check(doc, schema, &err)` | Schema 校验器:按导出的关键字集校验 DOM(`type/enum/minimum/maximum/maxLength/maxItems/required/properties/items/oneOf/anyOf/$ref`),错误带文档路径;**别名与 decode 一致**(成员名与 `KEY` 别名都算命中),`oneOf` 靠变体 required 判别 |
| `json_schema(type, info, &text)` | 导出 draft-07 子集 Schema:属性/`required`/`minimum`/`maximum`/`enum`/`x-aliases`,嵌套结构体走 `$defs`+`$ref`(自引用安全),union 出 `oneOf` 并按
  `UNION_STYLE` 标注 `x-union-style`(UNWRAP 变体直出裸值) |
| `json_value_size(value, type, &size)` | FAM 尺寸助手:返回 decode 目标所需字节(头 + JSON 尾长) |
| `reflect_gen.py` 骨架 | 为每个 struct 产出注释形式的 `SHREWD_BASIC_JSON_DEFINE` 策略模板(解开注释即用) |

## 10. 并发说明

值树支持并发**只读**共享(访问器与序列化均为只读);任何修改由应用自行同步。
reflect 的字段查找缓存是 `_Thread_local` 的(多线程安全,见 reflect 文档)。

**`json_decode*` 与 `json_free` 多线程自由使用**——所有权契约模型下映射层
没有任何共享状态(解码分配登记表已删除)。模块内仅剩 log 侧的全局状态:
默认无锁,并发使用时安装 `shrewd_basic_log_set_lock()` 锁钩子——注册表、
换流(含 fclose 旧流)与整条记录的输出全程互斥,记录不再交错;禁用级别的
短路仍在锁外(并发 `set_level` 最迟一条日志后生效)。不装钩子则由应用
串行化 `log_module_*`/`log_set_global_*` 与所有日志输出。无论哪种方式,
"不销毁仍可能被他人使用的模块"始终是应用契约。

## 11. 工程配套

- 测试 `shrewd-basic-json-tests`:文法严格性与宽松开关、数字保真、转义/代理对、
  DOM 访问与修改、紧凑/pretty 输出、文本级往返;
- 测试 `shrewd-basic-json-map-tests`:全形态 encode/decode 往返(含 F1 union、
  位组、多维行、FAM、动态数组、指针图)、decode 规则、错误路径与 json_free;
- 测试 `shrewd-basic-json-policy-tests`:键别名/必填/隐藏/省略/范围/自定义钩子/
  union 形态覆盖/键清单导出/严格模式;
- demo `programs/json_demo.c`:encode(紧凑/pretty)、decode、DOM 查询修改、策略;
- 基准 `tools/json_bench.sh`:parse/serialize/查询/encode/decode 的 ns/op 基线;
- 性能:数字 token、短字符串与**容器前 2 个成员**共享值节点的内联存储
  (同一块 32 字节,kind 互斥),小容器文档解析提升约 23%,宽文档持平;
  arena 与 encode 直写经评估暂缓(见提交说明);
- 构建随库源码自动 glob,子项目集成零污染;验证纪律同全库
  (GCC/Clang 严格告警、valgrind、ASan/UBSan)。
