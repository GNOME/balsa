/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-2001 Stuart Parmenter and others, see AUTHORS file.
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

#include "libbalsa.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
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

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "balsa-icons.h"

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#include "threads.h"
#endif

#include "sendmsg-window.h"
#include "address-book.h"
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
static void spell_check_done_cb(BalsaSpellCheck * spell_check,
				BalsaSendmsg *);
static void spell_check_set_sensitive(BalsaSendmsg * msg, gboolean state);

static void address_book_cb(GtkWidget *widget, BalsaSendmsg *smd_msg_wind);

static gint set_locale(GtkWidget *, BalsaSendmsg *, gint);

static void edit_with_gnome(GtkWidget* widget, BalsaSendmsg* msg);
static void change_identity_dialog_cb(GtkWidget*, BalsaSendmsg*);
static void update_msg_identity(BalsaSendmsg*, LibBalsaIdentity*);

static void sw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc);
static GString *
quoteBody(BalsaSendmsg * msg, LibBalsaMessage * message, SendType type);
static void set_list_post_address(BalsaSendmsg * msg);
static gboolean set_list_post_rfc2369(BalsaSendmsg * msg, GList * p);
static gchar *rfc2822_skip_comments(gchar * str);
static void address_changed_cb(GtkEditable * w, BalsaSendmsgAddress *sma);
static void set_ready(GtkEditable * w, BalsaSendmsgAddress *sma);

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

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

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
			   include_file_cb, GNOME_STOCK_MENU_OPEN),
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
      BALSA_PIXMAP_MENU_SEND, 'S', GDK_CONTROL_MASK, NULL },
#define MENU_FILE_QUEUE_POS 6
    { GNOME_APP_UI_ITEM, N_("_Queue"),
      N_("Queue this message in Outbox for sending"),
      queue_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_SEND, 'Q', GDK_CONTROL_MASK, NULL },
#define MENU_FILE_POSTPONE_POS 7
    GNOMEUIINFO_ITEM_STOCK(N_("_Postpone"), NULL,
			   postpone_message_cb, BALSA_PIXMAP_MENU_POSTPONE),
#define MENU_FILE_SAVE_POS 8
    GNOMEUIINFO_ITEM_STOCK(N_("_Save"), NULL,
			   save_message_cb, BALSA_PIXMAP_MENU_SAVE),
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
    {GNOME_APP_UI_ITEM, N_("Insert _Signature"), NULL,
     (gpointer) insert_signature_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_z, GDK_CONTROL_MASK, NULL},
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
    GNOMEUIINFO_ITEM_STOCK(N_("_Check Spelling"), 
                           N_("Check the spelling of the message"),
                           spell_check_cb,
                           GNOME_STOCK_MENU_SPELLCHECK),
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

static char * main_toolbar_spell_disable[] = {
    BALSA_PIXMAP_SEND,
    BALSA_PIXMAP_ATTACHMENT,
    BALSA_PIXMAP_SAVE,
    BALSA_PIXMAP_IDENTITY,
    GNOME_STOCK_PIXMAP_SPELLCHECK,
    GNOME_STOCK_PIXMAP_CLOSE
};

#if MENU_TOGGLE_KEYWORDS_POS+1 != VIEW_MENU_LENGTH
#error Inconsistency in defined lengths.
#endif

static void lang_brazilian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_catalan_cb(GtkWidget *, BalsaSendmsg *);
static void lang_chinese_simplified_cb(GtkWidget *, BalsaSendmsg *);
static void lang_chinese_traditional_cb(GtkWidget *, BalsaSendmsg *);
static void lang_danish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_german_cb(GtkWidget *, BalsaSendmsg *);
static void lang_dutch_cb(GtkWidget *, BalsaSendmsg *);
static void lang_english_cb(GtkWidget *, BalsaSendmsg *);
static void lang_estonian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_finnish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_french_cb(GtkWidget *, BalsaSendmsg *);
static void lang_greek_cb(GtkWidget *, BalsaSendmsg *);
static void lang_hungarian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_italian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_japanese_cb(GtkWidget *, BalsaSendmsg *);
static void lang_korean_cb(GtkWidget *, BalsaSendmsg *);
static void lang_latvian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_lithuanian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_norwegian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_polish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_portugese_cb(GtkWidget *, BalsaSendmsg *);
static void lang_romanian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_russian_iso_cb(GtkWidget *, BalsaSendmsg *);
static void lang_russian_koi_cb(GtkWidget *, BalsaSendmsg *);
static void lang_slovak_cb(GtkWidget *, BalsaSendmsg *);
static void lang_spanish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_swedish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_turkish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_ukrainian_cb(GtkWidget *, BalsaSendmsg *);

static GnomeUIInfo locale_aj_menu[] = {
    GNOMEUIINFO_ITEM_NONE(N_("Brazilian"), NULL, lang_brazilian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Catalan"), NULL, lang_catalan_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Chinese Simplified"), NULL, lang_chinese_simplified_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Chinese Traditional"), NULL, lang_chinese_traditional_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Catalan"), NULL, lang_catalan_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Danish"), NULL, lang_danish_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Dutch"), NULL, lang_dutch_cb),
    GNOMEUIINFO_ITEM_NONE(N_("English"), NULL, lang_english_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Estonian"), NULL, lang_estonian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Finnish"), NULL, lang_finnish_cb),
    GNOMEUIINFO_ITEM_NONE(N_("French"), NULL, lang_french_cb),
    GNOMEUIINFO_ITEM_NONE(N_("German"), NULL, lang_german_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Greek"), NULL, lang_greek_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Hungarian"), NULL, lang_hungarian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Italian"), NULL, lang_italian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Japanese"), NULL, lang_japanese_cb),
    GNOMEUIINFO_END
};

static GnomeUIInfo locale_kz_menu[] = {
    GNOMEUIINFO_ITEM_NONE(N_("Korean"), NULL, lang_korean_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Latvian"), NULL, lang_latvian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Lithuanian"), NULL, lang_lithuanian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Norwegian"), NULL, lang_norwegian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Polish"), NULL, lang_polish_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Portugese"), NULL, lang_portugese_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Romanian"), NULL, lang_romanian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Russian (ISO)"), NULL, lang_russian_iso_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Russian (KOI)"), NULL, lang_russian_koi_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Slovak"), NULL, lang_slovak_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Spanish"), NULL, lang_spanish_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Swedish"), NULL, lang_swedish_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Turkish"), NULL, lang_turkish_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Ukrainian"), NULL, lang_ukrainian_cb),
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

