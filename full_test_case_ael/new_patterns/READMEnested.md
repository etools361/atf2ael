## nested_ctrl — 嵌套控制流最小基础集（utilities 风格）

目的：提供一组“最小但可扩展”的嵌套 for/while/if（含混合嵌套）AEL，用于回归验证 `ir2ael` 在深层/混合嵌套下的泛化能力。

特点：
- 统一 utilities 风格：即使单语句也强制使用 `{}`，更贴近真实库文件的 brace/label scaffolding。
- 尽量语义简单：只保留控制流骨架与最少表达式，便于用 IR 反推模板规律。
- 对比口径与主流程一致：IR compare 统一使用 `--ignore-pos --ignore-locals`。

生成方式（可复现）：
- 运行 `python c_code/pattern_lab/gen_util_nested_base_patterns.py`
  - 输出目录固定为：`full_test_case_ael/_base_patterns/nested_ctrl/`

覆盖范围：
- pure nesting：`u_nest_{for,while,if}_{2,3,4}.ael`
- mixed nesting：`u_mix_{outer}_x_{inner}_{2,3,4}.ael`（outer/inner ∈ {for,while,if} 且 outer != inner）

关联说明：
- 更深入（5/6/8 层）的探索样例放在 `c_code/pattern_lab/`，用于归纳模板规律，不强制纳入基础全集以避免体积膨胀。
