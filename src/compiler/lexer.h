// lexer.h —— C 词法分析器接口

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
    int     peek;          // 单字符预读
    bool    peek_valid;
    Token   cur_token;
} Lexer;

// 初始化词法分析器
int  lexer_init(Lexer *lex, const char *filename);
void lexer_close(Lexer *lex);

// 获取下一个 token，存入 lex->cur_token
Token *lexer_next(Lexer *lex);

// token 类型名称（用于错误输出）
const char *token_type_name(TokenType t);

#ifdef __cplusplus
}
#endif

#endif // COMPILER_LEXER_H
