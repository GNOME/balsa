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

#include <gnome.h>
#include <gdk/gdkx.h>

#include "balsa-icons.h"

#include "pixmaps/balsa_attachment.xpm"
#include "pixmaps/balsa_compose.xpm"
#include "pixmaps/balsa_continue.xpm"
#include "pixmaps/balsa_receive.xpm"
#include "pixmaps/balsa_reply.xpm"
#include "pixmaps/balsa_reply_all.xpm"
#include "pixmaps/balsa_reply_group.xpm"
#include "pixmaps/balsa_forward.xpm"
#include "pixmaps/balsa_next.xpm"
#include "pixmaps/balsa_previous.xpm"
#include "pixmaps/balsa_postpone.xpm"
#include "pixmaps/balsa_print.xpm"
#include "pixmaps/balsa_save.xpm"
#include "pixmaps/balsa_send.xpm"
#include "pixmaps/balsa_send_receive.xpm"
#include "pixmaps/balsa_trash.xpm"
#include "pixmaps/balsa_trash_empty.xpm"
#include "pixmaps/balsa_next_unread.xpm"
#include "pixmaps/balsa_next_flagged.xpm"
#include "pixmaps/balsa_flagged.xpm"
#include "pixmaps/balsa_show_headers.xpm"
#include "pixmaps/balsa_show_preview.xpm"
#include "pixmaps/balsa_marked_new.xpm"
#include "pixmaps/balsa_marked_all.xpm"
#include "pixmaps/balsa_identity.xpm"
#include "pixmaps/balsa_close_mbox.xpm"

#include "pixmaps/mbox_draft.xpm"
#include "pixmaps/mbox_in.xpm"
#include "pixmaps/mbox_out.xpm"
#include "pixmaps/mbox_sent.xpm"
#include "pixmaps/mbox_trash.xpm"

#include "pixmaps/mbox_tray_empty.xpm"
#include "pixmaps/mbox_tray_full.xpm"

#include "pixmaps/mbox_dir_closed.xpm"
#include "pixmaps/mbox_dir_open.xpm"

#include "pixmaps/info_replied.xpm"
#include "pixmaps/info_read.xpm"
#include "pixmaps/info_forward.xpm"
#include "pixmaps/info_flagged.xpm"
#include "pixmaps/info_new.xpm"
#include "pixmaps/info_attachment.xpm"

#include "pixmaps/menu_flagged.xpm"
#include "pixmaps/menu_new.xpm"
#include "pixmaps/menu_identity.xpm"
#include "pixmaps/menu_forward.xpm"
#include "pixmaps/menu_reply.xpm"
#include "pixmaps/menu_reply_all.xpm"
#include "pixmaps/menu_reply_group.xpm"
#include "pixmaps/menu_postpone.xpm"
#include "pixmaps/menu_print.xpm"
#include "pixmaps/menu_next.xpm"
#include "pixmaps/menu_previous.xpm"
#include "pixmaps/menu_save.xpm"
#include "pixmaps/menu_send.xpm"
#include "pixmaps/menu_send_receive.xpm"
#include "pixmaps/menu_compose.xpm"
#include "pixmaps/menu_attachment.xpm"
#include "pixmaps/menu_receive.xpm"
#include "pixmaps/menu_next_flagged.xpm"
#include "pixmaps/menu_next_unread.xpm"
#include "pixmaps/menu_mark_all.xpm"

#include "pixmaps/other_close.xpm"

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

typedef struct _BalsaIcon BalsaIcon;
struct _BalsaIcon {
    GdkPixmap *p;
    GdkBitmap *b;
};

static BalsaIcon mbox_draft;
static BalsaIcon mbox_in;
static BalsaIcon mbox_out;
static BalsaIcon mbox_sent;
static BalsaIcon mbox_trash;

static BalsaIcon mbox_tray_empty;
static BalsaIcon mbox_tray_full;

static BalsaIcon mbox_dir_open;
static BalsaIcon mbox_dir_closed;

static BalsaIcon info_replied;
static BalsaIcon info_read;
static BalsaIcon info_forward;
static BalsaIcon info_flagged;
static BalsaIcon info_new;
static BalsaIcon info_attachment;

static void
create_icon(gchar ** data, GdkPixmap ** pmap, GdkBitmap ** bmap)
{
    /* Is there any reason to use gdkpixbuf here? */
    *pmap = gdk_pixmap_create_from_xpm_d(GDK_ROOT_PARENT(), bmap, 0, data);
}

