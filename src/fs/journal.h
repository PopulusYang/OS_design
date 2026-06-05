/*
 * journal.h
 * 元数据日志：事务开始、写日志、提交与挂载时重放，保证崩溃一致性。
 */
#ifndef JOURNAL_H
#define JOURNAL_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JOURNAL_MAGIC       0x4A524E4CU

#define JTXN_BEGIN          1U
#define JTXN_LOG            2U
#define JTXN_COMMIT         3U

#define JOURNAL_MAX_LOG     28

// 格式化时清零日志区并写入空日志头
int  journal_init_format(void);
// 挂载时重放已提交但未清理的日志记录
int  journal_replay(void);
// 开始新事务，递增序列号
int  journal_begin(void);
// 在当前事务中记录一块的完整副本
int  journal_log_block(int blockno, const void *data);
// 先写日志再回写目标块，最后更新日志头
int  journal_commit(void);
// 放弃当前事务，丢弃内存中的日志记录
void journal_abort(void);
// 写超级块、锚块、inode 块等元数据
int  journal_write_metadata(int blockno, const void *data);
// 写目录数据块，走日志保证一致性
int  journal_write_dir_block(int blockno, const void *data);
// 判断块号是否属于需日志保护的元数据
int  journal_is_metadata_block(int blockno);

#ifdef __cplusplus
}
#endif

#endif
