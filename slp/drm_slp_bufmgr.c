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

#include "config.h"

#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

#include "drm_slp_bufmgr.h"
#include "list.h"

#define PREFIX_LIB    "libdrm_slp_"
#define SUFFIX_LIB    ".so"
#define DEFAULT_LIB  PREFIX_LIB"default"SUFFIX_LIB

#define NUM_TRY_LOCK	10
#define SEM_NAME		"pixmap_1"
#define SEM_DEBUG 0

#define DRM_RETURN_IF_FAIL(cond)          {if (!(cond)) { fprintf (stderr, "[%s] : '%s' failed.\n", __FUNCTION__, #cond); return; }}
#define DRM_RETURN_VAL_IF_FAIL(cond, val) {if (!(cond)) { fprintf (stderr, "[%s] : '%s' failed.\n", __FUNCTION__, #cond); return val; }}

#define MGR_IS_VALID(mgr) (mgr && \
                                                mgr->link.next && \
                                                mgr->link.next->prev == &mgr->link)
#define BO_IS_VALID(bo) (bo && \
                                            MGR_IS_VALID(bo->bufmgr) && \
                                            bo->list.next && \
                                            bo->list.next->prev == &bo->list)

typedef struct{
	void* data;

	int is_valid;
	drm_data_free free_func ;
}drm_slp_user_data;

static struct list_head *gBufMgrs = NULL;

static int
_sem_wait_wrapper(sem_t* sem)
{
	int res = 0;
	int num_try = NUM_TRY_LOCK;

	do
	{
		res = sem_wait(sem);
		num_try--;
	} while((res == -1) && (errno == EINTR) && (num_try >= 0));

	if(res == -1)
	{
		fprintf(stderr,
				"[libdrm] error %s:%d(sem:%p, num_try:%d) PID:%04d\n",
				__FUNCTION__,
				__LINE__,
				sem,
				num_try,
				getpid());
		return 0;
	}
#if SEM_DEBUG
	else
	{
		fprintf(stderr,
				"[libdrm]   LOCK >> %s:%d(sem:%p, num_try:%d) PID:%04d\n",
				__FUNCTION__,
				__LINE__,
				sem,
				num_try,
				getpid());
	}
#endif

	return 1;
}

static int
_sem_post_wrapper(sem_t* sem)
{
	int res = 0;
	int num_try = NUM_TRY_LOCK;

	do
	{
		res = sem_post(sem);
		num_try--;

	} while((res == -1) && (errno == EINTR) && (num_try >= 0));

	if(res == -1)
	{
		fprintf(stderr,
				"[libdrm] error %s:%d(sem:%p, num_try:%d) PID:%04d\n",
				__FUNCTION__,
				__LINE__,
				sem,
				num_try,
				getpid());
		return 0;
	}
#if SEM_DEBUG
	else
	{
		fprintf(stderr,
				"[libdrm] UNLOCK << %s:%d(sem:%p, num_try:%d) PID:%04d\n",
				__FUNCTION__,
				__LINE__,
				sem,
				num_try,
				getpid());
	}
#endif

	return 1;
}

static int
_sem_open(drm_slp_bufmgr bufmgr)
{
	bufmgr->semObj.handle = sem_open(SEM_NAME, O_CREAT, 0777, 1);
	if(bufmgr->semObj.handle == SEM_FAILED)
	{
		fprintf(stderr,
				"[libdrm] error %s:%d(name:%s) PID:%04d\n",
				__FUNCTION__,
				__LINE__,
				SEM_NAME,
				getpid());
		bufmgr->semObj.handle = NULL;
		return 0;
	}
#if SEM_DEBUG
	else
	{
		fprintf(stderr,
				"[libdrm] OPEN %s:%d(sem:%p) PID:%04d\n",
				__FUNCTION__,
				__LINE__,
				bufmgr->semObj.handle,
				getpid());
	}
#endif

	bufmgr->semObj.status = STATUS_UNLOCK;

	return 1;
}

static int
_sem_close(drm_slp_bufmgr bufmgr)
{
	_sem_wait_wrapper(bufmgr->semObj.handle);
	sem_unlink(SEM_NAME);
	return 1;
}

static int
_sem_lock(drm_slp_bufmgr bufmgr)
{
	if(bufmgr->semObj.status != STATUS_UNLOCK) return 0;

	if(!_sem_wait_wrapper(bufmgr->semObj.handle)) return 0;
	bufmgr->semObj.status = STATUS_LOCK;
	return 1;
}

