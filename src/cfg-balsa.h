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
gboolean cfg_mailbox_delete( Mailbox *mb, const cfg_location_t *top );

gboolean cfg_mailbox_write_simple( Mailbox *mb );
Mailbox *cfg_mailbox_read_simple( const gchar *name );
gboolean cfg_mailbox_delete_simple( Mailbox *mb );

gboolean cfg_save( void );
gboolean cfg_load( void );

/* ************************************************************************ */

extern const cfg_metatype_spec_t spec_Printing_t;
extern const cfg_metatype_spec_t spec_GdkColor_t;
extern const cfg_metatype_spec_t spec_Address;
extern const cfg_metatype_spec_t spec_ServerType;
extern const cfg_metatype_spec_t spec_NonObvious;
extern const cfg_metatype_spec_t spec_Server;
extern const cfg_metatype_spec_t spec_Mailbox;
extern const cfg_metatype_spec_t spec_MailboxIMAP;
extern const cfg_metatype_spec_t spec_MailboxPOP3;
extern const cfg_metatype_spec_t spec_MailboxLocal;
extern const cfg_metatype_spec_t spec_MailboxArray;
extern const cfg_metatype_spec_t spec_BalsaApp;
extern const cfg_metatype_spec_t spec_mblist;
extern const cfg_metatype_spec_t spec_sm_state_t;

/* ************************************************************************ */

#endif
