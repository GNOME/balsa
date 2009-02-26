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


#ifndef __GMIME_STREAM_GVFS_H__
#define __GMIME_STREAM_GVFS_H__

/* note: this module will be compiled only if Gnome-Vfs is available */
#ifdef HAVE_GNOME_VFS

#include <libgnomevfs/gnome-vfs.h>
#include <gmime/gmime-stream.h>

G_BEGIN_DECLS

#define GMIME_TYPE_STREAM_GVFS            (g_mime_stream_gvfs_get_type ())
#define GMIME_STREAM_GVFS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GMIME_TYPE_STREAM_GVFS, GMimeStreamGvfs))
#define GMIME_STREAM_GVFS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GMIME_TYPE_STREAM_GVFS, GMimeStreamGvfsClass))
#define GMIME_IS_STREAM_GVFS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GMIME_TYPE_STREAM_GVFS))
#define GMIME_IS_STREAM_GVFS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GMIME_TYPE_STREAM_GVFS))
#define GMIME_STREAM_GVFS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GMIME_TYPE_STREAM_GVFS, GMimeStreamGvfsClass))

typedef struct _GMimeStreamGvfs GMimeStreamGvfs;
typedef struct _GMimeStreamGvfsClass GMimeStreamGvfsClass;

struct _GMimeStreamGvfs {
    GMimeStream parent_object;

    gboolean owner;
    gboolean eos;
    GnomeVFSHandle *handle;
};

struct _GMimeStreamGvfsClass {
    GMimeStreamClass parent_class;
};


GType g_mime_stream_gvfs_get_type(void);

GMimeStream *g_mime_stream_gvfs_new(GnomeVFSHandle * handle);
GMimeStream *g_mime_stream_gvfs_new_with_bounds(GnomeVFSHandle * handle,
                                                off_t start,
						off_t end);

gboolean g_mime_stream_gvfs_get_owner(GMimeStreamGvfs * stream);
void g_mime_stream_gvfs_set_owner(GMimeStreamGvfs * stream,
				  gboolean owner);

G_END_DECLS

#endif				/* HAVE_GNOME_VFS */

#endif				/* __GMIME_STREAM_GVFS_H__ */
