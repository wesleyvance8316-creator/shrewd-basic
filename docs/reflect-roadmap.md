# reflect 后续计划支持项

本文档是 reflect 模块(及其直接相关的应用层)的**候选功能清单与设计草案**,
作为后续迭代的计划底稿。每项含:动机/场景、拟议形态(接口草图)、设计要点
与难点、依赖、工作量估计与优先级建议。优先级分四档:

- **P0 已排队**:此前明确推迟或遗留,方向已定;
- **P1 建议做**:价值明确、代价可控,适合有迭代窗口时排入;
- **P2 需求驱动**:设计可行,但应等真实需求出现再做(避免为想象的需求写代码);
- **P3 条件性**:依赖工具链/标准/新模块的成熟,远期跟踪。

与现状的关系:reflect 核心(描述符/宏/查询/读写/dump/路径/`*_list` 容器/
图算法/visit/生成器)已闭环,见 [reflect API reference](reflect-api.md) 与
[reflect overview](reflect-overview.md)。

---

## A. 已排队(P0)

### A1. 类型注册表(名字→类型)

> 状态:**已完成**(2026-11-21,见提交 c536558/8ada219/ea8fc8f/
> 66a8aeb/d014507/84b7ab2 与验证脚本)

**现状**:只有"描述符指针→字段"方向;`shrewd_basic_reflect_type_find_list`
需要调用方自己建容器并实现三个 ops 回调,库里没有开箱即用的按名找类型。

**动机/场景**:协议分发(线格式带类型标签,如 `"point"` → point 描述符)、
插件式序列化(按配置名解码任意类型)、调试工具(按名 dump 任意注册类型)、
代码生成器运行时(按名取描述符)。

**拟议形态**:在 `*_list` 模式上提供**开箱即用的默认容器**,而非新的全局表:

```c
/* 调用方静态/栈上声明一个容器(容量编译期定),零堆分配 */
SHREWD_BASIC_REFLECT_REGISTRY_DEFINE(myreg, 32);

shrewd_basic_reflect_list_t list;
shrewd_basic_reflect_registry_bind(&myreg, &list);   /* 填 ops + ctx */

shrewd_basic_reflect_list_register(&list, &point_type, "point");
shrewd_basic_reflect_type_find_list(&list, "point", &found);
```

**设计要点**:
- 容器 = 固定容量的条目数组 + 线性/二分查找(条目可按注册名排序,注册时
  插入有序,查找 O(log n);上限溢出返回 `MEM_NOMEM` 或容量错,不驱逐——
  注册表**绝不丢条目**);
- 并发契约与 `*_list` 完全一致:注册期单线程,稳态只读任意并发;
- 库内**不**放全局默认表(当初放弃全局缓存的理由不变);若用户想要全局,
  自己 `static` 一个 registry 即可,三行代码;
- 别名 = 多次注册同类型不同名(现有语义)。

**依赖/规模**:纯新增文件(`reflect_registry.c`)+ 宏;约 200 行 + 测试。
**优先级:P0**(已两次推迟,且是 `*_list` 的自然补全)。

### A2. JSON 默认值二期(struct/数组 DEFAULT)

> 状态:**已完成**(2026-11-21,见提交 c536558/8ada219/ea8fc8f/
> 66a8aeb/d014507/84b7ab2 与验证脚本)

**归属**:json 模块([json reference](json.md)),此处记录以免遗漏。

**现状**:`DEFAULT`/`DEFAULT_STR` 一期只覆盖标量/枚举/bool/char/浮点/字符串;
struct 字段与数组字段的默认值被跳过(保持零值)。

**拟议形态**:
- struct 默认:`DEFAULT_JSON(member, text)`,`text` 为 JSON 文本的 C 字符串
  字面量,如:

  ```json
  {"x": 0, "y": 0}
  ```

  缺键时把这段 JSON 解析并物化(decode 期按 JSON 值分配,所有权契约不变);
- 数组默认:`DEFAULT_ARRAY(member, text)` 或同走 JSON 文本路线。