typedef struct {
    gchar *filename;
    gchar *force_mime_type;
    gboolean delete_on_destroy;
    gboolean as_extbody;
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

/* the array of locale names and charset names included in the MIME
   type information.  
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
#define LOC_HUNGARIAN_POS 12
    {"hu_HU", "ISO-8859-2", N_("Hungarian")},
#define LOC_ITALIAN_POS   13
    {"it_IT", "ISO-8859-15", N_("Italian")},
#define LOC_JAPANESE_POS  14
    {"ja_JP", "euc-jp", N_("Japanese")},
#define LOC_KOREAN_POS    15
    {"ko_KR", "euc-kr", N_("Korean")},
#define LOC_LATVIAN_POS    16
    {"lv_LV", "ISO-8859-13", N_("Latvian")},
#define LOC_LITHUANIAN_POS    17
    {"lt_LT", "ISO-8859-13", N_("Lithuanian")},
#define LOC_NORWEGIAN_POS 18
    {"no_NO", "ISO-8859-1", N_("Norwegian")},
#define LOC_POLISH_POS    19
    {"pl_PL", "ISO-8859-2", N_("Polish")},
#define LOC_PORTUGESE_POS 20
    {"pt_PT", "ISO-8859-15", N_("Portugese")},
#define LOC_ROMANIAN_POS 21
    {"ro_RO", "ISO-8859-2", N_("Romanian")},
#define LOC_RUSSIAN_ISO_POS   22
    {"ru_SU", "ISO-8859-5", N_("Russian (ISO)")},
#define LOC_RUSSIAN_KOI_POS   23
    {"ru_RU", "KOI8-R", N_("Russian (KOI)")},
#define LOC_SLOVAK_POS    24
    {"sk_SK", "ISO-8859-2", N_("Slovak")},
#define LOC_SPANISH_POS   25
    {"es_ES", "ISO-8859-15", N_("Spanish")},
#define LOC_SWEDISH_POS   26
    {"sv_SE", "ISO-8859-1", N_("Swedish")},
#define LOC_TURKISH_POS   27
    {"tr_TR", "ISO-8859-9", N_("Turkish")},
#define LOC_UKRAINIAN_POS 28
    {"uk_UK", "KOI8-U", N_("Ukrainian")}
};

static gint mail_headers_page;
static gint spell_check_page;


/* the callback handlers */
static void
address_book_cb(GtkWidget *widget, BalsaSendmsg *snd_msg_wind)
{
    GtkWidget *ab;
    GtkEntry *entry;
    gint button;

    entry = GTK_ENTRY(gtk_object_get_data(GTK_OBJECT(widget), "address-entry-widget"));

    ab = balsa_address_book_new(TRUE);
    gnome_dialog_set_parent(GNOME_DIALOG(ab), 
			    GTK_WINDOW(snd_msg_wind->window));

    gnome_dialog_close_hides (GNOME_DIALOG(ab), TRUE);
    button = gnome_dialog_run(GNOME_DIALOG(ab));
    if ( button == 0 ) {
	gchar *t;
	t = balsa_address_book_get_recipients(BALSA_ADDRESS_BOOK(ab));
	if ( t ) 
	    gtk_entry_set_text(GTK_ENTRY(entry), t);
	g_free(t);
    }
    gnome_dialog_close(GNOME_DIALOG(ab));
}

static gint
delete_handler(BalsaSendmsg* bsmsg)
{
    gint reply;
    if(balsa_app.debug) printf("delete_event_cb\n");
    if(bsmsg->modified) {
	gchar* str = 
	    g_strdup_printf(_("The message to '%s' is modified.\n"
			      "Save message to Draftbox?"),
			    gtk_entry_get_text(GTK_ENTRY(bsmsg->to[1])));
	GtkWidget* l = gtk_label_new(str);
	GnomeDialog* d =
	    GNOME_DIALOG(gnome_dialog_new(_("Closing the Compose Window"),
					  GNOME_STOCK_BUTTON_YES,
					  GNOME_STOCK_BUTTON_NO,
					  GNOME_STOCK_BUTTON_CANCEL,
					  NULL));
	gnome_dialog_set_accelerator(GNOME_DIALOG(d), 0, 'Y', 0);
	gnome_dialog_set_accelerator(GNOME_DIALOG(d), 1, 'N', 0);

	g_free(str);
	gnome_dialog_set_parent(d, GTK_WINDOW(bsmsg->window));
	gtk_widget_show(l);
	gtk_box_pack_start_defaults(GTK_BOX(d->vbox), l);
	reply = gnome_dialog_run_and_close(GNOME_DIALOG(d));
	if(reply == 0)
	    message_postpone(bsmsg);
	/* cancel action  when reply = "yes" or "no" */
	return (reply != 0) && (reply != 1);
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
    g_assert(bsm != NULL);
    g_assert(ELEMENTS(headerDescs) == ELEMENTS(bsm->view_checkitems));

    gtk_signal_disconnect(GTK_OBJECT(balsa_app.main_window), 
			  bsm->delete_sig_id);
    if(balsa_app.debug) g_message("balsa_sendmsg_destroy()_handler: Start.");

    if (bsm->orig_message) {
	if (bsm->orig_message->mailbox)
	    libbalsa_mailbox_close(bsm->orig_message->mailbox);
	gtk_object_unref(GTK_OBJECT(bsm->orig_message));
    }

    if (balsa_app.debug)
	printf("balsa_sendmsg_destroy_handler: Freeing bsm\n");
    release_toolbars(bsm->window);
    gtk_widget_destroy(bsm->window);
    g_list_free(bsm->spell_check_disable_list);
    if (bsm->font) {
	gdk_font_unref(bsm->font);
	bsm->font = NULL;
    }
    if (bsm->bad_address_style)
        gtk_style_unref(bsm->bad_address_style);

    g_free(bsm);


#ifdef BALSA_USE_THREADS
    if (bsm->quit_on_close) {
        libbalsa_wait_for_sending_thread(-1);
	gtk_main_quit();
    }
#else
    if (bsm->quit_on_close)
	gtk_main_quit();
#endif
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
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->to[1]), line+4);
            else if(!strncmp(line, "From: ", 6))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->from[1]), line+6);
            else if(!strncmp(line, "Reply-To: ", 10))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->reply_to[1]), 
                                   line+10);
            else if(!strncmp(line, "Bcc: ", 5))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->bcc[1]), line+5);
            else if(!strncmp(line, "Cc: ", 4))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->cc[1]), line+4);
            else if(!strncmp(line, "Comments: ", 10))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->comments[1]), 
                                   line+10);
            else if(!strncmp(line, "Subject: ", 9))
                gtk_entry_set_text(GTK_ENTRY(data_real->msg->subject[1]), 
                                   line+9);
            else break;
	}
    }
    gtk_editable_delete_text(GTK_EDITABLE(data_real->msg->text),0,-1);
    curposition = 0;
    while(fgets(line, sizeof(line), tmp)) {
        gtk_editable_insert_text(GTK_EDITABLE(data_real->msg->text),line, 
                                 strlen(line), &curposition);
    }
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
    pid_t pid, pid_ext;
    FILE *tmp;
    int tmpfd;

    strcpy(filename, TMP_PATTERN);
    tmpfd = mkstemp(filename);
    tmp   = fdopen(tmpfd, "w+");
    
    if(balsa_app.edit_headers) {
	gchar *from = gtk_entry_get_text(GTK_ENTRY(msg->from[1])),
            *to = gtk_entry_get_text(GTK_ENTRY(msg->to[1])),
            *reply_to =
            gtk_entry_get_text(GTK_ENTRY(msg->reply_to[1])), *cc =
            gtk_entry_get_text(GTK_ENTRY(msg->cc[1])),
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
    gtk_widget_set_sensitive(msg->text, FALSE);
    fputs(gtk_editable_get_chars(GTK_EDITABLE(msg->text), 0,
                                 gtk_text_get_length(GTK_TEXT
                                                     (msg->text))), tmp);
    fclose(tmp);
    if ((pid = fork()) < 0) {
        perror ("fork");
        return; 
    } 
    if (pid == 0) {
        setpgrp();
        
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
    LibBalsaIdentity* ident;

    ident = libbalsa_identity_select_dialog(GTK_WINDOW(msg->window),
					    _("Select Identity"),
					    &balsa_app.identities,
					    &msg->ident);
    if (ident != NULL)        
	update_msg_identity(msg, ident);
}


static void
update_msg_identity(BalsaSendmsg* msg, LibBalsaIdentity* ident)
{
    gchar* tmpstr=libbalsa_address_to_gchar(ident->address, 0);
    
    /* change entries to reflect new identity */
    gtk_entry_set_text(GTK_ENTRY(msg->from[1]), tmpstr);
    g_free(tmpstr);

    gtk_entry_set_text(GTK_ENTRY(msg->reply_to[1]), ident->replyto);
    gtk_entry_set_text(GTK_ENTRY(msg->bcc[1]), ident->bcc);
    
    /* change the subject to use the reply/forward strings */

    /* remove/add the signature depending on the new settings, change
     * the signature if path changed */

    /* update the current messages identity */
    msg->ident=ident;
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
    gint num = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(ilist),
						   "selectednumbertoremove"));
    gnome_icon_list_remove(ilist, num);
    gtk_object_remove_data(GTK_OBJECT(ilist), "selectednumbertoremove");
}


/* ask if an attachment shall be message/external-body */
static void
extbody_dialog_delete(GtkWidget *dialog, GdkEvent *event, 
		      gpointer user_data)
{
    GnomeIconList *ilist = 
	GNOME_ICON_LIST(gtk_object_get_user_data (GTK_OBJECT (dialog)));
    gtk_object_remove_data(GTK_OBJECT(ilist), "selectednumbertoextbody");
    gtk_widget_hide (dialog);
    gtk_object_destroy(GTK_OBJECT(dialog));
}

static void
no_change_to_extbody(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog = GTK_WIDGET(user_data);
    GnomeIconList *ilist;

    ilist = 
	GNOME_ICON_LIST(gtk_object_get_user_data (GTK_OBJECT (dialog)));
    gtk_object_remove_data(GTK_OBJECT(ilist), "selectednumbertoextbody");
    gtk_widget_hide (dialog);
    gtk_object_destroy(GTK_OBJECT(dialog));
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

    pix = libbalsa_icon_finder("message/external-body", attach->filename);
    label = g_strdup_printf ("%s (%s)", attach->filename, 
			     "message/external-body");
    pos = gnome_icon_list_append(ilist, pix, label);
    gnome_icon_list_set_icon_data_full(ilist, pos, attach, destroy_attachment);
    g_free(pix);
    g_free(label);
}


