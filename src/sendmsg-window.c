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
#include "i18n.h"
#include <ctype.h>
#include <glib.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include <sys/stat.h>		/* for check_if_regular_file() */
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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
static void toggle_sign_tb_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void toggle_encrypt_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void toggle_encrypt_tb_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
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
    {"ja_JP", "ISO-2022-JP",   N_("_Japanese (JIS)")},
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
			   attach_clicked, BALSA_PIXMAP_ATTACHMENT),
#define MENU_MSG_INCLUDE_POS 2
    GNOMEUIINFO_ITEM_STOCK(N_("I_nclude Message(s)"), NULL,
			   include_message_cb, BALSA_PIXMAP_COMPOSE),
#define MENU_FILE_ATTACH_MSG_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("Attach _Message(s)"), NULL,
			   attach_message_cb, BALSA_PIXMAP_FORWARD),
#define MENU_FILE_SEPARATOR1_POS 4
    GNOMEUIINFO_SEPARATOR,

#define MENU_FILE_SEND_POS 5
    { GNOME_APP_UI_ITEM, N_("Sen_d"),
      N_("Send this message"),
      send_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_SEND, GDK_Return, GDK_CONTROL_MASK, NULL },
#define MENU_FILE_QUEUE_POS 6
    { GNOME_APP_UI_ITEM, N_("_Queue"),
      N_("Queue this message in Outbox for sending"),
      queue_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_SEND, 'Q', GDK_CONTROL_MASK, NULL },
#define MENU_FILE_POSTPONE_POS 7
    GNOMEUIINFO_ITEM_STOCK(N_("_Postpone"), NULL,
			   postpone_message_cb, BALSA_PIXMAP_POSTPONE),
#define MENU_FILE_SAVE_POS 8
    { GNOME_APP_UI_ITEM, N_("_Save"),
      N_("Save this message"),
      save_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      GTK_STOCK_SAVE, 'S', GDK_CONTROL_MASK, NULL },
#define MENU_FILE_PRINT_POS 9
    GNOMEUIINFO_ITEM_STOCK(N_("_Print..."), N_("Print the edited message"),
			   print_message_cb, GTK_STOCK_PRINT),
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
                           BALSA_PIXMAP_IDENTITY),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_EDIT_GNOME EDIT_MENU_SELECT_IDENT + 2
    GNOMEUIINFO_ITEM_STOCK(N_("_Edit with Gnome-Editor"),
                           N_("Edit the current message with "
                              "the default Gnome editor"),
                           edit_with_gnome,
                           GTK_STOCK_EXECUTE), /*FIXME: Other icon */
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
			   include_message_cb, BALSA_PIXMAP_NEW),
#define MENU_FILE_ATTACH_MSG_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("Attach _Message(s)"), NULL,
			   attach_message_cb, BALSA_PIXMAP_FORWARD),
    GNOMEUIINFO_END
};

/* touchpad optimized version of the menu */
static GnomeUIInfo file_menu[] = {
#define MENU_FILE_ATTACH_POS 0
    GNOMEUIINFO_ITEM_STOCK(N_("_Attach File..."), NULL,
			   attach_clicked, BALSA_PIXMAP_ATTACHMENT),
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_SAVE_POS 2
    { GNOME_APP_UI_ITEM, N_("_Save"),
      N_("Save this message"),
      save_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      GTK_STOCK_SAVE, 'S', GDK_CONTROL_MASK, NULL },
#define MENU_FILE_PRINT_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("_Print..."), N_("Print the edited message"),
			   print_message_cb, GTK_STOCK_PRINT),
    GNOMEUIINFO_SUBTREE(N_("_More"), tu_file_more_menu),
#define MENU_FILE_POSTPONE_POS 5
    GNOMEUIINFO_ITEM_STOCK(N_("Sa_ve and Close"), NULL,
			   postpone_message_cb, BALSA_PIXMAP_POSTPONE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_SEND_POS 7
    { GNOME_APP_UI_ITEM, N_("Sen_d"),
      N_("Send this message"),
      send_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_SEND, GDK_Return, GDK_CONTROL_MASK, NULL },
#define MENU_FILE_QUEUE_POS 8
    { GNOME_APP_UI_ITEM, N_("Send _Later"),
      N_("Queue this message in Outbox for sending"),
      queue_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_SEND, 'Q', GDK_CONTROL_MASK, NULL },
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
                           BALSA_PIXMAP_IDENTITY),
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


/* ===================================================================
 *                attachment related stuff
 * =================================================================== */

enum {
    ATTACH_INFO_COLUMN = 0,
    ATTACH_ICON_COLUMN,
    ATTACH_TYPE_COLUMN,
    ATTACH_MODE_COLUMN,
    ATTACH_SIZE_COLUMN,
    ATTACH_DESC_COLUMN,
    ATTACH_NUM_COLUMNS
};

typedef struct _BalsaAttachInfo BalsaAttachInfo;
typedef struct _BalsaAttachInfoClass BalsaAttachInfoClass;

static gchar * attach_modes[] =
    {NULL, N_("Attachment"), N_("Inline"), N_("Reference") };

struct _BalsaAttachInfo {
    GObject parent_object;

    BalsaSendmsg *bm;                 /* send message back reference */

    GtkWidget *popup_menu;            /* popup menu */
    gchar *filename;                  /* file name of the attachment */
    gchar *force_mime_type;           /* force using this particular mime type */
    gchar *charset;                   /* forced character set */
    gboolean delete_on_destroy;       /* destroy the file when not used any more */
    gint mode;                        /* LIBBALSA_ATTACH_AS_ATTACHMENT etc. */
    LibBalsaMessageHeaders *headers;  /* information about a forwarded message */
};

struct _BalsaAttachInfoClass {
    GObjectClass parent_class;
};


static GType balsa_attach_info_get_type();
static void balsa_attach_info_init(GObject *object, gpointer data);
static BalsaAttachInfo* balsa_attach_info_new();
static void balsa_attach_info_destroy(GObject * object);


#define BALSA_MSG_ATTACH_MODEL(x)   gtk_tree_view_get_model(GTK_TREE_VIEW((x)->attachments[1]))


#define TYPE_BALSA_ATTACH_INFO          \
        (balsa_attach_info_get_type ())
#define BALSA_ATTACH_INFO(obj)          \
        (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_BALSA_ATTACH_INFO, BalsaAttachInfo))
#define IS_BALSA_ATTACH_INFO(obj)       \
        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_BALSA_ATTACH_INFO))

static void
balsa_attach_info_class_init(BalsaAttachInfoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    
    object_class->finalize = balsa_attach_info_destroy;
}

static GType
balsa_attach_info_get_type()
{
    static GType balsa_attach_info_type = 0 ;

    if (!balsa_attach_info_type) {
        static const GTypeInfo balsa_attach_info_info =
            {
                sizeof (BalsaAttachInfoClass),
                (GBaseInitFunc) NULL,
                (GBaseFinalizeFunc) NULL,
                (GClassInitFunc) balsa_attach_info_class_init,
                (GClassFinalizeFunc) NULL,
                NULL,
                sizeof(BalsaAttachInfo),
                0,
                (GInstanceInitFunc) balsa_attach_info_init
            };
        balsa_attach_info_type = 
           g_type_register_static (G_TYPE_OBJECT, "BalsaAttachInfo",
                                   &balsa_attach_info_info, 0);
    }
    return balsa_attach_info_type;
}

static void
balsa_attach_info_init(GObject *object, gpointer data)
{
    BalsaAttachInfo * info = BALSA_ATTACH_INFO(object);
    
    info->popup_menu = NULL;
    info->filename = NULL;
    info->force_mime_type = NULL;
    info->charset = NULL;
    info->delete_on_destroy = FALSE;
    info->mode = LIBBALSA_ATTACH_AS_ATTACHMENT;
    info->headers = NULL;
}

static BalsaAttachInfo*
balsa_attach_info_new(BalsaSendmsg *bm) 
{
    BalsaAttachInfo * info = g_object_new(TYPE_BALSA_ATTACH_INFO, NULL);

    info->bm = bm;
    return info;
}

