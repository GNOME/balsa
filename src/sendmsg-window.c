/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-2002 Stuart Parmenter and others, see AUTHORS file.
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
   the current usage is limited to fonts, font sets cannot be used because:

   - gdk caches font set based on their names and changing locale will
     not help when using wildcards(!) for the charset (and explicitly
     specified in XLFD charset is ignored anyway, as XCreateFontSet(3x)
     says).

   - the option would be to use setlocale to hit gdk_fontset_load()
     but...  there is an yet unidentified problem that leads to a
     nasty deferred setup-dependent crash with double free symptoms,
     when the selected font set is unavailable on the machine (and
     probably in other cases, too). I am tempted to write a test
     program and send it over to GDK gurus.

   Locale data is then used exclusively for the spelling checking.  */


#include "config.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <ctype.h>

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
    BalsaSendmsg *msg;
} balsa_edit_with_gnome_data;


static gchar *read_signature(BalsaSendmsg *msg);
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
static gboolean attach_message(BalsaSendmsg *msg, LibBalsaMessage *message);
static gint insert_selected_messages(BalsaSendmsg *msg, SendType type);
static gint attach_message_cb(GtkWidget *, BalsaSendmsg *);
static gint include_message_cb(GtkWidget *, BalsaSendmsg *);
static void close_window_cb(GtkWidget *, gpointer);
static gchar* check_if_regular_file(const gchar *);
static void balsa_sendmsg_destroy_handler(BalsaSendmsg * bsm);
static void check_readiness(BalsaSendmsg * bsmsg);
static void init_menus(BalsaSendmsg *);
static gint toggle_from_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_to_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_subject_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_cc_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_bcc_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_fcc_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_reply_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_attachments_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_comments_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_keywords_cb(GtkWidget *, BalsaSendmsg *);
static gint toggle_reqdispnotify_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static gint toggle_queue_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);

static void spell_check_cb(GtkWidget * widget, BalsaSendmsg *);

static void address_book_cb(GtkWidget *widget, BalsaSendmsg *smd_msg_wind);

static gint set_locale(GtkWidget *, BalsaSendmsg *, gint);

static void edit_with_gnome(GtkWidget* widget, BalsaSendmsg* msg);
static void change_identity_dialog_cb(GtkWidget*, BalsaSendmsg*);
static void repl_identity_signature(BalsaSendmsg* msg, 
                                    LibBalsaIdentity* new_ident,
                                    LibBalsaIdentity* old_ident,
                                    gint* replace_offset, gint siglen, 
                                    gchar* new_sig);
static gchar* prep_signature(LibBalsaIdentity* ident, gchar* sig);
static void update_msg_identity(BalsaSendmsg*, LibBalsaIdentity*);

static void sw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc);
static GString *
quoteBody(BalsaSendmsg * msg, LibBalsaMessage * message, SendType type);
static void set_list_post_address(BalsaSendmsg * msg);
static gboolean set_list_post_rfc2369(BalsaSendmsg * msg, GList * p);
static gchar *rfc2822_skip_comments(gchar * str);
static void address_changed_cb(LibBalsaAddressEntry * address_entry,
                               BalsaSendmsgAddress *sma);
static void set_ready(LibBalsaAddressEntry * address_entry,
                      BalsaSendmsgAddress *sma);
static void sendmsg_window_set_title(BalsaSendmsg * msg);

/* dialog callback */
static void response_cb(GtkDialog * dialog, gint response, gpointer data);

/* icon list callbacks */
static void select_attachment(GnomeIconList * ilist, gint num,
                              GdkEventButton * event, gpointer data);
static gboolean sw_popup_menu_cb(GtkWidget * widget, gpointer data);
/* helper */
static gboolean sw_do_popup(GnomeIconList * ilist, GdkEventButton * event);

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

static void cut_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void copy_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void paste_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void select_all_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void wrap_body_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void reflow_par_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static void reflow_body_cb(GtkWidget * widget, BalsaSendmsg * bsmsg);
static gint insert_signature_cb(GtkWidget *, BalsaSendmsg *);
static gint quote_messages_cb(GtkWidget *, BalsaSendmsg *);


static GnomeUIInfo file_menu[] = {
#define MENU_FILE_INCLUDE_POS 0
    GNOMEUIINFO_ITEM_STOCK(N_("_Include File..."), NULL,
			   include_file_cb, GTK_STOCK_OPEN),
#define MENU_FILE_ATTACH_POS 1
    GNOMEUIINFO_ITEM_STOCK(N_("_Attach File..."), NULL,
			   attach_clicked, BALSA_PIXMAP_MENU_ATTACHMENT),
#define MENU_MSG_INCLUDE_POS 2
    GNOMEUIINFO_ITEM_STOCK(N_("_Include Message(s)"), NULL,
			   include_message_cb, BALSA_PIXMAP_MENU_NEW),
#define MENU_FILE_ATTACH_MSG_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("Attach _Message(s)"), NULL,
			   attach_message_cb, BALSA_PIXMAP_MENU_FORWARD),
#define MENU_FILE_SEPARATOR1_POS 4
    GNOMEUIINFO_SEPARATOR,

