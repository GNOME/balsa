/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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
	G_GNUC_PRINTF(2, 3);

void balsa_information_parented(GtkWindow *widget,
                                LibBalsaInformationType type, 
                                const char *fmt, ...)
	G_GNUC_PRINTF(3, 4);

void balsa_information_real(GtkWindow *parent, LibBalsaInformationType type,
                            const char *msg);

#endif
