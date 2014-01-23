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

#ifndef __MESSAGE_WINDOW_H__
#define __MESSAGE_WINDOW_H__

#include "libbalsa.h"
#include "toolbar-factory.h"
#include <gtk/gtk.h>

typedef struct _MessageWindow MessageWindow;

void message_window_new(LibBalsaMailbox * mailbox, guint msgno);
BalsaToolbarModel *message_window_get_toolbar_model(void);
GtkUIManager *message_window_ui_manager_new(MessageWindow * mw);

#endif				/* __MESSAGE_WINDOW_H__ */