**设计要点**:JSON 文本路线实现统一(复用 parse+decode 通路),代价是
default 物化引入一次解析;也可收"原始字节镜像"但表达力差、易错。
**优先级:P0**(一期明确留下的尾巴)。

### A3. MSVC/ILP32 平台验证

> 状态:**已完成**(2026-11-21,提交 ea8fc8f)

**现状**:`tools/verify_platforms.sh` 已就位,但缺工具链时跳过;`_Thread_local`
在 MSVC C11 模式的行为尚未实测。

**动作**:有工具链环境时跑一轮(MSVC `/std:c11`、ILP32 交叉编译),修
`_Thread_local` 与 `__attribute__` 相关的可移植性问题,把结果记进文档。
**优先级:P0**(工程遗留,一次性)。

---

## B. 类型系统扩展

### B1. C 位域(bit-field)支持——经生成器 + 编译器 ABI

**现状**:C 位域成员**布局实现定义**(位序、跨单元、对齐均无标准),宏无法
内省,故编译期直接拒绝;`FIELD_BITS` 覆盖"真实单元+作者给定位段"(寄存器/
协议头)。

**拟议形态**:承认"纯宏不可行",走**离线生成**路线——`reflect_gen` 的
增强版读编译器 AST(GCC `-fdump-ada-spec` 或 clang libtooling)或 DWARF,
按**本编译器的实际布局**产出 `FIELD_BITS` 风格描述符:

```c
/* 生成器产出:unit 成员 + 每个位域一个 FIELD_BITS 条目 */
SHREWD_BASIC_REFLECT_FIELD_BITS(flags_t, raw, ok, 0, 1),
SHREWD_BASIC_REFLECT_FIELD_BITS(flags_t, raw, level, 1, 3),
```

**设计要点**:
- 描述符本身**不需要新机制**——`FIELD_BITS` + `bit_offset/bit_width` 已是
  载体;缺的只是"从 C 源拿到位段布局"的工具;
- 生成产物绑定编译器/目标 ABI(生成器输出顶部写明来源与指纹),换编译器
  重新生成——这正是"实现定义"应有的处理方式;
- 运行期 get/set/dump/equal 走现有 F_BITS 语义,零改动。
**依赖**:生成器升级(B1 与 G2 联动)。**规模**:工具 约 400 行。**优先级:P2**
(等真实位域结构体需求;嵌入式寄存器场景已有 FIELD_BITS)。

### B2. 匿名 struct/union 成员平铺

> 状态:**已完成**(2026-11-21;现有宏天然支持,固化为测试+文档)

**现状**:C11 匿名成员(`struct { struct { int x; int y; }; int z; }`)在描述符
里无法直接注解——匿名成员没有成员名。

**拟议形态**:字段宏支持"零偏移平铺"约定——
`FIELD_ANONYMOUS(struct_type, ref)`(成员名留空,offset 0,访问时把匿名
聚合的字段视为外层字段)。find_field/path 对平铺字段按子字段名查找;
dump 缩进不加层。

**设计要点**:平铺只对"首成员且零偏移"的匿名聚合成立(C 标准保证匿名
成员地址与外层相同);查找需要双层索引(可把平铺子字段直接提升进外层
字段表——`TYPE_DEFINE` 时手工列子字段即可,宏糖只是省事)。
**规模**:宏 + 查找/dump 小改 约 150 行。**优先级:P2**。

### B3. 更宽的标量:`long double` / `_Complex` / `_BitInt`

**现状**:13 种基础 kind;`long double`(x87 80 位/128 位)、复数、C23
`_BitInt(N)` 未覆盖(`_Generic` 会把它们拒之门外——这是特性)。

**拟议形态**:
- `long double`:新 kind 或按 `sizeof` 映射到 FLOAT/DOUBLE 的"承载性"方案
  (危险:精度损失,建议独立 kind + 按 16 字节原样拷贝,dump 显示十进制);
- `_Complex`:拆成实虚两字段的视图,或按字节块处理(先评估使用面);
- `_BitInt(N)`:C23,`_Generic` 需要逐 N 关联(宏爆炸),可能只能支持
  常见宽度(8/16/32/64)。
