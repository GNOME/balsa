/*
 * macosx-helpers.h
 *  
 * Helper functions for managing the IGE Mac Integration stuff
 *
 * Copyright (C) 2004 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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
 
#ifndef __MACOSX_HELPERS__
#define __MACOSX_HELPERS__
 
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#if HAVE_MACOSX_DESKTOP

#include <gtk/gtk.h>

void libbalsa_macosx_menu(GtkWidget * window, GtkMenuShell *menubar);
void libbalsa_macosx_menu_for_parent(GtkWidget *window, GtkWindow *parent);

#endif  /* HAVE_MACOSX_DESKTOP */

#endif  /* __MACOSX_HELPERS__ */
