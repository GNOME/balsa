/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>

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

	if (g_file_exists(cat))
	    return cat;

	g_free(cat);
    }

    if (prefixes == NULL) {
	if (warn)
            g_warning("Cannot find expected file \"%s\" "
                      "(spliced with \"%s\") with no extra prefixes",
	              filename, splice);
	return NULL;
    }

    for (i = 0; prefixes[i]; i++) {
	cat =
	    g_strconcat(prefixes[i], PATH_SEP_STR, splice, PATH_SEP_STR,
			filename, NULL);

	if (g_file_exists(cat))
	    return cat;

	g_free(cat);
    }

    if (warn)
        g_warning("Cannot find expected file \"%s\" "
                  "(spliced with \"%s\") even with extra prefixes",
	          filename, splice);
    return NULL;
}

/* balsa_icon_finder:
 *   locate a suitable icon (pixmap graphic) based on 'mime-type' and/or
 *   'filename', either of which can be NULL.  If both arguments are
 *   non-NULL, 'mime-type' has priority.  If both are NULL, the default
 *   'balsa/attachment.png' icon will be returned.  This function *MUST*
 *   return the complete path to the icon file.
 */
gchar *
libbalsa_icon_finder(const char *mime_type, const char *filename)
{
    const char *content_type, *icon_file;
    gchar *icon = NULL;
    
    if(mime_type)
        content_type = mime_type;
    else {
        if(!filename)
            return balsa_pixmap_finder ("balsa/attachment.png");
        content_type = libbalsa_lookup_mime_type(mime_type);
    }
    /* FIXME:
       or icon_file = gnome_desktop_item_find_icon(GVMGI(content_Type)?) */
    icon_file = gnome_vfs_mime_get_icon(content_type);
    if ( icon_file ) 
	icon = g_strdup (icon_file);

    if (!icon || !g_file_exists (icon)) {
	gchar *gnome_icon, *p_gnome_icon;
	
	gnome_icon = g_strdup_printf ("gnome-%s.png", content_type);   
	
	p_gnome_icon = strchr (gnome_icon, '/');
	if (p_gnome_icon != NULL)
	    *p_gnome_icon = '-';

	icon = balsa_pixmap_finder_no_warn (gnome_icon);

	/*
	 * FIXME: Should use a better icon. Since this one is small
	 * In pratice I don't think we will ever make it this far...
	 */
	if ( icon == NULL )
	    icon = balsa_pixmap_finder ("balsa/attachment.png");
	
	g_free (gnome_icon);
    }

    return (icon);
}