/* send attachment as external body - right mouse button callback */
static void
extbody_attachment(GtkWidget * widget, gpointer user_data)
{
    GtkWidget *dialog = GTK_WIDGET(user_data);
    GnomeIconList *ilist;
    gint num;
    attachment_t *oldattach;

    ilist = 
	GNOME_ICON_LIST(gtk_object_get_user_data (GTK_OBJECT (dialog)));
    num = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(ilist),
					      "selectednumbertoextbody"));
    oldattach = 
	(attachment_t *)gnome_icon_list_get_icon_data(ilist, num);
    g_return_if_fail(oldattach);
    gtk_object_remove_data(GTK_OBJECT(ilist), "selectednumbertoextbody");
    gtk_widget_hide (dialog);
    gtk_object_destroy(GTK_OBJECT(dialog));

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
    GtkWidget *dialog_vbox;
    GtkWidget *hbox;
    GtkWidget *pixmap;
    GtkWidget *label;
    GtkWidget *dialog_action_area;
    GtkWidget *button_no;
    GtkWidget *button_yes;
    gchar *l_text;
    gint num;
    attachment_t *attach;
    
    extbody_dialog = gnome_dialog_new (_("attach as reference?"), NULL);
    gtk_object_set_user_data (GTK_OBJECT (extbody_dialog), ilist);
    
    dialog_vbox = GNOME_DIALOG (extbody_dialog)->vbox;
    
    hbox = gtk_hbox_new (FALSE, 10);
    gtk_box_pack_start (GTK_BOX (dialog_vbox), hbox, TRUE, TRUE, 0);
    
    pixmap = gnome_pixmap_new_from_file (GNOME_DATA_PREFIX "/pixmaps/gnome-question.png");
    gtk_box_pack_start (GTK_BOX (hbox), pixmap, TRUE, TRUE, 0);

    num = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(ilist),
					      "selectednumbertoextbody"));
    attach = (attachment_t *)gnome_icon_list_get_icon_data(ilist, num);
    l_text = g_strdup_printf(
        _("Saying yes will not send the file `%s' itself, but just a MIME "
	  "message/external-body reference.  Note that the recipient must "
	  "have proper permissions to see the `real' file.\n\n"
	  "Do you really want to attach this file as reference?"),
	attach->filename);
    label = gtk_label_new (l_text);
    g_free (l_text);
    gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    
    dialog_action_area = GNOME_DIALOG (extbody_dialog)->action_area;
    gtk_button_box_set_layout (GTK_BUTTON_BOX (dialog_action_area), 
			       GTK_BUTTONBOX_END);
    gtk_button_box_set_spacing (GTK_BUTTON_BOX (dialog_action_area), 8);
    
    gnome_dialog_append_button (GNOME_DIALOG (extbody_dialog), 
				GNOME_STOCK_BUTTON_NO);
    button_no = g_list_last (GNOME_DIALOG (extbody_dialog)->buttons)->data;
    GTK_WIDGET_SET_FLAGS (button_no, GTK_CAN_DEFAULT);
    
    gnome_dialog_append_button (GNOME_DIALOG (extbody_dialog), 
				GNOME_STOCK_BUTTON_YES);
    button_yes = g_list_last (GNOME_DIALOG (extbody_dialog)->buttons)->data;
    GTK_WIDGET_SET_FLAGS (button_yes, GTK_CAN_DEFAULT);
    
    gtk_signal_connect (GTK_OBJECT (extbody_dialog), "delete_event",
			GTK_SIGNAL_FUNC (extbody_dialog_delete), NULL);
    gtk_signal_connect (GTK_OBJECT (button_no), "clicked",
			GTK_SIGNAL_FUNC (no_change_to_extbody), extbody_dialog);
    gtk_signal_connect (GTK_OBJECT (button_yes), "clicked",
			GTK_SIGNAL_FUNC (extbody_attachment), extbody_dialog);
    
    gtk_widget_grab_focus (button_no);
    gtk_widget_grab_default (button_no);
    gtk_widget_show_all(extbody_dialog);
}

/* send attachment as "real" file - right mouse button callback */
static void
file_attachment(GtkWidget * widget, GnomeIconList * ilist)
{
    gint num = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(ilist),
						   "selectednumbertofile"));
    attachment_t *attach, *oldattach;
    gchar *pix, *label;
    const gchar *content_type;

    oldattach = 
	(attachment_t *)gnome_icon_list_get_icon_data(ilist, num);
    g_return_if_fail(oldattach);
    gtk_object_remove_data(GTK_OBJECT(ilist), "selectednumbertofile");

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
    content_type = attach->force_mime_type ? attach->force_mime_type 
	: libbalsa_lookup_mime_type(attach->filename);
    pix = libbalsa_icon_finder(content_type, attach->filename);
    label = g_strdup_printf ("%s (%s)", g_basename(attach->filename), 
			     content_type);
    gnome_icon_list_insert(ilist, num, pix, label);
    gnome_icon_list_set_icon_data_full(ilist, num, attach, destroy_attachment);
    g_free(label);
    g_free(pix);
    gnome_icon_list_thaw(ilist);
}

/* the menu is created on right-button click on an attachement */
static GtkWidget *
create_popup_menu(GnomeIconList * ilist, gint num)
{
    GtkWidget *menu, *menuitem;
    attachment_t *attach = 
	(attachment_t *)gnome_icon_list_get_icon_data(ilist, num);

    menu = gtk_menu_new();
    menuitem = gtk_menu_item_new_with_label(_("Remove"));
    gtk_object_set_data(GTK_OBJECT(ilist), "selectednumbertoremove",
			GINT_TO_POINTER(num));
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(remove_attachment), ilist);
    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);

    /* a "real" (not temporary) file can be attached as external body */
    if (!attach->delete_on_destroy) {
	if (!attach->as_extbody) {
	    menuitem = gtk_menu_item_new_with_label(_("attach as reference"));
	    gtk_object_set_data(GTK_OBJECT(ilist), "selectednumbertoextbody",
	    			GINT_TO_POINTER(num));
	    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
	    		       GTK_SIGNAL_FUNC(show_extbody_dialog), ilist);
	} else {
	    menuitem = gtk_menu_item_new_with_label(_("attach as file"));
	    gtk_object_set_data(GTK_OBJECT(ilist), "selectednumbertofile",
	    			GINT_TO_POINTER(num));
	    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
	    		       GTK_SIGNAL_FUNC(file_attachment), ilist);
	}
	gtk_menu_append(GTK_MENU(menu), menuitem);
	gtk_widget_show(menuitem);
    }

    return menu;
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
	gtk_menu_popup(GTK_MENU(create_popup_menu(ilist, num)),
		       NULL, NULL, NULL, NULL, event->button, event->time);
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
void
add_attachment(GnomeIconList * iconlist, char *filename, 
               gboolean is_a_temp_file, gchar *forced_mime_type)
{
    GtkWidget *msgbox;
    const gchar *content_type;
    gchar *pix, *err_msg;

    content_type = forced_mime_type ? forced_mime_type 
	: libbalsa_lookup_mime_type(filename);
    pix = libbalsa_icon_finder(content_type, filename);

    if (balsa_app.debug)
	fprintf(stderr, "Trying to attach '%s'\n", filename);
    if ( (err_msg=check_if_regular_file(filename)) != NULL) {
	msgbox = gnome_message_box_new(err_msg, GNOME_MESSAGE_BOX_ERROR,
				       _("Cancel"), NULL);
	gtk_window_set_modal(GTK_WINDOW(msgbox), TRUE);
	gnome_dialog_run(GNOME_DIALOG(msgbox));
	g_free(err_msg);
	return;
    }

    if (pix && (err_msg=check_if_regular_file(pix)) == NULL) {
	gint pos;
	gchar *label;
	attachment_t *attach_data = g_malloc(sizeof(attachment_t));

	label = g_strdup_printf ("%s (%s)", g_basename(filename), content_type);

	pos = gnome_icon_list_append(iconlist, pix, label);
	attach_data->filename = filename;
	attach_data->force_mime_type = forced_mime_type 
	    ? g_strdup(forced_mime_type): NULL;

	attach_data->delete_on_destroy = is_a_temp_file;
	attach_data->as_extbody = FALSE;
	gnome_icon_list_set_icon_data_full(iconlist, pos, attach_data, destroy_attachment);

	g_free(label);

    } else { 
	if(pix) {
	    balsa_information
		(LIBBALSA_INFORMATION_ERROR,
		 _("The attachment pixmap (%s) cannot be used.\n %s"),
		 pix, err_msg);
	    g_free(err_msg);
	} else
	    balsa_information
		(LIBBALSA_INFORMATION_ERROR,
		 _("Default attachment pixmap (balsa/attachment.png) cannot be found:\n"
		   "Your balsa installation is corrupted."));
    }
    g_free ( pix ) ;
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
    gchar *filename, *dir, *p, *sel_file;
    GList *node;

    fs = GTK_FILE_SELECTION(data);
    bsmsg = gtk_object_get_user_data(GTK_OBJECT(fs));

    iconlist = GNOME_ICON_LIST(bsmsg->attachments[1]);
    sel_file = gtk_file_selection_get_filename(fs);
    dir = g_strdup(sel_file);
    p = strrchr(dir, '/');
    if (p)
	*(p + 1) = '\0';

    add_attachment(iconlist, g_strdup(sel_file), FALSE, NULL);
    for (node = GTK_CLIST(fs->file_list)->selection; node;
	 node = g_list_next(node)) {
	gtk_clist_get_text(GTK_CLIST(fs->file_list),
			   GPOINTER_TO_INT(node->data), 0, &p);
	filename = g_strconcat(dir, p, NULL);
	if (strcmp(filename, sel_file) != 0)
	    add_attachment(iconlist, filename, FALSE, NULL);
	else g_free(filename);
    }
    
    bsmsg->update_config = FALSE;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
	bsmsg->view_checkitems[MENU_TOGGLE_ATTACHMENTS_POS]), TRUE);
    bsmsg->update_config = TRUE;

    gtk_widget_destroy(GTK_WIDGET(fs));
    if (balsa_app.attach_dir)
	g_free(balsa_app.attach_dir);

    balsa_app.attach_dir = dir;	/* steal the reference to the string */

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
    /* start workaround for prematurely realized widget returned
     * by some GTK+ versions */
    if(GTK_WIDGET_REALIZED(fsw))
        gtk_widget_unrealize(fsw);
    /* end workaround for prematurely realized widget */
    gtk_window_set_wmclass(GTK_WINDOW(fsw), "file", "Balsa");
    gtk_object_set_user_data(GTK_OBJECT(fsw), bsm);

    fs = GTK_FILE_SELECTION(fsw);
    gtk_clist_set_selection_mode(GTK_CLIST(fs->file_list),
				 GTK_SELECTION_EXTENDED);
    if (balsa_app.attach_dir)
	gtk_file_selection_set_filename(fs, balsa_app.attach_dir);


    gtk_signal_connect(GTK_OBJECT(fs->ok_button), "clicked",
		       (GtkSignalFunc) attach_dialog_ok, fs);
    gtk_signal_connect_object(GTK_OBJECT(fs->cancel_button), "clicked",
			      (GtkSignalFunc)
			      GTK_SIGNAL_FUNC(gtk_widget_destroy),
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
	
    libbalsa_lock_mutt();
    mutt_mktemp(tmp_file_name);
    libbalsa_unlock_mutt();
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
    GtkWidget *index =
	balsa_window_find_current_index(balsa_app.main_window);
    gint pos=gtk_editable_get_position(GTK_EDITABLE(msg->text));
    GString *text = g_string_new("");
    
    if (index) {
	GList *node;
	GList *mailbox;
	GtkCTree *ctree = GTK_CTREE(BALSA_INDEX(index)->ctree);
    
	for (node = GTK_CLIST(ctree)->selection; node;
	     node = g_list_next(node)) {
	    LibBalsaMessage *message =
		gtk_ctree_node_get_row_data(ctree, GTK_CTREE_NODE(node->data));
	    GString *body = quoteBody(msg, message, type);
	    
	    g_string_append(text, body->str);
	    g_string_free(body, TRUE);
	}
    }
    
    gtk_editable_insert_text(GTK_EDITABLE(msg->text), text->str,
			     text->len, &pos);
    g_string_free(text, TRUE);
    
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
	GList *node;
	GtkCTree *ctree = GTK_CTREE(BALSA_INDEX(index)->ctree);
    
	for (node = GTK_CLIST(ctree)->selection; node;
	     node = g_list_next(node)) {
	    LibBalsaMessage *message =
		gtk_ctree_node_get_row_data(ctree, GTK_CTREE_NODE(node->data));

	    if(!attach_message(msg, message)) {
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Attaching message failed.\n"
                                       "Possible reason: not enough temporary space"));
                break;
            }
	}
    }
    
    return TRUE;
}


