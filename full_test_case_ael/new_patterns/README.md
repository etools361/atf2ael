# _base_patterns（最小可复现模式集）

本目录用于存放“最小可复现”的 AEL 模式单元，用来驱动 **泛化优先** 的规则修复与可回归验证。

- 来源：主要同步自 `c_code/base_patterns/ael/`
- 目的：让你在不跑 317 全集的情况下，快速回归关键模板（if/loop/locals/scope 等）
- 注意：目录名以下划线开头，核心脚本默认会在扫描 317 全集时跳过该目录，避免改变“317”这个固定全集规模。

推荐跑法（只跑本目录）：

```powershell
cd c_code
powershell -ExecutionPolicy Bypass -File .\test_atf2ael_hook_compare.ps1 -Clean -AelDir ..\full_test_case_ael\_base_patterns -Threads 8
```
