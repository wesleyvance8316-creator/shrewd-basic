# json API 总览

速查手册:每个接口给出签名与语义要点。完整语义、线格式契约与
设计论证见 [json.md](json.md);策略行宏的注解生成见
[annotations.md](annotations.md)。

**三层架构**:值模型(`shrewd_basic_json_value_t` DOM)↔ 文本进出
(parse/serialize)↔ 结构映射(reflect 描述符驱动的 encode/decode)+
策略注册(`shrewd_basic_json_info_t`,Go 结构体标签的 C 等价物)。

**所有权一句话**:构造/解析/解码出来的树归调用方,
`json_value_free`/`json_text_free`/`json_free` 对位释放;
`set`/`append`/`replace_at` 接管 item,失败时归还。

## 0. 选项(`shrewd_basic_json_options_t`,零值 = 文档默认)

| 字段 | 层 | 语义 |
|---|---|---|
| `pretty` / `indent` | serialize | 缩进输出;indent 0 = 2 |
| `max_depth` | parse/serialize | 嵌套上限(默认 32,按值层级) |
| `allow_comments` / `allow_trailing_commas` | parse | 宽松文法 |
| `allow_nan` | encode | NaN/inf → null(默认报错) |
| `allow_unknown_union` | encode | 未知 union tag → null body |
| `strict` | decode | 未知键与别名冲突报错 |
| `use_defaults` | decode | 缺失键物化 DEFAULT 值 |
| `key_match` | decode | `EXACT`(默认)/ `LOOSE`(忽略大小写与 `_`) |
| `escape_unicode` | serialize | 非 ASCII 全转义 `\uXXXX` |
| `validate_utf8` | parse | 拒绝串内非法 UTF-8(默认原样透传) |
| `skip_bom` | parse | 剥一个文档首 UTF-8 BOM(offset 相对剥除后文本) |
| `jsonl` | pull | 连续多文档(JSON Lines);`parse` 仍单文档 |
| `sort_keys` | serialize | 对象成员按键名字节序(非 JCS 规范形) |

## 1. 值模型

### 构造(创建即拥有)

| 接口 | 语义 |
|---|---|
| `json_new(kind, &v)` | null / array / object(kind 只能是这三个) |
| `json_new_bool(b, &v)` | 布尔 |
| `json_new_i64(i, &v)` / `json_new_u64(u, &v)` | 整数 token(u64 全位宽) |
| `json_new_f64(d, &v)` | 最短往返格式;非有限值 `ARG_INVALID` |
| `json_new_string(text, &v)` / `_new_string_n(text, n, &v)` | 字符串(拷贝) |

### 读取

| 接口 | 语义 |
|---|---|
| `json_kind(v)` / `json_count(v)` | 类型 / 成员数(对象或数组) |
| `json_at(v, i)` / `json_key_at(v, i)` | 第 i 个成员的值 / 键名 |
| `json_get(v, key)` | 首个同名成员(重复键第一个生效) |
| `json_to_bool/to_i64/to_u64/to_f64(v, &out)` | 按值读取;`1.0` 读作 1,越界/异型 → `JSON_TYPE_MISMATCH` |
| `json_to_string(v)` | 字符串内容(非字符串 NULL) |
| `json_number_text(v)` | 数字的**原始 token 文本**(往返保真) |

### 修改与释放

### shrewd_basic_json_set(object, key, item)
替换同名成员或追加;**接管 item**(失败归还)。对象保持成员文档序,
重复键只替换第一个。

### shrewd_basic_json_append(array, item)
追加元素;接管 item(仅接收非空且为值的 item)。

### shrewd_basic_json_remove(value, key) / shrewd_basic_json_remove_at(value, index)
删除并释放该子树;`remove` 找不到 → `KEY_NOTFOUND`。

### shrewd_basic_json_replace_at(value, index, item)
原位替换数组元素/对象成员(释放旧值,接管 item)。

### shrewd_basic_json_value_clone(value, &out)
深拷贝,**文本保真**:数字保留原始 token、对象成员序与重复键原样;
`out` 获得所有权。

### shrewd_basic_json_value_free(value)
整树释放(NULL 安全)。

## 2. 文本进出

### shrewd_basic_json_parse(text, length, &value, options, &error)
严格 RFC 8259(宽松项见选项表)。`error` 带 `offset`/`message`。

### shrewd_basic_json_serialize(value, &text, options, &error)
DOM → 紧凑文本(可 pretty / sort_keys / escape_unicode);
`json_text_free()` 释放文本。

### shrewd_basic_json_serialize_to(value, write, ctx, options, &error)
同上但流式写入 sink(`shrewd_basic_json_write_fn`),不落全量文本;
`json_write_file(ctx, data, len)` 是现成的文件 sink。

## 3. 结构映射(reflect 驱动)

`info` 为策略表(可 NULL = 纯元数据行为)。decode 契约:**先清零目标**,
失败可 `json_free` 回收;FAM 目标按 `json_value_size` 先行定尺寸。

### shrewd_basic_json_encode(object, type, info, &text, options, &error) / _encode_to(...)
struct → JSON 文本(encode_to 走 sink)。