static int
_sem_unlock(drm_slp_bufmgr bufmgr)
{
	if(bufmgr->semObj.status != STATUS_LOCK) return 0;

	_sem_post_wrapper(bufmgr->semObj.handle);
	bufmgr->semObj.status = STATUS_UNLOCK;
	return 1;
}

static drm_slp_bufmgr
_load_bufmgr(int fd, const char *file, void *arg)
{
	char path[PATH_MAX] = {0,};
	drm_slp_bufmgr bufmgr = NULL;
	int (*bufmgr_init)(drm_slp_bufmgr bufmgr, int fd, void *arg);
	void *module;

	snprintf(path, sizeof(path), BUFMGR_DIR "/%s", file);

	module = dlopen(path, RTLD_LAZY);
	if (!module) {
		fprintf(stderr,
			"[libdrm] failed to load module: %s(%s)\n",
			dlerror(), file);
		return NULL;
	}

	bufmgr_init = dlsym(module, "init_slp_bufmgr");
	if (!bufmgr_init) {
		fprintf(stderr,
			"[libdrm] failed to lookup init function: %s(%s)\n",
			dlerror(), file);
		return NULL;
	}

	bufmgr = calloc(sizeof(struct _drm_slp_bufmgr), 1);
	if(!bufmgr)
	{
		return NULL;
	}

	if(!bufmgr_init(bufmgr, fd, arg))
	{
		fprintf(stderr,"[libdrm] Fail to init module(%s)\n", file);
		free(bufmgr);
		bufmgr = NULL;
		return NULL;
	}

	fprintf(stderr,"[libdrm] Success to load module(%s)\n", file);

	return bufmgr;
}

drm_slp_bufmgr
drm_slp_bufmgr_init(int fd, void *arg)
{
    drm_slp_bufmgr bufmgr = NULL;
    const char *p = NULL;

    if (fd < 0)
        return NULL;

    if(gBufMgrs == NULL)
    {
        gBufMgrs = malloc(sizeof(struct list_head));
        LIST_INITHEAD(gBufMgrs);
    }
    else
    {
        LIST_FOR_EACH_ENTRY(bufmgr, gBufMgrs, link)
        {
            if(bufmgr->drm_fd == fd)
            {
                bufmgr->ref_count++;
                fprintf(stderr, "[libdrm] bufmgr ref: fd=%d, ref_count:%d\n", fd, bufmgr->ref_count);
                return bufmgr;
            }
        }
        bufmgr = NULL;
    }
    fprintf(stderr, "[libdrm] bufmgr init: fd=%d\n", fd);

    p = getenv ("SLP_BUFMGR_MODULE");
    if (p)
    {
        char file[PATH_MAX] = {0,};
        snprintf(file, sizeof(file), PREFIX_LIB"%s"SUFFIX_LIB, p);
        bufmgr = _load_bufmgr (fd, file, arg);
    }

    if (!bufmgr)
        bufmgr = _load_bufmgr (fd, DEFAULT_LIB, arg);

    if (!bufmgr)
    {
        struct dirent **namelist;
        int found = 0;
        int n;

        n = scandir(BUFMGR_DIR, &namelist, 0, alphasort);
        if (n < 0)
            fprintf(stderr,"[libdrm] no files : %s\n", BUFMGR_DIR);
        else
        {
            while(n--)
            {
                if (!found && strstr (namelist[n]->d_name, PREFIX_LIB))
                {
                    char *p = strstr (namelist[n]->d_name, SUFFIX_LIB);
                    if (!strcmp (p, SUFFIX_LIB))
                    {
                        bufmgr = _load_bufmgr (fd, namelist[n]->d_name, arg);
                        if (bufmgr)
                            found = 1;
                    }
                }
                free(namelist[n]);
            }
            free(namelist);
        }
    }

    if (!bufmgr)
    {
        fprintf(stderr,"[libdrm] backend is NULL.\n");
        return NULL;
    }

    if (pthread_mutex_init(&bufmgr->lock, NULL) != 0)
    {
        bufmgr->bufmgr_destroy(bufmgr);
        free(bufmgr);
        return NULL;
    }

    bufmgr->ref_count = 1;
    bufmgr->drm_fd = fd;

    LIST_INITHEAD(&bufmgr->bos);
    LIST_ADD(&bufmgr->link, gBufMgrs);

    return bufmgr;
}

