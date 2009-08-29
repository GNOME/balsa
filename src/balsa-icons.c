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

#include <string.h>

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-icons.h"

#include "libbalsa.h"
#include "mailbox.h"
#include "address-view.h"

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

/* comment out the next line to suppress info about loading images */
/* #define BICONS_VERBOSE */

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
    GtkIconSize sizes[BALSA_PIXMAP_SIZES];
} balsa_pixmap_t;

typedef struct {
    const gchar * def_id;
    const gchar * fb_id;
} pixmap_fallback_t;
    

void
balsa_icon_create(const gchar ** data, GdkPixmap ** pmap, GdkBitmap ** bmap)
{
    /* Is there any reason to use gdkpixbuf here? */
    *pmap = gdk_pixmap_create_from_xpm_d(gdk_get_default_root_window(),
                                         bmap, 0, (gchar **) data);
}

static GHashTable *balsa_icon_table;

static void
load_balsa_pixmap(GtkIconTheme *icon_theme, GtkIconFactory *factory, 
		  const balsa_pixmap_t *bpixmap)
{
    GdkPixbuf *pixbuf;
    GtkIconSet *icon_set;
    GError *error = NULL;
    gint n, width, height;
    const gchar * use_id;
    static pixmap_fallback_t fallback_id[] = {
	{ "user-trash", GTK_STOCK_DELETE },
	{ "user-trash-full", GTK_STOCK_DELETE },
	{ "mail-attachment", "stock_attach" },
	{ "emblem-important", "stock_mail-flag-for-followup"},
        { "stock_mail-reply", "stock_mail-replied"}, 
        { "stock_mail-reply-to-all", "stock_mail-replied"}, 
        { "stock_mail-forward", "stock_mail-replied"}, 
        { "gnome-fs-directory-accept", GTK_STOCK_OPEN},
        { "gnome-fs-directory", GTK_STOCK_DIRECTORY},
	{ NULL, NULL } };

    BICONS_LOG("loading icon %s (stock id %s)", bpixmap->name,
	       bpixmap->stock_id);

    /* check if the icon theme knows the icon and try to fall back to an
     * alternative name if not */
    if (!gtk_icon_theme_has_icon(icon_theme, bpixmap->stock_id)) {
	pixmap_fallback_t *fb = fallback_id;
	while (fb->def_id && strcmp(fb->def_id, bpixmap->stock_id))
	    fb++;
	if (fb->def_id) {
	    use_id = fb->fb_id;
            BICONS_LOG("\t(%s not found, fall back to %s)",
                       bpixmap->stock_id, use_id);
        } else {
	    BICONS_ERR("icon %s unknown, no fallback", bpixmap->stock_id);
	    use_id = GTK_STOCK_MISSING_IMAGE;
	}
    } else
	use_id = bpixmap->stock_id;

    g_hash_table_insert(balsa_icon_table, g_strdup(bpixmap->name), 
                        g_strdup(use_id));

    if (!gtk_icon_size_lookup(bpixmap->sizes[0], &width, &height)) {
	BICONS_ERR("failed: could not look up default icon size %d",
		   bpixmap->sizes[0]);
	return;
    }

    pixbuf = 
	gtk_icon_theme_load_icon(icon_theme, use_id, width,
				 GTK_ICON_LOOKUP_USE_BUILTIN, &error);
    if (!pixbuf) {
	BICONS_ERR("default size %d failed: %s", width, error->message);
	g_error_free(error);
	return;
    }
    BICONS_LOG("\tloaded with size %d", width);
    icon_set = gtk_icon_set_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);

    for (n = 1; 
	 n < BALSA_PIXMAP_SIZES && bpixmap->sizes[n] > GTK_ICON_SIZE_INVALID;
	 n++) {
	if (gtk_icon_size_lookup(bpixmap->sizes[n], &width, &height)) {
	    pixbuf = 
		gtk_icon_theme_load_icon(icon_theme, use_id, width,
					 GTK_ICON_LOOKUP_USE_BUILTIN, &error);
	    if (!pixbuf) {
		BICONS_ERR("additional size %d failed: %s", width,
			   error->message);
		g_clear_error(&error);
	    } else {
		GtkIconSource *icon_source = gtk_icon_source_new();
		
		BICONS_LOG("\tloaded with size %d", width);
		gtk_icon_source_set_pixbuf(icon_source, pixbuf);
		g_object_unref(pixbuf);
		gtk_icon_source_set_size(icon_source, bpixmap->sizes[n]);
		gtk_icon_source_set_size_wildcarded(icon_source, FALSE);
		gtk_icon_set_add_source(icon_set, icon_source);
                gtk_icon_source_free(icon_source);
	    }
	} else
	    BICONS_ERR("bad size %d", bpixmap->sizes[n]);
    }

    gtk_icon_factory_add(factory, bpixmap->name, icon_set);
    gtk_icon_set_unref(icon_set);
}

