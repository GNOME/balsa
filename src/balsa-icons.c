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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-icons.h"

#include "libbalsa.h"
#include "mailbox.h"
#include "address-view.h"

/* comment out the next line to suppress info about loading images */
#if 0
#define BICONS_VERBOSE
#endif

#ifdef BICONS_VERBOSE
#  define BICONS_LOG(...)   g_message(__VA_ARGS__)
#  define BICONS_ERR(...)   g_warning(__VA_ARGS__)
#else
#  define BICONS_LOG(...)
#  define BICONS_ERR(...)
#endif


#define BALSA_PIXMAP_SIZES     2
typedef struct {
    const gchar * name;
    const gchar * stock_id;
} balsa_pixmap_t;

typedef struct {
    const gchar * def_id;
    const gchar * fb_id;
} pixmap_fallback_t;

static GHashTable *balsa_icon_table;

static void
load_balsa_pixmap(GtkIconTheme *icon_theme, const balsa_pixmap_t *bpixmap)
{
    const gchar * use_id;
    static pixmap_fallback_t fallback_id[] = {
	{ "user-trash", "edit-delete" },
	{ "user-trash-full", "edit-delete" },
	{ "emblem-important", "stock_mail-flag-for-followup"},
        { "mail-reply-sender", "mail-replied"},
        { "stock_mail-reply-to-all", "mail-replied"},
        { "mail-forward", "mail-replied"},
        { "folder-drag-accept", "document-open"},
	{ NULL, NULL } };

    BICONS_LOG("loading icon %s (stock id %s)", bpixmap->name,
	       bpixmap->stock_id);

    /* check if the icon theme knows the icon and try to fall back to an
     * alternative name if not */
    if (!gtk_icon_theme_has_icon(icon_theme, bpixmap->stock_id)) {
	pixmap_fallback_t *fb = fallback_id;
	while (fb->def_id && g_strcmp0(fb->def_id, bpixmap->stock_id) != 0)
	    fb++;
	if (!fb->def_id) {
	    BICONS_LOG("No GTK or custom icon for %s\n", bpixmap);
	    return;
	}
	if (fb->def_id) {
	    use_id = fb->fb_id;
            BICONS_LOG("\t(%s not found, fall back to %s)",
                       bpixmap->stock_id, use_id);
        } else {
	    BICONS_ERR("icon %s unknown, no fallback", bpixmap->stock_id);
	    use_id = "image-missing";
	}
    } else {
	use_id = bpixmap->stock_id;
    }

    BICONS_LOG("\tuse_id %s", use_id);
    g_hash_table_insert(balsa_icon_table, g_strdup(bpixmap->name),
                        g_strdup(use_id));
}

