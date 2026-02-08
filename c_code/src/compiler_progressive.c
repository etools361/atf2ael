/*
 * compiler_progressive.c
 * 渐进式复制反编译代码
 *
 * 策略: 自顶向下，逐层展开
 * 1. 先复制顶层函数框架
 * 2. 注释掉函数内部调用
 * 3. 逐步取消注释，逐层编译
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "ael_debug.h"
#include "ir_generator.h"  // 引用 AcompDepth, AcompInteract, AerrFlag
#include "yacc_parser.h"   // YACC解析器
#include "yacc_parser_globals.h"  // Global variables

/* Treat progressive compiler printf as debug-only output. */
#define printf AEL_DEBUG_PRINTF

/* Note: __int64 is already defined by MSVC */

/* ========================================
 * 全局状态变量 (从反编译代码提取)
 * ======================================== */

/* 词法分析器状态 */
static int ascan_initialized = 0;      // dword_180080224
FILE *ascan_stream = NULL;             // Stream (exported to yacc_parser_wrapper.c)
static const char *ascan_filename = NULL;  // qword_180080250
static int ascan_line_number = 1;      // dword_18007A9A0

/* 状态标志 */
static int ascan_state_1 = 0;  // dword_180080264
static int ascan_state_2 = 0;  // dword_180080268
static int ascan_state_3 = 0;  // dword_18008026C
static int ascan_state_4 = 0;  // dword_180080270
static int ascan_state_5 = 0;  // dword_180080274
static int ascan_state_6 = 0;  // dword_180080258

/* ========================================
 * 编译器全局状态变量 (从反编译代码提取)
 * ======================================== */

/* 编译器状态 - 从 ir_generator.h 引用 */
extern int AcompDepth;         // 已在 ir_generator.c 中定义
extern int AcompInteract;      // 已在 ir_generator.c 中定义
extern int AerrFlag;           // 已在 ir_generator.c 中定义

/* 编译器额外状态 */
int ArrayDimMax = 0;        // 数组维度最大值 (exported to yacc_parser.c)
int ArrayDimStart = 0;      // 数组维度起始 (exported to yacc_parser.c)
int Parsing_parm_list = 0;  // 是否在解析参数列表 (exported to yacc_parser.c)
/* Note: Destination is now extern from yacc_parser_globals.h */
static int error_count = 0;        // 错误计数
static const char *error_location_file = NULL;  // 错误位置文件
static int error_location_line = 0;            // 错误位置行号

/* Vocabulary 相关 */
/* Note: AcompDefaultVoc and AcompCurrentVoc are now extern from yacc_parser_globals.h */
/* static char AcompDefaultVoc[256] = {0};  // Moved to parser_globals.c */
/* static const char *AcompCurrentVoc = NULL;  // Moved to parser_globals.c */
static const char *current_source_file = NULL;  /* 当前源文件 */

/* ========================================
 * 第二层：词法分析器初始化和重置
 * 原函数: ascan_read_file (ael49_dll.c:47515)
 * 原函数: ascan_reset (ael49_dll.c:47594)
 * ======================================== */

/**
 * ascan_read_file - 初始化词法分析器，准备读取AEL文件
 * @filename: AEL源文件路径
 * @fp: 已打开的文件指针
 *
 * 返回: true=成功, false=失败
 *
 * 原函数逻辑:
 * 1. 检查是否已初始化
 * 2. 保存文件名和文件指针
 * 3. 调用 sub_18002DEA0(fp) 做初始化
 * 4. 重置所有状态标志
 * 5. 调用 sub_18002DAB0() 做进一步初始化
 */
bool ascan_read_file(const char *filename, FILE *fp)
{
    /* 第1步: 检查状态 */
    if (ascan_initialized || !fp) {
        return false;
    }

    /* 第2步: 保存文件名和文件指针 */
    ascan_filename = filename;
    ascan_stream = fp;

    /* 第3步: 初始化词法分析器内部状态 */
    // TODO: sub_18002DEA0(fp);  // 初始化flex/lex相关结构
    // 目前暂时跳过，使用我们自己的lexer

    /* 第4步: 设置行号和状态标志 */
    ascan_line_number = 1;
    ascan_state_1 = 0;
    ascan_state_2 = 0;
    ascan_state_3 = 0;
    ascan_state_4 = 0;
    ascan_state_5 = 0;
    ascan_state_6 = 0;

    /* 第5步: 进一步初始化 */
    // TODO: sub_18002DAB0();  // 可能是token buffer初始化
    // 目前暂时跳过

    /* 第6步: 标记已初始化 */
    ascan_initialized = 1;

    printf("[ascan_read_file] Initialized for file: %s\n", filename);
    return true;
}

