/* Balsa E-Mail Client
 * (c) 1997-2000 Stuart Parmenter and others, see AUTHORS for a list of people
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

#ifndef __BALSA_QUOTECOLOR_H__
#define __BALSA_QUOTECOLOR_H__

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <regex.h>

extern void make_gradient (GdkColor colors[], gint, gint);
extern gint is_a_quote (const gchar *, const regex_t *rex);
extern void allocate_quote_colors (GtkWidget *, GdkColor color[],
      gint, gint);

 
#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __BALSA_QUOTECOLOR_H__ */
