/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __options_h__
#define __options_h__

#include <gnome.h>
#include "balsa-app.h"

#define PERS_POP3 0
#define PERS_IMAP 1
#define PERS_LOCAL 2

void personality_box (GtkWidget *, gpointer);

typedef struct _Balsa_Options Balsa_Options;
struct _Balsa_Options
  {
    GList *pers;
    GList *mailboxes;
  };

typedef struct _Personality Personality;
struct _Personality
  {
    int persnum;		/* starts with 0 */
    gchar *name;
    gchar *realname;
    gchar *replyto;
    int type;			/* 0 = POP3, 1 = IMAP, 2 = Local */

    gchar *p_pop3server;
    gchar *p_smtpserver;
    gchar *p_username;
    gchar *p_password;
    Mailbox *p_default_mailbox;
    int p_checkmail;

    gchar *i_imapserver;
    gchar *i_smtpserver;
    gchar *i_username;
    gchar *i_password;
    Mailbox *i_default_mailbox;
    int i_checkmail;

    gchar *l_mblocation;
    gchar *l_smtpserver;
    Mailbox *l_default_mailbox;
    int l_checkmail;
  };


typedef struct _personality_box_options personality_box_options;
struct _personality_box_options
  {
    Personality *pers;
    GtkWidget *accountname;

    GtkWidget *realname;
    GtkWidget *replyto;

    GtkWidget *pop3_pop3server;
    GtkWidget *pop3_smtpserver;
    GtkWidget *pop3_username;
    GtkWidget *pop3_password;
    GtkWidget *pop3_check_mail;
    GtkWidget *pop3_default_mailbox;

    GtkWidget *imap_imapserver;
    GtkWidget *imap_smtpserver;
    GtkWidget *imap_username;
    GtkWidget *imap_password;
    GtkWidget *imap_check_mail;
    GtkWidget *imap_default_mailbox;

    GtkWidget *local_mblocation;
    GtkWidget *local_smtpserver;
    GtkWidget *local_default_mailbox;
    GtkWidget *local_check_mail;
  };

#endif
/* __options_h__ */