void
drm_slp_bufmgr_destroy(drm_slp_bufmgr bufmgr)
{
    DRM_RETURN_IF_FAIL(MGR_IS_VALID(bufmgr));

    fprintf(stderr, "[DRM] bufmgr destroy: bufmgr:%p, drm_fd:%d\n",
                bufmgr, bufmgr->drm_fd);

    /*Check and Free bos*/
    if(!LIST_IS_EMPTY(&bufmgr->bos))
    {
        drm_slp_bo bo, tmp;

        LIST_FOR_EACH_ENTRY_SAFE(bo, tmp, &bufmgr->bos,  list)
        {
            fprintf(stderr, "[libdrm] Un-freed bo(%p, ref:%d) \n", bo, bo->ref_cnt);
            bo->ref_cnt = 1;
            drm_slp_bo_unref(bo);
        }
    }

    LIST_DEL(&bufmgr->link);
    bufmgr->bufmgr_destroy(bufmgr);

    if(bufmgr->semObj.isOpened)
    {
        _sem_close(bufmgr);
    }

    pthread_mutex_destroy(&bufmgr->lock);
    free(bufmgr);
}

int
drm_slp_bufmgr_lock(drm_slp_bufmgr bufmgr)
{
    DRM_RETURN_VAL_IF_FAIL(MGR_IS_VALID(bufmgr), 0);

    pthread_mutex_lock(&bufmgr->lock);

    if(bufmgr->bufmgr_lock)
    {
        int ret;
        ret = bufmgr->bufmgr_lock(bufmgr);
        pthread_mutex_unlock(&bufmgr->lock);
        return ret;
    }

    if(!bufmgr->semObj.isOpened)
    {
        if(_sem_open(bufmgr) != 1)
        {
            pthread_mutex_unlock(&bufmgr->lock);
            return 0;
        }
        bufmgr->semObj.isOpened = 1;
    }

    if(_sem_lock(bufmgr) != 1)
    {
        pthread_mutex_unlock(&bufmgr->lock);
        return 0;
    }

    pthread_mutex_unlock(&bufmgr->lock);

    return 1;
}

int
drm_slp_bufmgr_unlock(drm_slp_bufmgr bufmgr)
{
    DRM_RETURN_VAL_IF_FAIL(MGR_IS_VALID(bufmgr), 0);

    pthread_mutex_lock(&bufmgr->lock);

    if(bufmgr->bufmgr_unlock)
    {
        int ret;
        ret = bufmgr->bufmgr_unlock(bufmgr);
        pthread_mutex_unlock(&bufmgr->lock);
        return ret;
    }

    if(_sem_unlock(bufmgr) != 1)
    {
        pthread_mutex_unlock(&bufmgr->lock);
        return 0;
    }

    pthread_mutex_unlock(&bufmgr->lock);

    return 1;
}

int
drm_slp_bufmgr_cache_flush(drm_slp_bufmgr bufmgr, drm_slp_bo bo, int flags)
{
    int ret;

    DRM_RETURN_VAL_IF_FAIL(MGR_IS_VALID(bufmgr) || BO_IS_VALID(bo), 0);

    if (!bo)
        flags |= DRM_SLP_CACHE_ALL;

    if (bo)
    {
        DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), 0);

        if(!bo->bufmgr)
            return 0;

        pthread_mutex_lock(&bo->bufmgr->lock);
        ret = bo->bufmgr->bufmgr_cache_flush(bufmgr, bo, flags);
        pthread_mutex_unlock(&bo->bufmgr->lock);
    }
    else
    {
        pthread_mutex_lock(&bufmgr->lock);
        ret = bufmgr->bufmgr_cache_flush(bufmgr, NULL, flags);
        pthread_mutex_unlock(&bufmgr->lock);
    }

    return ret;
}

int
drm_slp_bo_size(drm_slp_bo bo)
{
    int size;
    drm_slp_bufmgr bufmgr;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), 0);

    bufmgr = bo->bufmgr;

    pthread_mutex_lock(&bufmgr->lock);
    size = bo->bufmgr->bo_size(bo);
    pthread_mutex_unlock(&bufmgr->lock);

    return size;
}

