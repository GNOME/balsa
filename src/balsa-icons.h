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
#define BALSA_PIXMAP_NEW			"balsa_compose"
#define BALSA_PIXMAP_CONTINUE			"balsa_continue"
#define BALSA_PIXMAP_RECEIVE			"balsa_receive"
#define BALSA_PIXMAP_REPLY			"balsa_reply"
#define BALSA_PIXMAP_REPLY_ALL			"balsa_reply_all"
#define BALSA_PIXMAP_REPLY_GROUP		"balsa_reply_group"
#define BALSA_PIXMAP_FORWARD			"balsa_forward"
#define BALSA_PIXMAP_NEXT			"balsa_next"
#define BALSA_PIXMAP_PREVIOUS			"balsa_previous"
#define BALSA_PIXMAP_POSTPONE			"balsa_postpone"
#define BALSA_PIXMAP_PRINT			"balsa_print"
#define BALSA_PIXMAP_SAVE			"balsa_save"
#define BALSA_PIXMAP_SEND			"balsa_send"
#define BALSA_PIXMAP_SEND_RECEIVE		"balsa_send_receive"
#define BALSA_PIXMAP_TRASH			"balsa_trash"
#define BALSA_PIXMAP_TRASH_EMPTY		"balsa_trash_empty"
#define BALSA_PIXMAP_NEXT_UNREAD		"balsa_next_unread"
#define BALSA_PIXMAP_NEXT_FLAGGED		"balsa_next_flagged"
#define BALSA_PIXMAP_SHOW_HEADERS		"balsa_show_headers"
#define BALSA_PIXMAP_SHOW_PREVIEW		"balsa_show_preview"
#define BALSA_PIXMAP_MARKED_NEW			"balsa_marked_new"
#define BALSA_PIXMAP_MARKED_ALL			"balsa_marked_all"
#define BALSA_PIXMAP_IDENTITY			"balsa_identity"
#define BALSA_PIXMAP_CLOSE_MBOX			"balsa_close_mbox"

#define BALSA_PIXMAP_MENU_NEW			"balsa_menu_new"
#define BALSA_PIXMAP_MENU_FLAGGED		"balsa_menu_flagged"
#define BALSA_PIXMAP_MENU_IDENTITY		"balsa_menu_identity"
#define BALSA_PIXMAP_MENU_FORWARD		"balsa_menu_forward"
#define BALSA_PIXMAP_MENU_REPLY	                "balsa_menu_reply"
#define BALSA_PIXMAP_MENU_REPLY_ALL		"balsa_menu_reply_all"
#define BALSA_PIXMAP_MENU_REPLY_GROUP		"balsa_menu_reply_group"
#define BALSA_PIXMAP_MENU_POSTPONE		"balsa_menu_postpone"
#define BALSA_PIXMAP_MENU_PRINT	                "balsa_menu_print"
#define BALSA_PIXMAP_MENU_NEXT			"balsa_menu_next"
#define BALSA_PIXMAP_MENU_PREVIOUS		"balsa_menu_previous"
#define BALSA_PIXMAP_MENU_SAVE			"balsa_menu_save"
#define BALSA_PIXMAP_MENU_SEND			"balsa_menu_send"
#define BALSA_PIXMAP_MENU_SEND_RECEIVE		"balsa_menu_send_receive"
#define BALSA_PIXMAP_MENU_COMPOSE		"balsa_menu_compose"
#define BALSA_PIXMAP_MENU_ATTACHMENT		"balsa_menu_attachment"
#define BALSA_PIXMAP_MENU_RECEIVE		"balsa_menu_receive"
#define BALSA_PIXMAP_MENU_NEXT_FLAGGED		"balsa_menu_next_flagged"
#define BALSA_PIXMAP_MENU_NEXT_UNREAD		"balsa_menu_next_unread"
#define BALSA_PIXMAP_MENU_MARK_ALL		"balsa_menu_mark_all"

#define BALSA_PIXMAP_OTHER_CLOSE		"balsa_other_close"

#define BALSA_PIXMAP_MBOX_DRAFT                 "balsa_mbox_draft"
#define BALSA_PIXMAP_MBOX_IN                    "balsa_mbox_in"
#define BALSA_PIXMAP_MBOX_OUT                   "balsa_mbox_out"
#define BALSA_PIXMAP_MBOX_SENT                  "balsa_mbox_sent"
#define BALSA_PIXMAP_MBOX_TRASH                 "balsa_mbox_trash"
#define BALSA_PIXMAP_MBOX_TRAY_EMPTY            "balsa_mbox_tray_empty"
#define BALSA_PIXMAP_MBOX_TRAY_FULL             "balsa_mbox_tray_full"
#define BALSA_PIXMAP_MBOX_DIR_OPEN              "balsa_mbox_dir_open"
#define BALSA_PIXMAP_MBOX_DIR_CLOSED            "balsa_mbox_dir_closed"

#define BALSA_PIXMAP_INFO_FLAGGED               "balsa_info_flagged"
#define BALSA_PIXMAP_INFO_REPLIED               "balsa_info_replied"
#define BALSA_PIXMAP_INFO_NEW                   "balsa_info_new"
#define BALSA_PIXMAP_INFO_ATTACHMENT            "balsa_info_attachment"
#define BALSA_PIXMAP_INFO_DELETED               "balsa_info_deleted"
#ifdef HAVE_GPGME
#  define BALSA_PIXMAP_GPG_SIGN                 "balsa_gpg_sign"
#  define BALSA_PIXMAP_GPG_ENCRYPT              "balsa_gpg_encrypt"
#  define BALSA_PIXMAP_INFO_SIGN                "balsa_info_sign"
#  define BALSA_PIXMAP_INFO_SIGN_GOOD           "balsa_info_sign_good"
#  define BALSA_PIXMAP_INFO_SIGN_NOTRUST        "balsa_info_sign_trust"
#  define BALSA_PIXMAP_INFO_SIGN_BAD            "balsa_info_sign_bad"
#  define BALSA_PIXMAP_INFO_ENCR                "balsa_info_encr"
#endif

void register_balsa_pixmaps(void);
void register_balsa_pixbufs(GtkWidget * widget);
void balsa_icon_create(const gchar ** data, GdkPixmap ** pmap,
                       GdkBitmap ** bmap);
#endif