#define MENU_FILE_SEND_POS 5
    { GNOME_APP_UI_ITEM, N_("_Send"),
      N_("Send this message"),
      send_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_SEND, 'S', GDK_CONTROL_MASK|GDK_SHIFT_MASK, NULL },
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
    GNOMEUIINFO_ITEM_STOCK(N_("Print..."), N_("Print the edited message"),
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
    GNOMEUIINFO_MENU_CUT_ITEM(cut_cb, NULL),
    GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
    GNOMEUIINFO_MENU_PASTE_ITEM(paste_cb, NULL),
    {GNOME_APP_UI_ITEM, N_("_Select All"), NULL,
     (gpointer) select_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'A', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_WRAP_BODY 5
    {GNOME_APP_UI_ITEM, N_("_Wrap Body"), N_("Wrap message lines"),
     (gpointer) wrap_body_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_z, GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_ADD_SIGNATURE 7
    {GNOME_APP_UI_ITEM, N_("Insert Si_gnature"), NULL,
     (gpointer) insert_signature_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_g, GDK_CONTROL_MASK, NULL},
#define EDIT_MENU_QUOTE 8
    {GNOME_APP_UI_ITEM, N_("_Quote Message(s)"), NULL,
     (gpointer) quote_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     0, 0, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_REFLOW_PARA 10
    {GNOME_APP_UI_ITEM, N_("_Reflow Paragraph"), NULL,
     (gpointer) reflow_par_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_r, GDK_CONTROL_MASK, NULL},
#define EDIT_MENU_REFLOW_MESSAGE 11
    {GNOME_APP_UI_ITEM, N_("R_eflow Message"), NULL,
     (gpointer) reflow_body_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_r, GDK_CONTROL_MASK | GDK_SHIFT_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_SPELL_CHECK 13
    GNOMEUIINFO_ITEM_STOCK(N_("C_heck Spelling"), 
                           N_("Check the spelling of the message"),
                           spell_check_cb,
                           GTK_STOCK_SPELL_CHECK),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_SELECT_IDENT 15
    GNOMEUIINFO_ITEM_STOCK(N_("Select _Identity..."), 
                           N_("Select the Identity to use for the message"),
                           change_identity_dialog_cb,
                           BALSA_PIXMAP_MENU_IDENTITY),
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_EDIT_GNOME 17
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
#define MENU_TOGGLE_TO_POS 1
    GNOMEUIINFO_TOGGLEITEM(N_("_To"), NULL, toggle_to_cb, NULL),
#define MENU_TOGGLE_SUBJECT_POS 2
    GNOMEUIINFO_TOGGLEITEM(N_("_Subject"), NULL, toggle_subject_cb, NULL),
#define MENU_TOGGLE_CC_POS 3
    GNOMEUIINFO_TOGGLEITEM(N_("_Cc"), NULL, toggle_cc_cb, NULL),
#define MENU_TOGGLE_BCC_POS 4
    GNOMEUIINFO_TOGGLEITEM(N_("_Bcc"), NULL, toggle_bcc_cb, NULL),
#define MENU_TOGGLE_FCC_POS 5
    GNOMEUIINFO_TOGGLEITEM(N_("_Fcc"), NULL, toggle_fcc_cb, NULL),
#define MENU_TOGGLE_REPLY_POS 6
    GNOMEUIINFO_TOGGLEITEM(N_("_Reply To"), NULL, toggle_reply_cb, NULL),
#define MENU_TOGGLE_ATTACHMENTS_POS 7
    GNOMEUIINFO_TOGGLEITEM(N_("_Attachments"), NULL, toggle_attachments_cb,
			   NULL),
#define MENU_TOGGLE_COMMENTS_POS 8
    GNOMEUIINFO_TOGGLEITEM(N_("_Comments"), NULL, toggle_comments_cb,
			   NULL),
#define MENU_TOGGLE_KEYWORDS_POS 9
    GNOMEUIINFO_TOGGLEITEM(N_("_Keywords"), NULL, toggle_keywords_cb,
			   NULL),
    GNOMEUIINFO_END
};

#if MENU_TOGGLE_KEYWORDS_POS+1 != VIEW_MENU_LENGTH
#error Inconsistency in defined lengths.
#endif

/* the array of locale names and charset names included in the MIME
   type information.  
   if you add a new encoding here add to SendCharset in libbalsa.c 
*/
struct {
    const gchar *locale, *charset, *lang_name;
} locales[] = {
#define LOC_BRAZILIAN_POS 0
    {"pt_BR", "ISO-8859-1", N_("Brazilian")},
#define LOC_CATALAN_POS   1
    {"ca_ES", "ISO-8859-15", N_("Catalan")},
#define LOC_CHINESE_SIMPLIFIED_POS   2
    {"zh_CN.GB2312", "gb2312", N_("Chinese Simplified")},
#define LOC_CHINESE_TRADITIONAL_POS   3
    {"zh_TW.Big5", "big5", N_("Chinese Traditional")},
#define LOC_DANISH_POS    4
    {"da_DK", "ISO-8859-1", N_("Danish")},
#define LOC_GERMAN_POS    5
    {"de_DE", "ISO-8859-15", N_("German")},
#define LOC_DUTCH_POS     6
    {"nl_NL", "ISO-8859-15", N_("Dutch")},
#define LOC_ENGLISH_POS   7
    /* English -> American English, argh... */
    {"en_US", "ISO-8859-1", N_("English")}, 
#define LOC_ESTONIAN_POS  8
    {"et_EE", "ISO-8859-15", N_("Estonian")},
#define LOC_FINNISH_POS   9
    {"fi_FI", "ISO-8859-15", N_("Finnish")},
#define LOC_FRENCH_POS    10
    {"fr_FR", "ISO-8859-15", N_("French")},
#define LOC_GREEK_POS     11 
    {"el_GR", "ISO-8859-7", N_("Greek")},
#define LOC_HEBREW_POS    12
    {"he_IL", "UTF-8", N_("Hebrew")},
#define LOC_HUNGARIAN_POS 13
    {"hu_HU", "ISO-8859-2", N_("Hungarian")},
#define LOC_ITALIAN_POS   14
    {"it_IT", "ISO-8859-15", N_("Italian")},
#define LOC_JAPANESE_POS  15
    {"ja_JP", "euc-jp", N_("Japanese")},
#define LOC_KOREAN_POS    16
    {"ko_KR", "euc-kr", N_("Korean")},
#define LOC_LATVIAN_POS    17
    {"lv_LV", "ISO-8859-13", N_("Latvian")},
#define LOC_LITHUANIAN_POS    18
    {"lt_LT", "ISO-8859-13", N_("Lithuanian")},
#define LOC_NORWEGIAN_POS 19
    {"no_NO", "ISO-8859-1", N_("Norwegian")},
#define LOC_POLISH_POS    20
    {"pl_PL", "ISO-8859-2", N_("Polish")},
#define LOC_PORTUGESE_POS 21
    {"pt_PT", "ISO-8859-15", N_("Portugese")},
#define LOC_ROMANIAN_POS 22
    {"ro_RO", "ISO-8859-2", N_("Romanian")},
#define LOC_RUSSIAN_ISO_POS   23
    {"ru_SU", "ISO-8859-5", N_("Russian (ISO)")},
#define LOC_RUSSIAN_KOI_POS   24
    {"ru_RU", "KOI8-R", N_("Russian (KOI)")},
#define LOC_SLOVAK_POS    25
    {"sk_SK", "ISO-8859-2", N_("Slovak")},
#define LOC_SPANISH_POS   26
    {"es_ES", "ISO-8859-15", N_("Spanish")},
#define LOC_SWEDISH_POS   27
    {"sv_SE", "ISO-8859-1", N_("Swedish")},
#define LOC_TURKISH_POS   28
    {"tr_TR", "ISO-8859-9", N_("Turkish")},
#define LOC_UKRAINIAN_POS 29
    {"uk_UK", "KOI8-U", N_("Ukrainian")},
#define LOC_UTF8_POS 30
    {"", "UTF-8", N_("Generic UTF-8")}
};

static void lang_set_cb(GtkWidget *widget, BalsaSendmsg *bsmsg);

static GnomeUIInfo locale_aj_menu[] = {
    GNOMEUIINFO_ITEM_DATA(N_("Brazilian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_BRAZILIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Catalan"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_CATALAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Chinese Simplified"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_CHINESE_SIMPLIFIED_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Chinese Traditional"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_CHINESE_TRADITIONAL_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Catalan"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_CATALAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Danish"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_DANISH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Dutch"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_DUTCH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("English"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_ENGLISH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Estonian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_ESTONIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Finnish"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_FINNISH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("French"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_FRENCH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("German"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_GERMAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Greek"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_GREEK_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Hebrew"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_HEBREW_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Hungarian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_HUNGARIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Italian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_ITALIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Japanese"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_JAPANESE_POS), NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo locale_kz_menu[] = {
    GNOMEUIINFO_ITEM_DATA(N_("Korean"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_KOREAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Latvian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_LATVIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Lithuanian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_LITHUANIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Norwegian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_NORWEGIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Polish"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_POLISH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Portugese"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_PORTUGESE_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Romanian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_ROMANIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Russian (ISO)"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_RUSSIAN_ISO_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Russian (KOI)"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_RUSSIAN_KOI_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Slovak"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_SLOVAK_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Spanish"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_SPANISH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Swedish"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_SWEDISH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Turkish"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_TURKISH_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("Ukrainian"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_UKRAINIAN_POS), NULL),
    GNOMEUIINFO_ITEM_DATA(N_("UTF-8"), NULL, lang_set_cb,
                          GINT_TO_POINTER(LOC_UTF8_POS), NULL),
    GNOMEUIINFO_END
};

/* two sections plus one place-holder for the current language.
*/
static GnomeUIInfo lang_menu[] = {
    GNOMEUIINFO_SUBTREE(N_("_A-J"), locale_aj_menu),
    GNOMEUIINFO_SUBTREE(N_("_K-Z"), locale_kz_menu),
    GNOMEUIINFO_SEPARATOR,
#define LANG_CURRENT_POS 3
    GNOMEUIINFO_ITEM_NONE(NULL, NULL, NULL),
    GNOMEUIINFO_END
};



static GnomeUIInfo opts_menu[] = {
#define OPTS_MENU_DISPNOTIFY_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("_Request Disposition Notification"), NULL, 
			   toggle_reqdispnotify_cb, NULL),
    GNOMEUIINFO_TOGGLEITEM(N_("_Always Queue Sent Mail"), NULL, 
			   toggle_queue_cb, NULL),
    GNOMEUIINFO_END
};

#define CASE_INSENSITIVE_NAME
#define PRESERVE_CASE TRUE
#define OVERWRITE_CASE FALSE

typedef struct {
    gchar *name;
    guint length;
} headerMenuDesc;

headerMenuDesc headerDescs[] = { {"from", 3}, {"to", 3}, {"subject", 2},
{"cc", 3}, {"bcc", 3}, {"fcc", 2},
{"replyto", 3}, {"attachments", 4},
{"comments", 2}, {"keywords", 2}
};

/* from libmutt/mime.h - Content-Disposition values */
enum
{
  DISPINLINE,
  DISPATTACH,
  DISPFORMDATA
};
/* i'm sure there's a subtle and nice way of making it visible here */
typedef struct {
    gchar *filename;
    gchar *force_mime_type;
    gboolean delete_on_destroy;
    gboolean as_extbody;
    guint disposition;
} attachment_t;

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


static void
append_comma_separated(GtkEditable *editable, const gchar * text)
{
    gint position;

    if (!text)
        return;

    gtk_editable_set_position(editable, -1);
    position = gtk_editable_get_position(editable);
    if (position > 0)
        gtk_editable_insert_text(editable, ", ", 2, &position);
    gtk_editable_insert_text(editable, text, -1, &position);
    gtk_editable_set_position(editable, position);
}

/* the callback handlers */
static void
address_book_cb(GtkWidget *widget, BalsaSendmsg *snd_msg_wind)
{
    GtkWidget *ab;
    LibBalsaAddressEntry *address_entry;
    gint response;

    address_entry =
        LIBBALSA_ADDRESS_ENTRY(g_object_get_data
                               (G_OBJECT(widget), "address-entry-widget"));

    ab = balsa_ab_window_new(TRUE, GTK_WINDOW(snd_msg_wind->window));

    response = gtk_dialog_run(GTK_DIALOG(ab));
    if ( response == GTK_RESPONSE_OK ) {
	gchar *t =
            balsa_ab_window_get_recipients(BALSA_AB_WINDOW(ab));

        append_comma_separated(GTK_EDITABLE(address_entry), t);
        g_free(t);
    }
    gtk_widget_destroy(ab);
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
                                   GTK_DIALOG_MODAL,
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
	    message_postpone(bsmsg);
	/* cancel action  when reply = "yes" or "no" */
	return (reply != GTK_RESPONSE_YES) && (reply != GTK_RESPONSE_NO);
    }
    return FALSE;
}
static gint
delete_event_cb(GtkWidget * widget, GdkEvent * e, gpointer data)
{
    BalsaSendmsg* bsmsg = (BalsaSendmsg *) data;
    return delete_handler(bsmsg);
}

static void
close_window_cb(GtkWidget * widget, gpointer data)
{
    BalsaSendmsg* bsmsg = (BalsaSendmsg *) data;
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
balsa_sendmsg_destroy_handler(BalsaSendmsg * bsm)
{
    gboolean quit_on_close;

    g_assert(bsm != NULL);
    g_assert(ELEMENTS(headerDescs) == ELEMENTS(bsm->view_checkitems));

    g_signal_handler_disconnect(G_OBJECT(balsa_app.main_window),
                                bsm->delete_sig_id);
    if(balsa_app.debug) g_message("balsa_sendmsg_destroy()_handler: Start.");

    if (bsm->orig_message) {
	if (bsm->orig_message->mailbox)
	    libbalsa_mailbox_close(bsm->orig_message->mailbox);
        /* check again! */
	if (bsm->orig_message->mailbox)
	    g_object_unref(G_OBJECT(bsm->orig_message->mailbox));
	g_object_unref(G_OBJECT(bsm->orig_message));
    }

    if (balsa_app.debug)
	printf("balsa_sendmsg_destroy_handler: Freeing bsm\n");
    gtk_widget_destroy(bsm->window);
    if (bsm->bad_address_style)
        g_object_unref(G_OBJECT(bsm->bad_address_style));
    quit_on_close = bsm->quit_on_close;
    g_free(bsm->fcc_url);
    g_free(bsm->charset);

    g_free(bsm);

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
static gint
find_locale_index_by_locale(const gchar * locale)
{
    unsigned i, j, maxfit = 0, maxpos = 0;

    if (!locale || strcmp(locale, "C") == 0)
	return LOC_ENGLISH_POS;
    for (i = 0; i < ELEMENTS(locales); i++) {
	for (j = 0; locale[j] && locales[i].locale[j] == locale[j]; j++);
	if (j > maxfit) {
	    maxfit = j;
	    maxpos = i;
	}
    }
    return maxpos;
}

/* fill_language_menu:
   fills in the system language.
*/
static void
fill_language_menu()
{
    int idxsys;
    idxsys = find_locale_index_by_locale(setlocale(LC_CTYPE, NULL));
    if (balsa_app.debug)
	printf("idxsys: %d %s for %s\n", idxsys, locales[idxsys].lang_name,
	       setlocale(LC_CTYPE, NULL));
    lang_menu[LANG_CURRENT_POS].label = (char *) locales[idxsys].lang_name;
}

static gboolean
edit_with_gnome_check(gpointer data) {
    FILE *tmp;
    balsa_edit_with_gnome_data *data_real = (balsa_edit_with_gnome_data *)data;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(data_real->msg->text));
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
    if(balsa_app.edit_headers) {
	while(fgets(line, sizeof(line), tmp)) {
            if(line[strlen(line)-1] == '\n')line[strlen(line)-1] = '\0';
            if(!strncmp(line, "To: ", 4))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->to[1]),
                                   line + 4);
            else if(!strncmp(line, "From: ", 6))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->from[1]), 
                                   line+6);
            else if(!strncmp(line, "Reply-To: ", 10))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->reply_to[1]), 
                                   line+10);
            else if(!strncmp(line, "Bcc: ", 5))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->bcc[1]), 
                                   line+5);
            else if(!strncmp(line, "Cc: ", 4))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->cc[1]), 
                                   line+4);
            else if(!strncmp(line, "Comments: ", 10))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->comments[1]), 
                                   line+10);
            else if(!strncmp(line, "Subject: ", 9))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->subject[1]), 
                                   line+9);
            else break;
	}
    }
    gtk_text_buffer_set_text(buffer, "", 0);
    curposition = 0;
    while(fgets(line, sizeof(line), tmp))
        gtk_text_buffer_insert_at_cursor(buffer, line, -1);
    g_free(data_real->filename);
    fclose(tmp);
    unlink(data_real->filename);
    gtk_widget_set_sensitive(data_real->msg->text, TRUE);
    g_free(data);
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
edit_with_gnome(GtkWidget* widget, BalsaSendmsg* msg)
{
    static const char TMP_PATTERN[] = "/tmp/balsa-edit-XXXXXX";
    gchar filename[sizeof(TMP_PATTERN)];
    gchar *command;
    gchar **cmdline;
    balsa_edit_with_gnome_data *data = 
        g_malloc(sizeof(balsa_edit_with_gnome_data));
    pid_t pid;
    FILE *tmp;
    int tmpfd;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text));
    GtkTextIter start, end;
    gchar *p;

    strcpy(filename, TMP_PATTERN);
    tmpfd = mkstemp(filename);
    tmp   = fdopen(tmpfd, "w+");
    
    if(balsa_app.edit_headers) {
        const gchar
            *from = gtk_entry_get_text(GTK_ENTRY(msg->from[1])),
            *to = gtk_entry_get_text(GTK_ENTRY(msg->to[1])),
            *reply_to = gtk_entry_get_text(GTK_ENTRY(msg->reply_to[1])),
            *cc = gtk_entry_get_text(GTK_ENTRY(msg->cc[1])),
            *bcc = gtk_entry_get_text(GTK_ENTRY(msg->bcc[1])),
	    *subject = gtk_entry_get_text(GTK_ENTRY(msg->subject[1])),
	    *comments = gtk_entry_get_text(GTK_ENTRY(msg->comments[1]));
	
	/* Write all the headers */
	fprintf(tmp, 
                "From: %s\n"
                "To: %s\n"
                "Cc: %s\n"
                "Bcc: %s\n"
                "Subject: %s\n"
                "Reply-To: %s\n"
                "Comments: %s\n\n\n",
                from, to, cc, bcc, subject, reply_to, comments);
    }

    gtk_widget_set_sensitive(GTK_WIDGET(msg->text), FALSE);
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
        
        command = g_strdup_printf(balsa_app.extern_editor_command, filename); 
        cmdline = g_strsplit(command, " ", 1024); 
        execvp (cmdline[0], cmdline); 
        perror ("execvp"); 
        g_strfreev (cmdline); 
        g_free(command);
        exit(127);
    }
    /* Return immediately. We don't want balsa to 'hang' */
    data->pid_editor = pid;
    data->filename = g_strdup(filename);
    data->msg = msg;
    gtk_idle_add((GtkFunction) edit_with_gnome_check, data);
}

