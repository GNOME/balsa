/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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
 * Support functions for completion
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_COMPLETION_H__
#define __LIBBALSA_ADDRESS_BOOK_COMPLETION_H__

#include "address.h"

typedef struct _CompletionData CompletionData;
struct _CompletionData {
	gchar *string;
	InternetAddress *ia;
};

CompletionData *completion_data_new(InternetAddress * ia,
                                    const gchar * nick_name);
void completion_data_free(CompletionData * data, gpointer unused);
gchar *completion_data_extract(CompletionData * data);
gint strncmp_word(const gchar * s1, const gchar * s2, gsize n);

#endif
