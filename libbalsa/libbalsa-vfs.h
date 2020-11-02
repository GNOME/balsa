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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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

#define LIBBALSA_VFS_MIME_ACTION "mime-action"

/* a vfs file description as GObject */
#define LIBBALSA_TYPE_VFS (libbalsa_vfs_get_type())

G_DECLARE_FINAL_TYPE(LibbalsaVfs,
                     libbalsa_vfs,
                     LIBBALSA,
                     VFS,
                     GObject)


LibbalsaVfs * libbalsa_vfs_new(void);
LibbalsaVfs * libbalsa_vfs_new_from_uri(const gchar * uri);
LibbalsaVfs * libbalsa_vfs_append(LibbalsaVfs * file,
                                  const gchar * text);
LibbalsaVfs * libbalsa_vfs_dir_append(LibbalsaVfs * dir,
                                      const gchar * filename);
const gchar * libbalsa_vfs_get_folder(LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_uri(LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_uri_utf8(LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_basename_utf8(LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_mime_type(LibbalsaVfs * file);
const gchar * libbalsa_vfs_get_charset(LibbalsaVfs * file);
LibBalsaTextAttribute libbalsa_vfs_get_text_attr(LibbalsaVfs * file);
guint64 libbalsa_vfs_get_size(LibbalsaVfs * file);
GMimeStream * libbalsa_vfs_create_stream(LibbalsaVfs * file,
                                         mode_t mode, 
                                         gboolean rdwr,
                                         GError ** err);
gboolean libbalsa_vfs_file_exists(LibbalsaVfs * file);
gboolean libbalsa_vfs_is_regular_file(LibbalsaVfs * file,
                                      GError **err);
gint libbalsa_vfs_file_unlink(LibbalsaVfs * file,
                              GError **err);

/* application launch helpers */
gboolean libbalsa_vfs_launch_app(LibbalsaVfs *file,
                                 const gchar *app_name,
                                 GError     **err);
gboolean libbalsa_vfs_launch_app_for_body(LibBalsaMessageBody *mime_body,
                                          const gchar         *app_name,
                                          GError             **err);
void libbalsa_vfs_fill_menu_by_content_type(GMenu       *menu,
                                            const gchar *content_type,
                                            const gchar *action);
GtkWidget * libbalsa_vfs_mime_button(const gchar * content_type,
                                     GCallback     callback,
                                     gpointer      data);

/* content type helpers */
gchar * libbalsa_vfs_content_description(const gchar * mime_type);
gchar * libbalsa_vfs_content_type_of_buffer(const guchar * buffer,
                                            gsize length);

G_END_DECLS


#endif				/* __LIBBALSA_VFS_H__ */