static void 
change_identity_dialog_cb(GtkWidget* widget, BalsaSendmsg* msg)
{
    libbalsa_identity_select_dialog(GTK_WINDOW(msg->window),
                                    _("Select Identity"),
                                    balsa_app.identities,
                                    msg->ident,
                                    ((LibBalsaIdentityCallback)
                                     update_msg_identity),
                                    msg);
}


static void
repl_identity_signature(BalsaSendmsg* msg, LibBalsaIdentity* new_ident,
                        LibBalsaIdentity* old_ident, gint* replace_offset, 
                        gint siglen, gchar* new_sig) 
{
    gint newsiglen;
    gboolean reply_type = (msg->type == SEND_REPLY || 
                           msg->type == SEND_REPLY_ALL ||
                           msg->type == SEND_REPLY_GROUP);
    gboolean forward_type = (msg->type == SEND_FORWARD_ATTACH || 
                             msg->type == SEND_FORWARD_INLINE);
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text));
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
    gtk_text_buffer_insert(buffer, &ins, new_sig, newsiglen);
}


static gchar*
prep_signature(LibBalsaIdentity* ident, gchar* sig)
{
    gchar* sig_tmp;

    /* empty signature is a legal signature */
    if(sig == NULL) return NULL;
    
    if (ident->sig_separator) {
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
 * update_msg_identity
 * 
 * Change the specified BalsaSendmsg current identity, and update the
 * corresponding fields. 
 * */
static void
update_msg_identity(BalsaSendmsg* msg, LibBalsaIdentity* ident)
{
    GtkTextBuffer *buffer = 
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text));
    GtkTextIter start, end;

    gint replace_offset = 0;
    gint siglen;
    gint i = 0;
    
    gchar* old_sig;
    gchar* new_sig;
    gchar* message_text;
    gchar* compare_str;
    gchar** message_split;
    gchar* tmpstr=libbalsa_address_to_gchar(ident->address, 0);
    
    LibBalsaIdentity* old_ident;

    
    g_return_if_fail(ident != NULL);


    /* change entries to reflect new identity */
    gtk_entry_set_text(GTK_ENTRY(msg->from[1]), tmpstr);
    g_free(tmpstr);

    gtk_entry_set_text(GTK_ENTRY(msg->reply_to[1]), ident->replyto);
    gtk_entry_set_text(GTK_ENTRY(msg->bcc[1]), ident->bcc);
    
    /* change the subject to use the reply/forward strings */

    /* -----------------------------------------------------------
     * remove/add the signature depending on the new settings, change
     * the signature if path changed */

    /* reconstruct the old signature to search with */
    old_ident = msg->ident;
    old_sig = read_signature(msg);
    old_sig = prep_signature(old_ident, old_sig);

    /* switch identities in msg here so we can use read_signature
     * again */
    msg->ident = ident;
    new_sig = read_signature(msg);
    new_sig = prep_signature(ident, new_sig);
    if(!new_sig) new_sig = g_strdup("");

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    message_text = gtk_text_iter_get_text(&start, &end);
    if (!old_sig) {
        replace_offset = msg->ident->sig_prepend ? 0 : strlen(message_text);
        repl_identity_signature(msg, ident, old_ident, &replace_offset,
                                0, new_sig);
    } else {
        /* split on sig separator */
        message_split = g_strsplit(message_text, "\n-- \n", 0);
        siglen = strlen(old_sig);
        while (message_split[i]) {
            /* put sig separator back to search */
            compare_str = g_strconcat("\n-- \n", message_split[i], NULL);
            
            /* try to find occurance of old signature */
            if (g_ascii_strncasecmp(old_sig, compare_str, siglen) == 0) {
                repl_identity_signature(msg, ident, old_ident,
                                        &replace_offset, siglen, new_sig);
            }
            
            replace_offset += strlen(i ? compare_str : message_split[i]);
            g_free(compare_str);
            i++;
        }
        /* if no sig seperators found, do a slower brute force approach */
        if (!message_split[0] || !message_split[1]) {
            compare_str = message_text;
            replace_offset = 0;
            
            while (*compare_str) {
                if (g_ascii_strncasecmp(old_sig, compare_str, siglen) == 0) {
                    repl_identity_signature(msg, ident, old_ident,
                                            &replace_offset, siglen, new_sig);
                }
                replace_offset++;
                compare_str++;
            }
        }
        g_strfreev(message_split);
    }
    
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
    attach->disposition = DISPATTACH;

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
    attach->disposition = DISPATTACH; /* sounds reasonable */
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
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                   event_button, event_time);
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
add_attachment(GnomeIconList * iconlist, char *filename, 
               gboolean is_a_temp_file, const gchar *forced_mime_type)
{
    GtkWidget *msgbox;
    gchar *content_type = NULL;
    gchar *pix, *err_msg;

    if (balsa_app.debug)
	fprintf(stderr, "Trying to attach '%s'\n", filename);
    if ( (err_msg=check_if_regular_file(filename)) != NULL) {
	msgbox = gtk_message_dialog_new(NULL,
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_CANCEL,
                                        err_msg);
	gtk_dialog_run(GTK_DIALOG(msgbox));
        gtk_widget_destroy(msgbox);
	g_free(err_msg);
        g_free(content_type);
	return FALSE;
    }

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
            g_print("Error converting \"%s\" to UTF-8: %s\n",
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
	if(forced_mime_type && !strcmp(forced_mime_type, "message/rfc822"))
	    attach_data->disposition = DISPINLINE;
	else
	    attach_data->disposition = DISPATTACH;
	gnome_icon_list_set_icon_data_full(iconlist, pos, attach_data, destroy_attachment);

        g_free(basename);
        g_free(utf8name);
	g_free(label);
    }

    g_free(pix) ;
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
    GnomeIconList *iconlist;
    BalsaSendmsg *bsmsg;
    gchar **files;
    gchar **tmp;

    fs = GTK_FILE_SELECTION(data);
    bsmsg = g_object_get_data(G_OBJECT(fs), "balsa-data");

    iconlist = GNOME_ICON_LIST(bsmsg->attachments[1]);
    files = gtk_file_selection_get_selections(fs);
    for (tmp = files; *tmp; ++tmp)
        add_attachment(iconlist, g_strdup(*tmp), FALSE, NULL);
    g_strfreev(files);
    
    bsmsg->update_config = FALSE;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
	bsmsg->view_checkitems[MENU_TOGGLE_ATTACHMENTS_POS]), TRUE);
    bsmsg->update_config = TRUE;

    if (balsa_app.attach_dir)
	g_free(balsa_app.attach_dir);
    balsa_app.attach_dir =
        g_strdup(gtk_file_selection_get_filename(fs));

    gtk_widget_destroy(GTK_WIDGET(fs));
    /* FIXME: show attachment list */
}

