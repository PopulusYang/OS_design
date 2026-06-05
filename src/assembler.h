/*
 * assembler.h
 * 两遍汇编器：把 .s 源文件编译为 UPX 可执行格式。
 */
#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#ifdef __cplusplus
extern "C" {
#endif

// 汇编源文件并写出 UPX 可执行文件
int assemble_file(const char *source_path, const char *output_path);

#ifdef __cplusplus
}
#endif

#endif
