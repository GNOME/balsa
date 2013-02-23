/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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

#ifndef __LIBBALSA_INFORMATION_H__
#define __LIBBALSA_INFORMATION_H__

#include <gtk/gtk.h>
#include <stdarg.h>

enum _LibBalsaInformationType {
    LIBBALSA_INFORMATION_MESSAGE,
    LIBBALSA_INFORMATION_WARNING,
    LIBBALSA_INFORMATION_ERROR,
    LIBBALSA_INFORMATION_DEBUG,
    LIBBALSA_INFORMATION_FATAL
};

typedef enum _LibBalsaInformationType LibBalsaInformationType;

typedef void (*LibBalsaInformationFunc) (GtkWindow *parent, 
                                         LibBalsaInformationType message_type,
                                         const gchar * fmt);


extern LibBalsaInformationFunc libbalsa_real_information_func;

void libbalsa_information(LibBalsaInformationType type,
                          const char *fmt, ...)
#ifdef __GNUC__
    __attribute__ ((format (printf, 2, 3)))
#endif
;
void libbalsa_information_parented(GtkWindow *parent,
                                   LibBalsaInformationType type,
                                   const char *fmt, ...)
#ifdef __GNUC__
    __attribute__ ((format (printf, 3, 4)))
#endif
;
void libbalsa_information_varg(GtkWindow *parent,
                               LibBalsaInformationType type,
                               const char *fmt, va_list ap);

#endif