/* attach_clicked - menu and toolbar callback */
static void
attach_clicked(GtkWidget * widget, gpointer data)
{
    GtkWidget *fsw;
    GnomeIconList *iconlist;
    GtkFileSelection *fs;
    BalsaSendmsg *bsm;

    bsm = data;

    iconlist = GNOME_ICON_LIST(bsm->attachments[1]);

    fsw = gtk_file_selection_new(_("Attach file"));
#if 0
    /* start workaround for prematurely realized widget returned
     * by some GTK+ versions */
    if(GTK_WIDGET_REALIZED(fsw))
        gtk_widget_unrealize(fsw);
    /* end workaround for prematurely realized widget */
#endif
    gtk_window_set_wmclass(GTK_WINDOW(fsw), "file", "Balsa");
    g_object_set_data(G_OBJECT(fsw), "balsa-data", bsm);

    fs = GTK_FILE_SELECTION(fsw);
    gtk_file_selection_set_select_multiple(fs, TRUE);
    if (balsa_app.attach_dir)
	gtk_file_selection_set_filename(fs, balsa_app.attach_dir);


    g_signal_connect(G_OBJECT(fs->ok_button), "clicked",
		     G_CALLBACK(attach_dialog_ok), fs);
    g_signal_connect_swapped(G_OBJECT(fs->cancel_button), "clicked",
			     G_CALLBACK(gtk_widget_destroy),
                             GTK_OBJECT(fsw));

    gtk_widget_show(fsw);

    return;
}

/* attach_message:
   returns TRUE on success, FALSE on failure.
*/
static gboolean 
attach_message(BalsaSendmsg *msg, LibBalsaMessage *message)
{
    gchar *name, tmp_file_name[PATH_MAX + 1];
	
    libbalsa_mktemp(tmp_file_name);
    mkdir(tmp_file_name, 0700);
    name = g_strdup_printf("%s/forwarded-message", tmp_file_name);
    if(!libbalsa_message_save(message, name)) {
        g_free(name);
        return FALSE;
    }
    add_attachment(GNOME_ICON_LIST(msg->attachments[1]), name,
		   TRUE, "message/rfc822");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
	    msg->view_checkitems[MENU_TOGGLE_ATTACHMENTS_POS]), TRUE);
    return TRUE;
}

static gint
insert_selected_messages(BalsaSendmsg *msg, SendType type)
{
    GtkTextBuffer *buffer = 
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text));
    GtkWidget *index =
	balsa_window_find_current_index(balsa_app.main_window);
    
    if (index) {
	GList *node, *l = balsa_index_selected_list(BALSA_INDEX(index));
    
	for (node = l; node; node = g_list_next(node)) {
	    LibBalsaMessage *message = node->data;
	    GString *body = quoteBody(msg, message, type);
	    
            gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);
	    g_string_free(body, TRUE);
	}
        g_list_free(l);
    }
    
    return TRUE;
}

static gint include_message_cb(GtkWidget *widget, BalsaSendmsg *msg)
{
    return insert_selected_messages(msg, SEND_FORWARD_INLINE);
}


static gint
attach_message_cb(GtkWidget * widget, BalsaSendmsg *msg) 
{
    GtkWidget *index =
	balsa_window_find_current_index(balsa_app.main_window);
    
    if (index) {
	GList *node, *l = balsa_index_selected_list(BALSA_INDEX(index));
    
	for (node = l; node; node = g_list_next(node)) {
	    LibBalsaMessage *message = node->data;

	    if(!attach_message(msg, message)) {
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Attaching message failed.\n"
                                       "Possible reason: not enough temporary space"));
                break;
            }
	}
        g_list_free(l);
    }
    
    return TRUE;
}


#if 0
static gint include_messages_cb(GtkWidget *widget, BalsaSendmsg *msg)
{
    return insert_selected_messages(msg, SEND_FORWARD_INLINE);
}
#endif /* 0 */

