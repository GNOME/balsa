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

#include "config.h"

#include "abook-completion.h"
#include "information.h"
#include "misc.h"

#define CASE_INSENSITIVE_NAME

/*
 * Create a new CompletionData
 */
CompletionData *
completion_data_new(LibBalsaAddress * address, gboolean alias)
{
    CompletionData *ret;

    ret = g_new0(CompletionData, 1);

    g_object_ref(address);
    ret->address = address;

#ifdef CASE_INSENSITIVE_NAME
    ret->string =
        g_ascii_strup(alias ? address->nick_name : address->full_name, -1);
#else
    ret->string = g_strdup(alias ? address->nick_name : address->full_name);
#endif

    return ret;
}

/*
 * Free a CompletionData
 */
void
completion_data_free(CompletionData * data)
{
    g_object_unref(data->address);
    g_free(data->string);
    g_free(data);
}

/*
 * The GCompletionFunc
 */
gchar *
completion_data_extract(CompletionData * data)
{
    return data->string;
}

gint
address_compare(LibBalsaAddress *a, LibBalsaAddress *b)
{
    g_return_val_if_fail(a != NULL, -1);
    g_return_val_if_fail(b != NULL, 1);

    return g_ascii_strcasecmp(a->full_name, b->full_name);
}
