/*
 * ir_generator.h
 * IR Instruction Generation Header
 */

#ifndef IR_GENERATOR_H
#define IR_GENERATOR_H

#include <stdbool.h>
#include <stdint.h>

/* Global state */
extern int AcompDepth;
extern int AcompInteract;
extern int AerrFlag;

/* IR output functions */
void ir_init(void);
void ir_output_to_file(const char *filename);
void ir_free_all(void);
int ir_get_count(void);

/* IR Generation Functions */
bool acomp_integer(int value);
bool acomp_real(double value);
bool acomp_imag(double value);
bool acomp_string(const char *str);
bool acomp_null(void);
bool acomp_bool(bool value);
bool acomp_true(void);
bool acomp_false(void);

bool acomp_add_global(void *vocab_ptr, const char *name);
bool acomp_add_local(const char *name);
bool acomp_word_ref(void *vocab_ptr, const char *name);

bool acomp_op(int16_t opcode, int16_t arg2, int16_t arg3, int arg4);

int acomp_num_local(void);
void acomp_drop_local(int count);
int ir_get_local_count(void);  /* Get current local variable count (excludes parameters) */

bool acomp_begin_funct(void *vocab_ptr, const char *name, int arg_count, int16_t func_word_id, int source_line);
bool acomp_define_funct(int local_count, int16_t func_word_id);
bool acomp_add_arg(const char *name);

int acomp_add_label(void);
bool acomp_set_label(int label_id);
bool acomp_branch_true(int label_id, int16_t line, int16_t col);

bool acomp_begin_loop(void);
bool acomp_end_loop(void);
int acomp_loop_again(void);
int acomp_loop_exit(void);
bool acomp_add_case(int case_value);
bool acomp_set_loop_default(void);
bool acomp_branch_table(int16_t line, int16_t col);

bool acomp_open_atf(const char *filename, const char *source_name, int mode);
bool acomp_close_atf(void);

#endif /* IR_GENERATOR_H */
