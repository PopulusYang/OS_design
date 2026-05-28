// c2s.h —— 编译器驱动接口

#ifndef COMPILER_C2S_H
#define COMPILER_C2S_H

#ifdef __cplusplus
extern "C" {
#endif

// 编译 C 源码到汇编
// src_path:  .c 文件路径
// out_path:  .s 输出文件路径
// 返回 0 成功，-1 失败
int compile_c_to_asm(const char *src_path, const char *out_path);

#ifdef __cplusplus
}
#endif

#endif // COMPILER_C2S_H