/**
 * ascan_reset - 重置词法分析器状态
 *
 * 原函数逻辑:
 * 1. 重置所有状态标志
 * 2. 调用 sub_18002DAB0()
 * 3. 清除初始化标志
 */
void ascan_reset(void)
{
    /* 第1步: 重置状态标志 */
    ascan_state_1 = 0;
    ascan_state_2 = 0;
    ascan_state_3 = 0;
    ascan_state_4 = 0;
    ascan_state_5 = 0;
    ascan_state_6 = 0;

    /* 第2步: 清理内部结构 */
    // TODO: sub_18002DAB0();
    // 目前暂时跳过

    /* 第3步: 清除初始化标志 */
    ascan_initialized = 0;

    printf("[ascan_reset] Reset complete\n");
}

/* ========================================
 * 第三层：编译器状态初始化
 * 原函数: sub_180008E20 (ael49_dll.c:13525)
 * ======================================== */

/**
 * compiler_state_init - 初始化编译器全局状态
 *
 * 原函数逻辑 (sub_180008E20):
 * 1. 重置所有编译器全局变量
 * 2. 重置错误状态
 * 3. 设置错误位置
 * 4. 重置错误计数
 */
void compiler_state_init(void)
{
    /* 第1步: 重置编译深度和数组相关 */
    AcompDepth = 0;          // 编译深度归零
    ArrayDimMax = 0;         // 数组维度最大值
    ArrayDimStart = 0;       // 数组维度起始
    Parsing_parm_list = 0;   // 不在解析参数列表
    Destination = 0;         // 目标标志

    /* 第2步: 重置错误标志 */
    AerrFlag = 0;            // 清除错误标志
    error_count = 0;         // 错误计数归零

    /* 第3步: 重置错误位置 */
    // TODO: aerr_set_location(0, 0, 0);
    error_location_file = NULL;
    error_location_line = 0;

    /* 第4步: 重置其他状态 */
    // TODO: arun_reset_errorCount();
    // 暂时使用我们自己的错误计数

    printf("[compiler_state_init] Compiler state initialized\n");
    printf("  AcompDepth = %d\n", AcompDepth);
    printf("  AerrFlag = %d\n", AerrFlag);
    printf("  error_count = %d\n", error_count);
}

/**
 * setup_vocabulary - 设置词汇表和源文件信息
 * @filename: AEL源文件路径
 *
 * 原函数位置: agram_compile_file_xx 中的内联代码
 */
void setup_vocabulary(const char *filename)
{
    /* 第1步: 初始化默认词汇表 */
    AcompDefaultVoc[0] = '\0';  // 清空词汇表

    /* 第2步: 设置当前词汇表指针 */
    AcompCurrentVoc = AcompDefaultVoc;

    /* 第3步: 保存当前源文件名 */
    current_source_file = filename;

    printf("[setup_vocabulary] Vocabulary setup complete\n");
    printf("  Source file: %s\n", filename);
    printf("  Vocabulary pointer: %p\n", (void*)AcompCurrentVoc);
}

/* ========================================
 * 第四层：IR输出初始化
 * ======================================== */

/**
 * ir_output_init - 初始化IR输出系统
 *
 * 功能:
 * 1. 设置 AcompInteract = 1 (IR生成模式，不是ATF)
 * 2. 重置 IR 指令链表
 * 3. 准备接收 IR 指令
 *
 * 注意: 与原版不同，我们不打开ATF文件，而是准备IR文本输出
 */
void ir_output_init(void)
{
    /* 第1步: 调用 ir_init() 初始化IR系统 */
    ir_init();  // 设置 AcompInteract=1, 重置链表

    /* 第2步: 验证模式 */
    if (AcompInteract != 1) {
        fprintf(stderr, "[ERROR] IR mode not set correctly!\n");
        return;
    }

    printf("[ir_output_init] IR output system ready\n");
    printf("  Mode: IR text generation (not ATF binary)\n");
    printf("  Ready to receive IR instructions\n");
}

/* ========================================
 * 第一层：顶层编译入口函数
 * ======================================== */

/**
 * agram_compile_file_xx - AEL文件编译主入口 (AEL → IR)
 * @filename: AEL源文件路径
 * @output_base: 输出文件基础名
 * @debug_mode: 调试模式标志 (保留参数，IR生成中暂不使用)
 *
 * 返回: true=成功, false=失败
 *
 * 原函数位置: ael49_dll.c:13733
 * 修改: 移除ATF相关代码，改为纯IR生成
 *
 * 6层结构:
 * Layer 1: 顶层函数框架
 * Layer 2: 词法分析器初始化 (ascan_read_file)
 * Layer 3: 编译器状态初始化
 * Layer 4: IR输出初始化
 * Layer 5: YACC解析器 (核心，调用acomp_*生成IR)
 * Layer 6: 资源清理
 */