drm_slp_bo
drm_slp_bo_ref(drm_slp_bo bo)
{
    drm_slp_bufmgr bufmgr;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), NULL);

    bufmgr = bo->bufmgr;

    pthread_mutex_lock(&bufmgr->lock);

    bo->ref_cnt++;

    pthread_mutex_unlock(&bufmgr->lock);

    return bo;
}

void
drm_slp_bo_unref(drm_slp_bo bo)
{
    drm_slp_bufmgr bufmgr;

    DRM_RETURN_IF_FAIL(BO_IS_VALID(bo));

    bufmgr = bo->bufmgr;

    if(0 >= bo->ref_cnt)
        return;

    pthread_mutex_lock(&bufmgr->lock);

    bo->ref_cnt--;
    if(bo->ref_cnt == 0)
    {
        if(bo->user_data)
        {
            void* rd;
            drm_slp_user_data* old_data;
            unsigned long key;

            while(1==drmSLFirst(bo->user_data, &key, &rd))
            {
                old_data = (drm_slp_user_data*)rd;

                if(old_data->is_valid && old_data->free_func)
                {
                    if(old_data->data)
                        old_data->free_func(old_data->data);
                    old_data->data = NULL;
                    free(old_data);
                }
                drmSLDelete(bo->user_data, key);
            }

            drmSLDestroy(bo->user_data);
            bo->user_data = (void*)0;
        }

        LIST_DEL(&bo->list);
        bufmgr->bo_free(bo);

        free(bo);
    }

    pthread_mutex_unlock(&bufmgr->lock);
}

drm_slp_bo
drm_slp_bo_alloc(drm_slp_bufmgr bufmgr, const char * name, int size, int flags)
{
    drm_slp_bo bo=NULL;

    DRM_RETURN_VAL_IF_FAIL( MGR_IS_VALID(bufmgr) && (size > 0), NULL);

    bo = calloc(sizeof(struct _drm_slp_bo), 1);
    if(!bo)
        return NULL;

    bo->bufmgr = bufmgr;

    pthread_mutex_lock(&bufmgr->lock);
    if(!bufmgr->bo_alloc(bo, name, size, flags))
    {
        free(bo);
        pthread_mutex_unlock(&bufmgr->lock);
        return NULL;
    }
    bo->ref_cnt = 1;
    LIST_ADD(&bo->list, &bufmgr->bos);
    pthread_mutex_unlock(&bufmgr->lock);

    return bo;
}

drm_slp_bo
drm_slp_bo_attach(drm_slp_bufmgr bufmgr,
                             const char*    name,
                             int type,
                             int size,
                             unsigned int handle)
{
    drm_slp_bo bo;

    DRM_RETURN_VAL_IF_FAIL(MGR_IS_VALID(bufmgr), NULL);

    bo = calloc(sizeof(struct _drm_slp_bo), 1);
    if(!bo)
        return NULL;

    bo->bufmgr = bufmgr;

    pthread_mutex_lock(&bufmgr->lock);
    if(!bufmgr->bo_attach(bo, name, type, size, handle))
    {
        free(bo);
        pthread_mutex_unlock(&bufmgr->lock);
        return NULL;
    }
    bo->ref_cnt = 1;
    LIST_ADD(&bo->list, &bufmgr->bos);
    pthread_mutex_unlock(&bufmgr->lock);

    return bo;
}

drm_slp_bo
drm_slp_bo_import(drm_slp_bufmgr bufmgr, unsigned int key)
{
    drm_slp_bo bo;

    DRM_RETURN_VAL_IF_FAIL(MGR_IS_VALID(bufmgr), NULL);

    bo = calloc(sizeof(struct _drm_slp_bo), 1);
    if(!bo)
        return NULL;

    bo->bufmgr = bufmgr;

    pthread_mutex_lock(&bufmgr->lock);
    if(!bufmgr->bo_import(bo, key))
    {
        free(bo);
        pthread_mutex_unlock(&bufmgr->lock);
        return NULL;
    }
    bo->ref_cnt = 1;
    LIST_ADD(&bo->list, &bufmgr->bos);
    pthread_mutex_unlock(&bufmgr->lock);

    return bo;
}

unsigned int
drm_slp_bo_export(drm_slp_bo bo)
{
    int ret;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), 0);

    pthread_mutex_lock(&bo->bufmgr->lock);
    ret = bo->bufmgr->bo_export(bo);
    pthread_mutex_unlock(&bo->bufmgr->lock);

    return ret;
}