static void
balsa_attach_info_destroy(GObject * object)
{
    BalsaAttachInfo * info;
    GObjectClass *parent_class;

    g_return_if_fail(object != NULL);
    g_return_if_fail(IS_BALSA_ATTACH_INFO(object));
    info = BALSA_ATTACH_INFO(object);

    /* unlink the file if necessary */
    if (info->delete_on_destroy) {
	char *last_slash = strrchr(info->filename, '/');

	if (balsa_app.debug)
	    fprintf (stderr, "%s:%s: unlink `%s'\n", __FILE__, __FUNCTION__,
		     info->filename);
	unlink(info->filename);
	*last_slash = 0;
	if (balsa_app.debug)
	    fprintf (stderr, "%s:%s: rmdir `%s'\n", __FILE__, __FUNCTION__,
		     info->filename);
	rmdir(info->filename);
    }

    /* clean up memory */
    if (info->popup_menu)
        gtk_widget_destroy(info->popup_menu);
    g_free(info->filename);
    g_free(info->force_mime_type);
    g_free(info->charset);
    libbalsa_message_headers_destroy(info->headers);

    parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    parent_class->finalize(object);    
}

/* ===================================================================
 *                end of attachment related stuff
 * =================================================================== */


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
    g_signal_handler_disconnect(G_OBJECT(balsa_app.main_window),
                                bsmsg->identities_changed_id);
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
    g_slist_foreach(bsmsg->charsets, (GFunc) g_free, NULL);
    g_slist_free(bsmsg->charsets);

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
    /* Cannot edit the new "From:" header. 
    { N_("From:"),     G_STRUCT_OFFSET(BalsaSendmsg, from[1])}, */
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
#if HAVE_GNOME_VFS29
        gboolean adduri = gnome_vfs_mime_application_supports_uris(app);
	const gchar *exec, *pct;
#else /* HAVE_GNOME_VFS29 */
	gboolean adduri = (app->expects_uris ==
                           GNOME_VFS_MIME_APPLICATION_ARGUMENT_TYPE_URIS);
#endif /* HAVE_GNOME_VFS29 */
        argc = 2;
        argv = g_new0 (char *, argc+1);
#if HAVE_GNOME_VFS29
	exec = gnome_vfs_mime_application_get_exec(app);
	pct = strstr(exec, " %");
	argv[0] = pct ? g_strndup(exec, pct - exec) : g_strdup(exec);
#else /* HAVE_GNOME_VFS29 */
        argv[0] = g_strdup(app->command);
#endif /* HAVE_GNOME_VFS29 */
        argv[1] = g_strdup_printf("%s%s", adduri ? "file://" : "", filename);

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
    gchar* tmpstr;
    const gchar* subject;
    gint replen, fwdlen;
    
    LibBalsaIdentity* old_ident;

    
    g_return_if_fail(ident != NULL);


    /* change entries to reflect new identity */
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsmsg->from[1]),
                             g_list_index(balsa_app.identities, ident));

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

    libbalsa_address_entry_set_domain(LIBBALSA_ADDRESS_ENTRY(bsmsg->to[1]),
                                      ident->domain);
    libbalsa_address_entry_set_domain(LIBBALSA_ADDRESS_ENTRY(bsmsg->cc[1]),
                                      ident->domain);
    libbalsa_address_entry_set_domain(LIBBALSA_ADDRESS_ENTRY(bsmsg->bcc[1]),
                                      ident->domain);
}


static void
sw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc)
{
    balsa_app.sw_height = alloc->height;
    balsa_app.sw_width = alloc->width;
}


/* remove_attachment - right mouse button callback */
static void
remove_attachment(GtkWidget * menu_item, BalsaAttachInfo *info)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    BalsaAttachInfo *test_info;

    g_return_if_fail(info->bm != NULL);

    /* get the selected element */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(info->bm->attachments[1]));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    /* make sure we got the right element */
    gtk_tree_model_get(model, &iter, ATTACH_INFO_COLUMN, &test_info, -1);
    if (test_info != info) {
	if (test_info)
	    g_object_unref(test_info);
	return;
    }
    g_object_unref(test_info);
    
    /* remove the attachment */
    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
}

static void
set_attach_menu_sensitivity(GtkWidget * widget, gpointer data)
{
    gint mode =
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "new-mode"));

    if (mode)
        gtk_widget_set_sensitive(widget, mode != GPOINTER_TO_INT(data));
}

/* change attachment mode - right mouse button callback */
static void
change_attach_mode(GtkWidget * menu_item, BalsaAttachInfo *info)
{
    gint new_mode =
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item),
                                          "new-mode"));
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    BalsaAttachInfo *test_info;

    g_return_if_fail(info->bm != NULL);

    /* get the selected element */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(info->bm->attachments[1]));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    /* make sure we got the right element */
    gtk_tree_model_get(model, &iter, ATTACH_INFO_COLUMN, &test_info, -1);
    if (test_info != info) {
	if (test_info)
	    g_object_unref(test_info);
	return;
    }
    g_object_unref(test_info);
    
    /* verify that the user *really* wants to attach as reference */
    if (info->mode != new_mode && new_mode == LIBBALSA_ATTACH_AS_EXTBODY) {
	GtkWidget *extbody_dialog, *parent;
	gchar *utf8name;
	GError *err = NULL;
	gint result;

	parent = gtk_widget_get_ancestor(menu_item, GNOME_TYPE_APP);
	utf8name = g_filename_to_utf8(info->filename, -1, NULL, NULL, &err);
	if (err)
	    g_error_free(err);
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
				   utf8name);
	g_free(utf8name);
	gtk_window_set_title(GTK_WINDOW(extbody_dialog),
			     _("Attach as Reference?"));
	result = gtk_dialog_run(GTK_DIALOG(extbody_dialog));
	gtk_widget_destroy(extbody_dialog);
	if (result != GTK_RESPONSE_YES)
	    return;
    }
    
    /* change the attachment mode */
    info->mode = new_mode;
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, ATTACH_MODE_COLUMN,
		       info->mode, -1);

    /* set the menu's sensitivities */
    gtk_container_forall(GTK_CONTAINER(gtk_widget_get_parent(menu_item)),
			 set_attach_menu_sensitivity,
                         GINT_TO_POINTER(info->mode));
}


/* attachment vfs menu - right mouse button callback */
static void
attachment_menu_vfs_cb(GtkWidget * menu_item, BalsaAttachInfo * info)
{
    gchar *id;
    
    g_return_if_fail(info != NULL);

    if ((id = g_object_get_data (G_OBJECT (menu_item), "mime_action"))) {
#if HAVE_GNOME_VFS29
        GnomeVFSMimeApplication *app=
            gnome_vfs_mime_application_new_from_desktop_id(id);
#else /* HAVE_GNOME_VFS29 */
        GnomeVFSMimeApplication *app=
            gnome_vfs_mime_application_new_from_id(id);
#endif /* HAVE_GNOME_VFS29 */
        if (app) {
#if HAVE_GNOME_VFS29
            gchar *uri = g_strconcat("file://", info->filename, NULL);
            GList *uris = g_list_prepend(NULL, uri);
            gnome_vfs_mime_application_launch(app, uris);
            g_free(uri);
            g_list_free(uris);
#else /* HAVE_GNOME_VFS29 */
	    gboolean tmp =
		(app->expects_uris ==
		 GNOME_VFS_MIME_APPLICATION_ARGUMENT_TYPE_URIS);
	    gchar *exe_str =
		g_strdup_printf("%s \"%s%s\"", app->command,
				tmp ? "file://" : "", info->filename);
                
	    gnome_execute_shell(NULL, exe_str);
	    fprintf(stderr, "Executed: %s\n", exe_str);
	    g_free (exe_str);
#endif /* HAVE_GNOME_VFS29 */
	    gnome_vfs_mime_application_free(app);    
        } else {
            fprintf(stderr, "lookup for application %s returned NULL\n", id);
        }
    }
}


/* URL external body - right mouse button callback */
static void
on_open_url_cb(GtkWidget * menu_item, BalsaAttachInfo * info)
{
    GError *err = NULL;

    g_return_if_fail(info != NULL);

    g_message("open URL %s", info->filename + 4);
    //    gnome_url_show(info->filename + 4, &err);
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Error showing %s: %s\n"),
			  info->filename + 4, err->message);
        g_error_free(err);
    }
}


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

/* Ask the user for a charset; returns ((LibBalsaCodeset) -1) on cancel. */
static void
sw_charset_combo_box_changed(GtkComboBox * combo_box,
                             GtkWidget * charset_button)
{
    gtk_widget_set_sensitive(charset_button,
                             gtk_combo_box_get_active(combo_box) == 0);
}

