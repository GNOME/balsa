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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __BALSA_SENDMSG_H__
#define __BALSA_SENDMSG_H__

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */

    typedef enum {
	SEND_NORMAL,		/* initialized by Compose */
	SEND_REPLY,		/* by Reply               */
	SEND_REPLY_ALL,		/* by Reply All           */
	SEND_REPLY_GROUP,       /* by Reply to Group      */
	SEND_FORWARD_ATTACH,    /* by Forward attached    */
	SEND_FORWARD_INLINE,    /* by Forward inline      */
	SEND_CONTINUE		/* by Continue postponed  */
    } SendType;


#define VIEW_MENU_LENGTH 10
    typedef struct _BalsaSendmsg BalsaSendmsg;
    typedef struct _BalsaSendmsgAddress BalsaSendmsgAddress;

    struct _BalsaSendmsgAddress {
        BalsaSendmsg *bsmsg;
        GtkWidget *label;
        gint min_addresses, max_addresses;
        gboolean ready;
    };

    struct _BalsaSendmsg {
	GtkWidget *window;
	GtkWidget *to[3], *from[3], *subject[2], *cc[3], *bcc[3], *fcc[3],
	    *reply_to[3];
        BalsaSendmsgAddress to_info, from_info, cc_info, bcc_info,
            reply_to_info;
	GtkWidget *comments[2], *keywords[2];
	GtkWidget *attachments[4];
	GtkWidget *text;
	GtkWidget *spell_checker;
	GtkWidget *notebook;
	LibBalsaMessage *orig_message;
	SendType type;
	/* language selection related data */
	gchar *charset;
	const gchar *locale;
	GtkWidget *current_language_menu;
	/* identity related data */
	LibBalsaIdentity* ident;
        /* fcc mailbox */
        gchar *fcc_url;
	/* widgets to be disabled when the address is incorrect */
	GtkWidget *ready_widgets[3];
	GtkWidget *view_checkitems[VIEW_MENU_LENGTH];
	gboolean update_config; /* is the window being set up or in normal  */
	                        /* operation and user actions should update */
	                        /* the config */
	gulong delete_sig_id;
        gulong changed_sig_id;
        guint wrap_timeout_id;
	gboolean modified;
	gboolean flow;          /* send format=flowed */ 
	gboolean req_dispnotify; /* send a MDN */ 
	gboolean quit_on_close; /* quit balsa after the compose window */
	                        /* is closed.                          */
        /* style for changing the color of address labels when the
         * address isn't valid: */
        GtkStyle *bad_address_style;  
#ifdef HAVE_GPGME
	guint gpg_mode;
	GtkWidget *gpg_sign_menu_item;
	GtkWidget *gpg_encrypt_menu_item;
#endif
        GtkWidget *header_table;
    };

    BalsaSendmsg *sendmsg_window_new(GtkWidget *, LibBalsaMessage *,
                                     SendType);
    void sendmsg_window_set_field(BalsaSendmsg *bsmsg, const gchar* key,
                                  const gchar* val);

    gboolean add_attachment(GnomeIconList * iconlist, char *filename, 
                            gboolean is_a_tmp_file, 
                            const gchar *forced_mime_type);

    typedef void (*field_setter)(BalsaSendmsg *d, const gchar*, const gchar*);

    void sendmsg_window_process_url(const char *url, field_setter func,
				    void *data);
    BalsaSendmsg *sendmsg_window_new_from_list(GtkWidget * w,
                                               GList * message_list,
                                               SendType type);
    BalsaToolbarModel *sendmsg_window_get_toolbar_model(void);

#define SENDMSG_WINDOW_QUIT_ON_CLOSE(bsmsg) ((bsmsg)->quit_on_close=TRUE)

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_SENDMSG_H__ */