unsigned int
drm_slp_bo_get_handle(drm_slp_bo bo, int device)
{
    unsigned int ret;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), 0);

    pthread_mutex_lock(&bo->bufmgr->lock);
    ret = bo->bufmgr->bo_get_handle(bo, device);
    pthread_mutex_unlock(&bo->bufmgr->lock);

    return ret;
}

unsigned int
drm_slp_bo_map(drm_slp_bo bo, int device, int opt)
{
    unsigned int ret;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), 0);

    pthread_mutex_lock(&bo->bufmgr->lock);
    if(bo->bufmgr->bo_lock)
    {
        bo->bufmgr->bo_lock(bo, 0, (void*)0);
    }

    ret = bo->bufmgr->bo_map(bo, device, opt);
    pthread_mutex_unlock(&bo->bufmgr->lock);

    return ret;
}

int
drm_slp_bo_unmap(drm_slp_bo bo, int device)
{
    int ret;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), 0);

    pthread_mutex_lock(&bo->bufmgr->lock);
    ret = bo->bufmgr->bo_unmap(bo, device);

    if(bo->bufmgr->bo_unlock)
    {
        bo->bufmgr->bo_unlock(bo);
    }
    pthread_mutex_unlock(&bo->bufmgr->lock);

    return 0;
}

int
drm_slp_bo_swap(drm_slp_bo bo1, drm_slp_bo bo2)
{
    void* temp;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo1), 0);
    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo2), 0);

    if(bo1->bufmgr->bo_size(bo1) != bo2->bufmgr->bo_size(bo2))
        return 0;

    pthread_mutex_lock(&bo1->bufmgr->lock);
    temp = bo1->priv;
    bo1->priv = bo2->priv;
    bo2->priv = temp;
    pthread_mutex_unlock(&bo1->bufmgr->lock);

    return 1;
}

int
drm_slp_bo_add_user_data(drm_slp_bo bo, unsigned long key, drm_data_free data_free_func)
{
    int ret;
    drm_slp_user_data* data;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), 0);

    if(!bo->user_data)
        bo->user_data = drmSLCreate();

    data = calloc(1, sizeof(drm_slp_user_data));
    if(!data)
        return 0;

    data->free_func = data_free_func;
    data->data = (void*)0;
    data->is_valid = 0;

    ret = drmSLInsert(bo->user_data, key, data);
    if(ret == 1) /* Already in list */
    {
        free(data);
        return 0;
    }

    return 1;
}

int
drm_slp_bo_set_user_data(drm_slp_bo bo, unsigned long key, void* data)
{
    void *rd;
    drm_slp_user_data* old_data;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo), 0);

    if(!bo->user_data)
        return 0;

    if(drmSLLookup(bo->user_data, key, &rd))
        return 0;

    old_data = (drm_slp_user_data*)rd;
    if (!old_data)
        return 0;

    if(old_data->is_valid)
    {
        if(old_data->free_func)
        {
            if(old_data->data)
                old_data->free_func(old_data->data);
                old_data->data = NULL;
        }
    }
    else
        old_data->is_valid = 1;

    old_data->data = data;

    return 1;
}

int
drm_slp_bo_get_user_data(drm_slp_bo bo, unsigned long key, void** data)
{
    void *rd;
    drm_slp_user_data* old_data;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo) && data && bo->user_data, 0);

    if(drmSLLookup(bo->user_data, key, &rd))
    {
        *data = NULL;
        return 0;
    }

    old_data = (drm_slp_user_data*)rd;
    if (!old_data)
    {
        *data = NULL;
        return 0;
    }

    *data = old_data->data;

    return 1;
}

int
drm_slp_bo_delete_user_data(drm_slp_bo bo, unsigned long key)
{
    void *rd;
    drm_slp_user_data* old_data=(void*)0;

    DRM_RETURN_VAL_IF_FAIL(BO_IS_VALID(bo) && bo->user_data, 0);

    if(drmSLLookup(bo->user_data, key, &rd))
        return 0;

    old_data = (drm_slp_user_data*)rd;
    if (!old_data)
        return 0;

    if(old_data->is_valid && old_data->free_func)
    {
        if(old_data->data)
            old_data->free_func(old_data->data);
        free(old_data);
    }
    drmSLDelete(bo->user_data, key);

    return 1;
}
