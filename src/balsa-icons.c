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

#include "config.h"

#include "balsa-icons.h"
#include "libbalsa.h"
#include "mailbox.h"

#include "pixmaps/balsa_attachment.xpm"
#include "pixmaps/balsa_compose.xpm"
#include "pixmaps/balsa_continue.xpm"
#include "pixmaps/balsa_receive.xpm"
#include "pixmaps/balsa_reply.xpm"
#include "pixmaps/balsa_reply_all.xpm"
#include "pixmaps/balsa_reply_group.xpm"
#include "pixmaps/balsa_forward.xpm"
#include "pixmaps/balsa_next.xpm"
#include "pixmaps/balsa_next_part.xpm"
#include "pixmaps/balsa_previous.xpm"
#include "pixmaps/balsa_previous_part.xpm"
#include "pixmaps/balsa_postpone.xpm"
#include "pixmaps/balsa_print.xpm"
#include "pixmaps/balsa_save.xpm"
#include "pixmaps/balsa_send.xpm"
#include "pixmaps/balsa_send_receive.xpm"
#include "pixmaps/balsa_trash.xpm"
#include "pixmaps/balsa_trash_empty.xpm"
#include "pixmaps/balsa_next_unread.xpm"
#include "pixmaps/balsa_next_flagged.xpm"
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
#include "pixmaps/info_flagged.xpm"
#include "pixmaps/info_new.xpm"
#include "pixmaps/info_attachment.xpm"
#ifdef HAVE_GPGME
#  include "pixmaps/balsa_gpg_sign.xpm"
#  include "pixmaps/balsa_gpg_encrypt.xpm"
#  include "pixmaps/balsa_gpg_recheck.xpm"
#  include "pixmaps/info_lock.xpm"
#  include "pixmaps/info_lock_good.xpm"
#  include "pixmaps/info_lock_sigtrust.xpm"
#  include "pixmaps/info_lock_bad.xpm"
#  include "pixmaps/info_lock_encr.xpm"
#endif

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
#include "pixmaps/menu_next_part.xpm"
#include "pixmaps/menu_previous.xpm"
#include "pixmaps/menu_previous_part.xpm"
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

void
balsa_icon_create(const gchar ** data, GdkPixmap ** pmap, GdkBitmap ** bmap)
{
    /* Is there any reason to use gdkpixbuf here? */
    *pmap = gdk_pixmap_create_from_xpm_d(gdk_get_default_root_window(),
                                         bmap, 0, (gchar **) data);
}

static void
register_balsa_pixmap(const gchar * stock_id, const char ** data,
                      GtkIconFactory * factory)
{
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_xpm_data(data);
    GtkIconSet *icon_set = gtk_icon_set_new_from_pixbuf(pixbuf);

    gtk_icon_factory_add(factory, stock_id, icon_set);
}

