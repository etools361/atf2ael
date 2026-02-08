# ATF2AEL（纯 C ATF → AEL 转换器）

本项目提供C的 ATF→AEL 转换器，用于把 ADS 产生的 ATF 字节码还原为可编译的 AEL。

## Status

| Item | Value |
| --- | --- |
| Platform | Windows |
| Language | C |
| Latest full-set conversion | full_test_case_ael, new_patterns included |

## 项目梗概

- **目标**：将 ATF（AEL 编译后的字节码）转换为可编译的 AEL。
- **输入/输出**：输入 `.atf`，输出 `.ael`。
- **测试用例**：`full_test_case_ael/` 为全量用例，`full_test_case_ael/new_patterns/` 为最小模式集。

## English Summary

ATF2AEL is a pure C converter that turns ADS ATF bytecode into compilable AEL. The repository includes a full AEL test set plus a minimal pattern subset under `full_test_case_ael`.

## 主要产物

- `c_code/build/atf2ael.exe`：单文件 ATF→AEL 转换器

## 目录结构要点

- `c_code/`：源码与构建入口
- `c_code/build/`：可执行文件输出目录
- `full_test_case_ael/`：AEL 测试用例
- `full_test_case_ael/new_patterns/`：最小模式集

## 快速开始

### 1) 构建

```powershell
cd c_code
.\build.bat
```

### 2) 单文件转换

```powershell
c_code/build/atf2ael.exe -In in.atf -Out out.ael
```

## atf2ael.exe 使用说明

```powershell
atf2ael.exe -In <file.atf> -Out <file.ael> [-StrictPos 0|1] [-AllowScopeBlocks 0|1]
```

参数说明：

- `-In`：输入 ATF 文件路径
- `-Out`：输出 AEL 文件路径
- `-StrictPos`：是否严格使用位置记录（默认 0，推荐 0）
- `-AllowScopeBlocks`：是否启用匿名作用域块的重建（默认 0；开启后可能改变花括号结构）

帮助：

```powershell
atf2ael.exe -h
```

## 常见说明

- ATF 为编译产物，需由 ADS 或其它流程生成。
- 转换输出以“可编译、可用”为目标，遇到缺失信息会尽量做最佳恢复。

## 获取 ATF 输入（简要）

ATF 通常由 ADS 或既有编译流程产出。你可以从现有工程的构建产物中收集 `.atf` 文件，然后用本工具批量或单文件转换为 `.ael`。

## 维护与扩展

- 入口：`c_code/atf2ael_main.c`
- 解析/转换实现：`c_code/src/` 下的模块
- 新增测试用例：放入 `full_test_case_ael/`，最小模式同步到 `full_test_case_ael/new_patterns/`

## 依赖环境

- Windows PowerShell
- MSVC x64
