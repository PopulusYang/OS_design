/*
 * ast.h
 * C 编译器抽象语法树：Token 类型、AST 节点、符号表与编译上下文。
 */
#ifndef COMPILER_AST_H
#define COMPILER_AST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOK_EOF = 0,

    TOK_INT, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_DO,
    TOK_RETURN, TOK_BREAK, TOK_CONTINUE, TOK_STRUCT, TOK_SIZEOF,

    TOK_IDENT, TOK_NUM, TOK_STRING, TOK_CHAR,

    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_NE, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_AMP, TOK_PIPE, TOK_TILDE, TOK_CARET,
    TOK_LSHIFT, TOK_RSHIFT,
    TOK_ASSIGN,
    TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN,

    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMI, TOK_COMMA, TOK_DOT,

    TOK_VOID,

} TokenType;

typedef struct {
    TokenType type;
    int       line;
    int       intval;
    char      strval[256];
} Token;

typedef enum {
    AST_NUM,
    AST_STRING,
    AST_IDENT,
    AST_UNARY,
    AST_BINARY,
    AST_ASSIGN,
    AST_CALL,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_DO_WHILE,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_BLOCK,
    AST_DECL,
    AST_FUNC,
    AST_EMPTY,
    AST_SUBSCRIPT,
} ASTKind;

typedef enum {
    BINOP_ADD, BINOP_SUB, BINOP_MUL, BINOP_DIV, BINOP_MOD,
    BINOP_EQ, BINOP_NE, BINOP_LT, BINOP_GT, BINOP_LE, BINOP_GE,
    BINOP_AND, BINOP_OR,
    BINOP_AMP, BINOP_PIPE, BINOP_XOR, BINOP_LSHIFT, BINOP_RSHIFT,
} BinOp;

typedef enum {
    UNOP_NEG, UNOP_NOT, UNOP_TILDE, UNOP_ADDR, UNOP_DEREF,
} UnOp;

struct ASTNode;
struct Symbol;

typedef struct ASTNode {
    ASTKind kind;
    int     line;

    union {
        int num_val;

        struct {
            char          *name;
            struct Symbol *sym;
        } ident;

        struct {
            char *str;
            char *label;
        } string;

        struct {
            UnOp          op;
            struct ASTNode *operand;
        } unary;

        struct {
            BinOp         op;
            struct ASTNode *left;
            struct ASTNode *right;
        } binary;

        struct {
            int           op;
            struct ASTNode *lvalue;
            struct ASTNode *rvalue;
        } assign;

        struct {
            char          *name;
            struct ASTNode **args;
            int           nargs;
        } call;

        struct {
            struct ASTNode *cond;
            struct ASTNode *then_stmt;
            struct ASTNode *else_stmt;
        } if_stmt;

        struct {
            struct ASTNode *cond;
            struct ASTNode *body;
        } while_stmt;

        struct {
            struct ASTNode *init;
            struct ASTNode *cond;
            struct ASTNode *step;
            struct ASTNode *body;
        } for_stmt;

        struct ASTNode *return_val;

        struct {
            struct ASTNode **stmts;
            int           nstmts;
        } block;

        struct {
            char          *name;
            struct ASTNode *init;
            struct Symbol *sym;
            int           is_global;
            int           array_size;
        } decl;

        struct {
            char          *name;
            char          **param_names;
            int           nparams;
            struct ASTNode *body;
            struct Symbol *sym;
        } func;

        struct {
            struct ASTNode *array;
            struct ASTNode *index;
        } subscript;
    };
} ASTNode;

typedef enum {
    SYM_GLOBAL_VAR,
    SYM_LOCAL_VAR,
    SYM_FUNC,
    SYM_PARAM,
} SymbolKind;

typedef struct Symbol {
    SymbolKind  kind;
    char        name[64];
    int         offset;
    int         size;
    int         func_nparams;
    int         array_size;
} Symbol;

typedef struct Scope {
    struct Symbol *symbols;
    int            nsymbols;
    int            capacity;
    struct Scope  *parent;
} Scope;

typedef struct Compiler {
    Scope     *global_scope;
    Scope     *current_scope;
    int        label_counter;
    int        string_counter;
    int        func_local_offset;
    int        nstrings;
    char     **string_labels;
    char     **string_values;
} Compiler;

Scope *scope_new(Scope *parent);
void   scope_free(Scope *s);
Symbol *scope_lookup(Scope *s, const char *name);
Symbol *scope_add(Scope *s, SymbolKind kind, const char *name, int offset, int size);

ASTNode *ast_new(ASTKind kind);
void     ast_free(ASTNode *n);

void     compiler_init(Compiler *c);
void     compiler_free(Compiler *c);
char    *compiler_new_label(Compiler *c);
char    *compiler_new_string_label(Compiler *c);
void     compiler_register_string(Compiler *c, const char *str, const char *label);

#ifdef __cplusplus
}
#endif

#endif
