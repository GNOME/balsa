/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-2003 Stuart Parmenter and others, see AUTHORS file.
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

/* FONT SELECTION DISCUSSION:
   We use pango now.
   Locale data is then used exclusively for the spelling checking.
*/


#include "config.h"

#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <ctype.h>
#include <glib.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include <sys/stat.h>		/* for check_if_regular_file() */
#include <sys/wait.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "libbalsa.h"
#include "misc.h"
#include "send.h"

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "balsa-icons.h"

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#include "threads.h"
#endif

#include "sendmsg-window.h"
#include "ab-window.h"
#include "address-entry.h"
#include "expand-alias.h"
#include "print.h"
#include "spell-check.h"
#include "toolbar-factory.h"

#define GNOME_MIME_BUG_WORKAROUND 1
typedef struct {
    pid_t pid_editor;
    gchar *filename;
    BalsaSendmsg *bsmsg;
} balsa_edit_with_gnome_data;


static gchar *read_signature(BalsaSendmsg *bsmsg);
static gint include_file_cb(GtkWidget *, BalsaSendmsg *);
static gint send_message_cb(GtkWidget *, BalsaSendmsg *);
static void send_message_toolbar_cb(GtkWidget *, BalsaSendmsg *);
static gint queue_message_cb(GtkWidget *, BalsaSendmsg *);
static gint message_postpone(BalsaSendmsg * bsmsg);
static void postpone_message_cb(GtkWidget *, BalsaSendmsg *);
static void save_message_cb(GtkWidget *, BalsaSendmsg *);
static void print_message_cb(GtkWidget *, BalsaSendmsg *);
static void attach_clicked(GtkWidget *, gpointer);
static void destroy_attachment (gpointer data);
static gboolean attach_message(BalsaSendmsg *bsmsg, LibBalsaMessage *message);
static gint insert_selected_messages(BalsaSendmsg *bsmsg, SendType type);
static gint attach_message_cb(GtkWidget *, BalsaSendmsg *);
static gint include_message_cb(GtkWidget *, BalsaSendmsg *);
static void close_window_cb(GtkWidget *, gpointer);
static gchar* check_if_regular_file(const gchar *);
static void balsa_sendmsg_destroy_handler(BalsaSendmsg * bsmsg);
static void check_readiness(BalsaSendmsg * bsmsg);
static void init_menus(BalsaSendmsg *);
static gint toggle_from_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_cc_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_bcc_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_fcc_cb(GtkWidget *, BalsaSendmsg *);
#if !defined(ENABLE_TOUCH_UI)
static gint toggle_reply_cb(GtkWidget *, BalsaSendmsg *);
#endif
static void toggle_reqdispnotify_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void toggle_format_cb(GtkCheckMenuItem * check_menu_item,
                             BalsaSendmsg * bsmsg);
#ifdef HAVE_GPGME
static void toggle_sign_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void toggle_sign_tb_cb(GtkToggleButton * widget, BalsaSendmsg * bsmsg);
static void toggle_encrypt_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void toggle_encrypt_tb_cb(GtkToggleButton * widget, BalsaSendmsg * bsmsg);
static void toggle_gpg_mode_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void bsmsg_setup_gpg_ui(BalsaSendmsg *bsmsg, GtkWidget *toolbar);
static void bsmsg_update_gpg_ui_on_ident_change(BalsaSendmsg *bsmsg,
                                                LibBalsaIdentity *new_ident);
#endif

#if defined(ENABLE_TOUCH_UI)
static gboolean bsmsg_check_format_compatibility(GtkWindow *parent,
                                                 const char *filename);
#endif /* ENABLE_TOUCH_UI */

static void spell_check_cb(GtkWidget * widget, BalsaSendmsg *);
static void sw_spell_check_response(BalsaSpellCheck * spell_check,
                                    gint response, BalsaSendmsg * bsmsg);

static void address_book_cb(GtkWidget *widget, BalsaSendmsg *bsmsg);
static void address_book_response(GtkWidget * ab, gint response,
                                  LibBalsaAddressEntry * address_entry);

static gint set_locale(BalsaSendmsg *, gint);

#if !defined(ENABLE_TOUCH_UI)
static void edit_with_gnome(GtkWidget* widget, BalsaSendmsg* bsmsg);
#endif
static void change_identity_dialog_cb(GtkWidget*, BalsaSendmsg*);
static void repl_identity_signature(BalsaSendmsg* bsmsg, 
                                    LibBalsaIdentity* new_ident,
                                    LibBalsaIdentity* old_ident,
                                    gint* replace_offset, gint siglen, 
                                    gchar* new_sig);
static gchar* prep_signature(LibBalsaIdentity* ident, gchar* sig);
static void update_bsmsg_identity(BalsaSendmsg*, LibBalsaIdentity*);

static void sw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc);
static GString *quoteBody(BalsaSendmsg * bsmsg, LibBalsaMessage * message,
                          SendType type);
static void set_list_post_address(BalsaSendmsg * bsmsg);
static gboolean set_list_post_rfc2369(BalsaSendmsg * bsmsg, GList * p);
static gchar *rfc2822_skip_comments(gchar * str);
static void address_changed_cb(LibBalsaAddressEntry * address_entry,
                               BalsaSendmsgAddress *sma);
static void set_ready(LibBalsaAddressEntry * address_entry,
                      BalsaSendmsgAddress *sma);
static void sendmsg_window_set_title(BalsaSendmsg * bsmsg);

/* dialog callback */
static void response_cb(GtkDialog * dialog, gint response, gpointer data);

/* icon list callbacks */
static void select_attachment(GnomeIconList * ilist, gint num,
                              GdkEventButton * event, gpointer data);
static gboolean sw_popup_menu_cb(GtkWidget * widget, gpointer data);
/* helpers */
static gboolean sw_do_popup(GnomeIconList * ilist, GdkEventButton * event);

/* Undo/Redo buffer helpers. */
static void sw_buffer_save(BalsaSendmsg * bsmsg);
static void sw_buffer_swap(BalsaSendmsg * bsmsg, gboolean undo);
static void sw_buffer_signals_connect(BalsaSendmsg * bsmsg);
static void sw_buffer_signals_disconnect(BalsaSendmsg * bsmsg);
static void sw_buffer_set_undo(BalsaSendmsg * bsmsg, gboolean undo,
			       gboolean redo);

/* Standard DnD types */
enum {
    TARGET_MESSAGES,
    TARGET_URI_LIST,
    TARGET_EMAIL,
    TARGET_STRING
};

static GtkTargetEntry drop_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES},
    {"text/uri-list", 0, TARGET_URI_LIST},
    { "STRING",     0, TARGET_STRING },
    { "text/plain", 0, TARGET_STRING },
};

static GtkTargetEntry email_field_drop_types[] = {
    {"x-application/x-email", 0, TARGET_EMAIL}
};

static void sw_undo_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void sw_redo_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void cut_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void copy_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void paste_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void select_all_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void wrap_body_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void reflow_selected_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static gint insert_signature_cb(GtkWidget *, BalsaSendmsg *);
static gint quote_messages_cb(GtkWidget *, BalsaSendmsg *);
static void lang_set_cb(GtkWidget *widget, BalsaSendmsg *bsmsg);

static void set_entry_to_subject(GtkEntry* entry, LibBalsaMessage * message,
                                 SendType type, LibBalsaIdentity* ident);

/* the array of locale names and charset names included in the MIME
   type information.  
   if you add a new encoding here add to SendCharset in libbalsa.c 
*/
struct SendLocales {
    const gchar *locale, *charset, *lang_name;
} locales[] = {
    {"pt_BR", "ISO-8859-1",    N_("_Brazilian")},
    {"ca_ES", "ISO-8859-15",   N_("_Catalan")},
    {"zh_CN.GB2312", "gb2312", N_("_Chinese Simplified")},
    {"zh_TW.Big5", "big5",     N_("_Chinese Traditional")},
    {"cs_CZ", "ISO-8859-2",    N_("_Czech")},
    {"da_DK", "ISO-8859-1",    N_("_Danish")},
    {"nl_NL", "ISO-8859-15",   N_("_Dutch")},
    {"en_US", "ISO-8859-1",    N_("_English (American)")}, 
    {"en_GB", "ISO-8859-1",    N_("_English (British)")}, 
    {"eo_XX", "UTF-8",         N_("_Esperanto")},
    {"et_EE", "ISO-8859-15",   N_("_Estonian")},
    {"fi_FI", "ISO-8859-15",   N_("_Finnish")},
    {"fr_FR", "ISO-8859-15",   N_("_French")},
    {"de_DE", "ISO-8859-15",   N_("_German")},
    {"el_GR", "ISO-8859-7",    N_("_Greek")},
    {"he_IL", "UTF-8",         N_("_Hebrew")},
    {"hu_HU", "ISO-8859-2",    N_("_Hungarian")},
    {"it_IT", "ISO-8859-15",   N_("_Italian")},
    {"ja_JP", "euc-jp",        N_("_Japanese")},
    {"ko_KR", "euc-kr",        N_("_Korean")},
    {"lv_LV", "ISO-8859-13",   N_("_Latvian")},
    {"lt_LT", "ISO-8859-13",   N_("_Lithuanian")},
    {"no_NO", "ISO-8859-1",    N_("_Norwegian")},
    {"pl_PL", "ISO-8859-2",    N_("_Polish")},
    {"pt_PT", "ISO-8859-15",   N_("_Portugese")},
    {"ro_RO", "ISO-8859-2",    N_("_Romanian")},
    {"ru_SU", "ISO-8859-5",    N_("_Russian (ISO)")},
    {"ru_RU", "KOI8-R",        N_("_Russian (KOI)")},
    {"sr_Cyrl", "ISO-8859-5",  N_("_Serbian")},
    {"sr_Latn", "ISO-8859-2",  N_("_Serbian (Latin)")},
    {"sk_SK", "ISO-8859-2",    N_("_Slovak")},
    {"es_ES", "ISO-8859-15",   N_("_Spanish")},
    {"sv_SE", "ISO-8859-1",    N_("_Swedish")},
    {"tr_TR", "ISO-8859-9",    N_("_Turkish")},
    {"uk_UK", "KOI8-U",        N_("_Ukrainian")},
    {"", "UTF-8",              N_("_Generic UTF-8")}
};

/* ===================================================================
   Balsa menus. Touchpad has some simplified menus which do not
   overlap very much with the default balsa menus. They are here
   because they represent an alternative probably appealing to the all
   proponents of GNOME2 dumbify approach (OK, I am bit unfair here).
   We first put shared menu items, next we define default balsa
   stuff we put touchpad optimized menus at the end.
*/
typedef struct {
    gchar *name;
    guint length;
} headerMenuDesc;

static GnomeUIInfo lang_menu[] = {
    GNOMEUIINFO_END
};


#ifdef HAVE_GPGME
static GnomeUIInfo gpg_mode_list[] = {
#define OPTS_MENU_GPG_3156_POS 0
    GNOMEUIINFO_RADIOITEM_DATA(N_("_GnuPG uses MIME mode"),
                               NULL,
                               toggle_gpg_mode_cb,
                               GINT_TO_POINTER(LIBBALSA_PROTECT_RFC3156),
                               NULL),
#define OPTS_MENU_GPG_2440_POS 1
    GNOMEUIINFO_RADIOITEM_DATA(N_("_GnuPG uses old OpenPGP mode"),
                               NULL,
                               toggle_gpg_mode_cb,
                               GINT_TO_POINTER(LIBBALSA_PROTECT_OPENPGP),
                               NULL),
#ifdef HAVE_SMIME
#define OPTS_MENU_SMIME_POS 2
    GNOMEUIINFO_RADIOITEM_DATA(N_("_S/MIME mode (GpgSM)"),
                               NULL,
                               toggle_gpg_mode_cb,
                               GINT_TO_POINTER(LIBBALSA_PROTECT_SMIMEV3),
                               NULL),
#endif
    GNOMEUIINFO_END
};
#endif


#if !defined(ENABLE_TOUCH_UI)
/* default balsa menu */
static GnomeUIInfo file_menu[] = {
#define MENU_FILE_INCLUDE_POS 0
    GNOMEUIINFO_ITEM_STOCK(N_("_Include File..."), NULL,
			   include_file_cb, GTK_STOCK_OPEN),
#define MENU_FILE_ATTACH_POS 1
    GNOMEUIINFO_ITEM_STOCK(N_("_Attach File..."), NULL,
			   attach_clicked, BALSA_PIXMAP_MENU_ATTACHMENT),
#define MENU_MSG_INCLUDE_POS 2
    GNOMEUIINFO_ITEM_STOCK(N_("I_nclude Message(s)"), NULL,
			   include_message_cb, BALSA_PIXMAP_MENU_NEW),
#define MENU_FILE_ATTACH_MSG_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("Attach _Message(s)"), NULL,
			   attach_message_cb, BALSA_PIXMAP_MENU_FORWARD),
#define MENU_FILE_SEPARATOR1_POS 4
    GNOMEUIINFO_SEPARATOR,

#define MENU_FILE_SEND_POS 5
    { GNOME_APP_UI_ITEM, N_("Sen_d"),
      N_("Send this message"),
      send_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_SEND, GDK_Return, GDK_CONTROL_MASK, NULL },
#define MENU_FILE_QUEUE_POS 6
    { GNOME_APP_UI_ITEM, N_("_Queue"),
      N_("Queue this message in Outbox for sending"),
      queue_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_SEND, 'Q', GDK_CONTROL_MASK, NULL },
#define MENU_FILE_POSTPONE_POS 7
    GNOMEUIINFO_ITEM_STOCK(N_("_Postpone"), NULL,
			   postpone_message_cb, BALSA_PIXMAP_MENU_POSTPONE),
#define MENU_FILE_SAVE_POS 8
    { GNOME_APP_UI_ITEM, N_("_Save"),
      N_("Save this message"),
      save_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_SAVE, 'S', GDK_CONTROL_MASK, NULL },
#define MENU_FILE_PRINT_POS 9
    GNOMEUIINFO_ITEM_STOCK(N_("_Print..."), N_("Print the edited message"),
			   print_message_cb, BALSA_PIXMAP_MENU_PRINT),
#define MENU_FILE_SEPARATOR2_POS 10
    GNOMEUIINFO_SEPARATOR,

#define MENU_FILE_CLOSE_POS 11
    GNOMEUIINFO_MENU_CLOSE_ITEM(close_window_cb, NULL),

    GNOMEUIINFO_END
};

/* Cut, Copy&Paste are in our case just a placeholders because they work
   anyway */
