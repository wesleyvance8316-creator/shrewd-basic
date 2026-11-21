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

## 2. 二进制序列化(struct ↔ bytes)候选

**动机**:协议编解码、共享内存、持久化快照——JSON 之外的第二大映射需求。

**拟议形态**(独立模块,如 `shrewd-basic/bin`):

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

**规模**:新模块约 800 行 + 测试。**状态:⏳ 需求驱动**——设计可直接套
json 的三层经验,需求确认即启动。

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

---

## 需求登记约定

新的应用层需求记入本档(动机/拟议形态/设计要点/规模/状态),
reflect 本体的演进记入 [reflect roadmap](reflect-roadmap.md);
两者的分界线:**改动 reflect 描述符/查询/图算法 = 本体;在既有能力上
做值互转/线格式 = 应用**。
