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

#include <gnome.h>
#include <gdk/gdkx.h>

#include "balsa-icons.h"

#include "pixmaps/all_headers.xpm"
#include "pixmaps/close.xpm"
#include "pixmaps/flag_new.xpm"
#include "pixmaps/flagged.xpm"

#include "pixmaps/identity.xpm"
#include "pixmaps/identity_menu.xpm"
#include "pixmaps/mark_all.xpm"
#include "pixmaps/mark_all_menu.xpm"
#include "pixmaps/next_unread.xpm"
#include "pixmaps/next_unread_menu.xpm"
#include "pixmaps/next_flagged.xpm"
#include "pixmaps/next_flagged_menu.xpm"
#include "pixmaps/reply_to_all.xpm"
#include "pixmaps/reply_to_all_menu.xpm"
#include "pixmaps/reply_to_group.xpm"
#include "pixmaps/reply_to_group_menu.xpm"

#include "pixmaps/close_mbox.xpm"
#include "pixmaps/empty_trash.xpm"
#include "pixmaps/preview.xpm"

#include "pixmaps/draftbox.xpm"
#include "pixmaps/inbox.xpm"
#include "pixmaps/outbox.xpm"
#include "pixmaps/sentbox.xpm"
#include "pixmaps/trash.xpm"

#include "pixmaps/tray_empty.xpm"
#include "pixmaps/tray_full.xpm"

#include "pixmaps/dir_closed.xpm"
#include "pixmaps/dir_open.xpm"

#include "pixmaps/replied.xpm"
#include "pixmaps/forwarded.xpm"
#include "pixmaps/envelope.xpm"
#include "pixmaps/multipart.xpm"

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

typedef struct _BalsaIcon BalsaIcon;
struct _BalsaIcon {
    GdkPixmap *p;
    GdkBitmap *b;
};

static BalsaIcon draftbox;
static BalsaIcon inbox;
static BalsaIcon outbox;
static BalsaIcon sentbox;
static BalsaIcon trash;

static BalsaIcon tray_empty;
static BalsaIcon tray_full;

static BalsaIcon dir_open;
static BalsaIcon dir_closed;

static BalsaIcon replied;
static BalsaIcon forwarded;
static BalsaIcon envelope;
static BalsaIcon multipart;

static void
create_icon(gchar ** data, GdkPixmap ** pmap, GdkBitmap ** bmap)
{
    /* Is there any reason to use gdkpixbuf here? */
    *pmap = gdk_pixmap_create_from_xpm_d(GDK_ROOT_PARENT(), bmap, 0, data);
}

void
balsa_icons_init(void)
{
    create_icon(draftbox_xpm, &draftbox.p, &draftbox.b);
    create_icon(inbox_xpm, &inbox.p, &inbox.b);
    create_icon(outbox_xpm, &outbox.p, &outbox.b);
    create_icon(sentbox_xpm, &sentbox.p, &sentbox.b);
    create_icon(trash_xpm, &trash.p, &trash.b);

    create_icon(tray_empty_xpm, &tray_empty.p, &tray_empty.b);
    create_icon(tray_full_xpm, &tray_full.p, &tray_full.b);

    create_icon(dir_closed_xpm, &dir_closed.p, &dir_closed.b);
    create_icon(dir_open_xpm, &dir_open.p, &dir_open.b);

    create_icon(replied_xpm, &replied.p, &replied.b);
    create_icon(forwarded_xpm, &forwarded.p, &forwarded.b);
    create_icon(envelope_xpm, &envelope.p, &envelope.b);
    create_icon(multipart_xpm, &multipart.p, &multipart.b);
}

GdkPixmap *
balsa_icon_get_pixmap(BalsaIconName name)
{
    switch (name) {
	case BALSA_ICON_DRAFTBOX:	return draftbox.p;
	case BALSA_ICON_INBOX:		return inbox.p;
	case BALSA_ICON_OUTBOX:		return outbox.p;
	case BALSA_ICON_SENTBOX:	return sentbox.p;
	case BALSA_ICON_TRASH:		return trash.p;

	case BALSA_ICON_TRAY_EMPTY:	return tray_empty.p;
	case BALSA_ICON_TRAY_FULL:	return tray_full.p;

	case BALSA_ICON_DIR_CLOSED:	return dir_closed.p;
	case BALSA_ICON_DIR_OPEN:	return dir_open.p;

	case BALSA_ICON_REPLIED:	return replied.p;
	case BALSA_ICON_FORWARDED:	return forwarded.p;
	case BALSA_ICON_ENVELOPE:	return envelope.p;
	case BALSA_ICON_MULTIPART:	return multipart.p;
    }

    return NULL;
}