void
register_balsa_pixmaps(void)
{
    const struct {
	const char *name;
	const char **xpm;
    } icons[] = {
	/* Toolbar icons */
	{ BALSA_PIXMAP_ATTACHMENT,	    balsa_attachment_xpm},
	{ BALSA_PIXMAP_NEW,		    balsa_compose_xpm},
	{ BALSA_PIXMAP_CONTINUE,	    balsa_continue_xpm},
	{ BALSA_PIXMAP_RECEIVE,		    balsa_receive_xpm},
	{ BALSA_PIXMAP_REPLY,		    balsa_reply_xpm},
	{ BALSA_PIXMAP_REPLY_ALL,	    balsa_reply_all_xpm},
	{ BALSA_PIXMAP_REPLY_GROUP,	    balsa_reply_group_xpm},
	{ BALSA_PIXMAP_FORWARD,		    balsa_forward_xpm},
	{ BALSA_PIXMAP_NEXT,		    balsa_next_xpm},
	{ BALSA_PIXMAP_NEXT_PART,		    balsa_next_part_xpm},
	{ BALSA_PIXMAP_PREVIOUS,	    balsa_previous_xpm},
	{ BALSA_PIXMAP_PREVIOUS_PART,	    balsa_previous_part_xpm},
	{ BALSA_PIXMAP_POSTPONE,	    balsa_postpone_xpm},
	{ BALSA_PIXMAP_PRINT,		    balsa_print_xpm},
	{ BALSA_PIXMAP_SAVE,		    balsa_save_xpm},
	{ BALSA_PIXMAP_SEND,		    balsa_send_xpm},
	{ BALSA_PIXMAP_SEND_RECEIVE,	    balsa_send_receive_xpm},
	{ BALSA_PIXMAP_TRASH,		    balsa_trash_xpm},
	{ BALSA_PIXMAP_TRASH_EMPTY,	    balsa_trash_empty_xpm},
	{ BALSA_PIXMAP_NEXT_UNREAD,	    balsa_next_unread_xpm},
	{ BALSA_PIXMAP_NEXT_FLAGGED,	    balsa_next_flagged_xpm},
	{ BALSA_PIXMAP_SHOW_HEADERS,	    balsa_show_headers_xpm},
	{ BALSA_PIXMAP_SHOW_PREVIEW,	    balsa_show_preview_xpm},
	{ BALSA_PIXMAP_MARKED_NEW,	    balsa_marked_new_xpm},
	{ BALSA_PIXMAP_MARKED_ALL,	    balsa_marked_all_xpm},
	{ BALSA_PIXMAP_IDENTITY,	    balsa_identity_xpm},
	{ BALSA_PIXMAP_CLOSE_MBOX,	    balsa_close_mbox_xpm},
#ifdef HAVE_GPGME
	{ BALSA_PIXMAP_GPG_SIGN,            balsa_gpg_sign_xpm},
	{ BALSA_PIXMAP_GPG_ENCRYPT,         balsa_gpg_encrypt_xpm},
	{ BALSA_PIXMAP_GPG_RECHECK,         balsa_gpg_recheck_xpm},
#endif

	/* Menu icons */
	{ BALSA_PIXMAP_MENU_NEW,	    menu_new_xpm},
	{ BALSA_PIXMAP_MENU_FLAGGED,	    menu_flagged_xpm},
	{ BALSA_PIXMAP_MENU_IDENTITY,	    menu_identity_xpm},
	{ BALSA_PIXMAP_MENU_FORWARD,	    menu_forward_xpm},
	{ BALSA_PIXMAP_MENU_REPLY,	    menu_reply_xpm},
	{ BALSA_PIXMAP_MENU_REPLY_ALL,	    menu_reply_all_xpm},
	{ BALSA_PIXMAP_MENU_REPLY_GROUP,    menu_reply_group_xpm},
	{ BALSA_PIXMAP_MENU_POSTPONE,	    menu_postpone_xpm},
	{ BALSA_PIXMAP_MENU_PRINT,	    menu_print_xpm},
	{ BALSA_PIXMAP_MENU_NEXT,	    menu_next_xpm},
	{ BALSA_PIXMAP_MENU_NEXT_PART,	    menu_next_part_xpm},
	{ BALSA_PIXMAP_MENU_PREVIOUS,	    menu_previous_xpm},
	{ BALSA_PIXMAP_MENU_PREVIOUS_PART,	    menu_previous_part_xpm},
	{ BALSA_PIXMAP_MENU_SAVE,	    menu_save_xpm},
	{ BALSA_PIXMAP_MENU_SEND,	    menu_send_xpm},
	{ BALSA_PIXMAP_MENU_SEND_RECEIVE,   menu_send_receive_xpm},
	{ BALSA_PIXMAP_MENU_COMPOSE,	    menu_compose_xpm},
	{ BALSA_PIXMAP_MENU_ATTACHMENT,	    menu_attachment_xpm},
	{ BALSA_PIXMAP_MENU_RECEIVE,	    menu_receive_xpm},
	{ BALSA_PIXMAP_MENU_NEXT_FLAGGED,   menu_next_flagged_xpm},
	{ BALSA_PIXMAP_MENU_NEXT_UNREAD,    menu_next_unread_xpm},
	{ BALSA_PIXMAP_MENU_MARK_ALL,	    menu_mark_all_xpm},

	/* Other icons */
	{ BALSA_PIXMAP_OTHER_CLOSE,		other_close_xpm},

        /* BalsaMBList icons */
        { BALSA_PIXMAP_MBOX_DRAFT,      mbox_draft_xpm },
        { BALSA_PIXMAP_MBOX_IN,         mbox_in_xpm },
        { BALSA_PIXMAP_MBOX_OUT,        mbox_out_xpm },
        { BALSA_PIXMAP_MBOX_SENT,       mbox_sent_xpm },
        { BALSA_PIXMAP_MBOX_TRASH,      mbox_trash_xpm },
        { BALSA_PIXMAP_MBOX_TRAY_EMPTY, mbox_tray_empty_xpm },
        { BALSA_PIXMAP_MBOX_TRAY_FULL,  mbox_tray_full_xpm },
        { BALSA_PIXMAP_MBOX_DIR_OPEN,   mbox_dir_open_xpm },
        { BALSA_PIXMAP_MBOX_DIR_CLOSED, mbox_dir_closed_xpm },

        /* BalsaIndex icons */
        { BALSA_PIXMAP_INFO_FLAGGED,    info_flagged_xpm },
        { BALSA_PIXMAP_INFO_REPLIED,    info_replied_xpm },
        { BALSA_PIXMAP_INFO_NEW,        info_new_xpm },
        { BALSA_PIXMAP_INFO_ATTACHMENT, info_attachment_xpm },
	{ BALSA_PIXMAP_INFO_DELETED,	mbox_trash_xpm}, /* share the icon */
#ifdef HAVE_GPGME
        { BALSA_PIXMAP_INFO_SIGN,       info_sign_xpm },
        { BALSA_PIXMAP_INFO_SIGN_GOOD,  info_sign_good_xpm },
	{ BALSA_PIXMAP_INFO_SIGN_NOTRUST, info_lock_sigtrust_xpm },
        { BALSA_PIXMAP_INFO_SIGN_BAD,   info_sign_bad_xpm },
        { BALSA_PIXMAP_INFO_ENCR,       info_encr_xpm },
#endif
    };

    unsigned i;
    GtkIconFactory *factory = gtk_icon_factory_new();

    gtk_icon_factory_add_default(factory);

    for(i = 0; i < ELEMENTS(icons); i++)
	register_balsa_pixmap(icons[i].name, icons[i].xpm,
                              factory);
}

