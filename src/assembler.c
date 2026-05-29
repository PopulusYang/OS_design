#include "assembler.h"
#include "kernel/cpu.h"       

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>



#define MAX_LINES    4096
#define MAX_LABELS   512
#define MAX_LINE_LEN 512
#define MAX_TOKENS   8
#define MAX_CODE     (64 * 1024)   
#define MAX_DATA     (64 * 1024)   


enum Pat {
    P_NONE,       
    P_R_R_R,      
    P_R_I,        
    P_R_R,        
    P_LD,         
    P_ST,         
    P_CMP,        
    P_PUSH,       
    P_JUMP,       
    P_SYSCALL,    
};

typedef struct {
    const char *name;
    uint8_t     opcode;
    int         pattern;
} InsnDef;


static const InsnDef INSNS[] = {
    {"HALT",    OP_HALT,    P_NONE},
    {"MOVI",    OP_MOVI,    P_R_I},
    {"MOV",     OP_MOV,     P_R_R},
    {"LD",      OP_LD,      P_LD},
    {"ST",      OP_ST,      P_ST},
    {"ADD",     OP_ADD,     P_R_R_R},
    {"SUB",     OP_SUB,     P_R_R_R},
    {"MUL",     OP_MUL,     P_R_R_R},
    {"DIV",     OP_DIV,     P_R_R_R},
    {"AND",     OP_AND,     P_R_R_R},
    {"OR",      OP_OR,      P_R_R_R},
    {"XOR",     OP_XOR,     P_R_R_R},
    {"CMP",     OP_CMP,     P_CMP},
    {"JMP",     OP_JMP,     P_JUMP},
    {"JZ",      OP_JZ,      P_JUMP},
    {"JNZ",     OP_JNZ,     P_JUMP},
    {"CALL",    OP_CALL,    P_JUMP},
    {"RET",     OP_RET,     P_NONE},
    {"PUSH",    OP_PUSH,    P_PUSH},
    {"POP",     OP_POP,     P_R_R},
    {"SYSCALL", OP_SYSCALL, P_SYSCALL},
    {"LUI",     OP_LUI,     P_R_I},
};


typedef struct {
    char name[64];
    int  addr;      
    int  section;   
} Label;


typedef struct {
    
    char  *lines[MAX_LINES];
    int    nlines;

    
    Label labels[MAX_LABELS];
    int   nlabels;

    
    int text_size;   
    int data_size;   
    int bss_size;    
    int stack_size;  
    int entry_idx;   

    
    uint8_t code[MAX_CODE];
    uint8_t data[MAX_DATA];
} Asm;




#ifdef __GNUC__
__attribute__((format(printf, 3, 4)))
#endif
static void asm_error_v(int lineno, const char *line,
                        const char *fmt, ...) {
    fprintf(stderr, "\033[1;31masm:%d:\033[0m ", lineno);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (line && *line) {
        
        char trimmed[120];
        strncpy(trimmed, line, sizeof(trimmed) - 1);
        trimmed[sizeof(trimmed) - 1] = '\0';
        
        char *p = trimmed;
        while (*p == ' ' || *p == '\t') p++;
        fprintf(stderr, "\n  \033[2m> %s\033[0m", *p ? p : trimmed);
    }
    fprintf(stderr, "\n");
}


static int read_all_lines(Asm *a, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "asm: cannot open '%s': %s\n", path, strerror(errno)); return -1; }
    char buf[MAX_LINE_LEN];
    a->nlines = 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (a->nlines >= MAX_LINES) { fprintf(stderr, "asm: too many lines\n"); fclose(f); return -1; }
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
        if (len > 0 && buf[len-1] == '\r') buf[--len] = '\0';
        
        char *cmt = strchr(buf, ';');
        if (!cmt) cmt = strstr(buf, "//");
        if (cmt) *cmt = '\0';
        a->lines[a->nlines] = strdup(buf);
        a->nlines++;
    }
    fclose(f);
    return 0;
}



static int tokenize(char *line, char *tokens[], int max_tok) {
    int n = 0;
    char *p = line;
    while (*p && n < max_tok) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\r'
               || *p == '[' || *p == ']' || *p == '+') p++;  
        if (!*p) break;
        tokens[n++] = p;
        if (*p == '"') {
            
            p++; 
            while (*p && *p != '"') {
                if (*p == '\\' && *(p+1)) p++; 
                p++;
            }
            if (*p == '"') p++; 
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != '\r'
                   && *p != '[' && *p != ']' && *p != '+') p++;
        }
        if (*p) { *p = '\0'; p++; }
    }
    return n;
}


