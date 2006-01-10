/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2005 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */
/*
 * LibBalsaMimeStreamShared: a subclass of GMimeStreamFs that supports
 * locking.
 *
 * A single lock is shared by the original stream and all substreams and
 * filtered streams derived from it.  Methods that change the stream
 * pointer (read, write, reset, seek) will fail with return value -1 if
 * the stream is not locked.  The lock should be held while carrying out
 * sequences of operations such as seek+read, seek+write, read+tell,
 * read+test-eos.
 */

#include "config.h"

#if BALSA_USE_THREADS

#include <gmime/gmime-stream.h>
#include <gmime/gmime-stream-filter.h>
#include "mime-stream-shared.h"

typedef struct _LibBalsaMimeStreamSharedLock LibBalsaMimeStreamSharedLock;

struct _LibBalsaMimeStreamShared {
    GMimeStreamFs parent_object;

    LibBalsaMimeStreamSharedLock *lock;
};

struct _LibBalsaMimeStreamSharedClass {
    GMimeStreamClass parent_class;
};

static void lbmss_stream_class_init(LibBalsaMimeStreamSharedClass * klass);

static void lbmss_finalize(GObject *object);

static ssize_t lbmss_stream_read(GMimeStream * stream, char *buf,
                                 size_t len);
static ssize_t lbmss_stream_write(GMimeStream * stream, const char *buf,
                                  size_t len);
static int lbmss_stream_reset(GMimeStream * stream);
static off_t lbmss_stream_seek(GMimeStream * stream, off_t offset,
                               GMimeSeekWhence whence);
static GMimeStream *lbmss_stream_substream(GMimeStream * stream,
                                           off_t start, off_t end);

static GMimeStreamFsClass *parent_class = NULL;
static GMutex *lbmss_mutex;
static GCond *lbmss_cond;

GType
libbalsa_mime_stream_shared_get_type(void)
{
    static GType type = 0;

    if (!type) {
        static const GTypeInfo info = {
            sizeof(LibBalsaMimeStreamSharedClass),
            NULL,               /* base_class_init */
            NULL,               /* base_class_finalize */
            (GClassInitFunc) lbmss_stream_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(LibBalsaMimeStreamShared),
            16,                 /* n_preallocs */
            NULL                /* instance init */
        };

        type =
            g_type_register_static(GMIME_TYPE_STREAM_FS,
                                   "LibBalsaMimeStreamShared", &info, 0);
    }

    return type;
}

static void
lbmss_stream_class_init(LibBalsaMimeStreamSharedClass * klass)
{
    GMimeStreamClass *stream_class = GMIME_STREAM_CLASS(klass);
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    parent_class = g_type_class_ref(GMIME_TYPE_STREAM_FS);
    lbmss_mutex  = g_mutex_new();
    lbmss_cond   = g_cond_new();

    object_class->finalize  = lbmss_finalize;

    stream_class->read      = lbmss_stream_read;
    stream_class->write     = lbmss_stream_write;
    stream_class->reset     = lbmss_stream_reset;
    stream_class->seek      = lbmss_stream_seek;
    stream_class->substream = lbmss_stream_substream;
}

/* The shared lock. */

struct _LibBalsaMimeStreamSharedLock {
    GThread *thread;
    guint count;
    guint ref_count;
};

static LibBalsaMimeStreamSharedLock *
lbmss_lock_new(void)
{
    LibBalsaMimeStreamSharedLock *lock;

    lock = g_new(LibBalsaMimeStreamSharedLock, 1);
    lock->thread = 0;
    lock->count = 0;
    lock->ref_count = 1;

    return lock;
}

static LibBalsaMimeStreamSharedLock *
lbmss_lock_ref(LibBalsaMimeStreamSharedLock * lock)
{
    ++lock->ref_count;

    return lock;
}

static void
lbmss_lock_unref(LibBalsaMimeStreamSharedLock * lock)
{
    g_assert(lock->ref_count > 0);

    if (--lock->ref_count == 0)
        g_free(lock);
}

/* Object class method. */

