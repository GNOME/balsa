/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#include "abook-completion.h"
#include "information.h"
#include "misc.h"

#define CASE_INSENSITIVE_NAME

/*
 * Create a new CompletionData
 */
CompletionData *
completion_data_new(LibBalsaAddress * address)
{
    GString *string;
    GList *list;
#ifdef CASE_INSENSITIVE_NAME
    gchar *string_n;
#endif
    CompletionData *ret;

    ret = g_new0(CompletionData, 1);

    g_object_ref(address);
    ret->address = address;

    string = g_string_new(address->full_name);
    if (address->nick_name
        && strcmp(address->nick_name, address->full_name)) {
        g_string_append_c(string, ' ');
        g_string_append(string, address->nick_name);
    }
    for (list = address->address_list; list; list = list->next) {
	g_string_append_c(string, ' ');
	g_string_append(string, list->data);
    }
#ifdef CASE_INSENSITIVE_NAME
    string_n = g_utf8_normalize(string->str, -1, G_NORMALIZE_ALL);
    g_string_free(string, TRUE);
    ret->string = g_utf8_casefold(string_n, -1);
    g_free(string_n);
#else
    ret->string = g_string_free(string, FALSE);
#endif

    return ret;
}

/*
 * Free a CompletionData
 */
void
completion_data_free(CompletionData * data)
{
    g_object_unref(data->address);
    g_free(data->string);
    g_free(data);
}

/*
 * The GCompletionFunc
 */
gchar *
completion_data_extract(CompletionData * data)
{
    return data->string;
}

gint
address_compare(LibBalsaAddress *a, LibBalsaAddress *b)
{
    g_return_val_if_fail(a != NULL, -1);
    g_return_val_if_fail(b != NULL, 1);

    return g_ascii_strcasecmp(a->full_name, b->full_name);
}

/*
 * A GCompletionStrncmpFunc for matching words instead of the whole
 * string.
 *
 * s1 is the user input, s2 is the target.
 */

gint
strncmp_word(const gchar * s1, const gchar * s2, gsize n)
{
    const gchar *match;
    gint retval;

    g_return_val_if_fail(s1 != NULL, -1);
    g_return_val_if_fail(s2 != NULL, 1);

    match = s2;
    do {
	if (!(retval = strncmp(s1, match, n)))
	    break;
	if ((match = strchr(match, ' ')))
	    ++match;
    } while (match);

    return retval;
}