/* attachments_add - attachments field D&D callback */
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
        GList *uri_list;
        
        for (uri_list = (GList *)selection_data->data; uri_list;
             uri_list = g_list_next(uri_list)) {
            const gchar *path = gnome_vfs_uri_get_path(uri_list->data);
	    add_attachment(GNOME_ICON_LIST(bsmsg->attachments[1]),
			   g_strdup(path), FALSE, NULL);
        }

	/* show attachment list */
	bsmsg->update_config = FALSE;
	gtk_check_menu_item_set_active(
				       GTK_CHECK_MENU_ITEM(bsmsg->view_checkitems[MENU_TOGGLE_ATTACHMENTS_POS]), TRUE);
	bsmsg->update_config = TRUE;
    } else if( info == TARGET_STRING) {
	add_extbody_attachment( GNOME_ICON_LIST(bsmsg->attachments[1]),
				selection_data->data, "text/html", FALSE, TRUE);
    }	
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
    arr[0] = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(arr[0]), arr[1]);
    gtk_misc_set_alignment(GTK_MISC(arr[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(arr[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), arr[0], 0, 1, y_pos, y_pos + 1,
		     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

    gtk_widget_modify_font(arr[1],
                           pango_font_description_from_string
                           (balsa_app.message_font));
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
 *         BalsaSendmsg *smw  - The send message window
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
		   const gchar * icon, BalsaSendmsg *smw, GtkWidget * arr[],
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
		     G_CALLBACK(address_book_cb), smw);
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
		       smw->ident->domain);
    g_signal_connect(G_OBJECT(arr[1]), "changed",
                     G_CALLBACK(address_changed_cb), sma);

    if (!smw->bad_address_style) {
        /* set up the style for flagging bad/incomplete addresses */
        GdkColor color = balsa_app.bad_address_color;

        smw->bad_address_style =
            gtk_style_copy(gtk_widget_get_style(GTK_WIDGET(arr[0])));

        if (gdk_colormap_alloc_color(balsa_app.colormap, &color, FALSE, TRUE)) {
            smw->bad_address_style->fg[GTK_STATE_NORMAL] = color;
        } else {
            fprintf(stderr, "Couldn't allocate bad address color!\n");
            fprintf(stderr, " red: %04x; green: %04x; blue: %04x.\n",
               color.red, color.green, color.blue);
        }
    }

    /* populate the info structure: */
    sma->msg = smw;
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
create_info_pane(BalsaSendmsg * msg, SendType type)
{
    GtkWidget *sw;
    GtkWidget *table;
    GtkWidget *frame;
    GtkWidget *sc;
    GtkWidget *align;

    table = gtk_table_new(11, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);

    /* msg->bad_address_style will be set in create_email_entry: */
    msg->bad_address_style = NULL;

    /* From: */
    create_email_entry(table, _("F_rom:"), 0, GNOME_STOCK_BOOK_BLUE,
                       msg, msg->from,
                       &msg->from_info, 1, 1);

    /* To: */
    create_email_entry(table, _("_To:"), 1, GNOME_STOCK_BOOK_RED,
                       msg, msg->to,
                       &msg->to_info, 1, -1);
    g_signal_connect_swapped(G_OBJECT(msg->to[1]), "changed",
                             G_CALLBACK(sendmsg_window_set_title), msg);

    /* Subject: */
    create_string_entry(table, _("S_ubject:"), 2, msg->subject);
    g_signal_connect_swapped(G_OBJECT(msg->subject[1]), "changed",
                             G_CALLBACK(sendmsg_window_set_title), msg);
    /* cc: */
    create_email_entry(table, _("Cc:"), 3, GNOME_STOCK_BOOK_YELLOW,
                       msg, msg->cc,
                       &msg->cc_info, 0, -1);

    /* bcc: */
    create_email_entry(table, _("Bcc:"), 4, GNOME_STOCK_BOOK_GREEN,
                       msg, msg->bcc,
                       &msg->bcc_info, 0, -1);

    /* fcc: */
    msg->fcc[0] = gtk_label_new_with_mnemonic(_("F_cc:"));
    gtk_misc_set_alignment(GTK_MISC(msg->fcc[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(msg->fcc[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), msg->fcc[0], 0, 1, 5, 6, GTK_FILL,
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
    if (type == SEND_CONTINUE && msg->orig_message->fcc_url)
        balsa_mblist_mru_add(&balsa_app.fcc_mru,
                             msg->orig_message->fcc_url);
    msg->fcc[1] =
        balsa_mblist_mru_option_menu(GTK_WINDOW(msg->window),
                                     &balsa_app.fcc_mru);
    gtk_label_set_mnemonic_widget(GTK_LABEL(msg->fcc[0]), msg->fcc[1]);
    align = gtk_alignment_new(0, 0.5, 0, 1);
    gtk_container_add(GTK_CONTAINER(align), msg->fcc[1]);
    gtk_table_attach(GTK_TABLE(table), align, 1, 3, 5, 6,
		     GTK_FILL, GTK_FILL, 0, 0);

    /* Reply To: */
    create_email_entry(table, _("_Reply To:"), 6, GNOME_STOCK_BOOK_BLUE,
                       msg, msg->reply_to,
                       &msg->reply_to_info, 0, -1);



    /* Attachment list */
    msg->attachments[0] = gtk_label_new_with_mnemonic(_("_Attachments:"));
    gtk_misc_set_alignment(GTK_MISC(msg->attachments[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(msg->attachments[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), msg->attachments[0], 0, 1, 7, 8,
		     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

    /* create icon list */
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

    msg->attachments[1] = gnome_icon_list_new(100, NULL, FALSE);
    g_signal_connect(G_OBJECT(msg->window), "drag_data_received",
		     G_CALLBACK(attachments_add), msg);
    gtk_drag_dest_set(GTK_WIDGET(msg->window), GTK_DEST_DEFAULT_ALL,
		      drop_types, ELEMENTS(drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    gtk_widget_set_size_request(msg->attachments[1], -1, 100);

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), msg->attachments[1]);
    gtk_container_add(GTK_CONTAINER(frame), sw);

    gtk_table_attach(GTK_TABLE(table), frame, 1, 3, 7, 8,
		     GTK_FILL | GTK_EXPAND,
		     GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 0);

    g_signal_connect(G_OBJECT(msg->attachments[1]), "select_icon",
		     G_CALLBACK(select_attachment), NULL);
    g_signal_connect(G_OBJECT(msg->attachments[1]), "popup-menu",
                     G_CALLBACK(sw_popup_menu_cb), NULL);

    gnome_icon_list_set_selection_mode(GNOME_ICON_LIST
				       (msg->attachments[1]),
				       GTK_SELECTION_MULTIPLE);
    GTK_WIDGET_SET_FLAGS(GNOME_ICON_LIST(msg->attachments[1]),
			 GTK_CAN_FOCUS);

    msg->attachments[2] = sw;
    msg->attachments[3] = frame;


    /* Comments: */
    create_string_entry(table, _("Comments:"), 8, msg->comments);

    /* Keywords: */
    create_string_entry(table, _("Keywords:"), 9, msg->keywords);

    sc = balsa_spell_check_new();
    msg->spell_checker = sc;

    gtk_widget_show_all(table);
    gtk_widget_hide(sc);
    return table;
}

/*
 * catch user `return' chars, and edit out any previous spaces
 * to make them `hard returns',
 * */
static void
insert_text_cb(GtkTextBuffer * buffer, GtkTextIter * iter,
               gchar *text, gint len, gpointer user_data)
{
    GtkTextIter tmp_iter;
    gunichar c = ' ';

    if (*text != '\n' || len != 1)
        return;

    tmp_iter = *iter;
    while (gtk_text_iter_backward_char(&tmp_iter)
           && (c = gtk_text_iter_get_char(&tmp_iter)) == ' ')
        /* nothing */;
    if (c != ' ')
        gtk_text_iter_forward_char(&tmp_iter);
    gtk_text_buffer_delete(buffer, &tmp_iter, iter);
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
    
    if (info != TARGET_MESSAGES)
        return;
    
    message_array = (LibBalsaMessage **) selection_data->data;
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
    
    while (*message_array) {
        GString *body = quoteBody(bsmsg, *message_array++, SEND_REPLY);
        gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);
        g_string_free(body, TRUE);
    }
}

/* create_text_area 
   Creates the text entry part of the compose window.
*/
static GtkWidget *
create_text_area(BalsaSendmsg * msg)
{
    GtkWidget *table;

    msg->text = gtk_text_view_new();
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(msg->text), 2);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(msg->text), 2);
    /* set the message font */
    gtk_widget_modify_font(msg->text,
                           pango_font_description_from_string
                           (balsa_app.message_font));
    if (msg->flow) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text));
        g_signal_connect(G_OBJECT(buffer), "insert-text",
                         G_CALLBACK(insert_text_cb), NULL);
    }
    gtk_text_view_set_editable(GTK_TEXT_VIEW(msg->text), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(msg->text), GTK_WRAP_WORD);
    balsa_spell_check_set_text(BALSA_SPELL_CHECK(msg->spell_checker),
			       GTK_TEXT_VIEW(msg->text));

    table = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(table),
    				   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(table), msg->text);
    g_signal_connect(G_OBJECT(msg->text), "drag_data_received",
		     G_CALLBACK(drag_data_quote), msg);
    gtk_drag_dest_set(GTK_WIDGET(msg->text), GTK_DEST_DEFAULT_ALL,
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
continueBody(BalsaSendmsg * msg, LibBalsaMessage * message)
{
    LibBalsaMessageBody *body;

    libbalsa_message_body_ref(message, TRUE);
    body = message->body_list;
    if (body) {
	if (libbalsa_message_body_type(body) == LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART)
	    body = body->parts;
	/* if the first part is of type text/plain with a NULL filename, it
	   was the message... */
	if (body && !body->filename) {
	    GString *rbdy;
	    gchar *body_type = libbalsa_message_body_get_content_type(body);
            gint llen = -1;
            GtkTextBuffer *buffer =
                gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text));

            if (msg->flow && libbalsa_flowed_rfc2646(body))
                llen = balsa_app.wraplength;
	    if (!strcmp(body_type, "text/plain") &&
		(rbdy = process_mime_part(message, body, NULL, llen, FALSE,
                                          msg->flow))) {
                gtk_text_buffer_insert_at_cursor(buffer, rbdy->str,
                                                 rbdy->len);
		g_string_free(rbdy, TRUE);
	    }
	    g_free(body_type);
	    body = body->next;
	}
	while (body) {
	    gchar *name, *body_type, tmp_file_name[PATH_MAX + 1];

	    libbalsa_mktemp(tmp_file_name);
	    if (body->filename) {
		mkdir(tmp_file_name, 0700);
		name = g_strdup_printf("%s/%s", tmp_file_name, body->filename);
	    } else
		name = g_strdup(tmp_file_name);
	    libbalsa_message_body_save(body, NULL, name);
	    body_type = libbalsa_message_body_get_content_type(body);
	    add_attachment(GNOME_ICON_LIST(msg->attachments[1]), name,
			   body->filename != NULL, body_type);
	    g_free(body_type);
	    body = body->next;
	}
    }

    if (!msg->charset)
	msg->charset = libbalsa_message_charset(message);
    libbalsa_message_body_unref(message);
}

/* quoteBody ------------------------------------------------------------
   quotes properly the body of the message.
   Use GString to optimize memory usage.
*/
static GString *
quoteBody(BalsaSendmsg * msg, LibBalsaMessage * message, SendType type)
{
    GString *body;
    gchar *str, *date = NULL;
    const gchar *personStr;

    libbalsa_message_body_ref(message, TRUE);

    personStr = libbalsa_address_get_name(message->from);
    if (!personStr)
	personStr = _("you");
    if (message->date)
	date = libbalsa_message_date_to_gchar(message, balsa_app.date_string);

    if (type == SEND_FORWARD_ATTACH) {
	const gchar *subject;

	str = g_strdup_printf(_("------forwarded message from %s------\n"), 
			      personStr);
	body = g_string_new(str);
	g_free(str);

	if (date) {
	    str = g_strdup_printf(_("Date: %s\n"), date);
	    g_string_append(body, str);
	    g_free(str);
	}

	subject = LIBBALSA_MESSAGE_GET_SUBJECT(message);
	if (subject) {
	    str = g_strdup_printf(_("Subject: %s\n"), subject);
	    g_string_append(body, str);
	    g_free(str);
	}

	if (message->from) {
	    gchar *from = libbalsa_address_to_gchar(message->from, 0);
	    str = g_strdup_printf(_("From: %s\n"), from);
	    g_string_append(body, str);
	    g_free(from);
	    g_free(str);
	}

	if (message->to_list) {
	    gchar *to_list = libbalsa_make_string_from_list(message->to_list);
	    str = g_strdup_printf(_("To: %s\n"), to_list);
	    g_string_append(body, str);
	    g_free(to_list);
	    g_free(str);
	}

	if (message->cc_list) {
	    gchar *cc_list = libbalsa_make_string_from_list(message->cc_list);
	    str = g_strdup_printf(_("CC: %s\n"), cc_list);
	    g_string_append(body, str);
	    g_free(cc_list);
	    g_free(str);
	}

	str = g_strdup_printf(_("Message-ID: %s\n"), message->message_id);
	g_string_append(body, str);
	g_free(str);

	if (message->references) {
	    GList *ref_list = message->references;

	    str = g_strdup_printf(_("References: %s"),
                                  (gchar *) ref_list->data);
	    g_string_append(body, str);
	    g_free(str);
	    ref_list = ref_list->next;

	    while (ref_list) {
		str = g_strdup_printf(" %s", (gchar *) ref_list->data);
		g_string_append(body, str);
		g_free(str);
		ref_list = ref_list->next;
	    }
		
	    g_string_append(body, "\n");
	}
    } else {
	if (date)
	    str = g_strdup_printf(_("On %s %s wrote:\n"), date, personStr);
	else
	    str = g_strdup_printf(_("%s wrote:\n"), personStr);
	body = content2reply(message,
			     (type == SEND_REPLY || type == SEND_REPLY_ALL || 
			      type == SEND_REPLY_GROUP) ?
			     balsa_app.quote_str : NULL,
			     balsa_app.wordwrap ? balsa_app.wraplength : -1,
			     balsa_app.reply_strip_html, msg->flow);
	if (body)
	    g_string_prepend(body, str);
	else
	    body = g_string_new(str);
	g_free(str);
    }
    
    g_free(date);

    if (!msg->charset)
	msg->charset = libbalsa_message_charset(message);
    libbalsa_message_body_unref(message);
    return body;
}

/* fillBody --------------------------------------------------------------
   fills the body of the message to be composed based on the given message.
   First quotes the original one, if autoquote is set,
   and then adds the signature.
   Optionally prepends the signature to quoted text.
*/
static void
fillBody(BalsaSendmsg * msg, LibBalsaMessage * message, SendType type)
{
    GString *body = NULL;
    gchar *signature;
    gboolean reply_any = (type == SEND_REPLY || type == SEND_REPLY_ALL
                          || type == SEND_REPLY_GROUP);
    gboolean forwd_any = (type == SEND_FORWARD_ATTACH
                          || type == SEND_FORWARD_INLINE);
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text));
    GtkTextIter start;

    if (message && ((balsa_app.autoquote && reply_any)
                    || type == SEND_FORWARD_INLINE))
        body = quoteBody(msg, message, type);
    else
	body = g_string_new("");

    if ((signature = read_signature(msg)) != NULL) {
	if ((reply_any && msg->ident->sig_whenreply)
       || (forwd_any && msg->ident->sig_whenforward)
       || (type == SEND_NORMAL && msg->ident->sig_sending)) {

	    if (msg->ident->sig_separator
		&& g_ascii_strncasecmp(signature, "--\n", 3)
		&& g_ascii_strncasecmp(signature, "-- \n", 4)) {
		gchar * tmp = g_strconcat("-- \n", signature, NULL);
		g_free(signature);
		signature = tmp;
	    }

	    if (msg->ident->sig_prepend && type != SEND_NORMAL) {
	    	g_string_prepend(body, "\n\n");
	    	g_string_prepend(body, signature);
	    } else {
	    	g_string_append(body, signature);
	    }
	    g_string_prepend_c(body, '\n');
	}
	g_free(signature);
    }

    gtk_text_buffer_set_text(buffer, body->str, body->len);
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);
    g_string_free(body, TRUE);
}

static gint insert_signature_cb(GtkWidget *widget, BalsaSendmsg *msg)
{
    gchar *signature;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text));
    
    if ((signature = read_signature(msg)) != NULL) {
	if (msg->ident->sig_separator
	    && g_ascii_strncasecmp(signature, "--\n", 3)
	    && g_ascii_strncasecmp(signature, "-- \n", 4)) {
	    gchar * tmp = g_strconcat("-- \n", signature, NULL);
	    g_free(signature);
	    signature = tmp;
	}
	
        gtk_text_buffer_insert_at_cursor(buffer, signature, -1);
	
	g_free(signature);
    }
    
    return TRUE;
}


static gint quote_messages_cb(GtkWidget *widget, BalsaSendmsg *msg)
{
    return insert_selected_messages(msg, SEND_REPLY);
}


/* set_entry_to_subject:
   set subject entry based on given replied/forwarded/continued message
   and the compose type.
*/
static void
set_entry_to_subject(GtkEntry* entry, LibBalsaMessage * message,
                     SendType type, LibBalsaIdentity* ident)
{
    const gchar *subject, *tmp;
    gchar *newsubject = NULL;
    gint i;

    if(!message) return;
    subject = LIBBALSA_MESSAGE_GET_SUBJECT(message);

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
	    if (message->from && message->from->address_list)
		newsubject = g_strdup_printf("%s from %s",
					     ident->forward_string,
					     libbalsa_address_get_mailbox(message->from, 0));
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
	    if (message->from && message->from->address_list)
		newsubject = 
		    g_strdup_printf("%s %s [%s]",
				    ident->forward_string, 
				    tmp,  libbalsa_address_get_mailbox(message->from, 0));
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
	return;
    default:
	return; /* or g_assert_never_reached() ? */
    }

    gtk_entry_set_text(entry, newsubject);
    g_free(newsubject);
}