static GnomeUIInfo edit_menu[] = {
#define EDIT_MENU_UNDO 0
    GNOMEUIINFO_MENU_UNDO_ITEM(sw_undo_cb, NULL),
#define EDIT_MENU_REDO EDIT_MENU_UNDO + 1
    GNOMEUIINFO_MENU_REDO_ITEM(sw_redo_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_CUT EDIT_MENU_REDO + 2
    GNOMEUIINFO_MENU_CUT_ITEM(cut_cb, NULL),
#define EDIT_MENU_COPY EDIT_MENU_CUT + 1
    GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
#define EDIT_MENU_PASTE EDIT_MENU_COPY + 1
    GNOMEUIINFO_MENU_PASTE_ITEM(paste_cb, NULL),
#define EDIT_MENU_SELECT_ALL EDIT_MENU_PASTE + 1
    GNOMEUIINFO_MENU_SELECT_ALL_ITEM(select_all_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_WRAP_BODY EDIT_MENU_SELECT_ALL + 2
    {GNOME_APP_UI_ITEM, N_("_Wrap Body"), N_("Wrap message lines"),
     (gpointer) wrap_body_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_b, GDK_CONTROL_MASK, NULL},
#define EDIT_MENU_REFLOW_SELECTED EDIT_MENU_WRAP_BODY + 1
    {GNOME_APP_UI_ITEM, N_("_Reflow Selected Text"), NULL,
     (gpointer) reflow_selected_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_r, GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_ADD_SIGNATURE EDIT_MENU_REFLOW_SELECTED + 2
    {GNOME_APP_UI_ITEM, N_("Insert Si_gnature"), NULL,
     (gpointer) insert_signature_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_g, GDK_CONTROL_MASK, NULL},
#define EDIT_MENU_QUOTE EDIT_MENU_ADD_SIGNATURE + 1
    {GNOME_APP_UI_ITEM, N_("_Quote Message(s)"), NULL,
     (gpointer) quote_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     0, 0, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_SPELL_CHECK EDIT_MENU_QUOTE + 2
    GNOMEUIINFO_ITEM_STOCK(N_("C_heck Spelling"), 
                           N_("Check the spelling of the message"),
                           spell_check_cb,
                           GTK_STOCK_SPELL_CHECK),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_SELECT_IDENT EDIT_MENU_SPELL_CHECK + 2
    GNOMEUIINFO_ITEM_STOCK(N_("Select _Identity..."), 
                           N_("Select the Identity to use for the message"),
                           change_identity_dialog_cb,
                           BALSA_PIXMAP_MENU_IDENTITY),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_EDIT_GNOME EDIT_MENU_SELECT_IDENT + 2
    GNOMEUIINFO_ITEM_STOCK(N_("_Edit with Gnome-Editor"),
                           N_("Edit the current message with "
                              "the default Gnome editor"),
                           edit_with_gnome,
                           BALSA_PIXMAP_MENU_IDENTITY), /*FIXME: Other icon */
    GNOMEUIINFO_END
};

typedef gint (*ViewMenuFunc)(GtkWidget * widget, BalsaSendmsg * bsmsg);
#define VIEW_MENU_FUNC(f) ((ViewMenuFunc) (f))

static GnomeUIInfo view_menu[] = {
#define MENU_TOGGLE_FROM_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("Fr_om"), NULL, toggle_from_cb, NULL),
#define MENU_TOGGLE_CC_POS 1
    GNOMEUIINFO_TOGGLEITEM(N_("_Cc"), NULL, toggle_cc_cb, NULL),
#define MENU_TOGGLE_BCC_POS 2
    GNOMEUIINFO_TOGGLEITEM(N_("_Bcc"), NULL, toggle_bcc_cb, NULL),
#define MENU_TOGGLE_FCC_POS 3
    GNOMEUIINFO_TOGGLEITEM(N_("_Fcc"), NULL, toggle_fcc_cb, NULL),
#define MENU_TOGGLE_REPLY_POS 4
    GNOMEUIINFO_TOGGLEITEM(N_("_Reply To"), NULL, toggle_reply_cb, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo opts_menu[] = {
#define OPTS_MENU_DISPNOTIFY_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("_Request Disposition Notification"), NULL, 
			   toggle_reqdispnotify_cb, NULL),
#define OPTS_MENU_FORMAT_POS 1
    GNOMEUIINFO_TOGGLEITEM(N_("_Format = Flowed"), NULL, 
			   toggle_format_cb, NULL),
#ifdef HAVE_GPGME
    GNOMEUIINFO_SEPARATOR,
#define OPTS_MENU_SIGN_POS 3
    GNOMEUIINFO_TOGGLEITEM(N_("_Sign Message"), 
			   N_("signs the message using GnuPG"),
			   toggle_sign_cb, NULL),
#define OPTS_MENU_ENCRYPT_POS 4
    GNOMEUIINFO_TOGGLEITEM(N_("_Encrypt Message"), 
			   N_("signs the message using GnuPG for all To: and CC: recipients"),
			   toggle_encrypt_cb, NULL),
    GNOMEUIINFO_RADIOLIST(gpg_mode_list),
#endif
    GNOMEUIINFO_END
};
#define DISPNOTIFY_WIDGET opts_menu[OPTS_MENU_DISPNOTIFY_POS].widget


headerMenuDesc headerDescs[] =  {
    {"from", 3}, {"cc", 3}, {"bcc", 3}, {"fcc", 2}, {"replyto", 3}
};

#define MAIN_MENUS_COUNT 5
static GnomeUIInfo main_menu[] = {
#define MAIN_FILE_MENU 0
    GNOMEUIINFO_MENU_FILE_TREE(file_menu),
#define MAIN_EDIT_MENU 1
    GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
#define MAIN_VIEW_MENU 2
    GNOMEUIINFO_SUBTREE(N_("_Show"), view_menu),
#define MAIN_CHARSET_MENU 3
    GNOMEUIINFO_SUBTREE(N_("_Language"), lang_menu),
#define MAIN_OPTION_MENU 4
    GNOMEUIINFO_SUBTREE(N_("_Options"), opts_menu),
    GNOMEUIINFO_END
};
#define LANG_MENU_WIDGET main_menu[MAIN_CHARSET_MENU].widget

#if MENU_TOGGLE_REPLY_POS+1 != VIEW_MENU_LENGTH
#error Inconsistency in defined lengths.
#endif


#else /* ENABLE_TOUCH_UI */
/* ===================================================================
 * End of default balsa menus and begin touchpad-optimized menus.
 * =================================================================== */
/* touchpad-optimized menu */
static GnomeUIInfo tu_file_more_menu[] = {
    GNOMEUIINFO_ITEM_STOCK(N_("_Include File..."), NULL,
			   include_file_cb, GTK_STOCK_OPEN),
    GNOMEUIINFO_ITEM_STOCK(N_("I_nclude Message(s)"), NULL,
			   include_message_cb, BALSA_PIXMAP_MENU_NEW),
#define MENU_FILE_ATTACH_MSG_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("Attach _Message(s)"), NULL,
			   attach_message_cb, BALSA_PIXMAP_MENU_FORWARD),
    GNOMEUIINFO_END
};

/* touchpad optimized version of the menu */
static GnomeUIInfo file_menu[] = {
#define MENU_FILE_ATTACH_POS 0
    GNOMEUIINFO_ITEM_STOCK(N_("_Attach File..."), NULL,
			   attach_clicked, BALSA_PIXMAP_MENU_ATTACHMENT),
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_SAVE_POS 2
    { GNOME_APP_UI_ITEM, N_("_Save"),
      N_("Save this message"),
      save_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_SAVE, 'S', GDK_CONTROL_MASK, NULL },
#define MENU_FILE_PRINT_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("_Print..."), N_("Print the edited message"),
			   print_message_cb, BALSA_PIXMAP_MENU_PRINT),
    GNOMEUIINFO_SUBTREE(N_("_More"), tu_file_more_menu),
#define MENU_FILE_POSTPONE_POS 5
    GNOMEUIINFO_ITEM_STOCK(N_("Sa_ve and Close"), NULL,
			   postpone_message_cb, BALSA_PIXMAP_MENU_POSTPONE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_SEND_POS 7
    { GNOME_APP_UI_ITEM, N_("Sen_d"),
      N_("Send this message"),
      send_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_SEND, GDK_Return, GDK_CONTROL_MASK, NULL },
#define MENU_FILE_QUEUE_POS 8
    { GNOME_APP_UI_ITEM, N_("Send _Later"),
      N_("Queue this message in Outbox for sending"),
      queue_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_SEND, 'Q', GDK_CONTROL_MASK, NULL },
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_CLOSE_POS 10
    GNOMEUIINFO_MENU_CLOSE_ITEM(close_window_cb, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo tu_edit_more_menu[] = {
#define EDIT_MENU_WRAP_BODY EDIT_MENU_SELECT_ALL + 2
    {GNOME_APP_UI_ITEM, N_("_Wrap Body"), N_("Wrap message lines"),
     (gpointer) wrap_body_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_b, GDK_CONTROL_MASK, NULL},
#define EDIT_MENU_REFLOW_SELECTED EDIT_MENU_WRAP_BODY + 1
    {GNOME_APP_UI_ITEM, N_("_Reflow Selected Text"), NULL,
     (gpointer) reflow_selected_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_r, GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
    {GNOME_APP_UI_ITEM, N_("_Quote Message(s)"), NULL,
     (gpointer) quote_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     0, 0, NULL},
    GNOMEUIINFO_END
};

/* Cut, Copy&Paste are in our case just a placeholders because they work
   anyway */
static GnomeUIInfo edit_menu[] = {
#define EDIT_MENU_UNDO 0
    GNOMEUIINFO_MENU_UNDO_ITEM(sw_undo_cb, NULL),
#define EDIT_MENU_REDO EDIT_MENU_UNDO + 1
    GNOMEUIINFO_MENU_REDO_ITEM(sw_redo_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_CUT EDIT_MENU_REDO + 2
    GNOMEUIINFO_MENU_CUT_ITEM(cut_cb, NULL),
#define EDIT_MENU_COPY EDIT_MENU_CUT + 1
    GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
#define EDIT_MENU_PASTE EDIT_MENU_COPY + 1
    GNOMEUIINFO_MENU_PASTE_ITEM(paste_cb, NULL),
#define EDIT_MENU_SELECT_ALL EDIT_MENU_PASTE + 1
    GNOMEUIINFO_MENU_SELECT_ALL_ITEM(select_all_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_ADD_SIGNATURE EDIT_MENU_SELECT_ALL + 2
    {GNOME_APP_UI_ITEM, N_("Insert Si_gnature"), NULL,
     (gpointer) insert_signature_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_g, GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SUBTREE(N_("_More"), tu_edit_more_menu),
    GNOMEUIINFO_END
};

typedef gint (*ViewMenuFunc)(GtkWidget * widget, BalsaSendmsg * bsmsg);
#define VIEW_MENU_FUNC(f) ((ViewMenuFunc) (f))

/* touchscreen-optimized version of the menu */
static GnomeUIInfo view_menu[] = {
#define MENU_TOGGLE_FROM_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("Fr_om"),NULL, toggle_from_cb, NULL),
#define MENU_TOGGLE_CC_POS 1
    GNOMEUIINFO_TOGGLEITEM(N_("_Cc"),  NULL, toggle_cc_cb, NULL),
#define MENU_TOGGLE_BCC_POS 2
    GNOMEUIINFO_TOGGLEITEM(N_("_Bcc"), NULL, toggle_bcc_cb, NULL),
#define MENU_TOGGLE_FCC_POS 3
    GNOMEUIINFO_TOGGLEITEM(N_("_Fcc"), NULL, toggle_fcc_cb, NULL),
    GNOMEUIINFO_END
};

headerMenuDesc headerDescs[] =
    { {"from", 3}, {"cc", 3}, {"bcc", 3}, {"fcc", 2} };
#if MENU_TOGGLE_FCC_POS+1 != VIEW_MENU_LENGTH
#error Inconsistency in defined lengths.
#endif

static GnomeUIInfo opts_menu[] = {
#define OPTS_MENU_FORMAT_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("_Format = Flowed"), NULL, 
			   toggle_format_cb, NULL),
#ifdef HAVE_GPGME
    GNOMEUIINFO_SEPARATOR,
#define OPTS_MENU_SIGN_POS 2
    GNOMEUIINFO_TOGGLEITEM(N_("_Sign Message"), 
			   N_("signs the message using GnuPG"),
			   toggle_sign_cb, NULL),
#define OPTS_MENU_ENCRYPT_POS 3
    GNOMEUIINFO_TOGGLEITEM(N_("_Encrypt Message"), 
			   N_("signs the message using GnuPG for all To: and CC: recipients"),
			   toggle_encrypt_cb, NULL),
    GNOMEUIINFO_RADIOLIST(gpg_mode_list),
#endif
    GNOMEUIINFO_END
};

static GnomeUIInfo tu_tools_menu[] = {
    GNOMEUIINFO_ITEM_STOCK(N_("C_heck Spelling"), 
                           N_("Check the spelling of the message"),
                           spell_check_cb,
                           GTK_STOCK_SPELL_CHECK),
#define MAIN_CHARSET_MENU_POS 1
    GNOMEUIINFO_SUBTREE(N_("_Language"), lang_menu),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_ITEM_STOCK(N_("Select _Identity..."), 
                           N_("Select the Identity to use for the message"),
                           change_identity_dialog_cb,
                           BALSA_PIXMAP_MENU_IDENTITY),
#define OPTS_MENU_DISPNOTIFY_POS 4
    GNOMEUIINFO_TOGGLEITEM(N_("_Request Disposition Notification"), NULL, 
			   toggle_reqdispnotify_cb, NULL),
    GNOMEUIINFO_SUBTREE(N_("_More"), opts_menu),
    GNOMEUIINFO_END
};
#define DISPNOTIFY_WIDGET tu_tools_menu[OPTS_MENU_DISPNOTIFY_POS].widget
#define LANG_MENU_WIDGET tu_tools_menu[MAIN_CHARSET_MENU_POS].widget


#define MAIN_MENUS_COUNT 5
static GnomeUIInfo main_menu[] = {
#define MAIN_FILE_MENU 0
    GNOMEUIINFO_MENU_FILE_TREE(file_menu),
#define MAIN_EDIT_MENU 1
    GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
#define MAIN_VIEW_MENU 2
    GNOMEUIINFO_SUBTREE(N_("_Show"), view_menu),
#define MAIN_OPTION_MENU 4
    GNOMEUIINFO_SUBTREE(N_("_Tools"), tu_tools_menu),
    GNOMEUIINFO_END
};

#endif /* ENABLE_TOUCH_UI */
/* ===================================================================
 *                End of touchpad-optimized menus.
 * =================================================================== */


/* i'm sure there's a subtle and nice way of making it visible here */
typedef struct {
    gchar *filename;
    gchar *force_mime_type;
    gboolean delete_on_destroy;
    gboolean as_extbody;
} attachment_t;


static void
append_comma_separated(GtkEditable *editable, const gchar * text)
{
    gint position;

    if (!text || !*text)
        return;

    gtk_editable_set_position(editable, -1);
    position = gtk_editable_get_position(editable);
    if (position > 0)
        gtk_editable_insert_text(editable, ", ", 2, &position);
    gtk_editable_insert_text(editable, text, -1, &position);
    gtk_editable_set_position(editable, position);
}

/* the callback handlers */
#define BALSA_SENDMSG_ADDRESS_BOOK_KEY "balsa-sendmsg-address-book"
static void
address_book_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkWidget *ab;
    LibBalsaAddressEntry *address_entry;

    /* Show only one dialog per window; one per address entry could be
     * confusing. */
    ab = g_object_get_data(G_OBJECT(bsmsg->window),
                           BALSA_SENDMSG_ADDRESS_BOOK_KEY);
    if (ab) {
        gdk_window_raise(ab->window);
        return;
    }

    address_entry =
        LIBBALSA_ADDRESS_ENTRY(g_object_get_data(G_OBJECT(widget),
                                                 "address-entry-widget"));
    gtk_widget_set_sensitive(GTK_WIDGET(address_entry), FALSE);

    ab = balsa_ab_window_new(TRUE, GTK_WINDOW(bsmsg->window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(ab), TRUE);
    g_signal_connect(G_OBJECT(ab), "response",
                     G_CALLBACK(address_book_response), address_entry);
    g_object_set_data(G_OBJECT(bsmsg->window),
                      BALSA_SENDMSG_ADDRESS_BOOK_KEY, ab);
    gtk_widget_show_all(ab);
}

/* Callback for the "response" signal for the address book dialog. */
static void
address_book_response(GtkWidget * ab, gint response,
                      LibBalsaAddressEntry * address_entry)
{
    GtkWindow *parent = gtk_window_get_transient_for(GTK_WINDOW(ab));

    if (response == GTK_RESPONSE_OK) {
        gchar *t = balsa_ab_window_get_recipients(BALSA_AB_WINDOW(ab));
        append_comma_separated(GTK_EDITABLE(address_entry), t);
        g_free(t);
    }

    gtk_widget_destroy(ab);
    g_object_set_data(G_OBJECT(parent), BALSA_SENDMSG_ADDRESS_BOOK_KEY,
                      NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(address_entry), TRUE);
}

static gint
delete_handler(BalsaSendmsg* bsmsg)
{
    gint reply;
    if(balsa_app.debug) printf("delete_event_cb\n");
    if(bsmsg->modified) {
        const gchar *tmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->to[1]));
	GtkWidget* d = 
            gtk_message_dialog_new(GTK_WINDOW(bsmsg->window),
                                   GTK_DIALOG_MODAL|
                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_YES_NO,
                                   _("The message to '%s' is modified.\n"
                                     "Save message to Draftbox?"),
                                   tmp);

        gtk_dialog_add_button(GTK_DIALOG(d),
                              GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	reply = gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
	if(reply == GTK_RESPONSE_YES)
	    if(!message_postpone(bsmsg))
                reply = GTK_RESPONSE_CANCEL;
	/* cancel action  when reply = "yes" or "no" */
	return (reply != GTK_RESPONSE_YES) && (reply != GTK_RESPONSE_NO);
    }
    return FALSE;
}
static gint
delete_event_cb(GtkWidget * widget, GdkEvent * e, gpointer data)
{
    BalsaSendmsg* bsmsg = data;
    return delete_handler(bsmsg);
}

static void
close_window_cb(GtkWidget * widget, gpointer data)
{
    BalsaSendmsg* bsmsg = data;   
    BALSA_DEBUG_MSG("close_window_cb: start\n");
    if(!delete_handler(bsmsg))
	gtk_widget_destroy(bsmsg->window);
    BALSA_DEBUG_MSG("close_window_cb: end\n");
}

static gint
destroy_event_cb(GtkWidget * widget, gpointer data)
{
    balsa_sendmsg_destroy_handler((BalsaSendmsg *) data);
    return TRUE;
}

/* the balsa_sendmsg destructor; copies first the shown headers setting
   to the balsa_app structure.
*/
static void
balsa_sendmsg_destroy_handler(BalsaSendmsg * bsmsg)
{
    gboolean quit_on_close;

    g_assert(bsmsg != NULL);
    g_assert(ELEMENTS(headerDescs) == ELEMENTS(bsmsg->view_checkitems));

    g_signal_handler_disconnect(G_OBJECT(balsa_app.main_window),
                                bsmsg->delete_sig_id);
    if(balsa_app.debug) g_message("balsa_sendmsg_destroy()_handler: Start.");

    if (bsmsg->orig_message) {
	if (bsmsg->orig_message->mailbox)
	    libbalsa_mailbox_close(bsmsg->orig_message->mailbox,
		    /* Respect pref setting: */
				   balsa_app.expunge_on_close);
	g_object_unref(G_OBJECT(bsmsg->orig_message));
    }

    if (balsa_app.debug)
	printf("balsa_sendmsg_destroy_handler: Freeing bsmsg\n");
    gtk_widget_destroy(bsmsg->window);
    if (bsmsg->bad_address_style)
        g_object_unref(G_OBJECT(bsmsg->bad_address_style));
    quit_on_close = bsmsg->quit_on_close;
    g_free(bsmsg->fcc_url);
    g_free(bsmsg->charset);

    if (bsmsg->spell_checker)
        gtk_widget_destroy(bsmsg->spell_checker);
    if (bsmsg->wrap_timeout_id)
        g_source_remove(bsmsg->wrap_timeout_id);

    g_object_unref(bsmsg->buffer2);

    g_free(bsmsg);

    if (quit_on_close) {
#ifdef BALSA_USE_THREADS
        libbalsa_wait_for_sending_thread(-1);
#endif
	gtk_main_quit();
    }
    if(balsa_app.debug) g_message("balsa_sendmsg_destroy(): Stop.");
}

/* language menu helper functions */
/* find_locale_index_by_locale:
   finds the longest fit so the one who has en_GB will gent en_US if en_GB
   is not defined.
   NOTE: test for the 'C' locale would not be necessary if people set LANG
   instead of LC_ALL. But it is simpler to set it here instead of answering
   the questions (OTOH, I am afraid that people will start claiming "but
   balsa can recognize my language!" on failures in other software.
*/
static unsigned
find_locale_index_by_locale(const gchar * locale)
{
    unsigned i, j, maxfit = 0, maxpos = 0;

    if (!locale || strcmp(locale, "C") == 0)
        locale = "en_US";
    for (i = 0; i < ELEMENTS(locales); i++) {
	for (j = 0; locale[j] && locales[i].locale[j] == locale[j]; j++);
	if (j > maxfit) {
	    maxfit = j;
	    maxpos = i;
	}
    }
    return maxpos;
}

#if !defined(ENABLE_TOUCH_UI)
static struct {
    gchar *label;
    glong struct_offset;
} headers[] = {
    { N_("To:"),       G_STRUCT_OFFSET(BalsaSendmsg, to[1])},
    { N_("From:"),     G_STRUCT_OFFSET(BalsaSendmsg, from[1])},
    { N_("Reply-To:"), G_STRUCT_OFFSET(BalsaSendmsg, reply_to[1])},
    { N_("Bcc:"),      G_STRUCT_OFFSET(BalsaSendmsg, bcc[1])},
    { N_("Cc:"),       G_STRUCT_OFFSET(BalsaSendmsg, cc[1])},
    { N_("Subject:"),  G_STRUCT_OFFSET(BalsaSendmsg, subject[1])}
};

static gboolean
edit_with_gnome_check(gpointer data) {
    FILE *tmp;
    balsa_edit_with_gnome_data *data_real = (balsa_edit_with_gnome_data *)data;
    GtkTextBuffer *buffer;

    pid_t pid;
    gint curposition;
    gchar line[81]; /* FIXME:All lines should wrap at this line */
    /* Editor not ready */
    pid = waitpid (data_real->pid_editor, NULL, WNOHANG);
    if(pid == -1) {
        perror("waitpid");
        return TRUE;
    } else if(pid == 0) return TRUE;
    
    tmp = fopen(data_real->filename, "r");
    if(tmp == NULL){
        perror("fopen");
        return TRUE;
    }
    gdk_threads_enter();
    if (balsa_app.edit_headers) {
        while (fgets(line, sizeof(line), tmp)) {
            guint i;

            if (line[strlen(line) - 1] == '\n')
                line[strlen(line) - 1] = '\0';

            for (i = 0; i < ELEMENTS(headers); i++) {
                gchar *p = _(headers[i].label);
                guint len = strlen(p);

                if (!strncmp(line, p, len)) {
                    GtkWidget *widget =
                        G_STRUCT_MEMBER(GtkWidget *, data_real->bsmsg,
                                        headers[i].struct_offset);
                    gtk_entry_set_text(GTK_ENTRY(widget), line + len + 1);
                    break;
                }
            }
            if (i >= ELEMENTS(headers))
                break;
        }
    }
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data_real->bsmsg->text));

    gtk_text_buffer_set_text(buffer, "", 0);
    curposition = 0;
    while(fgets(line, sizeof(line), tmp))
        libbalsa_insert_with_url(buffer, line, NULL, NULL, NULL);
    g_free(data_real->filename);
    fclose(tmp);
    unlink(data_real->filename);
    gtk_widget_set_sensitive(data_real->bsmsg->text, TRUE);
    g_free(data);
    gdk_threads_leave();

    return FALSE;
}

/* Edit the current file with an external editor.
 *
 * We fork twice current process, so we get:
 *
 * - Old (parent) process (this needs to continue because we don't want 
 *   balsa to 'hang' until the editor exits
 * - New (child) process (forks and waits for child to finish)
 * - New (grandchild) process (executes editor)
 */
static void
edit_with_gnome(GtkWidget* widget, BalsaSendmsg* bsmsg)
{
    static const char TMP_PATTERN[] = "/tmp/balsa-edit-XXXXXX";
    gchar filename[sizeof(TMP_PATTERN)];
    balsa_edit_with_gnome_data *data = 
        g_malloc(sizeof(balsa_edit_with_gnome_data));
    pid_t pid;
    FILE *tmp;
    int tmpfd;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start, end;
    gchar *p;
    GnomeVFSMimeApplication *app;
    char **argv;
    int argc;

    sw_buffer_save(bsmsg);
    strcpy(filename, TMP_PATTERN);
    tmpfd = mkstemp(filename);
    app = gnome_vfs_mime_get_default_application ("text/plain");
    if (app) {
	gboolean adduri = (app->expects_uris ==
                           GNOME_VFS_MIME_APPLICATION_ARGUMENT_TYPE_URIS);
        argc = 2;
        argv = g_new0 (char *, argc+1);
        argv[0] = g_strdup(app->command);
        argv[1] = g_strdup_printf("%s%s", adduri ? "file:" : "", filename);

        /* this does not work really well with gnome-terminal
         * that quits before the text editing application quits.
         * Blame gnome-terminal.
         * WORKAROUND: if the terminal is gnome-terminal, 
         * --disable-factory option is added as well.
         */
        if (app->requires_terminal) {
            gnome_prepend_terminal_to_vector(&argc, &argv);
            if(strstr(argv[0], "gnome-terminal")) {
                int i;
                gchar ** new_argv = g_new(char*, ++argc+1);
                new_argv[0] = argv[0];
                new_argv[1] = g_strdup("--disable-factory");
                for(i=2; i<=argc; i++)
                    new_argv[i] = argv[i-1];
                g_free(argv);
                argv = new_argv;
            }
        }
        gnome_vfs_mime_application_free (app);
    } else {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("Gnome editor is not defined"
                                     " in your preferred applications."));
        return;
    }


    tmp   = fdopen(tmpfd, "w+");
    
    if(balsa_app.edit_headers) {
        guint i;

        for (i = 0; i < ELEMENTS(headers); i++) {
            GtkWidget *widget = G_STRUCT_MEMBER(GtkWidget *, bsmsg,
                                                headers[i].struct_offset);
            const gchar *p = gtk_entry_get_text(GTK_ENTRY(widget));
            fprintf(tmp, "%s %s\n", _(headers[i].label), p);
        }
        fprintf(tmp, "\n");
    }

    gtk_widget_set_sensitive(GTK_WIDGET(bsmsg->text), FALSE);
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    p = gtk_text_iter_get_text(&start, &end);
    fputs(p, tmp);
    g_free(p);
    fclose(tmp);
    if ((pid = fork()) < 0) {
        perror ("fork");
        return; 
    } 
    if (pid == 0) {
        setpgid(0, 0);
        execvp (argv[0], argv); 
        perror ("execvp"); 
        g_strfreev (argv); 
        exit(127);
    }
    g_strfreev (argv); 
    /* Return immediately. We don't want balsa to 'hang' */
    data->pid_editor = pid;
    data->filename = g_strdup(filename);
    data->bsmsg = bsmsg;
    g_timeout_add(200, (GSourceFunc)edit_with_gnome_check, data);
}

#endif /* ENABLE_TOUCH_UI */

static void 
change_identity_dialog_cb(GtkWidget* widget, BalsaSendmsg* bsmsg)
{
    libbalsa_identity_select_dialog(GTK_WINDOW(bsmsg->window),
                                    _("Select Identity"),
                                    balsa_app.identities,
                                    bsmsg->ident,
                                    ((LibBalsaIdentityCallback)
                                     update_bsmsg_identity),
                                    bsmsg);
}


/* NOTE: replace_offset and siglen are  utf-8 character offsets. */
static void
repl_identity_signature(BalsaSendmsg* bsmsg, LibBalsaIdentity* new_ident,
                        LibBalsaIdentity* old_ident, gint* replace_offset, 
                        gint siglen, gchar* new_sig) 
{
    gint newsiglen;
    gboolean reply_type = (bsmsg->type == SEND_REPLY || 
                           bsmsg->type == SEND_REPLY_ALL ||
                           bsmsg->type == SEND_REPLY_GROUP);
    gboolean forward_type = (bsmsg->type == SEND_FORWARD_ATTACH || 
                             bsmsg->type == SEND_FORWARD_INLINE);
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter ins, end;
    
    gtk_text_buffer_get_iter_at_offset(buffer, &ins,
                                       *replace_offset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end,
                                       *replace_offset + siglen);
    gtk_text_buffer_delete(buffer, &ins, &end);

    newsiglen = strlen(new_sig);
    
    /* check to see if this is a reply or forward and compare identity
     * settings to determine whether to add signature */
    if ((reply_type && !new_ident->sig_whenreply) ||
        (forward_type && !new_ident->sig_whenforward)) {
        return;
    } 

    /* see if sig location is probably going to be the same */
    if (new_ident->sig_prepend == old_ident->sig_prepend) {
        /* account for sig length difference in replacement offset */
        *replace_offset += newsiglen - siglen;
    } else if (new_ident->sig_prepend) {
        /* sig location not the same between idents, take a WAG and
         * put it at the start of the message */
        gtk_text_buffer_get_start_iter(buffer, &ins);
        *replace_offset += newsiglen;
    } else {
        /* put it at the end of the message */
        gtk_text_buffer_get_end_iter(buffer, &ins);
    }
    gtk_text_buffer_place_cursor(buffer, &ins);
    libbalsa_insert_with_url(buffer, new_sig, NULL, NULL, NULL);
}


static gchar*
prep_signature(LibBalsaIdentity* ident, gchar* sig)
{
    gchar* sig_tmp;

    /* empty signature is a legal signature */
    if(sig == NULL) return NULL;

    if (ident->sig_separator
        && strncmp(sig, "--\n", 3)
        && strncmp(sig, "-- \n", 4)) {
        sig_tmp = g_strconcat("\n-- \n", sig, NULL);
        g_free(sig);
        sig = sig_tmp;
    } else {
        sig_tmp = g_strconcat("\n", sig, NULL);
        g_free(sig);
        sig = sig_tmp;
    }

    return sig;
}


/*
 * update_bsmsg_identity
 * 
 * Change the specified BalsaSendmsg current identity, and update the
 * corresponding fields. 
 * */
static void
update_bsmsg_identity(BalsaSendmsg* bsmsg, LibBalsaIdentity* ident)
{
    GtkTextBuffer *buffer = 
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start, end;

    gint replace_offset = 0;
    gint siglen;
    gint i = 0;
    
    gboolean found_sig = FALSE;
    gchar* old_sig;
    gchar* new_sig;
    gchar* message_text;
    gchar* compare_str;
    gchar** message_split;
    gchar* tmpstr=libbalsa_address_to_gchar(ident->address, 0);
    const gchar* subject;
    gint replen, fwdlen;
    
    LibBalsaIdentity* old_ident;

    
    g_return_if_fail(ident != NULL);


    /* change entries to reflect new identity */
    gtk_entry_set_text(GTK_ENTRY(bsmsg->from[1]), tmpstr);
    g_free(tmpstr);

#if !defined(ENABLE_TOUCH_UI)
    gtk_entry_set_text(GTK_ENTRY(bsmsg->reply_to[1]), ident->replyto);
#endif

    /* We'll add the auto-bcc for the new identity, but we don't clear
     * any current bcc entries unless it's exactly the auto-bcc for the
     * old identity; this will avoid accumulating duplicates if the user
     * switches identities that have the same auto-bcc, but at the same
     * time will leave any other bcc entries that have been set up;
     * ideally, we might parse the bcc list and remove the old auto-bcc,
     * but that looks like a lot of work...  pb */
    if (bsmsg->ident->bcc &&
	strcmp(gtk_entry_get_text(GTK_ENTRY(bsmsg->bcc[1])),
	       bsmsg->ident->bcc) == 0)
	gtk_entry_set_text(GTK_ENTRY(bsmsg->bcc[1]), "");
    append_comma_separated(GTK_EDITABLE(bsmsg->bcc[1]), ident->bcc);
    
    /* change the subject to use the reply/forward strings */
    subject = gtk_entry_get_text(GTK_ENTRY(bsmsg->subject[1]));

    /*
     * If the subject begins with the old reply string
     *    Then replace it with the new reply string.
     * Else, if the subject begins with the old forward string
     *    Then replace it with the new forward string.
     * Else, if the old reply string was empty, and the message
     *    is a reply, OR the old forward string was empty, and the
     *    message is a forward
     *    Then call set_entry_to_subject()
     * Else assume the user hand edited the subject and does
     *    not want it altered
     */
    old_ident = bsmsg->ident;
    if (((replen = strlen(old_ident->reply_string)) > 0) &&
	(strncmp(subject, old_ident->reply_string, replen) == 0)) {
	tmpstr = g_strconcat(ident->reply_string, &(subject[replen]), NULL);
	gtk_entry_set_text(GTK_ENTRY(bsmsg->subject[1]), tmpstr);
	g_free(tmpstr);
    } else if (((fwdlen = strlen(old_ident->forward_string)) > 0) &&
	       (strncmp(subject, old_ident->forward_string, fwdlen) == 0)) {
	tmpstr = g_strconcat(ident->forward_string, &(subject[fwdlen]), NULL);
	gtk_entry_set_text(GTK_ENTRY(bsmsg->subject[1]), tmpstr);
	g_free(tmpstr);
    } else if (((replen == 0) && (bsmsg->type == SEND_REPLY ||
				  bsmsg->type == SEND_REPLY_ALL ||
				  bsmsg->type == SEND_REPLY_GROUP)) ||
	       ((fwdlen == 0) && (bsmsg->type == SEND_FORWARD_ATTACH ||
				  bsmsg->type == SEND_FORWARD_INLINE))) {
	set_entry_to_subject(GTK_ENTRY(bsmsg->subject[1]), bsmsg->orig_message,
			     bsmsg->type, ident);
    }

    /* -----------------------------------------------------------
     * remove/add the signature depending on the new settings, change
     * the signature if path changed */

    /* reconstruct the old signature to search with */
    old_sig = read_signature(bsmsg);
    old_sig = prep_signature(old_ident, old_sig);

    /* switch identities in bsmsg here so we can use read_signature
     * again */
    bsmsg->ident = ident;
    if ((new_sig = read_signature(bsmsg)) != NULL) {
	SendType type = bsmsg->type;
	gboolean reply_any = (type == SEND_REPLY ||
			      type == SEND_REPLY_ALL ||
			      type == SEND_REPLY_GROUP);
	gboolean forwd_any = (type == SEND_FORWARD_ATTACH ||
			      type == SEND_FORWARD_INLINE);

	if ((reply_any && bsmsg->ident->sig_whenreply)
	    || (forwd_any && bsmsg->ident->sig_whenforward)
	    || (type == SEND_NORMAL && bsmsg->ident->sig_sending))
	    new_sig = prep_signature(ident, new_sig);
	else {
	    g_free(new_sig);
	    new_sig = NULL;
	}
    }
    if(!new_sig) new_sig = g_strdup("");

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    message_text = gtk_text_iter_get_text(&start, &end);
    if (!old_sig) {
        replace_offset = bsmsg->ident->sig_prepend 
            ? 0 : g_utf8_strlen(message_text, -1);
        repl_identity_signature(bsmsg, ident, old_ident, &replace_offset,
                                0, new_sig);
    } else {
        /* split on sig separator */
        message_split = g_strsplit(message_text, "\n-- \n", 0);
        siglen = g_utf8_strlen(old_sig, -1);

	/* check the special case of starting a message with a sig */
	compare_str = g_strconcat("\n", message_split[0], NULL);

	if (g_ascii_strncasecmp(old_sig, compare_str, siglen) == 0) {
	    g_free(compare_str);
	    repl_identity_signature(bsmsg, ident, old_ident,
				    &replace_offset, siglen - 1, new_sig);
	    found_sig = TRUE;
	} else {
	    g_free(compare_str);
	while (message_split[i]) {
		/* put sig separator back to search */
		compare_str = g_strconcat("\n-- \n", message_split[i], NULL);

		/* try to find occurance of old signature */
		if (g_ascii_strncasecmp(old_sig, compare_str, siglen) == 0) {
		    repl_identity_signature(bsmsg, ident, old_ident,
					    &replace_offset, siglen, new_sig);
		    found_sig = TRUE;
		}

		replace_offset +=
		    g_utf8_strlen(i ? compare_str : message_split[i], -1);
		g_free(compare_str);
		i++;
	    }
        }
        /* if no sig seperators found, do a slower brute force approach */
        if (!found_sig) {
            compare_str = message_text;
            replace_offset = 0;

	    /* check the special case of starting a message with a sig */
	    tmpstr = g_strconcat("\n", message_text, NULL);

	    if (g_ascii_strncasecmp(old_sig, tmpstr, siglen) == 0) {
		g_free(tmpstr);
		repl_identity_signature(bsmsg, ident, old_ident,
					&replace_offset, siglen - 1, new_sig);
	    } else {
		g_free(tmpstr);
		replace_offset++;
		compare_str = g_utf8_next_char(compare_str);
		while (*compare_str) {
		    if (g_ascii_strncasecmp(old_sig, compare_str, siglen) == 0) {
			repl_identity_signature(bsmsg, ident, old_ident,
						&replace_offset, siglen, new_sig);
		    }
		    replace_offset++;
		    compare_str = g_utf8_next_char(compare_str);
		}
	    }
        }
        g_strfreev(message_split);
    }
    
#ifdef HAVE_GPGME
    bsmsg_update_gpg_ui_on_ident_change(bsmsg, ident);
#endif

    g_free(old_sig);
    g_free(new_sig);
    g_free(message_text);
}


static void
sw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc)
{
    balsa_app.sw_height = alloc->height;
    balsa_app.sw_width = alloc->width;
}



/* remove_attachment - right mouse button callback */
static void
remove_attachment(GtkWidget * widget, GnomeIconList * ilist)
{
    gint num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(ilist),
                                                 "selectednumbertoremove"));
    gnome_icon_list_remove(ilist, num);
    g_object_set_data(G_OBJECT(ilist), "selectednumbertoremove", NULL);
}


/* ask if an attachment shall be message/external-body */
static gboolean
extbody_dialog_delete(GtkDialog * dialog)
{
    GnomeIconList *ilist = 
	GNOME_ICON_LIST(g_object_get_data(G_OBJECT(dialog), "balsa-data"));
    g_object_set_data(G_OBJECT(ilist), "selectednumbertoextbody", NULL);
    gtk_widget_destroy(GTK_WIDGET(dialog));

    return TRUE;
}

static void
no_change_to_extbody(GtkDialog * dialog)
{
    GnomeIconList *ilist = 
	GNOME_ICON_LIST(g_object_get_data(G_OBJECT(dialog), "balsa-data"));
    g_object_set_data(G_OBJECT(ilist), "selectednumbertoextbody", NULL);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}


static void
add_extbody_attachment(GnomeIconList *ilist, 
		       const gchar *name, const gchar *mime_type,
		       gboolean delete_on_destroy, gboolean is_url) {
    gchar *pix;
    gchar *label;
    attachment_t *attach;
    gint pos;

    g_return_if_fail(name != NULL); 
    
    attach = g_malloc(sizeof(attachment_t));
    if (is_url) 
	attach->filename = g_strdup_printf("URL %s", name);
    else
	attach->filename = g_strdup(name);
    attach->force_mime_type = mime_type != NULL ? g_strdup(mime_type) : NULL;
    attach->delete_on_destroy = delete_on_destroy;
    attach->as_extbody = TRUE;

    pix = libbalsa_icon_finder("message/external-body", attach->filename,NULL);
    label = g_strdup_printf ("%s (%s)", attach->filename, 
			     "message/external-body");
    pos = gnome_icon_list_append(ilist, pix, label);
    gnome_icon_list_set_icon_data_full(ilist, pos, attach, destroy_attachment);
    g_free(pix);
    g_free(label);
}


/* send attachment as external body - right mouse button callback */
static void
extbody_attachment(GtkDialog * dialog)
{
    GnomeIconList *ilist;
    gint num;
    attachment_t *oldattach;

    ilist = 
	GNOME_ICON_LIST(g_object_get_data(G_OBJECT(dialog), "balsa-data"));
    num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(ilist),
                                            "selectednumbertoextbody"));
    oldattach = 
	(attachment_t *)gnome_icon_list_get_icon_data(ilist, num);
    g_return_if_fail(oldattach);
    g_object_set_data(G_OBJECT(ilist), "selectednumbertoextbody", NULL);
    gtk_widget_destroy(GTK_WIDGET(dialog));

    /* remove the selected element and replace it */
    gnome_icon_list_freeze(ilist);
    add_extbody_attachment(ilist, oldattach->filename, 
			   oldattach->force_mime_type, 
			   oldattach->delete_on_destroy, FALSE);
    gnome_icon_list_remove(ilist, num);
    gnome_icon_list_thaw(ilist);
}