### shrewd_basic_json_encode_value(object, type, info, &value, options, &error)
struct → DOM 值。

### shrewd_basic_json_decode(text, length, object, type, info, options, &error) / _decode_value(value, object, type, info, options, &error)
JSON → struct(文本版含 parse)。错误带 JSON 路径。

### shrewd_basic_json_decode_pointer(value, pointer, object, type, info, options, &error)
局部解码:把 JSON Pointer 指到的子文档映射进对象。

### shrewd_basic_json_free(object, type, info)
递归释放 decode 分配的一切(owned 串、指针目标、动态表、FAM 外块)。

### shrewd_basic_json_value_size(value, type, &size)
FAM 尺寸助手:decode 目标所需字节(头 + JSON 携带的尾部)。

## 4. 策略注册

### SHREWD_BASIC_JSON_DEFINE(var, type_ref, ...) / SHREWD_BASIC_JSON_DECLARE(var)
注册/导入每类型策略表(DEFINE/DECLARE 纪律,同 reflect 描述符)。

### 行宏(一指令一行,同字段多行按方面合并、后行覆盖)

| 宏 | 语义 |
|---|---|
| `KEY(field, 规键, 别名...)` | encode 出**规键**(首名);decode 认全部 + **成员名永远可接受** |
| `REQUIRED` | decode 缺键 → `KEY_NOTFOUND`(use_defaults 物化可满足) |
| `DEFAULT(field, v)` / `DEFAULT_STR(field, "s")` / `DEFAULT_JSON(field, text)` | 缺失键默认值(use_defaults 物化;JSON 版任意形状) |
| `BORROWED(field)` | 指针归应用:decode 不写、free 不碰 |
| `HIDDEN` | 双向跳过 |
| `OMIT_EMPTY` | encode 省略零值(false/0/""/NULL/空容器) |
| `RANGE(field, lo, hi)` | 数值范围双向校验 |
| `OPS(field, json_ops)` / `OPS_ELEM(field, json_ops)` | encode/decode/check 钩子替换映射(字段级/逐元素) |
| `FORMAT(field, "s")` / `PATTERN(field, "s")` | schema 标注(不参与校验;校验走 `OPS->check`) |
| `UNION_STYLE(field, s)` | `NESTED`(F1 默认)/ `FLAT`(F4)/ `UNWRAP`(F2) |

### shrewd_basic_json_keys(type, info, fn, ctx)
按字段序导出合并视图(键清单/别名/标志/范围)——schema、文档、
前端类型的生成都从这里走。

## 5. 工具集

### shrewd_basic_json_pointer(root, pointer, &out)
RFC 6901 查询(`~0`/`~1` 转义;数组段下标、对象段键名)。

### shrewd_basic_json_pointer_set(root, pointer, item)
写入:末段替换成员/元素,`"-"` 数组追加;失败 item 归还调用方。

### shrewd_basic_json_schema(type, info, &text, options, &error)
导出 draft-07 子集:properties/required/minimum/maximum/enum/
x-aliases,嵌套 `$defs`+`$ref`(自引用安全),union 出 `oneOf` 并标注
`x-union-style`。

### shrewd_basic_json_schema_check(document, schema, &error)
按 schema 校验 DOM(type/enum/minimum/maximum/maxLength/maxItems/
required/properties/items/oneOf/anyOf/$ref);别名与 decode 一致;
错误带文档路径;format/pattern 不校验(无正则引擎)。

## 6. 流式解析(pull)

文法唯一实现在 pull;`json_parse` = pull + DOM 构建。token 文本存活
到下一次 `pull_next`/`pull_capture`;缓冲源无转义串与数字 token
**零拷贝**直指源文本。

### shrewd_basic_json_pull_open(&pull, text, length, options, &error) / _open_reader(&pull, read, ctx, options, &error)
缓冲源 / reader 回调源。

### shrewd_basic_json_pull_next(pull, &token)
下一 token:`OBJECT_BEGIN/KEY/VALUE/ARRAY_END...`,`TOKEN_END` 流末。

### shrewd_basic_json_pull_capture(pull, &out)
当前位置子树抓成 DOM(超大文档可只抓局部);`jsonl` 模式下流末
返回 `(OK, NULL)` 哨兵——文档循环:
`while ((OK == pull_capture(pull, &doc)) && (NULL != doc))`。

### shrewd_basic_json_pull_close(pull)

## 7. 错误码(`shrewd-basic/platform/error.h`,JSON 号段 -300)

| 码 | 语义 |
|---|---|
| `JSON_SYNTAX = -300` | 文法错(error 带 offset) |
| `JSON_TYPE_MISMATCH = -301` | 值与期望形状不符(映射层带路径) |
| `JSON_DEPTH = -302` | 嵌套超限 |
| `JSON_UNSUPPORTED = -303` | 无 JSON 表示的构造(如 BIT_INT、非有限数) |

并发:值树**只读**共享安全;`json_decode*`/`json_free` 多线程自由
使用(映射层零共享状态);log 侧全局状态见 [json.md](json.md) §10。