static LibBalsaCodeset
sw_get_user_codeset(BalsaSendmsg * bsmsg, gboolean * change_type,
                    const gchar * mime_type, const char *fname)
{
    GtkWidget *combo_box = NULL;
    gint codeset = -1;
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(_("Choose charset"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    NULL);
    gchar *msg = g_strdup_printf
        (_("File\n%s\nis not encoded in US-ASCII or UTF-8.\n"
           "Please choose the charset used to encode the file."),
         fname);
    GtkWidget *info = gtk_label_new(msg);
    GtkWidget *charset_button = libbalsa_charset_button_new();

    g_free(msg);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), info,
                       FALSE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), charset_button,
                       TRUE, TRUE, 5);
    gtk_widget_show(info);
    gtk_widget_show(charset_button);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    if (change_type) {
        GtkWidget *label = gtk_label_new(_("Attach as MIME type:"));
        GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
        combo_box = gtk_combo_box_new_text();

        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                           TRUE, TRUE, 5);
        gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
        gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box), mime_type);
        gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box),
                                  "application/octet-stream");
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), 0);
        g_signal_connect(G_OBJECT(combo_box), "changed",
                         G_CALLBACK(sw_charset_combo_box_changed),
                         charset_button);
        gtk_box_pack_start(GTK_BOX(hbox), combo_box, TRUE, TRUE, 0);
        gtk_widget_show_all(hbox);
    }

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        if (change_type)
            *change_type =
                gtk_combo_box_get_active(GTK_COMBO_BOX(combo_box)) != 0;
        if (!change_type || !*change_type)
	    codeset = gtk_combo_box_get_active(GTK_COMBO_BOX(charset_button));
    }

    gtk_widget_destroy(dialog);
    return (LibBalsaCodeset) codeset;
}

static gboolean
sw_set_charset(BalsaSendmsg * bsmsg, const gchar * filename,
               const gchar * content_type, gboolean * change_type,
               gchar ** attach_charset)
{
    const gchar *charset;
    LibBalsaTextAttribute attr = libbalsa_text_attr_file(filename);

    if (attr == 0)
        charset = "us-ascii";
    else if (attr & LIBBALSA_TEXT_HI_UTF8)
        charset = "UTF-8";
    else {
        LibBalsaCodesetInfo *info;
        LibBalsaCodeset codeset =
            sw_get_user_codeset(bsmsg, change_type, content_type, filename);
        if (*change_type)
            return TRUE;
        if (codeset == (LibBalsaCodeset) (-1))
            return FALSE;

        info = &libbalsa_codeset_info[codeset];
        charset = info->std;
        if (info->win && (attr & LIBBALSA_TEXT_HI_CTRL)) {
            charset = info->win;
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Character set for file %s changed "
                                   "from \"%s\" to \"%s\"."), filename,
                                 info->std, info->win);
        }
    }
    *attach_charset = g_strdup(charset);

    return TRUE;
}


static LibBalsaMessageHeaders *
get_fwd_mail_headers(const gchar *mailfile)
{
    int fd;
    GMimeStream *stream;
    GMimeParser *parser;
    GMimeMessage *message;
    LibBalsaMessageHeaders *headers;

    /* try to open the mail file */
    if ((fd = open(mailfile, O_RDONLY)) == -1)
	return NULL;
    if ((stream = g_mime_stream_fs_new(fd)) == NULL) {
	close(fd);
	return NULL;
    }

    /* parse the file */
    parser = g_mime_parser_new();
    g_mime_parser_init_with_stream(parser, stream);
    message = g_mime_parser_construct_message (parser);
    g_object_unref (parser);
    g_object_unref(stream);
    close(fd);
	
    /* get the headers from the gmime message */
    headers = g_new0(LibBalsaMessageHeaders, 1);
    libbalsa_message_headers_from_gmime(headers, message);
    if (!headers->subject) {
	const gchar * subject = g_mime_message_get_subject(message);

	if (!subject)
	    headers->subject = g_strdup(_("(no subject)"));
	else
	    headers->subject =
		g_mime_utils_header_decode_text((guchar *) subject);
    }
    libbalsa_utf8_sanitize(&headers->subject,
			   balsa_app.convert_unknown_8bit,
			   NULL);

    /* unref the gmime message and return the information */
    g_object_unref(message);
    return headers;
}


/* add_attachment:
   adds given filename to the list.
   takes over the ownership of filename.
*/
gboolean
add_attachment(BalsaSendmsg * bsmsg, gchar *filename, 
               gboolean is_a_temp_file, const gchar *forced_mime_type)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaAttachInfo *attach_data;
    gboolean can_inline, is_fwd_message;
    gchar *content_type = NULL;
    gchar *err_bsmsg;
    gchar *utf8name;
    GError *err = NULL;
    GdkPixbuf *pixbuf;
    GtkWidget *menu_item;
    struct stat attach_stat;

    if (balsa_app.debug)
	fprintf(stderr, "Trying to attach '%s'\n", filename);
    if ( (err_bsmsg=check_if_regular_file(filename)) != NULL) {
        balsa_information(LIBBALSA_INFORMATION_ERROR, err_bsmsg);
	g_free(err_bsmsg);
	g_free(filename);
	return FALSE;
    }

#if defined(ENABLE_TOUCH_UI)
    if(!bsmsg_check_format_compatibility(GTK_WINDOW(bsmsg->window),
		                         filename)) {
	g_free(filename);
        return FALSE;
    }
#endif /* ENABLE_TOUCH_UI */

    /* get the pixbuf for the attachment's content type */
    is_fwd_message = forced_mime_type &&
	!g_ascii_strncasecmp(forced_mime_type, "message/", 8) && is_a_temp_file;
    if (is_fwd_message)
	content_type = g_strdup(forced_mime_type);
    pixbuf = 
	libbalsa_icon_finder(forced_mime_type, filename, &content_type,
			     GTK_ICON_SIZE_LARGE_TOOLBAR);
    if (!content_type)
	/* Last ditch. */
	content_type = g_strdup("application/octet-stream");
	
    /* create a new attachment info block */
    attach_data = balsa_attach_info_new(bsmsg);
    attach_data->charset = NULL;
    if (!g_ascii_strncasecmp(content_type, "text/", 5)) {
	gboolean change_type = FALSE;
	if (!sw_set_charset(bsmsg, filename, content_type,
			    &change_type, &attach_data->charset)) {
	    g_free(content_type);
	    g_free(attach_data);
	    g_free(filename);
	    return FALSE;
	}
	if (change_type) {
	    forced_mime_type = "application/octet-stream";
	    g_free(content_type);
	    content_type = g_strdup(forced_mime_type);
	}
    }
    
    if (is_fwd_message) {
	attach_data->headers = get_fwd_mail_headers(filename);
	if (!attach_data->headers)
	    utf8name = g_strdup(_("forwarded message"));
	else {
            gchar *tmp =
                internet_address_list_to_string(attach_data->headers->from,
                                                FALSE);
	    utf8name = g_strdup_printf(_("Message from %s, subject: \"%s\""),
				       tmp,
				       attach_data->headers->subject);
	    g_free(tmp);
	}
    } else {
	const gchar *home = g_getenv("HOME");

	if (home && !strncmp(filename, home, strlen(home))) {
	    utf8name = g_filename_to_utf8(filename + strlen(home) - 1, -1,
					  NULL, NULL, &err);
	    if (utf8name)
		*utf8name = '~';
	} else
	    utf8name = g_filename_to_utf8(filename, -1, NULL, NULL, &err);

	if (err) {
	    balsa_information(LIBBALSA_INFORMATION_WARNING,
			      _("Error converting \"%s\" to UTF-8: %s\n"),
			      filename, err->message);
	    g_error_free(err);
	}
    }

    /* determine the size of the attachment */
    if (stat(filename, &attach_stat) == -1)
	attach_stat.st_size = 0;
    
    model = BALSA_MSG_ATTACH_MODEL(bsmsg);
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);
    
    attach_data->filename = filename;
    attach_data->force_mime_type = g_strdup(forced_mime_type);
    
    attach_data->delete_on_destroy = is_a_temp_file;
    can_inline = !is_a_temp_file &&
	(!g_ascii_strncasecmp(content_type, "text/", 5) ||
	 !g_ascii_strncasecmp(content_type, "image/", 6));
    attach_data->mode = LIBBALSA_ATTACH_AS_ATTACHMENT;
    
    /* build the attachment's popup menu */
    attach_data->popup_menu = gtk_menu_new();

    /* only real text/... and image/... parts may be inlined */
    if (can_inline) {
	menu_item = 
	    gtk_menu_item_new_with_label(attach_modes[LIBBALSA_ATTACH_AS_INLINE]);
	g_object_set_data(G_OBJECT(menu_item), "new-mode",
			  GINT_TO_POINTER(LIBBALSA_ATTACH_AS_INLINE));
	g_signal_connect(G_OBJECT(menu_item), "activate",
			 GTK_SIGNAL_FUNC(change_attach_mode),
			 (gpointer)attach_data);
	gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			      menu_item);
    }

    /* all real files can be attachments */
    if (can_inline || !is_a_temp_file) {
	menu_item = 
	    gtk_menu_item_new_with_label(attach_modes[LIBBALSA_ATTACH_AS_ATTACHMENT]);
	gtk_widget_set_sensitive(menu_item, FALSE);
	g_object_set_data(G_OBJECT(menu_item), "new-mode",
			  GINT_TO_POINTER(LIBBALSA_ATTACH_AS_ATTACHMENT));
	g_signal_connect(G_OBJECT(menu_item), "activate",
			 GTK_SIGNAL_FUNC(change_attach_mode),
			 (gpointer)attach_data);
	gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			      menu_item);
    }

    /* real files may be references (external body) */
    if (!is_a_temp_file) {
	menu_item = 
	    gtk_menu_item_new_with_label(attach_modes[LIBBALSA_ATTACH_AS_EXTBODY]);
	g_object_set_data(G_OBJECT(menu_item), "new-mode",
			  GINT_TO_POINTER(LIBBALSA_ATTACH_AS_EXTBODY));
	g_signal_connect(G_OBJECT(menu_item), "activate",
			 GTK_SIGNAL_FUNC(change_attach_mode),
			 (gpointer)attach_data);
	gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			      menu_item);
    }
	
    /* an attachment can be removed */
    menu_item = 
	gtk_menu_item_new_with_label(_("Remove"));
    g_signal_connect(G_OBJECT (menu_item), "activate",
		     GTK_SIGNAL_FUNC(remove_attachment),
		     (gpointer)attach_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			  menu_item);
    
    /* add the usual vfs menu so the user can inspect what (s)he actually
       attached... (only for non-message attachments) */
    if (!is_fwd_message)
	libbalsa_fill_vfs_menu_by_content_type(GTK_MENU(attach_data->popup_menu),
					       content_type, 
					       GTK_SIGNAL_FUNC(attachment_menu_vfs_cb),
					       (gpointer)attach_data);
    gtk_widget_show_all(attach_data->popup_menu);

    /* append to the list store */
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
		       ATTACH_INFO_COLUMN, attach_data,
		       ATTACH_ICON_COLUMN, pixbuf,
		       ATTACH_TYPE_COLUMN, content_type,
		       ATTACH_MODE_COLUMN, attach_data->mode,
		       ATTACH_SIZE_COLUMN, attach_stat.st_size,
		       ATTACH_DESC_COLUMN, utf8name,
		       -1);
    g_object_unref(attach_data);
    g_object_unref(pixbuf);
    g_free(utf8name);
    g_free(content_type);
    
    show_attachment_widget(bsmsg);

    return TRUE;
}

