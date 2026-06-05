/*
 * lexer.h
 * C 子集词法分析器：把源码切成 Token 流。
 */
#ifndef COMPILER_LEXER_H
#define COMPILER_LEXER_H

#include "compiler/ast.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    FILE   *file;
    char   *filename;
    int     line;
    int     col;
    int     peek;
    bool    peek_valid;
    Token   cur_token;
} Lexer;

int  lexer_init(Lexer *lex, const char *filename);
void lexer_close(Lexer *lex);
Token *lexer_next(Lexer *lex);
const char *token_type_name(TokenType t);

#ifdef __cplusplus
}
#endif

#endif
