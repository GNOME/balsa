/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#define BALSA_PIXMAP_SHOW_ALL_HEADERS	"show_all_headers"
#define BALSA_PIXMAP_SMALL_CLOSE	"small_close"
#define BALSA_PIXMAP_ENVELOPE		"envelope"
#define BALSA_PIXMAP_FLAG_UNREAD	"flag_unread"
#define BALSA_PIXMAP_FLAGGED		"flagged"

#define BALSA_PIXMAP_IDENTITY		"identity"
#define BALSA_PIXMAP_IDENTITY_MENU	"identity_menu"

#define BALSA_PIXMAP_MARK_ALL_MSGS	"mark_all"

#define BALSA_PIXMAP_NEXT_UNREAD	"next_unread"
#define BALSA_PIXMAP_NEXT_UNREAD_MENU	"next_unread_menu"
#define BALSA_PIXMAP_NEXT_FLAGGED       "next_flagged"
#define BALSA_PIXMAP_NEXT_FLAGGED_MENU  "next_flagged_menu"

#define BALSA_PIXMAP_MAIL_RPL_ALL	"reply_to_all"
#define BALSA_PIXMAP_MAIL_RPL_ALL_MENU	"reply_to_all_menu"
#define BALSA_PIXMAP_MAIL_RPL_GROUP	"reply_to_group"
#define BALSA_PIXMAP_MAIL_RPL_GROUP_MENU "reply_to_group_menu"

#define BALSA_PIXMAP_MAIL_CLOSE_MBOX	"close_mbox"

#define BALSA_PIXMAP_MAIL_EMPTY_TRASH	"empty_trash"

#define BALSA_PIXMAP_SHOW_PREVIEW	"show_preview"

typedef enum {
    BALSA_ICON_DRAFTBOX,
    BALSA_ICON_INBOX,
    BALSA_ICON_OUTBOX,
    BALSA_ICON_SENTBOX,
    BALSA_ICON_TRASH,

    BALSA_ICON_TRAY_EMPTY,
    BALSA_ICON_TRAY_FULL,

    BALSA_ICON_DIR_OPEN,
    BALSA_ICON_DIR_CLOSED,

    BALSA_ICON_REPLIED,
    BALSA_ICON_FORWARDED,
    BALSA_ICON_ENVELOPE,
    BALSA_ICON_MULTIPART,
} BalsaIconName;

void balsa_icons_init(void);
GdkPixmap *balsa_icon_get_pixmap(BalsaIconName icon);
GdkBitmap *balsa_icon_get_bitmap(BalsaIconName icon);
void register_balsa_pixmaps(void);
void register_balsa_pixmap(gchar * name, gchar ** data, guint xsize, guint ysize);
#endif
