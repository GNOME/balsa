/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#ifndef _BALSA_SM_BALSA_H
#define _BALSA_SM_BALSA_H

/* ************************************************************************ */

#include "sm.h"

typedef struct sm_state_s {
	/* SM: whether we should always save our session on exit... */
	gboolean asked_about_autosave;
	gboolean do_autosave;

	gboolean open_unread_mailbox;
	gboolean checkmail;
	gboolean compose_mode;
	gchar *compose_to;
	gboolean remember_mbs_to_open;
	GSList *mbs_to_open;
} sm_state_t;

extern sm_state_t balsa_state;

gboolean sm_init( int argc, char **argv );

/* ************************************************************************ */

#endif
