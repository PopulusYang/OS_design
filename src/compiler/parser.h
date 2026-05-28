// parser.h —— C 递归下降语法分析器接口

#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H

#include "compiler/lexer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Lexer  *lexer;
    Token   cur;        // 当前 token
    Compiler *comp;     // 编译器上下文（符号表）
    char    *filename;  // 用于错误报告
} Parser;

// 初始化语法分析器
int  parser_init(Parser *p, Lexer *lexer, Compiler *comp);
void parser_close(Parser *p);

// 解析整个翻译单元，返回 AST 根节点（AST_BLOCK）
ASTNode *parser_parse(Parser *p);

// 错误报告
void parser_error(Parser *p, const char *msg);

#ifdef __cplusplus
}
#endif

#endif // COMPILER_PARSER_H
