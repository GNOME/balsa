/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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
#include "quote-color.h"

#include "balsa-app.h"


/*
 * static gint is_a_quote (const gchar *str, const regex_t *rex)
 *
 * Returns how deep a quotation is nested in str.  Uses quoted regex
 * from balsa_app.quote_regex, which can be set by the user.
 * 
 * Input:
 *   str  - string to match the regexp.
 *   preg - the regular expression that matches the prefix. see regex(7).
 * 
 * Output:
 *   an integer saying how many levels deep.  
 * */
guint
is_a_quote(const gchar * str, GRegex * rex)
{
    guint cnt;

    g_return_val_if_fail(rex != NULL, 0);

    if (str == NULL)
	return 0;

    libbalsa_match_regex(str, rex, &cnt, NULL);

    return cnt;
}