static char *label_name(char *tok) {
    size_t len = strlen(tok);
    if (len > 1 && tok[len-1] == ':') { tok[len-1] = '\0'; return tok; }
    return NULL;
}


static int parse_reg(const char *s) {
    if (!s || !*s) return -1;
    if (strcasecmp(s, "SP") == 0) return 15;
    if (toupper(s[0]) == 'R') {
        
        char *end;
        long v = strtol(s + 1, &end, 10);
        if (*end == '\0' && v >= 0 && v <= 15) return (int)v;
    }
    return -1;
}


static int parse_imm(const char *s, int *out) {
    if (!s || !*s) return -1;
    if (s[0] == '\'' && s[1] && s[2] == '\'') { *out = (int)(unsigned char)s[1]; return 0; }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        { *out = (int)strtol(s + 2, NULL, 16); return 0; }
    if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
        { *out = (int)strtol(s + 2, NULL, 2);  return 0; }
    char *end;
    long v = strtol(s, &end, 0);
    if (*end) return -1;
    *out = (int)v;
    return 0;
}


static int is_symbol(const char *s) {
    if (!s || !*s) return 0;
    if (isdigit(s[0]) || s[0] == '-' || s[0] == '\'' || s[0] == '0') return 0;
    if (parse_reg(s) >= 0) return 0;
    return 1;
}


static int find_label(Asm *a, const char *name) {
    for (int i = 0; i < a->nlabels; i++)
        if (strcmp(a->labels[i].name, name) == 0) return a->labels[i].addr;
    return -1;
}


static int find_label_abs(Asm *a, const char *name) {
    for (int i = 0; i < a->nlabels; i++) {
        if (strcmp(a->labels[i].name, name) == 0) {
            if (a->labels[i].section == 0)
                return a->labels[i].addr * 4;  
            else {
                
                int tp = (a->text_size + 4095) / 4096;
                if (tp == 0) tp = 1;
                return tp * 4096 + a->labels[i].addr;
            }
        }
    }
    return -1;
}


static const InsnDef *find_insn(const char *name) {
    for (size_t i = 0; i < sizeof(INSNS)/sizeof(INSNS[0]); i++)
        if (strcasecmp(INSNS[i].name, name) == 0) return &INSNS[i];
    return NULL;
}


static uint32_t encode(uint8_t op, uint8_t rd, uint8_t rs1, uint8_t rs2, int16_t imm12) {
    return ((uint32_t)op << 24) | ((uint32_t)(rd & 0xF) << 20)
         | ((uint32_t)(rs1 & 0xF) << 16) | ((uint32_t)(rs2 & 0xF) << 12)
         | (uint32_t)(imm12 & 0xFFF);
}

static void emit32(uint8_t *buf, int *pos, uint32_t v) {
    buf[(*pos)++] = (uint8_t)(v);
    buf[(*pos)++] = (uint8_t)(v >> 8);
    buf[(*pos)++] = (uint8_t)(v >> 16);
    buf[(*pos)++] = (uint8_t)(v >> 24);
}



