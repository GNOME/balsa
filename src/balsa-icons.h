/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#ifndef __BALSA_ICONS_H__
#define __BALSA_ICONS_H__

#include "config.h"

#define BALSA_PIXMAP_ATTACHMENT			"balsa_attachment"
#define BALSA_PIXMAP_COMPOSE			"balsa_compose"
#define BALSA_PIXMAP_CONTINUE			"balsa_continue"
#define BALSA_PIXMAP_RECEIVE			"balsa_receive"
#define BALSA_PIXMAP_REPLY			"balsa_reply"
#define BALSA_PIXMAP_REPLY_ALL			"balsa_reply_all"
#define BALSA_PIXMAP_REPLY_GROUP		"balsa_reply_group"
#define BALSA_PIXMAP_FORWARD			"balsa_forward"
#define BALSA_PIXMAP_NEXT			"balsa_next"
#define BALSA_PIXMAP_NEXT_PART			"balsa_next_part"
#define BALSA_PIXMAP_PREVIOUS			"balsa_previous"
#define BALSA_PIXMAP_PREVIOUS_PART		"balsa_previous_part"
#define BALSA_PIXMAP_POSTPONE			"balsa_postpone"
#define BALSA_PIXMAP_SEND			"balsa_send"
#define BALSA_PIXMAP_SEND_RECEIVE		"balsa_send_receive"
#define BALSA_PIXMAP_TRASH_EMPTY		"balsa_trash_empty"
#define BALSA_PIXMAP_NEXT_UNREAD		"balsa_next_unread"
#define BALSA_PIXMAP_NEXT_FLAGGED		"balsa_next_flagged"
#define BALSA_PIXMAP_SHOW_HEADERS		"balsa_show_headers"
#define BALSA_PIXMAP_SHOW_PREVIEW		"balsa_show_preview"
#define BALSA_PIXMAP_MARKED_NEW			"balsa_marked_new"
#define BALSA_PIXMAP_MARK_ALL			"balsa_marked_all"
#define BALSA_PIXMAP_IDENTITY			"balsa_identity"

#define BALSA_OLD_PIXMAP_PRINT			"balsa_print"
#define BALSA_OLD_PIXMAP_SAVE			"balsa_save"
#define BALSA_OLD_PIXMAP_TRASH			"balsa_trash"
#define BALSA_OLD_PIXMAP_CLOSE_MBOX		"balsa_close_mbox"

#define BALSA_PIXMAP_MBOX_DRAFT                 "balsa_mbox_draft"
#define BALSA_PIXMAP_MBOX_IN                    "balsa_mbox_in"
#define BALSA_PIXMAP_MBOX_OUT                   "balsa_mbox_out"
#define BALSA_PIXMAP_MBOX_SENT                  "balsa_mbox_sent"
#define BALSA_PIXMAP_MBOX_TRAY_EMPTY            "balsa_mbox_tray_empty"
#define BALSA_PIXMAP_MBOX_TRAY_FULL             "balsa_mbox_tray_full"
#define BALSA_PIXMAP_MBOX_DIR_OPEN              "balsa_mbox_dir_open"
#define BALSA_PIXMAP_MBOX_DIR_CLOSED            "balsa_mbox_dir_closed"

#define BALSA_PIXMAP_INFO_FLAGGED               "balsa_info_flagged"
#define BALSA_PIXMAP_INFO_REPLIED               "balsa_info_replied"
#define BALSA_PIXMAP_INFO_NEW                   "balsa_info_new"
#define BALSA_PIXMAP_INFO_ATTACHMENT            BALSA_PIXMAP_ATTACHMENT
#define BALSA_PIXMAP_INFO_DELETED               GTK_STOCK_DELETE
#ifdef HAVE_GPGME
#  define BALSA_PIXMAP_GPG_SIGN                 "balsa_gpg_sign"
#  define BALSA_PIXMAP_GPG_ENCRYPT              "balsa_gpg_encrypt"
#  define BALSA_PIXMAP_GPG_RECHECK              "balsa_gpg_recheck"
#  define BALSA_PIXMAP_SIGN                     "balsa_sign"
#  define BALSA_PIXMAP_SIGN_GOOD                "balsa_sign_good"
#  define BALSA_PIXMAP_SIGN_NOTRUST             "balsa_sign_trust"
#  define BALSA_PIXMAP_SIGN_BAD                 "balsa_sign_bad"
#  define BALSA_PIXMAP_ENCR                     "balsa_encr"
#endif

#define BALSA_PIXMAP_BOOK_RED                   "balsa_book_red"
#define BALSA_PIXMAP_BOOK_YELLOW                "balsa_book_yellow"
#define BALSA_PIXMAP_BOOK_GREEN                 "balsa_book_green"
#define BALSA_PIXMAP_BOOK_BLUE                  "balsa_book_blue"
#define BALSA_PIXMAP_BOOK_OPEN                  "balsa_book_open"

void register_balsa_pixmaps(void);
void register_balsa_pixbufs(GtkWidget * widget);
void balsa_icon_create(const gchar ** data, GdkPixmap ** pmap,
                       GdkBitmap ** bmap);
#if GTK_CHECK_VERSION(2, 8, 0)
const gchar * balsa_icon_id(const gchar * name);
#endif                          /* GTK_CHECK_VERSION(2, 8, 0) */
#endif
