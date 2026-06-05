/*
 * c2s.c
 * 编译流水线：词法分析、语法分析、代码生成，输出 .s 汇编。
 */
#include "compiler/c2s.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/codegen.h"
#include "compiler/ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int compile_c_to_asm(const char *src_path, const char *out_path) {
    Compiler comp;
    Lexer lex;
    Parser parser;
    CodeGen cg;
    ASTNode *root = NULL;
    FILE *out = NULL;
    int ret = -1;

    compiler_init(&comp);

    if (lexer_init(&lex, src_path) != 0) {
        fprintf(stderr, "c2s: failed to open source file '%s'\n", src_path);
        goto cleanup;
    }

    parser_init(&parser, &lex, &comp);
    root = parser_parse(&parser);
    if (!root) {
        fprintf(stderr, "c2s: parse failed\n");
        parser_close(&parser);
        goto cleanup;
    }
    parser_close(&parser);
    lexer_close(&lex);

    out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "c2s: cannot create output file '%s'\n", out_path);
        goto cleanup;
    }

    codegen_init(&cg, &comp, out);
    codegen_emit(&cg, root);
    codegen_close(&cg);
    fclose(out);
    out = NULL;

    ret = 0;

cleanup:
    if (out) fclose(out);
    if (root) ast_free(root);
    compiler_free(&comp);
    return ret;
}
