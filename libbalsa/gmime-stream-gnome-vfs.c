/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*  GMime Gnome VFS stream module
 *  Written/Copyright (c) by Albrecht Dreﬂ <albrecht.dress@arcor.de>
 *  The basic structure of this file has been shamelessly stolen from the
 *  gmime-stream-fs module, written by Jeffrey Stedfast.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "gmime-stream-gnome-vfs.h"

/* note: this module will be compiled only if Gnome-Vfs is available */
#ifdef HAVE_GNOME_VFS

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/**
 * SECTION: gmime-stream-gvfs
 * @title: GMimeStreamGvfs
 * @short_description: A low-level FileSystem stream
 * @see_also: #GMimeStream
 *
 * A simple #GMimeStream implementation that sits on top of the
 * low-level UNIX file descriptor based I/O layer.
 **/


static void g_mime_stream_gvfs_class_init(GMimeStreamGvfsClass * klass);
static void g_mime_stream_gvfs_init(GMimeStreamGvfs * stream,
				    GMimeStreamGvfsClass * klass);
static void g_mime_stream_gvfs_finalize(GObject * object);

static ssize_t stream_read(GMimeStream * stream, char *buf, size_t len);
static ssize_t stream_write(GMimeStream * stream, const char *buf,
			    size_t len);
static int stream_flush(GMimeStream * stream);
static int stream_close(GMimeStream * stream);
static gboolean stream_eos(GMimeStream * stream);
static int stream_reset(GMimeStream * stream);
static gint64 stream_seek(GMimeStream * stream, gint64 offset,
			 GMimeSeekWhence whence);
static gint64 stream_tell(GMimeStream * stream);
static ssize_t stream_length(GMimeStream * stream);
static GMimeStream *stream_substream(GMimeStream * stream, gint64 start,
				     gint64 end);


static GMimeStreamClass *parent_class = NULL;


GType
g_mime_stream_gvfs_get_type(void)
{
    static GType type = 0;

    if (!type) {
	static const GTypeInfo info = {
	    sizeof(GMimeStreamGvfsClass),
	    NULL,		/* base_class_init */
	    NULL,		/* base_class_finalize */
	    (GClassInitFunc) g_mime_stream_gvfs_class_init,
	    NULL,		/* class_finalize */
	    NULL,		/* class_data */
	    sizeof(GMimeStreamGvfs),
	    0,			/* n_preallocs */
	    (GInstanceInitFunc) g_mime_stream_gvfs_init,
	};

	type =
	    g_type_register_static(GMIME_TYPE_STREAM, "GMimeStreamGvfs",
				   &info, 0);
    }

    return type;
}


static void
g_mime_stream_gvfs_class_init(GMimeStreamGvfsClass * klass)
{
    GMimeStreamClass *stream_class = GMIME_STREAM_CLASS(klass);
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    parent_class = g_type_class_ref(GMIME_TYPE_STREAM);

    object_class->finalize = g_mime_stream_gvfs_finalize;

    stream_class->read = stream_read;
    stream_class->write = stream_write;
    stream_class->flush = stream_flush;
    stream_class->close = stream_close;
    stream_class->eos = stream_eos;
    stream_class->reset = stream_reset;
    stream_class->seek = stream_seek;
    stream_class->tell = stream_tell;
    stream_class->length = stream_length;
    stream_class->substream = stream_substream;
}

static void
g_mime_stream_gvfs_init(GMimeStreamGvfs * stream,
			GMimeStreamGvfsClass * klass)
{
    stream->owner = TRUE;
    stream->eos = FALSE;
    stream->handle = NULL;
}

