@echo off
REM ========================================
REM Build New Parser - Direct MSVC Compilation
REM ========================================

setlocal enabledelayedexpansion

set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

echo [BUILD] ========================================
echo [BUILD] Building New AEL Parser
echo [BUILD] ========================================
echo [BUILD] Project: %PROJECT_DIR%
echo.

REM Create build directory
if not exist build mkdir build
echo [BUILD] Build directory ready

REM Clean old build artifacts
del /q build\*.exe build\*.obj build\*.pdb 2>nul

REM Setup Visual Studio environment
echo [BUILD] Setting up MSVC environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to setup MSVC environment
    echo [ERROR] Please ensure Visual Studio 2022 is installed
    exit /b 1
)
echo [BUILD] MSVC environment ready
echo.

REM List source files
echo [BUILD] Compiling new parser sources...
echo [BUILD]   - main.c
echo [BUILD]   - ir2ael_main.c
echo [BUILD]   - src/ael_parser_new.c
echo [BUILD]   - src/ael_parser_statements.c
echo [BUILD]   - src/ael_parser_functions.c
echo [BUILD]   - src/yacc_parser_tables.c
echo [BUILD]   - src/ascan_lex.c
echo [BUILD]   - src/ir_generator.c
echo [BUILD]   - src/ir_text_parser.c
echo [BUILD]   - src/opcode_metadata.c
echo [BUILD]   - src/token_to_subopcode.c
echo [BUILD]   - src/output.c
echo [BUILD]   - src/compiler_progressive.c
echo.

REM Compile ael2ir
cl.exe /nologo /W3 /O2 ^
    /I"include" ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /Fo:build\ ^
    /Fd:build\ ^
    /Fe:build\ael2ir.exe ^
    main.c ^
    src/ael_parser_new.c ^
    src/ael_parser_statements.c ^
    src/ael_parser_functions.c ^
    src/yacc_parser_tables.c ^
    src/parser_globals.c ^
    src/ascan_lex_minimal.c ^
    src/lexer_state.c ^
    src/ir_generator.c ^
    src/opcode_metadata.c ^
    src/token_to_subopcode.c ^
    src/output.c ^
    src/compiler_progressive.c

set COMPILE_EXIT=%ERRORLEVEL%

REM Compile ir2ael (bootstrap tool)
if %COMPILE_EXIT% EQU 0 (
    cl.exe /nologo /W3 /O2 ^
        /I"include" ^
        /D_CRT_SECURE_NO_WARNINGS ^
        /Fo:build\ ^
        /Fd:build\ ^
        /Fe:build\ir2ael.exe ^
        ir2ael_main.c ^
        src/ir_text_parser.c ^
        src/ael_emit.c ^
        src/ir2ael_helpers.c ^
        src/ir2ael_convert_state.c ^
        src/ir2ael_convert_decl.c ^
        src/ir2ael_convert_scope.c ^
        src/ir2ael_convert_load.c ^
        src/ir2ael_convert_flow.c ^
        src/ir2ael_convert_flow_switch.c ^
        src/ir2ael_convert_flow_loop.c ^
        src/ir2ael_convert_flow_end.c ^
        src/ir2ael_convert_flow_loop_ctl.c ^
        src/ir2ael_convert_flow_labels.c ^
        src/ir2ael_convert_flow_branch.c ^
        src/ir2ael_convert_flow_load_true.c ^
        src/ir2ael_convert_expr.c ^
        src/ir2ael_convert_expr_assign.c ^
        src/ir2ael_convert_expr_call.c ^
        src/ir2ael_convert_expr_ops.c ^
        src/ir2ael_convert_finalize.c ^
        src/ir2ael_convert.c
    set COMPILE_EXIT=%ERRORLEVEL%
)

REM Compile atf2ir (ATF -> IR converter)
if %COMPILE_EXIT% EQU 0 (
    cl.exe /nologo /W3 /O2 ^
        /I"..\..\atf2ir_c_code\include" ^
        /D_CRT_SECURE_NO_WARNINGS ^
        /Fo:build\ ^
        /Fd:build\ ^
        /Fe:build\atf2ir.exe ^
        ..\..\atf2ir_c_code\src\main.c ^
        ..\..\atf2ir_c_code\src\atf_reader.c ^
        ..\..\atf2ir_c_code\src\type_parser.c ^
        ..\..\atf2ir_c_code\src\context_manager.c ^
        ..\..\atf2ir_c_code\src\ir_writer.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_items.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_postpass.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_pass_switch.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_pass_locals.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_pass_loops.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_pass_utils.c ^
        ..\..\atf2ir_c_code\src\decoders\decoder_registry.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type3_vocab_decl.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type4_var_decl.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type5_var_ref.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type6_integer.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type7_real.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type8_complex.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type9_string.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type10_null.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type12_func_begin.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type13_marker.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type14_local_decl.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_var_ref_common.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type17_depth.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type18_operator.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type19_label_decl.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type20_label_mark.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type21_branch.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type22_func_def.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type23_drop_local.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type24_switch_table.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type25_char.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type26_bool.c
    set COMPILE_EXIT=%ERRORLEVEL%
)

