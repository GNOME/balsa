/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others
 * Written by (C) Albrecht Dreﬂ <albrecht.dress@arcor.de> 2007
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

#ifndef __BALSA_CITE_BAR_H__
#define __BALSA_CITE_BAR_H__

#include <gtk/gtk.h>


G_BEGIN_DECLS


#define BALSA_TYPE_CITE_BAR balsa_cite_bar_get_type()

G_DECLARE_FINAL_TYPE(BalsaCiteBar,
                     balsa_cite_bar,
                     BALSA,
                     CITE_BAR,
                     GtkWidget)

GtkWidget* balsa_cite_bar_new    (gint height, gint bars, gint dimension);
void       balsa_cite_bar_resize (BalsaCiteBar *cite_bar, gint height);


G_END_DECLS


#endif /* __BALSA_CITE_BAR_H__ */

