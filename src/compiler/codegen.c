/*
 * codegen.c
 * 把 AST 翻译成 UPFS 虚拟机汇编。大立即数用 LUI 拼接，
 * 有符号比较和移位调用 runtime.s 中的辅助函数。
 */
#include "compiler/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define REG_FP  14
#define REG_SP  15
#define REG_RET 0

static void emit_escaped_string(CodeGen *cg, const char *s) {
    fputc('"', cg->out);
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '\n': fputs("\\n", cg->out); break;
        case '\r': fputs("\\r", cg->out); break;
        case '\t': fputs("\\t", cg->out); break;
        case '\\': fputs("\\\\", cg->out); break;
        case '"':  fputs("\\\"", cg->out); break;
        default:   fputc(*p, cg->out); break;
        }
    }
    fputc('"', cg->out);
}

static int codegen_new_tmp(CodeGen *cg) {
    return ++cg->tmp_counter;
}

static void emit(CodeGen *cg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(cg->out, fmt, ap);
    va_end(ap);
}

static void emit_reg(CodeGen *cg, int r) {
    fprintf(cg->out, "R%d", r);
}

// MOVI 只能装 12 位有符号数，更大的数用 LUI 拼高位再 ADD 低位
static void emit_load_imm(CodeGen *cg, int reg, int32_t val) {
    if (val >= -2048 && val <= 2047) {
        emit(cg, "    MOVI ");
        emit_reg(cg, reg);
        emit(cg, ", %d\n", val);
    } else {
        int32_t high = (val >> 12) & 0xFFF;
        int32_t low = val & 0xFFF;
        if (low > 2047) {
            low = (int16_t)(low | 0xFFFFF000) & 0xFFF;
            high = ((val - low) >> 12) & 0xFFF;
        }
        emit(cg, "    LUI ");
        emit_reg(cg, reg);
        emit(cg, ", %d\n", high);
        if (low != 0) {
            int need_mark = (reg >= REG_ALLOC_MIN && reg <= REG_ALLOC_MAX && !cg->ra.used[reg]);
            if (need_mark) cg->ra.used[reg] = true;
            int tmp = regalloc_alloc(&cg->ra);
            emit(cg, "    MOVI ");
            emit_reg(cg, tmp);
            emit(cg, ", %d\n", (int16_t)low);
            emit(cg, "    ADD ");
            emit_reg(cg, reg);
            emit(cg, ", ");
            emit_reg(cg, reg);
            emit(cg, ", ");
            emit_reg(cg, tmp);
            emit(cg, "\n");
            regalloc_free(&cg->ra, tmp);
            if (need_mark) cg->ra.used[reg] = false;
        }
    }
}

static void emit_load_addr(CodeGen *cg, int reg, const char *label) {
    int need_mark = (reg >= REG_ALLOC_MIN && reg <= REG_ALLOC_MAX && !cg->ra.used[reg]);
    if (need_mark) cg->ra.used[reg] = true;
    emit(cg, "    LUI ");
    emit_reg(cg, reg);
    emit(cg, ", %s\n", label);
    int tmp = regalloc_alloc(&cg->ra);
    emit(cg, "    MOVI ");
    emit_reg(cg, tmp);
    emit(cg, ", %s\n", label);
    emit(cg, "    ADD ");
    emit_reg(cg, reg);
    emit(cg, ", ");
    emit_reg(cg, reg);
    emit(cg, ", ");
    emit_reg(cg, tmp);
    emit(cg, "\n");
    regalloc_free(&cg->ra, tmp);
    if (need_mark) cg->ra.used[reg] = false;
}

static void emit_load_local(CodeGen *cg, int reg, int offset) {
    emit(cg, "    LD ");
    emit_reg(cg, reg);
    emit(cg, ", R%d, %d\n", REG_FP, offset);
}

