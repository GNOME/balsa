/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */
/*
 * filter-error.c
 *
 * Functions for dealing with errors in the filter system
 */

#include "config.h"

#include "filter.h"


/*
 * The filter_errlist is an array of strings indexed by filter_errno
 * for use in filter_perror() and filter_strerror()
 */
gchar *filter_errlist[] = {
    "No error",
    "No filter configuration file found",
    "Unable to read filter configuration file",
    "Syntax error in the filter configuration file",
    "No message to filter",
    "Unable to allocate memory",
    "Error in regular expression syntax"
};


/*
 * filter_strerror()
 *
 * Returns a pointer to the appropriate string in filter_errlist
 * based on the errnum passed.  It corrects the sign of the integer
 * if need be.
 * 
 * Arguments:
 *    gint filter_errno - the error number for which a string is requested
 *
 * Returns:
 *    gchar * - pointer to the string in filter_errorlist
 */
gchar *
filter_strerror(gint error)
{
    return (filter_errlist[(error > 0) ? error : -error]);
}				/* end filter_strerror() */


/*
 * filter_perror()
 *
 * Prints an error message on stderr, using the description
 * string for the current filter_errno.
 *
 * Arguments
 *    gchar *s - string to be prepended to the error message.
 */
void
filter_perror(const gchar * s)
{
    gchar *error_string;

    error_string = filter_strerror(filter_errno);
    g_warning("%s: %s\n", s, error_string);
}				/* end filter_perror */