void
register_balsa_pixmaps(void)
{
    const balsa_pixmap_t balsa_icons[] = {
	/* icons for buttons and menus (24x24 and 16x16) */
	{ BALSA_PIXMAP_COMPOSE,         "stock_mail-compose", 
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_REPLY,           "stock_mail-reply", 
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_REPLY_GROUP,     "stock_mail-reply-to-all", 
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_REQUEST_MDN,     "stock_mail-reply", 
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_SEND,            "stock_mail-send",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_RECEIVE,         "stock_mail-receive",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_SEND_RECEIVE,    "stock_mail-send-receive",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_FORWARD,         "stock_mail-forward",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_IDENTITY,        "stock_contact",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_CONTINUE,	"stock_mail",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_POSTPONE,	"balsa-postpone",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_REPLY_ALL,	"balsa-reply-all",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_NEXT_PART,	"balsa-next-part",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_PREVIOUS_PART,	"balsa-previous-part",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_MARK_ALL,	"balsa-mark-all",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_ATTACHMENT,	"mail-attachment",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_NEXT,		"balsa-next",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_PREVIOUS,	"balsa-previous",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_NEXT_UNREAD,	"balsa-next-unread",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_NEXT_FLAGGED,	"balsa-next-flagged",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },

#ifdef HAVE_GPGME
	/* crypto status icons, for both the structure view tree (24x24)
	 * and the index (16x16) */
        { BALSA_PIXMAP_SIGN,            "balsa-signature-unknown",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
        { BALSA_PIXMAP_SIGN_GOOD,       "balsa-signature-good",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
	{ BALSA_PIXMAP_SIGN_NOTRUST,    "balsa-signature-notrust",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
        { BALSA_PIXMAP_SIGN_BAD,        "balsa-signature-bad",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
        { BALSA_PIXMAP_ENCR,            "balsa-encrypted",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
#endif

	/* the following book icons aren't strictly necessary as Gnome provides
	   them. However, this simplifies porting balsa if the Gnome libs
	   aren't present... */
        { BALSA_PIXMAP_BOOK_RED,        "stock_book_red",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },
        { BALSA_PIXMAP_BOOK_YELLOW,     "stock_book_yellow",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_BOOK_GREEN,      "stock_book_green",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_BOOK_BLUE,       "stock_book_blue",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_BOOK_OPEN,       "stock_book_open",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_MENU } },

	/* button-only icons */
	{ BALSA_PIXMAP_SHOW_HEADERS,    "stock_view-fields",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
	{ BALSA_PIXMAP_SHOW_PREVIEW,	"balsa-preview",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
	{ BALSA_PIXMAP_MARKED_NEW,	"balsa-marked-new",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
	{ BALSA_PIXMAP_TRASH_EMPTY,	"balsa-trash-empty",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
#ifdef HAVE_GPGME
	{ BALSA_PIXMAP_GPG_SIGN,        "balsa-sign",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
	{ BALSA_PIXMAP_GPG_ENCRYPT,     "balsa-encrypt",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
	{ BALSA_PIXMAP_GPG_RECHECK,     "balsa-crypt-check",
	  { GTK_ICON_SIZE_LARGE_TOOLBAR, GTK_ICON_SIZE_INVALID } },
#endif

	/* mailbox icons (16x16) */
        { BALSA_PIXMAP_MBOX_IN,         "stock_inbox",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_OUT,        "stock_outbox",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_DRAFT,      "balsa-mbox-draft",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_SENT,       "balsa-mbox-sent",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_TRASH,      "user-trash",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_TRASH_FULL, "user-trash-full",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_TRAY_FULL,  "balsa-mbox-tray-full",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_TRAY_EMPTY, "balsa-mbox-tray-empty",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_DIR_OPEN,   "gnome-fs-directory-accept",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_MBOX_DIR_CLOSED, "gnome-fs-directory",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
	
	/* index icons (16x16) */
        { BALSA_PIXMAP_INFO_REPLIED,    "stock_mail-replied",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
        { BALSA_PIXMAP_INFO_NEW,        "stock_mail-unread",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
	{ BALSA_PIXMAP_INFO_FLAGGED,    "emblem-important",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },

        /* drop-down icon for the address-view (16x16) */
	{ BALSA_PIXMAP_DROP_DOWN,       "balsa-drop-down",
	  { GTK_ICON_SIZE_MENU, GTK_ICON_SIZE_INVALID } },
	};

    unsigned i;
    GtkIconFactory *factory = gtk_icon_factory_new();
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();

    balsa_icon_table =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gtk_icon_factory_add_default(factory);
    gtk_icon_theme_append_search_path(icon_theme, BALSA_STD_PREFIX "/share/icons");
    gtk_icon_theme_append_search_path(icon_theme, BALSA_DATA_PREFIX);

    for (i = 0; i < ELEMENTS(balsa_icons); i++)
	load_balsa_pixmap(icon_theme, factory, balsa_icons + i);
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
	libbalsa_mailbox_set_good_icon, BALSA_PIXMAP_SIGN_GOOD}, {
	libbalsa_mailbox_set_notrust_icon, BALSA_PIXMAP_SIGN_NOTRUST}, {
	libbalsa_mailbox_set_bad_icon, BALSA_PIXMAP_SIGN_BAD}, {
	libbalsa_mailbox_set_sign_icon, BALSA_PIXMAP_SIGN}, {
	libbalsa_mailbox_set_encr_icon, BALSA_PIXMAP_ENCR},
#endif
        {
        libbalsa_address_view_set_book_icon,  BALSA_PIXMAP_BOOK_RED}, {
        libbalsa_address_view_set_close_icon, GTK_STOCK_CLOSE}, {
        libbalsa_address_view_set_drop_down_icon, BALSA_PIXMAP_DROP_DOWN},
    };
    guint i;

    for (i = 0; i < ELEMENTS(icons); i++)
	icons[i].set_icon(gtk_widget_render_icon(widget,
						 icons[i].icon,
						 GTK_ICON_SIZE_MENU,
						 NULL));
}

const gchar *
balsa_icon_id(const gchar * name)
{
    const gchar *retval = g_hash_table_lookup(balsa_icon_table, name);

    return retval ? retval : name;
}
