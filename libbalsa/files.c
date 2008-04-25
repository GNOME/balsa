/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include "config.h"
#include <ctype.h>
#include <string.h>

#ifdef HAVE_GNOME
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>
#include <libgnomevfs/gnome-vfs.h>
#endif

#include "misc.h"
#include "files.h"
#include <glib/gi18n.h>
#include "libbalsa-vfs.h"

static const gchar *permanent_prefixes[] = {
/*	BALSA_DATA_PREFIX,
	BALSA_STD_PREFIX,
	GNOME_DATA_PREFIX
	GNOME_STD_PREFIX,
	GNOME_LIB_PREFIX,*/
    BALSA_COMMON_PREFIXES,
    "src",
    ".",
    NULL
};

/* filename is the filename (naw!)
 * splice is what to put in between the prefix and the filename, if desired
 * prefixes is a null-termed array of strings of prefixes to try. There are defaults that are always
 *   tried.
 * We ignore proper slashing of names. Ie, /prefix//splice//file won't be caught.
 */
gchar *
balsa_file_finder(const gchar * filename, const gchar * splice,
		  const gchar ** prefixes, gboolean warn)
{
    gchar *cat;
    int i;

    g_return_val_if_fail(filename, NULL);

    if (splice == NULL)
	splice = "";

    for (i = 0; permanent_prefixes[i]; i++) {
	cat =
	    g_strconcat(permanent_prefixes[i], G_DIR_SEPARATOR_S, splice,
			G_DIR_SEPARATOR_S, filename, NULL);

	if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
	    return cat;

	g_free(cat);
    }

    if(prefixes) {
        for (i = 0; prefixes[i]; i++) {
            cat =
                g_strconcat(prefixes[i], G_DIR_SEPARATOR_S, splice,
			    G_DIR_SEPARATOR_S, filename, NULL);
            
            if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
                return cat;
            
            g_free(cat);
        }
    }
    cat =  g_strconcat("images", G_DIR_SEPARATOR_S, filename, NULL);
    if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
        return cat;
    g_free(cat);

    if (warn)
        g_warning("Cannot find expected file \"%s\" "
                  "(spliced with \"%s\") %s extra prefixes",
	          filename, splice,
                  prefixes ? "even with" : "with no");
    return NULL;
}


static GdkPixbuf *
libbalsa_default_attachment_pixbuf(gint size)
{
    char *icon;
    GdkPixbuf *tmp, *retval;
    GError * error = NULL;

    icon = balsa_pixmap_finder ("attachment.png");
    tmp = gdk_pixbuf_new_from_file(icon, &error);
    g_free(icon);
    if (!tmp) {
	g_error_free(error);
        return NULL;
    }

    retval = gdk_pixbuf_scale_simple(tmp, size, size, GDK_INTERP_BILINEAR);
    g_object_unref(tmp);

    return retval;
}


/* balsa_icon_finder:
 *   locate a suitable icon (pixmap graphic) based on 'mime-type' and/or
 *   'filename', either of which can be NULL.  If both arguments are
 *   non-NULL, 'mime-type' has priority.  If both are NULL, the default
 *   'attachment.png' icon will be returned.  This function *MUST*
 *   return the complete path to the icon file.
 */