static void
g_mime_stream_gvfs_finalize(GObject * object)
{
    GMimeStreamGvfs *stream = (GMimeStreamGvfs *) object;

    if (stream->owner && stream->handle)
	gnome_vfs_close(stream->handle);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static ssize_t
stream_read(GMimeStream * stream, char *buf, size_t len)
{
    GMimeStreamGvfs *gvfs = (GMimeStreamGvfs *) stream;
    GnomeVFSFileSize nread = 0;
    GnomeVFSResult result;

    if (stream->bound_end != -1 && stream->position >= stream->bound_end)
	return -1;

    if (stream->bound_end != -1)
	len = MIN(stream->bound_end - stream->position, (gint64) len);

    /* make sure we are at the right position */
    gnome_vfs_seek(gvfs->handle, GNOME_VFS_SEEK_START, stream->position);

    do {
        result = gnome_vfs_read(gvfs->handle, buf, len, &nread);
    } while (result == GNOME_VFS_ERROR_INTERRUPTED);

    if (result == GNOME_VFS_OK)
        stream->position += nread;
    else if (result == GNOME_VFS_ERROR_EOF || nread == 0)
        gvfs->eos = TRUE;
    else
        nread = -1;

    return nread;
}

static ssize_t
stream_write(GMimeStream * stream, const char *buf, size_t len)
{
    GMimeStreamGvfs *gvfs = (GMimeStreamGvfs *) stream;
    size_t nwritten = 0;
    GnomeVFSFileSize chunk;
    GnomeVFSResult result;

    if (stream->bound_end != -1 && stream->position >= stream->bound_end)
	return -1;

    if (stream->bound_end != -1)
	len = MIN(stream->bound_end - stream->position, (gint64) len);

    /* make sure we are at the right position */
    gnome_vfs_seek(gvfs->handle, GNOME_VFS_SEEK_START, stream->position);

    do {
        chunk = 0;
	do {
            result = gnome_vfs_write(gvfs->handle, buf, len, &chunk);
	} while (result == GNOME_VFS_ERROR_INTERRUPTED ||
                 result == GNOME_VFS_ERROR_IN_PROGRESS);

	if (chunk > 0)
	    nwritten += chunk;
    } while (result == GNOME_VFS_OK && nwritten < len);

    if (result == GNOME_VFS_ERROR_TOO_BIG ||
        result == GNOME_VFS_ERROR_NO_SPACE)
	gvfs->eos = TRUE;

    if (nwritten > 0)
	stream->position += nwritten;
    else if (result != GNOME_VFS_OK)
	return -1;

    return nwritten;
}

static int
stream_flush(GMimeStream * stream)
{
    GMimeStreamGvfs *gvfs = (GMimeStreamGvfs *) stream;

    g_return_val_if_fail(gvfs->handle != NULL, -1);

    return 0;
}

static int
stream_close(GMimeStream * stream)
{
    GMimeStreamGvfs *gvfs = (GMimeStreamGvfs *) stream;
    GnomeVFSResult rv;

    if (gvfs->handle == NULL)
	return 0;

    do {
	if ((rv = gnome_vfs_close(gvfs->handle)) == GNOME_VFS_OK)
	    gvfs->handle = NULL;
    } while (rv == GNOME_VFS_ERROR_INTERRUPTED);

    return (rv == GNOME_VFS_OK) ? 0 : -1;
}

static gboolean
stream_eos(GMimeStream * stream)
{
    GMimeStreamGvfs *gvfs = (GMimeStreamGvfs *) stream;

    g_return_val_if_fail(gvfs->handle != NULL, TRUE);

    return gvfs->eos;
}

static int
stream_reset(GMimeStream * stream)
{
    GMimeStreamGvfs *gvfs = (GMimeStreamGvfs *) stream;

    if (gvfs->handle == NULL)
	return -1;

    if (stream->position == stream->bound_start) {
	gvfs->eos = FALSE;
	return 0;
    }

    /* FIXME: if stream_read/write is always going to lseek to
     * make sure fd's seek position matches our own, we could just
     * set stream->position = stream->bound_start and be done. */
    if (gnome_vfs_seek(gvfs->handle, GNOME_VFS_SEEK_START, stream->bound_start) != GNOME_VFS_OK)
	return -1;

    gvfs->eos = FALSE;

    return 0;
}

static gint64
stream_seek(GMimeStream * stream, gint64 offset, GMimeSeekWhence whence)
{
    GMimeStreamGvfs *gvfs = (GMimeStreamGvfs *) stream;
    gint64 real;
    GnomeVFSFileSize gvfs_real;

    g_return_val_if_fail(gvfs->handle != NULL, -1);

    switch (whence) {
    case GMIME_STREAM_SEEK_SET:
	real = offset;
	break;
    case GMIME_STREAM_SEEK_CUR:
	real = stream->position + offset;
	break;
    case GMIME_STREAM_SEEK_END:
	if (offset > 0 || (stream->bound_end == -1 && !gvfs->eos)) {
	    /* need to do an actual lseek() here because
	     * we either don't know the offset of the end
	     * of the stream and/or don't know if we can
	     * seek past the end */
            if (gnome_vfs_seek(gvfs->handle, GNOME_VFS_SEEK_END, offset) != GNOME_VFS_OK ||
                gnome_vfs_tell(gvfs->handle, &gvfs_real) != GNOME_VFS_OK)
		return -1;
            real = (off_t) gvfs_real;
	} else if (gvfs->eos && stream->bound_end == -1) {
	    /* seeking backwards from eos (which happens
	     * to be our current position) */
	    real = stream->position + offset;
	} else {
	    /* seeking backwards from a known position */
	    real = stream->bound_end + offset;
	}

	break;
    default:
	g_assert_not_reached();
	return -1;
    }

    /* sanity check the resultant offset */
    if (real < stream->bound_start)
	return -1;

    /* short-cut if we are seeking to our current position */
    if (real == stream->position)
	return real;

    if (stream->bound_end != -1 && real > stream->bound_end)
	return -1;

    if (gnome_vfs_seek(gvfs->handle, GNOME_VFS_SEEK_START, real) != GNOME_VFS_OK ||
        gnome_vfs_tell(gvfs->handle, &gvfs_real) != GNOME_VFS_OK)
	return -1;
    else
        real = (gint64) gvfs_real;

    /* reset eos if appropriate */
    if ((stream->bound_end != -1 && real < stream->bound_end) ||
	(gvfs->eos && real < stream->position))
	gvfs->eos = FALSE;

    stream->position = real;

    return real;
}

static gint64
stream_tell(GMimeStream * stream)
{
    return stream->position;
}

static ssize_t
stream_length(GMimeStream * stream)
{
    GMimeStreamGvfs *gvfs = (GMimeStreamGvfs *) stream;
    GnomeVFSFileSize bound_end;

    if (stream->bound_end != -1)
	return stream->bound_end - stream->bound_start;

    if (gnome_vfs_seek(gvfs->handle, GNOME_VFS_SEEK_END, 0) != GNOME_VFS_OK ||
        gnome_vfs_tell(gvfs->handle, &bound_end) != GNOME_VFS_OK ||
        gnome_vfs_seek(gvfs->handle, GNOME_VFS_SEEK_START, stream->position) != GNOME_VFS_OK ||
        (gint64) bound_end < stream->bound_start)
        return -1;

    return (ssize_t) bound_end - stream->bound_start;
}

static GMimeStream *
stream_substream(GMimeStream * stream, gint64 start, gint64 end)
{
    GMimeStreamGvfs *gvfs;

    gvfs = g_object_new(GMIME_TYPE_STREAM_GVFS, NULL);
    g_mime_stream_construct(GMIME_STREAM(gvfs), start, end);
    gvfs->handle = GMIME_STREAM_GVFS(stream)->handle;
    gvfs->owner = FALSE;
    gvfs->eos = FALSE;

    return (GMimeStream *) gvfs;
}


/**
 * g_mime_stream_gvfs_new:
 * @handle: Gnome VFS handle
 *
 * Creates a new GMimeStreamGvfs object around @handle.
 *
 * Returns a stream using @handle.
 **/
GMimeStream *
g_mime_stream_gvfs_new(GnomeVFSHandle * handle)
{
    GMimeStreamGvfs *gvfs;
    GnomeVFSFileSize start;

    if (gnome_vfs_tell(handle, &start) != GNOME_VFS_OK)
        start = 0;

    gvfs = g_object_new(GMIME_TYPE_STREAM_GVFS, NULL);
    g_mime_stream_construct(GMIME_STREAM(gvfs), (gint64) start, -1);
    gvfs->owner = TRUE;
    gvfs->eos = FALSE;
    gvfs->handle = handle;

    return (GMimeStream *) gvfs;
}


/**
 * g_mime_stream_gvfs_new_with_bounds:
 * @handle: Gnome VFS handle
 * @start: start boundary
 * @end: end boundary
 *
 * Creates a new GMimeStreamGvfs object around @handle with bounds @start
 * and @end.
 *
 * Returns a stream using @handle with bounds @start and @end.
 **/
GMimeStream *
g_mime_stream_gvfs_new_with_bounds(GnomeVFSHandle * handle, gint64 start, gint64 end)
{
    GMimeStreamGvfs *gvfs;

    gvfs = g_object_new(GMIME_TYPE_STREAM_GVFS, NULL);
    g_mime_stream_construct(GMIME_STREAM(gvfs), start, end);
    gvfs->owner = TRUE;
    gvfs->eos = FALSE;
    gvfs->handle = handle;

    return (GMimeStream *) gvfs;
}


/**
 * g_mime_stream_gvfs_get_owner:
 * @stream: gvfs stream
 *
 * Gets whether or not @stream owns the backend file descriptor.
 *
 * Returns %TRUE if @stream owns the backend file descriptor or %FALSE
 * otherwise.
 **/
gboolean
g_mime_stream_gvfs_get_owner(GMimeStreamGvfs * stream)
{
    g_return_val_if_fail(GMIME_IS_STREAM_GVFS(stream), FALSE);

    return stream->owner;
}


/**
 * g_mime_stream_gvfs_set_owner:
 * @stream: gvfs stream
 * @owner: owner
 *
 * Sets whether or not @stream owns the backend GVFS pointer.
 *
 * Note: @owner should be %TRUE if the stream should close() the
 * backend file descriptor when destroyed or %FALSE otherwise.
 **/
void
g_mime_stream_gvfs_set_owner(GMimeStreamGvfs * stream, gboolean owner)
{
    g_return_if_fail(GMIME_IS_STREAM_GVFS(stream));

    stream->owner = owner;
}

#endif				/* HAVE_GNOME_VFS */
