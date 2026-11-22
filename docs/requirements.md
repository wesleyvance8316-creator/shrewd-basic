# 需求汇总(shrewd-basic)

各模块需求记录与边界决策的**唯一汇编**(按模块分章)。只记
"要什么 / 不要什么 / 触发条件",过程讨论与演进史不入档;API 见各
`*-api.md`,设计见各 `*-design.md` / 模块手册。

**总原则**:不替想象的调用方写代码;需求驱动项等真实场景出现再做;
公共抽象等**第二个消费者**出现再抽(文本层、二进制骨架均如此收敛);
"绝不静默出错"优先于便利——拿不准的行为响亮拒绝。

## 1. reflect 核心

**已交付**:描述符/查找/读写/打印、路径访问、指针与 ops 回调集、
枚举、环图安全(equal/hash/copy/destroy/visit/diff)、位域
(FIELD_BITS_UNIT + dwarf)、宽字符串与宽标量(LONG_DOUBLE/COMPLEX/
BIT_INT)、ABI 自检与指纹、四平台验证矩阵。——至此收尾,无功能债。

| 边界 | 决策 |
|---|---|
| C4 路径通配/范围 | **不做**(visit+过滤已覆盖,语法只扩错误面) |
| C5 struct merge/patch | **明确不做**(所有权语义无法收敛、必返工;文档直说不支持) |
| G2 重形态(多二进制 diff) | **不做**(轻量 --layout 已覆盖九成价值) |
| H1 标准化互操作 | **零动作跟踪**(ISO C 反射 TS 演进中,纯 const 数据即最大兼容) |

## 2. 生成器(reflect_gen.py)

**已交付**:注解语法 v2(`namespace: 项, 项;` C 语句式、位置零、
严格点名、引用闭包硬错误)、宽形态识别、union 生成、json 策略桩、
`dbus:` 挂载表桩、`--schema-dump`、golden 全文对账、ctest 门禁、
CMake/Make 下游样板。——收尾,无遗留。

## 3. json

**已交付**:值模型/严格解析/序列化/结构映射/策略层/工具集
(pointer、schema 导出+校验、pull、JSONL)、选项族(clone、
skip_bom、jsonl、sort_keys)、宽串 UTF-8 互转。——功能面完整。

| 边界 | 决策 |
|---|---|
| JSON Patch / Merge Patch | **不做**(同 C5 理由;pointer_set 已覆盖) |
| 任意精度数值运算 | **不做**(number_text 原文往返已覆盖,算术是另一个库) |
| format/pattern 实际校验 | **不做**(C11 无正则;校验走 OPS->check) |

## 4. bin(二进制序列化)

**已交付**:struct ↔ bytes、端序/对齐/版本戳、自描述头、规范宽度、
全形态线格式(含宽串码元序列)。

**需求驱动**(触发即做):`ops->get/set` 参与线格式、opaque 字段的
线格式——等真实协议需要自定义编解码再定形态。

## 5. 文本层与格式族

**已交付**:公共文本层(render_text/scan_text + ops->scan,严格
往返)+ INI / env·args / CSV / TOML 1.0 子集 / msgpack / CBOR
子集;所有权契约统一 json 模式,拒绝清单响亮。

| 项 | 决策与触发条件 |
|---|---|
| **XML** | **✅ 已实现**:严格子集 + attr/element 映射(policy 行:name/as_attr/REQUIRED/HIDDEN)、DTD/实体拒绝(XXE 结构性免疫)、错误带行号 |
| **BSON** | **✅ 已实现**:BSON 规范全线小端(int32 全长回填、cstring 键、数组=document);映射同 msgpack;特有类型(ObjectId/datetime/binary…)响亮拒绝 |
| **YAML** | **不自研**:缩进文法/隐式类型/锚点与"绝不静默出错"冲突,子集必半兼容;真需求 → 引第三方库 + ops/叶子适配 |
| **protobuf/FlatBuffers/Cap'n Proto** | **不适配**:线格式按字段编号寻址、需 IDL 编译器,与按名映射是两个物种 |
| TOML 超集余项 | datetime/多行串/内联表/数组表等 22 项拒绝构造——需求触发再逐项放开 |

## 6. sb_dbus(数据总线)

**已交付**:一/二/三期全量 + 三期候选清盘——inproc 核心(路由/
钩子九宫格/访问族/通知管道)、shm 数据面(ABI 指纹、robust 锁、
跨进程唤醒、环容量可配)、批量/事务(set_many/txn,全有或全无)、
coalesce(folded 计数)、make_guard、make_policy_ops(RANGE+HIDDEN)、
json 持久化(save/load)、生成器挂载表桩、全接口 demo 与基准。

**已知限制(v1,契约)**:OWNED set 为原始拷贝(平坦值类型);shm
崩溃丢最后一次未完提交;变更环滞后跳中间变更(终值必达);set_many
仅存储型节点原子;shm 各进程须本进程 mount(值槽按路径共享)。

**剩余候选(需求触发)**:策略行 KEY/DEFAULT 映射、事务集扩容、
recv 批量投递形态;C1(XML)/C2(BSON)/C3(bin ops)按需求触发。

## 7. 工程配套(持续义务)

风格门(clang-format/clang-tidy/条件括号)、pre-commit/pre-push 钩子、
ctest 全量、ASan/UBSan 复核、Linux 验证矩阵(gcc/clang + ILP32)、
demo 与测试同交付、文档与代码同提交。

**平台政策(2026-11-22)**:**暂停 Windows 生态支持,专注嵌入式
Linux**——MSVC/wine 验证停跑(WIN32 裁剪守卫保留在构建里但不再
验证);shm/pthread 等 POSIX 面由此无条件成立。恢复 Windows 时
须重跑 MSVC 矩阵并复核 WIN32 分支。
