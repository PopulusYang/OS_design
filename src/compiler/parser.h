/*
 * parser.h
 * 递归下降语法分析器：Token 流转为 AST。
 */
#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H

#include "compiler/lexer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Lexer  *lexer;
    Token   cur;
    Compiler *comp;
    char    *filename;
} Parser;

int  parser_init(Parser *p, Lexer *lexer, Compiler *comp);
void parser_close(Parser *p);
ASTNode *parser_parse(Parser *p);
void parser_error(Parser *p, const char *msg);

#ifdef __cplusplus
}
#endif

#endif
