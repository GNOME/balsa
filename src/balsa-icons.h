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

#define BALSA_PIXMAP_MENU_NEW			"menu_new"
#define BALSA_PIXMAP_MENU_FLAGGED		"menu_flagged"
#define BALSA_PIXMAP_MENU_IDENTITY		"menu_identity"
#define BALSA_PIXMAP_MENU_FORWARD		"menu_forward"
#define BALSA_PIXMAP_MENU_REPLY	                "menu_reply"
#define BALSA_PIXMAP_MENU_REPLY_ALL		"menu_reply_all"
#define BALSA_PIXMAP_MENU_REPLY_GROUP		"menu_reply_group"
#define BALSA_PIXMAP_MENU_POSTPONE		"menu_postpone"
#define BALSA_PIXMAP_MENU_PRINT	                "menu_print"
#define BALSA_PIXMAP_MENU_NEXT			"menu_next"
#define BALSA_PIXMAP_MENU_PREVIOUS		"menu_previous"
#define BALSA_PIXMAP_MENU_SAVE			"menu_save"
#define BALSA_PIXMAP_MENU_SEND			"menu_send"
#define BALSA_PIXMAP_MENU_SEND_RECEIVE		"menu_send_receive"
#define BALSA_PIXMAP_MENU_COMPOSE		"menu_compose"
#define BALSA_PIXMAP_MENU_ATTACHMENT		"menu_attachment"
#define BALSA_PIXMAP_MENU_RECEIVE		"menu_receive"
#define BALSA_PIXMAP_MENU_NEXT_FLAGGED		"menu_next_flagged"
#define BALSA_PIXMAP_MENU_NEXT_UNREAD		"menu_next_unread"
#define BALSA_PIXMAP_MENU_MARK_ALL		"menu_mark_all"

#define BALSA_PIXMAP_OTHER_CLOSE		"other_close"

typedef enum {
    BALSA_ICON_MBOX_DRAFT,
    BALSA_ICON_MBOX_IN,
    BALSA_ICON_MBOX_OUT,
    BALSA_ICON_MBOX_SENT,
    BALSA_ICON_MBOX_TRASH,

    BALSA_ICON_MBOX_TRAY_EMPTY,
    BALSA_ICON_MBOX_TRAY_FULL,

    BALSA_ICON_MBOX_DIR_OPEN,
    BALSA_ICON_MBOX_DIR_CLOSED,

    BALSA_ICON_INFO_REPLIED,
    BALSA_ICON_INFO_READ,
    BALSA_ICON_INFO_FORWARD,
    BALSA_ICON_INFO_FLAGGED,
    BALSA_ICON_INFO_NEW,
    BALSA_ICON_INFO_ATTACHMENT,
} BalsaIconName;

void balsa_icons_init(void);
GdkPixmap *balsa_icon_get_pixmap(BalsaIconName icon);
GdkBitmap *balsa_icon_get_bitmap(BalsaIconName icon);
void register_balsa_pixmaps(void);
#endif