void
register_balsa_pixbufs(GtkWidget * widget)
{
    static struct {
	void (*set_icon) (GdkPixbuf *);
	const gchar *icon;
    } icons[] = {
	{
	libbalsa_mailbox_set_unread_icon,  BALSA_PIXMAP_INFO_NEW}, {
	libbalsa_mailbox_set_trash_icon,   BALSA_PIXMAP_INFO_DELETED}, {
	libbalsa_mailbox_set_flagged_icon, BALSA_PIXMAP_INFO_FLAGGED}, {
	libbalsa_mailbox_set_replied_icon, BALSA_PIXMAP_INFO_REPLIED}, {
	libbalsa_mailbox_set_attach_icon, BALSA_PIXMAP_INFO_ATTACHMENT},
#ifdef HAVE_GPGME
	{
	libbalsa_mailbox_set_good_icon, BALSA_PIXMAP_INFO_SIGN_GOOD}, {
	libbalsa_mailbox_set_notrust_icon, BALSA_PIXMAP_INFO_SIGN_NOTRUST}, {
	libbalsa_mailbox_set_bad_icon, BALSA_PIXMAP_INFO_SIGN_BAD}, {
	libbalsa_mailbox_set_sign_icon, BALSA_PIXMAP_INFO_SIGN}, {
	libbalsa_mailbox_set_encr_icon, BALSA_PIXMAP_INFO_ENCR},
#endif
    };
    guint i;

    for (i = 0; i < ELEMENTS(icons); i++)
	icons[i].set_icon(gtk_widget_render_icon(widget,
						 icons[i].icon,
						 GTK_ICON_SIZE_MENU,
						 NULL));
}