static int pass1(Asm *a) {
    int section = 0;     
    int text_insns = 0;  
    int lineno = 0;

    a->data_size = 0;
    a->bss_size  = 0;
    a->stack_size = 4096;  
    a->entry_idx = -1;

    for (int i = 0; i < a->nlines; i++) {
        lineno = i + 1;
        char *line = a->lines[i];
        
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;

        char tmp_line[MAX_LINE_LEN];
        strncpy(tmp_line, line, MAX_LINE_LEN - 1);
        tmp_line[MAX_LINE_LEN - 1] = '\0';
        char *tokens[MAX_TOKENS];
        int nt = tokenize(tmp_line, tokens, MAX_TOKENS);
        if (nt == 0) continue;

        
        char *lbl = label_name(tokens[0]);
        if (lbl) {
            if (a->nlabels >= MAX_LABELS) { asm_error_v(lineno, line, "too many labels (max %d)", MAX_LABELS); return -1; }
            strncpy(a->labels[a->nlabels].name, lbl, 63);
            a->labels[a->nlabels].section = section;
            a->labels[a->nlabels].addr = (section == 0) ? text_insns : a->data_size;
            a->nlabels++;
            
            if (nt == 1) continue;
            tokens[0] = tokens[1];
            for (int j = 1; j < nt; j++) tokens[j-1] = tokens[j];
            nt--;
        }

        
        const char *dir = tokens[0];

        if (dir[0] == '.') {
            
            if (strcmp(dir, ".text") == 0) { section = 0; }
            else if (strcmp(dir, ".data") == 0) { section = 1; }
            else if (strcmp(dir, ".bss") == 0) { section = 2; }
            else if (strcmp(dir, ".entry") == 0) {
                
                if (nt < 2) { asm_error_v(lineno, line, "missing argument, .entry requires a label name"); return -1; }
            }
            else if (strcmp(dir, ".word") == 0) {
                if (nt < 2) { asm_error_v(lineno, line, ".word requires a numeric value"); return -1; }
                int v;
                if (parse_imm(tokens[1], &v) < 0) {
                    
                }
                a->data_size += 4;
            }
            else if (strcmp(dir, ".space") == 0) {
                if (nt < 2) { asm_error_v(lineno, line, ".space requires a byte count"); return -1; }
                int sz = atoi(tokens[1]);
                if (section == 2) a->bss_size += sz;
                else a->data_size += sz;   
            }
            else if (strcmp(dir, ".stack") == 0) {
                if (nt < 2) { asm_error_v(lineno, line, ".stack requires a byte count"); return -1; }
                a->stack_size = atoi(tokens[1]);
            }
            else if (strcmp(dir, ".ascii") == 0 || strcmp(dir, ".asciz") == 0) {
                char *s = tokens[1];
                if (s && s[0] == '"') {
                    s++;
                    char *end = strchr(s, '"');
                    if (end) *end = '\0';
                    
                    int real = 0;
                    for (char *c = s; *c; c++) {
                        if (*c == '\\' && *(c+1)) { c++; real++; }
                        else real++;
                    }
                    a->data_size += real + (strcmp(dir, ".asciz") == 0 ? 1 : 0);
                } else {
                    asm_error_v(lineno, line, "%s requires a string literal (\"...\")", dir); return -1;
                }
            }
            else {
                asm_error_v(lineno, line, "unknown directive '%s'", dir); return -1;
            }
        } else {
            
            if (section != 0) {
                asm_error_v(lineno, line, "instruction must be in .text section"); return -1;
            }
            const InsnDef *ins = find_insn(dir);
            if (!ins) {
                asm_error_v(lineno, line, "unknown instruction '%s'", dir); return -1;
            }
            text_insns++;
        }
    }

    a->text_size = text_insns * 4;
    if (a->text_size > MAX_CODE) { fprintf(stderr, "asm: code segment too large\n"); return -1; }
    if (a->data_size > MAX_DATA) { fprintf(stderr, "asm: data segment too large\n"); return -1; }
    return 0;
}



