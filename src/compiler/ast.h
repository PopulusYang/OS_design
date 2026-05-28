// ast.h —— C 编译器 AST 节点定义 + 符号表
//
// 所有 AST 节点使用 tagged union 结构，
// 符号表维护变量/函数/参数的作用域信息。

#ifndef COMPILER_AST_H
#define COMPILER_AST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Token 类型 ----------

typedef enum {
    TOK_EOF = 0,

    // 关键字
    TOK_INT, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_DO,
    TOK_RETURN, TOK_BREAK, TOK_CONTINUE, TOK_STRUCT, TOK_SIZEOF,

    // 标识符 & 字面量
    TOK_IDENT, TOK_NUM, TOK_STRING, TOK_CHAR,

    // 运算符
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_NE, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_AMP, TOK_PIPE, TOK_TILDE, TOK_CARET,
    TOK_LSHIFT, TOK_RSHIFT,
    TOK_ASSIGN,
    TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN,

    // 分隔符
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMI, TOK_COMMA, TOK_DOT,

    // 类型
    TOK_VOID,

} TokenType;

typedef struct {
    TokenType type;
    int       line;       // 源码行号
    int       intval;     // TOK_NUM 的值 / TOK_CHAR 的值
    char      strval[256]; // TOK_IDENT / TOK_STRING 的值
} Token;

// ---------- AST 节点类型 ----------

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

// 二元运算符
typedef enum {
    BINOP_ADD, BINOP_SUB, BINOP_MUL, BINOP_DIV, BINOP_MOD,
    BINOP_EQ, BINOP_NE, BINOP_LT, BINOP_GT, BINOP_LE, BINOP_GE,
    BINOP_AND, BINOP_OR,
    BINOP_AMP, BINOP_PIPE, BINOP_XOR, BINOP_LSHIFT, BINOP_RSHIFT,
} BinOp;

// 一元运算符
typedef enum {
    UNOP_NEG, UNOP_NOT, UNOP_TILDE, UNOP_ADDR, UNOP_DEREF,
} UnOp;

// 前向声明
struct ASTNode;
struct Symbol;

typedef struct ASTNode {
    ASTKind kind;
    int     line;

    union {
        // AST_NUM
        int num_val;

        // AST_IDENT
        struct {
            char          *name;
            struct Symbol *sym;
        } ident;

        // AST_STRING
        struct {
            char *str;
            char *label;   // .data 段 label
        } string;

        // AST_UNARY
        struct {
            UnOp          op;
            struct ASTNode *operand;
        } unary;

        // AST_BINARY
        struct {
            BinOp         op;
            struct ASTNode *left;
            struct ASTNode *right;
        } binary;

        // AST_ASSIGN
        struct {
            int           op;            // TOK_ASSIGN, TOK_PLUS_ASSIGN 等
            struct ASTNode *lvalue;
            struct ASTNode *rvalue;
        } assign;

        // AST_CALL
        struct {
            char          *name;
            struct ASTNode **args;
            int           nargs;
        } call;

        // AST_IF
        struct {
            struct ASTNode *cond;
            struct ASTNode *then_stmt;
            struct ASTNode *else_stmt;   // NULL = 无 else
        } if_stmt;

        // AST_WHILE / AST_DO_WHILE
        struct {
            struct ASTNode *cond;
            struct ASTNode *body;
        } while_stmt;

        // AST_FOR
        struct {
            struct ASTNode *init;
            struct ASTNode *cond;
            struct ASTNode *step;
            struct ASTNode *body;
        } for_stmt;

        // AST_RETURN
        struct ASTNode *return_val;

        // AST_BLOCK
        struct {
            struct ASTNode **stmts;
            int           nstmts;
        } block;

        // AST_DECL
        struct {
            char          *name;
            struct ASTNode *init;        // NULL = 无初始化
            struct Symbol *sym;
            int           is_global;
            int           array_size;    // >0 表示数组大小
        } decl;

        // AST_FUNC
        struct {
            char          *name;
            char          **param_names;
            int           nparams;
            struct ASTNode *body;        // NULL = 前向声明
            struct Symbol *sym;
        } func;

        // AST_SUBSCRIPT
        struct {
            struct ASTNode *array;
            struct ASTNode *index;
        } subscript;
    };
} ASTNode;

// ---------- 符号表 ----------

typedef enum {
    SYM_GLOBAL_VAR,
    SYM_LOCAL_VAR,
    SYM_FUNC,
    SYM_PARAM,
} SymbolKind;

typedef struct Symbol {
    SymbolKind  kind;
    char        name[64];
    int         offset;      // 局部变量: 栈偏移; 全局变量: .data 偏移
    int         size;        // 字节大小（int=4, 数组=4*n）
    int         func_nparams; // 函数参数数
    int         array_size;  // 数组大小（>0 表示数组）
} Symbol;

// 符号表作用域
typedef struct Scope {
    struct Symbol *symbols;
    int            nsymbols;
    int            capacity;
    struct Scope  *parent;
} Scope;

// 全局编译器上下文
typedef struct Compiler {
    Scope     *global_scope;
    Scope     *current_scope;
    int        label_counter;      // .L0, .L1, ...
    int        string_counter;     // .L_str_0, .L_str_1, ...
    int        func_local_offset;  // 当前函数局部变量栈偏移
    int        nstrings;           // 字符串字面量数量
    char     **string_labels;     // 字符串 label 数组
    char     **string_values;     // 字符串值数组
} Compiler;

// ---------- 符号表操作 ----------

Scope *scope_new(Scope *parent);
void   scope_free(Scope *s);
Symbol *scope_lookup(Scope *s, const char *name);
Symbol *scope_add(Scope *s, SymbolKind kind, const char *name, int offset, int size);

// ---------- AST 操作 ----------

ASTNode *ast_new(ASTKind kind);
void     ast_free(ASTNode *n);

// ---------- 编译器上下文 ----------

void     compiler_init(Compiler *c);
void     compiler_free(Compiler *c);
char    *compiler_new_label(Compiler *c);
char    *compiler_new_string_label(Compiler *c);
void     compiler_register_string(Compiler *c, const char *str, const char *label);

#ifdef __cplusplus
}
#endif

#endif // COMPILER_AST_H