static void
show_extbody_dialog(GtkWidget *widget, GnomeIconList *ilist)
{
    GtkWidget *extbody_dialog;
    GtkWidget *dialog_vbox, *parent;
    GtkWidget *hbox;
    gint num;
    attachment_t *attach;
    
    parent = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

    num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(ilist),
                                            "selectednumbertoextbody"));
    attach = gnome_icon_list_get_icon_data(ilist, num);
    extbody_dialog =
        gtk_message_dialog_new(GTK_WINDOW(parent),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("Saying yes will not send the file "
                                 "`%s' itself, but just a MIME "
                                 "message/external-body reference.  "
                                 "Note that the recipient must "
                                 "have proper permissions to see the "
                                 "`real' file.\n\n"
                                 "Do you really want to attach "
                                 "this file as reference?"),
                               attach->filename);

    gtk_window_set_title(GTK_WINDOW(extbody_dialog),
                         _("Attach as Reference?"));
    g_object_set_data(G_OBJECT(extbody_dialog), "balsa-data", ilist);
    
    dialog_vbox = GTK_DIALOG (extbody_dialog)->vbox;
    
    hbox = gtk_hbox_new (FALSE, 10);
    gtk_box_pack_start (GTK_BOX (dialog_vbox), hbox, TRUE, TRUE, 0);
    
    g_signal_connect(G_OBJECT(extbody_dialog), "response",
                     G_CALLBACK(response_cb), extbody_dialog);
    
    gtk_widget_show_all(extbody_dialog);
}

static void
response_cb(GtkDialog * dialog, gint response, gpointer data)
{
    switch (response) {
    case GTK_RESPONSE_NO:
        no_change_to_extbody(dialog);
        break;
    case GTK_RESPONSE_YES:
        extbody_attachment(dialog);
        break;
    default:
        extbody_dialog_delete(dialog);
        break;
    }
}

/* send attachment as "real" file - right mouse button callback */
static void
file_attachment(GtkWidget * widget, GnomeIconList * ilist)
{
    gint num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(ilist),
                                                 "selectednumbertofile"));
    attachment_t *attach, *oldattach;
    gchar *pix, *label;
    gchar *content_type;

    oldattach = 
	(attachment_t *)gnome_icon_list_get_icon_data(ilist, num);
    g_return_if_fail(oldattach);
    g_object_set_data(G_OBJECT(ilist), "selectednumbertofile", NULL);

    /* remove the selected element and replace it */
    gnome_icon_list_freeze(ilist);
    attach = g_malloc(sizeof(attachment_t));
    attach->filename = oldattach->filename ? 
	g_strdup(oldattach->filename) : NULL;
    attach->force_mime_type = oldattach->force_mime_type ? 
	g_strdup(attach->force_mime_type) : NULL;
    attach->delete_on_destroy = oldattach->delete_on_destroy;
    attach->as_extbody = FALSE;
    gnome_icon_list_remove(ilist, num);
    
    /* as this worked before, don't do too much (==any) error checking... */
    pix = libbalsa_icon_finder(attach->force_mime_type, attach->filename,
                               &content_type);
    {   /* scope */
        gchar *tmp = g_path_get_basename(attach->filename);
        label = g_strdup_printf("%s (%s)", tmp, content_type); 
        g_free(tmp);
    }
    gnome_icon_list_insert(ilist, num, pix, label);
    gnome_icon_list_set_icon_data_full(ilist, num, attach, destroy_attachment);
    g_free(label);
    g_free(pix);
    g_free(content_type);
    gnome_icon_list_thaw(ilist);
}

/* the menu is created on right-button click on an attachement */
static gboolean
sw_do_popup(GnomeIconList * ilist, GdkEventButton * event)
{
    GList *list;
    gint num;
    attachment_t *attach;
    GtkWidget *menu, *menuitem;
    gint event_button;
    guint event_time;

    if (!(list = gnome_icon_list_get_selection(ilist)))
        return FALSE;

    num = GPOINTER_TO_INT(list->data);
    attach = (attachment_t *)gnome_icon_list_get_icon_data(ilist, num);

    menu = gtk_menu_new();
    menuitem = gtk_menu_item_new_with_label(_("Remove"));
    g_object_set_data(G_OBJECT(ilist), "selectednumbertoremove",
                      GINT_TO_POINTER(num));
    g_signal_connect(G_OBJECT(menuitem), "activate",
		     G_CALLBACK(remove_attachment), ilist);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show(menuitem);

    /* a "real" (not temporary) file can be attached as external body */
    if (!attach->delete_on_destroy) {
	if (!attach->as_extbody) {
	    menuitem = gtk_menu_item_new_with_label(_("attach as reference"));
	    g_object_set_data(G_OBJECT(ilist), "selectednumbertoextbody",
                              GINT_TO_POINTER(num));
	    g_signal_connect(G_OBJECT(menuitem), "activate",
	    		     G_CALLBACK(show_extbody_dialog), ilist);
	} else {
	    menuitem = gtk_menu_item_new_with_label(_("attach as file"));
	    g_object_set_data(G_OBJECT(ilist), "selectednumbertofile",
                              GINT_TO_POINTER(num));
	    g_signal_connect(G_OBJECT(menuitem), "activate",
	    		     G_CALLBACK(file_attachment), ilist);
	}
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	gtk_widget_show(menuitem);
    }

    if (event) {
        event_button = event->button;
        event_time = event->time;
    } else {
        event_button = 0;
        event_time = gtk_get_current_event_time();
    }
    g_object_ref(menu);
    gtk_object_sink(GTK_OBJECT(menu));
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                   event_button, event_time);
    g_object_unref(menu);
    return TRUE;
}

/* select_icon --------------------------------------------------------------
   This signal is emitted when an icon becomes selected. If the event
   argument is NULL, then it means the icon became selected due to a
   range or rubberband selection. If it is non-NULL, it means the icon
   became selected due to an user-initiated event such as a mouse button
   press. The event can be examined to get this information.
*/

static void
show_attachment_widget(BalsaSendmsg *bsmsg)
{
    int pos;
    for(pos=0; pos<4; pos++)
        gtk_widget_show_all(GTK_WIDGET(bsmsg->attachments[pos]));
}

static void
hide_attachment_widget(BalsaSendmsg *bsmsg)
{
    int pos;
    for(pos=0; pos<4; pos++)
        gtk_widget_hide(GTK_WIDGET(bsmsg->attachments[pos]));
}

static void
select_attachment(GnomeIconList * ilist, gint num, GdkEventButton * event,
		  gpointer data)
{
    if (event == NULL)
	return;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3)
        sw_do_popup(ilist, event);
}

/* sw_popup_menu_cb:
 * callback for the "popup-menu" signal, which is issued when the user
 * hits shift-F10
 */
static gboolean
sw_popup_menu_cb(GtkWidget * widget, gpointer data)
{
    return sw_do_popup(GNOME_ICON_LIST(widget), NULL);
}

static void
destroy_attachment (gpointer data)
{
    attachment_t *attach = (attachment_t *)data;

    /* unlink the file if necessary */
    if (attach->delete_on_destroy) {
	char *last_slash = strrchr(attach->filename, '/');

	if (balsa_app.debug)
	    fprintf (stderr, "%s:%s: unlink `%s'\n", __FILE__, __FUNCTION__,
		     attach->filename);
	unlink(attach->filename);
	*last_slash = 0;
	if (balsa_app.debug)
	    fprintf (stderr, "%s:%s: rmdir `%s'\n", __FILE__, __FUNCTION__,
		     attach->filename);
	rmdir(attach->filename);
    }
    /* clean up memory */
    g_free(attach->filename);
    g_free(attach->force_mime_type);
    g_free(attach);
}

/* add_attachment:
   adds given filename to the list.
   takes over the ownership of filename.
*/
gboolean
add_attachment(BalsaSendmsg * bsmsg, char *filename, 
               gboolean is_a_temp_file, const gchar *forced_mime_type)
{
    GnomeIconList *iconlist = GNOME_ICON_LIST(bsmsg->attachments[1]);
    gchar *content_type = NULL;
    gchar *pix, *err_bsmsg;

    if (balsa_app.debug)
	fprintf(stderr, "Trying to attach '%s'\n", filename);
    if ( (err_bsmsg=check_if_regular_file(filename)) != NULL) {
        balsa_information(LIBBALSA_INFORMATION_ERROR, err_bsmsg);
	g_free(err_bsmsg);
        g_free(content_type);
	return FALSE;
    }

#if defined(ENABLE_TOUCH_UI)
    if(!bsmsg_check_format_compatibility(GTK_WINDOW(bsmsg->window), filename))
        return FALSE;
#endif /* ENABLE_TOUCH_UI */

    pix = libbalsa_icon_finder(forced_mime_type, filename, &content_type);

    {   /* scope */
	gint pos;
	gchar *label;
	attachment_t *attach_data = g_malloc(sizeof(attachment_t));
        gchar *utf8name;
        gchar *basename;
        GError *err = NULL;

        basename = g_path_get_basename(filename);
        utf8name = g_filename_to_utf8(basename, -1, NULL, NULL, &err);
        if (err) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
		    _("Error converting \"%s\" to UTF-8: %s\n"),
                    basename, err->message);
            g_error_free(err);
        }

        label = g_strdup_printf("%s (%s)", 
                                utf8name ? utf8name : basename,
                                content_type);

	pos = gnome_icon_list_append(iconlist, pix, label);
	attach_data->filename = filename;
	attach_data->force_mime_type = forced_mime_type 
	    ? g_strdup(forced_mime_type): NULL;

	attach_data->delete_on_destroy = is_a_temp_file;
	attach_data->as_extbody = FALSE;
	/* we should be smarter about this .. */
	gnome_icon_list_set_icon_data_full(iconlist, pos, attach_data, destroy_attachment);

        g_free(basename);
        g_free(utf8name);
	g_free(label);
    }
    show_attachment_widget(bsmsg);

    g_free(pix);
    g_free(content_type);
    return TRUE;
}

