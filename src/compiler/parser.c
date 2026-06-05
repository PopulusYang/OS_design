/*
 * parser.c
 * 递归下降语法分析：把 Token 流建成 AST，表达式部分用 Pratt 法处理优先级。
 */
#include "compiler/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parser_advance(Parser *p) {
    p->cur = *lexer_next(p->lexer);
}

void parser_error(Parser *p, const char *msg) {
    fprintf(stderr, "\033[1;31mparse error\033[0m at %s:%d: %s (got %s",
            p->filename ? p->filename : "<stdin>",
            p->cur.line, msg, token_type_name(p->cur.type));
    if (p->cur.type == TOK_IDENT || p->cur.type == TOK_NUM ||
        p->cur.type == TOK_STRING || p->cur.type == TOK_CHAR) {
        fprintf(stderr, " '%s'", p->cur.strval);
    }
    fprintf(stderr, ")\n");
}

static void expect(Parser *p, TokenType t, const char *msg) {
    if (p->cur.type != t) {
        parser_error(p, msg);
    }
    parser_advance(p);
}

// 运算符优先级，数值越大绑定越紧
typedef enum {
    PREC_LOWEST = 0,
    PREC_ASSIGN,
    PREC_OR,
    PREC_AND,
    PREC_PIPE,
    PREC_XOR,
    PREC_AMP,
    PREC_EQ,
    PREC_REL,
    PREC_SHIFT,
    PREC_ADD,
    PREC_MUL,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY,
} Precedence;

static Precedence token_precedence(TokenType t) {
    switch (t) {
        case TOK_ASSIGN: case TOK_PLUS_ASSIGN: case TOK_MINUS_ASSIGN:
        case TOK_STAR_ASSIGN: case TOK_SLASH_ASSIGN:
            return PREC_ASSIGN;
        case TOK_OR:   return PREC_OR;
        case TOK_AND:  return PREC_AND;
        case TOK_PIPE: return PREC_PIPE;
        case TOK_CARET: return PREC_XOR;
        case TOK_AMP:  return PREC_AMP;
        case TOK_EQ: case TOK_NE: return PREC_EQ;
        case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE:
            return PREC_REL;
        case TOK_LSHIFT: case TOK_RSHIFT: return PREC_SHIFT;
        case TOK_PLUS: case TOK_MINUS:    return PREC_ADD;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return PREC_MUL;
        default: return PREC_LOWEST;
    }
}

static BinOp token_to_binop(TokenType t) {
    switch (t) {
        case TOK_PLUS:  return BINOP_ADD;
        case TOK_MINUS: return BINOP_SUB;
        case TOK_STAR:  return BINOP_MUL;
        case TOK_SLASH: return BINOP_DIV;
        case TOK_PERCENT: return BINOP_MOD;
        case TOK_EQ:    return BINOP_EQ;
        case TOK_NE:    return BINOP_NE;
        case TOK_LT:    return BINOP_LT;
        case TOK_GT:    return BINOP_GT;
        case TOK_LE:    return BINOP_LE;
        case TOK_GE:    return BINOP_GE;
        case TOK_AND:   return BINOP_AND;
        case TOK_OR:    return BINOP_OR;
        case TOK_AMP:   return BINOP_AMP;
        case TOK_PIPE:  return BINOP_PIPE;
        case TOK_CARET: return BINOP_XOR;
        case TOK_LSHIFT: return BINOP_LSHIFT;
        case TOK_RSHIFT: return BINOP_RSHIFT;
        default: return BINOP_ADD;
    }
}

static UnOp token_to_unop(TokenType t) {
    switch (t) {
        case TOK_MINUS: return UNOP_NEG;
        case TOK_NOT:   return UNOP_NOT;
        case TOK_TILDE: return UNOP_TILDE;
        case TOK_AMP:   return UNOP_ADDR;
        case TOK_STAR:  return UNOP_DEREF;
        default: return UNOP_NEG;
    }
}

static ASTNode *parse_expression(Parser *p, Precedence prec);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_compound_statement(Parser *p);