REM Compile atf2ael (ATF -> IR -> AEL converter)
if %COMPILE_EXIT% EQU 0 (
    cl.exe /nologo /W3 /O2 ^
        /I"include" ^
        /I"..\..\atf2ir_c_code\include" ^
        /D_CRT_SECURE_NO_WARNINGS ^
        /Fo:build\ ^
        /Fd:build\ ^
        /Fe:build\atf2ael.exe ^
        atf2ael_main.c ^
        src\ir_text_parser.c ^
        src\ael_emit.c ^
        src\ir2ael_helpers.c ^
        src\ir2ael_convert_state.c ^
        src\ir2ael_convert_decl.c ^
        src\ir2ael_convert_scope.c ^
        src\ir2ael_convert_load.c ^
        src\ir2ael_convert_flow.c ^
        src\ir2ael_convert_flow_switch.c ^
        src\ir2ael_convert_flow_loop.c ^
        src\ir2ael_convert_flow_end.c ^
        src\ir2ael_convert_flow_loop_ctl.c ^
        src\ir2ael_convert_flow_labels.c ^
        src\ir2ael_convert_flow_branch.c ^
        src\ir2ael_convert_flow_load_true.c ^
        src\ir2ael_convert_expr.c ^
        src\ir2ael_convert_expr_assign.c ^
        src\ir2ael_convert_expr_call.c ^
        src\ir2ael_convert_expr_ops.c ^
        src\ir2ael_convert_finalize.c ^
        src\ir2ael_convert.c ^
        ..\..\atf2ir_c_code\src\atf_reader.c ^
        ..\..\atf2ir_c_code\src\type_parser.c ^
        ..\..\atf2ir_c_code\src\context_manager.c ^
        ..\..\atf2ir_c_code\src\ir_writer.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_items.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_postpass.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_pass_switch.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_pass_locals.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_pass_loops.c ^
        ..\..\atf2ir_c_code\src\atf_to_ir_pass_utils.c ^
        ..\..\atf2ir_c_code\src\decoders\decoder_registry.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type3_vocab_decl.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type4_var_decl.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type5_var_ref.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type6_integer.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type7_real.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type8_complex.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type9_string.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type10_null.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type12_func_begin.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type13_marker.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type14_local_decl.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_var_ref_common.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type17_depth.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type18_operator.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type19_label_decl.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type20_label_mark.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type21_branch.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type22_func_def.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type23_drop_local.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type24_switch_table.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type25_char.c ^
        ..\..\atf2ir_c_code\src\decoders\decode_type26_bool.c
    set COMPILE_EXIT=%ERRORLEVEL%
)

REM Check result
echo.
if %COMPILE_EXIT% EQU 0 (
    echo [BUILD] ========================================
    echo [BUILD] SUCCESS!
    echo [BUILD] ========================================
    REM Keep repository clean: remove intermediate build artifacts (keep only .exe)
    del /q build\*.obj build\*.pdb build\*.ilk 2>nul
    if exist build\ael2ir.exe (
        echo [BUILD] Output: build\ael2ir.exe
        dir build\ael2ir.exe | findstr "ael2ir"
    ) else (
        echo [WARNING] Compilation succeeded but executable not found
    )
    if exist build\ir2ael.exe (
        echo [BUILD] Output: build\ir2ael.exe
        dir build\ir2ael.exe | findstr "ir2ael"
    ) else (
        echo [WARNING] ir2ael compilation succeeded but executable not found
    )
    if exist build\atf2ir.exe (
        echo [BUILD] Output: build\atf2ir.exe
        dir build\atf2ir.exe | findstr "atf2ir"
    ) else (
        echo [WARNING] atf2ir compilation succeeded but executable not found
    )
    if exist build\atf2ael.exe (
        echo [BUILD] Output: build\atf2ael.exe
        dir build\atf2ael.exe | findstr "atf2ael"
    ) else (
        echo [WARNING] atf2ael compilation succeeded but executable not found
    )
) else (
    echo [BUILD] ========================================
    echo [BUILD] FAILED with exit code %COMPILE_EXIT%
    echo [BUILD] ========================================
)

echo.
exit /b %COMPILE_EXIT%
