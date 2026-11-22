# C 编码与格式规范

机器强制三件套,规范本身以本文与配置文件为准:

- `.clang-format` —— 排版;
- `.clang-tidy` —— 语义规则;
- `tools/check-style.sh` —— 闸门(三段检查)。

## 编码规则

1. **变量定义必须初始化赋初值**:标量/指针 `= 0` / `= NULL`,
   数组/聚合 `= {0}`。结构体成员不在此列。
   (`cppcoreguidelines-init-variables` + 人工/脚本覆盖数组与聚合)
2. **流程控制语句必须带完整 `{}`**,即使单语句。
   (`readability-braces-around-statements` + clang-format `InsertBraces` 双保险)
3. **多子条件各加括号**:`if`/`while`/`for` 条件含两个及以上子条件时,
   每个子条件用括号包裹:`if ((a) && (!b))`;单条件不强求
   (`if (0 == x)`)。换行时**布尔运算符置于下一行行首**。
   (`tools/check-condition-parens.py` 启发式检查)

## 排版规则(.clang-format 摘要)

| 项 | 值 |
|---|---|
| 缩进 / tab | 4 空格,禁 tab |
| 行宽 | 100 |
| 花括号 | **一律同行**(函数与控制语句均 attach) |
| 指针 | 右对齐(`void *x`) |
| 参数/实参 | 换行时逐行,不打包 |
| 续行 | 对齐开括号;布尔/二元运算符行首 |
| 尾注释 | 列对齐 |
| 宏 | 交给 clang-format(反斜杠右对齐,无 off/on 特区) |
| case | 与 switch 同列 |
| 头文件 | 不自动排序(手工分组) |

细节与例外以 `.clang-format` 为准;变更风格 = 改配置 + 全树重排一笔提交。

## 检查方式

```sh
tools/check-style.sh        # 三段:clang-format / clang-tidy / 条件括号
```

### Git 钩子(版本化)

`.githooks/` 提供两级风格闸门,克隆后一次性启用:

```sh
tools/install-hooks.sh      # git config core.hooksPath .githooks
```

- **pre-commit(快)**:对暂存的 .c/.h 做 `clang-format --dry-run` +
  条件括号检查;发现漂移即拒绝提交并给出修复命令。merge/rebase 期间
  自动跳过;
- **pre-push(全量)**:跑 `tools/check-style.sh` 整树(含 clang-tidy),
  较慢;紧急时 `git push --no-verify` 可绕过。

检查对象是工作树文件;`*.h.in` 模板与纯删除提交不参与。

`BUILD=BUILD_DIR` 指定 clang-tidy 的编译数据库目录(默认 build)。
clang-format 修复用 `clang-format -i FILE...`;clang-tidy 修复用
`clang-tidy -fix -p BUILD_DIR FILE`。
