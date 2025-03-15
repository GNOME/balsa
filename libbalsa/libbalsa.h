/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

#ifndef __LIBBALSA_H__
#define __LIBBALSA_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

typedef struct _LibBalsaCondition LibBalsaCondition;
typedef struct _LibBalsaIdentity LibBalsaIdentity;
typedef struct _LibBalsaMailbox LibBalsaMailbox;
typedef struct _LibBalsaMailboxRemote LibBalsaMailboxRemote;
typedef struct _LibBalsaMailboxSearchIter LibBalsaMailboxSearchIter;
typedef struct _LibBalsaMessage LibBalsaMessage;
typedef struct _LibBalsaMessageHeaders LibBalsaMessageHeaders;
typedef struct _LibBalsaMessageBody LibBalsaMessageBody;
typedef struct _LibBalsaServer LibBalsaServer;
typedef struct _LibBalsaSmtpServer LibBalsaSmtpServer;
typedef struct _LibbalsaVfs LibbalsaVfs;


#include "message.h"
#include "body.h"
#include "files.h"
#include "mime.h"

#include "information.h"

#include "address-book.h"
#include "address-book-vcard.h"
#include "address-book-ldif.h"
#include "address-book-extern.h"

#if ENABLE_LDAP
#include "address-book-ldap.h"
#endif
#if HAVE_GPE
#include "address-book-gpe.h"
#endif
#if HAVE_OSMO
#include "address-book-osmo.h"
#endif
#if HAVE_WEBDAV
#include "libbalsa-webdav.h"
#include "libbalsa-carddav.h"
#include "address-book-carddav.h"
#endif

/* Callback for testing whether a mailbox or server can be reached: */
typedef void LibBalsaCanReachCallback(GObject * object,
                                      gboolean  can_reach,
                                      gpointer  cb_data);

#include "mailbox.h"
#include "mailbox_local.h"
#include "mailbox_remote.h"
#include "mailbox_pop3.h"
#include "mailbox_imap.h"
#include "mailbox_mbox.h"
#include "mailbox_mh.h"
#include "mailbox_maildir.h"

/*
 * Error domains for GError:
 */
GQuark libbalsa_scanner_error_quark(void);
#define LIBBALSA_SCANNER_ERROR libbalsa_scanner_error_quark()
GQuark libbalsa_mailbox_error_quark(void);
#define LIBBALSA_MAILBOX_ERROR libbalsa_mailbox_error_quark()

/*
 * Error codes for GError:
 */
enum {
    LIBBALSA_SCANNER_ERROR_IMAP,
    LIBBALSA_MAILBOX_APPEND_ERROR,
    LIBBALSA_MAILBOX_AUTH_ERROR, /* retryable */
    LIBBALSA_MAILBOX_AUTH_CANCELLED, /* do not try again */
    LIBBALSA_MAILBOX_COPY_ERROR,
    LIBBALSA_MAILBOX_RENAME_ERROR,
    LIBBALSA_MAILBOX_CREATE_ERROR,
    LIBBALSA_MAILBOX_DELETE_ERROR,
    LIBBALSA_MAILBOX_NETWORK_ERROR,
    LIBBALSA_MAILBOX_OPEN_ERROR,
    LIBBALSA_MAILBOX_TOOMANYOPEN_ERROR,
    LIBBALSA_MAILBOX_ACCESS_ERROR,
    LIBBALSA_MAILBOX_DUPLICATES_ERROR,
    LIBBALSA_MAILBOX_TEMPDIR_ERROR
};


/*
 * Initialize the library
 */
void libbalsa_init(void);

void libbalsa_show_message_source(GtkApplication * application,
                                  LibBalsaMessage * msg,
                                  const gchar * font,
                                  gboolean *escape_specials);

gchar *libbalsa_guess_email_address(void);
gchar *libbalsa_guess_mail_spool(void);
gboolean libbalsa_is_sending_mail(void);

gchar *libbalsa_guess_ldap_server(void);


gchar* libbalsa_date_to_utf8(time_t date, const gchar *date_string);
LibBalsaMessageStatus libbalsa_get_icon_from_flags(LibBalsaMessageFlag flags);

gboolean libbalsa_is_cert_known(GTlsCertificate      *cert,
								GTlsCertificateFlags  errors);

gboolean libbalsa_am_i_subthread(void);

typedef enum {
    LIBBALSA_PROGRESS_NO = 0,
    LIBBALSA_PROGRESS_YES
} LibBalsaProgress;
#define LIBBALSA_PROGRESS_INIT LIBBALSA_PROGRESS_NO
/* We will not use the progress bar if the number of increments is less
 * than LIBBALSA_PROGRESS_MIN_COUNT, and we will not update the fraction
 * if the time since the last update is less than
 * LIBBALSA_PROGRESS_MIN_UPDATE_SECS seconds or if the fraction has
 * increased by less than LIBBALSA_PROGRESS_MIN_UPDATE_STEP. */
#define LIBBALSA_PROGRESS_MIN_COUNT        400
#define LIBBALSA_PROGRESS_MIN_UPDATE_USECS 50000
#define LIBBALSA_PROGRESS_MIN_UPDATE_STEP  0.05

extern void (*libbalsa_progress_set_text) (LibBalsaProgress * progress,
                                           const gchar * text,
                                           guint total);
extern void (*libbalsa_progress_set_fraction) (LibBalsaProgress * progress,
                                               gdouble fraction);
extern void (*libbalsa_progress_set_activity) (gboolean set,
                                               const gchar * text);

/*
 * Face and X-Face header support.
 */
gchar *libbalsa_get_header_from_path(const gchar * header,
                                     const gchar * path, gsize * size,
                                     GError ** err);
GtkWidget *libbalsa_get_image_from_face_header(const gchar * content,
                                               GError ** err);
#if HAVE_COMPFACE
GtkWidget *libbalsa_get_image_from_x_face_header(const gchar * content,
                                                 GError ** err);
#endif                          /* HAVE_COMPFACE */

GQuark libbalsa_image_error_quark(void);
#define LIBBALSA_IMAGE_ERROR libbalsa_image_error_quark()
enum LibBalsaImageError {
    LIBBALSA_IMAGE_ERROR_NO_DATA
#if HAVE_COMPFACE
        ,
    LIBBALSA_IMAGE_ERROR_FORMAT,
    LIBBALSA_IMAGE_ERROR_BUFFER,
    LIBBALSA_IMAGE_ERROR_BAD_DATA
#endif                          /* HAVE_COMPFACE */
};

gboolean libbalsa_use_headerbar(void);
GtkDialogFlags libbalsa_dialog_flags(void);

#if HAVE_GTKSOURCEVIEW
GtkWidget *libbalsa_source_view_new(gboolean highlight_phrases);
#endif                          /* HAVE_GTKSOURCEVIEW */

#ifdef HAVE_CANBERRA
gboolean libbalsa_play_sound_event(const gchar  *event_id,
                                   GError      **error);
gboolean libbalsa_play_sound_file(const gchar  *filename,
                                  GError      **error);
#endif /* HAVE_CANBERRA*/

#endif                          /* __LIBBALSA_H__ */