static int pass2(Asm *a) {
    int section = 0;
    int text_pos = 0;  
    int data_pos = 0;  
    int ip = 0;        
    int lineno = 0;
    char entry_label[64] = "";

    for (int i = 0; i < a->nlines; i++) {
        lineno = i + 1;
        char *line = a->lines[i];      
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;

        char tline[MAX_LINE_LEN];
        strncpy(tline, line, MAX_LINE_LEN - 1);
        tline[MAX_LINE_LEN - 1] = '\0';
        char *tokens[MAX_TOKENS];
        int nt = tokenize(tline, tokens, MAX_TOKENS);
        if (nt == 0) continue;

        
        char *lbl = label_name(tokens[0]);
        if (lbl) {
            if (nt == 1) continue;
            tokens[0] = tokens[1];
            for (int j = 1; j < nt; j++) tokens[j-1] = tokens[j];
            nt--;
        }

        const char *dir = tokens[0];

        if (dir[0] == '.') {
            if (strcmp(dir, ".text") == 0) { section = 0; }
            else if (strcmp(dir, ".data") == 0) { section = 1; }
            else if (strcmp(dir, ".bss") == 0) { section = 2; }
            else if (strcmp(dir, ".entry") == 0) {
                strncpy(entry_label, tokens[1], 63);
            }
            else if (strcmp(dir, ".word") == 0) {
                int v = 0;
                if (parse_imm(tokens[1], &v) < 0) v = 0;  
                emit32(a->data, &data_pos, (uint32_t)v);
            }
            else if (strcmp(dir, ".space") == 0) {
                int sz = atoi(tokens[1]);
                if (section == 1) { memset(a->data + data_pos, 0, (size_t)sz); data_pos += sz; }
            }
            else if (strcmp(dir, ".ascii") == 0 || strcmp(dir, ".asciz") == 0) {
                char *s = tokens[1];
                if (s && s[0] == '"') {
                    s++;
                    char *end = strchr(s, '"');
                    if (end) *end = '\0';
                    for (char *c = s; *c; c++) {
                        if (*c == '\\' && *(c+1)) {
                            c++;
                            switch (*c) {
                                case 'n':  a->data[data_pos++] = '\n'; break;
                                case 'r':  a->data[data_pos++] = '\r'; break;
                                case 't':  a->data[data_pos++] = '\t'; break;
                                case '0':  a->data[data_pos++] = '\0'; break;
                                case '\\': a->data[data_pos++] = '\\'; break;
                                case '"':  a->data[data_pos++] = '"';  break;
                                default:   a->data[data_pos++] = *c;   break;
                            }
                        } else {
                            a->data[data_pos++] = *c;
                        }
                    }
                    if (strcmp(dir, ".asciz") == 0) { a->data[data_pos++] = '\0'; }
                }
            }
        } else {
            const InsnDef *ins = find_insn(dir);
            if (!ins) continue;

            uint8_t rd = 0, rs1 = 0, rs2 = 0;
            int16_t imm12 = 0;

            switch (ins->pattern) {
            case P_NONE:
                break;

            case P_R_I:
                
                rd = (uint8_t)parse_reg(tokens[1]);
                if (rd == (uint8_t)-1) { asm_error_v(lineno, line, "invalid register"); return -1; }
                if (is_symbol(tokens[2])) {
                    int addr = find_label_abs(a, tokens[2]);
                    if (addr < 0) { asm_error_v(lineno, line, "undefined label '%s'", tokens[2]); return -1; }
                    if (ins->opcode == OP_LUI)
                        imm12 = (int16_t)(addr >> 12);
                    else
                        imm12 = (int16_t)(addr & 0xFFF);
                } else {
                    int v;
                    if (parse_imm(tokens[2], &v) < 0) { asm_error_v(lineno, line, "invalid immediate '%s'", tokens[2]); return -1; }
                    imm12 = (int16_t)v;
                }
                break;

            case P_R_R:
                
                rd = (uint8_t)parse_reg(tokens[1]);
                if (rd == (uint8_t)-1) { asm_error_v(lineno, line, "invalid register"); return -1; }
                if (ins->opcode == OP_POP) {
                    
                } else {
                    rs1 = (uint8_t)parse_reg(tokens[2]);
                    if (rs1 == (uint8_t)-1) { asm_error_v(lineno, line, "invalid register"); return -1; }
                }
                break;

            case P_R_R_R:
                rd  = (uint8_t)parse_reg(tokens[1]);
                rs1 = (uint8_t)parse_reg(tokens[2]);
                rs2 = (uint8_t)parse_reg(tokens[3]);
                if (rd == (uint8_t)-1 || rs1 == (uint8_t)-1 || rs2 == (uint8_t)-1) { asm_error_v(lineno, line, "invalid register"); return -1; }
                break;

            case P_LD:
                
                rd = (uint8_t)parse_reg(tokens[1]);
                if (rd == (uint8_t)-1) { asm_error_v(lineno, line, "invalid register"); return -1; }
                if (parse_reg(tokens[2]) >= 0) {
                    
                    rs1 = (uint8_t)parse_reg(tokens[2]);
                    if (is_symbol(tokens[3])) {
                        int addr = find_label_abs(a, tokens[3]);
                        if (addr < 0) { asm_error_v(lineno, line, "undefined label '%s'", tokens[3]); return -1; }
                        imm12 = (int16_t)addr;
                    } else {
                        int v;
                        if (parse_imm(tokens[3], &v) < 0) v = 0;
                        imm12 = (int16_t)v;
                    }
                } else if (is_symbol(tokens[2])) {
                    
                    int addr = find_label_abs(a, tokens[2]);
                    if (addr < 0) { asm_error_v(lineno, line, "undefined label '%s'", tokens[2]); return -1; }
                    rs1 = 0; imm12 = (int16_t)addr;
                } else {
                    asm_error_v(lineno, line, "bad LD operand, expected: LD Rd, Rs, [off] or LD Rd, [label]"); return -1;
                }
                break;

            case P_ST:
                
                rs2 = (uint8_t)parse_reg(tokens[1]);
                if (rs2 == (uint8_t)-1) { asm_error_v(lineno, line, "invalid register"); return -1; }
                if (parse_reg(tokens[2]) >= 0) {
                    
                    rs1 = (uint8_t)parse_reg(tokens[2]);
                    if (is_symbol(tokens[3])) {
                        int addr = find_label_abs(a, tokens[3]);
                        if (addr < 0) { asm_error_v(lineno, line, "undefined label '%s'", tokens[3]); return -1; }
                        imm12 = (int16_t)addr;
                    } else {
                        int v;
                        if (parse_imm(tokens[3], &v) < 0) v = 0;
                        imm12 = (int16_t)v;
                    }
                } else if (is_symbol(tokens[2])) {
                    
                    int addr = find_label_abs(a, tokens[2]);
                    if (addr < 0) { asm_error_v(lineno, line, "undefined label '%s'", tokens[2]); return -1; }
                    rs1 = 0; imm12 = (int16_t)addr;
                } else {
                    asm_error_v(lineno, line, "bad ST operand, expected: ST Rs, Rb, [off] or ST Rs, [label]"); return -1;
                }
                break;

            case P_CMP:
                rs1 = (uint8_t)parse_reg(tokens[1]);
                rs2 = (uint8_t)parse_reg(tokens[2]);
                if (rs1 == (uint8_t)-1 || rs2 == (uint8_t)-1) { asm_error_v(lineno, line, "invalid register"); return -1; }
                break;

            case P_PUSH:
                rs1 = (uint8_t)parse_reg(tokens[1]);
                if (rs1 == (uint8_t)-1) { asm_error_v(lineno, line, "invalid register"); return -1; }
                break;

            case P_JUMP: {
                
                
                int target;
                if (is_symbol(tokens[1])) {
                    target = find_label(a, tokens[1]);
                    if (target < 0) { asm_error_v(lineno, line, "undefined label '%s'", tokens[1]); return -1; }
                } else if (parse_imm(tokens[1], &target) == 0) {
                    imm12 = (int16_t)(target - ip);
                    break;
                } else {
                    asm_error_v(lineno, line, "invalid jump target '%s'", tokens[1]); return -1;
                }
                int16_t rel = (int16_t)(target - ip);
                imm12 = rel;
                break;
            }

            case P_SYSCALL:
                if (is_symbol(tokens[1])) {
                    asm_error_v(lineno, line, "SYSCALL requires immediate number, got label '%s'", tokens[1]); return -1;
                }
                { int v; if (parse_imm(tokens[1], &v) < 0) { asm_error_v(lineno, line, "invalid syscall number '%s'", tokens[1]); return -1; }
                  imm12 = (int16_t)v; }
                break;
            }

            uint32_t enc = encode(ins->opcode, rd, rs1, rs2, imm12);
            emit32(a->code, &text_pos, enc);
            ip++;
        }
    }

    
    if (entry_label[0]) {
        a->entry_idx = find_label(a, entry_label);
        if (a->entry_idx < 0) { fprintf(stderr, "asm: entry label '%s' not found\n", entry_label); return -1; }
    } else {
        a->entry_idx = 0;  
    }

    return 0;
}



