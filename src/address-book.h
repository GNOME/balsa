/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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
#ifndef __ADDRESS_BOOK_H__
#define __ADDRESS_BOOK_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern gint address_book_cb(GtkWidget * widget, gpointer data);
extern GList* ab_load_addresses (gboolean);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __ADDRESS_BOOK_H__ */