static gchar* 
check_if_regular_file(const gchar * filename)
{
    struct stat s;
    gchar *ptr = NULL;

    if (stat(filename, &s))
	ptr = g_strdup_printf(_("Cannot get info on file '%s': %s"),
			      filename, strerror(errno));
    else if (!S_ISREG(s.st_mode))
	ptr =
	    g_strdup_printf(
		_("Attachment %s is not a regular file."), filename);
    else if(access(filename, R_OK) != 0) {
	ptr =
	    g_strdup_printf(_("File %s cannot be read\n"), filename);
    }
    return ptr;
}

/* attach_dialog_ok:
   processes the attachment file selection. Adds them to the list,
   showing the attachment list, if was hidden.
*/
static void
attach_dialog_ok(GtkWidget * widget, gpointer data)
{
    GtkFileSelection *fs;
    BalsaSendmsg *bsmsg;
    gchar **files;
    gchar **tmp;
    int res = 0;

    fs = GTK_FILE_SELECTION(data);
    bsmsg = g_object_get_data(G_OBJECT(fs), "balsa-data");

    files = gtk_file_selection_get_selections(fs);
    for (tmp = files; *tmp; ++tmp)
        if(!add_attachment(bsmsg, g_strdup(*tmp), FALSE, NULL)) res++;

    g_strfreev(files);
    
    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir =
        g_path_get_dirname(gtk_file_selection_get_filename(fs));

    if(res==0) gtk_widget_destroy(GTK_WIDGET(fs));
}

/* attach_clicked - menu and toolbar callback */
static void
attach_clicked(GtkWidget * widget, gpointer data)
{
    GtkWidget *fsw;
    GtkFileSelection *fs;
    BalsaSendmsg *bsmsg;

    bsmsg = data;

    fsw = gtk_file_selection_new(_("Attach file"));
#if 0
    /* start workaround for prematurely realized widget returned
     * by some GTK+ versions */
    if(GTK_WIDGET_REALIZED(fsw))
        gtk_widget_unrealize(fsw);
    /* end workaround for prematurely realized widget */
#endif
    gtk_window_set_wmclass(GTK_WINDOW(fsw), "file", "Balsa");
    g_object_set_data(G_OBJECT(fsw), "balsa-data", bsmsg);

    fs = GTK_FILE_SELECTION(fsw);
    gtk_file_selection_set_select_multiple(fs, TRUE);
    if (balsa_app.attach_dir) {
        gchar* tmp = g_strconcat(balsa_app.attach_dir, "/", NULL);
	gtk_file_selection_set_filename(fs, tmp);
        g_free(tmp);
    }

    g_signal_connect(G_OBJECT(fs->ok_button), "clicked",
		     G_CALLBACK(attach_dialog_ok), fs);
    g_signal_connect_swapped(G_OBJECT(fs->cancel_button), "clicked",
			     G_CALLBACK(gtk_widget_destroy),
                             GTK_OBJECT(fsw));

    gtk_widget_show(fsw);
}

/* attach_message:
   returns TRUE on success, FALSE on failure.
*/
static gboolean 
attach_message(BalsaSendmsg *bsmsg, LibBalsaMessage *message)
{
    gchar *name, *tmp_file_name;
	
    if (libbalsa_mktempdir(&tmp_file_name) == FALSE)
	return FALSE;
    name = g_strdup_printf("%s/forwarded-message", tmp_file_name);
    g_free(tmp_file_name);

    if(!libbalsa_message_save(message, name)) {
        g_free(name);
        return FALSE;
    }
    add_attachment(bsmsg, name,
		   TRUE, "message/rfc822");
    return TRUE;
}

static gint
insert_selected_messages(BalsaSendmsg *bsmsg, SendType type)
{
    GtkTextBuffer *buffer = 
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkWidget *index =
	balsa_window_find_current_index(balsa_app.main_window);
    GList *l;
    
    if (index && (l = balsa_index_selected_list(BALSA_INDEX(index)))) {
	GList *node;
    
        sw_buffer_save(bsmsg);
	for (node = l; node; node = g_list_next(node)) {
	    LibBalsaMessage *message = node->data;
	    GString *body = quoteBody(bsmsg, message, type);
	    
            libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
	    g_string_free(body, TRUE);
	}
	g_list_foreach(l, (GFunc)g_object_unref, NULL);
        g_list_free(l);
    }

    return TRUE;
}

static gint include_message_cb(GtkWidget *widget, BalsaSendmsg *bsmsg)
{
    return insert_selected_messages(bsmsg, SEND_FORWARD_INLINE);
}


static gint
attach_message_cb(GtkWidget * widget, BalsaSendmsg *bsmsg) 
{
    GtkWidget *index =
	balsa_window_find_current_index(balsa_app.main_window);
    
    if (index) {
	GList *node, *l = balsa_index_selected_list(BALSA_INDEX(index));
    
	for (node = l; node; node = g_list_next(node)) {
	    LibBalsaMessage *message = node->data;

	    if(!attach_message(bsmsg, message)) {
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Attaching message failed.\n"
                                       "Possible reason: not enough temporary space"));
                break;
            }
	}
	g_list_foreach(l, (GFunc)g_object_unref, NULL);
        g_list_free(l);
    }
    
    return TRUE;
}


#if 0
static gint include_messages_cb(GtkWidget *widget, BalsaSendmsg *bsmsg)
{
    return insert_selected_messages(bsmsg, SEND_FORWARD_INLINE);
}
#endif /* 0 */

/* attachments_add - attachments field D&D callback */
static GSList*
uri2gslist(const char *uri_list)
{
  GSList *list = NULL;

  while (*uri_list) {
    char	*linebreak = strchr(uri_list, 13);
    char	*uri;
    int	length;
    
    if (!linebreak || linebreak[1] != '\n')
        return list;
    
    length = linebreak - uri_list;

    if (length && uri_list[0] != '#' && strncmp(uri_list,"file://",7)==0) {
	uri = g_strndup(uri_list+7, length-7);
	list = g_slist_append(list, uri);
      }

    uri_list = linebreak + 2;
  }
  return list;
}

static void
attachments_add(GtkWidget * widget,
		GdkDragContext * context,
		gint x,
		gint y,
		GtkSelectionData * selection_data,
		guint info, guint32 time, BalsaSendmsg * bsmsg)
{
    if (balsa_app.debug)
        printf("attachments_add: info %d\n", info);
    if (info == TARGET_MESSAGES) {
        LibBalsaMessage **message_array =
            (LibBalsaMessage **) selection_data->data;

        while (*message_array) {
            if(!attach_message(bsmsg, *message_array++))
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Attaching message failed.\n"
                                       "Possible reason: not enough temporary space"));
        }
    } else if (info == TARGET_URI_LIST) {
        GSList *uri_list = uri2gslist(selection_data->data);
        for (; uri_list; uri_list = g_slist_next(uri_list)) {
	    add_attachment(bsmsg,
			   uri_list->data, FALSE, NULL); /* steal strings */
        }
        g_slist_free(uri_list);
    } else if( info == TARGET_STRING) {
	add_extbody_attachment( GNOME_ICON_LIST(bsmsg->attachments[1]),
				selection_data->data, "text/html", FALSE, TRUE);
    }	
    gtk_drag_finish(context, TRUE, FALSE, time);
}

/* to_add - e-mail (To, From, Cc, Bcc) field D&D callback */
static void
to_add(GtkWidget * widget,
       GdkDragContext * context,
       gint x,
       gint y,
       GtkSelectionData * selection_data,
       guint info, guint32 time, GnomeIconList * iconlist)
{
    append_comma_separated(GTK_EDITABLE(widget), selection_data->data);
    gtk_drag_finish(context, TRUE, FALSE, time);
}

/*
 * static void create_email_or_string_entry()
 * 
 * Creates a gtk_label()/entry pair.
 *
 * Input: GtkWidget* table       - Table to attach to.
 *        const gchar* label     - Label string.
 *        int y_pos              - position in the table.
 *        arr                    - arr[1] is the entry widget.
 *      
 * Output: GtkWidget* arr[] - arr[0] will be the label widget.
 */
