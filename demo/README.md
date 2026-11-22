# shrewd-basic 消费示例

两个**独立最小工程**,演示下游项目如何调用本库并在构建期跑
`tools/reflect_gen.py` 生成描述符:

| 目录 | 构建系统 | 库接入方式 |
|---|---|---|
| [consumer-cmake](consumer-cmake/) | CMake | `add_subdirectory` 引入库目标 |
| [consumer-make](consumer-make/) | Make | 库源直接编译进可执行文件 |

两者共用同一模式:

1. **头文件带注解**(`/* reflect: type=NAME */` 严格点名 + 成员意图指令);
2. **构建期跑生成器**,产物落在构建目录(源树不落生成物);
3. **应用代码** `#include` 生成的 `.h`,与手写描述符同款使用。

生成器依赖:`consumer-cmake` 只需 python3(词法后端,构建期默认);
`consumer-make` 同。`--backend dwarf` 需另装 `python3-pyelftools`,用于
位域/真实布局场景,见 [reflect roadmap](../docs/reflect-roadmap.md)。