void
balsa_icons_init(void)
{
    create_icon(mbox_draft_xpm,		&mbox_draft.p,		&mbox_draft.b);
    create_icon(mbox_in_xpm,		&mbox_in.p,		&mbox_in.b);
    create_icon(mbox_out_xpm,		&mbox_out.p,		&mbox_out.b);
    create_icon(mbox_sent_xpm,		&mbox_sent.p,		&mbox_sent.b);
    create_icon(mbox_trash_xpm,		&mbox_trash.p,		&mbox_trash.b);

    create_icon(mbox_tray_empty_xpm,	&mbox_tray_empty.p,	&mbox_tray_empty.b);
    create_icon(mbox_tray_full_xpm,	&mbox_tray_full.p,	&mbox_tray_full.b);

    create_icon(mbox_dir_closed_xpm,	&mbox_dir_closed.p,	&mbox_dir_closed.b);
    create_icon(mbox_dir_open_xpm,	&mbox_dir_open.p,	&mbox_dir_open.b);

    create_icon(info_replied_xpm,	&info_replied.p,	&info_replied.b);
    create_icon(info_read_xpm,		&info_read.p,		&info_read.b);
    create_icon(info_forward_xpm,	&info_forward.p,	&info_forward.b);
    create_icon(info_flagged_xpm,	&info_flagged.p,	&info_flagged.b);
    create_icon(info_new_xpm,		&info_new.p,		&info_new.b);
    create_icon(info_attachment_xpm,	&info_attachment.p,	&info_attachment.b);
}

GdkPixmap *
balsa_icon_get_pixmap(BalsaIconName name)
{
    switch (name) {
	case BALSA_ICON_MBOX_DRAFT:		return mbox_draft.p;
	case BALSA_ICON_MBOX_IN:		return mbox_in.p;
	case BALSA_ICON_MBOX_OUT:		return mbox_out.p;
	case BALSA_ICON_MBOX_SENT:		return mbox_sent.p;
	case BALSA_ICON_MBOX_TRASH:		return mbox_trash.p;

	case BALSA_ICON_MBOX_TRAY_EMPTY:	return mbox_tray_empty.p;
	case BALSA_ICON_MBOX_TRAY_FULL:		return mbox_tray_full.p;

	case BALSA_ICON_MBOX_DIR_CLOSED:	return mbox_dir_closed.p;
	case BALSA_ICON_MBOX_DIR_OPEN:		return mbox_dir_open.p;

	case BALSA_ICON_INFO_REPLIED:		return info_replied.p;
	case BALSA_ICON_INFO_READ:		return info_read.p;
	case BALSA_ICON_INFO_FORWARD:		return info_forward.p;
	case BALSA_ICON_INFO_FLAGGED:		return info_flagged.p;
	case BALSA_ICON_INFO_NEW:		return info_new.p;
	case BALSA_ICON_INFO_ATTACHMENT:	return info_attachment.p;
    }

    return NULL;
}