**优先级:P3**(等需求;`_BitInt` 还受编译器成熟度约束)。

### B4. 宽字符字符串

**现状**:STRING kind 语义为 `char *`(NUL 结尾 UTF-8);`wchar_t` /
`char16_t` / `char32_t` 字符串无类型支持(json 侧有 UTF-8 校验,但源类型
只有 char)。

**拟议形态**:新 kind `STRING16/STRING32`(或统一 `WSTRING` + 宽度参数),
copy/equal/hash/dump 按码元序列语义;JSON 映射侧转 UTF-8。
**规模**:中(跨模块联动)。**优先级:P2**(国际化协议需求出现再做)。

### B5. 描述符携带对齐信息

> 状态:**已完成**(2026-11-21,提交 66a8aeb)

**现状**:字段有 size/stride,没有 `alignof`;二进制布局用途(见 F1)需要
手查 `alignof`。

**拟议形态**:`sb_refl_alignof` 加进 type 与 field 描述符(`_Alignof` 编译期
可得),宏自动填。**规模**:字段扩充 + 宏改动,约 100 行 + 全量测试重跑。
**兼容**:纯追加成员,源兼容。**优先级:P1**(小而有用,给 F1 铺路)。

### B6. 描述符 ABI 版本与自检

> 状态:**已完成**(2026-11-21,提交 66a8aeb)

**动机**:描述符跨 DSO/插件共享时,两侧头文件版本错配是无声的 UB(偏移
对不上)。

**拟议形态**:
- `sb_refl_abi`(编译期常量,宏写入)+ 注册/查询入口在 debug 构建里核对;
- `shrewd_basic_reflect_check_type(const type_t *)`:描述符合法性自检
  (offset+size 不越界、stride==size/count、flags 组合合法、引用描述符非
  NULL、索引上限)——测试与启动期一次性校验用。
**规模**:约 150 行。**优先级:P1**(低成本的运行期保险)。

---

## C. 查询与遍历增强

### C1. describe:类型自述打印

> 状态:**已完成**(2026-11-21,提交 d014507)

**现状**:dump 打印**对象**;打印**描述符本身**(类型长什么样)只能自己读
字段表。

**拟议形态**:

```c
int shrewd_basic_reflect_describe(const shrewd_basic_reflect_type_t *type);
/* 输出走 "reflect" log 模块(与 dump 同款单条记录):
   person_t (struct, 48 bytes)
     age      int32    off=0  size=4
     name     string   off=8  size=8 owned
     scores   int32[3] off=16 size=12
     home     point_t  off=28 size=8  (struct)
*/
```

**用途**:注册调试(宏注解错没错一眼看出)、工具输出、文档生成素材。
**规模**:dump 骨架复用,约 150 行。**优先级:P1**。

### C2. 布局报告(padding/holes,pahole-lite)

**拟议形态**:`describe` 的机器可读版或回调式
`shrewd_basic_reflect_layout(type, cb, ctx)` 输出:逐字段
offset/size/align、**填充字节区间**、总大小与"紧凑尺寸"的差。嵌入式排障
(结构体白胖)、序列化对齐审计的刚需。
**依赖**:B5(alignof)增强效果。**规模**:约 150 行。**优先级:P2**。

### C3. diff_all:收集全部差异

**现状**:`diff` 只报**首个**差异路径。

**拟议形态**:

```c
int shrewd_basic_reflect_diff_all(const void *a, const void *b,
                                  const shrewd_basic_reflect_type_t *type,
                                  shrewd_basic_reflect_diff_fn fn, void *ctx);
/* 回调收 (path, kind_of_difference);返回 CONTINUE/STOP */
```

**用途**:测试断言一次列全、配置比对工具。
**设计要点**:visit 骨架 + 双游标同步遍历;环处理与 diff 一致。
**规模**:约 250 行。**优先级:P2**。

### C4. 路径通配/范围选择

