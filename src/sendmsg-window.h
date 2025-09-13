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

#ifndef __BALSA_SENDMSG_H__
#define __BALSA_SENDMSG_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "libbalsa.h"
#include "address-view.h"
#include "toolbar-factory.h"

G_BEGIN_DECLS

    typedef enum {
       SEND_NORMAL,            /* initialized by Compose */
       SEND_REPLY,             /* by Reply               */
       SEND_REPLY_ALL,         /* by Reply All           */
       SEND_REPLY_GROUP,       /* by Reply to Group      */
       SEND_NEW_TO_SENDER,     /* by New to Sender       */
       SEND_FORWARD_ATTACH,    /* by Forward attached    */
       SEND_FORWARD_INLINE,    /* by Forward inline      */
       SEND_CONTINUE           /* by Continue postponed  */
    } SendType;

    typedef enum {
        SENDMSG_STATE_CLEAN,
        SENDMSG_STATE_MODIFIED,
        SENDMSG_STATE_AUTO_SAVED
    } SendmsgState;

#define VIEW_MENU_LENGTH 5

    typedef struct _BalsaSendmsgHeader BalsaSendmsgHeader;
    struct _BalsaSendmsgHeader {
	GtkWidget *name;
	GtkWidget *body;
    };

    typedef struct _BalsaSendmsg BalsaSendmsg;

    struct _BalsaSendmsg {
	GtkWidget *window;
	GtkWidget *toolbar;
        LibBalsaAddressView *recipient_view, *replyto_view;
	BalsaSendmsgHeader from;
	BalsaSendmsgHeader recipients;
	BalsaSendmsgHeader subject;
	BalsaSendmsgHeader replyto;
	BalsaSendmsgHeader fcc;
	GtkWidget *tree_view;
        gchar *in_reply_to;
        GList *references;
	GtkWidget *text;
#if !HAVE_GTKSPELL
	GtkWidget *spell_checker;
#endif                          /* HAVE_GTKSPELL */
	GtkWidget *notebook;
	LibBalsaMessage *parent_message; /* to which we're replying     */
	LibBalsaMessage *draft_message;  /* where the message was saved */
	SendType type;
        gboolean is_continue;
	/* language selection related data */
	gchar *spell_check_lang;
	GtkWidget *current_language_menu;
	/* identity related data */
	LibBalsaIdentity* ident;
        /* fcc mailbox */
        gchar *fcc_url;
	gboolean update_config; /* is the window being set up or in normal  */
	                        /* operation and user actions should update */
	                        /* the config */
	gulong delete_sig_id;
        gulong changed_sig_id;
#if !HAVE_GTKSOURCEVIEW
        gulong delete_range_sig_id;
#endif                          /* HAVE_GTKSOURCEVIEW */
        gulong insert_text_sig_id;
        guint autosave_timeout_id;
        SendmsgState state;
        gulong identities_changed_id;
	gboolean flow;          /* send format=flowed */ 
	gboolean send_mp_alt;   /* send multipart/alternative (plain and html) */ 
	gboolean req_mdn; 	 /* send a MDN */
	gboolean req_dsn;	 /* send a delivery status notification */
	guint crypt_mode;
	gboolean always_trust;
	gboolean attach_pubkey;

#if !HAVE_GTKSOURCEVIEW
        GtkTextBuffer *buffer2;       /* Undo buffer. */
#endif                          /* HAVE_GTKSOURCEVIEW */

        /* To update cursor after text is inserted. */
        GtkTextMark *insert_mark;

        GtkWidget *paned;
        gboolean ready_to_send;
    };

    BalsaSendmsg *sendmsg_window_compose(void);
    BalsaSendmsg *sendmsg_window_compose_with_address(const gchar *
                                                      address);
    BalsaSendmsg* sendmsg_window_new_to_sender(LibBalsaMailbox * mailbox,
                                               guint msgno);
    BalsaSendmsg *sendmsg_window_reply(LibBalsaMailbox *,
                                       guint msgno, SendType rt);
    BalsaSendmsg *sendmsg_window_reply_embedded(LibBalsaMessageBody *part,
                                                SendType reply_type);

    BalsaSendmsg *sendmsg_window_forward(LibBalsaMailbox *,
                                         guint msgno, gboolean attach);
    BalsaSendmsg *sendmsg_window_continue(LibBalsaMailbox *,
                                          guint msgno);

    void sendmsg_window_set_field(BalsaSendmsg *bsmsg, const gchar* key,
                                  const gchar* val, gboolean from_command_line);

    gboolean add_attachment(BalsaSendmsg * bsmsg,
                            const gchar *filename, 
                            gboolean is_a_tmp_file, 
                            const gchar *forced_mime_type);

    void sendmsg_window_process_url(BalsaSendmsg *bsmsg,
                                    const char *url,
                                    gboolean from_command_line);
    BalsaSendmsg *sendmsg_window_new_from_list(LibBalsaMailbox * mailbox,
                                               GArray * selected,
                                               SendType type);
    BalsaToolbarModel *sendmsg_window_get_toolbar_model(void);
    void sendmsg_window_add_action_entries(GActionMap * action_map);

G_END_DECLS

#endif				/* __BALSA_SENDMSG_H__ */
