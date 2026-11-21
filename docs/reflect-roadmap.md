# reflect 后续计划支持项

本文档是 reflect **本体**的候选功能清单与设计草案,作为后续迭代的计划
底稿。应用层需求(json 映射、二进制序列化等)另见
[reflect 应用与外部需求](reflect-apps.md)——分界线:改动描述符/查询/
图算法 = 本体;在既有能力上做值互转/线格式 = 应用。每项含:动机/场景、拟议形态(接口草图)、设计要点
与难点、依赖、工作量估计与优先级建议。优先级分四档:

- **P0 已排队**:此前明确推迟或遗留,方向已定;
- **P1 建议做**:价值明确、代价可控,适合有迭代窗口时排入;
- **P2 需求驱动**:设计可行,但应等真实需求出现再做(避免为想象的需求写代码);
- **P3 条件性**:依赖工具链/标准/新模块的成熟,远期跟踪。

与现状的关系:reflect 核心(描述符/宏/查询/读写/dump/路径/`*_list` 容器/
图算法/visit/生成器)已闭环,见 [reflect API reference](reflect-api.md) 与
[reflect overview](reflect-overview.md)。

---

## 进展综述(2026-11-22)

| 批次 | 状态 | 覆盖 |
|---|---|---|
| P0 已排队 | ✅ 全部完成 | A1 注册表、A3 MSVC/ILP32 验证 |
| P1 建议做 | ✅ 全部完成 | B5 对齐、B6 ABI+自检、C1 describe、E2 缓存配置 |
| P2 直接可实现 | ✅ 全部完成 | B2 匿名成员、C2 布局报告、C3 diff_all、D1 环哈希、D2 分配器注入 |
| P2 工具线 | ✅ 全部完成 | G1 DWARF 后端、B1 位域、G2 轻量版(--layout) |
| P2 需求驱动 | ✅ 已完成(reflect 层) | B4 宽字符 |
| P3 条件性 | ⏳ 跟踪 | B3 宽标量、C4/C5、H1 |

**验证矩阵**:native / clang / ILP32(ELF 32-bit)/ MSVC(真 cl.exe +
wine)四平台各 7 套件全绿,`tools/verify_platforms.sh` 一键复现;
`tools/reflect_gen_check.sh` 覆盖生成器双后端(lex/dwarf)、位域双向
对账与 --layout 报告。
**剩余决策点**:全部清零——reflect 本体候选均已落地或按需挂起。应用层
(json/bin 已实现;render/scan 等候选)见 [reflect-apps](reflect-apps.md)。

---

## A. 已排队(P0)

### A1. 类型注册表(名字→类型)

> 状态:**已完成**(2026-11-21,提交 c536558)

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

### A3. MSVC/ILP32 平台验证

> 状态:**已完成**(2026-11-21,提交 ea8fc8f)

**现状**:`tools/verify_platforms.sh` 已就位,但缺工具链时跳过;`_Thread_local`
在 MSVC C11 模式的行为尚未实测。

**动作**:有工具链环境时跑一轮(MSVC `/std:c11`、ILP32 交叉编译),修
`_Thread_local` 与 `__attribute__` 相关的可移植性问题,把结果记进文档。
**优先级:P0**(工程遗留,一次性)。
**实现注**:ILP32 以无 root 本地解包 multilib 全量验证(首跑抓到测试硬编码
尺寸缺陷);MSVC 经 msvc-wine(真 cl.exe + wine)构建 0 错 0 警、7/7 测试
全过,顺带修复三处 MSVC 缺陷(常量除零、未初始化、C4996)。配方固化在
`tools/verify_platforms.sh`,一条命令跑全平台。

---

## B. 类型系统扩展

### B1. C 位域(bit-field)支持——经生成器 + 编译器 ABI

> 状态:**已完成**(2026-11-22,提交 2abacef + 0adb727)

**现状**:C 位域成员**布局实现定义**(位序、跨单元、对齐均无标准),宏无法
内省;`FIELD_BITS` 覆盖"真实单元+作者给定位段"(寄存器/协议头),但 offsetof
碰不了位域的匿名存储单元。

**落地形态**:
- 新宏 `SHREWD_BASIC_REFLECT_FIELD_BITS_UNIT(struct_type, name, unit_ref,
  unit_offset, unit_size, bit_offset, bit_width)`:显式给出存储单元的
  描述符引用/偏移/宽度,值语义与 `FIELD_BITS` 完全一致(运行期零改动);
- `reflect_gen --backend dwarf` 读编译器实际布局(`DW_AT_data_bit_offset`
  + `DW_AT_bit_size`)产出该宏:单元窗口优先取声明类型的槽位,放不下则
  收缩到能覆盖位段的 1/2/4/8 字节窗口(重叠窗口的 RMW 互不干扰);
- 生成物绑定编译器/目标 ABI(输出头写明探针命令/编译器/三元组,并以
  `_Static_assert(sizeof/offsetof)` 对账防陈旧)——换工具链编译期即报错。

