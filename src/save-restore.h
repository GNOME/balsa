/* Balsa E-Mail Client
 * Copyright (C) 1998 Jay Painter and Stuart Parmenter
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
#ifndef __SAVE_RESTORE_H__
#define __SAVE_RESTORE_H__

#include "libbalsa.h"

/* FIXME XXX This should be a "configure" option.  Stuart? */
#define BALSA_CONFIG_FILE ".balsarc"


/* arp --- All proplist keys are assumed to be no large than this.
 * This is to try and minimize the number of magic numbers used when
 * key buffers are statically allocated on the stack
 */
#ifndef MAX_PROPLIST_KEY_LEN
/* NB!!! This includes space for the terminating \0! */
#define MAX_PROPLIST_KEY_LEN    32
#endif

typedef enum {
    SPECIAL_INBOX = 0,
    SPECIAL_SENT,
    SPECIAL_TRASH,
    SPECIAL_DRAFT
}  specialType;

void config_mailbox_set_as_special(Mailbox * mailbox, specialType which);

gint config_load (gchar * user_filename);
gint config_save (gchar * user_filename);

gchar* mailbox_get_pkey(const Mailbox * mbox);
gint config_mailbox_add (Mailbox * mailbox, char *key_arg);
gint config_mailbox_delete (const Mailbox *mailbox);
gint config_mailbox_update (Mailbox * mailbox, const gchar * old_mbox_pkey);
gint config_mailboxes_init (void);

gint config_global_load (void);
gint config_global_save (void);

#endif /* __SAVE_RESTORE_H__ */
