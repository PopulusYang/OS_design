// ast.c —— AST 节点和符号表操作实现

#include "compiler/ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------- 符号表 ----------

Scope *scope_new(Scope *parent) {
    Scope *s = malloc(sizeof(*s));
    s->symbols = NULL;
    s->nsymbols = 0;
    s->capacity = 0;
    s->parent = parent;
    return s;
}

void scope_free(Scope *s) {
    if (s) {
        free(s->symbols);
        free(s);
    }
}

Symbol *scope_lookup(Scope *s, const char *name) {
    while (s) {
        for (int i = 0; i < s->nsymbols; i++) {
            if (strcmp(s->symbols[i].name, name) == 0)
                return &s->symbols[i];
        }
        s = s->parent;
    }
    return NULL;
}

Symbol *scope_add(Scope *s, SymbolKind kind, const char *name, int offset, int size) {
    if (s->nsymbols >= s->capacity) {
        s->capacity = s->capacity == 0 ? 16 : s->capacity * 2;
        s->symbols = realloc(s->symbols, sizeof(Symbol) * s->capacity);
    }
    Symbol *sym = &s->symbols[s->nsymbols++];
    memset(sym, 0, sizeof(*sym));
    sym->kind = kind;
    strncpy(sym->name, name, sizeof(sym->name) - 1);
    sym->offset = offset;
    sym->size = size;
    return sym;
}

// ---------- AST 节点 ----------

ASTNode *ast_new(ASTKind kind) {
    ASTNode *n = calloc(1, sizeof(*n));
    n->kind = kind;
    return n;
}

void ast_free(ASTNode *n) {
    if (!n) return;
    switch (n->kind) {
    case AST_IDENT:
        free(n->ident.name);
        break;
    case AST_STRING:
        free(n->string.str);
        free(n->string.label);
        break;
    case AST_UNARY:
        ast_free(n->unary.operand);
        break;
    case AST_BINARY:
        ast_free(n->binary.left);
        ast_free(n->binary.right);
        break;
    case AST_ASSIGN:
        ast_free(n->assign.lvalue);
        ast_free(n->assign.rvalue);
        break;
    case AST_CALL:
        free(n->call.name);
        for (int i = 0; i < n->call.nargs; i++)
            ast_free(n->call.args[i]);
        free(n->call.args);
        break;
    case AST_IF:
        ast_free(n->if_stmt.cond);
        ast_free(n->if_stmt.then_stmt);
        ast_free(n->if_stmt.else_stmt);
        break;
    case AST_WHILE:
    case AST_DO_WHILE:
        ast_free(n->while_stmt.cond);
        ast_free(n->while_stmt.body);
        break;
    case AST_FOR:
        ast_free(n->for_stmt.init);
        ast_free(n->for_stmt.cond);
        ast_free(n->for_stmt.step);
        ast_free(n->for_stmt.body);
        break;
    case AST_RETURN:
        ast_free(n->return_val);
        break;
    case AST_BLOCK:
        for (int i = 0; i < n->block.nstmts; i++)
            ast_free(n->block.stmts[i]);
        free(n->block.stmts);
        break;
    case AST_DECL:
        free(n->decl.name);
        ast_free(n->decl.init);
        break;
    case AST_FUNC:
        free(n->func.name);
        for (int i = 0; i < n->func.nparams; i++)
            free(n->func.param_names[i]);
        free(n->func.param_names);
        ast_free(n->func.body);
        break;
    case AST_SUBSCRIPT:
        ast_free(n->subscript.array);
        ast_free(n->subscript.index);
        break;
    default:
        break;
    }
    free(n);
}

// ---------- 编译器上下文 ----------

void compiler_init(Compiler *c) {
    memset(c, 0, sizeof(*c));
    c->global_scope = scope_new(NULL);
    c->current_scope = c->global_scope;
    c->label_counter = 0;
    c->string_counter = 0;
    c->func_local_offset = 0;
    c->nstrings = 0;
    c->string_labels = NULL;
    c->string_values = NULL;
}

void compiler_free(Compiler *c) {
    scope_free(c->global_scope);
    for (int i = 0; i < c->nstrings; i++) {
        free(c->string_labels[i]);
        free(c->string_values[i]);
    }
    free(c->string_labels);
    free(c->string_values);
}

char *compiler_new_label(Compiler *c) {
    char buf[32];
    snprintf(buf, sizeof(buf), ".L%d", c->label_counter++);
    return strdup(buf);
}

char *compiler_new_string_label(Compiler *c) {
    char buf[32];
    snprintf(buf, sizeof(buf), ".L_str_%d", c->string_counter++);
    return strdup(buf);
}

void compiler_register_string(Compiler *c, const char *str, const char *label) {
    c->nstrings++;
    c->string_labels = realloc(c->string_labels, sizeof(char *) * c->nstrings);
    c->string_values = realloc(c->string_values, sizeof(char *) * c->nstrings);
    c->string_labels[c->nstrings - 1] = strdup(label);
    c->string_values[c->nstrings - 1] = strdup(str);
}
