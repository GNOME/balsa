/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*  GMime GIO stream module
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
#include "gmime-stream-gio.h"

/* note: this module will be compiled only if GIO is available */
#if HAVE_GIO

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

static void g_mime_stream_gio_class_init(GMimeStreamGioClass *klass);
static void g_mime_stream_gio_init(GMimeStreamGio *stream,
				   GMimeStreamGioClass *klass);
static void g_mime_stream_gio_finalize(GObject *object);

static ssize_t stream_read(GMimeStream *stream,
			   char *buf,
			   size_t len);
static ssize_t stream_write(GMimeStream *stream,
			    const char *buf,
			    size_t len);
static int stream_flush(GMimeStream *stream);
static int stream_close(GMimeStream *stream);
static gboolean stream_eos(GMimeStream *stream);
static int stream_reset(GMimeStream *stream);
static off_t stream_seek(GMimeStream *stream,
			  off_t offset,
			  GMimeSeekWhence whence);
static off_t stream_tell(GMimeStream *stream);
static ssize_t stream_length(GMimeStream *stream);
static GMimeStream *stream_substream(GMimeStream *stream,
				     off_t start,
				     off_t end);


static GMimeStreamClass *parent_class = NULL;

#define GIO_DEBUG  1

#ifdef GIO_DEBUG
#define GIO_DEBUG_INIT   GError * gioerr = NULL
#define GIO_DEBUG_OUT    &gioerr
#define GIO_DEBUG_MSG(textmsg)						\
    do {								\
	if (gioerr) {							\
	    g_debug("%s::%s::%d " textmsg ": err %d: %s\n",		\
		    __FILE__, __FUNCTION__, __LINE__,			\
		    gioerr->code, gioerr->message);			\
	    g_error_free(gioerr);					\
	    gioerr = NULL;						\
	}								\
    } while(0)
#else
#define GIO_DEBUG_INIT
#define GIO_DEBUG_OUT    NULL
#define GIO_DEBUG_MSG    
#endif


GType
g_mime_stream_gio_get_type(void)
{
    static GType type = 0;
	
    if (!type) {
	static const GTypeInfo info = {
	    sizeof (GMimeStreamGioClass),
	    NULL, /* base_class_init */
	    NULL, /* base_class_finalize */
	    (GClassInitFunc) g_mime_stream_gio_class_init,
	    NULL, /* class_finalize */
	    NULL, /* class_data */
	    sizeof (GMimeStreamGio),
	    0,    /* n_preallocs */
	    (GInstanceInitFunc) g_mime_stream_gio_init,
	};
		
	type = g_type_register_static(GMIME_TYPE_STREAM, "GMimeStreamGio", &info, 0);
    }
	
    return type;
}


