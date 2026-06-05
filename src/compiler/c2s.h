/*
 * c2s.h
 * 编译器入口：把 .c 源文件编译成 .s 汇编文件。
 */
#ifndef COMPILER_C2S_H
#define COMPILER_C2S_H

#ifdef __cplusplus
extern "C" {
#endif

int compile_c_to_asm(const char *src_path, const char *out_path);

#ifdef __cplusplus
}
#endif

#endif