static ASTNode *parse_primary(Parser *p) {
    if (p->cur.type == TOK_NUM) {
        ASTNode *n = ast_new(AST_NUM);
        n->line = p->cur.line;
        n->num_val = p->cur.intval;
        parser_advance(p);
        return n;
    }

    if (p->cur.type == TOK_CHAR) {
        ASTNode *n = ast_new(AST_NUM);
        n->line = p->cur.line;
        n->num_val = p->cur.intval;
        parser_advance(p);
        return n;
    }

    if (p->cur.type == TOK_STRING) {
        ASTNode *n = ast_new(AST_STRING);
        n->line = p->cur.line;
        n->string.str = strdup(p->cur.strval);
        n->string.label = compiler_new_string_label(p->comp);
        compiler_register_string(p->comp, p->cur.strval, n->string.label);
        parser_advance(p);
        return n;
    }

    if (p->cur.type == TOK_IDENT) {
        ASTNode *n = ast_new(AST_IDENT);
        n->line = p->cur.line;
        n->ident.name = strdup(p->cur.strval);
        n->ident.sym = scope_lookup(p->comp->current_scope, n->ident.name);
        parser_advance(p);
        return n;
    }

    if (p->cur.type == TOK_LPAREN) {
        parser_advance(p);
        ASTNode *n = parse_expression(p, PREC_LOWEST);
        expect(p, TOK_RPAREN, "expected ')'");
        return n;
    }

    parser_error(p, "unexpected token in expression");
    ASTNode *n = ast_new(AST_NUM);
    n->num_val = 0;
    return n;
}

static ASTNode *parse_expression(Parser *p, Precedence prec) {
    if (p->cur.type == TOK_MINUS || p->cur.type == TOK_NOT ||
        p->cur.type == TOK_TILDE || p->cur.type == TOK_AMP ||
        p->cur.type == TOK_STAR) {
        TokenType op = p->cur.type;
        parser_advance(p);
        ASTNode *operand = parse_expression(p, PREC_UNARY);
        ASTNode *n = ast_new(AST_UNARY);
        n->unary.op = token_to_unop(op);
        n->unary.operand = operand;
        return n;
    }

    ASTNode *left = parse_primary(p);
    if (!left) return NULL;

    while (1) {
        TokenType t = p->cur.type;

        if (t == TOK_LPAREN) {
            parser_advance(p);
            ASTNode *call = ast_new(AST_CALL);
            call->call.name = left->kind == AST_IDENT ? strdup(left->ident.name) : strdup("(anon)");

            if (p->cur.type != TOK_RPAREN) {
                call->call.args = NULL;
                call->call.nargs = 0;
                while (1) {
                    call->call.nargs++;
                    call->call.args = realloc(call->call.args,
                        sizeof(ASTNode *) * call->call.nargs);
                    call->call.args[call->call.nargs - 1] =
                        parse_expression(p, PREC_LOWEST);
                    if (p->cur.type == TOK_COMMA) {
                        parser_advance(p);
                        continue;
                    }
                    break;
                }
            } else {
                call->call.args = NULL;
                call->call.nargs = 0;
            }
            expect(p, TOK_RPAREN, "expected ')' after function arguments");
            left = call;
            continue;
        }

        if (t == TOK_LBRACKET) {
            parser_advance(p);
            ASTNode *sub = ast_new(AST_SUBSCRIPT);
            sub->subscript.array = left;
            sub->subscript.index = parse_expression(p, PREC_LOWEST);
            expect(p, TOK_RBRACKET, "expected ']'");
            left = sub;
            continue;
        }

        if (t == TOK_DOT) {
            parser_advance(p);
            if (p->cur.type != TOK_IDENT) {
                parser_error(p, "expected member name after '.'");
                parser_advance(p);
                continue;
            }
            ASTNode *member = ast_new(AST_IDENT);
            member->ident.name = strdup(p->cur.strval);
            parser_advance(p);
            ASTNode *dot = ast_new(AST_BINARY);
            dot->binary.op = BINOP_ADD;
            dot->binary.left = left;
            dot->binary.right = member;
            left = dot;
            continue;
        }

        if (t == TOK_ASSIGN || t == TOK_PLUS_ASSIGN || t == TOK_MINUS_ASSIGN ||
            t == TOK_STAR_ASSIGN || t == TOK_SLASH_ASSIGN ||
            t == TOK_EQ || t == TOK_NE || t == TOK_LT || t == TOK_GT ||
            t == TOK_LE || t == TOK_GE || t == TOK_AND || t == TOK_OR ||
            t == TOK_AMP || t == TOK_PIPE || t == TOK_CARET ||
            t == TOK_LSHIFT || t == TOK_RSHIFT ||
            t == TOK_PLUS || t == TOK_MINUS || t == TOK_STAR ||
            t == TOK_SLASH || t == TOK_PERCENT) {

            Precedence this_prec = token_precedence(t);
            if (this_prec < prec) break;

            parser_advance(p);
            ASTNode *right = parse_expression(p, this_prec + (t == TOK_ASSIGN ? 0 : 1));

            if (t == TOK_ASSIGN) {
                ASTNode *n = ast_new(AST_ASSIGN);
                n->assign.op = TOK_ASSIGN;
                n->assign.lvalue = left;
                n->assign.rvalue = right;
                left = n;
            } else if (t == TOK_PLUS_ASSIGN || t == TOK_MINUS_ASSIGN ||
                       t == TOK_STAR_ASSIGN || t == TOK_SLASH_ASSIGN) {
                ASTNode *binop = ast_new(AST_BINARY);
                binop->binary.op = token_to_binop(
                    t == TOK_PLUS_ASSIGN ? TOK_PLUS :
                    t == TOK_MINUS_ASSIGN ? TOK_MINUS :
                    t == TOK_STAR_ASSIGN ? TOK_STAR : TOK_SLASH);
                binop->binary.left = left;
                binop->binary.right = right;

                ASTNode *n = ast_new(AST_ASSIGN);
                n->assign.op = TOK_ASSIGN;
                n->assign.lvalue = left;
                n->assign.rvalue = binop;
                left = n;
            } else {
                ASTNode *n = ast_new(AST_BINARY);
                n->binary.op = token_to_binop(t);
                n->binary.left = left;
                n->binary.right = right;
                left = n;
            }
            continue;
        }

        break;
    }

    return left;
}