static void
text_changed(GtkWidget* w, BalsaSendmsg* msg)
{
    msg->modified = TRUE;
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
setup_headers_from_message(BalsaSendmsg* cw, LibBalsaMessage *message)
{
    set_entry_from_address_list(LIBBALSA_ADDRESS_ENTRY(cw->to[1]),
                                message->to_list);
    set_entry_from_address_list(LIBBALSA_ADDRESS_ENTRY(cw->cc[1]),
                                message->cc_list);
    set_entry_from_address_list(LIBBALSA_ADDRESS_ENTRY(cw->bcc[1]),
                                message->bcc_list);
}


/* 
 * set_identity_from_mailbox
 * 
 * Attempt to determine the default identity from the mailbox containing
 * the message.
 **/
static gboolean
set_identity_from_mailbox(BalsaSendmsg* msg)
{
    gchar *identity;
    LibBalsaMessage *message = msg->orig_message;
    LibBalsaIdentity* ident;
    GList *ilist;

    if( message && message->mailbox && balsa_app.identities) {
        identity = message->mailbox->identity_name;
        if(!identity) return FALSE;
        for (ilist = balsa_app.identities;
             ilist != NULL;
             ilist = g_list_next(ilist)) {
            ident = LIBBALSA_IDENTITY(ilist->data);
            if (!g_ascii_strcasecmp(identity, ident->identity_name)) {
                msg->ident = ident;
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
guess_identity(BalsaSendmsg* msg)
{
    gboolean done = FALSE;
    GList *alist;
    LibBalsaMessage *message = msg->orig_message;
    if( !message || !message->to_list || !balsa_app.identities)
        return FALSE; /* use default */

    /*
     * Loop through all the addresses in the message's To:
     * field, and look for an identity that matches one of them.
     */
    for (alist = message->to_list;!done && alist;alist = alist->next) {
        LibBalsaAddress *addy;
        GList *nth_address, *ilist;
        gchar *address_string;
        
        addy = alist->data;
        nth_address = g_list_nth(addy->address_list, 0);
        address_string = (gchar*)nth_address->data;
        for (ilist = balsa_app.identities;
             !done && ilist;
             ilist = g_list_next(ilist)) {
            LibBalsaIdentity* ident;
            
            ident = LIBBALSA_IDENTITY(ilist->data);
            if (!g_ascii_strcasecmp(address_string,
                              (gchar*)(ident->address->address_list->data))) {
                msg->ident = ident;
                done = TRUE;
		}
        }
    }
    return done;
}

static void
setup_headers_from_identity(BalsaSendmsg* cw, LibBalsaIdentity *ident)    
{
    gchar* str = libbalsa_address_to_gchar(ident->address, 0);
    gtk_entry_set_text(GTK_ENTRY(cw->from[1]), str);
    g_free(str); 
    if(ident->replyto)
        gtk_entry_set_text(GTK_ENTRY(cw->reply_to[1]), ident->replyto);
    if(ident->bcc)
	gtk_entry_set_text(GTK_ENTRY(cw->bcc[1]), ident->bcc);
}

/* Toolbar buttons and their callbacks. */
static const struct callback_item {
    const char *icon_id;
    BalsaToolbarFunc callback;
} callback_table[] = {
    {BALSA_PIXMAP_ATTACHMENT, BALSA_TOOLBAR_FUNC(attach_clicked)},
    {BALSA_PIXMAP_IDENTITY, BALSA_TOOLBAR_FUNC(change_identity_dialog_cb)},
    {BALSA_PIXMAP_POSTPONE, BALSA_TOOLBAR_FUNC(postpone_message_cb)},
    {BALSA_PIXMAP_PRINT, BALSA_TOOLBAR_FUNC(print_message_cb)},
    {BALSA_PIXMAP_SAVE, BALSA_TOOLBAR_FUNC(save_message_cb)},
    {BALSA_PIXMAP_SEND, BALSA_TOOLBAR_FUNC(send_message_toolbar_cb)},
    {GTK_STOCK_CLOSE, BALSA_TOOLBAR_FUNC(close_window_cb)},
    {GTK_STOCK_SPELL_CHECK, BALSA_TOOLBAR_FUNC(spell_check_cb)}
};

/* Standard buttons; "" means a separator. */
static const gchar* compose_toolbar[] = {
    BALSA_PIXMAP_SEND,
    "",
    BALSA_PIXMAP_ATTACHMENT,
    "",
    BALSA_PIXMAP_SAVE,
    "",
    BALSA_PIXMAP_IDENTITY,
    "",
    GTK_STOCK_SPELL_CHECK,
    "",
    BALSA_PIXMAP_PRINT,
    "",
    GTK_STOCK_CLOSE,
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

BalsaSendmsg *
sendmsg_window_new(GtkWidget * widget, LibBalsaMessage * message,
		   SendType type)
{
    BalsaToolbarModel *model;
    GtkWidget *toolbar;
    GtkWidget *window;
    GtkWidget *paned = gtk_vpaned_new();
    BalsaSendmsg *msg = NULL;
    unsigned i;
    gchar* tmp;

    g_assert((type == SEND_NORMAL && message == NULL)
             || (type != SEND_NORMAL && message != NULL));

    msg = g_malloc(sizeof(BalsaSendmsg));
    msg->charset  = NULL;
    msg->locale   = NULL;
    msg->fcc_url  = NULL;
    msg->ident = balsa_app.current_ident;
    msg->update_config = FALSE;
    msg->modified = FALSE; 
    msg->flow = balsa_app.wordwrap && balsa_app.send_rfc2646_format_flowed;
    msg->quit_on_close = FALSE;
    msg->orig_message = message;
    msg->window = window = gnome_app_new("balsa", NULL);
    msg->type = type;

    if (message) {
        /* ref message so we don't lose it even if it is deleted */
	g_object_ref(G_OBJECT(message));
	/* reference the original mailbox so we don't loose the
	   mail even if the mailbox is closed. Alternatively,
	   one could try using weak references or destroy notification
	   to take care of it. In such a case, the orig_message field
	   would be cleared

           we'll reference it both by opening it, in caseit's closed
           elsewhere, and by ref'ing it, in case the mbnode that owns
           it is destroyed in a rescan
	*/
	if (message->mailbox) {
	    libbalsa_mailbox_open(message->mailbox);
            g_object_ref(G_OBJECT(message->mailbox));
        }
    }

    g_signal_connect(G_OBJECT(msg->window), "delete-event",
		     G_CALLBACK(delete_event_cb), msg);
    g_signal_connect(G_OBJECT(msg->window), "destroy",
		     G_CALLBACK(destroy_event_cb), msg);
    g_signal_connect(G_OBJECT(msg->window), "size_allocate",
		     G_CALLBACK(sw_size_alloc_cb), msg);

    fill_language_menu();

    gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, msg);
    /*
     * `Reflow paragraph' and `Reflow message' don't seem to make much
     * sense when we're using `format=flowed'
     * */
    gtk_widget_set_sensitive(edit_menu[EDIT_MENU_REFLOW_PARA].widget,
                             !msg->flow);
    gtk_widget_set_sensitive(edit_menu[EDIT_MENU_REFLOW_MESSAGE].widget,
                             !msg->flow);

    model = sendmsg_window_get_toolbar_model();
    toolbar = balsa_toolbar_new(model);
    for(i=0; i < ELEMENTS(callback_table); i++)
        balsa_toolbar_set_callback(toolbar, callback_table[i].icon_id,
                                   G_CALLBACK(callback_table[i].callback),
                                   msg);

    gnome_app_set_toolbar(GNOME_APP(window), GTK_TOOLBAR(toolbar));

    msg->ready_widgets[0] = file_menu[MENU_FILE_SEND_POS].widget;
    msg->ready_widgets[1] = file_menu[MENU_FILE_QUEUE_POS].widget;
    msg->ready_widgets[2] = file_menu[MENU_FILE_POSTPONE_POS].widget;
    msg->current_language_menu = lang_menu[LANG_CURRENT_POS].widget;

    /* set options - just the Disposition Notification request for now */
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (opts_menu[OPTS_MENU_DISPNOTIFY_POS].widget), balsa_app.req_dispnotify);

    /* Set up the default identity */
    if(!set_identity_from_mailbox(msg))
        /* Get the identity from the To: field of the original message */
        guess_identity(msg);

    /* create the top portion with the to, from, etc in it */
    gtk_paned_add1(GTK_PANED(paned), create_info_pane(msg, type));

    /* create text area for the message */
    gtk_paned_add2(GTK_PANED(paned), create_text_area(msg));

    /* fill in that info: */

    /* To: */
    if (type == SEND_REPLY || type == SEND_REPLY_ALL) {
	LibBalsaAddress *addr = NULL;

	addr = (message->reply_to) ? message->reply_to : message->from;

	tmp = libbalsa_address_to_gchar(addr, 0);
	gtk_entry_set_text(GTK_ENTRY(msg->to[1]), tmp);
	g_free(tmp);
    } else if ( type == SEND_REPLY_GROUP ) {
        set_list_post_address(msg);
    }

    /* From: */
    setup_headers_from_identity(msg, msg->ident);

    /* Subject: */
    set_entry_to_subject(GTK_ENTRY(msg->subject[1]), message, type, msg->ident);

    if (type == SEND_CONTINUE)
	setup_headers_from_message(msg, message);

    if (type == SEND_REPLY_ALL) {
	tmp = libbalsa_make_string_from_list(message->to_list);

	gtk_entry_set_text(GTK_ENTRY(msg->cc[1]), tmp);
	g_free(tmp);

	if (message->cc_list) {
	    tmp = libbalsa_make_string_from_list(message->cc_list);
	    append_comma_separated(GTK_EDITABLE(msg->cc[1]), tmp);
	    g_free(tmp);
	}
    }
    gtk_paned_set_position(GTK_PANED(paned), -1);
    gnome_app_set_contents(GNOME_APP(window), paned);

    if (type == SEND_CONTINUE)
	continueBody(msg, message);
    else
	fillBody(msg, message, type);

    /* set the menus  - and charset index - and display the window */
    /* FIXME: this will also reset the font, copying the text back and 
       forth which is sub-optimal.
     */
    init_menus(msg);

    /* set the initial window title */
    sendmsg_window_set_title(msg);

    /*
     * restore the SendMsg window size
     */
    gtk_window_set_default_size(GTK_WINDOW(window), 
			balsa_app.sw_width,
			balsa_app.sw_height);

    gtk_window_set_wmclass(GTK_WINDOW(window), "compose", "Balsa");

    gtk_widget_show(window);


    if (type == SEND_NORMAL || type == SEND_FORWARD_ATTACH || 
	type == SEND_FORWARD_INLINE)
	gtk_widget_grab_focus(msg->to[1]);
    else
	gtk_widget_grab_focus(msg->text);

    if (type == SEND_FORWARD_ATTACH) {
	if(!attach_message(msg, message))
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Attaching message failed.\n"
                                       "Possible reason: not enough temporary space"));
    }

    if (type == SEND_CONTINUE
        && gnome_icon_list_get_num_icons(GNOME_ICON_LIST
                                         (msg->attachments[1])))
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
                                       (msg->view_checkitems
                                        [MENU_TOGGLE_ATTACHMENTS_POS]),
                                       TRUE);

    msg->update_config = TRUE;
 
    msg->delete_sig_id = 
	g_signal_connect(G_OBJECT(balsa_app.main_window), "delete-event",
			 G_CALLBACK(delete_event_cb), msg);
    g_signal_connect(G_OBJECT
                     (gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg->text))),
                     "changed", G_CALLBACK(text_changed), msg);
    return msg;
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

        gtk_text_buffer_insert_at_cursor(buffer, val, -1);

        return;
    } else if (g_ascii_strcasecmp(key, "to")  ==0) entry = bsmsg->to[1];
    else if(g_ascii_strcasecmp(key, "subject")==0) entry = bsmsg->subject[1];
    else if(g_ascii_strcasecmp(key, "cc")     ==0) entry = bsmsg->cc[1];
    else if(g_ascii_strcasecmp(key, "bcc")    ==0) entry = bsmsg->bcc[1];
    else if(g_ascii_strcasecmp(key, "replyto")==0) entry = bsmsg->reply_to[1];
    else return;

    append_comma_separated(GTK_EDITABLE(entry), val);
}

