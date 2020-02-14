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
 * filter-private.h
 *
 * private filter defninitions
 */

#ifndef __FILTER_PRIVATE_H__
#define __FILTER_PRIVATE_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif


/* regex options */
#define FILTER_REGCOMP       (REG_NEWLINE | REG_NOSUB | REG_EXTENDED)
#define FILTER_REGEXEC       0

/* regex struct */
struct _LibBalsaConditionRegex {
    gchar *string;
    GRegex *compiled;
};

#endif				/* __FILTER_PRIVATE_H__ */
