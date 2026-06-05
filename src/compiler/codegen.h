/*
 * codegen.h
 * 把 AST 翻译成 UPFS 虚拟机汇编（.s 文件）。
 */
#ifndef COMPILER_CODEGEN_H
#define COMPILER_CODEGEN_H

#include "compiler/ast.h"
#include "compiler/regalloc.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Compiler *comp;
    FILE     *out;
    RegAlloc  ra;
    int       tmp_counter;
    char      cur_func[64];
    char      *break_label;
    char      *continue_label;
    int       local_var_size;
    int       saved_regs_mask;
} CodeGen;

int  codegen_init(CodeGen *cg, Compiler *comp, FILE *out);
void codegen_close(CodeGen *cg);
void codegen_emit(CodeGen *cg, ASTNode *root);

#ifdef __cplusplus
}
#endif

#endif