GdkBitmap *
balsa_icon_get_bitmap(BalsaIconName name)
{
    switch (name) {
	case BALSA_ICON_MBOX_DRAFT:		return mbox_draft.b;
	case BALSA_ICON_MBOX_IN:		return mbox_in.b;
	case BALSA_ICON_MBOX_OUT:		return mbox_out.b;
	case BALSA_ICON_MBOX_SENT:		return mbox_sent.b;
	case BALSA_ICON_MBOX_TRASH:		return mbox_trash.b;

	case BALSA_ICON_MBOX_TRAY_EMPTY:	return mbox_tray_empty.b;
	case BALSA_ICON_MBOX_TRAY_FULL:		return mbox_tray_full.b;

	case BALSA_ICON_MBOX_DIR_CLOSED:	return mbox_dir_closed.b;
	case BALSA_ICON_MBOX_DIR_OPEN:		return mbox_dir_open.b;

	case BALSA_ICON_INFO_REPLIED:		return info_replied.b;
	case BALSA_ICON_INFO_READ:		return info_read.b;
	case BALSA_ICON_INFO_FORWARD:		return info_forward.b;
	case BALSA_ICON_INFO_FLAGGED:		return info_flagged.b;
	case BALSA_ICON_INFO_NEW:		return info_new.b;
	case BALSA_ICON_INFO_ATTACHMENT:	return info_attachment.b;
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
	{ BALSA_PIXMAP_ATTACHMENT,	    balsa_attachment_xpm,  24, 24 },
	{ BALSA_PIXMAP_NEW,		    balsa_compose_xpm,	   24, 24 },
	{ BALSA_PIXMAP_CONTINUE,	    balsa_continue_xpm,	   24, 24 },
	{ BALSA_PIXMAP_RECEIVE,		    balsa_receive_xpm,	   24, 24 },
	{ BALSA_PIXMAP_REPLY,		    balsa_reply_xpm,	   24, 24 },
	{ BALSA_PIXMAP_REPLY_ALL,	    balsa_reply_all_xpm,   24, 24 },
	{ BALSA_PIXMAP_REPLY_GROUP,	    balsa_reply_group_xpm, 24, 24 },
	{ BALSA_PIXMAP_FORWARD,		    balsa_forward_xpm,	   24, 24 },
	{ BALSA_PIXMAP_NEXT,		    balsa_next_xpm,	   24, 24 },
	{ BALSA_PIXMAP_PREVIOUS,	    balsa_previous_xpm,	   24, 24 },
	{ BALSA_PIXMAP_POSTPONE,	    balsa_postpone_xpm,	   24, 24 },
	{ BALSA_PIXMAP_PRINT,		    balsa_print_xpm,	   24, 24 },
	{ BALSA_PIXMAP_SAVE,		    balsa_save_xpm,	   24, 24 },
	{ BALSA_PIXMAP_SEND,		    balsa_send_xpm,	   24, 24 },
	{ BALSA_PIXMAP_SEND_RECEIVE,	    balsa_send_receive_xpm,24, 24 },
	{ BALSA_PIXMAP_TRASH,		    balsa_trash_xpm,	   24, 24 },
	{ BALSA_PIXMAP_TRASH_EMPTY,	    balsa_trash_empty_xpm, 24, 24 },
	{ BALSA_PIXMAP_NEXT_UNREAD,	    balsa_next_unread_xpm, 24, 24 },
	{ BALSA_PIXMAP_NEXT_FLAGGED,	    balsa_next_flagged_xpm,24, 24 },
	{ BALSA_PIXMAP_SHOW_HEADERS,	    balsa_show_headers_xpm,24, 24 },
	{ BALSA_PIXMAP_SHOW_PREVIEW,	    balsa_show_preview_xpm,24, 24 },
	{ BALSA_PIXMAP_MARKED_NEW,	    balsa_marked_new_xpm,  24, 24 },
	{ BALSA_PIXMAP_MARKED_ALL,	    balsa_marked_all_xpm,  24, 24 },
	{ BALSA_PIXMAP_IDENTITY,	    balsa_identity_xpm,	   24, 24 },
	{ BALSA_PIXMAP_CLOSE_MBOX,	    balsa_close_mbox_xpm,  24, 24 },
	{ BALSA_PIXMAP_TOGGLE_FLAGGED,      balsa_flagged_xpm,     24, 24 },

	/* Menu icons */
	{ BALSA_PIXMAP_MENU_NEW,	    menu_new_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_FLAGGED,	    menu_flagged_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_IDENTITY,	    menu_identity_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_FORWARD,	    menu_forward_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_REPLY,	    menu_reply_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_REPLY_ALL,	    menu_reply_all_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_REPLY_GROUP,    menu_reply_group_xpm,  16, 16 },
	{ BALSA_PIXMAP_MENU_POSTPONE,	    menu_postpone_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_PRINT,	    menu_print_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_NEXT,	    menu_next_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_PREVIOUS,	    menu_previous_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_SAVE,	    menu_save_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_SEND,	    menu_send_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_SEND_RECEIVE,   menu_send_receive_xpm, 16, 16 },
	{ BALSA_PIXMAP_MENU_COMPOSE,	    menu_compose_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_ATTACHMENT,	    menu_attachment_xpm,   16, 16 },
	{ BALSA_PIXMAP_MENU_RECEIVE,	    menu_receive_xpm,	   16, 16 },
	{ BALSA_PIXMAP_MENU_NEXT_FLAGGED,   menu_next_flagged_xpm, 16, 16 },
	{ BALSA_PIXMAP_MENU_NEXT_UNREAD,    menu_next_unread_xpm,  16, 16 },
	{ BALSA_PIXMAP_MENU_MARK_ALL,	    menu_mark_all_xpm,	   16, 16 },

	/* Other icons */
	{ BALSA_PIXMAP_OTHER_CLOSE,		other_close_xpm,    9, 9 },
    };

    unsigned i;
    for(i = 0; i < ELEMENTS(icons); i++)
	register_balsa_pixmap(icons[i].name, icons[i].xpm,
			      icons[i].w, icons[i].h);
}