GdkPixbuf *
libbalsa_icon_finder(const char *mime_type, const LibbalsaVfs * for_file, 
                     gchar** used_type, GtkIconSize size)
{
    char *content_type;
    gchar *icon = NULL;
    GdkPixbuf *pixbuf = NULL;
    gint width, height;
#ifdef HAVE_GNOME
    const char *icon_file;
    GtkIconTheme *icon_theme;
    const gchar * filename = NULL;
#endif

    if (!gtk_icon_size_lookup(size, &width, &height))
	width = height = 16;
    
    if (mime_type)
        content_type = g_strdup(mime_type);
    else if (for_file) {
        content_type = g_strdup(libbalsa_vfs_get_mime_type(for_file));
        filename = libbalsa_vfs_get_uri(for_file);
    } else
	content_type = g_strdup("application/octet-stream");

#ifdef HAVE_GNOME
    /* gtk+ 2.4.0 and above: use the default icon theme to get the icon */
    if ((icon_theme = gtk_icon_theme_get_default()))
	if ((icon =
	     gnome_icon_lookup(icon_theme, NULL, filename, NULL, NULL,
			       content_type, 0, NULL))) {
	    pixbuf =
		gtk_icon_theme_load_icon(icon_theme, icon, width, 0, NULL);
	    g_free(icon);
	    if (pixbuf) {
		if (used_type)
		    *used_type = content_type;
		else 
		    g_free(content_type);
		return pixbuf;
	    }
	}

#if 0
    icon_file = gnome_vfs_mime_get_icon(content_type);
#else
    icon_file = gnome_vfs_mime_get_value(content_type, "icon_filename");
#endif
    
    /* check if the icon file is good and try harder otherwise */
    if (icon_file && g_file_test (icon_file, G_FILE_TEST_IS_REGULAR))
	icon = g_strdup(icon_file);
    else {
	gchar *gnome_icon, *p_gnome_icon, *tmp;
  	
	gnome_icon = g_strdup_printf ("gnome-%s.png", content_type);   
	p_gnome_icon = strchr (gnome_icon, '/');
	if (p_gnome_icon != NULL)
	    *p_gnome_icon = '-';

        tmp = g_strconcat("document-icons/", gnome_icon, NULL);
        icon = gnome_vfs_icon_path_from_filename(tmp);
        g_free(tmp);

	if (icon == NULL)
            icon = balsa_pixmap_finder_no_warn (gnome_icon);
	
	g_free (gnome_icon);
    }
#endif /* HAVE_GNOME */
    /* load the pixbuf */
    if (icon == NULL)
	pixbuf = libbalsa_default_attachment_pixbuf(width);
    else {
	GError *error = NULL;
	GdkPixbuf *tmp_pb;
	
	if ((tmp_pb = gdk_pixbuf_new_from_file(icon, &error))) {
	    pixbuf = gdk_pixbuf_scale_simple(tmp_pb, width, width,
					     GDK_INTERP_BILINEAR);
	    g_object_unref(tmp_pb);
	} else {
	    pixbuf = libbalsa_default_attachment_pixbuf(width);
 	    g_error_free(error);
	}
	g_free(icon);
    }
    
    if(used_type) *used_type = content_type;
    else g_free(content_type);
    
    return pixbuf;
}

#ifdef HAVE_GNOME
static void 
add_vfs_menu_item(GtkMenu * menu, const GnomeVFSMimeApplication *app,
		  GCallback callback, gpointer data)
{
    gchar *menu_label = g_strdup_printf(_("Open with %s"), app->name);
    GtkWidget *menu_item = gtk_menu_item_new_with_label (menu_label);
    
    g_object_set_data_full(G_OBJECT (menu_item), "mime_action", 
			   g_strdup(app->id), g_free);
    g_signal_connect(G_OBJECT (menu_item), "activate", callback, data);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
    g_free (menu_label);
}
#endif /* HAVE_GNOME */
/* helper: fill the passed menu with vfs items */
void
libbalsa_fill_vfs_menu_by_content_type(GtkMenu * menu,
				       const gchar * content_type,
				       GCallback callback, gpointer data)
{
#ifdef HAVE_GNOME
    GList* list;
    GnomeVFSMimeApplication *def_app;
    GList *app_list;
    
    g_return_if_fail(data != NULL);

    if((def_app=gnome_vfs_mime_get_default_application(content_type))) {
        add_vfs_menu_item(menu, def_app, callback, data);
    }
    

    app_list = gnome_vfs_mime_get_all_applications(content_type);
    for (list = app_list; list; list = list->next) {
        GnomeVFSMimeApplication *app = list->data;
        if (app && (!def_app || strcmp(app->name, def_app->name) != 0))
            add_vfs_menu_item(menu, app, callback, data);
    }
    gnome_vfs_mime_application_free(def_app);
    
    gnome_vfs_mime_application_list_free (app_list);
#endif /* HAVE_GNOME */
}