static void emit_store_local(CodeGen *cg, int val_reg, int offset) {
    emit(cg, "    ST ");
    emit_reg(cg, val_reg);
    emit(cg, ", R%d, %d\n", REG_FP, offset);
}

static void emit_load_global(CodeGen *cg, int reg, Symbol *sym) {
    emit_load_addr(cg, reg, sym->name);
    emit(cg, "    LD ");
    emit_reg(cg, reg);
    emit(cg, ", ");
    emit_reg(cg, reg);
    emit(cg, ", 0\n");
}

static void emit_store_global(CodeGen *cg, int val_reg, Symbol *sym) {
    int tmp = regalloc_alloc(&cg->ra);
    emit_load_addr(cg, tmp, sym->name);
    emit(cg, "    ST ");
    emit_reg(cg, val_reg);
    emit(cg, ", ");
    emit_reg(cg, tmp);
    emit(cg, ", 0\n");
    regalloc_free(&cg->ra, tmp);
}

static int emit_expr(CodeGen *cg, ASTNode *n, int target_reg);

static int emit_expr(CodeGen *cg, ASTNode *n, int target_reg) {
    if (!n) return target_reg;

    switch (n->kind) {
    case AST_NUM: {
        emit_load_imm(cg, target_reg, n->num_val);
        return target_reg;
    }

    case AST_STRING: {
        emit_load_addr(cg, target_reg, n->string.label);
        return target_reg;
    }

    case AST_IDENT: {
        Symbol *sym = n->ident.sym;
        if (!sym) sym = scope_lookup(cg->comp->current_scope, n->ident.name);
        if (!sym) {
            emit_load_imm(cg, target_reg, 0);
            return target_reg;
        }
        switch (sym->kind) {
        case SYM_GLOBAL_VAR:
            emit_load_global(cg, target_reg, sym);
            break;
        case SYM_LOCAL_VAR:
        case SYM_PARAM:
            emit_load_local(cg, target_reg, sym->offset);
            break;
        case SYM_FUNC:
            emit_load_addr(cg, target_reg, n->ident.name);
            break;
        }
        return target_reg;
    }

    case AST_BINARY: {
        int rleft = regalloc_alloc(&cg->ra);
        emit_expr(cg, n->binary.left, rleft);

        int rright = regalloc_alloc(&cg->ra);
        emit_expr(cg, n->binary.right, rright);

        switch (n->binary.op) {
        case BINOP_LT:
        case BINOP_GT:
        case BINOP_LE:
        case BINOP_GE: {
            const char *rt_func;
            switch (n->binary.op) {
                case BINOP_LT: rt_func = "__rt_lt"; break;
                case BINOP_GT: rt_func = "__rt_gt"; break;
                case BINOP_LE: rt_func = "__rt_le"; break;
                case BINOP_GE: rt_func = "__rt_ge"; break;
                default: rt_func = "__rt_lt"; break;
            }
            emit(cg, "    MOV R1, ");
            emit_reg(cg, rleft);
            emit(cg, "\n");
            emit(cg, "    MOV R2, ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            emit(cg, "    CALL %s\n", rt_func);
            emit(cg, "    MOV ");
            emit_reg(cg, target_reg);
            emit(cg, ", R0\n");
            break;
        }
        case BINOP_EQ: {
            emit(cg, "    CMP ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            emit(cg, "    JZ .Leq_%d\n", cg->tmp_counter);
            emit_load_imm(cg, target_reg, 0);
            emit(cg, "    JMP .Lend_eq_%d\n", cg->tmp_counter);
            emit(cg, ".Leq_%d:\n", cg->tmp_counter);
            emit_load_imm(cg, target_reg, 1);
            emit(cg, ".Lend_eq_%d:\n", cg->tmp_counter);
            cg->tmp_counter++;
            break;
        }
        case BINOP_NE: {
            emit(cg, "    CMP ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            emit(cg, "    JNZ .Lne_%d\n", cg->tmp_counter);
            emit_load_imm(cg, target_reg, 0);
            emit(cg, "    JMP .Lend_ne_%d\n", cg->tmp_counter);
            emit(cg, ".Lne_%d:\n", cg->tmp_counter);
            emit_load_imm(cg, target_reg, 1);
            emit(cg, ".Lend_ne_%d:\n", cg->tmp_counter);
            cg->tmp_counter++;
            break;
        }
        case BINOP_AND: {
            int lbl = cg->tmp_counter++;
            int zero = regalloc_alloc(&cg->ra);
            emit_load_imm(cg, zero, 0);
            emit(cg, "    CMP ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, zero);
            emit(cg, "\n");
            emit(cg, "    JZ .Land_false_%d\n", lbl);
            emit(cg, "    CMP ");
            emit_reg(cg, rright);
            emit(cg, ", ");
            emit_reg(cg, zero);
            emit(cg, "\n");
            emit(cg, "    JZ .Land_false_%d\n", lbl);
            emit_load_imm(cg, target_reg, 1);
            emit(cg, "    JMP .Land_end_%d\n", lbl);
            emit(cg, ".Land_false_%d:\n", lbl);
            emit_load_imm(cg, target_reg, 0);
            emit(cg, ".Land_end_%d:\n", lbl);
            regalloc_free(&cg->ra, zero);
            break;
        }
        case BINOP_OR: {
            int lbl = cg->tmp_counter++;
            int zero = regalloc_alloc(&cg->ra);
            emit_load_imm(cg, zero, 0);
            emit(cg, "    CMP ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, zero);
            emit(cg, "\n");
            emit(cg, "    JNZ .Lor_true_%d\n", lbl);
            emit(cg, "    CMP ");
            emit_reg(cg, rright);
            emit(cg, ", ");
            emit_reg(cg, zero);
            emit(cg, "\n");
            emit(cg, "    JNZ .Lor_true_%d\n", lbl);
            emit_load_imm(cg, target_reg, 0);
            emit(cg, "    JMP .Lor_end_%d\n", lbl);
            emit(cg, ".Lor_true_%d:\n", lbl);
            emit_load_imm(cg, target_reg, 1);
            emit(cg, ".Lor_end_%d:\n", lbl);
            regalloc_free(&cg->ra, zero);
            break;
        }
        case BINOP_ADD:
            emit(cg, "    ADD ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            break;
        case BINOP_SUB:
            emit(cg, "    SUB ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            break;
        case BINOP_MUL:
            emit(cg, "    MUL ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            break;
        case BINOP_DIV:
            emit(cg, "    DIV ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            break;
        case BINOP_MOD: {
            int tmp = regalloc_alloc(&cg->ra);
            emit(cg, "    DIV ");
            emit_reg(cg, tmp);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            emit(cg, "    MUL ");
            emit_reg(cg, tmp);
            emit(cg, ", ");
            emit_reg(cg, tmp);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            emit(cg, "    SUB ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, tmp);
            emit(cg, "\n");
            regalloc_free(&cg->ra, tmp);
            break;
        }
        case BINOP_AMP:
            emit(cg, "    AND ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            break;
        case BINOP_PIPE:
            emit(cg, "    OR ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            break;
        case BINOP_XOR:
            emit(cg, "    XOR ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rleft);
            emit(cg, ", ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            break;
        case BINOP_LSHIFT:
        case BINOP_RSHIFT:
            emit(cg, "    MOV R1, ");
            emit_reg(cg, rleft);
            emit(cg, "\n");
            emit(cg, "    MOV R2, ");
            emit_reg(cg, rright);
            emit(cg, "\n");
            emit(cg, "    CALL %s\n",
                n->binary.op == BINOP_LSHIFT ? "__rt_lshift" : "__rt_rshift");
            emit(cg, "    MOV ");
            emit_reg(cg, target_reg);
            emit(cg, ", R0\n");
            break;
        }

        regalloc_free(&cg->ra, rleft);
        regalloc_free(&cg->ra, rright);
        return target_reg;
    }

    case AST_UNARY: {
        int rop = emit_expr(cg, n->unary.operand, target_reg);
        switch (n->unary.op) {
        case UNOP_NEG: {
            int tmp = regalloc_alloc(&cg->ra);
            emit_load_imm(cg, tmp, 0);
            emit(cg, "    SUB ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, tmp);
            emit(cg, ", ");
            emit_reg(cg, rop);
            emit(cg, "\n");
            regalloc_free(&cg->ra, tmp);
            break;
        }
        case UNOP_NOT: {
            int lbl = cg->tmp_counter++;
            int zero = regalloc_alloc(&cg->ra);
            emit_load_imm(cg, zero, 0);
            emit(cg, "    CMP ");
            emit_reg(cg, rop);
            emit(cg, ", ");
            emit_reg(cg, zero);
            emit(cg, "\n");
            emit(cg, "    JZ .Lunot_%d\n", lbl);
            emit_load_imm(cg, target_reg, 0);
            emit(cg, "    JMP .Lunot_end_%d\n", lbl);
            emit(cg, ".Lunot_%d:\n", lbl);
            emit_load_imm(cg, target_reg, 1);
            emit(cg, ".Lunot_end_%d:\n", lbl);
            regalloc_free(&cg->ra, zero);
            break;
        }
        case UNOP_TILDE: {
            int tmp = regalloc_alloc(&cg->ra);
            emit_load_imm(cg, tmp, -1);
            emit(cg, "    XOR ");
            emit_reg(cg, target_reg);
            emit(cg, ", ");
            emit_reg(cg, rop);
            emit(cg, ", ");
            emit_reg(cg, tmp);
            emit(cg, "\n");
            regalloc_free(&cg->ra, tmp);
            break;
        }
        case UNOP_ADDR:
        case UNOP_DEREF:
            break;
        }
        return target_reg;
    }

    case AST_ASSIGN: {
        int rval = regalloc_alloc(&cg->ra);
        emit_expr(cg, n->assign.rvalue, rval);

        ASTNode *lv = n->assign.lvalue;
        if (lv->kind == AST_IDENT) {
            Symbol *sym = lv->ident.sym;
            if (sym) {
                switch (sym->kind) {
                case SYM_GLOBAL_VAR:
                    emit_store_global(cg, rval, sym);
                    break;
                case SYM_LOCAL_VAR:
                case SYM_PARAM:
                    emit_store_local(cg, rval, sym->offset);
                    break;
                }
            }
        } else if (lv->kind == AST_SUBSCRIPT) {
            int base = regalloc_alloc(&cg->ra);
            int idx = regalloc_alloc(&cg->ra);
            emit_expr(cg, lv->subscript.array, base);
            emit_expr(cg, lv->subscript.index, idx);
            int scale = regalloc_alloc(&cg->ra);
            emit_load_imm(cg, scale, 4);
            emit(cg, "    MUL ");
            emit_reg(cg, idx);
            emit(cg, ", ");
            emit_reg(cg, idx);
            emit(cg, ", ");
            emit_reg(cg, scale);
            emit(cg, "\n");
            emit(cg, "    ADD ");
            emit_reg(cg, base);
            emit(cg, ", ");
            emit_reg(cg, base);
            emit(cg, ", ");
            emit_reg(cg, idx);
            emit(cg, "\n");
            emit(cg, "    ST ");
            emit_reg(cg, rval);
            emit(cg, ", ");
            emit_reg(cg, base);
            emit(cg, ", 0\n");
            regalloc_free(&cg->ra, base);
            regalloc_free(&cg->ra, idx);
            regalloc_free(&cg->ra, scale);
        }
        emit(cg, "    MOV ");
        emit_reg(cg, target_reg);
        emit(cg, ", ");
        emit_reg(cg, rval);
        emit(cg, "\n");
        regalloc_free(&cg->ra, rval);
        return target_reg;
    }

    case AST_CALL: {
        for (int i = 0; i < n->call.nargs && i < 8; i++) {
            emit_expr(cg, n->call.args[i], i + 1);
        }
        emit(cg, "    CALL %s\n", n->call.name);
        emit(cg, "    MOV ");
        emit_reg(cg, target_reg);
        emit(cg, ", R0\n");
        return target_reg;
    }

    case AST_SUBSCRIPT: {
        int base = regalloc_alloc(&cg->ra);
        int idx = regalloc_alloc(&cg->ra);
        emit_expr(cg, n->subscript.array, base);
        emit_expr(cg, n->subscript.index, idx);
        int scale = regalloc_alloc(&cg->ra);
        emit_load_imm(cg, scale, 4);
        emit(cg, "    MUL ");
        emit_reg(cg, idx);
        emit(cg, ", ");
        emit_reg(cg, idx);
        emit(cg, ", ");
        emit_reg(cg, scale);
        emit(cg, "\n");
        emit(cg, "    ADD ");
        emit_reg(cg, base);
        emit(cg, ", ");
        emit_reg(cg, base);
        emit(cg, ", ");
        emit_reg(cg, idx);
        emit(cg, "\n");
        emit(cg, "    LD ");
        emit_reg(cg, target_reg);
        emit(cg, ", ");
        emit_reg(cg, base);
        emit(cg, ", 0\n");
        regalloc_free(&cg->ra, base);
        regalloc_free(&cg->ra, idx);
        regalloc_free(&cg->ra, scale);
        return target_reg;
    }

    default:
        emit_load_imm(cg, target_reg, 0);
        return target_reg;
    }
}

static void emit_cond_jump(CodeGen *cg, ASTNode *cond, const char *label, bool jump_if_true) {
    if (!cond) {
        if (jump_if_true) emit(cg, "    JMP %s\n", label);
        return;
    }

    if (cond->kind == AST_BINARY &&
        (cond->binary.op == BINOP_LT || cond->binary.op == BINOP_GT ||
         cond->binary.op == BINOP_LE || cond->binary.op == BINOP_GE)) {
        int rleft = regalloc_alloc(&cg->ra);
        int rright = regalloc_alloc(&cg->ra);
        emit_expr(cg, cond->binary.left, rleft);
        emit_expr(cg, cond->binary.right, rright);
        const char *rt_func;
        switch (cond->binary.op) {
            case BINOP_LT: rt_func = "__rt_lt"; break;
            case BINOP_GT: rt_func = "__rt_gt"; break;
            case BINOP_LE: rt_func = "__rt_le"; break;
            case BINOP_GE: rt_func = "__rt_ge"; break;
            default: rt_func = "__rt_lt"; break;
        }
        emit(cg, "    MOV R1, ");
        emit_reg(cg, rleft);
        emit(cg, "\n");
        emit(cg, "    MOV R2, ");
        emit_reg(cg, rright);
        emit(cg, "\n");
        emit(cg, "    CALL %s\n", rt_func);
        {
            int tmp = regalloc_alloc(&cg->ra);
            emit_load_imm(cg, tmp, 0);
            emit(cg, "    CMP R0, ");
            emit_reg(cg, tmp);
            emit(cg, "\n");
            regalloc_free(&cg->ra, tmp);
        }
        if (jump_if_true) {
            emit(cg, "    JNZ %s\n", label);
        } else {
            emit(cg, "    JZ %s\n", label);
        }
        regalloc_free(&cg->ra, rleft);
        regalloc_free(&cg->ra, rright);
        return;
    }

    if (cond->kind == AST_BINARY &&
        (cond->binary.op == BINOP_EQ || cond->binary.op == BINOP_NE)) {
        int rleft = regalloc_alloc(&cg->ra);
        int rright = regalloc_alloc(&cg->ra);
        emit_expr(cg, cond->binary.left, rleft);
        emit_expr(cg, cond->binary.right, rright);
        emit(cg, "    CMP ");
        emit_reg(cg, rleft);
        emit(cg, ", ");
        emit_reg(cg, rright);
        emit(cg, "\n");
        if (cond->binary.op == BINOP_EQ) {
            emit(cg, jump_if_true ? "    JZ %s\n" : "    JNZ %s\n", label);
        } else {
            emit(cg, jump_if_true ? "    JNZ %s\n" : "    JZ %s\n", label);
        }
        regalloc_free(&cg->ra, rleft);
        regalloc_free(&cg->ra, rright);
        return;
    }

    int reg = regalloc_alloc(&cg->ra);
    int zero = regalloc_alloc(&cg->ra);
    emit_expr(cg, cond, reg);
    emit_load_imm(cg, zero, 0);
    emit(cg, "    CMP ");
    emit_reg(cg, reg);
    emit(cg, ", ");
    emit_reg(cg, zero);
    emit(cg, "\n");
    if (jump_if_true) {
        emit(cg, "    JNZ %s\n", label);
    } else {
        emit(cg, "    JZ %s\n", label);
    }
    regalloc_free(&cg->ra, reg);
    regalloc_free(&cg->ra, zero);
}

static void emit_stmt(CodeGen *cg, ASTNode *n) {
    if (!n) return;

    switch (n->kind) {
    case AST_BLOCK: {
        for (int i = 0; i < n->block.nstmts; i++) {
            emit_stmt(cg, n->block.stmts[i]);
        }
        break;
    }

    case AST_DECL: {
        if (n->decl.init) {
            int reg = regalloc_alloc(&cg->ra);
            emit_expr(cg, n->decl.init, reg);
            if (n->decl.sym) {
                if (n->decl.is_global) {
                } else {
                    emit_store_local(cg, reg, n->decl.sym->offset);
                }
            }
            regalloc_free(&cg->ra, reg);
        }
        break;
    }

    case AST_IF: {
        char *else_label = compiler_new_label(cg->comp);
        char *end_label = compiler_new_label(cg->comp);

        emit_cond_jump(cg, n->if_stmt.cond, else_label, false);
        emit_stmt(cg, n->if_stmt.then_stmt);
        emit(cg, "    JMP %s\n", end_label);
        emit(cg, "%s:\n", else_label);
        if (n->if_stmt.else_stmt) {
            emit_stmt(cg, n->if_stmt.else_stmt);
        }
        emit(cg, "%s:\n", end_label);
        break;
    }

    case AST_WHILE: {
        char *loop_label = compiler_new_label(cg->comp);
        char *end_label = compiler_new_label(cg->comp);
        char *old_break = cg->break_label;
        char *old_cont = cg->continue_label;
        cg->break_label = end_label;
        cg->continue_label = loop_label;

        emit(cg, "%s:\n", loop_label);
        emit_cond_jump(cg, n->while_stmt.cond, end_label, false);
        emit_stmt(cg, n->while_stmt.body);
        emit(cg, "    JMP %s\n", loop_label);
        emit(cg, "%s:\n", end_label);

        cg->break_label = old_break;
        cg->continue_label = old_cont;
        break;
    }

    case AST_DO_WHILE: {
        char *loop_label = compiler_new_label(cg->comp);
        char *end_label = compiler_new_label(cg->comp);
        char *old_break = cg->break_label;
        char *old_cont = cg->continue_label;
        cg->break_label = end_label;
        cg->continue_label = loop_label;

        emit(cg, "%s:\n", loop_label);
        emit_stmt(cg, n->while_stmt.body);
        emit_cond_jump(cg, n->while_stmt.cond, end_label, false);
        emit(cg, "    JMP %s\n", loop_label);
        emit(cg, "%s:\n", end_label);

        cg->break_label = old_break;
        cg->continue_label = old_cont;
        break;
    }

    case AST_FOR: {
        char *cond_label = compiler_new_label(cg->comp);
        char *loop_label = compiler_new_label(cg->comp);
        char *end_label = compiler_new_label(cg->comp);
        char *old_break = cg->break_label;
        char *old_cont = cg->continue_label;
        cg->break_label = end_label;
        cg->continue_label = loop_label;

        emit_stmt(cg, n->for_stmt.init);
        emit(cg, "%s:\n", cond_label);
        if (n->for_stmt.cond) {
            emit_cond_jump(cg, n->for_stmt.cond, end_label, false);
        }
        emit_stmt(cg, n->for_stmt.body);
        emit(cg, "%s:\n", loop_label);
        if (n->for_stmt.step) {
            emit_stmt(cg, n->for_stmt.step);
        }
        emit(cg, "    JMP %s\n", cond_label);
        emit(cg, "%s:\n", end_label);

        cg->break_label = old_break;
        cg->continue_label = old_cont;
        break;
    }

    case AST_RETURN: {
        if (n->return_val) {
            emit_expr(cg, n->return_val, REG_RET);
        }
        emit(cg, "    JMP %s_epilogue\n", cg->cur_func);
        break;
    }

    case AST_BREAK: {
        if (cg->break_label) {
            emit(cg, "    JMP %s\n", cg->break_label);
        }
        break;
    }

    case AST_CONTINUE: {
        if (cg->continue_label) {
            emit(cg, "    JMP %s\n", cg->continue_label);
        }
        break;
    }

    case AST_EMPTY:
        break;

    default:
        {
            int tmp = regalloc_alloc(&cg->ra);
            emit_expr(cg, n, tmp);
            regalloc_free(&cg->ra, tmp);
        }
        break;
    }
}

static void count_local_vars(ASTNode *node, int *total) {
    if (!node) return;
    switch (node->kind) {
    case AST_BLOCK:
        for (int i = 0; i < node->block.nstmts; i++)
            count_local_vars(node->block.stmts[i], total);
        break;
    case AST_DECL:
        if (!node->decl.is_global)
            *total += node->decl.sym ? node->decl.sym->size : 4;
        break;
    case AST_IF:
        count_local_vars(node->if_stmt.then_stmt, total);
        count_local_vars(node->if_stmt.else_stmt, total);
        break;
    case AST_WHILE:
    case AST_DO_WHILE:
        count_local_vars(node->while_stmt.body, total);
        break;
    case AST_FOR:
        count_local_vars(node->for_stmt.init, total);
        count_local_vars(node->for_stmt.body, total);
        break;
    default:
        break;
    }
}

static void emit_func(CodeGen *cg, ASTNode *n) {
    strncpy(cg->cur_func, n->func.name, sizeof(cg->cur_func) - 1);

    cg->local_var_size = 0;
    if (n->func.body)
        count_local_vars(n->func.body, &cg->local_var_size);
    cg->local_var_size = (cg->local_var_size + 3) & ~3;
    cg->local_var_size += 64;

    emit(cg, "%s:\n", n->func.name);

    emit(cg, "    PUSH R13\n");
    emit(cg, "    PUSH R12\n");
    emit(cg, "    PUSH R11\n");
    emit(cg, "    PUSH R10\n");
    emit(cg, "    PUSH R9\n");
    emit(cg, "    PUSH R14\n");
    emit(cg, "    MOV  R14, R15\n");

    if (cg->local_var_size > 0) {
        int tmp = regalloc_alloc(&cg->ra);
        emit_load_imm(cg, tmp, cg->local_var_size);
        emit(cg, "    SUB  R15, R15, ");
        emit_reg(cg, tmp);
        emit(cg, "\n");
        regalloc_free(&cg->ra, tmp);
    }

    for (int i = 0; i < n->func.nparams && i < 8; i++) {
        Symbol *sym = scope_lookup(cg->comp->current_scope, n->func.param_names[i]);
        if (sym) {
            sym->offset = 24 + i * 4;
            emit(cg, "    ST ");
            emit_reg(cg, i + 1);
            emit(cg, ", R14, %d\n", sym->offset);
        }
    }

    if (n->func.body) {
        emit_stmt(cg, n->func.body);
    }

    emit(cg, "%s_epilogue:\n", cg->cur_func);

    if (cg->local_var_size > 0) {
        emit(cg, "    MOV  R15, R14\n");
    } else {
        emit(cg, "    MOV  R15, R14\n");
    }

    emit(cg, "    POP  R14\n");
    emit(cg, "    POP  R9\n");
    emit(cg, "    POP  R10\n");
    emit(cg, "    POP  R11\n");
    emit(cg, "    POP  R12\n");
    emit(cg, "    POP  R13\n");
    emit(cg, "    RET\n");
    emit(cg, "\n");
}

static void emit_global_data(CodeGen *cg, ASTNode *n) {
    if (n->kind != AST_DECL) return;
    if (!n->decl.is_global) return;

    Symbol *sym = n->decl.sym;
    if (!sym) return;

    if (sym->array_size > 0) {
        emit(cg, "%s:\n", sym->name);
        emit(cg, "    .space %d\n", sym->size);
    } else {
        emit(cg, "%s:\n", sym->name);
        if (n->decl.init && n->decl.init->kind == AST_NUM) {
            emit(cg, "    .word %d\n", n->decl.init->num_val);
        } else {
            emit(cg, "    .space 4\n");
        }
    }
}

void codegen_emit(CodeGen *cg, ASTNode *root) {
    if (!root || root->kind != AST_BLOCK) return;

    emit(cg, ".text\n");
    emit(cg, ".entry _start\n\n");

    emit(cg, "_start:\n");
    emit(cg, "    CALL main\n");
    emit(cg, "    SYSCALL 0\n\n");

    for (int i = 0; i < root->block.nstmts; i++) {
        ASTNode *n = root->block.stmts[i];
        if (n->kind == AST_DECL && n->decl.is_global) {
            emit_global_data(cg, n);
        }
    }

    emit(cg, "\n");
    for (int i = 0; i < root->block.nstmts; i++) {
        ASTNode *n = root->block.stmts[i];
        if (n->kind == AST_FUNC) {
            cg->comp->current_scope = scope_new(cg->comp->global_scope);
            cg->comp->func_local_offset = 0;

            for (int j = 0; j < n->func.nparams && j < 8; j++) {
                scope_add(cg->comp->current_scope, SYM_PARAM,
                    n->func.param_names[j], 24 + j * 4, 4);
            }

            emit_func(cg, n);

            scope_free(cg->comp->current_scope);
            cg->comp->current_scope = cg->comp->global_scope;
        }
    }

    if (cg->comp->nstrings > 0) {
        emit(cg, "\n.data\n");
        for (int i = 0; i < cg->comp->nstrings; i++) {
            emit(cg, "%s:\n", cg->comp->string_labels[i]);
            fputs("    .asciz ", cg->out);
            emit_escaped_string(cg, cg->comp->string_values[i]);
            fputc('\n', cg->out);
        }
    }

    emit(cg, "\n.stack 4096\n");
}

int codegen_init(CodeGen *cg, Compiler *comp, FILE *out) {
    memset(cg, 0, sizeof(*cg));
    cg->comp = comp;
    cg->out = out;
    regalloc_init(&cg->ra);
    cg->tmp_counter = 0;
    cg->break_label = NULL;
    cg->continue_label = NULL;
    return 0;
}

void codegen_close(CodeGen *cg) {
    (void)cg;
}
