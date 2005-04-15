/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __LIBBALSA_H__
#define __LIBBALSA_H__

typedef struct _LibBalsaMailbox LibBalsaMailbox;
typedef struct _LibBalsaMailboxRemote LibBalsaMailboxRemote;
typedef struct _LibBalsaMailboxSearchIter LibBalsaMailboxSearchIter;
typedef struct _LibBalsaMessage LibBalsaMessage;
typedef struct _LibBalsaMessageHeaders LibBalsaMessageHeaders;
typedef struct _LibBalsaMessageBody LibBalsaMessageBody;
typedef struct _LibBalsaServer LibBalsaServer;
typedef struct _LibBalsaSmtpServer LibBalsaSmtpServer;
typedef struct _LibBalsaCondition LibBalsaCondition;


#include "message.h"
#include "body.h"
#include "files.h"
#include "mime.h"

#include "information.h"

#include "server.h"

#include "address-book.h"
#include "address-book-vcard.h"
#include "address-book-ldif.h"
#include "address-book-extern.h"

#if ENABLE_LDAP
#include "address-book-ldap.h"
#endif

#include "mailbox.h"
#include "mailbox_local.h"
#include "mailbox_remote.h"
#include "mailbox_pop3.h"
#include "mailbox_imap.h"
#include "mailbox_mbox.h"
#include "mailbox_mh.h"
#include "mailbox_maildir.h"

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

/*
 * Error domains for GError: only one for now, more to come.
 */
enum {
    LIBBALSA_SCANNER_ERROR,
    LIBBALSA_MAILBOX_ERROR
};

/*
 * Error codes for GError: only one for now, more to come.
 */
enum {
    LIBBALSA_SCANNER_ERROR_IMAP,
    LIBBALSA_MAILBOX_APPEND_ERROR,
    LIBBALSA_MAILBOX_AUTH_ERROR,
    LIBBALSA_MAILBOX_COPY_ERROR,
    LIBBALSA_MAILBOX_RENAME_ERROR,
    LIBBALSA_MAILBOX_CREATE_ERROR,
    LIBBALSA_MAILBOX_DELETE_ERROR,
    LIBBALSA_MAILBOX_NETWORK_ERROR,
    LIBBALSA_MAILBOX_OPEN_ERROR,
    LIBBALSA_MAILBOX_TOOMANYOPEN_ERROR
};


/*
 * Initialize the library
 */
void libbalsa_init(LibBalsaInformationFunc information_callback);
void libbalsa_set_spool(const gchar * spool);

void libbalsa_show_message_source(LibBalsaMessage * msg,
                                  const gchar * font,
                                  gboolean *escape_specials);
gchar *libbalsa_rot(const gchar * pass);

gchar *libbalsa_guess_email_address(void);
gchar *libbalsa_guess_mail_spool(void);
gboolean libbalsa_is_sending_mail(void);
void libbalsa_wait_for_sending_thread(gint max_seconds);

gchar *libbalsa_guess_pop_server(void);
gchar *libbalsa_guess_imap_server(void);
gchar *libbalsa_guess_ldap_server(void);

gchar *libbalsa_guess_imap_inbox(void);

gchar *libbalsa_guess_ldap_base(void);
gchar *libbalsa_guess_ldap_name(void);

gchar *libbalsa_guess_ldif_file(void);

gboolean libbalsa_ldap_exists(const gchar *server);

void libbalsa_assure_balsa_dir(void);
gchar* libbalsa_date_to_gchar(const time_t *date, const gchar *date_string);
LibBalsaMessageStatus libbalsa_get_icon_from_flags(LibBalsaMessageFlag flags);

#ifdef USE_TLS
#include <openssl/ssl.h>
gboolean libbalsa_is_cert_known(X509* cert, long vfy_result);
void libbalsa_certs_destroy(void);
#endif

#ifdef BALSA_USE_THREADS
#include <pthread.h>
pthread_t libbalsa_get_main_thread(void);
gboolean libbalsa_am_i_subthread(void);
void libbalsa_threads_init(void);
void libbalsa_threads_destroy(void);
#else
#define libbalsa_am_i_subthread() FALSE
#endif /* BALSA_USE_THREADS */
void libbalsa_message(const char *fmt, ...);
gchar * libbalsa_rot(const gchar * pass);


#endif                          /* __LIBBALSA_H__ */
