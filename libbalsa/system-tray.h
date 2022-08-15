/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2022 Stuart Parmenter and others,
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

#ifndef _SYSTEM_TRAY_H_
#define _SYSTEM_TRAY_H_

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif


#ifdef ENABLE_SYSTRAY


#include <glib.h>


G_BEGIN_DECLS


typedef void (*libbalsa_systray_cb_t)(gpointer);


void libbalsa_systray_icon_init(GtkMenu               *menu,
								libbalsa_systray_cb_t  activate_cb,
								gpointer               activate_cb_data);
void libbalsa_systray_icon_enable(gboolean enable);
void libbalsa_systray_icon_attention(gboolean attention);
void libbalsa_systray_icon_destroy(void);


G_END_DECLS


#endif  /* ENABLE_SYSTRAY */


#endif /* _SYSTEM_TRAY_H_ */
