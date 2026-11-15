# json 模块 — 架构、值模型与文本契约

JSON 支持按**分层**组织,本文覆盖已落地的第 0 层(值模型 + 解析/序列化)与全部
线格式契约;结构映射(第 1 层)与策略注册(第 2 层)在本文末尾的分层图中标注。

---

## 1. 分层架构

```
┌────────────────────────────────────────────────┐
│ 第 2 层:JSON 策略注册(键名/别名/省略/必填/范围) │  后续落地
├────────────────────────────────────────────────┤
│ 第 1 层:结构映射(struct ↔ DOM,reflect 驱动)    │  后续落地
├────────────────────────────────────────────────┤
│ 第 0 层:JSON 值模型(DOM)+ 解析/序列化  ← 本文  │  已落地
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

## 5. 所有权

- 解析/创建的值树归调用方,`shrewd_basic_json_value_free()` 释放整树;
- `set`/`append` **接管** item;被替换/删除的子树由库释放;
- `serialize` 分配的文本用 `shrewd_basic_json_text_free()` 释放(经
  `platform_alloc` 族分配)。

## 6. 错误与日志

错误码(`shrewd-basic/platform/error.h`):

| 码 | 含义 |
|---|---|
| `JSON_SYNTAX = -300` | 文本不合文法(`error.offset` 指向出错字节,`message` 简述) |
| `JSON_TYPE_MISMATCH = -301` | 值与目标形态不符(如非数字读数字) |
| `JSON_DEPTH = -302` | 嵌套超限 |
| `JSON_UNSUPPORTED = -303` | 无 JSON 表示的构造(上层使用) |

日志走共享 `"json"` 模块:WARN 参数错、TRACE 解析/序列化进度。

## 7. 工程配套

- 测试 `shrewd-basic-json-tests`:文法严格性与宽松开关、数字保真、转义/代理对、
  DOM 访问与修改、紧凑/pretty 输出、文本级往返;
- 构建随库源码自动 glob,子项目集成零污染;验证纪律同全库
  (GCC/Clang 严格告警、valgrind、ASan/UBSan)。