static gchar *
read_signature(BalsaSendmsg *msg)
{
    FILE *fp = NULL;
    size_t len = 0;
    gchar *ret = NULL;

    if (msg->ident->signature_path == NULL)
	return NULL;

    if(msg->ident->sig_executable){
        /* signature is executable */
        if (!(fp = popen(msg->ident->signature_path,"r")))
            return NULL;
         len = libbalsa_readfile_nostat(fp, &ret);
         pclose(fp);    
	}
     else{
         /* sign is normal file */
         if (!(fp = fopen(msg->ident->signature_path, "r")))
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
                        "Please choose the charset used to encode the file."));
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
            gtk_text_buffer_insert_at_cursor(buffer, s, bytes_written);
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
    string = NULL;
    len = libbalsa_readfile(fl, &string);
    fclose(fl);
    
    if(g_utf8_validate(string, -1, NULL)) {
	gtk_text_buffer_insert_at_cursor(buffer, string, len);
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
        && bsmsg->cc_info.ready && bsmsg->bcc_info.ready
        && bsmsg->reply_to_info.ready;
    return ready;
}

static void
address_changed_cb(LibBalsaAddressEntry * address_entry,
                   BalsaSendmsgAddress * sma)
{
    if (!libbalsa_address_entry_matching(address_entry)) {
        set_ready(address_entry, sma);
        check_readiness(sma->msg);
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
            gtk_widget_set_style(sma->label, sma->msg->bad_address_style);
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
    struct tm *footime;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start, end;

    g_assert(bsmsg != NULL);
    message = libbalsa_message_new();

    ctmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->from[1]));
    message->from = libbalsa_address_new_from_string(ctmp);

    tmp = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->subject[1]), 0, -1);
    strip_chars(tmp, "\r\n");
    LIBBALSA_MESSAGE_SET_SUBJECT(message, tmp);

    ctmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->to[1]));
    message->to_list = libbalsa_address_new_list_from_string(ctmp);

    ctmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->cc[1]));
    message->cc_list = libbalsa_address_new_list_from_string(ctmp);

    ctmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->bcc[1]));
    message->bcc_list = libbalsa_address_new_list_from_string(ctmp);

    /* get the fcc-box from the option menu widget */
    bsmsg->fcc_url =
        g_strdup(balsa_mblist_mru_option_menu_get(bsmsg->fcc[1]));

    ctmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->reply_to[1]));
    if (*ctmp)
	message->reply_to = libbalsa_address_new_from_string(ctmp);

    if (balsa_app.req_dispnotify)
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
	footime = localtime(&bsmsg->orig_message->date);
	strftime(recvtime, sizeof(recvtime),
		 "%a, %b %d, %Y at %H:%M:%S %z", footime);

	if (bsmsg->orig_message->message_id) {
	  message->references = g_list_prepend(
	    message->references, g_strdup(bsmsg->orig_message->message_id));
	    message->in_reply_to =
		g_strconcat(bsmsg->orig_message->message_id, "; from ",
			    (gchar *) bsmsg->orig_message->
			    from->address_list->data, " on ", recvtime,
			    NULL);
	}
    }

    body = libbalsa_message_body_new(message);
    body->disposition = DISPINLINE; /* this is the main body */
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    body->buffer = gtk_text_iter_get_text(&start, &end);

    if (bsmsg->flow) {
        body->buffer =
            libbalsa_wrap_rfc2646(body->buffer, balsa_app.wraplength, TRUE,
                                  FALSE);
    } else if (balsa_app.wordwrap) {
        libbalsa_wrap_string(body->buffer, balsa_app.wraplength);
    }
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
		body->mime_type = g_strdup(attach->force_mime_type);
	    body->attach_as_extbody = attach->as_extbody;
	    body->disposition = attach->disposition;
	    libbalsa_message_append_part(message, body);
	}
    }

    message->date = time(NULL);

    return message;
}

/* "send message" menu and toolbar callback.
 * FIXME: automatic charset detection, as libmutt does for strings?
 */
static gint
send_message_handler(BalsaSendmsg * bsmsg, gboolean queue_only)
{
    gboolean successful = TRUE;
    LibBalsaMessage *message;
    LibBalsaMailbox *fcc;
    const gchar* ctmp;
    gchar *res;
    GError *err = NULL;
    gsize bytes_read, bytes_written;
    GtkTextIter start, end;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    if (!is_ready_to_send(bsmsg))
	return FALSE;

    if (balsa_app.debug)
	fprintf(stderr, "sending with charset: %s\n", bsmsg->charset);

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    ctmp = gtk_text_iter_get_text(&start, &end);
    res = g_convert(ctmp, strlen(ctmp), bsmsg->charset, "UTF-8", 
		    &bytes_read, &bytes_written, &err);

    g_free(res);
    if(err) {
	gchar *err_msg = 
	    g_strdup_printf(_("The message cannot be encoded in charset %s.\n"
			      "Please choose a language for this message."),
			    bsmsg->charset);
	GtkWidget* msgbox = gtk_message_dialog_new(GTK_WINDOW(bsmsg->window),
						   GTK_DIALOG_MODAL,
						   GTK_MESSAGE_ERROR,
						   GTK_BUTTONS_OK,
						   err_msg);
	gtk_dialog_run(GTK_DIALOG(msgbox));
        gtk_widget_destroy(msgbox);
	g_error_free(err);
	return FALSE;
    }

    message = bsmsg2message(bsmsg);
    fcc = balsa_find_mailbox_by_url(bsmsg->fcc_url);

    if(queue_only)
	libbalsa_message_queue(message, balsa_app.outbox, fcc,
                               balsa_app.encoding_style,
                               bsmsg->flow);
    else 
#if ENABLE_ESMTP
	successful = libbalsa_message_send(message, balsa_app.outbox, fcc,
					   balsa_app.encoding_style,  
			   		   balsa_app.smtp_server,
			   		   balsa_app.smtp_authctx,
                                           balsa_app.smtp_tls_mode,
                                           bsmsg->flow);
#else
        successful = libbalsa_message_send(message, balsa_app.outbox, fcc,
					   balsa_app.encoding_style,
					   bsmsg->flow); 
#endif
    if (successful && bsmsg->orig_message) {
	if (bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL ||
	    bsmsg->type == SEND_REPLY_GROUP) {
	    libbalsa_message_reply(bsmsg->orig_message);
	} else if (bsmsg->type == SEND_CONTINUE) {
	    GList * messages = g_list_prepend(NULL, bsmsg->orig_message);

	    libbalsa_messages_delete(messages, TRUE);
	    g_list_free(messages);
	}
    }

    g_object_unref(G_OBJECT(message));
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
	if (bsmsg->type == SEND_CONTINUE && bsmsg->orig_message) {
	    GList * messages = g_list_prepend(NULL, bsmsg->orig_message);

	    libbalsa_messages_delete(messages, TRUE);
	    g_list_free(messages);
	}
    }
    g_object_unref(G_OBJECT(message));
    return successp;
}

/* "postpone message" menu and toolbar callback */
static void
postpone_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    if (is_ready_to_send(bsmsg)) {
        message_postpone(bsmsg);
        gtk_widget_destroy(bsmsg->window);
    }
}


static void
save_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gboolean thereturn;
    
    thereturn = message_postpone(bsmsg);

    if(thereturn) {
	GList *draft_entry;

	if(bsmsg->orig_message) {
	    if(bsmsg->orig_message->mailbox)
		libbalsa_mailbox_close(bsmsg->orig_message->mailbox);
            /* check again! */
	    if(bsmsg->orig_message->mailbox)
	        g_object_unref(G_OBJECT(bsmsg->orig_message->mailbox));
	    g_object_unref(G_OBJECT(bsmsg->orig_message));
	}
	bsmsg->type=SEND_CONTINUE;

	libbalsa_mailbox_open(balsa_app.draftbox);
	draft_entry=g_list_last(balsa_app.draftbox->message_list);
	bsmsg->orig_message=LIBBALSA_MESSAGE(draft_entry->data);
	bsmsg->orig_message->mailbox=balsa_app.draftbox;
	g_object_ref(G_OBJECT(bsmsg->orig_message));
    
    }
}

static void
print_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *msg = bsmsg2message(bsmsg);
    message_print(msg);
    gtk_object_destroy(GTK_OBJECT(msg));
    return;
}

static void
cut_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_NONE);

    gtk_text_buffer_cut_clipboard(buffer, clipboard, TRUE);
}

static void
copy_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_NONE);

    gtk_text_buffer_copy_clipboard(buffer, clipboard);
}
static void
paste_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_NONE);

    gtk_text_buffer_paste_clipboard(buffer, clipboard, NULL, TRUE);
}