**测试**:test_cbit(真实 C 位域宏手写用例)+ reflect_gen_bits_smoke
(生成器产出与 C 位域双向对账:C 写→描述符读、描述符写→C 读、
RMW 保邻组、混合单元宽度)。

### B2. 匿名 struct/union 成员平铺

> 状态:**已完成**(2026-11-21,提交 e8abee7;现有宏天然支持,固化为测试+文档)

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

> 状态:**已完成(reflect 层)**(2026-11-22,提交 b2c8fd3;json/bin 的
> UTF-8 互转按需二期)

**现状**:STRING kind 语义为 `char *`(NUL 结尾 UTF-8)之外,新增
`STRING16`/`STRING32` 两个 kind 与 `shrewd_basic_reflect_string16/32`
描述符:值是指向(或本身就是)NUL 码元结尾的码元序列,equal/diff/hash
按码元比较,copy/destroy 克隆/释放 owned 内容,dump 逐码元转义
(`\uXXXX`/`\UXXXXXXXX`)。

**落地形态**:
- 显式宏族 `FIELD_STRING16/32`(借用)、`_OWNED`、`_BUFFER`,外加
  `FIELD_WSTRING` 三形态按 `WCHAR_MAX` 编译期归入 16/32——**刻意不做
  `_Generic` 自动映射**(char32_t 是 unsigned int 的别名,自动关联会
  静默吞掉整型指针,违背"不支持即编译报错"原则);
- json/bin:free 路径识别宽字符串,宽缓冲在 json 编解码处干净报
  `JSON_UNSUPPORTED`——UTF-8 互转是二期,本期保证不坏。

**测试**:test_wstring(kind/宽度分派、内容语义、NUL 截断、owned 克隆、
dump 转义、diff 路径);四平台矩阵全绿(MSVC 16 位 / ILP32·Linux 32 位
wchar_t 两侧分派均实测)。

### B5. 描述符携带对齐信息

> 状态:**已完成**(2026-11-21,提交 66a8aeb)

**现状**:字段有 size/stride,没有 `alignof`;二进制布局用途(见
[reflect-apps](reflect-apps.md) 的二进制序列化)需要手查 `alignof`。

**拟议形态**:`sb_refl_alignof` 加进 type 与 field 描述符(`_Alignof` 编译期
可得),宏自动填。**规模**:字段扩充 + 宏改动,约 100 行 + 全量测试重跑。
**兼容**:纯追加成员,源兼容。**优先级:P1**(小而有用,给二进制序列化
铺路,已随 P1 落地)。

### B6. 描述符 ABI 版本与自检

> 状态:**已完成**(2026-11-21,提交 66a8aeb)

**动机**:描述符跨 DSO/插件共享时,两侧头文件版本错配是无声的 UB(偏移
对不上)。

**拟议形态**:
- `sb_refl_abi`(编译期常量,宏写入)+ 注册/查询入口在 debug 构建里核对;
- `shrewd_basic_reflect_check_type(const type_t *)`:描述符合法性自检
  (实现注:仅显式 API,未在查询入口自动核验——启动期/测试里调用)
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

> 状态:**已完成**(2026-11-21,提交 4687408)

**拟议形态**:`describe` 的机器可读版或回调式
`shrewd_basic_reflect_layout(type, cb, ctx)` 输出:逐字段
offset/size/align、**填充字节区间**、总大小与"紧凑尺寸"的差。嵌入式排障
(结构体白胖)、序列化对齐审计的刚需。
**依赖**:B5(alignof)增强效果。**规模**:约 150 行。**优先级:P2**。

### C3. diff_all:收集全部差异

> 状态:**已完成**(2026-11-21,提交 4d4a2d7)

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

> 状态:**已完成**(2026-11-21,提交 aa932b6)

**现状**:hash 对环"按形状折叠",双相似但形状不同的环(两者在 equal 下
相等)可能哈希不同——文档已注明的 caveat。

**拟议形态**:环检测时给回边分配**拓扑序编号**(DFS 发现序号),折叠值用
"指向第 k 个已访问环入口"的规范化标记,而非形状标记;两个双相似的环获得
相同折叠。**难点**:规范化的正确性论证(需要等价类证明)+ 测试构造。
**实现注**:最终采用**迭代颜色细化**(比拓扑序方案更强且论证简单):每个
指针目标是一个图节点,固定 16 轮把节点颜色按"内容折叠+边贡献上一轮目标色"
细化——双模拟图每轮颜色相等,自环/二元环/长环/菱形同哈希;剩余 caveat 仅
深度封顶区(与 equal 封顶一致)。
**规模**:约 200 行 + 理论自检。**优先级:P2**(caveat 不影响绝大多数数据;
若 hash 进哈希表键场景再做)。

### D2. copy/destroy 的分配器注入

