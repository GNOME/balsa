/* Balsa E-Mail Client
 *
 * This file handles Balsa's configuration information.
 *
 * This file is Copyright (C) 1998-1999 Nat Friedman
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

#ifndef _BALSA_CONFIG_BALSA_H
#define _BALSA_CONFIG_BALSA_H

/* ************************************************************************ */

#include "cfg-engine.h"
#include "libbalsa.h"

/* ************************************************************************ */

gboolean cfg_mailbox_write( Mailbox *mb, const cfg_location_t *top );
Mailbox *cfg_mailbox_read( const gchar *name, const cfg_location_t *top );
gboolean cfg_mailbox_delete( Mailbox *mb, cfg_location_t *top );

gboolean cfg_mailbox_write_simple( Mailbox *mb );
Mailbox *cfg_mailbox_read_simple( const gchar *name );
gboolean cfg_mailbox_delete_simple( Mailbox *mb );

gboolean cfg_save( void );
gboolean cfg_load( void );

/* ************************************************************************ */

/* Make sure we're clear of the old code. */

#define config_load Choke on this
#define config_save Choke on this

#define config_mailbox_add Choke on this
#define config_mailbox_delete Choke on this
#define config_mailbox_update Choke on this
#define config_mailboxes_init Choke on this

#define config_global_load Choke on this
#define config_global_save Choke on this

/* ************************************************************************ */

#endif