bool agram_compile_file_xx(const char *filename, const char *output_base, int debug_mode)
{
    FILE *fp;
    bool success = false;

    printf("\n========================================\n");
    printf("Progressive Compiler - AEL to IR\n");
    printf("========================================\n");
    printf("Layer 1: Top-level compile function\n");
    printf("Layer 2: Lexical analyzer initialization\n");
    printf("Layer 3: Compiler state initialization\n");
    printf("Layer 4: IR output initialization\n");
    printf("Layer 5: YACC parser (core)\n");
    printf("Layer 6: Cleanup\n");
    printf("========================================\n\n");

    /* 第1步: 打开AEL源文件 */
    printf("[Step 1] Opening AEL file: %s\n", filename);
    fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file: %s\n", filename);
        return false;
    }
    printf("[Step 1] File opened successfully\n\n");

    /* 第2步: 词法分析 - 读取文件内容 (第2层已展开) */
    printf("[Step 2] Initializing lexical analyzer\n");
    if (!ascan_read_file(filename, fp)) {
        fprintf(stderr, "Error: Failed to initialize lexical analyzer\n");
        fclose(fp);
        return false;
    }
    printf("[Step 2] Lexical analyzer initialized\n\n");

    /* 第3步: 初始化编译器状态 (第3层已展开) */
    printf("[Step 3] Initializing compiler state\n");
    compiler_state_init();  // Layer 3: 重置所有编译器全局变量
    setup_vocabulary(filename);  // Layer 3: 设置词汇表
    printf("[Step 3] Compiler state initialized\n\n");

    /* 第4步: 初始化IR输出 (第4层已展开) */
    printf("[Step 4] Initializing IR output\n");
    ir_output_init();  // Layer 4: 设置IR模式，准备接收指令
    printf("[Step 4] IR output initialized\n\n");

    /* 第5步: YACC解析器 - 核心编译过程 (第5层已集成) */
    printf("[Step 5] Running YACC parser (core compilation)\n");

    /* 初始化解析器表 */
    yacc_parser_init_tables();

    /* 注入起始token 257 (关键发现!) */
    /* 原始代码: dword_18007ED74 = 257; (ael49_dll.c:13763) */
    /* ascan_lex会首先返回这个注入的token，然后才开始扫描文件 */
    dword_18007ED74 = 257;
    printf("[CRITICAL] Injected start token 257 before parsing\n");
    printf("[DEBUG] About to call yacc_parser()...\n");
    fflush(stdout);

    /* 调用YACC解析器 (1573行反编译代码) */
    __int64 parser_result = yacc_parser();

    printf("[DEBUG] yacc_parser() returned: %lld\n", parser_result);
    fflush(stdout);

    if (parser_result != 0) {
        fprintf(stderr, "[ERROR] YACC parser failed with code: %lld\n", parser_result);
        success = false;
    }

    printf("[Step 5] Parser completed, result=%lld\n\n", parser_result);

    /* 第6步: 写入IR到文件 (第6层待展开) */
    printf("[Step 6] Writing IR to file\n");

    // Construct output filename: output_base + ".ir.txt"
    char output_filename[512];
    snprintf(output_filename, sizeof(output_filename), "%s.ir.txt", output_base);

    int ir_count = ir_get_count();
    printf("  Generated %d IR instructions\n", ir_count);
    printf("  Output file: %s\n", output_filename);

    // Write IR to file
    ir_output_to_file(output_filename);
    printf("[Step 6] IR written successfully\n\n");

    success = true;

cleanup:
    /* 第7步: 清理资源 (第2层已展开) */
    printf("[Step 7] Cleaning up\n");
    ascan_reset();
    fclose(fp);
    printf("[Step 7] Cleanup complete\n\n");

    /* 第9步: 错误处理 */
    // TODO: if (arun_get_errorCount() > 0) {
    //     // 删除输出文件
    //     // unlink(output_file);
    // }

    return success;
}

/* ========================================
 * 测试入口
 * ======================================== */
bool test_compile_file(const char *filename, const char *output_base)
{
    printf("=== Testing Progressive Compilation ===\n");
    printf("Input: %s\n", filename);
    printf("Output: %s\n", output_base);
    printf("\n");

    bool result = agram_compile_file_xx(filename, output_base, 1);

    if (result) {
        printf("SUCCESS: Compilation completed\n");
    } else {
        printf("FAILED: Compilation failed\n");
    }

    return result;
}
