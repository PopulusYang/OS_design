// codegen.h —— 代码生成器接口

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
    FILE     *out;          // 输出 .s 文件
    RegAlloc  ra;
    int       tmp_counter;  // 临时变量计数器
    char      cur_func[64]; // 当前函数名
    char      *break_label; // 当前循环的 break label
    char      *continue_label; // 当前循环的 continue label
    int       local_var_size;  // 当前函数局部变量总字节数
    int       saved_regs_mask;   // 需要保存的 callee-saved 寄存器位图
} CodeGen;

// 初始化代码生成器
int  codegen_init(CodeGen *cg, Compiler *comp, FILE *out);
void codegen_close(CodeGen *cg);

// 从 AST 生成汇编
void codegen_emit(CodeGen *cg, ASTNode *root);

#ifdef __cplusplus
}
#endif

#endif // COMPILER_CODEGEN_H