/* add_urlref_attachment:
   adds given url as reference to the to the list.
   frees url.
*/
static gboolean
add_urlref_attachment(BalsaSendmsg * bsmsg, gchar *url)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaAttachInfo *attach_data;
    GdkPixbuf * pixbuf;
    GtkWidget *menu_item;

    if (balsa_app.debug)
	fprintf(stderr, "Trying to attach '%s'\n", url);

    /* get the pixbuf for the attachment's content type */
    pixbuf = gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
				    GTK_STOCK_JUMP_TO,
				    GTK_ICON_SIZE_MENU, NULL);
	
    /* create a new attachment info block */
    attach_data = balsa_attach_info_new(bsmsg);
    attach_data->charset = NULL;
    
    model = BALSA_MSG_ATTACH_MODEL(bsmsg);
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);
    
    attach_data->filename = g_strconcat("URL:", url, NULL);
    attach_data->force_mime_type = g_strdup("message/external-body");
    attach_data->delete_on_destroy = FALSE;
    attach_data->mode = LIBBALSA_ATTACH_AS_EXTBODY;
    
    /* build the attachment's popup menu - may only be removed */
    attach_data->popup_menu = gtk_menu_new();
    menu_item = 
	gtk_menu_item_new_with_label(_("Remove"));
    g_signal_connect(G_OBJECT (menu_item), "activate",
		     GTK_SIGNAL_FUNC(remove_attachment),
		     (gpointer)attach_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			  menu_item);
    
    /* add a separator and the usual vfs menu so the user can inspect what
       (s)he actually attached... (only for non-message attachments) */
    gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			  gtk_separator_menu_item_new());
    menu_item = 
	gtk_menu_item_new_with_label(_("Open..."));
    g_signal_connect(G_OBJECT (menu_item), "activate",
		     GTK_SIGNAL_FUNC(on_open_url_cb),
		     (gpointer)attach_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			  menu_item);
    gtk_widget_show_all(attach_data->popup_menu);

    /* append to the list store */
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
		       ATTACH_INFO_COLUMN, attach_data,
		       ATTACH_ICON_COLUMN, pixbuf,
		       ATTACH_TYPE_COLUMN, _("(URL)"),
		       ATTACH_MODE_COLUMN, attach_data->mode,
		       ATTACH_SIZE_COLUMN, 0,
		       ATTACH_DESC_COLUMN, url,
		       -1);
    g_object_unref(attach_data);
    g_object_unref(pixbuf);
    g_free(url);
    
    show_attachment_widget(bsmsg);

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
attach_dialog_response(GtkWidget * dialog, gint response,
	               BalsaSendmsg * bsmsg)
{
    GtkFileChooser * fc;
    GSList *files, *list;
    int res = 0;

    if (response != GTK_RESPONSE_OK) {
	gtk_widget_destroy(dialog);
	return;
    }

    fc = GTK_FILE_CHOOSER(dialog);
    files = gtk_file_chooser_get_filenames(fc);
    for (list = files; list; list = list->next)
        if(!add_attachment(bsmsg, list->data, FALSE, NULL))
	    res++;

    /* add_attachment takes ownership of the filenames. */
    g_slist_free(files);
    
    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir = gtk_file_chooser_get_current_folder(fc);

    if (res == 0)
        gtk_widget_destroy(dialog);
}

/* attach_clicked - menu and toolbar callback */
static void
attach_clicked(GtkWidget * widget, gpointer data)
{
    GtkWidget *fsw;
    GtkFileChooser *fc;
    BalsaSendmsg *bsmsg;

    bsmsg = data;

    fsw =
        gtk_file_chooser_dialog_new(_("Attach file"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(fsw), TRUE);

    fc = GTK_FILE_CHOOSER(fsw);
    gtk_file_chooser_set_select_multiple(fc, TRUE);
    if (balsa_app.attach_dir)
	gtk_file_chooser_set_current_folder(fc, balsa_app.attach_dir);

    g_signal_connect(G_OBJECT(fc), "response",
		     G_CALLBACK(attach_dialog_response), bsmsg);

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
    int	length;
    
    if (!linebreak || linebreak[1] != '\n')
        return list;
    
    length = linebreak - uri_list;

    if (length && uri_list[0] != '#' && strncmp(uri_list,"file://",7)==0) {
	gchar *this_uri = g_strndup(uri_list, length);
	gchar *uri;

	uri = g_filename_from_uri(this_uri, NULL, NULL);
	g_free(this_uri);
	if (uri)
	    list = g_slist_append(list, uri);
      }

    uri_list = linebreak + 2;
  }
  return list;
}

/* Helper: check if the passed parameter contains a valid RFC 2396 URI (leading
 * & trailing whitespaces allowed). Return a newly allocated string with the
 * spaces stripped on success or NULL on fail. Note that the URI may still be
 * malformed. */
static gchar *
rfc2396_uri(const gchar *instr)
{
    gchar *s1, *uri;
    static const gchar *uri_extra = ";/?:@&=+$,-_.!~*'()%";

    /* remove leading and trailing whitespaces */
    uri = g_strchomp(g_strchug(g_strdup(instr)));

    /* check that the string starts with ftp[s]:// or http[s]:// */
    if (g_ascii_strncasecmp(uri, "ftp://", 6) &&
	g_ascii_strncasecmp(uri, "ftps://", 7) &&
	g_ascii_strncasecmp(uri, "http://", 7) &&
	g_ascii_strncasecmp(uri, "https://", 8)) {
	g_free(uri);
	return NULL;
    }

    /* verify that the string contains only valid chars (see rfc 2396) */
    s1 = uri + 6;   /* skip verified beginning */
    while (*s1 != '\0') {
	if (!g_ascii_isalnum(*s1) && !strchr(uri_extra, *s1)) {
	    g_free(uri);
	    return NULL;
	}
	s1++;
    }
    
    /* success... */
    return uri;
}

