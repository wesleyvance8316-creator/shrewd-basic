# API 稳定性承诺(1.0)

> 版本:**1.0.0**(2026-11-23)。定位:三方开源库,面向**嵌入式
> Linux**,**不发布预编译 .so**——消费方从源码/静态库集成。因此
> 承诺档位为**源码级稳定**,不含 DSO ABI 承诺。

## 承诺(1.x 内)

| 类别 | 规则 |
|---|---|
| **公共 API** | `shrewd-basic/**/*.h` 的全部公共符号冻结:不删、不改名、不改签名、不改语义 |
| **加法** | MINOR 版本可加:新函数、新枚举值(**只追加**)、新错误码(号段规则内)、新结构体;现有符号行为只可放宽(原先报错的输入变合法) |
| **修复** | PATCH 版本:缺陷修复;若修复必须改变行为,发布说明中显著标注 |
| **破坏性变更** | 仅限 2.0:删/改符号、改签名、收紧语义、移除枚举值/错误码 |
| **结构体加字段** | 属 MINOR,**使用方需重编译**(不发 .so,无 ABI 承诺;发布说明标注 "recompile required")。既有字段布局不重排、不删 |

## 非承诺面

- `include/shrewd-basic-internal/**`:内部头,**随时可变**,不算 API;
- 宏展开的中间标识符(`var##_fields` 一类)与生成器产出物的排版;
- `shrewd-basic-warnings` 等构建辅助目标;`SHREWD_BASIC_BUILD_SHARED`
  仅供本地产物(无 SONAME 承诺);
- 测试/demo/bench 程序的输出文本。

## 机制性约定(升格为承诺)

- **kind 枚举只追加**(既有值恒定,append-only);
- **错误码按号段分配**(-200 reflect / -300 json / -400 bin /
  -500 dbus / -100 log),新码只在自家号段内追加;
- **描述符 ABI**:公共结构体加字段 = MINOR + 重编译;`sb_refl_abi`
  与运行期指纹机制照旧用于跨边界的布局核对。

## 弃用流程

1. MINOR 版本中于头文件标注 `@deprecated` 并给替代方案;
2. 至少跨一个 MINOR 后,才在下个 MAJOR 移除。

## 版本规则(SemVer)

`MAJOR. MINOR. PATCH`——MAJOR 破坏、MINOR 加法、PATCH 修复;
CMake `project(VERSION)` 与本文件同步;共享构建的 `SOVERSION`
跟 MAJOR(仅供参考,无发布物)。

## 守护

`tools/check-api-symbols.sh` 对照 `tools/api-symbols.txt` 清单:
**消失的公共符号 = 破坏性变更,门禁失败**;新增符号须同步清单
(门禁同样失败以保持清单精确)。CI 每次 push 自动核验。
