/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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

#ifndef __index_h__
#define __index_h__

#include "c-client.h"
#include "balsa-app.h"
#include "mailbox.h"

void index_select (GtkWidget * widget, 
		   MAILSTREAM * stream,
		   glong mesgno);
void index_next_message ();
void index_previous_message ();

void index_delete_message();
void index_undelete_message();

void mailbox_menu_update ();

#endif /* __index__h__ */