**拟议形态**:路径语法扩展 `items[2..5]`、`*.name`,配套批量 get/set/diff。
**难点**:语法复杂化、错误面扩大、与现有"每段一个索引"语义的兼容。
**评估**:真实需求(选择器式批量操作)未出现;visit + 应用侧过滤已覆盖
多数场景。**优先级:P3**(需求驱动,不做语法先行)。

### C5. struct 级 merge/patch

**拟议形态**:`reflect_merge(dst, src, type)`(深合并:指针目标递归合并、
标量覆盖),或对齐 JSON Merge Patch 语义的 `reflect_patch(dst, patch, type)`。
**难点**:所有权(dst 旧目标的释放边界)、深合并的语义细则(数组整替还是
逐元素)。**评估**:json 侧已有 pointer_set/decode 覆盖常见用法。
**优先级:P3**。

---

## D. 图算法改进

### D1. 环哈希规范化(消除 caveat)

**现状**:hash 对环"按形状折叠",双相似但形状不同的环(两者在 equal 下
相等)可能哈希不同——文档已注明的 caveat。

**拟议形态**:环检测时给回边分配**拓扑序编号**(DFS 发现序号),折叠值用
"指向第 k 个已访问环入口"的规范化标记,而非形状标记;两个双相似的环获得
相同折叠。**难点**:规范化的正确性论证(需要等价类证明)+ 测试构造。
**规模**:约 200 行 + 理论自检。**优先级:P2**(caveat 不影响绝大多数数据;
若 hash 进哈希表键场景再做)。

### D2. copy/destroy 的分配器注入

**现状**:copy 走 `shrewd_basic_platform_calloc`;嵌入式若要池分配,只能
整替换平台层或走 `ops->copy`。

**拟议形态**:`shrewd_basic_reflect_copy_with(const allocator_t *, ...)`,
默认参数保持现 API。**规模**:小(把分配点收拢到一个上下文)。
**优先级:P2**(平台层钩子已能覆盖大部分定制需求)。

---

## E. 并发与便利设施

### E1. 默认注册容器(= A1 的载体)

见 A1。附带文档配方:"全局注册表 = 应用 static 一个 registry"。

### E2. 查找缓存的构建期配置

> 状态:**已完成**(2026-11-21,提交 84b7ab2)

**现状**:`REFLECT_LOOKUP_SLOTS 8` / `REFLECT_LOOKUP_FIELDS 64` 是库内常量。

**拟议形态**:`-DSHREWD_BASIC_REFLECT_LOOKUP_SLOTS=n` 构建期覆盖(0 = 关闭
缓存,永远线性);`*_list` 家族的 `REFLECT_LOOKUP_FIELDS` 已公开。
**规模**:极小。**优先级:P1**。

---

## F. 应用层扩展(新模块候选,超出 reflect 本体)

### F1. 二进制序列化(struct ↔ bytes)

**动机**:协议编解码、共享内存、持久化快照——JSON 之外的第二大映射需求。

**拟议形态**(独立模块 `shrewd-basic/bin`):

```c
int shrewd_basic_bin_encode(const void *obj, const type_t *type,
                            const bin_options_t *opt, buf_t *out);
int shrewd_basic_bin_decode(const void *bytes, size_t n, void *obj,
                            const type_t *type, const bin_options_t *opt);
/* opt: 端序(LE/BE/本机)、对齐(紧凑 vs 自然)、长度前缀宽度、版本号 */
```

**设计要点**:位组/多维/union(带 tag)/FAM 全部有自然线格式;所有权契约
沿用 json 的"decode 先清零、free 递归非空"模型(登记表不需要);B5 的
alignof 与版本号(字段序指纹)是它的前置件。
**规模**:新模块 约 800 行 + 测试。**优先级:P2**(需求确认即启动,设计可
直接套 json 的三层经验)。

### F2. 通用文本格式化/解析(render/scan)

**动机**:ops->render 已有雏形(dump 用);反向 scan(文本→字段)没有。
**拟议形态**:字段级 `scan` ops + `reflect_scan(obj, type, text, fmt)`。
**优先级:P3**(展示/配置解析需求驱动)。

