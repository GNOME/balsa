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

#ifndef __LIBBALSA_MISC_H__
#define __LIBBALSA_MISC_H__

#include <stdio.h>

#include "libbalsa.h"
#if ENABLE_ESMTP
#include <auth-client.h>
#endif

gchar *libbalsa_make_string_from_list(const GList *);

size_t libbalsa_readfile(FILE * fp, char **buf);
size_t libbalsa_readfile_nostat(FILE * fp, char **buf);

gchar *libbalsa_get_hostname(void);
gchar *libbalsa_get_domainname(void);
gchar *libbalsa_escape_specials(const gchar* str);
gchar *libbalsa_deescape_specials(const gchar* str);

gboolean libbalsa_find_word(const gchar * word, const gchar * str);
void libbalsa_wrap_string(gchar * str, int width);

const char* libbalsa_set_charset(const gchar * charset);
#if ENABLE_ESMTP
gboolean libbalsa_process_queue(LibBalsaMailbox* outbox, gint encoding,
				gchar* smtp_server,
				auth_context_t smtp_authctx);
#else
gboolean libbalsa_process_queue(LibBalsaMailbox* outbox, gint encoding);
#endif
void libbalsa_marshal_POINTER__NONE(GtkObject *object, GtkSignalFunc func,
				gpointer func_data, GtkArg *args);
void libbalsa_marshal_POINTER__OBJECT(GtkObject * object,
				      GtkSignalFunc func,
				      gpointer func_data, GtkArg * args);
void libbalsa_marshall_POINTER__POINTER_POINTER(GtkObject *object, 
						GtkSignalFunc func,
						gpointer func_data, GtkArg *args);

gboolean libbalsa_delete_directory_contents(const gchar *path);


#endif				/* __LIBBALSA_MISC_H__ */
