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
#include <string.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
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

/* balsa_icon_finder:
 *   locate a suitable icon (pixmap graphic) based on 'mime-type' and/or
 *   'filename', either of which can be NULL.  If both arguments are
 *   non-NULL, 'mime-type' has priority.  If both are NULL, the default
 *   'attachment.png' icon will be returned.  This function *MUST*
 *   return the complete path to the icon file.
 */
gchar *
libbalsa_icon_finder(const char *mime_type, const char *filename, 
                     gchar** used_type)
{
    char *content_type;
    const char *icon_file;
    gchar *icon = NULL;
    
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
        return balsa_pixmap_finder ("attachment.png");
    }
#endif                          /* GTK_CHECK_VERSION(2, 4, 0) */
    /* FIXME:
       or icon_file = gnome_desktop_item_find_icon(GVMGI(content_Type)?) */
    icon_file = gnome_vfs_mime_get_icon(content_type);
    if ( icon_file ) 
	icon = g_strdup (icon_file);

    if (!icon || !g_file_test (icon, G_FILE_TEST_IS_REGULAR)) {
	gchar *gnome_icon, *p_gnome_icon, *tmp;
#if GTK_CHECK_VERSION(2, 4, 0)
	GtkIconTheme *icon_theme;
	GtkIconInfo *icon_info;

	gnome_icon = g_strdup_printf("gnome-mime-%s", content_type);   
#else                           /* GTK_CHECK_VERSION(2, 4, 0) */
	
	gnome_icon = g_strdup_printf ("gnome-%s.png", content_type);   
#endif                          /* GTK_CHECK_VERSION(2, 4, 0) */
	
	p_gnome_icon = strchr (gnome_icon, '/');
	if (p_gnome_icon != NULL)
	    *p_gnome_icon = '-';

#if GTK_CHECK_VERSION(2, 4, 0)
	icon_theme = gtk_icon_theme_get_default();
	icon_info = gtk_icon_theme_lookup_icon(icon_theme, gnome_icon, 48, 0);
	if (icon_info) {
	    g_free(gnome_icon);
	    icon = g_strdup(gtk_icon_info_get_filename(icon_info));
	    gtk_icon_info_free(icon_info);
	    if (used_type)
		*used_type = content_type;
	    return icon;
	}
#endif                          /* GTK_CHECK_VERSION(2, 4, 0) */
        tmp = g_strconcat("document-icons/", gnome_icon, NULL);
	g_free(icon);
        icon = gnome_vfs_icon_path_from_filename(tmp);
        g_free(tmp);

	if (icon == NULL)
            icon = balsa_pixmap_finder_no_warn (gnome_icon);

	/*
	 * FIXME: Should use a better icon. Since this one is small
	 * In pratice I don't think we will ever make it this far...
	 */
	if ( icon == NULL )
	    icon = balsa_pixmap_finder ("attachment.png");
	
	g_free (gnome_icon);
    }

    if(used_type) *used_type = content_type;
    else g_free(content_type);

    return (icon);
}