static void
attachments_add(GtkWidget * widget,
		GdkDragContext * context,
		gint x,
		gint y,
		GtkSelectionData * selection_data,
		guint info, guint32 time, BalsaSendmsg * bsmsg)
{
    gboolean drag_result = TRUE;

    if (balsa_app.debug)
        printf("attachments_add: info %d\n", info);
    if (info == TARGET_MESSAGES) {
	BalsaIndex *index = *(BalsaIndex **) selection_data->data;
	LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
	gint i;
        
	for (i = index->selected->len; --i >= 0;) {
	    guint msgno = g_array_index(index->selected, guint, i);
	    LibBalsaMessage *message =
		libbalsa_mailbox_get_message(mailbox, msgno);
            if(!attach_message(bsmsg, message))
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Attaching message failed.\n"
                                       "Possible reason: not enough temporary space"));
	    g_object_unref(message);
        }
    } else if (info == TARGET_URI_LIST) {
        GSList *uri_list = uri2gslist((gchar *) selection_data->data);
        for (; uri_list; uri_list = g_slist_next(uri_list)) {
	    add_attachment(bsmsg,
			   uri_list->data, FALSE, NULL); /* steal strings */
        }
        g_slist_free(uri_list);
    } else if( info == TARGET_STRING) {
	gchar *url = rfc2396_uri((gchar *) selection_data->data);

	if (url)
	    add_urlref_attachment(bsmsg, url);
	else
	    drag_result = FALSE;
    }	
    gtk_drag_finish(context, drag_result, FALSE, time);
}

/* to_add - e-mail (To, From, Cc, Bcc) field D&D callback */
static void
to_add(GtkWidget * widget,
       GdkDragContext * context,
       gint x,
       gint y,
       GtkSelectionData * selection_data,
       guint info, guint32 time)
{
    append_comma_separated(GTK_EDITABLE(widget),
	                   (gchar *) selection_data->data);
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

static void
sw_combo_box_changed(GtkComboBox * combo_box, BalsaSendmsg * bsmsg)
{
    gint active = gtk_combo_box_get_active(combo_box);
    LibBalsaIdentity *ident =
        g_list_nth_data(balsa_app.identities, active);

    update_bsmsg_identity(bsmsg, ident);
}

static void
create_from_entry(GtkWidget * table, BalsaSendmsg * bsmsg)
{
    GList *list;
    GtkListStore *store;
    GtkCellRenderer *renderer;

    store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    for (list = balsa_app.identities; list; list = list->next) {
        LibBalsaIdentity *ident = list->data;
        gchar *from = internet_address_to_string(ident->ia, FALSE);
	gchar *name = g_strconcat("(", ident->identity_name, ")", NULL);
        GtkTreeIter iter;

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, from, 1, name, -1);
        g_free(from);
        g_free(name);
    }
    bsmsg->from[1] = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(bsmsg->from[1]), renderer,
                               TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(bsmsg->from[1]),
                                   renderer, "text", 0, NULL);
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(bsmsg->from[1]), renderer,
	                       FALSE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(bsmsg->from[1]),
                                   renderer, "text", 1, NULL);
    g_object_unref(store);
    g_signal_connect(bsmsg->from[1], "changed",
                     G_CALLBACK(sw_combo_box_changed), bsmsg);
    create_email_or_string_entry(table, _("F_rom:"), 0, bsmsg->from);
}

static gboolean 
attachment_button_press_cb(GtkWidget * widget, GdkEventButton * event,
			   gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreePath *path;

    g_return_val_if_fail(event, FALSE);
    if (event->type != GDK_BUTTON_PRESS || event->button != 3
        || event->window != gtk_tree_view_get_bin_window(tree_view))
        return FALSE;

    if (gtk_tree_view_get_path_at_pos(tree_view, event->x, event->y,
                                      &path, NULL, NULL, NULL)) {
        GtkTreeIter iter;
        GtkTreeSelection * selection =
            gtk_tree_view_get_selection(tree_view);
        GtkTreeModel * model = gtk_tree_view_get_model(tree_view);

	gtk_tree_selection_unselect_all(selection);
	gtk_tree_selection_select_path(selection, path);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree_view), path, NULL,
				 FALSE);
	if (gtk_tree_model_get_iter (model, &iter, path)) {
	    BalsaAttachInfo *attach_info;

	    gtk_tree_model_get(model, &iter, ATTACH_INFO_COLUMN, &attach_info, -1);
	    if (attach_info) {
		if (attach_info->popup_menu)
		    gtk_menu_popup(GTK_MENU(attach_info->popup_menu), NULL, NULL,
				   NULL, NULL, event->button, event->time);
		g_object_unref(attach_info);
	    }
        }
        gtk_tree_path_free(path);
    }

    return TRUE;
}


static gboolean
attachment_popup_cb(GtkWidget *widget, gpointer user_data)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaAttachInfo *attach_info;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return FALSE;
    
    gtk_tree_model_get(model, &iter, ATTACH_INFO_COLUMN, &attach_info, -1);
    if (attach_info) {
	if (attach_info->popup_menu)
	gtk_menu_popup(GTK_MENU(attach_info->popup_menu), NULL, NULL, NULL,
		       NULL, 0, gtk_get_current_event_time());
	g_object_unref(attach_info);
    }
	
    return TRUE;
}


static void
render_attach_mode(GtkTreeViewColumn *column, GtkCellRenderer *cell,
		   GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gint mode;

    gtk_tree_model_get(model, iter, ATTACH_MODE_COLUMN, &mode, -1);
    g_object_set(cell, "text", attach_modes[mode], NULL);
}


