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
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>
#include <libgnomevfs/gnome-vfs.h>

#include "misc.h"
#include "files.h"

#if BALSA_MAJOR > 1
#define PATH_SEP_STR G_DIR_SEPARATOR_S
#endif


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
	    g_strconcat(permanent_prefixes[i], PATH_SEP_STR, splice,
			PATH_SEP_STR, filename, NULL);

	if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
	    return cat;

	g_free(cat);
    }

    if(prefixes) {
        for (i = 0; prefixes[i]; i++) {
            cat =
                g_strconcat(prefixes[i], PATH_SEP_STR, splice, PATH_SEP_STR,
                            filename, NULL);
            
            if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
                return cat;
            
            g_free(cat);
        }
    }
    cat =  g_strconcat("images", PATH_SEP_STR, filename, NULL);
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
libbalsa_icon_finder(const char *mime_type, const char *filename, 
                     gchar** used_type, GtkIconSize size)
{
    char *content_type;
    const char *icon_file;
    gchar *icon;
    GdkPixbuf *pixbuf = NULL;
    gint width, height;
#if GTK_CHECK_VERSION(2, 4, 0)
    GtkIconTheme *icon_theme;
#endif

    if (!gtk_icon_size_lookup(size, &width, &height))
	width = height = 16;
    
    if (mime_type)
        content_type = g_strdup(mime_type);
    else if(filename)
        content_type = libbalsa_lookup_mime_type(filename);
#if GTK_CHECK_VERSION(2, 4, 0)
    else
	content_type = g_strdup("application/octet-stream");

#else                           /* GTK_CHECK_VERSION(2, 4, 0) */
    else {
        if(used_type) *used_type = g_strdup("application/octet-stream");
        return libbalsa_default_attachment_pixbuf(width);
    }
#endif                          /* GTK_CHECK_VERSION(2, 4, 0) */
  
#if GTK_CHECK_VERSION(2, 4, 0)
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
#endif

    icon_file = gnome_vfs_mime_get_icon(content_type);
    
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


#if !GTK_CHECK_VERSION(2, 4, 0)
static gboolean in_gnome_vfs(const GnomeVFSMimeApplication *default_app, 
                             const GList *short_list, const gchar *cmd) 
{
    gchar *cmd_base=g_strdup(cmd), *arg=strchr(cmd_base, '%');
    
    /* Note: Tries to remove the entrire argument containing %f etc., so that
             we e.g. get rid of the whole "file:%f", not just "%f" */
    if(arg) {
        while(arg!=cmd && *arg!=' ')
            arg--;
        
        *arg='\0';
    }
    g_strstrip(cmd_base);
    
    if(default_app && default_app->command &&
       strcmp(default_app->command, cmd_base)==0) {
        g_free(cmd_base);
        return TRUE;
    } else {
        const GList *item;

        for(item=short_list; item; item=g_list_next(item)) {
            GnomeVFSMimeApplication *app=item->data;
            
            if(app->command && strcmp(app->command, cmd_base)==0) {
                g_free(cmd_base);
                return TRUE;
            }
        }
    }
    g_free(cmd_base);
    
    return FALSE;
}
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

/* helper: fill the passed menu with vfs items */
void
libbalsa_fill_vfs_menu_by_content_type(GtkMenu * menu, const gchar * content_type,
				       GCallback callback, gpointer data)
{
    GList* list;
#if GTK_CHECK_VERSION(2, 4, 0)
    GnomeVFSMimeApplication *def_app;
    GList *app_list;
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    GtkWidget *menu_item;
    GList* key_list, *app_list;
    gchar* key;
    const gchar* cmd;
    gchar* menu_label;
    gchar** split_key;
    gint i;
    GnomeVFSMimeApplication *def_app, *app;
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    
#if !GTK_CHECK_VERSION(2, 4, 0)
    key_list = list = gnome_vfs_mime_get_key_list(content_type);
    /* gdk_threads_leave(); releasing GDK lock was necessary for broken
     * gnome-vfs versions */
    app_list = gnome_vfs_mime_get_short_list_applications(content_type);
    /* gdk_threads_enter(); */
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

    if((def_app=gnome_vfs_mime_get_default_application(content_type))) {
        add_vfs_menu_item(menu, def_app, callback, data);
    }
    

#if GTK_CHECK_VERSION(2, 4, 0)
    app_list = gnome_vfs_mime_get_all_applications(content_type);
    for (list = app_list; list; list = list->next) {
        GnomeVFSMimeApplication *app = list->data;
        if (app && (!def_app || strcmp(app->name, def_app->name) != 0))
            add_vfs_menu_item(menu, app, callback, data);
    }
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    while (list) {
        key = list->data;

        if (key && g_ascii_strcasecmp (key, "icon-filename") 
            && g_ascii_strncasecmp (key, "fm-", 3)
	    && g_ascii_strncasecmp (key, "category", 8)
            /* Get rid of additional GnomeVFS entries: */
            && (!strstr(key, "_") || strstr(key, "."))
            && g_ascii_strncasecmp(key, "description", 11)) {
            
            if ((cmd = gnome_vfs_mime_get_value (content_type, key)) != NULL &&
                !in_gnome_vfs(def_app, app_list, cmd)) {
                if (g_ascii_strcasecmp (key, "open") == 0 || 
                    g_ascii_strcasecmp (key, "view") == 0 || 
                    g_ascii_strcasecmp (key, "edit") == 0 ||
                    g_ascii_strcasecmp (key, "ascii-view") == 0) {
                    /* uppercase first letter, make label */
                    menu_label = g_strdup_printf ("%s (\"%s\")", key, cmd);
                    *menu_label = toupper (*menu_label);
                } else {
                    split_key = g_strsplit (key, ".", -1);

                    i = 0;
                    while (split_key[i+1] != NULL) {
                        ++i;
                    }
                    menu_label = split_key[i];
                    menu_label = g_strdup (menu_label);
                    g_strfreev (split_key);
                }
                menu_item = gtk_menu_item_new_with_label (menu_label);
                g_object_set_data (G_OBJECT (menu_item), "mime_action", 
                                   key);
                g_signal_connect (G_OBJECT (menu_item), "activate",
				  callback, data);
                gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
                g_free (menu_label);
            }
        }
        list = g_list_next (list);
    }

    list=app_list;

    while (list) {
        app=list->data;

        if(app && (!def_app || strcmp(app->name, def_app->name)!=0)) {
            add_vfs_menu_item(menu, app, callback, data);
        }

        list = g_list_next (list);
    }
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    gnome_vfs_mime_application_free(def_app);
    

#if !GTK_CHECK_VERSION(2, 4, 0)
    g_list_free (key_list);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    gnome_vfs_mime_application_list_free (app_list);
}