static void
create_email_or_string_entry(GtkWidget * table, const gchar * label,
                             int y_pos, GtkWidget * arr[])
{
    PangoFontDescription *desc;

    arr[0] = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(arr[0]), arr[1]);
    gtk_misc_set_alignment(GTK_MISC(arr[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(arr[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), arr[0], 0, 1, y_pos, y_pos + 1,
		     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

    desc = pango_font_description_from_string(balsa_app.message_font);
    gtk_widget_modify_font(arr[1], desc);
    pango_font_description_free(desc);

    gtk_table_attach(GTK_TABLE(table), arr[1], 1, 2, y_pos, y_pos + 1,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 0, 0);
}


/*
 * static void create_string_entry()
 * 
 * Creates a gtk_label()/gtk_entry() pair.
 *
 * Input: GtkWidget* table       - Table to attach to.
 *        const gchar* label     - Label string.
 *        int y_pos              - position in the table.
 *      
 * Output: GtkWidget* arr[] - arr[0] will be the label widget.
 *                          - arr[1] will be the entry widget.
 */
static void
create_string_entry(GtkWidget * table, const gchar * label, int y_pos,
                    GtkWidget * arr[])
{
    arr[1] = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(arr[1]), 2048);
    create_email_or_string_entry(table, label, y_pos, arr);
}

/*
 * static void create_email_entry()
 *
 * Creates a gtk_label()/libbalsa_address_entry() and button in a table for
 * e-mail entries, eg. To:.  It also sets up some callbacks in gtk.
 *
 * Input:  GtkWidget *table   - table to insert the widgets into.
 *         const gchar *label - label to use.
 *         int y_pos          - How far down in the table to put label.
 *         const gchar *icon  - icon for the button.
 *         BalsaSendmsg *bsmsg  - The send message window
 *         gint min_addresses - The minimum acceptable number of
 *                              addresses.
 *         gint max_addresses - If not -1, the maximum acceptable number
 *                              of addresses.
 * 
 * Output: GtkWidget *arr[]   - An array of GtkWidgets, as follows:
 *            arr[0]          - the label.
 *            arr[1]          - the entrybox.
 *            arr[2]          - the button.
 *         BalsaSendmsgAddress *sma
 *                            - a structure with info about the
 *                              address, passed to the "changed" signal
 *                              callback.
 */
static void
create_email_entry(GtkWidget * table, const gchar * label, int y_pos,
		   const gchar * icon, BalsaSendmsg *bsmsg, GtkWidget * arr[],
                   BalsaSendmsgAddress *sma, gint min_addresses,
                   gint max_addresses)
{
    arr[1] = libbalsa_address_entry_new();
    create_email_or_string_entry(table, label, y_pos, arr);

    arr[2] = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(arr[2]), GTK_RELIEF_NONE);
    GTK_WIDGET_UNSET_FLAGS(arr[2], GTK_CAN_FOCUS);
    gtk_container_add(GTK_CONTAINER(arr[2]),
		      gtk_image_new_from_stock(icon,
                                               GTK_ICON_SIZE_BUTTON));
    gtk_table_attach(GTK_TABLE(table), arr[2], 2, 3, y_pos, y_pos + 1,
		     0, 0, 0, 0);

    g_signal_connect(G_OBJECT(arr[2]), "clicked",
		     G_CALLBACK(address_book_cb), bsmsg);
    g_object_set_data(G_OBJECT(arr[2]), "address-entry-widget", arr[1]);
    g_signal_connect(G_OBJECT(arr[1]), "drag_data_received",
		     G_CALLBACK(to_add), NULL);
    gtk_drag_dest_set(GTK_WIDGET(arr[1]), GTK_DEST_DEFAULT_ALL,
		      email_field_drop_types,
		      ELEMENTS(email_field_drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    libbalsa_address_entry_set_find_match(LIBBALSA_ADDRESS_ENTRY(arr[1]),
		       expand_alias_find_match);
    libbalsa_address_entry_set_domain(LIBBALSA_ADDRESS_ENTRY(arr[1]),
		       bsmsg->ident->domain);
    g_signal_connect(G_OBJECT(arr[1]), "changed",
                     G_CALLBACK(address_changed_cb), sma);

    if (!bsmsg->bad_address_style) {
        /* set up the style for flagging bad/incomplete addresses */
        GdkColor color = balsa_app.bad_address_color;

        bsmsg->bad_address_style =
            gtk_style_copy(gtk_widget_get_style(GTK_WIDGET(arr[0])));

        if (gdk_colormap_alloc_color(balsa_app.colormap, &color, FALSE, TRUE)) {
            bsmsg->bad_address_style->fg[GTK_STATE_NORMAL] = color;
        } else {
            fprintf(stderr, "Couldn't allocate bad address color!\n");
            fprintf(stderr, " red: %04x; green: %04x; blue: %04x.\n",
               color.red, color.green, color.blue);
        }
    }

    /* populate the info structure: */
    sma->bsmsg = bsmsg;
    sma->label = arr[0];
    sma->min_addresses = min_addresses;
    sma->max_addresses = max_addresses;
    sma->ready = TRUE;

    /* set initial label style: */
    set_ready(LIBBALSA_ADDRESS_ENTRY(arr[1]), sma);
}



/* create_info_pane 
   creates upper panel with the message headers: From, To, ... and 
   returns it.
*/
static GtkWidget *
create_info_pane(BalsaSendmsg * bsmsg, SendType type)
{
    GtkWidget *sw;
    GtkWidget *table;
    GtkWidget *frame;
    GtkWidget *align;

    bsmsg->header_table = table = gtk_table_new(11, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);

    /* bsmsg->bad_address_style will be set in create_email_entry: */
    bsmsg->bad_address_style = NULL;

    /* From: */
    create_email_entry(table, _("F_rom:"), 0, GNOME_STOCK_BOOK_BLUE,
                       bsmsg, bsmsg->from,
                       &bsmsg->from_info, 1, 1);

    /* To: */
    create_email_entry(table, _("_To:"), 1, GNOME_STOCK_BOOK_RED,
                       bsmsg, bsmsg->to,
                       &bsmsg->to_info, 1, -1);
    g_signal_connect_swapped(G_OBJECT(bsmsg->to[1]), "changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);

    /* Subject: */
    create_string_entry(table, _("S_ubject:"), 2, bsmsg->subject);
    g_signal_connect_swapped(G_OBJECT(bsmsg->subject[1]), "changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);
    /* cc: */
    create_email_entry(table, _("Cc:"), 3, GNOME_STOCK_BOOK_YELLOW,
                       bsmsg, bsmsg->cc,
                       &bsmsg->cc_info, 0, -1);

    /* bcc: */
    create_email_entry(table, _("Bcc:"), 4, GNOME_STOCK_BOOK_GREEN,
                       bsmsg, bsmsg->bcc,
                       &bsmsg->bcc_info, 0, -1);

    /* fcc: mailbox folder where the message copy will be written to */
    bsmsg->fcc[0] = gtk_label_new_with_mnemonic(_("F_cc:"));
    gtk_misc_set_alignment(GTK_MISC(bsmsg->fcc[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(bsmsg->fcc[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), bsmsg->fcc[0], 0, 1, 5, 6, GTK_FILL,
		     GTK_FILL | GTK_SHRINK, 0, 0);

    if (!balsa_app.fcc_mru)
        balsa_mblist_mru_add(&balsa_app.fcc_mru, balsa_app.sentbox->url);
    balsa_mblist_mru_add(&balsa_app.fcc_mru, "");
    if (balsa_app.copy_to_sentbox) {
        /* move the NULL option to the bottom */
        balsa_app.fcc_mru = g_list_reverse(balsa_app.fcc_mru);
        balsa_mblist_mru_add(&balsa_app.fcc_mru, "");
        balsa_app.fcc_mru = g_list_reverse(balsa_app.fcc_mru);
    }
    if (type == SEND_CONTINUE && bsmsg->orig_message->headers &&
	bsmsg->orig_message->headers->fcc_url)
        balsa_mblist_mru_add(&balsa_app.fcc_mru,
                             bsmsg->orig_message->headers->fcc_url);
    bsmsg->fcc[1] =
        balsa_mblist_mru_option_menu(GTK_WINDOW(bsmsg->window),
                                     &balsa_app.fcc_mru);
    gtk_label_set_mnemonic_widget(GTK_LABEL(bsmsg->fcc[0]), bsmsg->fcc[1]);
    align = gtk_alignment_new(0, 0.5, 0, 1);
    gtk_container_add(GTK_CONTAINER(align), bsmsg->fcc[1]);
    gtk_table_attach(GTK_TABLE(table), align, 1, 3, 5, 6,
		     GTK_FILL, GTK_FILL, 0, 0);

#if !defined(ENABLE_TOUCH_UI)
    /* Reply To: */
    create_email_entry(table, _("_Reply To:"), 6, GNOME_STOCK_BOOK_BLUE,
                       bsmsg, bsmsg->reply_to,
                       &bsmsg->reply_to_info, 0, -1);
#endif /* ENABLE_TOUCH_UI */
    /* Attachment list */
    bsmsg->attachments[0] = gtk_label_new_with_mnemonic(_("_Attachments:"));
    gtk_misc_set_alignment(GTK_MISC(bsmsg->attachments[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(bsmsg->attachments[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), bsmsg->attachments[0], 0, 1, 7, 8,
		     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

    /* create icon list */
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

    bsmsg->attachments[1] = gnome_icon_list_new(100, NULL, FALSE);
    g_signal_connect(G_OBJECT(bsmsg->window), "drag_data_received",
		     G_CALLBACK(attachments_add), bsmsg);
    gtk_drag_dest_set(GTK_WIDGET(bsmsg->window), GTK_DEST_DEFAULT_ALL,
		      drop_types, ELEMENTS(drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    gtk_widget_set_size_request(bsmsg->attachments[1], -1, 100);

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), bsmsg->attachments[1]);
    gtk_container_add(GTK_CONTAINER(frame), sw);

    gtk_table_attach(GTK_TABLE(table), frame, 1, 3, 7, 8,
		     GTK_FILL | GTK_EXPAND,
		     GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 0);

    g_signal_connect(G_OBJECT(bsmsg->attachments[1]), "select_icon",
		     G_CALLBACK(select_attachment), NULL);
    g_signal_connect(G_OBJECT(bsmsg->attachments[1]), "popup-menu",
                     G_CALLBACK(sw_popup_menu_cb), NULL);

    gnome_icon_list_set_selection_mode(GNOME_ICON_LIST
				       (bsmsg->attachments[1]),
				       GTK_SELECTION_MULTIPLE);
    GTK_WIDGET_SET_FLAGS(GNOME_ICON_LIST(bsmsg->attachments[1]),
			 GTK_CAN_FOCUS);

    bsmsg->attachments[2] = sw;
    bsmsg->attachments[3] = frame;

    gtk_widget_show_all(table);
    hide_attachment_widget(bsmsg);
    return table;
}

static gboolean
has_file_attached(BalsaSendmsg *bsmsg, const char *fname)
{
    guint i, n =
        gnome_icon_list_get_num_icons(GNOME_ICON_LIST
                                      (bsmsg->attachments[1]));
    
    for (i = 0; i<n; i++) {
        attachment_t *attach;
        
        attach = (attachment_t *)
            gnome_icon_list_get_icon_data(GNOME_ICON_LIST
                                          (bsmsg->attachments[1]), i);
        if(strcmp(attach->filename, fname) == 0)
            return TRUE;
    }
    return FALSE;
}

/* drag_data_quote - text area D&D callback */
static void
drag_data_quote(GtkWidget * widget,
                GdkDragContext * context,
                gint x,
                gint y,
                GtkSelectionData * selection_data,
                guint info, guint32 time, BalsaSendmsg * bsmsg)
{
    LibBalsaMessage **message_array;
    GtkTextBuffer *buffer;
    if (context->action == GDK_ACTION_ASK)
        context->action = GDK_ACTION_COPY;

    switch(info) {
    case TARGET_MESSAGES:
        message_array = (LibBalsaMessage **) selection_data->data;
        buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        
        while (*message_array) {
            GString *body = quoteBody(bsmsg, *message_array++, SEND_REPLY);
            libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
            g_string_free(body, TRUE);
        }
        break;
    case TARGET_URI_LIST: {
        GSList *uri_list = uri2gslist(selection_data->data);
        for (; uri_list; uri_list = g_slist_next(uri_list)) {
            /* Since current GtkTextView gets this signal twice for
             * every action (#150141) we need to check for duplicates,
             * which is a good idea anyway. */
            if(!has_file_attached(bsmsg, uri_list->data))
                add_attachment(bsmsg,  /* steal strings */
                               uri_list->data, FALSE, NULL);
        }
        g_slist_free(uri_list);
    }
        break;
    case TARGET_EMAIL:
    case TARGET_STRING: /* perhaps we should allow dropping in these, too? */
    default: return;
    }
    gtk_drag_finish(context, TRUE, FALSE, time);
}

/* create_text_area 
   Creates the text entry part of the compose window.
*/
static GtkWidget *
create_text_area(BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view;
    PangoFontDescription *desc;
    GtkTextBuffer *buffer;
    GtkWidget *table;

    bsmsg->text = gtk_text_view_new();
    text_view = GTK_TEXT_VIEW(bsmsg->text);
    gtk_text_view_set_left_margin(text_view, 2);
    gtk_text_view_set_right_margin(text_view, 2);

    /* set the message font */
    desc = pango_font_description_from_string(balsa_app.message_font);
    gtk_widget_modify_font(bsmsg->text, desc);
    pango_font_description_free(desc);

    buffer = gtk_text_view_get_buffer(text_view);
    bsmsg->buffer2 =
         gtk_text_buffer_new(gtk_text_buffer_get_tag_table(buffer));
    gtk_text_buffer_create_tag(buffer, "soft", NULL);
    gtk_text_buffer_create_tag(buffer, "url", NULL);
    gtk_text_view_set_editable(text_view, TRUE);
#if GTK_CHECK_VERSION(2, 4, 0)
    gtk_text_view_set_wrap_mode(text_view, GTK_WRAP_WORD_CHAR);
#else
    gtk_text_view_set_wrap_mode(text_view, GTK_WRAP_WORD);
#endif

    table = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(table),
    				   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(table), bsmsg->text);
    g_signal_connect(G_OBJECT(bsmsg->text), "drag_data_received",
		     G_CALLBACK(drag_data_quote), bsmsg);
    /* GTK_DEST_DEFAULT_ALL in drag_set would trigger bug 150141 */
    gtk_drag_dest_set(GTK_WIDGET(bsmsg->text), 0,
		      drop_types, ELEMENTS(drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    gtk_widget_show_all(GTK_WIDGET(table));

    return table;
}

/* continueBody ---------------------------------------------------------
   a short-circuit procedure for the 'Continue action'
   basically copies the first text/plain part over to the entry field.
   Attachments (if any) are saved temporarily in subfolders to preserve
   their original names and then attached again.
   NOTE that rbdy == NULL if message has no text parts.
*/
static void
continueBody(BalsaSendmsg * bsmsg, LibBalsaMessage * message)
{
    LibBalsaMessageBody *body;

    body = message->body_list;
    if (body) {
	if (libbalsa_message_body_type(body) == LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART)
	    body = body->parts;
	/* if the first part is of type text/plain with a NULL filename, it
	   was the message... */
	if (body && !body->filename) {
	    GString *rbdy;
	    gchar *body_type = libbalsa_message_body_get_mime_type(body);
            gint llen = -1;
            GtkTextBuffer *buffer =
                gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

            if (bsmsg->flow && libbalsa_message_body_is_flowed(body))
                llen = balsa_app.wraplength;
	    if (!strcmp(body_type, "text/plain") &&
		(rbdy = process_mime_part(message, body, NULL, llen, FALSE,
                                          bsmsg->flow))) {
                libbalsa_insert_with_url(buffer, rbdy->str, NULL, NULL, NULL);
		g_string_free(rbdy, TRUE);
	    }
	    g_free(body_type);
	    body = body->next;
	}
	while (body) {
	    gchar *name, *body_type, *tmp_file_name;
	    int fd;

	    if (body->filename) {
		libbalsa_mktempdir(&tmp_file_name);
		name = g_strdup_printf("%s/%s", tmp_file_name, body->filename);
		g_free(tmp_file_name);
		libbalsa_message_body_save(body, name);
	    } else {
		fd = g_file_open_tmp("balsa-continue-XXXXXX", &name, NULL);
		libbalsa_message_body_save_fd(body, fd);
	    }
	    body_type = libbalsa_message_body_get_mime_type(body);
	    add_attachment(bsmsg, name,
			   body->filename != NULL, body_type);
	    g_free(body_type);
	    body = body->next;
	}
    }
}

/* quoteBody ------------------------------------------------------------
   quotes properly the body of the message.
   Use GString to optimize memory usage.
*/
static GString *
quoteBody(BalsaSendmsg * bsmsg, LibBalsaMessage * message, SendType type)
{
    GString *body;
    gchar *str, *date = NULL;
    gchar *personStr;
    const gchar *orig_address;

    g_return_val_if_fail(message->headers, NULL);

    if (message->headers->from && 
	(orig_address = libbalsa_address_get_name(message->headers->from))) {
        personStr = g_strdup(orig_address);
        libbalsa_utf8_sanitize(&personStr,
                               balsa_app.convert_unknown_8bit,
                               NULL);
    } else
        personStr = g_strdup(_("you"));

    if (message->headers->date)
        date = libbalsa_message_date_to_gchar(message,
                                              balsa_app.date_string);

    if (type == SEND_FORWARD_ATTACH) {
	gchar *subject;

	str = g_strdup_printf(_("------forwarded message from %s------\n"), 
			      personStr);
	body = g_string_new(str);
	g_free(str);

	if (date)
	    g_string_append_printf(body, "%s %s\n", _("Date:"), date);

	subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));
	libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit,
			       NULL);
	if (subject)
	    g_string_append_printf(body, "%s %s\n", _("Subject:"), subject);
	g_free(subject);

	if (message->headers->from) {
	    gchar *from = libbalsa_address_to_gchar(message->headers->from, 0);
	    g_string_append_printf(body, "%s %s\n", _("From:"), from);
	    g_free(from);
	}

	if (message->headers->to_list) {
	    gchar *to_list =
		libbalsa_make_string_from_list(message->headers->to_list);
	    g_string_append_printf(body, "%s %s\n", _("To:"), to_list);
	    g_free(to_list);
	}

	if (message->headers->cc_list) {
	    gchar *cc_list = 
		libbalsa_make_string_from_list(message->headers->cc_list);
	    g_string_append_printf(body, "%s %s\n", _("Cc:"), cc_list);
	    g_free(cc_list);
	}

	g_string_append_printf(body, _("Message-ID: %s\n"),
                               message->message_id);

	if (message->references) {
	    GList *ref_list = message->references;

	    g_string_append(body, _("References:"));

	    for (ref_list = message->references; ref_list;
                 ref_list = g_list_next(ref_list))
		g_string_append_printf(body, " <%s>",
				       (gchar *) ref_list->data);
		
	    g_string_append_c(body, '\n');
	}
    } else {
	if (date)
	    str = g_strdup_printf(_("On %s, %s wrote:\n"), date, personStr);
	else
	    str = g_strdup_printf(_("%s wrote:\n"), personStr);
	body = content2reply(message,
			     (type == SEND_REPLY || type == SEND_REPLY_ALL || 
			      type == SEND_REPLY_GROUP) ?
			     balsa_app.quote_str : NULL,
			     balsa_app.wordwrap ? balsa_app.wraplength : -1,
			     balsa_app.reply_strip_html, bsmsg->flow);
	if (body) {
	    gchar *buf = body->str;

	    g_string_free(body, FALSE);
	    libbalsa_utf8_sanitize(&buf, balsa_app.convert_unknown_8bit,
				   NULL);
	    body = g_string_new(buf);
	    g_free(buf);
	    g_string_prepend(body, str);
	} else
	    body = g_string_new(str);
	g_free(str);
    }
    
    g_free(date);
    g_free(personStr);

    return body;
}

/* fillBody --------------------------------------------------------------
   fills the body of the message to be composed based on the given message.
   First quotes the original one, if autoquote is set,
   and then adds the signature.
   Optionally prepends the signature to quoted text.
*/
static void
fillBody(BalsaSendmsg * bsmsg, LibBalsaMessage * message, SendType type)
{
    GString *body;
    gchar *signature;
    gboolean reply_any = (type == SEND_REPLY || type == SEND_REPLY_ALL
                          || type == SEND_REPLY_GROUP);
    gboolean forwd_any = (type == SEND_FORWARD_ATTACH
                          || type == SEND_FORWARD_INLINE);
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start;

    if (message && ((balsa_app.autoquote && reply_any)
                    || type == SEND_FORWARD_INLINE))
        body = quoteBody(bsmsg, message, type);
    else
	body = g_string_new("");

    if ((signature = read_signature(bsmsg)) != NULL) {
	if ((reply_any && bsmsg->ident->sig_whenreply)
       || (forwd_any && bsmsg->ident->sig_whenforward)
       || (type == SEND_NORMAL && bsmsg->ident->sig_sending)) {

	    signature = prep_signature(bsmsg->ident, signature);

	    if (bsmsg->ident->sig_prepend && type != SEND_NORMAL) {
	    	g_string_prepend(body, "\n\n");
	    	g_string_prepend(body, signature);
	    } else {
	    	g_string_append(body, signature);
	    }
	    g_string_prepend_c(body, '\n');
	}
	g_free(signature);
    }

    libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);
    g_string_free(body, TRUE);
}

static gint insert_signature_cb(GtkWidget *widget, BalsaSendmsg *bsmsg)
{
    gchar *signature;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    
    if ((signature = read_signature(bsmsg)) != NULL) {
        sw_buffer_save(bsmsg);
	if (bsmsg->ident->sig_separator
	    && g_ascii_strncasecmp(signature, "--\n", 3)
	    && g_ascii_strncasecmp(signature, "-- \n", 4)) {
	    gchar * tmp = g_strconcat("-- \n", signature, NULL);
	    g_free(signature);
	    signature = tmp;
	}
	
        libbalsa_insert_with_url(buffer, signature, NULL, NULL, NULL);
	
	g_free(signature);
    }
    
    return TRUE;
}


static gint quote_messages_cb(GtkWidget *widget, BalsaSendmsg *bsmsg)
{
    return insert_selected_messages(bsmsg, SEND_REPLY);
}


/* set_entry_to_subject:
   set subject entry based on given replied/forwarded/continued message
   and the compose type.
*/
static void
set_entry_to_subject(GtkEntry* entry, LibBalsaMessage * message,
                     SendType type, LibBalsaIdentity* ident)
{
    const gchar *tmp;
    gchar *subject, *newsubject = NULL;
    gint i;

    if(!message) return;
    subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));
    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit,
			   NULL);

    switch (type) {
    case SEND_REPLY:
    case SEND_REPLY_ALL:
    case SEND_REPLY_GROUP:
	if (!subject) {
	    newsubject = g_strdup(ident->reply_string);
	    break;
	}
	
	tmp = subject;
	if (g_ascii_strncasecmp(tmp, "re:", 3) == 0 || g_ascii_strncasecmp(tmp, "aw:", 3) == 0) {
	    tmp += 3;
	} else if (g_ascii_strncasecmp(tmp, _("Re:"), strlen(_("Re:"))) == 0) {
	    tmp += strlen(_("Re:"));
	} else {
	    i = strlen(ident->reply_string);
	    if (g_ascii_strncasecmp(tmp, ident->reply_string, i)
		== 0) {
		tmp += i;
	    }
	}
	while( *tmp && isspace((int)*tmp) ) tmp++;
	newsubject = g_strdup_printf("%s %s", 
				     ident->reply_string, 
				     tmp);
	g_strchomp(newsubject);
	break;

    case SEND_FORWARD_ATTACH:
    case SEND_FORWARD_INLINE:
	if (!subject) {
	    if (message->headers && message->headers->from &&
		message->headers->from->address_list)
		newsubject = g_strdup_printf("%s from %s",
					     ident->forward_string,
					     libbalsa_address_get_mailbox(message->headers->from, 0));
	    else
		newsubject = g_strdup(ident->forward_string);
	} else {
	    tmp = subject;
	    if (g_ascii_strncasecmp(tmp, "fwd:", 4) == 0) {
		tmp += 4;
	    } else if (g_ascii_strncasecmp(tmp, _("Fwd:"), strlen(_("Fwd:"))) == 0) {
		tmp += strlen(_("Fwd:"));
	    } else {
		i = strlen(ident->forward_string);
		if (g_ascii_strncasecmp(tmp, ident->forward_string, i) == 0) {
		    tmp += i;
		}
	    }
	    while( *tmp && isspace((int)*tmp) ) tmp++;
	    if (message->headers && message->headers->from &&
		message->headers->from->address_list)
		newsubject = 
		    g_strdup_printf("%s %s [%s]",
				    ident->forward_string, 
				    tmp, libbalsa_address_get_mailbox(message->headers->from, 0));
	    else {
		newsubject = 
		    g_strdup_printf("%s %s", 
				    ident->forward_string, 
				    tmp);
		g_strchomp(newsubject);
	    }
	}
	break;
    case SEND_CONTINUE:
	if (subject)
	    gtk_entry_set_text(entry, subject);
	g_free(subject);
	return;
    default:
	return; /* or g_assert_never_reached() ? */
    }

    gtk_entry_set_text(entry, newsubject);
    g_free(subject);
    g_free(newsubject);
}

static void
sw_buffer_signals_block(BalsaSendmsg * bsmsg, GtkTextBuffer * buffer)
{
    g_signal_handler_block(buffer, bsmsg->changed_sig_id);
    g_signal_handler_block(buffer, bsmsg->delete_range_sig_id);
}

static void
sw_buffer_signals_unblock(BalsaSendmsg * bsmsg, GtkTextBuffer * buffer)
{
    g_signal_handler_unblock(buffer, bsmsg->changed_sig_id);
    g_signal_handler_unblock(buffer, bsmsg->delete_range_sig_id);
}

static gboolean
sw_wrap_timeout_cb(BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
    GtkTextIter now;

    gdk_threads_enter();

    text_view = GTK_TEXT_VIEW(bsmsg->text);
    buffer = gtk_text_view_get_buffer(text_view);
    gtk_text_buffer_get_iter_at_mark(buffer, &now,
                                     gtk_text_buffer_get_insert(buffer));

    bsmsg->wrap_timeout_id = 0;
    sw_buffer_signals_block(bsmsg, buffer);
    libbalsa_unwrap_buffer(buffer, &now, 1);
    libbalsa_wrap_view(text_view, balsa_app.wraplength);
    sw_buffer_signals_unblock(bsmsg, buffer);
    gtk_text_view_scroll_to_mark(text_view,
                                 gtk_text_buffer_get_insert(buffer),
                                 0, FALSE, 0, 0);

    gdk_threads_leave();

    return FALSE;
}

static void
text_changed(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    if (bsmsg->flow) {
        if (bsmsg->wrap_timeout_id)
            g_source_remove(bsmsg->wrap_timeout_id);
        bsmsg->wrap_timeout_id =
            g_timeout_add(500, (GSourceFunc) sw_wrap_timeout_cb, bsmsg);
    }

    bsmsg->modified = TRUE;
}

static void
set_entry_from_address_list(LibBalsaAddressEntry * address_entry,
                            GList * list)
{
    if (list) {
	gchar* tmp = libbalsa_make_string_from_list(list);
        gtk_entry_set_text(GTK_ENTRY(address_entry), tmp);
	g_free(tmp);
    }
}

static void
setup_headers_from_message(BalsaSendmsg* bsmsg, LibBalsaMessage *message)
{
    g_return_if_fail(message->headers);

    set_entry_from_address_list(LIBBALSA_ADDRESS_ENTRY(bsmsg->to[1]),
                                message->headers->to_list);
    set_entry_from_address_list(LIBBALSA_ADDRESS_ENTRY(bsmsg->cc[1]),
                                message->headers->cc_list);
    set_entry_from_address_list(LIBBALSA_ADDRESS_ENTRY(bsmsg->bcc[1]),
                                message->headers->bcc_list);
}


/* 
 * set_identity_from_mailbox
 * 
 * Attempt to determine the default identity from the mailbox containing
 * the message.
 **/
static gboolean
set_identity_from_mailbox(BalsaSendmsg* bsmsg)
{
    const gchar *identity;
    LibBalsaMessage *message = bsmsg->orig_message;
    LibBalsaIdentity* ident;
    GList *ilist;

    if( message && message->mailbox && balsa_app.identities) {
        identity = libbalsa_mailbox_get_identity_name(message->mailbox);
        if(!identity) return FALSE;
        for (ilist = balsa_app.identities;
             ilist != NULL;
             ilist = g_list_next(ilist)) {
            ident = LIBBALSA_IDENTITY(ilist->data);
            if (!g_ascii_strcasecmp(identity, ident->identity_name)) {
                bsmsg->ident = ident;
                return TRUE;
            }
        }
    }

    return FALSE; /* use default */
}

/* 
 * guess_identity
 * 
 * Attempt to determine if a message should be associated with a
 * particular identity, other than the default.  The to_list of the
 * original message needs to be set in order for it to work.
 **/
static gboolean
guess_identity(BalsaSendmsg* bsmsg)
{
    LibBalsaMessage *message = bsmsg->orig_message;
    const gchar *address_string;
    GList *ilist;
    LibBalsaIdentity *ident;
    gchar *tmp;


    if( !message  || !message->headers || !message->headers->to_list ||
	!balsa_app.identities)
        return FALSE; /* use default */

    if (bsmsg->type == SEND_CONTINUE) {
 	if (message->headers->from) {
 	    /*
 	    * Look for an identity that matches the From: address.
 	    */
 	    address_string = message->headers->from->address_list->data;
 	    for (ilist = balsa_app.identities; ilist;
 		 ilist = g_list_next(ilist)) {
 		ident = LIBBALSA_IDENTITY(ilist->data);
 		if ((tmp = ident->address->address_list->data)
 		    && !g_ascii_strcasecmp(address_string, tmp)) {
 		    bsmsg->ident = ident;
 		    return( TRUE );
 		}
	    }
 	}
    } else if (bsmsg->type != SEND_NORMAL) {
 	/* bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL ||
 	*  bsmsg->type == SEND_REPLY_GROUP || bsmsg->type == SEND_FORWARD_ATTACH ||
 	*  bsmsg->type == SEND_FORWARD_INLINE */
 	LibBalsaAddress *addy;
 	GList *alist;
 
 	/*
 	* Loop through all the addresses in the message's To:
 	* field, and look for an identity that matches one of them.
 	*/
 	for (alist = message->headers->to_list; alist; alist = g_list_next(alist)) {
 	    addy = alist->data;
 	    address_string = addy->address_list->data;
 	    for (ilist = balsa_app.identities; ilist;
 		 ilist = g_list_next(ilist)) {
 		ident = LIBBALSA_IDENTITY(ilist->data);
 		if ((tmp = ident->address->address_list->data)
 		    && !g_ascii_strcasecmp(address_string, tmp)) {
 		    bsmsg->ident = ident;
 		    return TRUE;
 		}
 	    }
 	}
 
 	/* No match in the to_list, try the cc_list */
 	for (alist = message->headers->cc_list; alist; alist = g_list_next(alist)) {
 	    addy = alist->data;
 	    address_string = addy->address_list->data;
 	    for (ilist = balsa_app.identities; ilist;
 		 ilist = g_list_next(ilist)) {
 		ident = LIBBALSA_IDENTITY(ilist->data);
 		if ((tmp = ident->address->address_list->data)
 		    && !g_ascii_strcasecmp(address_string, tmp)) {
 		    bsmsg->ident = ident;
 		    return TRUE;
 		}
 	    }
 	}
    }
    return FALSE;
}

static void
setup_headers_from_identity(BalsaSendmsg* bsmsg, LibBalsaIdentity *ident)    
{
    gchar* str = libbalsa_address_to_gchar(ident->address, 0);
    gtk_entry_set_text(GTK_ENTRY(bsmsg->from[1]), str);
    g_free(str); 
#if !defined(ENABLE_TOUCH_UI)
    if(ident->replyto)
        gtk_entry_set_text(GTK_ENTRY(bsmsg->reply_to[1]), ident->replyto);
#endif
    if(ident->bcc)
	gtk_entry_set_text(GTK_ENTRY(bsmsg->bcc[1]), ident->bcc);
}

static int
comp_send_locales(const void* a, const void* b)
{
    return g_utf8_collate(((struct SendLocales*)a)->lang_name,
                          ((struct SendLocales*)b)->lang_name);
}

/* create_lang_menu:
   create language menu for the compose window. The order cannot be
   hardcoded because it depends on the current locale.
*/
static void
create_lang_menu(GtkWidget* parent, BalsaSendmsg *bsmsg)
{
    unsigned i, selected_pos;
    GtkWidget* langs = gtk_menu_item_get_submenu(GTK_MENU_ITEM(parent));
    static gboolean locales_sorted = FALSE;
    GSList *group = NULL;

    if(!locales_sorted) {
        for(i=0; i<ELEMENTS(locales); i++)
            locales[i].lang_name = _(locales[i].lang_name);
        qsort(locales, ELEMENTS(locales), sizeof(struct SendLocales),
              comp_send_locales);
        locales_sorted = TRUE;
    }
    /* find the preferred charset... */
    selected_pos = find_locale_index_by_locale(setlocale(LC_CTYPE, NULL));
    if (bsmsg->charset
	&& g_ascii_strcasecmp(locales[selected_pos].charset, 
                              bsmsg->charset) != 0) {
	for(i=0; 
	    i<ELEMENTS(locales) && 
		g_ascii_strcasecmp(locales[i].charset, bsmsg->charset) != 0;
	    i++)
	    ;
        selected_pos = (i == ELEMENTS(locales)) ?
            find_locale_index_by_locale("en_US") : i;
    }
    
    set_locale(bsmsg, selected_pos);

    for(i=0; i<ELEMENTS(locales); i++) {
        GtkWidget *w = 
            gtk_radio_menu_item_new_with_mnemonic(group,
                                                  locales[i].lang_name);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(w));
        if(i==selected_pos)
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), TRUE);

        g_signal_connect(G_OBJECT(w), "activate", 
                         G_CALLBACK(lang_set_cb), bsmsg);
        g_object_set_data(G_OBJECT(w), GNOMEUIINFO_KEY_UIDATA, 
                          GINT_TO_POINTER(i));
        gtk_widget_show(w);
        gtk_menu_shell_append(GTK_MENU_SHELL(langs), w);
    }                         
}
        

/* Toolbar buttons and their callbacks. */
static const struct callback_item {
    const char *icon_id;
    BalsaToolbarFunc callback;
} callback_table[] = {
    {BALSA_PIXMAP_ATTACHMENT,  BALSA_TOOLBAR_FUNC(attach_clicked)},
    {BALSA_PIXMAP_IDENTITY,    BALSA_TOOLBAR_FUNC(change_identity_dialog_cb)},
    {BALSA_PIXMAP_POSTPONE,    BALSA_TOOLBAR_FUNC(postpone_message_cb)},
    {BALSA_PIXMAP_PRINT,       BALSA_TOOLBAR_FUNC(print_message_cb)},
    {BALSA_PIXMAP_SAVE,        BALSA_TOOLBAR_FUNC(save_message_cb)},
    {BALSA_PIXMAP_SEND,        BALSA_TOOLBAR_FUNC(send_message_toolbar_cb)},
    {GTK_STOCK_CLOSE,          BALSA_TOOLBAR_FUNC(close_window_cb)},
    {GTK_STOCK_SPELL_CHECK,    BALSA_TOOLBAR_FUNC(spell_check_cb)},
#ifdef HAVE_GPGME
    {BALSA_PIXMAP_GPG_SIGN,    BALSA_TOOLBAR_FUNC(toggle_sign_tb_cb)},
    {BALSA_PIXMAP_GPG_ENCRYPT, BALSA_TOOLBAR_FUNC(toggle_encrypt_tb_cb)},
#endif
    {GTK_STOCK_UNDO,           BALSA_TOOLBAR_FUNC(sw_undo_cb)},
    {GTK_STOCK_REDO,           BALSA_TOOLBAR_FUNC(sw_redo_cb)},
};

/* Standard buttons; "" means a separator. */
static const gchar* compose_toolbar[] = {
#if defined(ENABLE_TOUCH_UI)
    GTK_STOCK_UNDO,
    GTK_STOCK_REDO,
    GTK_STOCK_SPELL_CHECK,
    "",
    BALSA_PIXMAP_ATTACHMENT,
    "",
    BALSA_PIXMAP_SAVE,
    "",
    BALSA_PIXMAP_SEND,
    "",
    GTK_STOCK_CLOSE,
    "",
    BALSA_PIXMAP_IDENTITY,
#else /* ENABLE_TOUCH_UI */
    BALSA_PIXMAP_SEND,
    "",
    BALSA_PIXMAP_ATTACHMENT,
    "",
    BALSA_PIXMAP_SAVE,
    "",
    GTK_STOCK_UNDO,
    GTK_STOCK_REDO,
    "",
    BALSA_PIXMAP_IDENTITY,
    "",
    GTK_STOCK_SPELL_CHECK,
    "",
    BALSA_PIXMAP_PRINT,
    "",
    GTK_STOCK_CLOSE,
#endif /* ENABLE_TOUCH_UI */
};

/* Create the toolbar model for the compose window's toolbar.
 */
BalsaToolbarModel *
sendmsg_window_get_toolbar_model(void)
{
    static BalsaToolbarModel *model = NULL;
    GSList *legal;
    GSList *standard;
    GSList **current;
    guint i;

    if (model)
        return model;

    legal = NULL;
    for (i = 0; i < ELEMENTS(callback_table); i++)
        legal = g_slist_append(legal, g_strdup(callback_table[i].icon_id));

    standard = NULL;
    for (i = 0; i < ELEMENTS(compose_toolbar); i++)
        standard = g_slist_append(standard, g_strdup(compose_toolbar[i]));

    current = &balsa_app.compose_window_toolbar_current;

    model = balsa_toolbar_model_new(legal, standard, current);

    return model;
}

static void
bsmsg_identites_changed_cb(BalsaSendmsg *bsmsg)
{
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(bsmsg->window));
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_IDENTITY,
                                       g_list_length(balsa_app.identities)>1);
}
BalsaSendmsg *
sendmsg_window_new(GtkWidget * widget, LibBalsaMessage * message,
		   SendType type)
{
    BalsaToolbarModel *model;
    GtkWidget *toolbar;
    GtkWidget *window;
    GtkWidget *main_box = gtk_vbox_new(FALSE, 0);
    BalsaSendmsg *bsmsg = NULL;
    unsigned i;
    gchar* tmp;

    g_assert((type == SEND_NORMAL && message == NULL)
             || (type != SEND_NORMAL && message != NULL));

    bsmsg = g_malloc(sizeof(BalsaSendmsg));
    bsmsg->charset  = NULL;
    bsmsg->locale   = NULL;
    bsmsg->fcc_url  = NULL;
    bsmsg->ident = balsa_app.current_ident;
    bsmsg->update_config = FALSE;
    bsmsg->quit_on_close = FALSE;
    bsmsg->orig_message = message;

    bsmsg->window = window = gnome_app_new("balsa", NULL);
    /*
     * restore the SendMsg window size
     */
    gtk_window_set_default_size(GTK_WINDOW(window), 
			balsa_app.sw_width,
			balsa_app.sw_height);

    gtk_window_set_wmclass(GTK_WINDOW(window), "compose", "Balsa");
    gtk_widget_show(window);

    bsmsg->type = type;
    bsmsg->spell_checker = NULL;
#ifdef HAVE_GPGME
    bsmsg->gpg_mode = LIBBALSA_PROTECT_RFC3156;
#endif
    bsmsg->wrap_timeout_id = 0;

    if (message) {
        /* ref message so we don't lose it even if it is deleted */
	g_object_ref(G_OBJECT(message));
	/* reference the original mailbox so we don't loose the
	 * mail even if the mailbox is closed.
	 */
	if (message->mailbox)
	    libbalsa_mailbox_open(message->mailbox, NULL);
    }

    g_signal_connect(G_OBJECT(bsmsg->window), "delete-event",
		     G_CALLBACK(delete_event_cb), bsmsg);
    g_signal_connect(G_OBJECT(bsmsg->window), "destroy",
		     G_CALLBACK(destroy_event_cb), bsmsg);
    g_signal_connect(G_OBJECT(bsmsg->window), "size_allocate",
		     G_CALLBACK(sw_size_alloc_cb), bsmsg);

    gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, bsmsg);
    /* Save the widgets that we need to toggle--they'll be overwritten
     * if another compose window is opened. */
    bsmsg->undo_widget = edit_menu[EDIT_MENU_UNDO].widget;
    bsmsg->redo_widget = edit_menu[EDIT_MENU_REDO].widget;

    bsmsg->flow = balsa_app.wordwrap;
    gtk_widget_set_sensitive(edit_menu[EDIT_MENU_REFLOW_SELECTED].widget,
                             bsmsg->flow);

    model = sendmsg_window_get_toolbar_model();
    toolbar = balsa_toolbar_new(model);
    for(i=0; i < ELEMENTS(callback_table); i++)
        balsa_toolbar_set_callback(toolbar, callback_table[i].icon_id,
                                   G_CALLBACK(callback_table[i].callback),
                                   bsmsg);

    gnome_app_set_toolbar(GNOME_APP(window), GTK_TOOLBAR(toolbar));
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_IDENTITY,
                                       g_list_length(balsa_app.identities)>1);
    g_signal_connect_swapped(balsa_app.main_window, "identities-changed",
                             (GCallback)bsmsg_identites_changed_cb, bsmsg);
    sw_buffer_set_undo(bsmsg, TRUE, FALSE);

    bsmsg->ready_widgets[0] = file_menu[MENU_FILE_SEND_POS].widget;
    bsmsg->ready_widgets[1] = file_menu[MENU_FILE_QUEUE_POS].widget;
    bsmsg->ready_widgets[2] = file_menu[MENU_FILE_POSTPONE_POS].widget;

    /* set options */
    bsmsg->req_dispnotify = FALSE;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(DISPNOTIFY_WIDGET),
                                   FALSE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
                                   (opts_menu[OPTS_MENU_FORMAT_POS].
                                    widget), balsa_app.wordwrap);

    /* Set up the default identity */
    if(!set_identity_from_mailbox(bsmsg))
        /* Get the identity from the To: field of the original message */
        guess_identity(bsmsg);
