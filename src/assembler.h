// assembler.h —— UPFS 两遍汇编器
//
// 将 UPFS VM 汇编源码 (.s) 翻译为 .upx 可执行文件。
// 作为 shell 内建命令 asm 的后端。

#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#ifdef __cplusplus
extern "C" {
#endif

// 将 source_path 处的汇编源码汇编为 .upx 文件，写入 output_path。
// 成功返回 0，失败返回 -1 并向 stderr 打印错误。
int assemble_file(const char *source_path, const char *output_path);

#ifdef __cplusplus
}
#endif

#endif // ASSEMBLER_H