GdkBitmap *
balsa_icon_get_bitmap(BalsaIconName name)
{
    switch (name) {
	case BALSA_ICON_DRAFTBOX:	return draftbox.b;
	case BALSA_ICON_INBOX:		return inbox.b;
	case BALSA_ICON_OUTBOX:		return outbox.b;
	case BALSA_ICON_SENTBOX:	return sentbox.b;
	case BALSA_ICON_TRASH:		return trash.b;

	case BALSA_ICON_TRAY_EMPTY:	return tray_empty.b;
	case BALSA_ICON_TRAY_FULL:	return tray_full.b;

	case BALSA_ICON_DIR_CLOSED:	return dir_closed.b;
	case BALSA_ICON_DIR_OPEN:	return dir_open.b;

	case BALSA_ICON_REPLIED:	return replied.b;
	case BALSA_ICON_FORWARDED:	return forwarded.b;
	case BALSA_ICON_ENVELOPE:	return envelope.b;
	case BALSA_ICON_MULTIPART:	return multipart.b;
    }

    return NULL;
}

static void
register_balsa_pixmap(const gchar* name, char** data, guint xsize, guint ysize)
{
    GnomeStockPixmapEntryData *entry;
    entry = g_malloc0(sizeof(*entry));

    entry->type     = GNOME_STOCK_PIXMAP_TYPE_DATA;
    entry->xpm_data = data;
    entry->width    = xsize;
    entry->height   = ysize;
    gnome_stock_pixmap_register(name, GNOME_STOCK_PIXMAP_REGULAR,
				(GnomeStockPixmapEntry *) entry);
}

void
register_balsa_pixmaps(void)
{
    const struct {
        const char* name;
        char** xpm;
        int w, h;
    } icons[] = {
        /* Toolbar icons */
        { BALSA_PIXMAP_SHOW_ALL_HEADERS, all_headers_xpm,    24, 24 },
        { BALSA_PIXMAP_SHOW_PREVIEW,     preview_xpm,        24, 24 },
        { BALSA_PIXMAP_FLAG_UNREAD,      flag_new_xpm,       24, 24 },
        { BALSA_PIXMAP_IDENTITY,         identity_xpm,       24, 24 },
        { BALSA_PIXMAP_MARK_ALL_MSGS,    mark_all_xpm,       24, 24 },
        { BALSA_PIXMAP_NEXT_UNREAD,      next_unread_xpm,    24, 24 },
        { BALSA_PIXMAP_NEXT_FLAGGED,     next_flagged_xpm,   24, 24 },
        { BALSA_PIXMAP_MAIL_RPL_GROUP,   reply_to_group_xpm, 24, 24 },
        { BALSA_PIXMAP_MAIL_EMPTY_TRASH, empty_trash_xpm,    24, 24 },
        { BALSA_PIXMAP_MAIL_RPL_ALL,     reply_to_all_xpm,   24, 24 },
        { BALSA_PIXMAP_MAIL_CLOSE_MBOX,  close_mbox_xpm,     24, 24 },
        /* Menu icons */
        { BALSA_PIXMAP_ENVELOPE,            envelope_xpm,            16, 16 },
        { BALSA_PIXMAP_FLAGGED,             flagged_xpm,             16, 16 },
        { BALSA_PIXMAP_IDENTITY_MENU,       identity_menu_xpm,       16, 16 },
        { BALSA_PIXMAP_MARK_ALL_MSGS_MENU,  mark_all_menu_xpm,       16, 16 },
        { BALSA_PIXMAP_NEXT_UNREAD_MENU,    next_unread_menu_xpm,    16, 16 },
        { BALSA_PIXMAP_NEXT_FLAGGED_MENU,   next_flagged_menu_xpm,   16, 16 },
        { BALSA_PIXMAP_MAIL_RPL_ALL_MENU,   reply_to_all_menu_xpm,   16, 16 },
        { BALSA_PIXMAP_MAIL_RPL_GROUP_MENU, reply_to_group_menu_xpm, 16, 16 },
        /* Other icons */
        { BALSA_PIXMAP_SMALL_CLOSE,      close_xpm,          9, 9 },
    };
    unsigned i;
    for(i=0; i<ELEMENTS(icons); i++)
        register_balsa_pixmap(icons[i].name, icons[i].xpm, 
                              icons[i].w, icons[i].h);
}