static ASTNode *parse_declaration(Parser *p);

static ASTNode *parse_statement(Parser *p) {
    if (p->cur.type == TOK_INT) {
        return parse_declaration(p);
    }

    if (p->cur.type == TOK_IF) {
        parser_advance(p);
        expect(p, TOK_LPAREN, "expected '(' after 'if'");
        ASTNode *cond = parse_expression(p, PREC_LOWEST);
        expect(p, TOK_RPAREN, "expected ')' after if condition");
        ASTNode *then_stmt = parse_statement(p);
        ASTNode *else_stmt = NULL;
        if (p->cur.type == TOK_ELSE) {
            parser_advance(p);
            else_stmt = parse_statement(p);
        }
        ASTNode *n = ast_new(AST_IF);
        n->if_stmt.cond = cond;
        n->if_stmt.then_stmt = then_stmt;
        n->if_stmt.else_stmt = else_stmt;
        return n;
    }

    if (p->cur.type == TOK_WHILE) {
        parser_advance(p);
        expect(p, TOK_LPAREN, "expected '(' after 'while'");
        ASTNode *cond = parse_expression(p, PREC_LOWEST);
        expect(p, TOK_RPAREN, "expected ')' after while condition");
        ASTNode *body = parse_statement(p);
        ASTNode *n = ast_new(AST_WHILE);
        n->while_stmt.cond = cond;
        n->while_stmt.body = body;
        return n;
    }

    if (p->cur.type == TOK_FOR) {
        parser_advance(p);
        expect(p, TOK_LPAREN, "expected '(' after 'for'");
        ASTNode *init = NULL, *cond = NULL, *step = NULL;

        if (p->cur.type != TOK_SEMI) {
            init = parse_statement(p);
        } else {
            parser_advance(p);
        }

        if (p->cur.type != TOK_SEMI) {
            cond = parse_expression(p, PREC_LOWEST);
        }
        expect(p, TOK_SEMI, "expected ';' in for loop");

        if (p->cur.type != TOK_RPAREN) {
            step = parse_expression(p, PREC_LOWEST);
        }
        expect(p, TOK_RPAREN, "expected ')' after for header");

        ASTNode *body = parse_statement(p);
        ASTNode *n = ast_new(AST_FOR);
        n->for_stmt.init = init;
        n->for_stmt.cond = cond;
        n->for_stmt.step = step;
        n->for_stmt.body = body;
        return n;
    }

    if (p->cur.type == TOK_DO) {
        parser_advance(p);
        ASTNode *body = parse_statement(p);
        expect(p, TOK_WHILE, "expected 'while' after do block");
        expect(p, TOK_LPAREN, "expected '(' after 'while'");
        ASTNode *cond = parse_expression(p, PREC_LOWEST);
        expect(p, TOK_RPAREN, "expected ')' after do-while condition");
        expect(p, TOK_SEMI, "expected ';' after do-while");
        ASTNode *n = ast_new(AST_DO_WHILE);
        n->while_stmt.cond = cond;
        n->while_stmt.body = body;
        return n;
    }

    if (p->cur.type == TOK_RETURN) {
        parser_advance(p);
        ASTNode *n = ast_new(AST_RETURN);
        if (p->cur.type != TOK_SEMI) {
            n->return_val = parse_expression(p, PREC_LOWEST);
        }
        expect(p, TOK_SEMI, "expected ';' after return");
        return n;
    }

    if (p->cur.type == TOK_BREAK) {
        parser_advance(p);
        expect(p, TOK_SEMI, "expected ';' after break");
        return ast_new(AST_BREAK);
    }

    if (p->cur.type == TOK_CONTINUE) {
        parser_advance(p);
        expect(p, TOK_SEMI, "expected ';' after continue");
        return ast_new(AST_CONTINUE);
    }

    if (p->cur.type == TOK_LBRACE) {
        return parse_compound_statement(p);
    }

    if (p->cur.type != TOK_SEMI) {
        ASTNode *expr = parse_expression(p, PREC_LOWEST);
        expect(p, TOK_SEMI, "expected ';' after expression");
        ASTNode *n = ast_new(AST_BLOCK);
        n->block.stmts = malloc(sizeof(ASTNode *));
        n->block.stmts[0] = expr;
        n->block.nstmts = 1;
        return n;
    }

    parser_advance(p);
    return ast_new(AST_EMPTY);
}

