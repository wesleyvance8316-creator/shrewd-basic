# reflect 应用与外部需求

reflect 是通用地基(描述符 + 查询/读写/图算法/遍历);本档收录**建立在
其上的应用层**——已实现的 json 映射与在候选清单里的映射/序列化需求。
应用层的共同特征:结构描述只在 reflect 一处(单一事实源),应用只做
"叶子层编码"(值 ↔ 文本/字节的互转);公共递归(遍历、比较、拷贝、
所有权)全部复用 reflect 既有能力,visit/diff 全差异等 API 正是为它们
准备的。

## 1. json 映射(已实现)

**定位**:reflect 的第一个应用——结构体 ↔ JSON 文本的双向映射,外加
独立可用的 JSON 值模型(DOM)。完整参考见 [json reference](json.md),
此处是能力现状与设计要点。

### 形态:三层

| 层 | 内容 | 不依赖 reflect |
|---|---|---|
| 值模型(DOM) | RFC 8259 严格解析/序列化、流式 pull 解析、访问器/修改器 | ✅ 独立可用 |
| 结构映射 | `shrewd_basic_json_encode()` / `shrewd_basic_json_decode()` 双向 | 描述符驱动 |
| 策略注册 | Go 结构体标签的 C 等价物(`SHREWD_BASIC_JSON_DEFINE`) | 引用描述符,不复制 |

### 能力清单(现状全绿)

- **文本进出**:严格 RFC 8259 解析(错误带路径/偏移)、美化序列化、
  pull 流式分词器(缓冲与 `shrewd_basic_json_pull_open_reader()` 读取器
  双源)、`\uXXXX` 转义、UTF-8 校验选项;
- **值模型**:`shrewd_basic_json_value_t` 构造/查询/修改、
  `shrewd_basic_json_pointer()`(RFC 6901)与写入、`shrewd_basic_json_schema()`
  导出(draft-07 子集)+ `shrewd_basic_json_schema_check()` 校验;
- **结构映射**:标量/枚举/字符串/文本缓冲/位组/定长与动态数组/柔性尾部/
  指针图的编解码;派生计数成员;键别名与宽松匹配;
- **策略行**:`KEY`(规键+别名)、`REQUIRED`、`HIDDEN`、`OMIT_EMPTY`、
  `RANGE`、`OPS`/`OPS_ELEM`(自定义编解码/校验/释放)、`FORMAT`/`PATTERN`
  (schema 标注)、`UNION_STYLE`、`DEFAULT`/`DEFAULT_STR`/`DEFAULT_JSON`;
- **union 线格式**:F1 嵌套(默认)/F4 平铺/F2 单字段直出;
- **所有权契约**(无登记表):decode 先清零、默认值由策略物化、
  `BORROWED` 字段归应用——`shrewd_basic_json_free()` 递归释放一切非空
  指针即精确;
- **并发**:`shrewd_basic_json_decode*()` 与 `shrewd_basic_json_free()`
  无共享状态,多线程自由使用(映射层零全局)。

### 应用演进记录

- **默认值二期**(原路线图 A2):由 `SHREWD_BASIC_JSON_DEFAULT_JSON(member, text)`
  统一交付——任意形状(嵌套 struct/数组/ops 字段)的默认值以 JSON 文本
  声明、decode 物化、与解码值同源同销;原拟议的独立数组默认宏不再需要。
- 工程配套:3 测试套件(值模型/映射/策略)、`programs/json_demo.c`、
  `tools/json_bench.*`。

## 2. 二进制序列化(struct ↔ bytes,已实现)

**动机**:协议编解码、共享内存、持久化快照——JSON 之外的第二大映射需求。

**落地形态**(独立模块 `shrewd-basic/bin`,头文件
`shrewd-basic/formats/bin.h`):

```c
int shrewd_basic_bin_encode(const void *obj, const type_t *type,
                            const bin_options_t *opt, buf_t *out);
int shrewd_basic_bin_decode(const void *bytes, size_t n, void *obj,
                            const type_t *type, const bin_options_t *opt);
/* opt: 端序(LE/BE/本机)、对齐(紧凑 vs 自然)、长度前缀宽度、版本号 */
```

**设计要点**:位组/多维/union(带 tag)/FAM 全部有自然线格式;所有权契约
沿用 json 的"decode 先清零、free 递归非空"模型(无需登记表);描述符的
`sb_refl_align`(对齐)与版本指纹是前置件,已就位。

**实现现状**(2026-11-21):`shrewd_basic_bin_encode()` /
`shrewd_basic_bin_decode()` / `shrewd_basic_bin_value_size()` /
`shrewd_basic_bin_free()` / `shrewd_basic_bin_bytes_free()`,选项含端序
(LE/BE/本机)、自然对齐填充、用户版本戳;12 字节自描述头("SBIN"+格式
版本+flags+版本号),规范宽度标量(成员宽度不泄漏到线格式,ILP32 的 long
仍编 8 字节 int64)、presence/长度前缀的字符串与指针、派生计数的动态
数组/哨兵表/柔性尾部、tag+活动变体的 union、按单元规范宽度的位组。
所有权与错误契约同 json(BIN_FORMAT/BIN_UNSUPPORTED 号段 -400);环无
线格式(报错,共享目标按树展开);ops-get/set 与 opaque 字段暂不支持。
测试 8 套件(valgrind 干净),`programs/bin_demo.c` 演示。
**状态:✅ 已实现**。

