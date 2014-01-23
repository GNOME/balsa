/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "quote-color.h"

#include "balsa-app.h"


/*
 * void make_gradient()
 *
 * Makes a gradient color on the array, from the first entry to the last.
 */
void
make_gradient(GdkColor colors[], gint first, gint last)
{
    gint /*add, */ i;
    double dr, dg, db;

    dr = (double) (colors[last].red - colors[first].red) / (last - first + 1);
    dg = (double) (colors[last].green - colors[first].green) / (last - first + 1);
    db = (double) (colors[last].blue - colors[first].blue) / (last - first + 1);

    for (i = (first + 1); i < last; i++) {
	colors[i].red = colors[i - 1].red + dr;
	colors[i].blue = colors[i - 1].blue + db;
	colors[i].green = colors[i - 1].green + dg;
    }
}

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
#if USE_GREGEX
is_a_quote(const gchar * str, GRegex * rex)
#else                           /* USE_GREGEX */
is_a_quote(const gchar * str, regex_t * rex)
#endif                          /* USE_GREGEX */
{
    guint cnt;

    g_return_val_if_fail(rex != NULL, 0);

    if (str == NULL)
	return 0;

    libbalsa_match_regex(str, rex, &cnt, NULL);

    return cnt;
}


/*
 * void allocate_quote_colors.
 *
 * Allocate a color for each of the gradients from the correct
 * colormap.
 */
void
allocate_quote_colors(GtkWidget * widget, GdkColor color[],
                      gint first, gint last)
{
    gint i;

    for (i = first; i <= last; i++) {
        if (!gdk_colormap_alloc_color(balsa_app.colormap,
                                      &color[i], FALSE, TRUE)) {
            /* Use black instead: */
            color[i].red = color[i].green = color[i].blue = 0;
            gdk_colormap_alloc_color(balsa_app.colormap, &color[i], FALSE,
                                     TRUE);
        }
    }
}



