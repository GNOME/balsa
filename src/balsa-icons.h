/* Balsa E-Mail Client
 * Copyright (C) 1998 Stuart Parmenter
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

typedef enum
  {
    BALSA_ICON_INBOX,
    BALSA_ICON_OUTBOX,
    BALSA_ICON_TRASH,

    BALSA_ICON_TRAY_EMPTY,
    BALSA_ICON_TRAY_FULL,

    BALSA_ICON_DIR_OPEN,
    BALSA_ICON_DIR_CLOSED,

    BALSA_ICON_REPLIED,
    BALSA_ICON_FORWARDED,

    BALSA_ICON_ENVELOPE,

    BALSA_ICON_ARROW,
    BALSA_ICON_MULTIPART,

    BALSA_ICON_FLAGGED
  }
BalsaIconName;

void balsa_icons_init (void);
GdkPixmap *balsa_icon_get_pixmap (BalsaIconName icon);
GdkBitmap *balsa_icon_get_bitmap (BalsaIconName icon);

#endif
