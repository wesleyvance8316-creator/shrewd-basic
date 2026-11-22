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

## 3. 通用文本格式化/解析(render/scan)候选

**动机**:`ops->render` 已有雏形(dump 使用);反向 `scan`(文本→字段)
没有。目标是字段级文本互转的通用层(配置解析、展示)。

**拟议形态**:字段级 `scan` ops + `shrewd_basic_reflect_scan(obj, type,
text, fmt)` / `shrewd_basic_reflect_format(...)`。

**状态:⏳ 需求驱动**(展示/配置解析场景出现再做)。

## 4. 其他映射(CSV/INI/YAML)候选

visit/diff 全差异已把公共递归做好,每种格式只是叶子层编码:

- **CSV**:定长字段表 ↔ 行(嵌套结构展平为列前缀);
- **INI**:节 ↔ 结构体(嵌套深度有限,配合路径语法);
- **YAML**:子集(块映射/序列),避免完整规范的泥潭。

**状态:⏳ 需求驱动**。与二进制序列化共用"契约式所有权 + 策略行"骨架。

## 5. 宽字符串互转(json/bin 二期)候选

reflect 层的宽字符串(`STRING16`/`STRING32`)已落地([roadmap B4](reflect-roadmap.md));
json/bin 侧目前对宽字符串干净报 `JSON_UNSUPPORTED`/`BIN_UNSUPPORTED`
(不静默出错)。待补的互转:

- **json ↔ UTF-8**:encode 转出(代理对合成)、decode 转入(代理对校验);
- **bin 线格式**:码元序列 + 长度前缀,端序跟随 bin 选项(建议默认小端,
  文档写死);
- 自有宽字符串的 free 路径已就绪,互转落地即可闭环。

**状态:⏳ 需求驱动**(UTF-16/32 数据源出现再做)。

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
