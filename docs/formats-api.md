# 格式族 API 总览(文本层 + 六格式)

速查手册:公共文本层与 INI/env/CSV/TOML/msgpack/CBOR 六个格式
模块的接口与语义。json/bin 各有专册([json-api](json-api.md),
bin 见 [reflect-apps](reflect-apps.md) §2);分类决策见
[reflect-apps](reflect-apps.md) §4;演示程序 `programs/formats_demo.c`
覆盖全部接口。

**共用契约**:所有权统一 json 模式(decode 先清零、free 只回收内容
不碰根);叶子互转走公共文本层(严格:拒绝静默窄化/截断);错误码
复用 `REFLECT_*`(-200)、`JSON_*`(-300)、`BIN_*`(-400)。

## 0. 公共文本层(reflect 层)

```c
int shrewd_basic_reflect_render_text(field, value, buf, cap, &out_len); /* 叶子→文本 */
int shrewd_basic_reflect_scan_text(field, value, text, len);            /* 文本→叶子 */
```

- **往返安全**:浮点按十进制有效位(`%.9g`/`%.17g`/`LDBL_DECIMAL_DIG`)、
  枚举按常量名(扫描 miss 落回十进制);
- **严格**:全文消费、拒静默窄化(浮点窄化即 `REFLECT_TEXT`)、
  缓冲超长 `REFLECT_MISMATCH`;缓冲拷贝+NUL、owned 分配、借用/宽串
  `REFLECT_NOT_SUPPORTED`;
- `ops->scan` 钩子镜像 `ops->render`(自定义叶子);
- `render_text` 的 `buf` 可 NULL 测量;截断 `REFLECT_MISMATCH` +
  `out_len` 报全量。

## 1. INI(formats/ini.h)

```c
int  shrewd_basic_ini_encode(object, type, &out_text, options, error);
int  shrewd_basic_ini_decode(text, length, object, type, options, error);
void shrewd_basic_ini_text_free(text);
int  shrewd_basic_ini_free(object, type);
```

- 形态:行注释(`;`/`#`)、`[section]`/`[a.b]` 路径节头(嵌套
  struct)、`key = value`;数组/指针等非叶子响亮拒绝;
- decode:未知键默认跳过、`options.strict` 报错;坏行/坏节/坏值带
  **行号**(`ini_error_t {line, message}`)。

## 2. env / args(formats/env.h)

```c
int shrewd_basic_env_scan(object, type, prefix);          /* 环境变量 */
int shrewd_basic_args_scan(object, type, prefix, argc, argv); /* 命令行 */
int shrewd_basic_env_free(object, type);
```

- 命名规则 `prefix + UPPER(member)`(`APP_` + `log_level` =
  `APP_LOG_LEVEL`);env 缺省名不动字段(覆盖式语义);
- args 接受 `NAME=VALUE` 与 `--NAME=VALUE`,首个匹配生效,其余忽略;
  只映射根级叶子。

## 3. CSV(formats/csv.h)

```c
int  shrewd_basic_csv_encode(rows, count, row_type, &out_text, error);
int  shrewd_basic_csv_decode(text, length, &out_rows, &out_count, row_type, error);
int  shrewd_basic_csv_rows_free(rows, count, row_type);
void shrewd_basic_csv_text_free(text);
```

- 行 ↔ struct、表 ↔ struct 数组;嵌套展平 `a.b` 列前缀;
- RFC 4180 引号(`"` 双写、内嵌换行);表头**按名匹配**(列序无关)、
  缺列留零、未知/重复/列宽不齐带行号报错。

## 4. TOML(formats/toml.h)

```c
int  shrewd_basic_toml_encode(object, type, &out_text, error);
int  shrewd_basic_toml_decode(text, length, object, type, error);
void shrewd_basic_toml_text_free(text);
int  shrewd_basic_toml_free(object, type);
```

- **TOML 1.0 子集**:`[table]`/`[a.b]`、基本/字面串(转义含 `\u`/
  `\U`+代理对)、整数(十进制/`0x`/下划线/`+`)、浮点(保形)、
  布尔、同类型标量数组;**22 项不支持构造带行号响亮拒绝**
  (datetime/多行串/内联表/点分键/数组表/引号键/异类型数组/重复键表…)。

## 5. msgpack(formats/msgpack.h)

```c
int  shrewd_basic_msgpack_encode(object, type, &out_bytes, &out_size);
int  shrewd_basic_msgpack_decode(bytes, size, object, type);
void shrewd_basic_msgpack_bytes_free(bytes);
int  shrewd_basic_msgpack_free(object, type);
```

- 与 json **同形映射**(map{str 键}、标量最小编码、BE);char →
  单字符 str、枚举 → 名、long double → f64 精度护栏、complex →
  `[re, im]`、宽串 ↔ UTF-8(严格转换器)、指针 nil/递归;
- 拒绝清单**入口即拒**(位组/union/FAM/动态/哨兵/BIT_INT/opaque/
  ops 钩子字段);decode 全消费(尾随字节 `BIN_FORMAT`)。

## 6. CBOR(formats/cbor.h)

```c
int  shrewd_basic_cbor_encode(object, type, &out_bytes, &out_size);
int  shrewd_basic_cbor_decode(bytes, size, object, type);
void shrewd_basic_cbor_bytes_free(bytes);
int  shrewd_basic_cbor_free(object, type);
```

- RFC 8949 子集:定长形、最小整数、f32/f64、true/false/null;
  **tag/不定长/简单扩展/bytes 项响亮拒绝**;f16 仅解码(扩 f32);
- 形状映射与 msgpack 完全一致(同族二进制格式,共享
  `bin-internal.h` 读写骨架)。

## 7. 使用示例(节选,全量见 programs/formats_demo.c)

```c
/* INI 配置往返 */
shrewd_basic_ini_decode(text, strlen(text), &cfg, &cfg_reflect, NULL, &err);
char *wire = NULL;
shrewd_basic_ini_encode(&cfg, &cfg_reflect, &wire, NULL, &err);
shrewd_basic_ini_text_free(wire);
shrewd_basic_ini_free(&cfg, &cfg_reflect);

/* env/args 启动灌配置(覆盖式) */
shrewd_basic_env_scan(&cfg, &cfg_reflect, "APP_");
shrewd_basic_args_scan(&cfg, &cfg_reflect, "APP_", argc, argv);

/* CSV 表 */
shrewd_basic_csv_decode(text, len, (void **)&rows, &count, &row_reflect, NULL);
shrewd_basic_csv_rows_free(rows, count, &row_reflect);

/* msgpack / CBOR 二进制往返 */
shrewd_basic_msgpack_encode(&obj, &obj_reflect, &bytes, &size);
shrewd_basic_cbor_encode(&obj, &obj_reflect, &bytes, &size);
```

## 8. 已知限制

- TOML 子集边界见 §4(拒绝清单逐项带行号);CSV 借用串拒绝
  (scan_text 契约);msgpack/CBOR 的 BIT_INT 走 ops 逃生口;
- env/args 只映射根级叶子;INI 数组 v1 拒绝。