#ifdef HAVE_GPGME
    bsmsg_setup_gpg_ui(bsmsg, toolbar);
#endif

    /* create the top portion with the to, from, etc in it */
    gtk_box_pack_start(GTK_BOX(main_box), create_info_pane(bsmsg, type),
                       FALSE, FALSE, 0);

    /* create text area for the message */
    gtk_box_pack_start(GTK_BOX(main_box), create_text_area(bsmsg),
                       TRUE, TRUE, 0);

    /* fill in that info:
     * ref the message so that we have all needed headers */
    if (message)
	libbalsa_message_body_ref(message, TRUE, TRUE);

    /* To: */
    if (type == SEND_REPLY || type == SEND_REPLY_ALL) {
        LibBalsaAddress *addr =
            (message->headers->reply_to) 
	    ? message->headers->reply_to : message->headers->from;

        if (addr) {
            tmp = libbalsa_address_to_gchar(addr, 0);
            libbalsa_utf8_sanitize(&tmp, balsa_app.convert_unknown_8bit,
                                   NULL);
            gtk_entry_set_text(GTK_ENTRY(bsmsg->to[1]), tmp);
            g_free(tmp);
        }
    } else if ( type == SEND_REPLY_GROUP ) {
        set_list_post_address(bsmsg);
    }

    /* From: */
    setup_headers_from_identity(bsmsg, bsmsg->ident);

    /* Subject: */
    set_entry_to_subject(GTK_ENTRY(bsmsg->subject[1]), message, type, bsmsg->ident);

    if (type == SEND_CONTINUE) {
	setup_headers_from_message(bsmsg, message);

	/* Replace "From" and "Reply-To" with values from the
	 * continued messages - they may have been hand edited. */
	if (message->headers->from != NULL)
	    gtk_entry_set_text(GTK_ENTRY(bsmsg->from[1]),
			       libbalsa_address_to_gchar(message->headers->from, 0));
#if !defined(ENABLE_TOUCH_UI)
	if (message->headers->reply_to != NULL)
	    gtk_entry_set_text(GTK_ENTRY(bsmsg->reply_to[1]),
			       libbalsa_address_to_gchar(message->headers->reply_to, 0));
#endif
    }

    if (type == SEND_REPLY_ALL) {
	tmp = libbalsa_make_string_from_list(message->headers->to_list);

 	libbalsa_utf8_sanitize(&tmp, balsa_app.convert_unknown_8bit,
 			       NULL);
	gtk_entry_set_text(GTK_ENTRY(bsmsg->cc[1]), tmp);
	g_free(tmp);

	if (message->headers->cc_list) {
	    tmp = libbalsa_make_string_from_list(message->headers->cc_list);
 	    libbalsa_utf8_sanitize(&tmp, balsa_app.convert_unknown_8bit,
 				   NULL);
	    append_comma_separated(GTK_EDITABLE(bsmsg->cc[1]), tmp);
	    g_free(tmp);
	}
    }
    gnome_app_set_contents(GNOME_APP(window), main_box);

    /* set the menus - and language index */
    if (message && !bsmsg->charset)
	bsmsg->charset = libbalsa_message_charset(message);
    init_menus(bsmsg);

    /* Connect to "text-changed" here, so that we catch the initial text
     * and wrap it... */
    sw_buffer_signals_connect(bsmsg);

    if (type == SEND_CONTINUE)
	continueBody(bsmsg, message);
    else
	fillBody(bsmsg, message, type);
    if (message)
	libbalsa_message_body_unref(message);
    /* ...but mark it as unmodified. */
    bsmsg->modified = FALSE;
    /* Save the initial state, so that `undo' will restore it. */
    sw_buffer_save(bsmsg);

    /* set the initial window title */
    sendmsg_window_set_title(bsmsg);


    if (type == SEND_NORMAL || type == SEND_FORWARD_ATTACH || 
	type == SEND_FORWARD_INLINE)
	gtk_widget_grab_focus(bsmsg->to[1]);
    else
	gtk_widget_grab_focus(bsmsg->text);

    if (type == SEND_FORWARD_ATTACH) {
	if(!attach_message(bsmsg, message))
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Attaching message failed.\n"
                                       "Possible reason: not enough temporary space"));
    }

    bsmsg->update_config = TRUE;
 
    bsmsg->delete_sig_id = 
	g_signal_connect(G_OBJECT(balsa_app.main_window), "delete-event",
			 G_CALLBACK(delete_event_cb), bsmsg);
    return bsmsg;
}

/* decode_and_strdup:
   decodes given URL string up to the delimiter and places the
   eos pointer in newstr if supplied (eos==NULL if end of string was reached)
*/
static gchar*
decode_and_strdup(const gchar*str, int delim, gchar** newstr)
{
    gchar num[3];
    GString *s = g_string_new(NULL);
    /* eos points to the character after the last to parse */
    gchar *eos = strchr(str, delim); 

    if(!eos) eos = (gchar*)str + strlen(str);
    while(str<eos) {
	switch(*str) {
	case '+':
	    g_string_append_c(s, ' ');
	    str++;
	    break;
	case '%':
	    if(str+2<eos) {
		strncpy(num, str+1, 2); num[2] = 0;
		g_string_append_c(s, strtol(num,NULL,16));
	    }
	    str+=3;
	    break;
	default:
	    g_string_append_c(s, *str++);
	}
    }
    if(newstr) *newstr = *eos ? eos+1 : NULL;
    eos = s->str;
    g_string_free(s,FALSE);
    return eos;
}
    
/* process_url:
   extracts all characters until NUL or question mark; parse later fields
   of format 'key'='value' with ampersands as separators.
*/ 
void 
sendmsg_window_process_url(const char *url, field_setter func, void *data)
{
    gchar * ptr, *to, *key, *val;

    to = decode_and_strdup(url,'?', &ptr);
    func(data, "to", to);
    g_free(to);
    while(ptr) {
	key = decode_and_strdup(ptr,'=', &ptr);
	if(ptr) {
	    val = decode_and_strdup(ptr,'&', &ptr);
	    func(data, key, val);
	    g_free(val);
	}
	g_free(key);
    }
}

/* sendmsg_window_set_field:
   sets given field of the compose window to the specified value.
*/
void
sendmsg_window_set_field(BalsaSendmsg * bsmsg, const gchar * key,
                         const gchar * val)
{
    GtkWidget *entry;
    g_return_if_fail(bsmsg);

    if (g_ascii_strcasecmp(key, "body") == 0) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

        libbalsa_insert_with_url(buffer, val, NULL, NULL, NULL);

        return;
    } else if (g_ascii_strcasecmp(key, "to")  ==0) entry = bsmsg->to[1];
    else if(g_ascii_strcasecmp(key, "subject")==0) entry = bsmsg->subject[1];
    else if(g_ascii_strcasecmp(key, "cc")     ==0) entry = bsmsg->cc[1];
    else if(g_ascii_strcasecmp(key, "bcc")    ==0) entry = bsmsg->bcc[1];
#if !defined(ENABLE_TOUCH_UI)
    else if(g_ascii_strcasecmp(key, "replyto")==0) entry = bsmsg->reply_to[1];
#endif
    else return;

    append_comma_separated(GTK_EDITABLE(entry), val);
}

static gchar *
read_signature(BalsaSendmsg *bsmsg)
{
    FILE *fp = NULL;
    size_t len = 0;
    gchar *ret = NULL, *path;

    if (bsmsg->ident->signature_path == NULL)
	return NULL;

    path = libbalsa_expand_path(bsmsg->ident->signature_path);
    if(bsmsg->ident->sig_executable){
        /* signature is executable */
	fp = popen(path,"r");
	g_free(path);
        if (!fp)
            return NULL;
         len = libbalsa_readfile_nostat(fp, &ret);
         pclose(fp);    
	}
     else{
         /* sign is normal file */
	 fp = fopen(path, "r");
	 g_free(path);
         if (!fp)
             return NULL;
         len = libbalsa_readfile_nostat(fp, &ret);
         fclose(fp);
	}
    return ret;
}

/* opens the load file dialog box, allows selection of the file and includes
   it at current point */
static const gchar* conv_charsets[] = {
 "ISO-8859-1", "ISO-8859-15", "ISO-8859-2", "ISO-8859-9", "ISO-8859-13",
 "EUC-KR", "EUC-JP", "EUC-TW", "KOI8-R"
};
enum{
    CHARSET_COLUMN,
    N_COLUMNS
};

static void
do_insert_string_select_ch(BalsaSendmsg* bsmsg, GtkTextBuffer *buffer,
                           const gchar* string, size_t len)
{
    GError* err = NULL;
    gsize bytes_read, bytes_written;
    gchar* s;
    guint i;
    GtkCellRenderer *renderer;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeViewColumn *column;
    GtkWidget * dialog =
        gtk_dialog_new_with_buttons(_("Choose charset"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                    GTK_STOCK_CANCEL, GTK_STOCK_CANCEL,
                                    NULL);
    GtkWidget* info = 
        gtk_label_new(_("This file is not encoded in US-ASCII or UTF-8.\n"
                        "Please choose the charset used to encode the file.\n"
			"(choose Generic UTF-8 if unsure)."));
    GtkListStore* store = gtk_list_store_new(N_COLUMNS,
                                             G_TYPE_STRING);
    GtkWidget* tree = 
        gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref(store);
    
    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer,
                                                      "text", CHARSET_COLUMN,
                                                      NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       info, FALSE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       tree, TRUE, TRUE, 5);
    gtk_widget_show(info);
    gtk_widget_show(tree);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    GTK_RESPONSE_OK);
    
    for(i=0; i<ELEMENTS(conv_charsets); i++) {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, CHARSET_COLUMN, conv_charsets[i], -1);
    }
    
    do {
        const gchar* charset = NULL;
        GtkTreeSelection *select;
        
        if(gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
            break;
        select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        if (gtk_tree_selection_get_selected(select, &model, &iter))
            gtk_tree_model_get(model, &iter, CHARSET_COLUMN, &charset, -1);
        g_print("Trying charset: %s\n", charset);
        s=g_convert(string, len, "UTF-8", charset,
                    &bytes_read, &bytes_written, &err);
        if(!err) {
            libbalsa_insert_with_url(buffer, s, NULL, NULL, NULL);
            g_free(s);
            break;
        }
        g_free(s);
        g_error_free(err);
    } while(1);
    
    gtk_widget_destroy(dialog);
}
static void
do_insert_file(GtkWidget * selector, GtkFileSelection * fs)
{
    const gchar *fname;
    FILE *fl;
    BalsaSendmsg *bsmsg;
    GtkTextBuffer *buffer;
    gchar * string;
    size_t len;

    bsmsg = (BalsaSendmsg *) g_object_get_data(G_OBJECT(fs), "balsa-data");
    fname = gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs));

    if ((fl = fopen(fname, "rt")) ==NULL) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Could not open the file %s.\n"), fname);
	return;
    }

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    sw_buffer_save(bsmsg);
    string = NULL;
    len = libbalsa_readfile(fl, &string);
    fclose(fl);
    
    if(g_utf8_validate(string, -1, NULL)) {
	libbalsa_insert_with_url(buffer, string, NULL, NULL, NULL);
    } else do_insert_string_select_ch(bsmsg, buffer, string, len);
    g_free(string);
    gtk_widget_destroy(GTK_WIDGET(fs));
}

static gint
include_file_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkWidget *file_selector;

    file_selector = gtk_file_selection_new(_("Include file"));
    gtk_window_set_wmclass(GTK_WINDOW(file_selector), "file", "Balsa");
    g_object_set_data(G_OBJECT(file_selector), "balsa-data", bsmsg);

    gtk_file_selection_hide_fileop_buttons(GTK_FILE_SELECTION
					   (file_selector));

    g_signal_connect(G_OBJECT
                     (GTK_FILE_SELECTION(file_selector)->ok_button),
                     "clicked", G_CALLBACK(do_insert_file), file_selector);

    /* Ensure that the dialog box is destroyed when the user clicks a button. */

    g_signal_connect_swapped(G_OBJECT
                             (GTK_FILE_SELECTION(file_selector)->cancel_button),
                             "clicked",
                             G_CALLBACK(gtk_widget_destroy),
                             (gpointer) file_selector);

    /* Display that dialog */
    gtk_widget_show(file_selector);

    return TRUE;
}


/* is_ready_to_send returns TRUE if the message is ready to send or 
   postpone.
*/
static gboolean
is_ready_to_send(BalsaSendmsg * bsmsg)
{
    gboolean ready;
    ready = bsmsg->from_info.ready && bsmsg->to_info.ready
        && bsmsg->cc_info.ready && bsmsg->bcc_info.ready;
#if !defined(ENABLE_TOUCH_UI)
    ready = ready && bsmsg->reply_to_info.ready;
#endif
    return ready;
}

static void
address_changed_cb(LibBalsaAddressEntry * address_entry,
                   BalsaSendmsgAddress * sma)
{
    if (!libbalsa_address_entry_matching(address_entry)) {
        set_ready(address_entry, sma);
        check_readiness(sma->bsmsg);
    }
}

static void
set_ready(LibBalsaAddressEntry * address_entry, BalsaSendmsgAddress *sma)
{
    gint len = 0;
    const gchar *tmp = gtk_entry_get_text(GTK_ENTRY(address_entry));

    if (*tmp) {
        GList *list = libbalsa_address_new_list_from_string(tmp);

        if (list) {
            len = g_list_length(list);

            g_list_foreach(list, (GFunc) g_object_unref, NULL);
            g_list_free(list);
        } else {
            /* error */
            len = -1;
        }
        
    }

    if (len < sma->min_addresses
        || (sma->max_addresses >= 0 && len > sma->max_addresses)) {
        if (sma->ready) {
            sma->ready = FALSE;
            gtk_widget_set_style(sma->label, sma->bsmsg->bad_address_style);
        }
    } else {
        if (!sma->ready) {
            sma->ready = TRUE;
            gtk_widget_set_style(sma->label, NULL);
        }
    }
}

