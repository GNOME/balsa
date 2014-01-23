/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __BALSA_DRUID_PAGE_FINISH_H__
#define __BALSA_DRUID_PAGE_FINISH_H__

#ifdef __cplusplus
extern "C" {
#endif                          /* __cplusplus */

#include <gtk/gtk.h>

#include "assistant_helper.h"
#include "assistant_init.h"

/*
 * Public methods
 */
    void balsa_druid_page_finish(GtkAssistant * druid,
                                 GdkPixbuf * default_logo);

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif
