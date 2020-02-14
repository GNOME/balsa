/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include "mime-stream-shared.h"

#include <unistd.h>

#include <gmime/gmime-stream.h>
#include <gmime/gmime-stream-filter.h>

typedef struct _LibBalsaMimeStreamSharedLock LibBalsaMimeStreamSharedLock;

struct _LibBalsaMimeStreamShared {
    GMimeStreamFs parent_object;

    LibBalsaMimeStreamSharedLock *lock;
};

static void lbmss_finalize(GObject *object);

static ssize_t lbmss_stream_read(GMimeStream * stream, char *buf,
                                 size_t len);
static ssize_t lbmss_stream_write(GMimeStream * stream, const char *buf,
                                  size_t len);
static int lbmss_stream_reset(GMimeStream * stream);
static gint64 lbmss_stream_seek(GMimeStream * stream, gint64 offset,
                               GMimeSeekWhence whence);
static GMimeStream *lbmss_stream_substream(GMimeStream * stream,
                                           gint64 start, gint64 end);

static GMutex lbmss_mutex;
static GCond lbmss_cond;

G_DEFINE_TYPE(LibBalsaMimeStreamShared, libbalsa_mime_stream_shared, GMIME_TYPE_STREAM_FS)

static void
libbalsa_mime_stream_shared_class_init(LibBalsaMimeStreamSharedClass * klass)
{
    GMimeStreamClass *stream_class = GMIME_STREAM_CLASS(klass);
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize  = lbmss_finalize;

    stream_class->read      = lbmss_stream_read;
    stream_class->write     = lbmss_stream_write;
    stream_class->reset     = lbmss_stream_reset;
    stream_class->seek      = lbmss_stream_seek;
    stream_class->substream = lbmss_stream_substream;
}

static void
libbalsa_mime_stream_shared_init(LibBalsaMimeStreamShared * stream)
{
}

/* The shared lock. */

struct _LibBalsaMimeStreamSharedLock {
    GThread *thread;
    guint count;
};

static LibBalsaMimeStreamSharedLock *
lbmss_lock_new(void)
{
    LibBalsaMimeStreamSharedLock *lock;

    lock         = g_new(LibBalsaMimeStreamSharedLock, 1);
    lock->thread = NULL;
    lock->count  = 0;

    return lock;
}

/* Object class method. */

static void
lbmss_finalize(GObject *object)
{
    LibBalsaMimeStreamShared *shared_stream = (LibBalsaMimeStreamShared *) object;
    GMimeStreamFs *fs_stream                = (GMimeStreamFs *) object;

    if (fs_stream->owner)
        g_free(shared_stream->lock);

    G_OBJECT_CLASS(libbalsa_mime_stream_shared_parent_class)->finalize(object);
}

/* Stream class methods. */

#define lbmss_thread_has_lock(stream) \
    (LIBBALSA_MIME_STREAM_SHARED(stream)->lock->thread == g_thread_self())

static ssize_t
lbmss_stream_read(GMimeStream * stream, char *buf, size_t len)
{
    g_return_val_if_fail(lbmss_thread_has_lock(stream), -1);
    return GMIME_STREAM_CLASS(libbalsa_mime_stream_shared_parent_class)->read(stream, buf, len);
}

static ssize_t
lbmss_stream_write(GMimeStream * stream, const char *buf, size_t len)
{
    g_return_val_if_fail(lbmss_thread_has_lock(stream), -1);
    return GMIME_STREAM_CLASS(libbalsa_mime_stream_shared_parent_class)->write(stream, buf, len);
}

static int
lbmss_stream_reset(GMimeStream * stream)
{
    g_return_val_if_fail(lbmss_thread_has_lock(stream), -1);
    return GMIME_STREAM_CLASS(libbalsa_mime_stream_shared_parent_class)->reset(stream);
}

static gint64
lbmss_stream_seek(GMimeStream * stream, gint64 offset,
                  GMimeSeekWhence whence)
{
    g_return_val_if_fail(lbmss_thread_has_lock(stream), -1);
    return GMIME_STREAM_CLASS(libbalsa_mime_stream_shared_parent_class)->seek(stream, offset, whence);
}

static GMimeStream *
lbmss_stream_substream(GMimeStream * stream, gint64 start, gint64 end)
{
    LibBalsaMimeStreamShared *shared_stream;
    GMimeStreamFs *fstream;

    shared_stream =
        g_object_new(LIBBALSA_TYPE_MIME_STREAM_SHARED, NULL, NULL);
    shared_stream->lock = LIBBALSA_MIME_STREAM_SHARED(stream)->lock;

    fstream = GMIME_STREAM_FS(shared_stream);
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
    LibBalsaMimeStreamShared *shared_stream;
    GMimeStreamFs *fstream;
    gint64 start;

    shared_stream =
        g_object_new(LIBBALSA_TYPE_MIME_STREAM_SHARED, NULL, NULL);
    shared_stream->lock = lbmss_lock_new();

    fstream = GMIME_STREAM_FS(shared_stream);
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
    LibBalsaMimeStreamShared *shared_stream;
    LibBalsaMimeStreamSharedLock *lock;
    GThread *thread_self;

    g_return_if_fail(GMIME_IS_STREAM(stream));

    if (GMIME_IS_STREAM_FILTER(stream))
        stream = ((GMimeStreamFilter *) stream)->source;

    if (!LIBBALSA_IS_MIME_STREAM_SHARED(stream))
        return;

    shared_stream = (LibBalsaMimeStreamShared *) stream;
    lock = shared_stream->lock;
    thread_self = g_thread_self();

    g_mutex_lock(&lbmss_mutex);
    while (lock->count > 0 && lock->thread != thread_self)
        g_cond_wait(&lbmss_cond, &lbmss_mutex);
    ++lock->count;
    lock->thread = thread_self;
    g_mutex_unlock(&lbmss_mutex);
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
    LibBalsaMimeStreamShared *shared_stream;
    LibBalsaMimeStreamSharedLock *lock;

    g_return_if_fail(GMIME_IS_STREAM(stream));

    if (GMIME_IS_STREAM_FILTER(stream))
        stream = ((GMimeStreamFilter *) stream)->source;

    if (!LIBBALSA_IS_MIME_STREAM_SHARED(stream))
        return;

    shared_stream = (LibBalsaMimeStreamShared *) stream;
    lock = shared_stream->lock;
    g_return_if_fail(lock->count > 0);

    g_mutex_lock(&lbmss_mutex);
    if (--lock->count == 0) {
        lock->thread = NULL;
        g_cond_signal(&lbmss_cond);
    }
    g_mutex_unlock(&lbmss_mutex);
}
