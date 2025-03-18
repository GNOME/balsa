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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "files.h"

#include <ctype.h>
#include <string.h>

#include <gio/gio.h>

#include "misc.h"
#include "libbalsa.h"
#include <glib/gi18n.h>

static GdkPixbuf *
libbalsa_default_attachment_pixbuf(gint size)
{
    char *icon;
    GdkPixbuf *tmp, *retval;

    icon = libbalsa_pixmap_finder ("attachment.png");
    tmp = gdk_pixbuf_new_from_file(icon, NULL);
    g_free(icon);
    if (!tmp)
        return NULL;

    retval = gdk_pixbuf_scale_simple(tmp, size, size, GDK_INTERP_BILINEAR);
    g_object_unref(tmp);

    return retval;
}


/* libbalsa_icon_finder:
 *   locate a suitable icon (pixmap graphic) based on 'mime-type' and/or
 *   'filename', either of which can be NULL.  If both arguments are
 *   non-NULL, 'mime-type' has priority.  If both are NULL, the default
 *   'attachment.png' icon will be returned.  This function *MUST*
 *   return the complete path to the icon file.
 */
GdkPixbuf *
libbalsa_icon_finder(GtkWidget         * widget,
                     const char        * mime_type,
                     const LibbalsaVfs * for_file,
                     gchar            ** used_type,
                     GtkIconSize         size)
{
    const gchar *content_type;
    GdkPixbuf *pixbuf = NULL;
    gint width, height;
    GtkIconTheme *icon_theme;
    GIcon *icon;

    if (!gtk_icon_size_lookup(size, &width, &height))
        width = 16;

    if (mime_type)
        content_type = mime_type;
    else if (for_file) {
        content_type = libbalsa_vfs_get_mime_type((LibbalsaVfs *) for_file);
    } else
        content_type = "application/octet-stream";

    /* ask GIO for the icon */
    if ((icon_theme = gtk_icon_theme_get_default()) == NULL)
        return NULL;

    icon = g_content_type_get_icon(content_type);

    if (icon != NULL) {
        if (G_IS_THEMED_ICON(icon)) {
            gint i;
            GStrv icon_names;

            g_object_get(icon, "names", &icon_names, NULL);

            if (icon_names != NULL) {
                for (i = 0; pixbuf == NULL && icon_names[i] != NULL; i++) {
                    pixbuf =
                        gtk_icon_theme_load_icon(icon_theme, icon_names[i], width,
                                                 GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
                }
                g_strfreev(icon_names);
            }
        }
        g_object_unref(icon);
    }

    if (pixbuf == NULL) {
        /* load the default pixbuf */
        pixbuf = libbalsa_default_attachment_pixbuf(width);
    }

    if (used_type) {
        g_free(*used_type);
        *used_type = g_strdup(content_type);
    }

    return pixbuf;
}
