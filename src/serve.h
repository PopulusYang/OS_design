




#ifndef SERVE_H
#define SERVE_H

#ifdef __cplusplus
extern "C" {
#endif


int serve_main(int port);

int upfs_api_session(int in_fd, int out_fd);

const char *shared_disk_path(void);
void        shared_set_disk(const char *path);

#ifdef __cplusplus
}
#endif

#endif 
