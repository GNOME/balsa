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
 * filter-private.h
 *
 * private filter defninitions
 */

#ifndef __FILTER_PRIVATE_H__
#define __FILTER_PRIVATE_H__

#if !GLIB_CHECK_VERSION(2, 14, 0)
#  ifdef HAVE_PCRE
#    include <pcreposix.h>
#  else
#    include <sys/types.h>
#    include <regex.h>
#  endif
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */


/* regex options */
#define FILTER_REGCOMP       (REG_NEWLINE | REG_NOSUB | REG_EXTENDED)
#define FILTER_REGEXEC       0

/* regex struct */
struct _LibBalsaConditionRegex {
    gchar *string;
#if GLIB_CHECK_VERSION(2, 14, 0)
    GRegex *compiled;
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
    regex_t *compiled;
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
};

#endif				/* __FILTER_PRIVATE_H__ */
