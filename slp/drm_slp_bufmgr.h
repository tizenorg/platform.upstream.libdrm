/**************************************************************************

xserver-xorg-video-sec

Copyright 2011 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: SooChan Lim <sc1.lim@samsung.com>, Sangjin Lee <lsj119@samsung.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#ifndef _DRM_SLP_BUFMGR_H_
#define _DRM_SLP_BUFMGR_H_

#include <semaphore.h>
#include <pthread.h>
#include <xf86drm.h>

typedef struct _drm_slp_bo * drm_slp_bo;
typedef struct _drm_slp_bufmgr * drm_slp_bufmgr;

struct list_head
{
    struct list_head *prev;
    struct list_head *next;
};

struct _drm_slp_bo
{
    struct list_head list;
    drm_slp_bufmgr bufmgr;
    int ref_cnt;		/*atomic count*/
    void *user_data;

    /* private data */
    void *priv;
};

typedef enum
{
    STATUS_UNLOCK,
    STATUS_READY_TO_LOCK,
    STATUS_LOCK,
} lock_status;

struct _drm_slp_bufmgr
{
    struct list_head bos;  /*list head of bo*/

    pthread_mutex_t lock;
    struct {
        int isOpened;
        lock_status status;
        sem_t* handle;
    } semObj;

    void         (*bufmgr_destroy)(drm_slp_bufmgr bufmgr);
    int          (*bufmgr_cache_flush)(drm_slp_bufmgr bufmgr, drm_slp_bo bo, int flags);

    int          (*bo_size)(drm_slp_bo bo);

    void         (*bo_free)(drm_slp_bo bo);
    int          (*bo_alloc)(drm_slp_bo bo,
                                        const char*    name,
                                        int            size,
                                        int flags);
    int          (*bo_attach)(drm_slp_bo bo,
                                        const char*    name,
                                        int          type,
                                        int            size,
                                        unsigned int          handle);
    int          (*bo_import)(drm_slp_bo bo, unsigned int key);
    unsigned int (*bo_export)(drm_slp_bo bo);

    unsigned int (*bo_get_handle)(drm_slp_bo bo, int device);
    unsigned int        (*bo_map)(drm_slp_bo bo, int device, int opt);
    int          (*bo_unmap)(drm_slp_bo bo, int device);


    /* Padding for future extension */
    int (*bufmgr_lock) (drm_slp_bufmgr bufmgr);
    int (*bufmgr_unlock) (drm_slp_bufmgr bufmgr);
    int (*bo_lock) (drm_slp_bo bo, unsigned int checkOnly, unsigned int* isLocked);
    int (*bo_unlock) (drm_slp_bo bo);
    void (*reserved5) (void);
    void (*reserved6) (void);

    /* private data */
    void *priv;

    struct list_head link;  /*link of bufmgr*/

    int drm_fd;
    int ref_count;
};

/* DRM_SLP_MEM_TYPE */
#define DRM_SLP_MEM_GEM             0
#define DRM_SLP_MEM_USERPTR     1
#define DRM_SLP_MEM_DMABUF      2
#define DRM_SLP_MEM_GPU             3

/* DRM_SLP_DEVICE_TYPE */
#define DRM_SLP_DEVICE_DEFAULT   0  //Default handle
#define DRM_SLP_DEVICE_CPU 1
#define DRM_SLP_DEVICE_2D   2
#define DRM_SLP_DEVICE_3D   3
#define DRM_SLP_DEVICE_MM  4

/* DRM_SLP_OPTION */
#define DRM_SLP_OPTION_READ	(1 << 0)
#define DRM_SLP_OPTION_WRITE	(1 << 1)

/* DRM_SLP_CACHE */
#define DRM_SLP_CACHE_INV   0x01
#define DRM_SLP_CACHE_CLN   0x02
#define DRM_SLP_CACHE_ALL   0x10
#define DRM_SLP_CACHE_FLUSH     (DRM_SLP_CACHE_INV|DRM_SLP_CACHE_CLN)
#define DRM_SLP_CACHE_FLUSH_ALL (DRM_SLP_CACHE_FLUSH|DRM_SLP_CACHE_ALL)

enum DRM_SLP_BO_FLAGS{
    DRM_SLP_BO_DEFAULT = 0,
    DRM_SLP_BO_SCANOUT = (1<<0),
    DRM_SLP_BO_NONCACHABLE = (1<<1),
    DRM_SLP_BO_WC = (1<<2),
};

/* Functions for buffer mnager */
drm_slp_bufmgr
drm_slp_bufmgr_init(int fd, void * arg);
void
drm_slp_bufmgr_destroy(drm_slp_bufmgr bufmgr);
int
drm_slp_bufmgr_lock(drm_slp_bufmgr bufmgr);
int
drm_slp_bufmgr_unlock(drm_slp_bufmgr bufmgr);
int
drm_slp_bufmgr_cache_flush(drm_slp_bufmgr bufmgr, drm_slp_bo bo, int flags);


/*Functions for bo*/
int
drm_slp_bo_size (drm_slp_bo bo);
drm_slp_bo
drm_slp_bo_ref(drm_slp_bo bo);
void
drm_slp_bo_unref(drm_slp_bo bo);
drm_slp_bo
drm_slp_bo_alloc(drm_slp_bufmgr bufmgr,
                            const char* name,
                            int size,
                            int flags);
drm_slp_bo
drm_slp_bo_attach(drm_slp_bufmgr bufmgr,
                             const char*    name,
                             int type,
                             int size,
                             unsigned int handle);
drm_slp_bo
drm_slp_bo_import(drm_slp_bufmgr bufmgr, unsigned int key);
unsigned int
drm_slp_bo_export(drm_slp_bo bo);
unsigned int
drm_slp_bo_get_handle(drm_slp_bo, int device);
unsigned int
drm_slp_bo_map(drm_slp_bo bo, int device, int opt);
int
drm_slp_bo_unmap(drm_slp_bo bo, int device);
int
drm_slp_bo_swap(drm_slp_bo bo1, drm_slp_bo bo2);

/*Functions for userdata of bo*/
typedef void	(*drm_data_free)(void *);
int
drm_slp_bo_add_user_data(drm_slp_bo bo, unsigned long key, drm_data_free data_free_func);
int
drm_slp_bo_delete_user_data(drm_slp_bo bo, unsigned long key);
int
drm_slp_bo_set_user_data(drm_slp_bo bo, unsigned long key, void* data);
int
drm_slp_bo_get_user_data(drm_slp_bo bo, unsigned long key, void** data);
#endif /* _DRM_SLP_BUFMGR_H_ */