static gint include_messages_cb(GtkWidget *widget, BalsaSendmsg *msg)
{
    return insert_selected_messages(msg, SEND_FORWARD_INLINE);
}

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
	GList *names, *l;
	
	names = gnome_uri_list_extract_filenames(selection_data->data);
	
	for (l = names; l; l = l->next)
	    add_attachment(GNOME_ICON_LIST(bsmsg->attachments[1]),
			   g_strdup((char *) l->data), FALSE, NULL);
	
	gnome_uri_list_free_strings(names);
	
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

    if (!*gtk_entry_get_text(GTK_ENTRY(widget))) {
	gtk_entry_set_text(GTK_ENTRY(widget), selection_data->data);
    } else {
	gtk_entry_append_text(GTK_ENTRY(widget), ",");
	gtk_entry_append_text(GTK_ENTRY(widget), selection_data->data);
    }
}


/*
 * static void create_address_entry()
 * 
 * Creates a gtk_label()/libbalsa_address_entry() pair.
 *
 * Input: GtkWidget* table       - Table to attach to.
 *        const gchar* label     - Label string.
 *        int y_pos              - position in the table.
 *      
 * Output: GtkWidget* arr[] - arr[0] will be the label widget.
 *                          - arr[1] will be the entry widget.
 */
static void
create_address_entry(GtkWidget * table, const gchar * label, int y_pos,
		    GtkWidget * arr[])
{
    arr[0] = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(arr[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(arr[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), arr[0], 0, 1, y_pos, y_pos + 1,
		     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

    arr[1] = libbalsa_address_entry_new();
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
    arr[0] = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(arr[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(arr[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), arr[0], 0, 1, y_pos, y_pos + 1,
		     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

    arr[1] = gtk_entry_new_with_max_length(2048);
    gtk_table_attach(GTK_TABLE(table), arr[1], 1, 2, y_pos, y_pos + 1,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 0, 0);
}

/*
 * static void create_email_entry()
 *
 * Creates a gtk_label()/gtk_entry() and button in a table for
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
    create_address_entry(table, label, y_pos, arr);

    arr[2] = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(arr[2]), GTK_RELIEF_NONE);
    GTK_WIDGET_UNSET_FLAGS(arr[2], GTK_CAN_FOCUS);
    gtk_container_add(GTK_CONTAINER(arr[2]),
		      gnome_stock_pixmap_widget(NULL, icon));
    gtk_table_attach(GTK_TABLE(table), arr[2], 2, 3, y_pos, y_pos + 1,
		     0, 0, 0, 0);

    gtk_signal_connect(GTK_OBJECT(arr[2]), "clicked",
		       GTK_SIGNAL_FUNC(address_book_cb),
		       smw);
    gtk_object_set_data(GTK_OBJECT(arr[2]), "address-entry-widget", 
			arr[1]);
    gtk_signal_connect(GTK_OBJECT(arr[1]), "drag_data_received",
		       GTK_SIGNAL_FUNC(to_add), NULL);
    gtk_drag_dest_set(GTK_WIDGET(arr[1]), GTK_DEST_DEFAULT_ALL,
		      email_field_drop_types,
		      ELEMENTS(email_field_drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    libbalsa_address_entry_set_find_match(LIBBALSA_ADDRESS_ENTRY(arr[1]),
		       expand_alias_find_match);
    libbalsa_address_entry_set_domain(LIBBALSA_ADDRESS_ENTRY(arr[1]),
		       balsa_app.current_ident->domain);
    gtk_signal_connect(GTK_OBJECT(arr[1]), "changed",
                       GTK_SIGNAL_FUNC(address_changed_cb), sma);

    if (!smw->bad_address_style) {
        /* set up the style for flagging bad/incomplete addresses */
        smw->bad_address_style =
            gtk_style_copy(gtk_widget_get_style(GTK_WIDGET(arr[0])));
        smw->bad_address_style->fg[GTK_STATE_NORMAL] =
            balsa_app.bad_address_color;
    }

    /* populate the info structure: */
    sma->msg = smw;
    sma->label = arr[0];
    sma->min_addresses = min_addresses;
    sma->max_addresses = max_addresses;
    sma->ready = TRUE;

    /* set initial label style: */
    set_ready(GTK_EDITABLE(arr[1]), sma);
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
    GtkWidget *nb;
    GList     *glist = NULL;


    table = gtk_table_new(11, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);

    /* msg->bad_address_style will be set in create_email_entry: */
    msg->bad_address_style = NULL;

    /* From: */
    create_email_entry(table, _("From:"), 0, GNOME_STOCK_MENU_BOOK_BLUE,
                       msg, msg->from,
                       &msg->from_info, 1, 1);

    /* To: */
    create_email_entry(table, _("To:"), 1, GNOME_STOCK_MENU_BOOK_RED,
                       msg, msg->to,
                       &msg->to_info, 1, -1);

    /* Subject: */
    create_string_entry(table, _("Subject:"), 2, msg->subject);
    /* cc: */
    create_email_entry(table, _("Cc:"), 3, GNOME_STOCK_MENU_BOOK_YELLOW,
                       msg, msg->cc,
                       &msg->cc_info, 0, -1);

    /* bcc: */
    create_email_entry(table, _("Bcc:"), 4, GNOME_STOCK_MENU_BOOK_GREEN,
                       msg, msg->bcc,
                       &msg->bcc_info, 0, -1);

    /* fcc: */
    msg->fcc[0] = gtk_label_new(_("Fcc:"));
    gtk_misc_set_alignment(GTK_MISC(msg->fcc[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(msg->fcc[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), msg->fcc[0], 0, 1, 5, 6, GTK_FILL,
		     GTK_FILL | GTK_SHRINK, 0, 0);

    msg->fcc[1] = gtk_combo_new();
    gtk_combo_set_use_arrows(GTK_COMBO(msg->fcc[1]), 0);
    gtk_combo_set_use_arrows_always(GTK_COMBO(msg->fcc[1]), 0);

    glist = g_list_append(glist, balsa_app.sentbox->name);
    glist = g_list_append(glist, balsa_app.draftbox->name);
    glist = g_list_append(glist, balsa_app.outbox->name);
    glist = g_list_append(glist, balsa_app.trash->name);
    if (balsa_app.copy_to_sentbox)
	glist = g_list_append(glist, "");
    else
	glist = g_list_prepend(glist, "");

    /* FIXME: glist = g_list_concat(glist, mblist_get_mailbox_name_list()) */

    gtk_combo_set_popdown_strings(GTK_COMBO(msg->fcc[1]), glist);
    gtk_table_attach(GTK_TABLE(table), msg->fcc[1], 1, 3, 5, 6,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 0, 0);

    /* Reply To: */
    create_email_entry(table, _("Reply To:"), 6, GNOME_STOCK_MENU_BOOK_BLUE,
                       msg, msg->reply_to,
                       &msg->reply_to_info, 0, -1);



    /* Attachment list */
    msg->attachments[0] = gtk_label_new(_("Attachments:"));
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
    gtk_signal_connect(GTK_OBJECT(msg->window), "drag_data_received",
		       GTK_SIGNAL_FUNC(attachments_add), msg);
    gtk_drag_dest_set(GTK_WIDGET(msg->window), GTK_DEST_DEFAULT_ALL,
		      drop_types, ELEMENTS(drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    gtk_widget_set_usize(msg->attachments[1], -1, 100);

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), msg->attachments[1]);
    gtk_container_add(GTK_CONTAINER(frame), sw);

    gtk_table_attach(GTK_TABLE(table), frame, 1, 3, 7, 8,
		     GTK_FILL | GTK_EXPAND,
		     GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 0);

    gtk_signal_connect(GTK_OBJECT(msg->attachments[1]), "select_icon",
		       GTK_SIGNAL_FUNC(select_attachment), NULL);

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
    gtk_signal_connect(GTK_OBJECT(sc), "done-spell-check",
		       GTK_SIGNAL_FUNC(spell_check_done_cb), msg);
    msg->spell_checker = sc;

    nb = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(nb), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);

    /* add the spell check widget to the notebook last */
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), GTK_WIDGET(sc),
			     gtk_label_new("Spell Check"));
    spell_check_page = gtk_notebook_page_num(GTK_NOTEBOOK(nb), sc);

    /* add the mail headers table to the notebook first */
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), GTK_WIDGET(table),
			     gtk_label_new("Mail Headers"));
    mail_headers_page = gtk_notebook_page_num(GTK_NOTEBOOK(nb), table);

    gtk_notebook_set_page(GTK_NOTEBOOK(nb), mail_headers_page);
    msg->notebook = nb;

    gtk_widget_show_all(table);
    gtk_widget_show_all(nb);
    gtk_widget_hide(sc);
    return nb;
}

/*
 * catch user `return' chars, and edit out any previous spaces
 * to make them `hard returns',
 * */
static void
insert_text_cb(GtkEditable * msg_text, gchar * new_text,
               gint new_text_length, gint * position, gpointer user_data)
{
    if (new_text_length == 1 && *new_text == '\n') {
        gint j = *position;
        gchar *text = gtk_editable_get_chars(msg_text, 0, j);
        gint i = j;
        while (--i >= 0 && text[i] == ' ');
        if (++i < j) {
            gtk_editable_delete_text(msg_text, i, j);
            *position = i;
        }
        g_free(text);
    }
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
    if (info == TARGET_MESSAGES) {
        LibBalsaMessage **message_array =
            (LibBalsaMessage **) selection_data->data;
        GString *text = g_string_new(NULL);
        gint pos = gtk_editable_get_position(GTK_EDITABLE(widget));
        gint orig_pos = pos;

        while (*message_array) {
            GString *body = quoteBody(bsmsg, *message_array++, SEND_REPLY);

            g_string_append(text, body->str);
            g_string_free(body, TRUE);
        }
        gtk_editable_insert_text(GTK_EDITABLE(widget), text->str,
                                 text->len, &pos);
        gtk_editable_set_position(GTK_EDITABLE(widget), orig_pos);
        g_string_free(text, TRUE);
    }
}

/* create_text_area 
   Creates the text entry part of the compose window.
*/
static GtkWidget *
create_text_area(BalsaSendmsg * msg)
{
    GtkWidget *table;

    msg->text = gtk_text_new(NULL, NULL);
    if (msg->flow)
        gtk_signal_connect(GTK_OBJECT(msg->text), "insert-text",
                           insert_text_cb, NULL);
    gtk_text_set_editable(GTK_TEXT(msg->text), TRUE);
    gtk_text_set_word_wrap(GTK_TEXT(msg->text), TRUE);
    balsa_spell_check_set_text(BALSA_SPELL_CHECK(msg->spell_checker),
			       GTK_TEXT(msg->text));

    table = gtk_scrolled_window_new(GTK_TEXT(msg->text)->hadj,
				    GTK_TEXT(msg->text)->vadj);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(table),
    				   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(table), msg->text);
    gtk_signal_connect(GTK_OBJECT(msg->text), "drag_data_received",
		       GTK_SIGNAL_FUNC(drag_data_quote), msg);
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

    libbalsa_message_body_ref(message);
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

            if (msg->flow && libbalsa_flowed_rfc2646(body))
                llen = balsa_app.wraplength;
	    if (!strcmp(body_type, "text/plain") &&
		(rbdy = process_mime_part(message, body, NULL, llen, FALSE,
                                          msg->flow))) {
		gtk_text_insert(GTK_TEXT(msg->text), NULL, NULL, NULL, 
				rbdy->str, rbdy->len);
		g_string_free(rbdy, TRUE);
	    }
	    g_free(body_type);
	    body = body->next;
	}
	while (body) {
	    gchar *name, *body_type, tmp_file_name[PATH_MAX + 1];

	    libbalsa_lock_mutt();
	    mutt_mktemp(tmp_file_name);
	    libbalsa_unlock_mutt();
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

    libbalsa_message_body_ref(message);

    personStr = libbalsa_address_get_name(message->from);
    if (!personStr)
	personStr = _("you");
    if (message->date)
	date = libbalsa_message_date_to_gchar(message, balsa_app.date_string);

    if (type == SEND_FORWARD_ATTACH) {
	const gchar *subject;

	str = g_strdup_printf(_("------forwarded message------\n"), 
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

	    str = g_strdup_printf(_("References: %s"), ref_list->data);
	    g_string_append(body, str);
	    g_free(str);
	    ref_list = ref_list->next;

	    while (ref_list) {
		str = g_strdup_printf(" %s", ref_list->data);
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
    gint pos = 0;
    gboolean reply_any = (type == SEND_REPLY || type == SEND_REPLY_ALL
                          || type == SEND_REPLY_GROUP);
    gboolean forwd_any = (type == SEND_FORWARD_ATTACH
                          || type == SEND_FORWARD_INLINE);

    if (message && ((balsa_app.autoquote && reply_any)
                    || type == SEND_FORWARD_INLINE))
        body = quoteBody(msg, message, type);
    else
	body = g_string_new("");

    if ((signature = read_signature(msg)) != NULL) {
	if ((reply_any && balsa_app.current_ident->sig_whenreply)
       || (forwd_any && balsa_app.current_ident->sig_whenforward)
       || (type == SEND_NORMAL && balsa_app.current_ident->sig_sending)) {

	    if (balsa_app.current_ident->sig_separator
		&& g_strncasecmp(signature, "--\n", 3)
		&& g_strncasecmp(signature, "-- \n", 4)) {
		gchar * tmp = g_strconcat("-- \n", signature, NULL);
		g_free(signature);
		signature = tmp;
	    }

	    if (balsa_app.current_ident->sig_prepend && type != SEND_NORMAL) {
	    	g_string_prepend(body, "\n\n");
	    	g_string_prepend(body, signature);
	    } else {
	    	g_string_append(body, signature);
	    }
	    g_string_prepend_c(body, '\n');
	}
	g_free(signature);
    }

    gtk_editable_insert_text(GTK_EDITABLE(msg->text), body->str, body->len,
			     &pos);
    gtk_editable_set_position(GTK_EDITABLE(msg->text), 0);
    g_string_free(body, TRUE);
}

static gint insert_signature_cb(GtkWidget *widget, BalsaSendmsg *msg)
{
    gchar *signature;
    gint pos=gtk_editable_get_position(GTK_EDITABLE(msg->text));
    
    if ((signature = read_signature(msg)) != NULL) {
	if (balsa_app.current_ident->sig_separator
	    && g_strncasecmp(signature, "--\n", 3)
	    && g_strncasecmp(signature, "-- \n", 4)) {
	    gchar * tmp = g_strconcat("-- \n", signature, NULL);
	    g_free(signature);
	    signature = tmp;
	}
	
	gtk_editable_insert_text(GTK_EDITABLE(msg->text), signature, 
				 strlen(signature), &pos);
	
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
	if (g_strncasecmp(tmp, "re:", 3) == 0 || g_strncasecmp(tmp, "aw:", 3) == 0) {
	    tmp += 3;
	} else if (g_strncasecmp(tmp, _("Re:"), strlen(_("Re:"))) == 0) {
	    tmp += strlen(_("Re:"));
	} else {
	    i = strlen(ident->reply_string);
	    if (g_strncasecmp(tmp, ident->reply_string, i)
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
	    if (g_strncasecmp(tmp, "fwd:", 4) == 0) {
		tmp += 4;
	    } else if (g_strncasecmp(tmp, _("Fwd:"), strlen(_("Fwd:"))) == 0) {
		tmp += strlen(_("Fwd:"));
	    } else {
		i = strlen(ident->forward_string);
		if (g_strncasecmp(tmp, ident->forward_string, i) == 0) {
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
set_entry_from_address_list(GtkEntry* field, GList* list)
{
    if (list) {
	gchar* tmp = libbalsa_make_string_from_list(list);
	gtk_entry_set_text(field, tmp);
	g_free(tmp);
    }
}

static void
setup_headers_from_message(BalsaSendmsg* cw, LibBalsaMessage *message)
{
    set_entry_from_address_list(GTK_ENTRY(cw->to[1]),  message->to_list);
    set_entry_from_address_list(GTK_ENTRY(cw->cc[1]),  message->cc_list);
    set_entry_from_address_list(GTK_ENTRY(cw->bcc[1]), message->bcc_list);
}


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
            if (!g_strcasecmp(address_string,
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

BalsaSendmsg *
sendmsg_window_new(GtkWidget * widget, LibBalsaMessage * message,
		   SendType type)
{
    static const struct callback_item {
        const char* icon_id;
        GtkSignalFunc callback;
    } callback_table[] = {
        { BALSA_PIXMAP_ATTACHMENT,       attach_clicked },
        { BALSA_PIXMAP_IDENTITY,         change_identity_dialog_cb },
        { BALSA_PIXMAP_POSTPONE,         postpone_message_cb },
        { BALSA_PIXMAP_PRINT,            print_message_cb },
        { BALSA_PIXMAP_SAVE,             save_message_cb },
        { BALSA_PIXMAP_SEND,             send_message_toolbar_cb },
        { GNOME_STOCK_PIXMAP_CLOSE,      close_window_cb },
        { GNOME_STOCK_PIXMAP_SPELLCHECK, spell_check_cb } };
    GtkWidget *window;
    GtkWidget *paned = gtk_vpaned_new();
    BalsaSendmsg *msg = NULL;
    GList *list;
    unsigned i;
    gchar* tmp;

    msg = g_malloc(sizeof(BalsaSendmsg));
    msg->font     = NULL;
    msg->charset  = NULL;
    msg->locale   = NULL;
    msg->ident = balsa_app.current_ident;
    msg->update_config = FALSE;
    msg->modified = FALSE; 
    msg->flow = balsa_app.wordwrap && balsa_app.send_rfc2646_format_flowed;
    msg->quit_on_close = FALSE;

    switch (type) {
    case SEND_REPLY:
    case SEND_REPLY_ALL:
    case SEND_REPLY_GROUP:
	window = gnome_app_new("balsa", _("Reply to "));
	msg->orig_message = message;
	break;

    case SEND_FORWARD_ATTACH:
    case SEND_FORWARD_INLINE:
	window = gnome_app_new("balsa", _("Forward message"));
	msg->orig_message = message;
	break;

    case SEND_CONTINUE:
	window = gnome_app_new("balsa", _("Continue message"));
	msg->orig_message = message;
	break;

    default:
	window = gnome_app_new("balsa", _("New message"));
	msg->orig_message = NULL;
	break;
    }
    if (message) { /* ref message so we don't loose it even if it is deleted */
	gtk_object_ref(GTK_OBJECT(message));
	/* reference the original mailbox so we don't loose the
	   mail even if the mailbox is closed. Alternatively,
	   one could try using weak references or destroy notification
	   to take care of it. In such a case, the orig_message field
	   would be cleared
	*/
	if (message->mailbox)
	    libbalsa_mailbox_open(message->mailbox);
    }
    msg->window = window;
    msg->type = type;

    gtk_signal_connect(GTK_OBJECT(msg->window), "delete-event",
		       GTK_SIGNAL_FUNC(delete_event_cb), msg);
    gtk_signal_connect(GTK_OBJECT(msg->window), "destroy",
		       GTK_SIGNAL_FUNC(destroy_event_cb), msg);

	 gtk_signal_connect(GTK_OBJECT(msg->window), "size_allocate",
		       GTK_SIGNAL_FUNC(sw_size_alloc_cb), msg);

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

    for(i=0; i < ELEMENTS(callback_table); i++)
        set_toolbar_button_callback(TOOLBAR_COMPOSE, callback_table[i].icon_id,
                                    callback_table[i].callback, msg);

    gnome_app_set_toolbar(GNOME_APP(window),
			  get_toolbar(GTK_WIDGET(window), TOOLBAR_COMPOSE));

    msg->ready_widgets[0] = file_menu[MENU_FILE_SEND_POS].widget;
    msg->ready_widgets[1] = file_menu[MENU_FILE_QUEUE_POS].widget;
    msg->ready_widgets[2] = file_menu[MENU_FILE_POSTPONE_POS].widget;
    msg->ready_widgets[3] = get_tool_widget(window, TOOLBAR_COMPOSE,
                                            BALSA_PIXMAP_SEND);
    msg->ready_widgets[4] = get_tool_widget(window, TOOLBAR_COMPOSE,
                                            BALSA_PIXMAP_POSTPONE);
    msg->current_language_menu = lang_menu[LANG_CURRENT_POS].widget;

    /* set options - just the Disposition Notification request for now */
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (opts_menu[OPTS_MENU_DISPNOTIFY_POS].widget), balsa_app.req_dispnotify);

    /* create spell checking disable widget list */
    list = NULL;

    for (i = 0; i < MAIN_MENUS_COUNT; ++i) {
	if (i != MAIN_FILE_MENU)
	    list = g_list_append(list, (gpointer) main_menu[i].widget);
    }
    for (i = 0; i < MENU_FILE_CLOSE_POS; ++i) {
	if (i != MENU_FILE_SEPARATOR1_POS && i != MENU_FILE_SEPARATOR2_POS)
	    list = g_list_append(list, (gpointer) file_menu[i].widget);
    }

    for(i=0; i<ELEMENTS(main_toolbar_spell_disable); i++)
	list = g_list_prepend(
	    list, get_tool_widget(window, 1, main_toolbar_spell_disable[i]));
    msg->spell_check_disable_list = list;

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

    /* Get the identity from the To: field of the original message */
    guess_identity(msg);

    /* From: */
    setup_headers_from_identity(msg, msg->ident);

    /* Fcc: */
    if (type == SEND_CONTINUE && message->fcc_mailbox != NULL)
	gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(msg->fcc[1])->entry),
			   message->fcc_mailbox);

    /* Subject: */
    set_entry_to_subject(GTK_ENTRY(msg->subject[1]), message, type, msg->ident);

    if (type == SEND_CONTINUE)
	setup_headers_from_message(msg, message);

    if (type == SEND_REPLY_ALL) {
	tmp = libbalsa_make_string_from_list(message->to_list);

	gtk_entry_set_text(GTK_ENTRY(msg->cc[1]), tmp);
	g_free(tmp);

	if (message->cc_list) {
	    gtk_entry_append_text(GTK_ENTRY(msg->cc[1]), ", ");

	    tmp = libbalsa_make_string_from_list(message->cc_list);
	    gtk_entry_append_text(GTK_ENTRY(msg->cc[1]), tmp);
	    g_free(tmp);
	}
    }
    gtk_paned_set_position(GTK_PANED(paned), -1);
    gnome_app_set_contents(GNOME_APP(window), paned);

    if (type == SEND_CONTINUE)
	continueBody(msg, message);
    else
	fillBody(msg, message, type);

    /* set the toolbar so we are consistant with the rest of balsa */
    {
	GnomeDockItem *item;
	GtkWidget *toolbar;

	item = gnome_app_get_dock_item_by_name(GNOME_APP(window),
					       GNOME_APP_TOOLBAR_NAME);
	toolbar = gnome_dock_item_get_child(item);

	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar),
			      balsa_app.toolbar_style);
    }

    /* set the menus  - and charset index - and display the window */
    /* FIXME: this will also reset the font, copying the text back and 
       forth which is sub-optimal.
     */
    init_menus(msg);
    gtk_notebook_set_page(GTK_NOTEBOOK(msg->notebook), mail_headers_page);

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

    if (type == SEND_CONTINUE && 
	GNOME_ICON_LIST(msg->attachments[1])->icons)
 	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
	    msg->view_checkitems[MENU_TOGGLE_ATTACHMENTS_POS]), TRUE);

    msg->update_config = TRUE;
 
    msg->delete_sig_id = 
	gtk_signal_connect(GTK_OBJECT(balsa_app.main_window), "delete-event",
			   (GtkSignalFunc)delete_event_cb, msg);
    gtk_signal_connect(GTK_OBJECT(msg->text), "changed",
		       (GtkSignalFunc)text_changed, msg);
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
sendmsg_window_set_field(BalsaSendmsg *bsmsg, const gchar* key,
			      const gchar* val)
{
    GtkWidget* entry;
    g_return_if_fail(bsmsg);
 
    if     (g_strcasecmp(key, "to")     ==0) entry = bsmsg->to[1];
    else if(g_strcasecmp(key, "subject")==0) entry = bsmsg->subject[1];
    else if(g_strcasecmp(key, "cc")     ==0) entry = bsmsg->cc[1];
    else if(g_strcasecmp(key, "bcc")    ==0) entry = bsmsg->bcc[1];
    else if(g_strcasecmp(key, "replyto")==0) entry = bsmsg->reply_to[1];
    else return;

    gtk_entry_set_text(GTK_ENTRY(entry), val);
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
static void
do_insert_file(GtkWidget * selector, GtkFileSelection * fs)
{
    gchar *fname;
    guint cnt;
    gchar buf[4096];
    FILE *fl;
    BalsaSendmsg *bsmsg;

    bsmsg = (BalsaSendmsg *) gtk_object_get_user_data(GTK_OBJECT(fs));
    fname = gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs));

    cnt = gtk_editable_get_position(GTK_EDITABLE(bsmsg->text));

    if (!(fl = fopen(fname, "rt"))) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Could not open the file %s.\n"), fname);
    } else {
	gnome_appbar_push(balsa_app.appbar, _("Loading..."));

	gtk_text_freeze(GTK_TEXT(bsmsg->text));
	gtk_text_set_point(GTK_TEXT(bsmsg->text), cnt);
	while ((cnt = fread(buf, 1, sizeof(buf), fl)) > 0) {
	    if (balsa_app.debug)
		printf("%s cnt: %d (max: %d)\n", fname, cnt, sizeof(buf));
	    gtk_text_insert(GTK_TEXT(bsmsg->text), bsmsg->font,
			    NULL, NULL, buf, cnt);
	}
	if (balsa_app.debug)
	    printf("%s cnt: %d (max: %d)\n", fname, cnt, sizeof(buf));

	gtk_text_thaw(GTK_TEXT(bsmsg->text));
	fclose(fl);
	gnome_appbar_pop(balsa_app.appbar);
    }
    /* g_free(fname); */
    gtk_widget_destroy(GTK_WIDGET(fs));

}

static gint
include_file_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    GtkWidget *file_selector;

    file_selector = gtk_file_selection_new(_("Include file"));
    /* start workaround for prematurely realized widget returned
     * by some GTK+ versions */
    if(GTK_WIDGET_REALIZED(file_selector))
        gtk_widget_unrealize(file_selector);
    /* end workaround for prematurely realized widget */
    gtk_window_set_wmclass(GTK_WINDOW(file_selector), "file", "Balsa");
    gtk_object_set_user_data(GTK_OBJECT(file_selector), bsmsg);

    gtk_file_selection_hide_fileop_buttons(GTK_FILE_SELECTION
					   (file_selector));

    gtk_signal_connect(GTK_OBJECT
		       (GTK_FILE_SELECTION(file_selector)->ok_button),
		       "clicked", GTK_SIGNAL_FUNC(do_insert_file),
		       file_selector);

    /* Ensure that the dialog box is destroyed when the user clicks a button. */

    gtk_signal_connect_object(GTK_OBJECT
			      (GTK_FILE_SELECTION
			       (file_selector)->cancel_button), "clicked",
			      GTK_SIGNAL_FUNC(gtk_widget_destroy),
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
address_changed_cb(GtkEditable * w, BalsaSendmsgAddress *sma)
{
    set_ready(w, sma);
    check_readiness(sma->msg);
}

static void
set_ready(GtkEditable * w, BalsaSendmsgAddress *sma)
{
    gint len = 0;
    gchar *tmp = gtk_editable_get_chars(w, 0, -1);

    if (tmp && *tmp) {
        GList *list = libbalsa_address_new_list_from_string(tmp);

        if (list) {
            len = g_list_length(list);

            g_list_foreach(list, (GFunc) gtk_object_unref, NULL);
            g_list_free(list);
        } else {
            /* error */
            len = -1;
        }
        
    }
    g_free(tmp);

    if (len < sma->min_addresses
        || (sma->max_addresses >= 0 && len > sma->max_addresses)) {
        if (sma->ready) {
            sma->ready = FALSE;
            gtk_widget_set_style(sma->label, sma->msg->bad_address_style);
        }
    } else {
        if (!sma->ready) {
            sma->ready = TRUE;
            gtk_widget_set_rc_style(sma->label);
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
    gchar recvtime[50];
    struct tm *footime;

    g_assert(bsmsg != NULL);
    message = libbalsa_message_new();

    message->from = 
        libbalsa_address_new_from_string(gtk_entry_get_text
                                         (GTK_ENTRY(bsmsg->from[1])));

    tmp = g_strdup(gtk_entry_get_text(GTK_ENTRY(bsmsg->subject[1])));
    strip_chars(tmp, "\r\n");
    LIBBALSA_MESSAGE_SET_SUBJECT(message, tmp);

    message->to_list =
	libbalsa_address_new_list_from_string(gtk_entry_get_text
					      (GTK_ENTRY(bsmsg->to[1])));
    message->cc_list =
	libbalsa_address_new_list_from_string(gtk_entry_get_text
					      (GTK_ENTRY(bsmsg->cc[1])));
    message->bcc_list =
	libbalsa_address_new_list_from_string(gtk_entry_get_text
					      (GTK_ENTRY(bsmsg->bcc[1])));

    if ((tmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->reply_to[1]))) != NULL
	&& *tmp)
	message->reply_to = libbalsa_address_new_from_string(tmp);

    if (balsa_app.req_dispnotify)
	libbalsa_message_set_dispnotify(message, balsa_app.current_ident->address);

    if (bsmsg->orig_message != NULL &&
	!GTK_OBJECT_DESTROYED(bsmsg->orig_message)) {

	if (bsmsg->orig_message->references != NULL) {
	    for (list = bsmsg->orig_message->references; list;
		 list = list->next) {
		message->references =
		    g_list_append(message->references,
				  g_strdup(list->data));
	    }
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
    body->buffer = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->text), 0,
					  gtk_text_get_length(GTK_TEXT
							      (bsmsg->text)));
    if (bsmsg->flow) {
        body->buffer =
            libbalsa_wrap_rfc2646(body->buffer, balsa_app.wraplength, TRUE,
                                  FALSE);
    } else if (balsa_app.wordwrap)
        libbalsa_wrap_string(body->buffer, balsa_app.wraplength);
    body->charset = g_strdup(bsmsg->charset);
    libbalsa_message_append_part(message, body);

    {				/* handle attachments */
	gint i;
	for (i = 0; i < GNOME_ICON_LIST(bsmsg->attachments[1])->icons; i++) {
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
	    libbalsa_message_append_part(message, body);
	}
    }

    tmp = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(bsmsg->fcc[1])->entry));
    message->fcc_mailbox = tmp ? g_strdup(tmp) : NULL;
    message->date = time(NULL);

    return message;
}

/* "send message" menu and toolbar callback */
static gint
send_message_handler(BalsaSendmsg * bsmsg, gboolean queue_only)
{
    gboolean successful = TRUE;
    LibBalsaMessage *message;
    LibBalsaMailbox *fcc = NULL;
    const char* old_charset;

    if (!is_ready_to_send(bsmsg))
	return FALSE;

    old_charset = libbalsa_set_charset(bsmsg->charset);

    if (balsa_app.debug)
	fprintf(stderr, "sending with charset: %s\n", bsmsg->charset);

    message = bsmsg2message(bsmsg);
    fcc = message->fcc_mailbox && *(message->fcc_mailbox)
	? mblist_find_mbox_by_name(balsa_app.mblist, message->fcc_mailbox)
	: NULL;

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
    libbalsa_set_charset(old_charset);
    if (successful) {
	if (bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL ||
	    bsmsg->type == SEND_REPLY_GROUP) {
	    if (bsmsg->orig_message)
		libbalsa_message_reply(bsmsg->orig_message);
	} else if (bsmsg->type == SEND_CONTINUE) {
	    if (bsmsg->orig_message) {
		libbalsa_message_delete(bsmsg->orig_message, TRUE);
		balsa_index_sync_backend(bsmsg->orig_message->mailbox);
	    }
	}
    }

    gtk_object_destroy(GTK_OBJECT(message));
    gtk_widget_destroy(bsmsg->window);

    return TRUE;
}

/* "send message" toolbar callback */
static void
send_message_toolbar_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    libbalsa_address_entry_clear_to_send(bsmsg->to[1]);
    libbalsa_address_entry_clear_to_send(bsmsg->cc[1]);
    libbalsa_address_entry_clear_to_send(bsmsg->bcc[1]);
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
    libbalsa_address_entry_clear_to_send(bsmsg->to[1]);
    libbalsa_address_entry_clear_to_send(bsmsg->cc[1]);
    libbalsa_address_entry_clear_to_send(bsmsg->bcc[1]);
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
				  message->fcc_mailbox,
                                             balsa_app.encoding_style,
                                             bsmsg->flow);
    else
	successp = libbalsa_message_postpone(message, balsa_app.draftbox, 
                                  NULL,
				  message->fcc_mailbox,
                                             balsa_app.encoding_style,
                                             bsmsg->flow);
    if(successp) {
	if (bsmsg->type == SEND_CONTINUE && bsmsg->orig_message) {
	    libbalsa_message_delete(bsmsg->orig_message, TRUE);
	    balsa_index_sync_backend(bsmsg->orig_message->mailbox);
	}
    }
    gtk_object_unref(GTK_OBJECT(message));
    return successp;
}

/* "postpone message" menu and toolbar callback */
static void
postpone_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    if (is_ready_to_send(bsmsg)) {
        gboolean thereturn = message_postpone(bsmsg);
        gtk_widget_destroy(bsmsg->window);
    }
}


