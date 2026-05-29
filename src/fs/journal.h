
#ifndef JOURNAL_H
#define JOURNAL_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JOURNAL_MAGIC       0x4A524E4CU  /* "JRNL" */

#define JTXN_BEGIN          1U
#define JTXN_LOG            2U
#define JTXN_COMMIT         3U

#define JOURNAL_MAX_LOG     28

int  journal_init_format(void);
int  journal_replay(void);
int  journal_begin(void);
int  journal_log_block(int blockno, const void *data);
int  journal_commit(void);
void journal_abort(void);
int  journal_write_metadata(int blockno, const void *data);
int  journal_write_dir_block(int blockno, const void *data);
int  journal_is_metadata_block(int blockno);

#ifdef __cplusplus
}
#endif

#endif