static void
g_mime_stream_gio_class_init(GMimeStreamGioClass *klass)
{
    GMimeStreamClass *stream_class = GMIME_STREAM_CLASS(klass);
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
	
    parent_class = g_type_class_ref(GMIME_TYPE_STREAM);
	
    object_class->finalize = g_mime_stream_gio_finalize;
	
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
g_mime_stream_gio_init(GMimeStreamGio *stream,
		       GMimeStreamGioClass *klass)
{
    stream->eos = FALSE;
    stream->gfile = NULL;
    stream->stream = NULL;
}

static void
g_mime_stream_gio_finalize(GObject *object)
{
    GMimeStreamGio *stream = (GMimeStreamGio *) object;
    
    g_return_if_fail(stream);
    if (stream->stream) {
	g_object_unref(stream->stream);  // will also call g_(input|output)_stream_close
	stream->stream = NULL;
    }
    
    if (stream->gfile)
	g_object_unref(G_OBJECT(stream->gfile));

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static ssize_t
stream_read(GMimeStream *stream, char *buf, size_t len)
{
    GMimeStreamGio *gios = (GMimeStreamGio *) stream;
    gsize nread;
    gboolean result;
    GIO_DEBUG_INIT;
	
    g_return_val_if_fail(stream, -1);
    if (stream->bound_end != -1 && stream->position >= stream->bound_end)
	return -1;
	
    if (stream->bound_end != -1)
	len = MIN (stream->bound_end - stream->position, (off_t) len);
    
    /* try to create the stream if necessary */
    if (!gios->stream) {
	gios->stream = G_OBJECT(g_file_read(gios->gfile, NULL, GIO_DEBUG_OUT));
	GIO_DEBUG_MSG("g_file_read");
	if (!gios->stream)
	    return -1;
    }

    /* make sure we are at the right position */
    g_seekable_seek(G_SEEKABLE(gios->stream), (goffset) stream->position,
		    G_SEEK_SET, NULL, NULL);

    result = g_input_stream_read_all(G_INPUT_STREAM(gios->stream), buf, len, 
				     &nread, NULL, GIO_DEBUG_OUT);
    GIO_DEBUG_MSG("g_input_stream_read_all");
	
    if (result)
	stream->position += nread;
    if (nread == 0)
	gios->eos = TRUE;
	
    return nread;
}

static ssize_t
stream_write(GMimeStream *stream, const char *buf, size_t len)
{
    GMimeStreamGio *gios = (GMimeStreamGio *) stream;
    gsize nwritten;
    gboolean result;
    GIO_DEBUG_INIT;
	
    g_return_val_if_fail(stream, -1);
    if (stream->bound_end != -1 && stream->position >= stream->bound_end)
	return -1;
	
    if (stream->bound_end != -1)
	len = MIN (stream->bound_end - stream->position, (off_t) len);
    
    /* try to create the stream if necessary */
    if (!gios->stream) {
	gios->stream = G_OBJECT(g_file_append_to(gios->gfile, G_FILE_CREATE_NONE, NULL, GIO_DEBUG_OUT));
	GIO_DEBUG_MSG("g_file_append_to");
	if (!gios->stream)
	    return -1;
    }
	
    /* make sure we are at the right position */
    g_seekable_seek(G_SEEKABLE(gios->stream), (goffset) stream->position,
		    G_SEEK_SET, NULL, NULL);
    
    result = g_output_stream_write_all(G_OUTPUT_STREAM(gios->stream), buf, len,
				       &nwritten, NULL, GIO_DEBUG_OUT);
    GIO_DEBUG_MSG("g_output_stream_write_all");
	
    if (result)
	stream->position += nwritten;
    else 
	return -1;
	
    return nwritten;
}

static int
stream_flush(GMimeStream *stream)
{
    GMimeStreamGio *gios = (GMimeStreamGio *) stream;
    gboolean result;
    GIO_DEBUG_INIT;
    
    g_return_val_if_fail(stream, -1);
    g_return_val_if_fail(G_IS_OUTPUT_STREAM(gios->stream), -1);
    result = g_output_stream_flush(G_OUTPUT_STREAM(gios->stream), NULL, GIO_DEBUG_OUT);
    GIO_DEBUG_MSG("g_output_stream_flush");
    return result ? 0 : -1;
}

static int
stream_close(GMimeStream *stream)
{
    GMimeStreamGio *gios = (GMimeStreamGio *) stream;

    g_return_val_if_fail(stream, -1);
    if (gios->stream) {
	g_object_unref(gios->stream);
	gios->stream = NULL;
    }
    
    return 0;
}

static gboolean
stream_eos(GMimeStream *stream)
{
    GMimeStreamGio *gios = (GMimeStreamGio *) stream;
	
    g_return_val_if_fail(stream, TRUE);
    if (gios->stream)
        g_return_val_if_fail(G_IS_INPUT_STREAM(gios->stream), TRUE);
	
    return gios->eos;
}

static int
stream_reset(GMimeStream *stream)
{
    GMimeStreamGio *gios = (GMimeStreamGio *) stream;
    gboolean result;
    GIO_DEBUG_INIT;

    g_return_val_if_fail(stream, -1);
    if (!gios->stream)
	return -1;
	
    if (stream->position == stream->bound_start) {
	gios->eos = FALSE;
	return 0;
    }
    
    result = g_seekable_seek(G_SEEKABLE(gios->stream), (goffset)stream->bound_start,
			     G_SEEK_SET, NULL, GIO_DEBUG_OUT);
    GIO_DEBUG_MSG("g_seekable_seek");

    if (!result)
	return -1;
	
    gios->eos = FALSE;
    return 0;
}

static off_t
stream_seek(GMimeStream *stream, off_t offset, GMimeSeekWhence whence)
{
    GMimeStreamGio *gios = (GMimeStreamGio *) stream;
    goffset real;
    gboolean result;
    GIO_DEBUG_INIT;
	
    g_return_val_if_fail(stream, -1);
    g_return_val_if_fail(gios->stream, -1);
	
    switch (whence) {
    case GMIME_STREAM_SEEK_SET:
	real = offset;
	break;
    case GMIME_STREAM_SEEK_CUR:
	real = stream->position + offset;
	break;
    case GMIME_STREAM_SEEK_END:
	if (offset > 0 || (stream->bound_end == -1 && !gios->eos)) {
	    /* need to do an actual lseek() here because
	     * we either don't know the offset of the end
	     * of the stream and/or don't know if we can
	     * seek past the end */
	    result = g_seekable_seek(G_SEEKABLE(gios->stream), (goffset)offset,
				     G_SEEK_END, NULL, GIO_DEBUG_OUT);
	    GIO_DEBUG_MSG("g_seekable_seek");
	    if (!result)
		return -1;
	    else
		real = g_seekable_tell(G_SEEKABLE(gios->stream));
	} else if (gios->eos && stream->bound_end == -1) {
	    /* seeking backwards from eos (which happens
	     * to be our current position) */
	    real = stream->position + offset;
	} else {
	    /* seeking backwards from a known position */
	    real = stream->bound_end + offset;
	}
		
	break;
    default:
	g_assert_not_reached ();
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
    
    result = g_seekable_seek(G_SEEKABLE(gios->stream), (goffset) real,
			     G_SEEK_SET, NULL, GIO_DEBUG_OUT);
    GIO_DEBUG_MSG("g_seekable_seek");
    if (!result)
	return -1;
    real = g_seekable_tell(G_SEEKABLE(gios->stream));
	
    /* reset eos if appropriate */
    if ((stream->bound_end != -1 && real < stream->bound_end) ||
	(gios->eos && real < stream->position))
	gios->eos = FALSE;
	
    stream->position = real;
	
    return real;
}

static off_t
stream_tell (GMimeStream *stream)
{
    g_return_val_if_fail(stream, -1);
    return stream->position;
}

static ssize_t
stream_length(GMimeStream *stream)
{
    goffset bound_end;
    GFileInputStream *istream;
    gboolean sres;
    GIO_DEBUG_INIT;
	
    g_return_val_if_fail(stream, -1);
    if (stream->bound_end != -1)
	return stream->bound_end - stream->bound_start;
    
    istream = g_file_read(GMIME_STREAM_GIO(stream)->gfile, NULL, GIO_DEBUG_OUT);
    GIO_DEBUG_MSG("g_file_read");
    if (!istream)
	return -1;

    sres = g_seekable_seek(G_SEEKABLE(istream), (goffset) 0, G_SEEK_END, NULL, GIO_DEBUG_OUT);
    GIO_DEBUG_MSG("g_seekable_seek");
    if (!sres) {
	g_object_unref(istream);
	return -1;
    }
    bound_end = g_seekable_tell(G_SEEKABLE(istream));
    g_object_unref(istream);
	
    if (bound_end < stream->bound_start)
	return -1;
	
    return bound_end - stream->bound_start;
}

static GMimeStream *
stream_substream(GMimeStream *stream, off_t start, off_t end)
{
    GMimeStreamGio *gios;
	
    g_return_val_if_fail(stream, NULL);
    gios = g_object_newv(GMIME_TYPE_STREAM_GIO, 0, NULL);
    g_mime_stream_construct (GMIME_STREAM(gios), start, end);
    gios->gfile = GMIME_STREAM_GIO(stream)->gfile;
    g_object_ref(G_OBJECT(gios->gfile));
    gios->eos = FALSE;
    gios->stream = NULL;
	
    return (GMimeStream *) gios;
}


/**
 * g_mime_stream_gio_new:
 * @gfile: GIO File
 *
 * Creates a new #GMimeStreamGio object around @gfile.
 *
 * Returns: a stream using @fd.
 **/
GMimeStream *
g_mime_stream_gio_new(GFile * gfile)
{
    GMimeStreamGio *gios;
	
    g_return_val_if_fail(gfile, NULL);
    gios = g_object_newv (GMIME_TYPE_STREAM_GIO, 0, NULL);
    g_mime_stream_construct(GMIME_STREAM (gios), 0, -1);
    gios->eos = FALSE;
    gios->gfile = gfile;
    g_object_ref(G_OBJECT(gios->gfile));
    gios->stream = NULL;
	
    return (GMimeStream *) gios;
}


/**
 * g_mime_stream_gio_new_with_bounds:
 * @fd: file descriptor
 * @start: start boundary
 * @end: end boundary
 *
 * Creates a new #GMimeStreamGio object around @fd with bounds @start
 * and @end.
 *
 * Returns: a stream using @fd with bounds @start and @end.
 **/
GMimeStream *
g_mime_stream_gio_new_with_bounds(GFile * gfile, off_t start, off_t end)
{
    GMimeStreamGio *gios;
	
    g_return_val_if_fail(gfile, NULL);
    gios = g_object_newv (GMIME_TYPE_STREAM_GIO, 0, NULL);
    g_mime_stream_construct (GMIME_STREAM (gios), start, end);
    gios->eos = FALSE;
    gios->gfile = gfile;
    g_object_ref(G_OBJECT(gios->gfile));
    gios->stream = NULL;
	
    return (GMimeStream *) gios;
}


#endif  /* HAVE_GIO */