static void
render_attach_size(GtkTreeViewColumn *column, GtkCellRenderer *cell,
		   GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gint mode;
    gint size;
    gchar *sstr;

    gtk_tree_model_get(model, iter, ATTACH_MODE_COLUMN, &mode,
		       ATTACH_SIZE_COLUMN, &size, -1);
    if (mode == LIBBALSA_ATTACH_AS_EXTBODY) {
	g_object_set(cell, "text", "-", NULL);
	return;
    }

    if (size > 1200000)
	sstr = g_strdup_printf("%.2fMB", (gfloat)size / (gfloat)(1024 * 1024));
    else if (size > 1200)
	sstr = g_strdup_printf("%.2fkB", (gfloat)size / (gfloat)1024);
    else
	sstr = g_strdup_printf("%dB", size);
    g_object_set(cell, "text", sstr, NULL);
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
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeView *view;
    GtkTreeViewColumn *column;

    bsmsg->header_table = table = gtk_table_new(11, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);

    /* bsmsg->bad_address_style will be set in create_email_entry: */
    bsmsg->bad_address_style = NULL;

    /* From: */
    create_from_entry(table, bsmsg);

    /* To: */
    create_email_entry(table, _("_To:"), 1, BALSA_PIXMAP_BOOK_RED,
                       bsmsg, bsmsg->to,
                       &bsmsg->to_info, 1, -1);
    g_signal_connect_swapped(G_OBJECT(bsmsg->to[1]), "changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);

    /* Subject: */
    create_string_entry(table, _("S_ubject:"), 2, bsmsg->subject);
    g_signal_connect_swapped(G_OBJECT(bsmsg->subject[1]), "changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);
    /* cc: */
    create_email_entry(table, _("Cc:"), 3, BALSA_PIXMAP_BOOK_YELLOW,
                       bsmsg, bsmsg->cc,
                       &bsmsg->cc_info, 0, -1);

    /* bcc: */
    create_email_entry(table, _("Bcc:"), 4, BALSA_PIXMAP_BOOK_GREEN,
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
    create_email_entry(table, _("_Reply To:"), 6, BALSA_PIXMAP_BOOK_BLUE,
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

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

    store = gtk_list_store_new(ATTACH_NUM_COLUMNS,
			       TYPE_BALSA_ATTACH_INFO,
			       GDK_TYPE_PIXBUF,
			       G_TYPE_STRING,
			       G_TYPE_INT,
			       G_TYPE_INT,
			       G_TYPE_STRING);

    bsmsg->attachments[1] = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    view = GTK_TREE_VIEW(bsmsg->attachments[1]);
    gtk_tree_view_set_headers_visible(view, TRUE);
    gtk_tree_view_set_rules_hint(view, TRUE);
    g_object_unref(store);

    /* column for type icon */
    renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set(G_OBJECT(renderer), "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes(view,
						-1, NULL, renderer,
						"pixbuf", ATTACH_ICON_COLUMN,
						NULL);

    /* column for the mime type */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes(view,
						-1, _("Type"), renderer,
						"text",	ATTACH_TYPE_COLUMN,
						NULL);

    /* column for the attachment mode */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "xalign", 0.0, NULL);
    column = gtk_tree_view_column_new_with_attributes(_("Mode"), renderer,
						      "text", ATTACH_MODE_COLUMN,
						      NULL);
    gtk_tree_view_column_set_cell_data_func(column,
					    renderer, render_attach_mode,
                                            NULL, NULL);
    gtk_tree_view_append_column(view, column);

    /* column for the attachment size */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
    column = gtk_tree_view_column_new_with_attributes(_("Size"), renderer,
						      "text", ATTACH_SIZE_COLUMN,
						      NULL);
    gtk_tree_view_column_set_cell_data_func(column,
					    renderer, render_attach_size,
                                            NULL, NULL);
    gtk_tree_view_append_column(view, column);

    /* column for the file type/description */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes(view,
						-1, _("Description"), renderer,
						"text", ATTACH_DESC_COLUMN,
						NULL);

    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(view),
				GTK_SELECTION_SINGLE);
    g_signal_connect(view, "popup-menu",
                     G_CALLBACK(attachment_popup_cb), NULL);
    g_signal_connect(view, "button_press_event",
                     G_CALLBACK(attachment_button_press_cb), NULL);

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

    bsmsg->attachments[2] = sw;
    bsmsg->attachments[3] = frame;

    gtk_widget_show_all(table);
    hide_attachment_widget(bsmsg);
    return table;
}

typedef struct {
    gchar * name;
    gboolean found;
} has_file_attached_t;

static gboolean
has_file_attached(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
		  gpointer data)
{
    has_file_attached_t *find_file = (has_file_attached_t *)data;
    BalsaAttachInfo *info;

    gtk_tree_model_get(model, iter, ATTACH_INFO_COLUMN, &info, -1);
    if (!info)
	return FALSE;
    if (!strcmp(find_file->name, info->filename))
	find_file->found = TRUE;
    g_object_unref(info);

    return find_file->found;
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
    GtkTextBuffer *buffer;
    BalsaIndex *index;
    LibBalsaMailbox *mailbox;
    gint i;

    if (context->action == GDK_ACTION_ASK)
        context->action = GDK_ACTION_COPY;

    switch(info) {
    case TARGET_MESSAGES:
	index = *(BalsaIndex **) selection_data->data;
	mailbox = index->mailbox_node->mailbox;
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
       
	for (i = index->selected->len; --i >= 0;) {
	    guint msgno = g_array_index(index->selected, guint, i);
	    LibBalsaMessage *message;
            GString *body;

	    message = libbalsa_mailbox_get_message(mailbox, msgno);
            body = quoteBody(bsmsg, message, SEND_REPLY);
	    g_object_unref(message);
            libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
            g_string_free(body, TRUE);
        }
        break;
    case TARGET_URI_LIST: {
        GSList *uri_list = uri2gslist((gchar *) selection_data->data);
        for (; uri_list; uri_list = g_slist_next(uri_list)) {
            /* Since current GtkTextView gets this signal twice for
             * every action (#150141) we need to check for duplicates,
             * which is a good idea anyway. */
	    has_file_attached_t find_file;

	    find_file.name = uri_list->data;
	    find_file.found = FALSE;
	    gtk_tree_model_foreach(BALSA_MSG_ATTACH_MODEL(bsmsg),
				   has_file_attached, &find_file);
            if (!find_file.found)
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
    gtk_text_view_set_wrap_mode(text_view, GTK_WRAP_WORD_CHAR);

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

/* Check whether the string can be converted. */
static gboolean
sw_can_convert(const gchar * string, gssize len,
               const gchar * to_codeset, const gchar * from_codeset,
               gchar ** result)
{
    gsize bytes_read, bytes_written;
    GError *err = NULL;
    gchar *s;

    s = g_convert(string, len, to_codeset, from_codeset,
                  &bytes_read, &bytes_written, &err);
    if (err) {
        g_error_free(err);
        g_free(s);
        s = NULL;
    }

    if (result)
        *result = s;
    else
        g_free(s);

    return !err;
}

/*
 * bsmsg->charsets is a list of charsets associated with quoted or
 * included messages, and included files; we use GMime's
 * "iconv-friendly" name and try to avoid duplicates; the first charset
 * that works is used, and we prepend new choices, so the priority is:
 * - the user's new language choice;
 * - the charsets of quoted and included text;
 * - the user's default language.
 */
static void
sw_prepend_charset(BalsaSendmsg * bsmsg, const gchar * charset)
{
    const gchar *charset_iconv;

    if (!charset || g_ascii_strcasecmp(charset, "UTF-8") == 0)
	return;

    charset_iconv = g_mime_charset_iconv_name(charset);
    if (!g_slist_find_custom(bsmsg->charsets, charset_iconv,
                             (GCompareFunc) strcmp))
        bsmsg->charsets =
            g_slist_prepend(bsmsg->charsets, g_strdup(charset_iconv));
}

static void
sw_charset_cb(const gchar * charset, gpointer data)
{
    sw_prepend_charset((BalsaSendmsg *) data, charset);
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
                                          bsmsg->flow, sw_charset_cb,
					  bsmsg))) {
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
		libbalsa_message_body_save(body, name, FALSE);
	    } else {
		fd = g_file_open_tmp("balsa-continue-XXXXXX", &name, NULL);
		libbalsa_message_body_save_fd(body, fd, FALSE);
	    }
	    body_type = libbalsa_message_body_get_mime_type(body);
	    add_attachment(bsmsg, name, TRUE, body_type);
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
	(orig_address =
	 libbalsa_address_get_name_from_list(message->headers->from))) {
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
	    gchar *from =
		internet_address_list_to_string(message->headers->from,
			                        FALSE);
	    g_string_append_printf(body, "%s %s\n", _("From:"), from);
	    g_free(from);
	}

	if (message->headers->to_list) {
	    gchar *to_list =
		internet_address_list_to_string(message->headers->to_list,
			                        FALSE);
	    g_string_append_printf(body, "%s %s\n", _("To:"), to_list);
	    g_free(to_list);
	}

	if (message->headers->cc_list) {
	    gchar *cc_list = 
		internet_address_list_to_string(message->headers->cc_list,
			                        FALSE);
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
			     balsa_app.reply_strip_html, bsmsg->flow,
			     sw_charset_cb, bsmsg);
	if (body) {
	    gchar *buf;

	    buf = g_string_free(body, FALSE);
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
    } else
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("No signature found!"));
    
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
	    if (message->headers && message->headers->from)
		newsubject = g_strdup_printf("%s from %s",
					     ident->forward_string,
					     libbalsa_address_get_mailbox_from_list
						 (message->headers->from));
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
	    if (message->headers && message->headers->from)
		newsubject = 
		    g_strdup_printf("%s %s [%s]",
				    ident->forward_string, 
				    tmp,
                                    libbalsa_address_get_mailbox_from_list
					(message->headers->from));
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
                            InternetAddressList * list)
{
    if (list) {
	gchar* tmp = internet_address_list_to_string(list, FALSE);
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
    const gchar *tmp;


    if( !message  || !message->headers || !message->headers->to_list ||
	!balsa_app.identities)
        return FALSE; /* use default */

    if (bsmsg->type == SEND_CONTINUE) {
 	if (message->headers->from) {
 	    /*
 	    * Look for an identity that matches the From: address.
 	    */
 	    address_string =
		libbalsa_address_get_mailbox_from_list(message->headers->
                                                       from);
 	    for (ilist = balsa_app.identities; ilist;
 		 ilist = g_list_next(ilist)) {
 		ident = LIBBALSA_IDENTITY(ilist->data);
 		if ((tmp = ident->ia->value.addr)
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
 	InternetAddressList *alist;
 
 	/*
 	* Loop through all the addresses in the message's To:
 	* field, and look for an identity that matches one of them.
 	*/
 	for (alist = message->headers->to_list; alist; alist = alist->next) {
	    if (!(address_string =
	          libbalsa_address_get_mailbox_from_list(alist)))
		continue;
 	    for (ilist = balsa_app.identities; ilist;
 		 ilist = g_list_next(ilist)) {
 		ident = LIBBALSA_IDENTITY(ilist->data);
 		if ((tmp = ident->ia->value.addr)
 		    && !g_ascii_strcasecmp(address_string, tmp)) {
 		    bsmsg->ident = ident;
 		    return TRUE;
 		}
 	    }
 	}
 
 	/* No match in the to_list, try the cc_list */
 	for (alist = message->headers->cc_list; alist; alist = alist->next) {
	    if (!(address_string =
		  libbalsa_address_get_mailbox_from_list(alist)))
		continue;
 	    for (ilist = balsa_app.identities; ilist;
 		 ilist = g_list_next(ilist)) {
 		ident = LIBBALSA_IDENTITY(ilist->data);
 		if ((tmp = ident->ia->value.addr)
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
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsmsg->from[1]),
                             g_list_index(balsa_app.identities, ident));
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
    {GTK_STOCK_PRINT,          BALSA_TOOLBAR_FUNC(print_message_cb)},
    {GTK_STOCK_SAVE,           BALSA_TOOLBAR_FUNC(save_message_cb)},
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
    GTK_STOCK_SAVE,
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
    GTK_STOCK_SAVE,
    "",
    GTK_STOCK_UNDO,
    GTK_STOCK_REDO,
    "",
    BALSA_PIXMAP_IDENTITY,
    "",
    GTK_STOCK_SPELL_CHECK,
    "",
    GTK_STOCK_PRINT,
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
bsmsg_identities_changed_cb(BalsaSendmsg *bsmsg)
{
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(bsmsg->window));
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_IDENTITY,
                                       g_list_length(balsa_app.identities)>1);
}

static void
sw_cc_add_list(GString * cc, InternetAddressList * list)
{
    for (; list; list = list->next) {
        InternetAddress *ia;

        if ((ia = list->address)) {
            GList *ident;
            gchar *tmp;

            /* do not insert any of my identities into the cc: list */
            for (ident = balsa_app.identities; ident; ident = ident->next)
                if (libbalsa_ia_rfc2821_equal
                    (ia, LIBBALSA_IDENTITY(ident->data)->ia))
                    break;
            if (!ident && (tmp = internet_address_to_string(ia, FALSE))) {
                if (cc->len > 0)
                    g_string_append(cc, ", ");
                g_string_append(cc, tmp);
                g_free(tmp);
            }
        }
    }
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
    bsmsg->charsets  = NULL;
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
    bsmsg->identities_changed_id = 
        g_signal_connect_swapped(balsa_app.main_window, "identities-changed",
                                 (GCallback)bsmsg_identities_changed_cb,
                                 bsmsg);
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
    if (message) {
	libbalsa_message_body_ref(message, TRUE, TRUE);
#ifdef HAVE_GPGME
	/* scan the message for encrypted parts - this is only possible if
	   there is *no* other ref to it */
	balsa_message_perform_crypto(message, LB_MAILBOX_CHK_CRYPT_ALWAYS,
				     TRUE, 1);
#endif
    }

    /* To: */
    if (type == SEND_REPLY || type == SEND_REPLY_ALL) {
        InternetAddressList *addr =
            (message->headers->reply_to) 
	    ? message->headers->reply_to : message->headers->from;

        if (addr) {
            tmp = internet_address_list_to_string(addr, FALSE);
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

#if !defined(ENABLE_TOUCH_UI)
	if (message->headers->reply_to != NULL) {
	    tmp = internet_address_list_to_string(message->headers->reply_to,
			                          FALSE);
 	    libbalsa_utf8_sanitize(&tmp, balsa_app.convert_unknown_8bit,
 				   NULL);
	    gtk_entry_set_text(GTK_ENTRY(bsmsg->reply_to[1]), tmp);
	    g_free(tmp);
	}
#endif
    }

    if (type == SEND_REPLY_ALL) {
	GString *new_cc = g_string_new("");

	sw_cc_add_list(new_cc, message->headers->to_list);
	sw_cc_add_list(new_cc, message->headers->cc_list);

	tmp = g_string_free(new_cc, FALSE);
	libbalsa_utf8_sanitize(&tmp, balsa_app.convert_unknown_8bit,
			       NULL);
	gtk_entry_set_text(GTK_ENTRY(bsmsg->cc[1]), tmp);
	g_free(tmp);
    }
    gnome_app_set_contents(GNOME_APP(window), main_box);

    /* set the menus - and language index */
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

    if (bsmsg->ident->signature_path == NULL||
        *bsmsg->ident->signature_path == '\0')
	return NULL;

    path = libbalsa_expand_path(bsmsg->ident->signature_path);
    if(bsmsg->ident->sig_executable){
        /* signature is executable */
	fp = popen(path,"r");
	g_free(path);
        if (!fp) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Error executing signature generator %s"),
                              bsmsg->ident->signature_path);
            return NULL;
        }
        len = libbalsa_readfile_nostat(fp, &ret);
        pclose(fp);    
    } else {
        /* sign is normal file */
        fp = fopen(path, "r");
        g_free(path);
        if (!fp) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Cannot open signature file '%s' "
                                "for reading"),
                              bsmsg->ident->signature_path);
            return NULL;
        }
        len = libbalsa_readfile_nostat(fp, &ret);
        fclose(fp);
    }
    if(!ret)
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Error reading signature from %s"), path);
    else {
        if(!libbalsa_utf8_sanitize(&ret, FALSE, NULL))
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Signature in %s is not a UTF-8 text."),
                              bsmsg->ident->signature_path);
    }

    return ret;
}

/* opens the load file dialog box, allows selection of the file and includes
   it at current point */

static void
do_insert_string_select_ch(BalsaSendmsg* bsmsg, GtkTextBuffer *buffer,
                           const gchar* string, size_t len,
                           const gchar* fname)
{
    const gchar *charset = NULL;
    LibBalsaTextAttribute attr = libbalsa_text_attr_string(string);

    do {
	LibBalsaCodeset codeset;
	LibBalsaCodesetInfo *info;
	gchar* s;

        if ((codeset = sw_get_user_codeset(bsmsg, NULL, NULL, fname))
            == (LibBalsaCodeset) (-1))
            break;
        info = &libbalsa_codeset_info[codeset];

	charset = info->std;
        if (info->win && (attr & LIBBALSA_TEXT_HI_CTRL))
            charset = info->win;

        g_print("Trying charset: %s\n", charset);
        if (sw_can_convert(string, len, "UTF-8", charset, &s)) {
            libbalsa_insert_with_url(buffer, s, NULL, NULL, NULL);
	    sw_prepend_charset(bsmsg, charset);
            g_free(s);
            break;
        }
    } while(1);
}

static void
insert_file_response(GtkWidget * selector, gint response,
	             BalsaSendmsg * bsmsg)
{
    GtkFileChooser *fc;
    gchar *fname;
    FILE *fl;
    GtkTextBuffer *buffer;
    gchar * string;
    size_t len;
    LibBalsaTextAttribute attr;
    GSList *list;

    if (response != GTK_RESPONSE_OK) {
	gtk_widget_destroy(selector);
	return;
    }

    fc = GTK_FILE_CHOOSER(selector);
    fname = gtk_file_chooser_get_filename(fc);

    if ((fl = fopen(fname, "rt")) ==NULL) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Could not open the file %s.\n"), fname);
	g_free(fname);
	return;
    }

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    sw_buffer_save(bsmsg);
    string = NULL;
    len = libbalsa_readfile(fl, &string);
    fclose(fl);
    
    attr = libbalsa_text_attr_string(string);
    if (!attr || attr & LIBBALSA_TEXT_HI_UTF8)
	/* Ascii or utf-8 */
        libbalsa_insert_with_url(buffer, string, NULL, NULL, NULL);
    else {
	/* Neither ascii nor utf-8... */
	gchar *s = NULL;

        for (list = bsmsg->charsets; list; list = list->next) {
            if (sw_can_convert
                (string, -1, "UTF-8", (const gchar *) list->data, &s))
                break;
            g_free(s);
	    s = NULL;
        }

	if (s) {
	    /* ...but seems to be in a current charset. */
            libbalsa_insert_with_url(buffer, s, NULL, NULL, NULL);
	    g_free(s);
	} else
	    /* ...and can't be decoded from any current charset. */
            do_insert_string_select_ch(bsmsg, buffer, string, len, fname);
    }
    g_free(string);

    /* Use the same folder as for attachments. */
    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir = gtk_file_chooser_get_current_folder(fc);

    gtk_widget_destroy(selector);
    g_free(fname);
}

