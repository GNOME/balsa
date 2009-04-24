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


#ifndef __GMIME_STREAM_GIO_H__
#define __GMIME_STREAM_GIO_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include <gio/gio.h>
#include <gmime/gmime-stream.h>

G_BEGIN_DECLS

#define GMIME_TYPE_STREAM_GIO            (g_mime_stream_gio_get_type())
#define GMIME_STREAM_GIO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GMIME_TYPE_STREAM_GIO, GMimeStreamGio))
#define GMIME_STREAM_GIO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GMIME_TYPE_STREAM_GIO, GMimeStreamGioClass))
#define GMIME_IS_STREAM_GIO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GMIME_TYPE_STREAM_GIO))
#define GMIME_IS_STREAM_GIO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GMIME_TYPE_STREAM_GIO))
#define GMIME_STREAM_GIO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GMIME_TYPE_STREAM_GIO, GMimeStreamGioClass))

typedef struct _GMimeStreamGio GMimeStreamGio;
typedef struct _GMimeStreamGioClass GMimeStreamGioClass;

struct _GMimeStreamGio {
    GMimeStream parent_object;
	
    gboolean eos;
    GFile *gfile;
    GObject *stream;
};

struct _GMimeStreamGioClass {
    GMimeStreamClass parent_class;
};


GType g_mime_stream_gio_get_type(void);

GMimeStream *g_mime_stream_gio_new(GFile * gfile);
GMimeStream *g_mime_stream_gio_new_with_bounds(GFile * gfile,
					       gint64 start,
					       gint64 end);

G_END_DECLS

#endif /* __GMIME_STREAM_GIO_H__ */