static void
lbmss_finalize(GObject *object)
{
    LibBalsaMimeStreamShared *stream_shared =
        (LibBalsaMimeStreamShared *) object;

    lbmss_lock_unref(stream_shared->lock);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

/* Stream class methods. */

#define lbmss_thread_has_lock(stream)                     \
    (LIBBALSA_MIME_STREAM_SHARED(stream)->lock->count > 0 \
     && LIBBALSA_MIME_STREAM_SHARED(stream)->lock->thread == g_thread_self())

static ssize_t
lbmss_stream_read(GMimeStream * stream, char *buf, size_t len)
{
    g_return_val_if_fail(lbmss_thread_has_lock(stream), -1);
    return GMIME_STREAM_CLASS(parent_class)->read(stream, buf, len);
}

static ssize_t
lbmss_stream_write(GMimeStream * stream, const char *buf, size_t len)
{
    g_return_val_if_fail(lbmss_thread_has_lock(stream), -1);
    return GMIME_STREAM_CLASS(parent_class)->write(stream, buf, len);
}

static int
lbmss_stream_reset(GMimeStream * stream)
{
    g_return_val_if_fail(lbmss_thread_has_lock(stream), -1);
    return GMIME_STREAM_CLASS(parent_class)->reset(stream);
}

static off_t
lbmss_stream_seek(GMimeStream * stream, off_t offset,
                  GMimeSeekWhence whence)
{
    g_return_val_if_fail(lbmss_thread_has_lock(stream), -1);
    return GMIME_STREAM_CLASS(parent_class)->seek(stream, offset, whence);
}

static GMimeStream *
lbmss_stream_substream(GMimeStream * stream, off_t start, off_t end)
{
    LibBalsaMimeStreamShared *stream_shared;
    GMimeStreamFs *fstream;

    stream_shared =
        g_object_new(LIBBALSA_TYPE_MIME_STREAM_SHARED, NULL, NULL);
    stream_shared->lock =
        lbmss_lock_ref(LIBBALSA_MIME_STREAM_SHARED(stream)->lock);

    fstream = GMIME_STREAM_FS(stream_shared);
    fstream->owner = FALSE;
    fstream->fd = GMIME_STREAM_FS(stream)->fd;
    g_mime_stream_construct(GMIME_STREAM(fstream), start, end);

    return GMIME_STREAM(fstream);
}

/* Public methods. */

/**
 * libbalsa_mime_stream_shared_new:
 * @fd: file descriptor
 *
 * Create a new GMimeStreamShared object around @fd.
 *
 * Returns a stream using @fd.
 *
 **/
GMimeStream *
libbalsa_mime_stream_shared_new(int fd)
{
    LibBalsaMimeStreamShared *stream_shared;
    GMimeStreamFs *fstream;
    off_t start;

    stream_shared =
        g_object_new(LIBBALSA_TYPE_MIME_STREAM_SHARED, NULL, NULL);
    stream_shared->lock = lbmss_lock_new();

    fstream = GMIME_STREAM_FS(stream_shared);
    fstream->owner = TRUE;
    fstream->eos = FALSE;
    fstream->fd = fd;

    start = lseek(fd, 0, SEEK_CUR);
    g_mime_stream_construct(GMIME_STREAM(fstream), start, -1);

    return GMIME_STREAM(fstream);
}

/**
 * libbalsa_mime_stream_shared_lock:
 * @stream: shared stream
 *
 * Lock the shared stream
 **/
void
libbalsa_mime_stream_shared_lock(GMimeStream * stream)
{
    LibBalsaMimeStreamShared *stream_shared;
    LibBalsaMimeStreamSharedLock *lock;
    GThread *thread_self;

    g_return_if_fail(GMIME_IS_STREAM(stream));

    if (GMIME_IS_STREAM_FILTER(stream))
        stream = ((GMimeStreamFilter *) stream)->source;

    if (!LIBBALSA_IS_MIME_STREAM_SHARED(stream))
        return;

    stream_shared = (LibBalsaMimeStreamShared *) stream;
    lock = stream_shared->lock;
    thread_self = g_thread_self();

    g_mutex_lock(lbmss_mutex);
    while (lock->count > 0 && lock->thread != thread_self)
        g_cond_wait(lbmss_cond, lbmss_mutex);
    ++lock->count;
    lock->thread = thread_self;
    g_mutex_unlock(lbmss_mutex);
}

/**
 * libbalsa_mime_stream_shared_unlock:
 * @stream: shared stream
 *
 * Unlock the shared stream
 **/
void
libbalsa_mime_stream_shared_unlock(GMimeStream * stream)
{
    LibBalsaMimeStreamShared *stream_shared;
    LibBalsaMimeStreamSharedLock *lock;

    g_return_if_fail(GMIME_IS_STREAM(stream));

    if (GMIME_IS_STREAM_FILTER(stream))
        stream = ((GMimeStreamFilter *) stream)->source;

    if (!LIBBALSA_IS_MIME_STREAM_SHARED(stream))
        return;

    stream_shared = (LibBalsaMimeStreamShared *) stream;
    lock = stream_shared->lock;
    g_return_if_fail(lock->count > 0);

    g_mutex_lock(lbmss_mutex);
    if (--lock->count == 0)
        g_cond_signal(lbmss_cond);
    g_mutex_unlock(lbmss_mutex);
}

#endif                          /* BALSA_USE_THREADS */