## 3. 通用文本层(render/scan)——建设中(2026-11-22 决策)

**动机**:`ops->render` 已有雏形(dump 使用);反向 `scan`(文本→字段)
没有。INI/CSV/env/args/TOML 全部共享同一个"文本 ↔ 叶子值"需求——
多个消费者收敛,公共层从"过早抽象"转为**必要地基**。

**落地形态**:字段级 `scan`/`render` ops + `shrewd_basic_reflect_scan_text()`
/ `shrewd_basic_reflect_render_text()`(叶子文本互转:标量/枚举按名/
文本缓冲与 owned 串;浮点往返安全;严格全文消费)。各格式(第 4 节)
只做结构层(节/行/表)。

**状态:✅ 已落地**(render_text/scan_text + ops->scan;五个格式模块共享)。

## 4. 格式映射全景(分类与决策,2026-11-22)

visit/diff 全差异已把公共递归做好,每种格式只是**结构层编码**(叶子
走第 3 节公共文本层);所有权契约统一 json 模式(decode 先清零、
free 递归回收)。

| 格式 | 分类 | 复杂度 | 决策与要点 |
|---|---|---|---|
| **INI** | ✅ 已实现 | 低 | 节 ↔ 嵌套结构体(`[a.b]` 路径),键值 ↔ 叶子;注释入档;未知键 strict;行号报错 |
| **env / args** | ✅ 已实现 | 低 | `PREFIX_` + UPPER(member) 映射;env 覆盖式语义,args 收 `NAME=VALUE`/`--NAME=VALUE` |
| **CSV** | ✅ 已实现 | 低-中 | 表 ↔ struct 数组,RFC 4180;表头按名匹配,嵌套 `a.b` 展平,错误带行号 |
| **TOML** | ✅ 已实现 | 中 | **TOML 1.0 子集**(类型化值/数组/`[a.b]` 表);22 项不支持构造带行号响亮拒绝 |
| **msgpack** | ✅ 已实现 | 中 | 二进制 JSON 同形(map+标量+数组,BE,最小编码);宽串 ↔ UTF-8 str;拒绝清单入口即拒 |
| **CBOR / BSON** | ⏳ 后续讨论 | 中 | 前批(上表)做完后专项评估 |
| **XML** | ⏳ 规划(需求触发即做) | 中高 | 难点=映射语义(attr/element 需注解指令);形态:严格子集、DTD/外部实体默认关闭(XXE 防护);**真实数据源出现即立项** |
| **YAML** | ❌ 不自研 | 高 | 缩进文法/隐式类型(挪威问题)/锚点别名与"绝不静默出错"正面冲突,子集必然半兼容;真需求 → **引第三方库 + ops/叶子适配层** |
| **protobuf/FlatBuffers/Cap'n Proto** | ❌ 不适配 | — | 线格式按**字段编号**寻址、需 IDL 编译器与未知字段保留,与 reflect 按名映射是两个物种 |

## 5. 宽字符串互转(json/bin)——已落地

> 状态:**已完成**(2026-11-22,提交 fca96c4)。reflect 层宽字符串
> ([roadmap B4](reflect-roadmap.md))与应用层互转全部闭环。

- **json ↔ UTF-8**:encode 转出(代理对合成)、decode 转入(代理对
  校验),严格双向——非法码元序列报 `REFLECT_ENCODING`,绝不替换字符;
- **bin 线格式**:码元序列 + 长度前缀(u32 码元数),端序跟随 bin
  选项;缓冲 = 无 presence 的码元序列,指针 = presence + 码元序列;
- 共享转换器 `reflect_utf.c`(`reflect_utf8_from_units` /
  `reflect_units_from_utf8`,strict),自有宽字符串 free 路径闭环;
- 同批落地的宽标量(json):`LONG_DOUBLE` 数字(double 精度护栏)、
  `COMPLEX` `[re, im]`、`BIT_INT` 不支持指路 ops;bin 侧三型原宽
  原始字节。

---

## 构建选项(按需裁剪)

数据格式应用是**可选件**:核心(reflect/log/platform/ds)总是构建,
每个格式一个 CMake 开关,启用项目需要的即可:

```sh
cmake -B build -DSHREWD_BASIC_FORMAT_JSON=ON \   # json(默认 ON)
          -DSHREWD_BASIC_FORMAT_BIN=OFF          # bin(默认 ON)
```

| 选项 | 影响 | 默认 |
|---|---|---|
| `SHREWD_BASIC_FORMAT_JSON` | json 源码、json 测试三套件、json demo | ON |
| `SHREWD_BASIC_FORMAT_BIN` | bin 源码、bin 测试、bin demo | ON |

生成的 `shrewd-basic/config/config.h` 提供特性探测宏
`SHREWD_BASIC_FORMAT_JSON` / `SHREWD_BASIC_FORMAT_BIN`(0/1),消费方
代码可按宏条件编译。未来的 yaml/xml/ini/csv 各自追加一个同款选项
即可(顶层 CMakeLists 的选项块 + library/tests/programs 各一段
`if()`)。

---

## 需求登记约定

新的应用层需求记入本档(动机/拟议形态/设计要点/规模/状态),
reflect 本体的演进记入 [reflect roadmap](reflect-roadmap.md);
两者的分界线:**改动 reflect 描述符/查询/图算法 = 本体;在既有能力上
做值互转/线格式 = 应用**。