static void
select_all_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_move_mark_by_name(buffer, "insert", &start);
    gtk_text_buffer_move_mark_by_name(buffer, "selection_bound", &end);
}

static void
wrap_body_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gint pos;
    gchar *the_text;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start, end, now;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    the_text = gtk_text_iter_get_text(&start, &end);

    gtk_text_buffer_get_iter_at_mark(buffer, &now,
                                     gtk_text_buffer_get_insert(buffer));
    pos = gtk_text_iter_get_offset(&now);

    if (bsmsg->flow) {
        the_text =
            libbalsa_wrap_rfc2646(the_text, balsa_app.wraplength, TRUE,
                                  TRUE);
    } else
        libbalsa_wrap_string(the_text, balsa_app.wraplength);

    gtk_text_buffer_set_text(buffer, the_text, -1);
    gtk_text_buffer_get_iter_at_offset(buffer, &now, pos);
    gtk_text_buffer_place_cursor(buffer, &now);
    g_free(the_text);
}

static void
do_reflow(GtkTextView * txt, gint mode)
{
    gint pos;
    gchar *the_text;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(txt);
    GtkTextIter start, end, now;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    the_text = gtk_text_iter_get_text(&start, &end);

    gtk_text_buffer_get_iter_at_mark(buffer, &now,
                                     gtk_text_buffer_get_insert(buffer));
    pos = gtk_text_iter_get_offset(&now);

    reflow_string(the_text, mode, &pos, balsa_app.wraplength);

    gtk_text_buffer_set_text(buffer, the_text, -1);
    gtk_text_buffer_get_iter_at_offset(buffer, &now, pos);
    gtk_text_buffer_place_cursor(buffer, &now);
    g_free(the_text);
}

static void
reflow_par_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    do_reflow(GTK_TEXT_VIEW(bsmsg->text), 0);
}

static void
reflow_body_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    do_reflow(GTK_TEXT_VIEW(bsmsg->text), -1);
}

/* To field "changed" signal callback. */
static void
check_readiness(BalsaSendmsg * msg)
{
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(msg->window));
    unsigned i;
    gboolean state = is_ready_to_send(msg);

    for (i = 0; i < ELEMENTS(msg->ready_widgets); i++)
        gtk_widget_set_sensitive(msg->ready_widgets[i], state);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_SEND, state);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_POSTPONE, state);
}

/* toggle_entry:
   auxiliary function for "header show/hide" toggle menu entries.
   saves the show header configuration.
 */
static gint
toggle_entry(BalsaSendmsg * bmsg, GtkWidget * entry[], int pos, int cnt)
{
    unsigned i;
    GtkWidget *parent;
    gchar str[ELEMENTS(headerDescs) * 20]; /* assumes that longest header ID
					      has no more than 19 chars   */

    if (GTK_CHECK_MENU_ITEM(bmsg->view_checkitems[pos])->active) {
	while (cnt--)
	    gtk_widget_show(GTK_WIDGET(entry[cnt]));
    } else {
	while (cnt--)
	    gtk_widget_hide(GTK_WIDGET(entry[cnt]));

	/* force size recomputation if embedded in paned */
        for (parent = entry[0]; parent; 
             parent = gtk_widget_get_parent(parent))
	    if (GTK_IS_PANED(parent)) {
	        gtk_paned_set_position(GTK_PANED(parent), -1);
                break;
            }
    }

    if(bmsg->update_config) { /* then save the config */
	str[0] = '\0';
	for (i = 0; i < ELEMENTS(headerDescs); i++)
	    if (GTK_CHECK_MENU_ITEM(bmsg->view_checkitems[i])->active) {
		strcat(str, headerDescs[i].name);
		strcat(str, " ");
	    }
	g_free(balsa_app.compose_headers);
	balsa_app.compose_headers = g_strdup(str);
    }

    return TRUE;
}

static gint
toggle_to_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->to, MENU_TOGGLE_TO_POS, 3);
}

static gint
toggle_from_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->from, MENU_TOGGLE_FROM_POS, 3);
}

static gint
toggle_subject_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->subject, MENU_TOGGLE_SUBJECT_POS, 2);
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

static gint
toggle_reply_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->reply_to, MENU_TOGGLE_REPLY_POS, 3);
}

static gint
toggle_attachments_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->attachments,
			MENU_TOGGLE_ATTACHMENTS_POS, 4);
}

static gint
toggle_comments_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->comments, MENU_TOGGLE_COMMENTS_POS,
			2);}

static gint
toggle_keywords_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    return toggle_entry(bsmsg, bsmsg->keywords, MENU_TOGGLE_KEYWORDS_POS,
			2);
}

static gint
toggle_reqdispnotify_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    balsa_app.req_dispnotify = GTK_CHECK_MENU_ITEM(widget)->active;
    return TRUE;
}

static gint
toggle_queue_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    balsa_app.always_queue_sent_mail = GTK_CHECK_MENU_ITEM(widget)->active;
    return TRUE;
}

/* init_menus:
   performs the initial menu setup: shown headers as well as correct
   message charset. Copies also the the menu pointers for further usage
   at the message close  - they would be overwritten if another compose
   window was opened simultaneously.
*/
static void
init_menus(BalsaSendmsg * msg)
{
    unsigned i;

    g_assert(ELEMENTS(headerDescs) == ELEMENTS(msg->view_checkitems));

    for (i = 0; i < ELEMENTS(headerDescs); i++) {
	msg->view_checkitems[i] = view_menu[i].widget;
	if (libbalsa_find_word(headerDescs[i].name, 
			       balsa_app.compose_headers)) {
	    /* show... (well, it has already been shown). */
	    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
					   (view_menu[i].widget), TRUE);
	} else {
	    /* or hide... */
	    VIEW_MENU_FUNC(view_menu[i].moreinfo)(view_menu[i].widget, msg);
	}
    }

    /* set the charset... */
    i = find_locale_index_by_locale(setlocale(LC_CTYPE, NULL));
    if (msg->charset
	&& g_ascii_strcasecmp(locales[i].charset, msg->charset) != 0) {
	for(i=0; 
	    i<ELEMENTS(locales) && 
		g_ascii_strcasecmp(locales[i].charset, msg->charset) != 0;
	    i++)
	    ;
    }
    if (i == ELEMENTS(locales))
	i = LOC_ENGLISH_POS;
    
    set_locale(NULL, msg, i);

    /* gray 'send' and 'postpone' */
    check_readiness(msg);
}

/* set_locale:
   w - menu widget that has been changed. if the even is generated during 
   menu initialization, w is NULL.
   msg is the compose window,
   idx - corresponding entry index in locale_names.
*/

static gint
set_locale(GtkWidget * w, BalsaSendmsg * msg, gint idx)
{
    gchar *tmp;

    g_free(msg->charset);
    msg->charset = g_strdup(locales[idx].charset);
    msg->locale = locales[idx].locale;
    tmp = g_strdup_printf("%s (%s, %s)", _(locales[idx].lang_name),
			  locales[idx].locale, locales[idx].charset);
    gtk_label_set_text(GTK_LABEL
		       (GTK_BIN(msg->current_language_menu)->child), tmp);
    g_free(tmp);
    return FALSE;
}

/* spell_check_cb
 * 
 * Start the spell check
 * */
static void
spell_check_cb(GtkWidget * widget, BalsaSendmsg * msg)
{
    BalsaSpellCheck *sc;

    sc = BALSA_SPELL_CHECK(msg->spell_checker);

    /* configure the spell checker */
    balsa_spell_check_set_language(sc, msg->locale);

    balsa_spell_check_set_character_set(sc, msg->charset);
    balsa_spell_check_set_module(sc,
				 spell_check_modules_name
				 [balsa_app.module]);
    balsa_spell_check_set_suggest_mode(sc,
				       spell_check_suggest_mode_name
				       [balsa_app.suggestion_mode]);
    balsa_spell_check_set_ignore_length(sc, balsa_app.ignore_size);

    /* this will block until the dialog is closed */
    balsa_spell_check_start(sc);

    check_readiness(msg);
}

static void
lang_set_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    gint i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),
                                               GNOMEUIINFO_KEY_UIDATA));
    set_locale(w, bsmsg, i);
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
            gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);
            g_string_free(body, TRUE);
        }
    }

    return bsmsg;
}

/* set_list_post_address:
 * look for the address for posting messages to a list */
static void
set_list_post_address(BalsaSendmsg * msg)
{
    LibBalsaMessage *message = msg->orig_message;

    if (message->mailbox->mailing_list_address) {
        gchar *tmp =
            libbalsa_address_to_gchar(message->mailbox->
                                      mailing_list_address, 0);
        gtk_entry_set_text(GTK_ENTRY(msg->to[1]), tmp);
        g_free(tmp);
    } else {
        GList *lst = libbalsa_message_user_hdrs(message);
        if (!set_list_post_rfc2369(msg, lst)) {
            /* we didn't find "list-post", so try some nonstandard
             * alternatives: */
            GList *p;
            for (p = lst; p; p = g_list_next(p)) {
                gchar **pair = p->data;
                if (libbalsa_find_word(pair[0],
                                       "x-beenthere x-mailing-list")) {
                    gtk_entry_set_text(GTK_ENTRY(msg->to[1]), pair[1]);
                    break;
                }
            }
        }
        g_list_foreach(lst, (GFunc) g_strfreev, NULL);
        g_list_free(lst);
    }
}

/* set_list_post_rfc2369:
 * look for "List-Post:" header, and get the address */
static gboolean
set_list_post_rfc2369(BalsaSendmsg * msg, GList * p)
{
    while (p) {
        gchar **pair = p->data;
        if (libbalsa_find_word(pair[0], "list-post")) {
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
            gchar *url = pair[1];
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
                                               sendmsg_window_set_field,
                                               msg);
                    return TRUE;
                }
                if (!(*++close
                      && *(close = rfc2822_skip_comments(close)) == ','))
                    break;
                url = ++close;
            }
            /* RFC 2369: There MUST be no more than one of each field
             * present in any given message; so we can quit after
             * (unsuccessfully) processing one. */
            return FALSE;
        }
        /* it wasn't a list-post line */
        p = g_list_next(p);
    }
    /* we didn't find a list-post line */
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
sendmsg_window_set_title(BalsaSendmsg * msg)
{
    gchar *title_format;
    gchar *title;

    if (libbalsa_address_entry_matching(LIBBALSA_ADDRESS_ENTRY(msg->to[1])))
        return;

    switch (msg->type) {
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
                            gtk_entry_get_text(GTK_ENTRY(msg->to[1])),
                            gtk_entry_get_text(GTK_ENTRY(msg->subject[1])));
    gtk_window_set_title(GTK_WINDOW(msg->window), title);
    g_free(title);
}
