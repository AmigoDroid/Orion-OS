
#ifndef ORION_FS_H
#define ORION_FS_H

/* OrionFS v2 - ver docs/ORIONFS.md */

int fs_mount(void);
int fs_format(void);
void fs_list(void);
int fs_create(const char *name, unsigned char uid);
int fs_write(const char *name, const char *content);
int fs_read(const char *name);
int fs_remove(const char *name);

#endif
