/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2005 Stuart Parmenter and others,
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

#include "libbalsa-conf.h"

#if defined(HAVE_GNOME) && !defined(GNOME_DISABLE_DEPRECATED)

#include <string.h>

#define BALSA_CONFIG_PREFIX "balsa/"    /* FIXME */

void
libbalsa_conf_foreach_section(const gchar * prefix,
                              LibBalsaConfForeachFunc func,
                              gpointer data)
{
    gint pref_len;
    void *iterator;
    gchar *key;

    pref_len = strlen(prefix);
    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, NULL))) {
        if (strncmp(key, prefix, pref_len) == 0
            && func(key, key + pref_len, data)) {
            g_free(key);
            g_free(iterator);
            break;
        }
        g_free(key);
    }
}

#else                           /* HAVE_GNOME */

void
libbalsa_conf_foreach_section(const gchar * prefix,
                              LibBalsaConfForeachFunc func,
                              gpointer data)
{
    g_warning("Not implemented");
}

#endif                          /* HAVE_GNOME */
