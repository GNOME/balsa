/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __LIBBALSA_MIME_H__
#define __LIBBALSA_MIME_H__

#include <stdio.h>

#include "libbalsa.h"

GString *process_mime_part(LibBalsaMessage * message,
			   LibBalsaMessageBody * body,
			   gchar * reply_prefix_str, gint llen,
			   gboolean ignore_html, gboolean flow);
GString *content2reply(LibBalsaMessage * message,
		       gchar * reply_prefix_str, gint llen,
		       gboolean ignore_html, gboolean flow);	/* arp */

#endif				/* __LIBBALSA_MIME_H__ */