static void
save_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gboolean thereturn;
    LibBalsaMessage *message;
    
    if (!is_ready_to_send(bsmsg)) 
        return;

    message = bsmsg2message(bsmsg);
    gtk_object_ref(GTK_OBJECT(message));

    thereturn = message_postpone(bsmsg);

    if(thereturn) {
	GList *draft_entry;

	libbalsa_mailbox_open(balsa_app.draftbox);
	draft_entry=g_list_last(balsa_app.draftbox->message_list);

	if(bsmsg->orig_message) {
	    if(bsmsg->orig_message->mailbox)
		libbalsa_mailbox_close(bsmsg->orig_message->mailbox);
	    gtk_object_unref(GTK_OBJECT(bsmsg->orig_message));
	}
	bsmsg->type=SEND_CONTINUE;
	bsmsg->orig_message=LIBBALSA_MESSAGE(draft_entry->data);
	bsmsg->orig_message->mailbox=balsa_app.draftbox;
	gtk_object_ref(GTK_OBJECT(bsmsg->orig_message));
    
    }
}

static void
print_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
#ifndef HAVE_GNOME_PRINT
    balsa_information(
	LIBBALSA_INFORMATION_ERROR,
	_("Balsa has been compiled without gnome-print support.\n"
	  "Printing is not possible."));