static ASTNode *parse_compound_statement(Parser *p) {
    expect(p, TOK_LBRACE, "expected '{'");

    ASTNode *block = ast_new(AST_BLOCK);
    block->block.stmts = NULL;
    block->block.nstmts = 0;

    p->comp->current_scope = scope_new(p->comp->current_scope);

    while (p->cur.type != TOK_RBRACE && p->cur.type != TOK_EOF) {
        block->block.nstmts++;
        block->block.stmts = realloc(block->block.stmts,
            sizeof(ASTNode *) * block->block.nstmts);
        block->block.stmts[block->block.nstmts - 1] = parse_statement(p);
    }

    expect(p, TOK_RBRACE, "expected '}'");

    p->comp->current_scope = p->comp->current_scope->parent;

    return block;
}

static ASTNode *parse_declaration(Parser *p) {
    expect(p, TOK_INT, "expected 'int'");

    if (p->cur.type == TOK_IDENT) {
        char *name = strdup(p->cur.strval);
        parser_advance(p);

        if (p->cur.type == TOK_LPAREN) {
            parser_advance(p);
            char **param_names = NULL;
            int nparams = 0;

            if (p->cur.type != TOK_RPAREN) {
                while (1) {
                    if (p->cur.type == TOK_INT) parser_advance(p);
                    if (p->cur.type == TOK_IDENT) {
                        nparams++;
                        param_names = realloc(param_names, sizeof(char *) * nparams);
                        param_names[nparams - 1] = strdup(p->cur.strval);

                        scope_add(p->comp->current_scope, SYM_PARAM,
                            p->cur.strval, 0, 4);
                        parser_advance(p);
                    }
                    if (p->cur.type == TOK_COMMA) {
                        parser_advance(p);
                        continue;
                    }
                    break;
                }
            }
            expect(p, TOK_RPAREN, "expected ')' after function parameters");

            if (p->cur.type == TOK_SEMI) {
                parser_advance(p);
                ASTNode *n = ast_new(AST_FUNC);
                n->func.name = name;
                n->func.param_names = param_names;
                n->func.nparams = nparams;
                n->func.body = NULL;
                n->func.sym = scope_add(p->comp->global_scope, SYM_FUNC, name, 0, 0);
                n->func.sym->func_nparams = nparams;
                return n;
            }

            ASTNode *body = parse_compound_statement(p);

            ASTNode *n = ast_new(AST_FUNC);
            n->func.name = name;
            n->func.param_names = param_names;
            n->func.nparams = nparams;
            n->func.body = body;
            n->func.sym = scope_add(p->comp->global_scope, SYM_FUNC, name, 0, 0);
            n->func.sym->func_nparams = nparams;
            return n;
        }

        int array_size = 0;
        if (p->cur.type == TOK_LBRACKET) {
            parser_advance(p);
            if (p->cur.type == TOK_NUM) {
                array_size = p->cur.intval;
                parser_advance(p);
            }
            expect(p, TOK_RBRACKET, "expected ']' in array declaration");
        }

        ASTNode *init = NULL;
        if (p->cur.type == TOK_ASSIGN) {
            parser_advance(p);
            init = parse_expression(p, PREC_LOWEST);
        }
        expect(p, TOK_SEMI, "expected ';' after variable declaration");

        int is_global = (p->comp->current_scope == p->comp->global_scope);

        Symbol *sym;
        if (is_global) {
            sym = scope_add(p->comp->global_scope, SYM_GLOBAL_VAR, name, 0,
                array_size > 0 ? array_size * 4 : 4);
            sym->array_size = array_size;
        } else {
            p->comp->func_local_offset += 4;
            if (array_size > 0) {
                p->comp->func_local_offset += array_size * 4;
            }
            sym = scope_add(p->comp->current_scope, SYM_LOCAL_VAR, name,
                -p->comp->func_local_offset,
                array_size > 0 ? array_size * 4 : 4);
            sym->array_size = array_size;
        }

        ASTNode *n = ast_new(AST_DECL);
        n->decl.name = name;
        n->decl.init = init;
        n->decl.sym = sym;
        n->decl.is_global = is_global;
        n->decl.array_size = array_size;
        return n;
    }

    parser_error(p, "expected identifier after 'int'");
    return ast_new(AST_EMPTY);
}

ASTNode *parser_parse(Parser *p) {
    parser_advance(p);

    ASTNode *unit = ast_new(AST_BLOCK);
    unit->block.stmts = NULL;
    unit->block.nstmts = 0;

    while (p->cur.type != TOK_EOF) {
        if (p->cur.type == TOK_INT) {
            unit->block.nstmts++;
            unit->block.stmts = realloc(unit->block.stmts,
                sizeof(ASTNode *) * unit->block.nstmts);
            unit->block.stmts[unit->block.nstmts - 1] = parse_declaration(p);
        } else {
            parser_error(p, "unexpected token at top level");
            parser_advance(p);
        }
    }

    return unit;
}

int parser_init(Parser *p, Lexer *lexer, Compiler *comp) {
    memset(p, 0, sizeof(*p));
    p->lexer = lexer;
    p->comp = comp;
    p->filename = lexer->filename ? strdup(lexer->filename) : NULL;
    return 0;
}

void parser_close(Parser *p) {
    free(p->filename);
}
