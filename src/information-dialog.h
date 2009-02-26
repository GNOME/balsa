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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __INFORMATION_DIALOG_H__
#define __INFORMATION_DIALOG_H__

#include "libbalsa.h"

typedef enum _BalsaInformationShow BalsaInformationShow;
enum _BalsaInformationShow {
    BALSA_INFORMATION_SHOW_NONE = 0,
    BALSA_INFORMATION_SHOW_DIALOG,
    BALSA_INFORMATION_SHOW_LIST,
    BALSA_INFORMATION_SHOW_BAR,
    BALSA_INFORMATION_SHOW_STDERR,
};

void balsa_information(LibBalsaInformationType type, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__ ((format (printf, 2, 3)))
#endif
;

void balsa_information_parented(GtkWindow *widget,
                                LibBalsaInformationType type, 
                                const char *fmt, ...)
#ifdef __GNUC__
    __attribute__ ((format (printf, 3, 4)));
#endif
;

void balsa_information_real(GtkWindow *parent, LibBalsaInformationType type,
                            const char *msg);

#endif
