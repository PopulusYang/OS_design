// lexer.c —— C 词法分析器实现
//
// 将 C 源码分解为 Token 流。支持：
//   - 关键字：int, void, if, else, while, for, do, return, break, continue, struct, sizeof
//   - 运算符：+ - * / % == != < > <= >= && || ! & | ^ ~ << >> = += -= *= /=
//   - 分隔符：( ) { } [ ] , ; .
//   - 字面量：十进制/十六进制整数，字符常量 'x'，字符串 "..."
//   - 注释：// 行注释 和 /* ... */ 块注释

#include "compiler/lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ---------- 关键字表 ----------

static const struct {
    const char *name;
    TokenType   type;
} KEYWORDS[] = {
    {"int",      TOK_INT},
    {"void",     TOK_VOID},
    {"if",       TOK_IF},
    {"else",     TOK_ELSE},
    {"while",    TOK_WHILE},
    {"for",      TOK_FOR},
    {"do",       TOK_DO},
    {"return",   TOK_RETURN},
    {"break",    TOK_BREAK},
    {"continue", TOK_CONTINUE},
    {"struct",   TOK_STRUCT},
    {"sizeof",   TOK_SIZEOF},
};

static TokenType lookup_keyword(const char *s) {
    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++)
        if (strcmp(KEYWORDS[i].name, s) == 0)
            return KEYWORDS[i].type;
    return TOK_IDENT;
}

// ---------- 内部辅助 ----------

static int lexer_getch(Lexer *lex) {
    if (lex->peek_valid) {
        lex->peek_valid = false;
        return lex->peek;
    }
    int c = fgetc(lex->file);
    if (c == '\n') lex->line++;
    return c;
}

static void lexer_ungetch(Lexer *lex, int c) {
    lex->peek = c;
    lex->peek_valid = true;
}

static void lexer_error(Lexer *lex, const char *msg) {
    fprintf(stderr, "\033[1;31mlexer error\033[0m at %s:%d: %s\n",
            lex->filename ? lex->filename : "<stdin>", lex->line, msg);
}

// ---------- 公共接口 ----------

int lexer_init(Lexer *lex, const char *filename) {
    memset(lex, 0, sizeof(*lex));
    lex->file = fopen(filename, "r");
    if (!lex->file) {
        fprintf(stderr, "lexer: cannot open '%s'\n", filename);
        return -1;
    }
    lex->filename = strdup(filename);
    lex->line = 1;
    lex->col = 0;
    return 0;
}

void lexer_close(Lexer *lex) {
    if (lex->file) fclose(lex->file);
    free(lex->filename);
    lex->file = NULL;
    lex->filename = NULL;
}