#else
    LibBalsaMessage *msg = bsmsg2message(bsmsg);
    message_print(msg);
    gtk_object_destroy(GTK_OBJECT(msg));
#endif
    return;
}

static void
cut_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gtk_editable_cut_clipboard(GTK_EDITABLE(bsmsg->text));
}

static void
copy_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gtk_editable_copy_clipboard(GTK_EDITABLE(bsmsg->text));
}
static void
paste_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gtk_editable_paste_clipboard(GTK_EDITABLE(bsmsg->text));
}

static void
select_all_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gtk_editable_select_region(GTK_EDITABLE(bsmsg->text), 0, -1);
}

static void
wrap_body_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gint pos, dummy;
    gchar *the_text;

    pos = gtk_editable_get_position(GTK_EDITABLE(bsmsg->text));

    the_text = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->text), 0, -1);
    if (bsmsg->flow) {
        the_text =
            libbalsa_wrap_rfc2646(the_text, balsa_app.wraplength, TRUE,
                                  TRUE);
    } else
        libbalsa_wrap_string(the_text, balsa_app.wraplength);

    gtk_text_freeze(GTK_TEXT(bsmsg->text));
    gtk_editable_delete_text(GTK_EDITABLE(bsmsg->text), 0, -1);
    dummy = 0;
    gtk_editable_insert_text(GTK_EDITABLE(bsmsg->text), the_text,
			     strlen(the_text), &dummy);
    gtk_editable_set_position(GTK_EDITABLE(bsmsg->text),
                              MIN(pos, dummy));
    gtk_text_thaw(GTK_TEXT(bsmsg->text));
    g_free(the_text);
}