void
balsa_register_pixmaps(void)
{
    const balsa_pixmap_t balsa_icons[] = {
	/* icons for buttons and menus (24x24 and 16x16) */
	{ BALSA_PIXMAP_COMPOSE,         "mail-message-new" },
	{ BALSA_PIXMAP_REPLY,           "mail-reply-sender" },
	{ BALSA_PIXMAP_REPLY_GROUP,     "stock_mail-reply-to-all" },
	{ BALSA_PIXMAP_REQUEST_MDN,     "mail-reply-sender" },
	{ BALSA_PIXMAP_SEND,            "mail-send" },
	{ BALSA_PIXMAP_RECEIVE,         "stock_mail-receive" },
	{ BALSA_PIXMAP_SEND_RECEIVE,    "mail-send-receive" },
	{ BALSA_PIXMAP_QUEUE,           "mail-queue" },
	{ BALSA_PIXMAP_SEND_QUEUED,		"balsa-send-queued" },
	{ BALSA_PIXMAP_FORWARD,         "mail-forward" },
	{ BALSA_PIXMAP_IDENTITY,        "stock_contact" },
	{ BALSA_PIXMAP_CONTINUE,	"stock_mail" },
	{ BALSA_PIXMAP_POSTPONE,	"balsa-postpone" },
	{ BALSA_PIXMAP_REPLY_ALL,	"balsa-reply-all" },
	{ BALSA_PIXMAP_NEXT_PART,	"balsa-next-part" },
	{ BALSA_PIXMAP_PREVIOUS_PART,	"balsa-previous-part" },
	{ BALSA_PIXMAP_MARK_ALL,	"balsa-mark-all" },
	{ BALSA_PIXMAP_ATTACHMENT,	"mail-attachment" },
	{ BALSA_PIXMAP_NEXT,		"balsa-next" },
	{ BALSA_PIXMAP_PREVIOUS,	"balsa-previous" },
	{ BALSA_PIXMAP_NEXT_UNREAD,	"balsa-next-unread" },
	{ BALSA_PIXMAP_NEXT_FLAGGED,	"balsa-next-flagged" },

#ifdef HAVE_GPGME
	/* crypto status icons, for both the structure view tree (24x24)
	 * and the index (16x16) */
        { BALSA_PIXMAP_SIGN,            "balsa-signature-unknown" },
        { BALSA_PIXMAP_SIGN_GOOD,       "balsa-signature-good" },
	{ BALSA_PIXMAP_SIGN_NOTRUST,    "balsa-signature-notrust" },
        { BALSA_PIXMAP_SIGN_BAD,        "balsa-signature-bad" },
        { BALSA_PIXMAP_ENCR,            "balsa-encrypted" },
#endif

	/* the following book icons aren't strictly necessary as Gnome provides
	   them. However, this simplifies porting balsa if the Gnome libs
	   aren't present... */
        { BALSA_PIXMAP_BOOK_RED,        "stock_book_red" },
        { BALSA_PIXMAP_BOOK_YELLOW,     "stock_book_yellow" },
        { BALSA_PIXMAP_BOOK_GREEN,      "stock_book_green" },
        { BALSA_PIXMAP_BOOK_BLUE,       "stock_book_blue" },
        { BALSA_PIXMAP_BOOK_OPEN,       "stock_book_open" },

	/* button-only icons */
	{ BALSA_PIXMAP_SHOW_HEADERS,    "stock_view-fields" },
	{ BALSA_PIXMAP_SHOW_PREVIEW,	"balsa-preview" },
	{ BALSA_PIXMAP_MARKED_NEW,	"balsa-marked-new" },
	{ BALSA_PIXMAP_TRASH_EMPTY,	"balsa-trash-empty" },
#ifdef HAVE_GPGME
	{ BALSA_PIXMAP_GPG_SIGN,        "balsa-sign" },
	{ BALSA_PIXMAP_GPG_ENCRYPT,     "balsa-encrypt" },
	{ BALSA_PIXMAP_GPG_RECHECK,     "balsa-crypt-check" },
#endif

	/* mailbox icons (16x16) */
        { BALSA_PIXMAP_MBOX_IN,         "mail-inbox" },
        { BALSA_PIXMAP_MBOX_OUT,        "mail-outbox" },
        { BALSA_PIXMAP_MBOX_DRAFT,      "balsa-mbox-draft" },
        { BALSA_PIXMAP_MBOX_SENT,       "balsa-mbox-sent" },
        { BALSA_PIXMAP_MBOX_TRASH,      "user-trash" },
        { BALSA_PIXMAP_MBOX_TRASH_FULL, "user-trash-full" },
        { BALSA_PIXMAP_MBOX_TRAY_FULL,  "balsa-mbox-tray-full" },
        { BALSA_PIXMAP_MBOX_TRAY_EMPTY, "balsa-mbox-tray-empty" },
        { BALSA_PIXMAP_MBOX_DIR_OPEN,   "folder-drag-accept" },
        { BALSA_PIXMAP_MBOX_DIR_CLOSED, "folder" },

	/* index icons (16x16) */
        { BALSA_PIXMAP_INFO_REPLIED,    "mail-replied" },
        { BALSA_PIXMAP_INFO_NEW,        "mail-unread" },
	{ BALSA_PIXMAP_INFO_FLAGGED,    "emblem-important" },

        /* drop-down icon for the address-view (16x16) */
	{ BALSA_PIXMAP_DROP_DOWN,       "pan-down-symbolic" },
	};

    unsigned i;
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();

    balsa_icon_table =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gtk_icon_theme_append_search_path(icon_theme, BALSA_DATA_PREFIX);

    for (i = 0; i < G_N_ELEMENTS(balsa_icons); i++)
	load_balsa_pixmap(icon_theme, balsa_icons + i);
}

void
balsa_unregister_pixmaps(void)
{
    if (balsa_icon_table) {
        g_hash_table_destroy(balsa_icon_table);
        balsa_icon_table = NULL;
    }
}

void
balsa_register_pixbufs(GtkWidget * widget)
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
	libbalsa_mailbox_set_good_icon, BALSA_PIXMAP_SIGN_GOOD}, {
	libbalsa_mailbox_set_notrust_icon, BALSA_PIXMAP_SIGN_NOTRUST}, {
	libbalsa_mailbox_set_bad_icon, BALSA_PIXMAP_SIGN_BAD}, {
	libbalsa_mailbox_set_sign_icon, BALSA_PIXMAP_SIGN}, {
	libbalsa_mailbox_set_encr_icon, BALSA_PIXMAP_ENCR},
#endif
        {
        libbalsa_address_view_set_book_icon,  BALSA_PIXMAP_BOOK_RED}, {
        libbalsa_address_view_set_close_icon, "window-close-symbolic"}, {
        libbalsa_address_view_set_drop_down_icon, BALSA_PIXMAP_DROP_DOWN},
    };
    guint i;
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();

    for (i = 0; i < G_N_ELEMENTS(icons); i++) {
        GdkPixbuf *pixbuf;
        GError *err = NULL;
        gint width, height;
        const gchar *use_id = balsa_icon_id(icons[i].icon);

        if (use_id == NULL) /* No icon table */
            break;

        gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
        pixbuf =
            gtk_icon_theme_load_icon(icon_theme, use_id, width,
                                     GTK_ICON_LOOKUP_USE_BUILTIN, &err);
        if (err) {
            g_print("%s %s size %d err %s\n", __func__, use_id,
                    width, err->message);
            g_clear_error(&err);
        } else {
            icons[i].set_icon(pixbuf);
        }
    }
}

const gchar *
balsa_icon_id(const gchar * name)
{
    const gchar *retval = NULL;

    if (balsa_icon_table != NULL) {
        retval = g_hash_table_lookup(balsa_icon_table, name);
        if (retval == NULL)
            retval = name;
    }

    return retval;
}