### F3. 其他映射(CSV/INI/YAML)

与 F1 同理,visit/diff 已把公共递归做好,每种格式只是叶子层编码。
**优先级:P3**。

---

## G. 工具链

### G1. 生成器增强:读 DWARF / 编译器 AST

**现状**:`tools/reflect_gen.py` 解析 C 源的 typedef(词法级),产出宏注解。

**拟议形态**两条升级路线:
- **DWARF 路线**:离线读 `gcc -g` 产物的 `.debug_info`,按 DIE 树产出描述符
  (优点:布局 100% 真实,含 B1 位域;缺点:要带 DWARF 解析器,或调
  `objdump --dwarf`/`llvm-dwarfdump` 做后端);
- **AST 路线**:clang libtooling 精确解析(优点:语义全;缺点:重依赖)。

**定位**:DWARF 是"生成反射元数据的好原料",运行时依旧只带描述符
(运行时读 DWARF 的五大障碍见 reflect 模块讨论,不做)。
**优先级:P2**(与 B1 联动)。

### G2. pahole 式布局工具

C2 的 CLI 化:`reflect_layout --type point_t demo` 输出填充报告;可基于
生成器(G1)或 describe(C1)。**优先级:P3**。

---

## H. 面向未来

### H1. C 反射标准化互操作

ISO C 反射研究(编译期反射 TS)在推进;C23 已确定 `_Generic`/`typeof` 等
底座。**动作**:跟踪标准进展;宏体系设计上保持"描述符是纯 const 数据"的
形态——若标准化反射落地,生成器改为产出标准反射调用或保持宏作为兼容层,
使用方式不破坏。**优先级:P3(跟踪)**。

---

## 明确不做(及原因)

| 项 | 原因 |
|---|---|
| C 位域的纯宏/纯运行时方案 | 位段布局实现定义,不可内省(走 B1 生成器路线) |
| VLA 字段 | 运行时尺寸,描述符模型是编译期元数据 |
| 运行时读 DWARF 当反射 | 段不加载/格式复杂/可剥离/定位错配/C 零成本哲学(详见模块讨论) |
| 跨编译器二进制 ABI 稳定承诺 | 描述符结构是源级 API;二进制布局仅在同工具链内保证 |
| 库内全局注册表/全局缓存 | 与"共享状态归应用"的既有决策冲突(A1 用应用侧 registry 解决) |

---

---

## 优先级总表

| 项 | 名称 | 优先级 | 规模 |
|---|---|---|---|
| A1 | 类型注册表(默认容器) | P0 | 约 200 行 |
| A2 | JSON DEFAULT 二期(struct/数组) | P0 | 中 |
| A3 | MSVC/ILP32 验证 | P0 | 一次性 |
| B5 | 描述符对齐信息 | P1 | 约 100 行 |
| B6 | ABI 版本 + check_type 自检 | P1 | 约 150 行 |
| C1 | describe 类型自述 | P1 | 约 150 行 |
| E2 | 查找缓存构建期配置 | P1 | 极小 |
| B1 | 位域(生成器+ABI) | P2 | 工具 约 400 行 |
| B2 | 匿名成员平铺 | P2 | 约 150 行 |
| B4 | 宽字符字符串 | P2 | 中 |
| C2 | 布局报告 | P2 | 约 150 行 |
| C3 | diff_all | P2 | 约 250 行 |
| D1 | 环哈希规范化 | P2 | 约 200 行 |
| D2 | 分配器注入 | P2 | 小 |
| F1 | 二进制序列化模块 | P2 | 约 800 行 |
| G1 | 生成器读 DWARF/AST | P2 | 工具 |
| B3 | long double/_Complex/_BitInt | P3 | 中 |
| C4 | 路径通配/范围 | P3 | 大 |
| C5 | struct merge/patch | P3 | 中 |
| F2/F3 | 文本格式化、CSV/INI 映射 | P3 | 中 |
| G2 | pahole 工具 | P3 | 小 |
| H1 | C 标准反射互操作 | P3 | 跟踪 |
