/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * libbalsa vfs glue layer library
 * Copyright (C) 2008 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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

#ifndef __LIBBALSA_VFS_H__
#define __LIBBALSA_VFS_H__


#include <glib.h>
#include <glib-object.h>
#include <gmime/gmime.h>
#include <gtk/gtk.h>
#include "libbalsa.h"
#include "misc.h"


G_BEGIN_DECLS


/* a vfs file description as GObject */
#define LIBBALSA_TYPE_VFS            (libbalsa_vfs_get_type())
#define LIBBALSA_VFS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), LIBBALSA_TYPE_VFS, LibbalsaVfs))
#define LIBBALSA_VFS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), LIBBALSA_TYPE_VFS, LibbalsaVfsClass))
#define LIBBALSA_IS_VFS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), LIBBALSA_TYPE_VFS))
#define LIBBALSA_IS_VFS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), LIBBALSA_TYPE_VFS))
#define LIBBALSA_VFS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), LIBBALSA_TYPE_VFS, LibbalsaVfsClass))

typedef struct _LibbalsaVfsClass LibbalsaVfsClass;


struct _LibbalsaVfs {
    GObject parent;

    struct _LibbalsaVfsPriv * priv;
};

struct _LibbalsaVfsClass {
    GObjectClass parent;
};

gboolean libbalsa_vfs_local_only(void);

GType libbalsa_vfs_get_type(void);
LibbalsaVfs * libbalsa_vfs_new(void);
LibbalsaVfs * libbalsa_vfs_new_from_uri(const gchar * uri);
LibbalsaVfs * libbalsa_vfs_append(const LibbalsaVfs * file,
                                  const gchar * text);
LibbalsaVfs * libbalsa_vfs_dir_append(const LibbalsaVfs * dir,
                                      const gchar * filename);
const gchar * libbalsa_vfs_get_folder(const LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_uri(const LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_uri_utf8(const LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_basename_utf8(const LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_mime_type(const LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_charset(const LibbalsaVfs * file);
LibBalsaTextAttribute libbalsa_vfs_get_text_attr(const LibbalsaVfs * file);
guint64 libbalsa_vfs_get_size(const LibbalsaVfs * file);
GMimeStream * libbalsa_vfs_create_stream(const LibbalsaVfs * file,
                                         mode_t mode, 
                                         gboolean rdwr,
                                         GError ** err);
gboolean libbalsa_vfs_file_exists(const LibbalsaVfs * file);
gboolean libbalsa_vfs_is_regular_file(const LibbalsaVfs * file,
                                      GError **err);
gint libbalsa_vfs_file_unlink(const LibbalsaVfs * file,
                              GError **err);

/* application launch helpers */
gboolean libbalsa_vfs_launch_app(const LibbalsaVfs * file,
                                 GObject * object,
                                 GError **err);
gboolean libbalsa_vfs_launch_app_for_body(LibBalsaMessageBody * mime_body,
                                          GObject * object,
                                          GError **err);
void libbalsa_vfs_fill_menu_by_content_type(GtkMenu * menu,
                                            const gchar * content_type,
                                            GCallback callback,
                                            gpointer data);
GtkWidget * libbalsa_vfs_mime_button(LibBalsaMessageBody * mime_body,
                                     const gchar * content_type,
                                     GCallback callback,
                                     gpointer data);

/* content type helpers */
gchar * libbalsa_vfs_content_description(const gchar * mime_type);
gchar * libbalsa_vfs_content_type_of_buffer(const guchar * buffer,
                                            gsize length);

G_END_DECLS


#endif				/* __LIBBALSA_VFS_H__ */