static void
do_reflow(GtkText * txt, gint mode)
{
    gint pos, dummy;
    gchar *the_text;

    pos = gtk_editable_get_position(GTK_EDITABLE(txt));
    the_text = gtk_editable_get_chars(GTK_EDITABLE(txt), 0, -1);
    reflow_string(the_text, mode, &pos, balsa_app.wraplength);

    gtk_text_freeze(txt);
    gtk_editable_delete_text(GTK_EDITABLE(txt), 0, -1);
    dummy = 0;
    gtk_editable_insert_text(GTK_EDITABLE(txt), the_text,
			     strlen(the_text), &dummy);
    gtk_text_thaw(txt);
    gtk_editable_set_position(GTK_EDITABLE(txt), pos);
    g_free(the_text);
}

static void
reflow_par_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    do_reflow(GTK_TEXT(bsmsg->text),
	      gtk_editable_get_position(GTK_EDITABLE(bsmsg->text)));
}

static void
reflow_body_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    do_reflow(GTK_TEXT(bsmsg->text), -1);
}

/* To field "changed" signal callback. */
static void
check_readiness(BalsaSendmsg * msg)
{
    unsigned i;
    gboolean state = is_ready_to_send(msg);

    for (i = 0; i < ELEMENTS(msg->ready_widgets); i++)
        if(msg->ready_widgets[i])
            gtk_widget_set_sensitive(msg->ready_widgets[i], state);
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
	parent = GTK_WIDGET(GTK_WIDGET(entry[0])->parent)->parent->parent;
	if (parent)
	    gtk_paned_set_position(GTK_PANED(parent), -1);
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
	    GTK_SIGNAL_FUNC(view_menu[i].moreinfo) (view_menu[i].widget, msg);
	}
    }

    /* set the charset... */
    i = find_locale_index_by_locale(setlocale(LC_CTYPE, NULL));
    if (msg->charset
	&& g_strcasecmp(locales[i].charset, msg->charset) != 0) {
	for(i=0; 
	    i<ELEMENTS(locales) && 
		g_strcasecmp(locales[i].charset, msg->charset) != 0;
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
    GtkStyle *style;
    gchar *tmp;

    if (msg->font)
	gdk_font_unref(msg->font);
    msg->charset = locales[idx].charset;
    msg->locale = locales[idx].locale;
    tmp = g_strdup_printf("%s (%s, %s)", _(locales[idx].lang_name),
			  locales[idx].locale, locales[idx].charset);
    gtk_label_set_text(GTK_LABEL
		       (GTK_BIN(msg->current_language_menu)->child), tmp);
    g_free(tmp);
    
    msg->font = balsa_get_font_by_charset(balsa_app.message_font,msg->charset);

    if (msg->font) {
	gdk_font_ref(msg->font);
	/* Set the new message style */
	style = gtk_style_copy(gtk_widget_get_style(msg->text));
	style->font = msg->font;
	gtk_widget_set_style (msg->text, style);
	gtk_widget_set_style (msg->to[1], style);
        gtk_widget_set_style (msg->from[1], style);
	gtk_widget_set_style (msg->subject[1], style);
        gtk_widget_set_style (msg->cc[1], style);
        gtk_widget_set_style (msg->bcc[1], style);
        gtk_widget_set_style (msg->fcc[1], style);
        gtk_widget_set_style (msg->reply_to[1], style);
        gtk_widget_set_style (msg->comments[1], style);
        gtk_widget_set_style (msg->keywords[1], style);

	gtk_style_unref(style);
    }				/* endif: font found */
    return FALSE;
}

/* spell_check_cb
 * 
 * Start the spell check, disable appropriate menu items so users
 * can't screw up spell-check midway through 
 * */
static void
spell_check_cb(GtkWidget * widget, BalsaSendmsg * msg)
{
    BalsaSpellCheck *sc;

    sc = BALSA_SPELL_CHECK(msg->spell_checker);

    /* configure the spell checker */
    balsa_spell_check_set_language(sc, msg->locale);

    gtk_widget_show_all(GTK_WIDGET(sc));
    balsa_spell_check_set_character_set(sc, msg->charset);
    balsa_spell_check_set_module(sc,
				 spell_check_modules_name
				 [balsa_app.module]);
    balsa_spell_check_set_suggest_mode(sc,
				       spell_check_suggest_mode_name
				       [balsa_app.suggestion_mode]);
    balsa_spell_check_set_ignore_length(sc, balsa_app.ignore_size);
    if (msg->font)
	balsa_spell_check_set_font(sc, msg->font);
    gtk_notebook_set_page(GTK_NOTEBOOK(msg->notebook), spell_check_page);

    /* disable menu and toolbar items so message can't be modified */
    spell_check_set_sensitive(msg, FALSE);

    if (balsa_app.debug)
	g_print("BalsaSendmsg: switching page to %d\n", spell_check_page);

    balsa_spell_check_start(BALSA_SPELL_CHECK(msg->spell_checker));
}


static void
spell_check_done_cb(BalsaSpellCheck * spell_check, BalsaSendmsg * msg)
{
    gtk_widget_hide(GTK_WIDGET(spell_check));
    /* switch notebook page back to mail headers */
    gtk_notebook_set_page(GTK_NOTEBOOK(msg->notebook), mail_headers_page);

    /* reactivate menu and toolbar items */
    spell_check_set_sensitive(msg, TRUE);

    if (balsa_app.debug)
	g_print("BalsaSendmsg: switching page to %d\n", mail_headers_page);
}


static void
spell_check_set_sensitive(BalsaSendmsg * msg, gboolean state)
{
    GList *list;

    for(list = msg->spell_check_disable_list; list; list = list->next)
	gtk_widget_set_sensitive(GTK_WIDGET(list->data), state);

    if (state)
	check_readiness(msg);
}


static void
lang_brazilian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_BRAZILIAN_POS);
}
static void
lang_catalan_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_CATALAN_POS);
}
static void
lang_chinese_simplified_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_CHINESE_SIMPLIFIED_POS);
}
static void
lang_chinese_traditional_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_CHINESE_TRADITIONAL_POS);
}
static void
lang_danish_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_DANISH_POS);
}
static void
lang_german_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_GERMAN_POS);
}
static void
lang_dutch_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_DUTCH_POS);
}
static void
lang_english_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_ENGLISH_POS);
}
static void
lang_estonian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_ESTONIAN_POS);
}
static void
lang_finnish_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_FINNISH_POS);
}
static void
lang_french_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_FRENCH_POS);
}
static void
lang_greek_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_GREEK_POS);
}
static void
lang_hungarian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_HUNGARIAN_POS);
}
static void
lang_italian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_ITALIAN_POS);
}
static void
lang_japanese_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_JAPANESE_POS);
}
static void
lang_korean_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_KOREAN_POS);
}
static void
lang_latvian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_LATVIAN_POS);
}
static void
lang_lithuanian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_LITHUANIAN_POS);
}
static void
lang_norwegian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_NORWEGIAN_POS);
}
static void
lang_polish_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_POLISH_POS);
}
static void
lang_portugese_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_PORTUGESE_POS);
}
static void
lang_romanian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_ROMANIAN_POS);
}
static void
lang_russian_iso_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_RUSSIAN_ISO_POS);
}
static void
lang_russian_koi_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_RUSSIAN_KOI_POS);
}
static void
lang_slovak_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_SLOVAK_POS);
}
static void
lang_spanish_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_SPANISH_POS);
}
static void
lang_swedish_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_SWEDISH_POS);
}
static void
lang_turkish_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_TURKISH_POS);
}
static void
lang_ukrainian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_UKRAINIAN_POS);
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
    gint pos;

    g_return_val_if_fail(message_list != NULL, NULL);

    message = message_list->data;
    bsmsg = sendmsg_window_new(w, message, type);
    if (type == SEND_FORWARD_INLINE)
        pos = gtk_editable_get_position(GTK_EDITABLE(bsmsg->text));

    while (message_list = g_list_next(message_list)) {
        message = message_list->data;
        if (type == SEND_FORWARD_ATTACH)
            attach_message(bsmsg, message);
        else if (type == SEND_FORWARD_INLINE) {
            GString *body = quoteBody(bsmsg, message, type);
            gtk_editable_insert_text(GTK_EDITABLE(bsmsg->text), body->str,
                                     body->len, &pos);
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
                if (g_strncasecmp(url, "mailto:", 7) == 0) {
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
        } else if (!isblank(*str))
            break;
        ++str;
    }
    return str;
}