Token *lexer_next(Lexer *lex) {
    Token *t = &lex->cur_token;
    memset(t, 0, sizeof(*t));
    t->line = lex->line;

    int c;
    // 跳过空白
    while (1) {
        c = lexer_getch(lex);
        if (c == EOF) { t->type = TOK_EOF; return t; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        break;
    }

    // 注释处理
    if (c == '/') {
        int c2 = lexer_getch(lex);
        if (c2 == '/') {
            // 行注释
            while (1) {
                c = lexer_getch(lex);
                if (c == EOF || c == '\n') { lexer_ungetch(lex, c); return lexer_next(lex); }
            }
        } else if (c2 == '*') {
            // 块注释
            int prev = 0;
            while (1) {
                c = lexer_getch(lex);
                if (c == EOF) { lexer_error(lex, "unterminated block comment"); t->type = TOK_EOF; return t; }
                if (prev == '*' && c == '/') break;
                prev = c;
            }
            return lexer_next(lex);
        } else {
            lexer_ungetch(lex, c2);
            t->type = TOK_SLASH;
            return t;
        }
    }

    // 标识符 / 关键字
    if (isalpha(c) || c == '_') {
        int i = 0;
        t->strval[i++] = (char)c;
        while (1) {
            c = lexer_getch(lex);
            if (isalnum(c) || c == '_') {
                if (i < (int)sizeof(t->strval) - 1) t->strval[i++] = (char)c;
            } else {
                lexer_ungetch(lex, c);
                break;
            }
        }
        t->strval[i] = '\0';
        t->type = lookup_keyword(t->strval);
        return t;
    }

    // 数字
    if (isdigit(c)) {
        int val = 0;
        if (c == '0') {
            int c2 = lexer_getch(lex);
            if (c2 == 'x' || c2 == 'X') {
                // 十六进制
                while (1) {
                    c2 = lexer_getch(lex);
                    int digit = 0;
                    if (isdigit(c2)) digit = c2 - '0';
                    else if (c2 >= 'a' && c2 <= 'f') digit = c2 - 'a' + 10;
                    else if (c2 >= 'A' && c2 <= 'F') digit = c2 - 'A' + 10;
                    else { lexer_ungetch(lex, c2); break; }
                    val = val * 16 + digit;
                }
                t->type = TOK_NUM; t->intval = val; return t;
            } else {
                lexer_ungetch(lex, c2);
            }
        }
        // 十进制
        val = c - '0';
        while (1) {
            c = lexer_getch(lex);
            if (isdigit(c)) val = val * 10 + (c - '0');
            else { lexer_ungetch(lex, c); break; }
        }
        t->type = TOK_NUM; t->intval = val;
        return t;
    }

    // 字符常量 'x'
    if (c == '\'') {
        int ch = lexer_getch(lex);
        if (ch == '\\') {
            ch = lexer_getch(lex);
            switch (ch) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case 'r': ch = '\r'; break;
                case '0': ch = '\0'; break;
                case '\\': ch = '\\'; break;
                case '\'': ch = '\''; break;
            }
        }
        int close = lexer_getch(lex);
        if (close != '\'') lexer_error(lex, "unterminated char constant");
        t->type = TOK_CHAR; t->intval = ch;
        return t;
    }

    // 字符串常量 "..."
    if (c == '"') {
        int i = 0;
        while (1) {
            c = lexer_getch(lex);
            if (c == EOF) { lexer_error(lex, "unterminated string"); break; }
            if (c == '"') break;
            if (c == '\\') {
                c = lexer_getch(lex);
                switch (c) {
                    case 'n':  c = '\n'; break;
                    case 't':  c = '\t'; break;
                    case 'r':  c = '\r'; break;
                    case '0':  c = '\0'; break;
                    case '\\': c = '\\'; break;
                    case '"':  c = '"';  break;
                }
            }
            if (i < (int)sizeof(t->strval) - 1) t->strval[i++] = (char)c;
        }
        t->strval[i] = '\0';
        t->type = TOK_STRING;
        return t;
    }

    // 单字符 / 双字符运算符
    switch (c) {
        case '+': {
            int c2 = lexer_getch(lex);
            if (c2 == '=') { t->type = TOK_PLUS_ASSIGN; }
            else if (c2 == '+') { /* skip ++ for now */ t->type = TOK_PLUS; lexer_ungetch(lex, '+'); }
            else { lexer_ungetch(lex, c2); t->type = TOK_PLUS; }
            return t;
        }
        case '-': {
            int c2 = lexer_getch(lex);
            if (c2 == '>') { /* arrow operator -> not supported */ }
            if (c2 == '=') { t->type = TOK_MINUS_ASSIGN; }
            else { lexer_ungetch(lex, c2); t->type = TOK_MINUS; }
            return t;
        }
        case '*': {
            int c2 = lexer_getch(lex);
            if (c2 == '=') { t->type = TOK_STAR_ASSIGN; }
            else { lexer_ungetch(lex, c2); t->type = TOK_STAR; }
            return t;
        }
        case '/': {
            int c2 = lexer_getch(lex);
            if (c2 == '=') { t->type = TOK_SLASH_ASSIGN; }
            else { lexer_ungetch(lex, c2); t->type = TOK_SLASH; }
            return t;
        }
        case '%': t->type = TOK_PERCENT; return t;
        case '=': {
            int c2 = lexer_getch(lex);
            if (c2 == '=') { t->type = TOK_EQ; }
            else { lexer_ungetch(lex, c2); t->type = TOK_ASSIGN; }
            return t;
        }
        case '!': {
            int c2 = lexer_getch(lex);
            if (c2 == '=') { t->type = TOK_NE; }
            else { lexer_ungetch(lex, c2); t->type = TOK_NOT; }
            return t;
        }
        case '<': {
            int c2 = lexer_getch(lex);
            if (c2 == '=') { t->type = TOK_LE; }
            else if (c2 == '<') { t->type = TOK_LSHIFT; }
            else { lexer_ungetch(lex, c2); t->type = TOK_LT; }
            return t;
        }
        case '>': {
            int c2 = lexer_getch(lex);
            if (c2 == '=') { t->type = TOK_GE; }
            else if (c2 == '>') { t->type = TOK_RSHIFT; }
            else { lexer_ungetch(lex, c2); t->type = TOK_GT; }
            return t;
        }
        case '&': {
            int c2 = lexer_getch(lex);
            if (c2 == '&') { t->type = TOK_AND; }
            else { lexer_ungetch(lex, c2); t->type = TOK_AMP; }
            return t;
        }
        case '|': {
            int c2 = lexer_getch(lex);
            if (c2 == '|') { t->type = TOK_OR; }
            else { lexer_ungetch(lex, c2); t->type = TOK_PIPE; }
            return t;
        }
        case '^': t->type = TOK_CARET; return t;
        case '~': t->type = TOK_TILDE; return t;
        case '(': t->type = TOK_LPAREN; return t;
        case ')': t->type = TOK_RPAREN; return t;
        case '{': t->type = TOK_LBRACE; return t;
        case '}': t->type = TOK_RBRACE; return t;
        case '[': t->type = TOK_LBRACKET; return t;
        case ']': t->type = TOK_RBRACKET; return t;
        case ';': t->type = TOK_SEMI; return t;
        case ',': t->type = TOK_COMMA; return t;
        case '.': t->type = TOK_DOT; return t;
        default:
            lexer_error(lex, "unexpected character");
            t->type = TOK_EOF;
            return t;
    }
}

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOK_INT: return "int";
        case TOK_VOID: return "void";
        case TOK_IF: return "if";
        case TOK_ELSE: return "else";
        case TOK_WHILE: return "while";
        case TOK_FOR: return "for";
        case TOK_DO: return "do";
        case TOK_RETURN: return "return";
        case TOK_BREAK: return "break";
        case TOK_CONTINUE: return "continue";
        case TOK_STRUCT: return "struct";
        case TOK_SIZEOF: return "sizeof";
        case TOK_IDENT: return "identifier";
        case TOK_NUM: return "number";
        case TOK_STRING: return "string";
        case TOK_CHAR: return "char";
        case TOK_PLUS: return "'+'";
        case TOK_MINUS: return "'-'";
        case TOK_STAR: return "'*'";
        case TOK_SLASH: return "'/'";
        case TOK_PERCENT: return "'%'";
        case TOK_EQ: return "'=='";
        case TOK_NE: return "'!='";
        case TOK_LT: return "'<'";
        case TOK_GT: return "'>'";
        case TOK_LE: return "'<='";
        case TOK_GE: return "'>='";
        case TOK_AND: return "'&&'";
        case TOK_OR: return "'||'";
        case TOK_NOT: return "'!'";
        case TOK_AMP: return "'&'";
        case TOK_PIPE: return "'|'";
        case TOK_TILDE: return "'~'";
        case TOK_CARET: return "'^'";
        case TOK_LSHIFT: return "'<<'";
        case TOK_RSHIFT: return "'>>'";
        case TOK_ASSIGN: return "'='";
        case TOK_LPAREN: return "'('";
        case TOK_RPAREN: return "')'";
        case TOK_LBRACE: return "'{'";
        case TOK_RBRACE: return "'}'";
        case TOK_LBRACKET: return "'['";
        case TOK_RBRACKET: return "']'";
        case TOK_SEMI: return "';'";
        case TOK_COMMA: return "','";
        case TOK_DOT: return "'.'";
        case TOK_EOF: return "EOF";
        default: return "unknown";
    }
}