static int write_upx(Asm *a, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "asm: cannot create '%s': %s\n", path, strerror(errno)); return -1; }

    
    fwrite("UPX\0", 1, 4, f);
    uint32_t entry = (uint32_t)a->entry_idx;
    uint32_t text_sz = (uint32_t)a->text_size;
    uint32_t data_sz = (uint32_t)a->data_size;
    uint32_t bss_sz  = (uint32_t)a->bss_size;
    uint32_t stk_sz  = (uint32_t)a->stack_size;
    fwrite(&entry,  4, 1, f);
    fwrite(&text_sz, 4, 1, f);
    fwrite(&data_sz, 4, 1, f);
    fwrite(&bss_sz,  4, 1, f);
    fwrite(&stk_sz,  4, 1, f);
    
    fwrite(a->code, 1, (size_t)a->text_size, f);
    fwrite(a->data, 1, (size_t)a->data_size, f);

    fclose(f);
    return 0;
}



int assemble_file(const char *source_path, const char *output_path) {
    Asm a;
    memset(&a, 0, sizeof(a));

    if (read_all_lines(&a, source_path) < 0) return -1;
    if (pass1(&a) < 0) goto cleanup;
    if (pass2(&a) < 0) goto cleanup;
    if (write_upx(&a, output_path) < 0) goto cleanup;

    
    printf("  text: %d bytes (%d insns) | data: %d | bss: %d | stack: %d\n",
           a.text_size, a.text_size / 4, a.data_size, a.bss_size, a.stack_size);
    printf("  entry: instruction %d\n", a.entry_idx);

cleanup:
    for (int i = 0; i < a.nlines; i++) free(a.lines[i]);
    return (a.text_size > 0 || a.data_size > 0) ? 0 : -1;
}