static void
strip_chars(gchar * str, const gchar * char2strip)
{
    gchar *ins = str;
    while (*str) {
	if (strchr(char2strip, *str) == NULL)
	    *ins++ = *str;
	str++;
    }
    *ins = '\0';
}

static void
sw_wrap_body(BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view = GTK_TEXT_VIEW(bsmsg->text);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);

    if (bsmsg->flow) {
	sw_buffer_signals_block(bsmsg, buffer);
        libbalsa_unwrap_buffer(buffer, &start, -1);
        libbalsa_wrap_view(text_view, balsa_app.wraplength);
	sw_buffer_signals_unblock(bsmsg, buffer);
    } else {
        GtkTextIter now;
        gint pos;
        gchar *the_text;

        gtk_text_buffer_get_iter_at_mark(buffer, &now,
                                         gtk_text_buffer_get_insert(buffer));
        pos = gtk_text_iter_get_offset(&now);

        the_text = gtk_text_iter_get_text(&start, &end);
        libbalsa_wrap_string(the_text, balsa_app.wraplength);
        gtk_text_buffer_set_text(buffer, "", 0);
        libbalsa_insert_with_url(buffer, the_text, NULL, NULL, NULL);
        g_free(the_text);

        gtk_text_buffer_get_iter_at_offset(buffer, &now, pos);
        gtk_text_buffer_place_cursor(buffer, &now);
    }
    gtk_text_view_scroll_to_mark(text_view,
                                 gtk_text_buffer_get_insert(buffer),
                                 0, FALSE, 0, 0);
}

/* bsmsg2message:
   creates Message struct based on given BalsaMessage
   stripping EOL chars is necessary - the GtkEntry fields can in principle 
   contain them. Such characters might screw up message formatting
   (consider moving this code to mutt part).
*/
static LibBalsaMessage *
bsmsg2message(BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;
    GList *list;
    gchar *tmp;
    const gchar *ctmp;
    gchar recvtime[50];
    GtkTextIter start, end;

    g_assert(bsmsg != NULL);
    message = libbalsa_message_new();

    ctmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->from[1]));
    message->headers->from = libbalsa_address_new_from_string(ctmp);

    tmp = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->subject[1]), 0, -1);
    strip_chars(tmp, "\r\n");
    LIBBALSA_MESSAGE_SET_SUBJECT(message, tmp);

    message->headers->to_list =
        libbalsa_address_entry_get_list(LIBBALSA_ADDRESS_ENTRY(bsmsg->to[1]));

    message->headers->cc_list =
        libbalsa_address_entry_get_list(LIBBALSA_ADDRESS_ENTRY(bsmsg->cc[1]));
    
    message->headers->bcc_list =
        libbalsa_address_entry_get_list(LIBBALSA_ADDRESS_ENTRY(bsmsg->bcc[1]));


    /* get the fcc-box from the option menu widget */
    bsmsg->fcc_url =
        g_strdup(balsa_mblist_mru_option_menu_get(bsmsg->fcc[1]));

#if !defined(ENABLE_TOUCH_UI)
    ctmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->reply_to[1]));
    if (*ctmp)
	message->headers->reply_to = libbalsa_address_new_from_string(ctmp);
#endif

    if (bsmsg->req_dispnotify)
	libbalsa_message_set_dispnotify(message, bsmsg->ident->address);

    if (bsmsg->orig_message != NULL) {

	if (bsmsg->orig_message->references != NULL) {
	    for (list = bsmsg->orig_message->references; list;
		 list = list->next) {
		message->references =
		    g_list_prepend(message->references,
				  g_strdup(list->data));
	    }
	    message->references = g_list_reverse(message->references);
	}
	ctime_r(&bsmsg->orig_message->headers->date, recvtime);
        if(recvtime[0]) /* safety check; remove trailing '\n' */
            recvtime[strlen(recvtime)-1] = '\0';
	if (bsmsg->orig_message->message_id) {
	    message->references =
		g_list_append(message->references,
			       g_strdup(bsmsg->orig_message->message_id));
	    message->in_reply_to =
		g_list_prepend(NULL,
		    bsmsg->orig_message->headers->from
		    ? g_strconcat("<", bsmsg->orig_message->message_id,
				  "> (from ",
				  bsmsg->orig_message->headers->from->
				  address_list->data, " on ", recvtime, ")",
				  NULL)
		    : g_strconcat("<", bsmsg->orig_message->message_id, ">",
				  NULL));
	}
    }

    body = libbalsa_message_body_new(message);

    /* Get the text from the buffer. First make sure it's wrapped. */
    if (balsa_app.wordwrap)
	sw_wrap_body(bsmsg);
    /* Copy it to buffer2, so we can change it without changing the
     * display. */
    sw_buffer_save(bsmsg);
    if (bsmsg->flow)
	libbalsa_prepare_delsp(bsmsg->buffer2);
    gtk_text_buffer_get_bounds(bsmsg->buffer2, &start, &end);
    body->buffer = gtk_text_iter_get_text(&start, &end);
    /* Disable undo and redo, because buffer2 was changed. */
    sw_buffer_set_undo(bsmsg, FALSE, FALSE);

    body->charset = g_strdup(bsmsg->charset);
    libbalsa_message_append_part(message, body);

    {                           /* handle attachments */
        guint i;
        guint n =
            gnome_icon_list_get_num_icons(GNOME_ICON_LIST
                                          (bsmsg->attachments[1]));

        for (i = 0; i < n; i++) {
	    attachment_t *attach;
	    
	    body = libbalsa_message_body_new(message);
	    /* PKGW: This used to be g_strdup'ed. However, the original pointer 
	       was strduped and never freed, so we'll take it. 
	       A. Dreß, 2001/May/05: However, when printing a message from the
	       composer, this will lead to a crash upon send as the resulting
	       message will be unref'd after printing. */
	    /* A. Dreß, 2001/Aug/29: now the `attachment_t' struct has it's own
	       destroy method, which frees the original filename later. So it's
	       safe (and necessary) to make a copy of it for the new body. */
	    attach = 
		(attachment_t *)gnome_icon_list_get_icon_data(GNOME_ICON_LIST
							      (bsmsg->attachments[1]), i);
	    body->filename = g_strdup(attach->filename);
	    if (attach->force_mime_type)
		body->content_type = g_strdup(attach->force_mime_type);
	    body->attach_as_extbody = attach->as_extbody;
	    libbalsa_message_append_part(message, body);
	}
    }

    message->headers->date = time(NULL);
#ifdef HAVE_GPGME
    if (balsa_app.has_openpgp || balsa_app.has_smime)
        message->gpg_mode = 
            (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) != 0 ? bsmsg->gpg_mode : 0;
    else
        message->gpg_mode = 0;
#endif

    return message;
}

static gboolean
is_charset_ok(BalsaSendmsg *bsmsg)
{
    gchar *tmp, *res;
    gsize bytes_read, bytes_written;
    GError *err = NULL;
    GtkTextIter start, end;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    tmp = gtk_text_iter_get_text(&start, &end);
    res = g_convert(tmp, strlen(tmp), bsmsg->charset, "UTF-8", 
		    &bytes_read, &bytes_written, &err);
    g_free(tmp);
    g_free(res);
    if (err) {
        balsa_information_parented
            (GTK_WINDOW(bsmsg->window),
             LIBBALSA_INFORMATION_ERROR,
             _("The message cannot be encoded in charset %s.\n"
               "Please choose a language for this message.\n"
               "For multi-language messages, choose UTF-8."),
             bsmsg->charset);
        g_error_free(err);
        return FALSE;
    }
    return TRUE;
}
/* "send message" menu and toolbar callback.
 * FIXME: automatic charset detection, as libmutt does for strings?
 */
static gint
send_message_handler(BalsaSendmsg * bsmsg, gboolean queue_only)
{
    LibBalsaMsgCreateResult result;
    LibBalsaMessage *message;
    LibBalsaMailbox *fcc;

    if (!is_ready_to_send(bsmsg))
	return FALSE;

    if (balsa_app.debug)
	fprintf(stderr, "sending with charset: %s\n", bsmsg->charset);

    if(!is_charset_ok(bsmsg))
        return FALSE;
#ifdef HAVE_GPGME
    if ((bsmsg->gpg_mode & LIBBALSA_PROTECT_OPENPGP) != 0 &&
        (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) != 0 &&
        gnome_icon_list_get_num_icons(GNOME_ICON_LIST
                                      (bsmsg->attachments[1])) > 0) {
	/* we are going to RFC2440 sign/encrypt a multipart... */
	GtkWidget *dialog;
	gint choice;

	dialog = gtk_message_dialog_new
            (GTK_WINDOW(bsmsg->window),
             GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
             GTK_MESSAGE_QUESTION,
             GTK_BUTTONS_OK_CANCEL,
             _("You selected OpenPGP mode for a message with attachments. "
               "In this mode, only the first part will be signed and/or "
               "encrypted. You should select MIME mode if the complete "
               "message shall be protected. Do you really want to proceed?"));
	choice = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	if (choice != GTK_RESPONSE_OK)
	    return FALSE;
    }
#endif

    message = bsmsg2message(bsmsg);
    fcc = balsa_find_mailbox_by_url(bsmsg->fcc_url);

#ifdef HAVE_GPGME
    balsa_information(LIBBALSA_INFORMATION_DEBUG,
		      _("sending message with gpg mode %d"),
		      message->gpg_mode);
#endif

    if(queue_only)
	result = libbalsa_message_queue(message, balsa_app.outbox, fcc,
					balsa_app.encoding_style,
					bsmsg->flow);
    else 
#if ENABLE_ESMTP
	result = libbalsa_message_send
            (message, balsa_app.outbox, fcc,
             balsa_app.encoding_style, balsa_find_sentbox_by_url,
             balsa_app.smtp_server,
             (balsa_app.smtp_user && *balsa_app.smtp_user)
             ? balsa_app.smtp_authctx : NULL,
             balsa_app.smtp_tls_mode,
             bsmsg->flow, balsa_app.debug);
#else
        result = libbalsa_message_send(message, balsa_app.outbox, fcc,
                                       balsa_find_sentbox_by_url,
				       balsa_app.encoding_style,
				       bsmsg->flow, balsa_app.debug); 
#endif
    if (result == LIBBALSA_MESSAGE_CREATE_OK && bsmsg->orig_message
        && bsmsg->orig_message->mailbox
	&& !bsmsg->orig_message->mailbox->readonly) {
	if (bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL ||
	    bsmsg->type == SEND_REPLY_GROUP) {
	    libbalsa_message_reply(bsmsg->orig_message);
	} else if (bsmsg->type == SEND_CONTINUE) {
	    GList * messages = g_list_prepend(NULL, bsmsg->orig_message);

	    libbalsa_messages_change_flag(messages,
                                          LIBBALSA_MESSAGE_FLAG_DELETED,
                                          TRUE);
	    g_list_free(messages);
	}
    }

    g_object_unref(G_OBJECT(message));

    if (result != LIBBALSA_MESSAGE_CREATE_OK) {
        const char *msg;
        switch(result) {
        default:
        case LIBBALSA_MESSAGE_CREATE_ERROR: 
            msg = _("Message could not be created"); break;
        case LIBBALSA_MESSAGE_QUEUE_ERROR:
            msg = _("Message could not be queued in outbox"); break;
        case LIBBALSA_MESSAGE_SAVE_ERROR:
            msg = _("Message could not be saved in sentbox"); break;
        case LIBBALSA_MESSAGE_SEND_ERROR:
            msg = _("Message could not be sent"); break;
        }
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
				   _("Send failed: %s"), msg);
	return FALSE;
    }

    gtk_widget_destroy(bsmsg->window);

    return TRUE;
}

/* "send message" toolbar callback */
static void
send_message_toolbar_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    libbalsa_address_entry_clear_to_send(LIBBALSA_ADDRESS_ENTRY
                                         (bsmsg->to[1]));
    libbalsa_address_entry_clear_to_send(LIBBALSA_ADDRESS_ENTRY
                                         (bsmsg->cc[1]));
    libbalsa_address_entry_clear_to_send(LIBBALSA_ADDRESS_ENTRY
                                         (bsmsg->bcc[1]));
    send_message_handler(bsmsg, balsa_app.always_queue_sent_mail);
}


/* "send message" menu callback */
static gint
send_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    /*
     * First, check if aliasing is on, and get it to nullify the
     * match.  Otherwise we send mail to "John (John Doe <jdoe@public.com>)"
     */
    libbalsa_address_entry_clear_to_send(LIBBALSA_ADDRESS_ENTRY
                                         (bsmsg->to[1]));
    libbalsa_address_entry_clear_to_send(LIBBALSA_ADDRESS_ENTRY
                                         (bsmsg->cc[1]));
    libbalsa_address_entry_clear_to_send(LIBBALSA_ADDRESS_ENTRY
                                         (bsmsg->bcc[1]));
    return send_message_handler(bsmsg, FALSE);
}


static gint
queue_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return send_message_handler(bsmsg, TRUE);
}

static gboolean
message_postpone(BalsaSendmsg * bsmsg)
{
    gboolean successp;
    LibBalsaMessage *message;

    if(!is_charset_ok(bsmsg))
        return FALSE;
    message = bsmsg2message(bsmsg);

    if ((bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL ||
        bsmsg->type == SEND_REPLY_GROUP))
	successp = libbalsa_message_postpone(message, balsa_app.draftbox,
                                             bsmsg->orig_message,
                                             bsmsg->fcc_url,
                                             balsa_app.encoding_style,
                                             bsmsg->flow);
    else
	successp = libbalsa_message_postpone(message, balsa_app.draftbox, 
                                             NULL,
                                             bsmsg->fcc_url,
                                             balsa_app.encoding_style,
                                             bsmsg->flow);
    if(successp) {
	if (bsmsg->type == SEND_CONTINUE && bsmsg->orig_message
	    && bsmsg->orig_message->mailbox
	    && !bsmsg->orig_message->mailbox->readonly) {
	    GList * messages = g_list_prepend(NULL, bsmsg->orig_message);

	    libbalsa_messages_change_flag(messages,
                                          LIBBALSA_MESSAGE_FLAG_DELETED,
                                          TRUE);
	    g_list_free(messages);
	}
    } else
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not postpone message."));

    g_object_unref(G_OBJECT(message));
    return successp;
}

/* "postpone message" menu and toolbar callback */
static void
postpone_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    if (is_ready_to_send(bsmsg)) {
        if(message_postpone(bsmsg)) {
            balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                       LIBBALSA_INFORMATION_MESSAGE,
                                       _("Message postponed."));
            gtk_widget_destroy(bsmsg->window);
        }
    }
}


static void
save_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GError *err = NULL;
    if (!message_postpone(bsmsg))
        return;
    if(!libbalsa_mailbox_open(balsa_app.draftbox, &err)) {
	balsa_information_parented(GTK_WINDOW(bsmsg->window),
				   LIBBALSA_INFORMATION_WARNING,
				   _("Could not open draftbox: %s"),
				   err ? err->message : _("Unknown error"));
	g_clear_error(&err);
	return;
    }

    if (bsmsg->orig_message) {
	if (bsmsg->orig_message->mailbox)
	    libbalsa_mailbox_close(bsmsg->orig_message->mailbox,
		    /* Respect pref setting: */
				   balsa_app.expunge_on_close);
	g_object_unref(G_OBJECT(bsmsg->orig_message));
    }
    bsmsg->type = SEND_CONTINUE;
    bsmsg->modified = FALSE;

    bsmsg->orig_message =
	libbalsa_mailbox_get_message(balsa_app.draftbox,
				     libbalsa_mailbox_total_messages
				     (balsa_app.draftbox));
    balsa_information_parented(GTK_WINDOW(bsmsg->window),
                               LIBBALSA_INFORMATION_MESSAGE,
                               _("Message saved."));
}

static void
print_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message;

    message = bsmsg2message(bsmsg);
    message_print(message, GTK_WINDOW(bsmsg->window));
    g_object_unref(message);
}

/*
 * Helpers for the undo and redo buffers.
 */
static void
sw_buffer_signals_connect(BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    bsmsg->changed_sig_id =
        g_signal_connect(buffer, "changed",
                         G_CALLBACK(text_changed), bsmsg);
    bsmsg->delete_range_sig_id =
        g_signal_connect_swapped(buffer, "delete-range",
                                 G_CALLBACK(sw_buffer_save), bsmsg);
}

static void
sw_buffer_signals_disconnect(BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    g_signal_handler_disconnect(buffer, bsmsg->changed_sig_id);
    g_signal_handler_disconnect(buffer, bsmsg->delete_range_sig_id);
}

static void sw_buffer_set_undo(BalsaSendmsg * bsmsg, gboolean undo,
	                       gboolean redo)
{
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(bsmsg->window));

    gtk_widget_set_sensitive(bsmsg->undo_widget, undo);
    balsa_toolbar_set_button_sensitive(toolbar, GTK_STOCK_UNDO, undo);
    gtk_widget_set_sensitive(bsmsg->redo_widget, redo);
    balsa_toolbar_set_button_sensitive(toolbar, GTK_STOCK_REDO, redo);
}

static void
sw_buffer_swap(BalsaSendmsg * bsmsg, gboolean undo)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    sw_buffer_signals_disconnect(bsmsg);
    g_object_ref(G_OBJECT(buffer));
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(bsmsg->text), bsmsg->buffer2);
    g_object_unref(bsmsg->buffer2);
    bsmsg->buffer2 = buffer;
    sw_buffer_signals_connect(bsmsg);
    sw_buffer_set_undo(bsmsg, !undo, undo);
}

static void
sw_buffer_save(BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start, end, iter;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_set_text(bsmsg->buffer2, "", 0);
    gtk_text_buffer_get_start_iter(bsmsg->buffer2, &iter);
    gtk_text_buffer_insert_range(bsmsg->buffer2, &iter, &start, &end);

    sw_buffer_set_undo(bsmsg, TRUE, FALSE);
}

/*
 * Menu and toolbar callbacks.
 */

static void
sw_undo_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    sw_buffer_swap(bsmsg, TRUE);
}

static void
sw_redo_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    sw_buffer_swap(bsmsg, FALSE);
}

/*
 * Cut, copy, and paste callbacks, and a helper.
 */
static void
clipboard_helper(BalsaSendmsg * bsmsg, gchar * signal)
{
    guint signal_id;
    GtkWidget *focus_widget =
        gtk_window_get_focus(GTK_WINDOW(bsmsg->window));

    signal_id =
        g_signal_lookup(signal, G_TYPE_FROM_INSTANCE(focus_widget));
    if (signal_id)
        g_signal_emit(focus_widget, signal_id, (GQuark) 0);
}

static void
cut_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    sw_buffer_save(bsmsg);
    clipboard_helper(bsmsg, "cut-clipboard");
}

static void
copy_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    clipboard_helper(bsmsg, "copy-clipboard");
}

static void
paste_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    sw_buffer_save(bsmsg);
    clipboard_helper(bsmsg, "paste-clipboard");
}

/*
 * More menu callbacks.
 */
static void
select_all_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    libbalsa_window_select_all(GTK_WINDOW(bsmsg->window));
}

static void
wrap_body_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    sw_buffer_save(bsmsg);
    sw_wrap_body(bsmsg);
}

static void
reflow_selected_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view = GTK_TEXT_VIEW(bsmsg->text);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    regex_t rex;

    if (!bsmsg->flow)
	return;

    if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED)) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Could not compile %s"),
			  _("Quoted Text Regular Expression"));
	return;
    }

    sw_buffer_save(bsmsg);

    sw_buffer_signals_block(bsmsg, buffer);
    libbalsa_unwrap_selection(buffer, &rex);
    libbalsa_wrap_view(text_view, balsa_app.wraplength);
    sw_buffer_signals_unblock(bsmsg, buffer);

    bsmsg->modified = TRUE;
    gtk_text_view_scroll_to_mark(text_view,
				 gtk_text_buffer_get_insert(buffer),
				 0, FALSE, 0, 0);

    regfree(&rex);
}

/* To field "changed" signal callback. */
static void
check_readiness(BalsaSendmsg * bsmsg)
{
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(bsmsg->window));
    unsigned i;
    gboolean state = is_ready_to_send(bsmsg);

    for (i = 0; i < ELEMENTS(bsmsg->ready_widgets); i++)
        gtk_widget_set_sensitive(bsmsg->ready_widgets[i], state);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_SEND, state);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_POSTPONE, state);
}

/* toggle_entry:
   auxiliary function for "header show/hide" toggle menu entries.
   saves the show header configuration.
 */
static gint
toggle_entry(BalsaSendmsg * bbsmsg, GtkWidget * entry[], int pos, int cnt)
{
    unsigned i;

    if (GTK_CHECK_MENU_ITEM(bbsmsg->view_checkitems[pos])->active) {
	while (cnt--)
	    gtk_widget_show_all(GTK_WIDGET(entry[cnt]));
    } else {
	while (cnt--)
	    gtk_widget_hide(GTK_WIDGET(entry[cnt]));
    }

    if(bbsmsg->update_config) { /* then save the config */
        GString *str = g_string_new(NULL);

	for (i = 0; i < ELEMENTS(headerDescs); i++)
	    if (GTK_CHECK_MENU_ITEM(bbsmsg->view_checkitems[i])->active) {
                if (str->len > 0)
                    g_string_append_c(str, ' ');
                g_string_append(str, headerDescs[i].name);
	    }
	g_free(balsa_app.compose_headers);
	balsa_app.compose_headers = g_string_free(str, FALSE);
    }

    return TRUE;
}