> 状态:**已完成**(2026-11-21,提交 2ab9dad)

**现状**:copy 走 `shrewd_basic_platform_calloc`;嵌入式若要池分配,只能
整替换平台层或走 `ops->copy`。

**拟议形态**:`shrewd_basic_reflect_copy_with(const allocator_t *, ...)`,
默认参数保持现 API。**规模**:小(把分配点收拢到一个上下文)。
**优先级:P2**(平台层钩子已能覆盖大部分定制需求)。

---

## E. 并发与便利设施

### E1. 默认注册容器(= A1 的载体)

> 状态:**已完成**(2026-11-21,提交 c536558,随 A1 交付)

见 A1。附带文档配方:"全局注册表 = 应用 static 一个 registry"。

### E2. 查找缓存的构建期配置

> 状态:**已完成**(2026-11-21,提交 84b7ab2)

**现状**:`REFLECT_LOOKUP_SLOTS 8` / `REFLECT_LOOKUP_FIELDS 64` 是库内常量。

**拟议形态**:`-DSHREWD_BASIC_REFLECT_LOOKUP_SLOTS=n` 构建期覆盖(0 = 关闭
缓存,永远线性);`*_list` 家族的 `REFLECT_LOOKUP_FIELDS` 已公开。
**规模**:极小。**优先级:P1**。

---

## F. 应用层扩展

已迁出:json 映射(已实现)与二进制序列化/文本格式化/其他映射等应用层
需求,见 [reflect 应用与外部需求](reflect-apps.md)。

## G. 工具链

### G1. 生成器增强:读 DWARF / 编译器 AST

> 状态:**已完成**(2026-11-22,提交 0adb727)

**现状**:`tools/reflect_gen.py` 双后端——`--backend lex`(词法级,零
依赖)与 `--backend dwarf`(编译探针 TU,pyelftools 读 `.debug_info`,
布局 100% 真实,含 B1 位域)。选择 pyelftools 而非 objdump 文本解析
(格式随版本漂移)或 clang libtooling(重依赖)。

**指纹防陈旧**:dwarf 生成物顶部写明探针命令/编译器/目标三元组,正文
以 `_Static_assert(sizeof/offsetof)` 对账——换工具链编译期即报错。

**定位**:DWARF 是"生成反射元数据的好原料",运行时依旧只带描述符
(运行时读 DWARF 的五大障碍见 reflect 模块讨论,不做)。
**遗留**:AST 路线(clang libtooling)不采;`reflect_gen_check.sh`
四轮自检(lex/dwarf/位域/layout)已固化。

### G2. pahole 式布局工具

> 状态:**已完成(轻量版)**(2026-11-22,随 G1 交付:`--layout`)

`reflect_gen.py INPUT --backend dwarf --layout` 输出 pahole-lite 报告:
逐字段 offset/size/align/类型拼写、位组的位区间与单元、填充区间、
packed 字节数(区间并集)。重形态(独立多二进制/diff CLI)按需求再议。

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

| 项 | 名称 | 优先级 | 规模 | 状态 |
|---|---|---|---|---|
| A1 | 类型注册表(默认容器) | P0 | 约 200 行 | ✅ c536558 |
| A3 | MSVC/ILP32 验证 | P0 | 一次性 | ✅ ea8fc8f |
| B5 | 描述符对齐信息 | P1 | 约 100 行 | ✅ 66a8aeb |
| B6 | ABI 版本 + check_type 自检 | P1 | 约 150 行 | ✅ 66a8aeb |
| C1 | describe 类型自述 | P1 | 约 150 行 | ✅ d014507 |
| E2 | 查找缓存构建期配置 | P1 | 极小 | ✅ 84b7ab2 |
| B1 | 位域(生成器+ABI) | P2 | 工具 约 400 行 | ✅ 2abacef+0adb727 |
| B2 | 匿名成员平铺 | P2 | 约 150 行 | ✅ e8abee7 |
| B4 | 宽字符字符串 | P2 | 中 | ✅ b2c8fd3(reflect 层) |
| C2 | 布局报告 | P2 | 约 150 行 | ✅ 4687408 |
| C3 | diff_all | P2 | 约 250 行 | ✅ 4d4a2d7 |
| D1 | 环哈希规范化 | P2 | 约 200 行 | ✅ aa932b6 |
| D2 | 分配器注入 | P2 | 小 | ✅ 2ab9dad |
| G1 | 生成器读 DWARF | P2 | 工具 | ✅ 0adb727 |
| B3 | long double/_Complex/_BitInt | P3 | 中 | ⏳ P3 |
| C4 | 路径通配/范围 | P3 | 大 | ⏳ P3 |
| C5 | struct merge/patch | P3 | 中 | ⏳ P3 |
| G2 | pahole 工具(轻量版) | P3 | 小 | ✅ 0adb727(--layout) |
| H1 | C 标准反射互操作 | P3 | 跟踪 | ⏳ 跟踪 |
