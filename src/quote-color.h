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

#ifndef __BALSA_QUOTECOLOR_H__
#define __BALSA_QUOTECOLOR_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include <gtk/gtk.h>

G_BEGIN_DECLS

    extern guint is_a_quote(const gchar *, GRegex * rex);

G_END_DECLS

#endif				/* __BALSA_QUOTECOLOR_H__ */
