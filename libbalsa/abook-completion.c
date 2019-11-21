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
#include "abook-completion.h"

#include <string.h>

#include "information.h"
#include "misc.h"

#define CASE_INSENSITIVE_NAME

/*
 * Create a new CompletionData
 */
CompletionData *
completion_data_new(InternetAddress * ia, const gchar * nick_name)
{
    GString *string;
    gchar *address_string;
    gchar *p, *q;
#ifdef CASE_INSENSITIVE_NAME
    gchar *string_n;
#endif
    CompletionData *ret;

    ret = g_new(CompletionData, 1);

    ret->ia = g_object_ref(ia);

    string = g_string_new(nick_name);
    if (string->len > 0)
	g_string_append_c(string, ' ');
    address_string = internet_address_to_string(ia, NULL, FALSE);
    /* Remove '"' and '<'. */
    for (p = q = address_string; *p; p++)
        if (*p != '"' && *p != '<')
            *q++ = *p;
    *q = '\0';
    g_string_append(string, address_string);
    g_free(address_string);
#ifdef CASE_INSENSITIVE_NAME
    string_n = g_utf8_normalize(string->str, -1, G_NORMALIZE_ALL);
    g_string_free(string, TRUE);
    if (string_n != NULL) {
        ret->string = g_utf8_casefold(string_n, -1);
        g_free(string_n);
    }
#else
    ret->string = g_string_free(string, FALSE);
#endif

    return ret;
}

/*
 * Free a CompletionData
 */
void
completion_data_free(CompletionData * data, gpointer unused G_GNUC_UNUSED)
{
    g_object_unref(data->ia);
    g_free(data->string);
    g_free(data);
}

/*
 * The LibBalsaCompletionFunc
 */
gchar *
completion_data_extract(CompletionData * data)
{
    return data->string;
}

/*
 * A LibBalsaCompletionStrncmpFunc for matching words instead of the
 * whole string.
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
