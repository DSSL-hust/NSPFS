#ifndef _USERFS_INTERFACE_H_
#define _USERFS_INTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

void init_fs(void);
void shutdown_fs(void);
void synclog(void);
//logs

extern unsigned char initialized;

//utils
int bms_search(char *txt, char *pat);

#ifdef __cplusplus
}
#endif

#endif
