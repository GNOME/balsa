/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Jay Painter and Stuart Parmenter
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

#include "libbalsa.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <ctype.h>

#include <sys/stat.h>	/* for check_if_regular_file() */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "misc.h"
#include "mime.h"
#include "sendmsg-window.h"
#include "address-book.h"
#include "main.h"
#include "quote-color.h"


/*
 * void make_gradient()
 *
 * Makes a gradient color on the array, from the first entry to the last.
 */
void
make_gradient (GdkColor colors[], gint first, gint last)
{
   gint /*add,*/ i;
   double dr, dg, db;

   /*g_message ("make_gradient(): Start");*/
   dr = (double) (colors[last].red - colors[first].red) / (last - first + 1);
   dg = (double) (colors[last].green - colors[first].green)/(last - first + 1);
   db = (double) (colors[last].blue - colors[first].blue) / (last - first + 1);
   for (i = (first + 1); i < last; i++)
   {
      colors[i].red = colors[i - 1].red + dr;
      colors[i].blue = colors[i - 1].blue + db;
      colors[i].green = colors[i - 1].green + dg;
   }
   /*g_message ("make_gradient(): End");*/
}


/*
 * static gint is_a_quote (gchar *str)
 *
 * Returns how deep a quotation is nested in str.
 * It takes the same regexp as Mutt's default:
 *   ^([ \t]*[|>:}#])+
 * 
 * Input:
 *   str - string to match the regexp.
 * Output:
 *   an integer saying how many levels deep.
 */
extern gint
is_a_quote (gchar *str)
{
   gchar *s;
   gint i = 0;

   s = str;
   while (s != NULL)
   {
	  switch (s[0])
	  {
		 case '|':
		 case '>':
		 case ':':
		 case '}':
		 case '#':
			i++;
			break;

		 case ' ':
		 case '\t':
			break;

		 default:
			return i;
	  }
	  s++;
   }
   return i;
}


/*
 * void allocate_quote_colors.
 *
 * Allocate a color for each of the gradients from the correct
 * colormap.
 */
void
allocate_quote_colors (GtkWidget *widget, GdkColor color[],
                       gint first, gint last)
{
   gint i;

   for (i = first; i <= last; i++)
   {
      if (!gdk_colormap_alloc_color (balsa_app.colormap, &color[i], FALSE, TRUE))
	gdk_color_black (balsa_app.colormap, &color[i]);
   }
}