static gint
toggle_from_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->from, MENU_TOGGLE_FROM_POS, 3);
}

static gint
toggle_cc_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->cc, MENU_TOGGLE_CC_POS, 3);
}

static gint
toggle_bcc_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->bcc, MENU_TOGGLE_BCC_POS, 3);
}

static gint
toggle_fcc_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->fcc, MENU_TOGGLE_FCC_POS, 2);
}

#if !defined(ENABLE_TOUCH_UI)
static gint
toggle_reply_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->reply_to, MENU_TOGGLE_REPLY_POS, 3);
}
#endif /* ENABLE_TOUCH_UI */

static void
toggle_reqdispnotify_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    bsmsg->req_dispnotify = GTK_CHECK_MENU_ITEM(widget)->active;
}

static void
toggle_format_cb(GtkCheckMenuItem * check_menu_item, BalsaSendmsg * bsmsg)
{
    bsmsg->flow = gtk_check_menu_item_get_active(check_menu_item);
    gtk_widget_set_sensitive(edit_menu[EDIT_MENU_REFLOW_SELECTED].widget,
                             bsmsg->flow);
}

#ifdef HAVE_GPGME
static void 
toggle_sign_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gboolean butval, radio_on;
    GtkWidget *toolbar = g_object_get_data(G_OBJECT(widget), "toolbar");
    GtkWidget *rb1 = g_object_get_data(G_OBJECT(widget), "radbut-1");
    GtkWidget *rb2 = g_object_get_data(G_OBJECT(widget), "radbut-2");
#ifdef HAVE_SMIME
    GtkWidget *rb3 = g_object_get_data(G_OBJECT(widget), "radbut-3");
#endif

    g_return_if_fail(toolbar != NULL);
    g_return_if_fail(rb1 != NULL);
    g_return_if_fail(rb2 != NULL);
    butval = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
    if (butval)
	bsmsg->gpg_mode |= LIBBALSA_PROTECT_SIGN;
    else
	bsmsg->gpg_mode &= ~LIBBALSA_PROTECT_SIGN;

    radio_on = (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) > 0;
    gtk_widget_set_sensitive(rb1, radio_on);
    gtk_widget_set_sensitive(rb2, radio_on);
#ifdef HAVE_SMIME
    gtk_widget_set_sensitive(rb3, radio_on);
#endif

    balsa_toolbar_set_button_active(toolbar, BALSA_PIXMAP_GPG_SIGN, butval);
}


static void 
toggle_sign_tb_cb(GtkToggleButton * widget, BalsaSendmsg * bsmsg)
{
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_sign_menu_item),
				   gtk_toggle_button_get_active(widget));
}


static void 
toggle_encrypt_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gboolean butval, radio_on;
    GtkWidget *toolbar = g_object_get_data(G_OBJECT(widget), "toolbar");
    GtkWidget *rb1 = g_object_get_data(G_OBJECT(widget), "radbut-1");
    GtkWidget *rb2 = g_object_get_data(G_OBJECT(widget), "radbut-2");
#ifdef HAVE_SMIME
    GtkWidget *rb3 = g_object_get_data(G_OBJECT(widget), "radbut-3");
#endif

    g_return_if_fail(toolbar != NULL);
    g_return_if_fail(rb1 != NULL);
    g_return_if_fail(rb2 != NULL);
    butval = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
    if (butval)
	bsmsg->gpg_mode |= LIBBALSA_PROTECT_ENCRYPT;
    else
	bsmsg->gpg_mode &= ~LIBBALSA_PROTECT_ENCRYPT;

    radio_on = (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) > 0;
    gtk_widget_set_sensitive(rb1, radio_on);
    gtk_widget_set_sensitive(rb2, radio_on);
#ifdef HAVE_SMIME
    gtk_widget_set_sensitive(rb3, radio_on);
#endif

    balsa_toolbar_set_button_active(toolbar, BALSA_PIXMAP_GPG_ENCRYPT, butval);
}


static void 
toggle_encrypt_tb_cb(GtkToggleButton * widget, BalsaSendmsg * bsmsg)
{
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_encrypt_menu_item),
				   gtk_toggle_button_get_active(widget));
}


static void
toggle_gpg_mode_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gint rfc_flag = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
						      GNOMEUIINFO_KEY_UIDATA));

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
	bsmsg->gpg_mode = (bsmsg->gpg_mode & ~LIBBALSA_PROTECT_PROTOCOL) | rfc_flag;
}
#endif  /* HAVE_GPGME */


/* init_menus:
   performs the initial menu setup: shown headers as well as correct
   message charset. Copies also the the menu pointers for further usage
   at the message close  - they would be overwritten if another compose
   window was opened simultaneously.
*/
static void
init_menus(BalsaSendmsg * bsmsg)
{
    unsigned i;

    g_assert(ELEMENTS(headerDescs) == ELEMENTS(bsmsg->view_checkitems));

    for (i = 0; i < ELEMENTS(headerDescs); i++) {
	bsmsg->view_checkitems[i] = view_menu[i].widget;
	if (libbalsa_find_word(headerDescs[i].name, 
			       balsa_app.compose_headers)) {
	    /* show... (well, it has already been shown). */
	    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
					   (view_menu[i].widget), TRUE);
	} else {
	    /* or hide... */
	    VIEW_MENU_FUNC(view_menu[i].moreinfo)(view_menu[i].widget, bsmsg);
	}
    }

    create_lang_menu(LANG_MENU_WIDGET, bsmsg);

    /* gray 'send' and 'postpone' */
    check_readiness(bsmsg);
}

/* set_locale:
   bsmsg is the compose window,
   idx - corresponding entry index in locales.
*/

static gint
set_locale(BalsaSendmsg * bsmsg, gint idx)
{
    g_free(bsmsg->charset);
    bsmsg->charset = g_strdup(locales[idx].charset);
    bsmsg->locale = locales[idx].locale;
    return FALSE;
}

/* spell_check_cb
 * 
 * Start the spell check
 * */
static void
spell_check_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view = GTK_TEXT_VIEW(bsmsg->text);
    BalsaSpellCheck *sc;

    if (bsmsg->spell_checker) {
        if (bsmsg->spell_checker->window) {
            gdk_window_raise(bsmsg->spell_checker->window);
            return;
        } else
            /* A spell checker was created, but not shown because of
             * errors; we'll destroy it, and create a new one. */
            gtk_widget_destroy(bsmsg->spell_checker);
    }

    sw_buffer_signals_disconnect(bsmsg);

    bsmsg->spell_checker = balsa_spell_check_new(GTK_WINDOW(bsmsg->window));
    sc = BALSA_SPELL_CHECK(bsmsg->spell_checker);
    g_object_add_weak_pointer(G_OBJECT(sc), (gpointer) &bsmsg->spell_checker);

    /* configure the spell checker */
    balsa_spell_check_set_text(sc, text_view);
    balsa_spell_check_set_language(sc, bsmsg->locale);

    balsa_spell_check_set_character_set(sc, bsmsg->charset);
    balsa_spell_check_set_module(sc,
				 spell_check_modules_name
				 [balsa_app.module]);
    balsa_spell_check_set_suggest_mode(sc,
				       spell_check_suggest_mode_name
				       [balsa_app.suggestion_mode]);
    balsa_spell_check_set_ignore_length(sc, balsa_app.ignore_size);
    g_signal_connect(G_OBJECT(sc), "response",
                     G_CALLBACK(sw_spell_check_response), bsmsg);
    gtk_text_view_set_editable(text_view, FALSE);

    balsa_spell_check_start(sc);
}

static void
sw_spell_check_response(BalsaSpellCheck * spell_check, gint response, 
                        BalsaSendmsg * bsmsg)
{
    gtk_widget_destroy(GTK_WIDGET(spell_check));
    bsmsg->spell_checker = NULL;
    gtk_text_view_set_editable(GTK_TEXT_VIEW(bsmsg->text), TRUE);
    sw_buffer_signals_connect(bsmsg);
}

static void
lang_set_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w))) {
	gint i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),
						   GNOMEUIINFO_KEY_UIDATA));
	set_locale(bsmsg, i);
    }
}

/* sendmsg_window_new_from_list:
 * like sendmsg_window_new, but takes a GList of messages, instead of a
 * single message;
 * called by compose_from_list (balsa-index.c)
 */
BalsaSendmsg *
sendmsg_window_new_from_list(GtkWidget * w, GList * message_list,
                             SendType type)
{
    BalsaSendmsg *bsmsg;
    LibBalsaMessage *message;
    GtkTextBuffer *buffer;

    g_return_val_if_fail(message_list != NULL, NULL);

    message = message_list->data;
    bsmsg = sendmsg_window_new(w, message, type);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    while ((message_list = g_list_next(message_list))) {
        message = message_list->data;
        if (type == SEND_FORWARD_ATTACH)
            attach_message(bsmsg, message);
        else if (type == SEND_FORWARD_INLINE) {
            GString *body = quoteBody(bsmsg, message, type);
            libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
            g_string_free(body, TRUE);
        }
    }

    bsmsg->modified = FALSE;

    return bsmsg;
}

/* set_list_post_address:
 * look for the address for posting messages to a list */
static void
set_list_post_address(BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message = bsmsg->orig_message;
    LibBalsaAddress *mailing_list_address;
    GList *p;

    mailing_list_address =
	libbalsa_mailbox_get_mailing_list_address(message->mailbox);
    if (mailing_list_address) {
        gchar *tmp = libbalsa_address_to_gchar(mailing_list_address, 0);
 	libbalsa_utf8_sanitize(&tmp, balsa_app.convert_unknown_8bit,
 			       NULL);
        gtk_entry_set_text(GTK_ENTRY(bsmsg->to[1]), tmp);
        g_free(tmp);
        return;
    }

    if ((p = libbalsa_message_find_user_hdr(message, "list-post"))
	&& set_list_post_rfc2369(bsmsg, p))
	return;

    /* we didn't find "list-post", so try some nonstandard
     * alternatives: */

    if ((p = libbalsa_message_find_user_hdr(message, "x-beenthere"))
	|| (p = libbalsa_message_find_user_hdr(message, "x-mailing-list"))) {
	gchar **pair = p->data;
	gtk_entry_set_text(GTK_ENTRY(bsmsg->to[1]), pair[1]);
    }
}

/* set_list_post_rfc2369:
 * look for "List-Post:" header, and get the address */
static gboolean
set_list_post_rfc2369(BalsaSendmsg * bsmsg, GList * p)
{
    gchar **pair;
    gchar *url;

    pair = p->data;
    url = pair[1];

    /* RFC 2369: To allow for future extension, client
     * applications MUST follow the following guidelines for
     * handling the contents of the header fields described in
     * this document:
     * 1) Except where noted for specific fields, if the content
     *    of the field (following any leading whitespace,
     *    including comments) begins with any character other
     *    than the opening angle bracket '<', the field SHOULD
     *    be ignored.
     * 2) Any characters following an angle bracket enclosed URL
     *    SHOULD be ignored, unless a comma is the first
     *    non-whitespace/comment character after the closing
     *    angle bracket.
     * 3) If a sub-item (comma-separated item) within the field
     *    is not an angle-bracket enclosed URL, the remainder of
     *    the field (the current, and all subsequent sub-items)
     *    SHOULD be ignored. */
    /* RFC 2369: The client application should use the
     * left most protocol that it supports, or knows how to
     * access by a separate application. */
    while (*(url = rfc2822_skip_comments(url)) == '<') {
	gchar *close = strchr(++url, '>');
	if (!close)
	    /* broken syntax--break and return FALSE */
	    break;
	if (g_ascii_strncasecmp(url, "mailto:", 7) == 0) {
	    /* we support mailto! */
	    *close = '\0';
	    sendmsg_window_process_url(url + 7,
				       sendmsg_window_set_field, bsmsg);
	    return TRUE;
	}
	if (!(*++close && *(close = rfc2822_skip_comments(close)) == ','))
	    break;
	url = ++close;
    }
    return FALSE;
}

/* rfc2822_skip_comments:
 * skip CFWS (comments and folding white space) 
 *
 * CRLFs have already been stripped, so we need to look only for
 * comments and white space
 *
 * returns a pointer to the first character following the CFWS,
 * which may point to a '\0' character but is never a NULL pointer */
static gchar *
rfc2822_skip_comments(gchar * str)
{
    gint level = 0;

    while (*str) {
        if (*str == '(')
            /* start of a comment--they nest */
            ++level;
        else if (level > 0) {
            if (*str == ')')
                /* end of a comment */
                --level;
            else if (*str == '\\' && *++str == '\0')
                /* quoted-pair: we must test for the end of the string,
                 * which would be an error; in this case, return a
                 * pointer to the '\0' character following the '\\' */
                break;
        } else if (!(*str == ' ' || *str == '\t'))
            break;
        ++str;
    }
    return str;
}

/* Set the title for the compose window;
 *
 * handler for the "changed" signals of the "To:" address and the
 * "Subject:" field;
 *
 * also called directly from sendmsg_window_new.
 */
static void
sendmsg_window_set_title(BalsaSendmsg * bsmsg)
{
    gchar *title_format;
    gchar *title;

    if (libbalsa_address_entry_matching(LIBBALSA_ADDRESS_ENTRY(bsmsg->to[1])))
        return;

    switch (bsmsg->type) {
    case SEND_REPLY:
    case SEND_REPLY_ALL:
    case SEND_REPLY_GROUP:
        title_format = _("Reply to %s: %s");
        break;

    case SEND_FORWARD_ATTACH:
    case SEND_FORWARD_INLINE:
        title_format = _("Forward message to %s: %s");
        break;

    case SEND_CONTINUE:
        title_format = _("Continue message to %s: %s");
        break;

    default:
        title_format = _("New message to %s: %s");
        break;
    }

    title = g_strdup_printf(title_format,
                            gtk_entry_get_text(GTK_ENTRY(bsmsg->to[1])),
                            gtk_entry_get_text(GTK_ENTRY(bsmsg->subject[1])));
    gtk_window_set_title(GTK_WINDOW(bsmsg->window), title);
    g_free(title);
}

#ifdef HAVE_GPGME
static void
bsmsg_update_gpg_ui_on_ident_change(BalsaSendmsg *bsmsg,
                                    LibBalsaIdentity *ident)
{
    if (balsa_app.has_openpgp || balsa_app.has_smime) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_sign_menu_item),
                                       ident->gpg_sign);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_encrypt_menu_item),
                                       ident->gpg_encrypt);
    } else {
        GtkWidget *toolbar =
            balsa_toolbar_get_from_gnome_app(GNOME_APP(bsmsg->window));

        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_sign_menu_item),
                                       FALSE);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_encrypt_menu_item),
                                       FALSE);
        gtk_widget_set_sensitive(opts_menu[OPTS_MENU_SIGN_POS].widget, FALSE);
        gtk_widget_set_sensitive(opts_menu[OPTS_MENU_ENCRYPT_POS].widget, FALSE);
        balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_GPG_SIGN, FALSE);
        balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_GPG_ENCRYPT, FALSE);
    }
}

static void
bsmsg_setup_gpg_ui(BalsaSendmsg *bsmsg, GtkWidget *toolbar)
{
    bsmsg->gpg_sign_menu_item = opts_menu[OPTS_MENU_SIGN_POS].widget;
    g_object_set_data(G_OBJECT(bsmsg->gpg_sign_menu_item), "toolbar", toolbar);
    g_object_set_data(G_OBJECT(bsmsg->gpg_sign_menu_item), "radbut-1", 
		      gpg_mode_list[OPTS_MENU_GPG_3156_POS].widget);
    g_object_set_data(G_OBJECT(bsmsg->gpg_sign_menu_item), "radbut-2", 
		      gpg_mode_list[OPTS_MENU_GPG_2440_POS].widget);
    bsmsg->gpg_encrypt_menu_item = opts_menu[OPTS_MENU_ENCRYPT_POS].widget;
    g_object_set_data(G_OBJECT(bsmsg->gpg_encrypt_menu_item), "toolbar", 
                      toolbar);
    g_object_set_data(G_OBJECT(bsmsg->gpg_encrypt_menu_item), "radbut-1", 
		      gpg_mode_list[OPTS_MENU_GPG_3156_POS].widget);
    g_object_set_data(G_OBJECT(bsmsg->gpg_encrypt_menu_item), "radbut-2", 
		      gpg_mode_list[OPTS_MENU_GPG_2440_POS].widget);
    gtk_widget_set_sensitive(gpg_mode_list[OPTS_MENU_GPG_3156_POS].widget,
			     FALSE);
    gtk_widget_set_sensitive(gpg_mode_list[OPTS_MENU_GPG_2440_POS].widget,
			     FALSE);
#ifdef HAVE_SMIME
    g_object_set_data(G_OBJECT(bsmsg->gpg_sign_menu_item), "radbut-3", 
                      gpg_mode_list[OPTS_MENU_SMIME_POS].widget);
    g_object_set_data(G_OBJECT(bsmsg->gpg_encrypt_menu_item), "radbut-3", 
                      gpg_mode_list[OPTS_MENU_SMIME_POS].widget);
    gtk_widget_set_sensitive(gpg_mode_list[OPTS_MENU_SMIME_POS].widget,
                             FALSE);
#endif
    /* preset sign/encrypt according to current identity */
    if (balsa_app.has_openpgp || balsa_app.has_smime) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_sign_menu_item),
                                       bsmsg->ident->gpg_sign);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_encrypt_menu_item),
                                       bsmsg->ident->gpg_encrypt);
        switch (bsmsg->ident->crypt_protocol)
            {
            case LIBBALSA_PROTECT_OPENPGP:
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gpg_mode_list[OPTS_MENU_GPG_2440_POS].widget),
                                               TRUE);
                break;
#ifdef HAVE_SMIME
            case LIBBALSA_PROTECT_SMIMEV3:
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gpg_mode_list[OPTS_MENU_SMIME_POS].widget),
                                               TRUE);
                break;
#endif
            case LIBBALSA_PROTECT_RFC3156:
            default:
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gpg_mode_list[OPTS_MENU_GPG_3156_POS].widget),
                                               TRUE);
            }
    } else {
        GtkWidget *toolbar =
            balsa_toolbar_get_from_gnome_app(GNOME_APP(bsmsg->window));

        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_sign_menu_item),
                                       FALSE);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bsmsg->gpg_encrypt_menu_item),
                                       FALSE);
        gtk_widget_set_sensitive(opts_menu[OPTS_MENU_SIGN_POS].widget, FALSE);
        gtk_widget_set_sensitive(opts_menu[OPTS_MENU_ENCRYPT_POS].widget, FALSE);
        balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_GPG_SIGN, FALSE);
        balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_GPG_ENCRYPT, FALSE);
        balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_IDENTITY, g_list_length(balsa_app.identities)>1); 
   }
}
#endif /* HAVE_GPGME */

#if defined(ENABLE_TOUCH_UI)
static gboolean
bsmsg_check_format_compatibility(GtkWindow *parent, const gchar *filename)
{
    static const struct {
        const char *linux_extension, *linux_program;
        const char *other_extension, *other_program;
    } compatibility_table[] = {
        { ".abw",      "AbiWord",  ".doc", "Microsoft Word"  },
        { ".gnumeric", "Gnumeric", ".xls", "Microsoft Excel" }
    };
    GtkDialog *dialog;
    GtkWidget *label, *checkbox = NULL;
    unsigned i, fn_len = strlen(filename);
    int response;
    gchar *str;

    if(!balsa_app.do_file_format_check)
        return TRUE; /* blank accept from the User */

    for(i=0; i<ELEMENTS(compatibility_table); i++) {
        unsigned le_len = strlen(compatibility_table[i].linux_extension);
        int offset = fn_len - le_len;
        
        if(offset>0 &&
           strcmp(filename+offset, compatibility_table[i].linux_extension)==0)
            break; /* a match has been found */
    }
    if(i>=ELEMENTS(compatibility_table))
        return TRUE; /* no potential compatibility problems */

    /* time to ask the user for his/her opinion */
    dialog = (GtkDialog*)gtk_dialog_new_with_buttons
        ("Compatibility check", parent,
         GTK_DIALOG_MODAL| GTK_DIALOG_DESTROY_WITH_PARENT,
         GTK_STOCK_CANCEL,                   GTK_RESPONSE_CANCEL,
         "_Attach it in the current format", GTK_RESPONSE_OK, NULL);

    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);
    str = g_strdup_printf
        ("This file is currently in the %s's own format.\n"
         "If you need to send it to people who use %s, "
         "then open the file in the %s, use \"Save As\" "
         "on the \"File\" menu, and select the \"%s\" format. "
         "When you click \"OK\" it will save a new "
         "\"%s\" version of the file, with \"%s\" on the end "
         "of the document name, which you can then attach instead.",
         compatibility_table[i].linux_program,
         compatibility_table[i].other_program,
         compatibility_table[i].linux_program,
         compatibility_table[i].other_program,
         compatibility_table[i].other_program,
         compatibility_table[i].other_extension);
    gtk_box_set_spacing(GTK_BOX(dialog->vbox), 10);
    gtk_box_pack_start_defaults(GTK_BOX(dialog->vbox),
                       label = gtk_label_new(str));
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    g_free(str);
    checkbox = gtk_check_button_new_with_mnemonic
        ("_Do not show this dialog any more.");
    gtk_box_pack_start_defaults(GTK_BOX(dialog->vbox), checkbox);
    gtk_widget_show(checkbox);
    gtk_widget_show(label);
    response = gtk_dialog_run(dialog);
    balsa_app.do_file_format_check = 
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox));
    gtk_widget_destroy(GTK_WIDGET(dialog));
    return response == GTK_RESPONSE_OK;
}
#endif /* ENABLE_TOUCH_UI */
