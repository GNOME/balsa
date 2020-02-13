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

/*
 * filter-edit.h
 *
 * Variables and definitions for the filter edit dialog
 */

#ifndef __FILTER_EXPORT_H__
#define __FILTER_EXPORT_H__

#include "filter.h"
#include "filter-funcs.h"

/*
 * fex = filter export
 */

void fex_destroy_window_cb(GtkWidget * widget,gpointer throwaway);

/* button callbacks */
void fex_dialog_response(GtkWidget * widget, gint response, gpointer data);

#endif /*__FILTER_EXPORT_H__ */