static gint
include_file_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkWidget *file_selector;

    file_selector =
	gtk_file_chooser_dialog_new(_("Include file"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(file_selector), TRUE);
    /* Use the same folder as for attachments. */
    if (balsa_app.attach_dir)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER
                                            (file_selector),
                                            balsa_app.attach_dir);
    g_signal_connect(G_OBJECT(file_selector), "response",
                     G_CALLBACK(insert_file_response), bsmsg);

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
    ready = bsmsg->to_info.ready
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
    set_ready(address_entry, sma);
    check_readiness(sma->bsmsg);
}

static void
set_ready(LibBalsaAddressEntry * address_entry, BalsaSendmsgAddress *sma)
{
    gint len = libbalsa_address_entry_addresses(address_entry);

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


static gboolean
attachment2message(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
		   gpointer data)
{
    LibBalsaMessage *message = LIBBALSA_MESSAGE(data);
    BalsaAttachInfo *attachment;
    LibBalsaMessageBody *body;

    /* get the attachment information */
    gtk_tree_model_get(model, iter, ATTACH_INFO_COLUMN, &attachment, -1);

    /* create the attachment */
    body = libbalsa_message_body_new(message);
    body->filename = g_strdup(attachment->filename);
    body->content_type = g_strdup(attachment->force_mime_type);
    body->charset = g_strdup(attachment->charset);
    body->attach_mode = attachment->mode;
    libbalsa_message_append_part(message, body);

    /* clean up */
    g_object_unref(attachment);
    return FALSE;
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
    gchar recvtime[50];
    GtkTextIter start, end;
#if !defined(ENABLE_TOUCH_UI)
    const gchar *ctmp;
#endif
    gint active;
    LibBalsaIdentity *ident;

    g_assert(bsmsg != NULL);
    message = libbalsa_message_new();

    active = gtk_combo_box_get_active(GTK_COMBO_BOX(bsmsg->from[1]));
    ident = g_list_nth_data(balsa_app.identities, active);
    message->headers->from = internet_address_list_prepend(NULL, ident->ia);

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
	message->headers->reply_to = internet_address_parse_string(ctmp);
#endif

    if (bsmsg->req_dispnotify)
	libbalsa_message_set_dispnotify(message, bsmsg->ident->ia);

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
				  libbalsa_address_get_mailbox_from_list
				  (bsmsg->orig_message-> headers->from),
				  " on ", recvtime, ")",
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
    if (bsmsg->flow)
	body->buffer =
	    libbalsa_wrap_rfc2646(body->buffer, balsa_app.wraplength,
                                  TRUE, FALSE, TRUE);
    /* Disable undo and redo, because buffer2 was changed. */
    sw_buffer_set_undo(bsmsg, FALSE, FALSE);

    body->charset = g_strdup(libbalsa_text_attr_string(body->buffer) ?
                             bsmsg->charset : "us-ascii");
    libbalsa_message_append_part(message, body);

    /* add attachments */
    gtk_tree_model_foreach(BALSA_MSG_ATTACH_MODEL(bsmsg),
			   attachment2message, message);

    message->headers->date = time(NULL);
#ifdef HAVE_GPGME
    if (balsa_app.has_openpgp || balsa_app.has_smime)
        message->gpg_mode = 
            (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) != 0 ? bsmsg->gpg_mode : 0;
    else
        message->gpg_mode = 0;
#endif

    /* remember the parent window */
    g_object_set_data(G_OBJECT(message), "parent-window",
		      GTK_WINDOW(bsmsg->window));

    return message;
}

static gboolean
is_charset_ok(BalsaSendmsg *bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start, end;
    gchar *tmp;
    GSList *list;

    g_free(bsmsg->charset);
    bsmsg->charset = NULL;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    tmp = gtk_text_iter_get_text(&start, &end);

    if (!libbalsa_text_attr_string(tmp)) {
	g_free(tmp);
	return TRUE;
    }

    for (list = bsmsg->charsets; list; list = list->next) {
	const gchar *charset = list->data;

	if (sw_can_convert(tmp, -1, charset, "UTF-8", NULL)) {
	    bsmsg->charset = g_strdup(charset);
	    g_free(tmp);
	    return TRUE;
	}
    }

    for (list = bsmsg->charsets; list; list = list->next) {
	const gchar *charset = list->data;
	/* Try the corresponding CP125x charset, if any. */
        const gchar *windows_charset =
	    g_mime_charset_iso_to_windows(charset);

        if (strcmp(windows_charset, g_mime_charset_canon_name(charset))) {
	    /* Yes, there is one. */
            const gchar *iconv_charset =
                g_mime_charset_iconv_name(windows_charset);

            if (sw_can_convert(tmp, -1, iconv_charset, "UTF-8", NULL)) {
		/* Change the message charset. */
                bsmsg->charset = g_strdup(iconv_charset);
		g_free(tmp);
		return TRUE;
            }
        }
    }

    bsmsg->charset = g_strdup("UTF-8");
    g_free(tmp);
    return TRUE;
}

/* "send message" menu and toolbar callback.
 */
static gint
send_message_handler(BalsaSendmsg * bsmsg, gboolean queue_only)
{
    LibBalsaMsgCreateResult result;
    LibBalsaMessage *message;
    LibBalsaMailbox *fcc;
#ifdef HAVE_GPGME
    GtkTreeIter iter;
#endif

    if (!is_ready_to_send(bsmsg))
	return FALSE;

    if (balsa_app.debug)
	fprintf(stderr, "sending with charset: %s\n", bsmsg->charset);

    if(!is_charset_ok(bsmsg))
        return FALSE;

#ifdef HAVE_GPGME
    if ((bsmsg->gpg_mode & LIBBALSA_PROTECT_OPENPGP) != 0 &&
        (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) != 0 &&
	gtk_tree_model_get_iter_first(BALSA_MSG_ATTACH_MODEL(bsmsg), &iter)) {
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

#if ENABLE_ESMTP
    if(queue_only)
	result = libbalsa_message_queue(message, balsa_app.outbox, fcc,
					bsmsg->ident->smtp_server,
					bsmsg->flow);
    else 
        result = libbalsa_message_send(message, balsa_app.outbox, fcc,
                                       balsa_find_sentbox_by_url,
				       bsmsg->ident->smtp_server,
                                       bsmsg->flow, balsa_app.debug);
#else
    if(queue_only)
	result = libbalsa_message_queue(message, balsa_app.outbox, fcc,
					bsmsg->flow);
    else 
        result = libbalsa_message_send(message, balsa_app.outbox, fcc,
                                       balsa_find_sentbox_by_url,
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
    send_message_handler(bsmsg, balsa_app.always_queue_sent_mail);
}


/* "send message" menu callback */
static gint
send_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
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
                                             bsmsg->flow);
    else
	successp = libbalsa_message_postpone(message, balsa_app.draftbox, 
                                             NULL,
                                             bsmsg->fcc_url,
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
    balsa_window_select_all(GTK_WINDOW(bsmsg->window));
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
    GtkWidget *focus_widget;
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
    regex_t rex;

    focus_widget = gtk_window_get_focus(GTK_WINDOW(bsmsg->window));
    if (focus_widget && GTK_IS_ENTRY(focus_widget)
        && libbalsa_address_entry_show_matches((GtkEntry *) focus_widget))
        return;

    if (!bsmsg->flow)
	return;

    if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED)) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Could not compile %s"),
			  _("Quoted Text Regular Expression"));
	return;
    }

    sw_buffer_save(bsmsg);

    text_view = GTK_TEXT_VIEW(bsmsg->text);
    buffer = gtk_text_view_get_buffer(text_view);
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
    return toggle_entry(bsmsg, bsmsg->from, MENU_TOGGLE_FROM_POS, 2);
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
toggle_sign_tb_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
                                   (bsmsg->gpg_sign_menu_item),
                                   gtk_toggle_tool_button_get_active
                                   (GTK_TOGGLE_TOOL_BUTTON(widget)));
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
toggle_encrypt_tb_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
                                   (bsmsg->gpg_encrypt_menu_item),
                                   gtk_toggle_tool_button_get_active
                                   (GTK_TOGGLE_TOOL_BUTTON(widget)));
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
    sw_prepend_charset(bsmsg, bsmsg->charset);
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

    balsa_spell_check_set_character_set(sc, "UTF-8");
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
    InternetAddressList *mailing_list_address;
    GList *p;

    mailing_list_address =
	libbalsa_mailbox_get_mailing_list_address(message->mailbox);
    if (mailing_list_address) {
        gchar *tmp =
	    internet_address_list_to_string(mailing_list_address, FALSE);
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
	if (bsmsg->ident->always_trust)
	    bsmsg->gpg_mode |= LIBBALSA_PROTECT_ALWAYS_TRUST;
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
        ("File %s is currently in the %s's own format.\n"
         "If you need to send it to people who use %s, "
         "then open the file in the %s, use \"Save As\" "
         "on the \"File\" menu, and select the \"%s\" format. "
         "When you click \"OK\" it will save a new "
         "\"%s\" version of the file, with \"%s\" on the end "
         "of the document name, which you can then attach instead.",
         filename,
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
