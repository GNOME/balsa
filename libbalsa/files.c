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

static const gchar *permanent_prefixes[] = {
    BALSA_DATA_PREFIX,
    "src",
    "."
};

/* filename is the filename (naw!)
 * splice is what to put in between the prefix and the filename, if desired
 * We ignore proper slashing of names. Ie, /prefix//splice//file won't be caught.
 */
gchar *
balsa_file_finder(const char * filename,
                  const char * splice)
{
    char *cat;
    unsigned i;

    g_return_val_if_fail(filename != NULL, NULL);

    if (splice == NULL)
	splice = "";

    for (i = 0; i < G_N_ELEMENTS(permanent_prefixes); i++) {
	cat = g_build_filename(permanent_prefixes[i], splice, filename, NULL);

	if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
	    return cat;

	g_free(cat);
    }

    cat = g_build_filename("images", filename, NULL);
    if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
        return cat;
    g_free(cat);

    g_warning("Cannot find expected file “%s” (spliced with “%s”)", filename, splice);

    return NULL;
}


static GIcon *
libbalsa_default_attachment_icon(void)
{
    char *path;
    GFile *file;
    GIcon *gicon;

    path = balsa_pixmap_finder("attachment.png");
    file = g_file_new_for_path(path);
    g_free(path);

    gicon = g_file_icon_new(file);
    g_object_unref(file);

    return gicon;
}


/* libbalsa_icon_finder:
 *   locate a suitable icon based on 'mime-type' and/or
 *   'filename', either of which can be NULL.  If both arguments are
 *   non-NULL, 'mime-type' has priority.  If both are NULL, the default
 *   'attachment.png' icon will be returned.
 */
GIcon *
libbalsa_icon_finder(const char        * mime_type,
                     const LibbalsaVfs * for_file,
                     char             ** used_type)
{
    const gchar *content_type;
    GIcon *gicon;

    if (mime_type)
        content_type = mime_type;
    else if (for_file) {
        content_type = libbalsa_vfs_get_mime_type((LibbalsaVfs *) for_file);
    } else
        content_type = "application/octet-stream";

    gicon = g_content_type_get_icon(content_type);

    if (gicon == NULL) {
        /* load the default icon */
        gicon = libbalsa_default_attachment_icon();
    }

    if (used_type)
        *used_type = g_strdup(content_type);

    return gicon;
}


/* libbalsa_icon_name_finder:
 *   locate a suitable icon based on 'mime-type' and/or
 *   'filename', either of which can be NULL.  If both arguments are
 *   non-NULL, 'mime-type' has priority.  If both are NULL, the default
 *   "attachment" icon name will be returned.
 */
char *
libbalsa_icon_name_finder(const char        * mime_type,
                          const LibbalsaVfs * for_file,
                          char             ** used_type)
{
    const char *content_type;
    char *content_icon;

    if (mime_type)
        content_type = mime_type;
    else if (for_file) {
        content_type = libbalsa_vfs_get_mime_type((LibbalsaVfs *) for_file);
    } else
        content_type = "application/octet-stream";

    content_icon = g_content_type_get_generic_icon_name(content_type);

    if (content_icon == NULL) {
        /* load the default icon */
        content_icon = g_strdup("attachment");
    }

    if (used_type)
        *used_type = g_strdup(content_type);

    return content_icon;
}
