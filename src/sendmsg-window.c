/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-2000 Stuart Parmenter and others, see AUTHORS file.
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
     nasty deferred setup-depentent crash with double free symptoms,
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
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "libbalsa.h"

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#include "threads.h"
#endif

#include "sendmsg-window.h"
#include "address-book.h"
#include "expand-alias.h"
#include "main.h"
#include "print.h"
#include "spell-check.h"

static gchar *read_signature(void);
static gint include_file_cb(GtkWidget *, BalsaSendmsg *);
static gint send_message_cb(GtkWidget *, BalsaSendmsg *);
static gint postpone_message_cb(GtkWidget *, BalsaSendmsg *);
static gint print_message_cb(GtkWidget *, BalsaSendmsg *);
static gint attach_clicked(GtkWidget *, gpointer);
static gint close_window(GtkWidget *, gpointer);
static gint check_if_regular_file(const gchar *);
static void balsa_sendmsg_destroy(BalsaSendmsg * bsm);

static void check_readiness(GtkEditable * w, BalsaSendmsg * bsmsg);
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

static void spell_check_cb(GtkWidget * widget, BalsaSendmsg *);
static void spell_check_done_cb(BalsaSpellCheck * spell_check,
				BalsaSendmsg *);
static void spell_check_set_sensitive(BalsaSendmsg * msg, gboolean state);

static gint set_locale(GtkWidget *, BalsaSendmsg *, gint);

/* Standard DnD types */
enum {
    TARGET_URI_LIST,
    TARGET_EMAIL,
};

static GtkTargetEntry drop_types[] = {
    {"text/uri-list", 0, TARGET_URI_LIST}
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


static GnomeUIInfo main_toolbar[] = {
#define TOOL_SEND_POS 0
    GNOMEUIINFO_ITEM_STOCK(N_("Send"), N_("Send this mail"),
			   send_message_cb,
			   GNOME_STOCK_PIXMAP_MAIL_SND),
    GNOMEUIINFO_SEPARATOR,
#define TOOL_ATTACH_POS 2
    GNOMEUIINFO_ITEM_STOCK(N_("Attach"),
			   N_("Add attachments to this message"),
			   attach_clicked, GNOME_STOCK_PIXMAP_ATTACH),
    GNOMEUIINFO_SEPARATOR,
#define TOOL_POSTPONE_POS 4
    GNOMEUIINFO_ITEM_STOCK(N_("Postpone"),
			   N_("Continue this message later"),
			   postpone_message_cb, GNOME_STOCK_PIXMAP_SAVE),
    GNOMEUIINFO_SEPARATOR,
#define TOOL_SPELLING_POS 6
    GNOMEUIINFO_ITEM_STOCK(N_("Check Spelling"),
			   N_
			   ("Run a spelling check on the current message"),
			   spell_check_cb, GNOME_STOCK_PIXMAP_SPELLCHECK),
    GNOMEUIINFO_SEPARATOR,
#define TOOL_PRINT_POS 8
    GNOMEUIINFO_ITEM_STOCK(N_("Print"), N_("Print"),
			   print_message_cb, GNOME_STOCK_PIXMAP_PRINT),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_ITEM_STOCK(N_("Cancel"), N_("Cancel"),
			   close_window, GNOME_STOCK_PIXMAP_CLOSE),
    GNOMEUIINFO_END
};

static GnomeUIInfo file_menu[] = {
#define MENU_FILE_INCLUDE_POS 0
    GNOMEUIINFO_ITEM_STOCK(N_("_Include File..."), NULL,
			   include_file_cb, GNOME_STOCK_MENU_OPEN),

#define MENU_FILE_ATTACH_POS 1
    GNOMEUIINFO_ITEM_STOCK(N_("_Attach File..."), NULL,
			   attach_clicked, GNOME_STOCK_MENU_ATTACH),
    GNOMEUIINFO_SEPARATOR,

#define MENU_FILE_SEND_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("_Send"),
			   N_("Send the currently edited message"),
			   send_message_cb, GNOME_STOCK_MENU_MAIL_SND),

#define MENU_FILE_POSTPONE_POS 4
    GNOMEUIINFO_ITEM_STOCK(N_("_Postpone"), NULL,
			   postpone_message_cb, GNOME_STOCK_MENU_SAVE),

#define MENU_FILE_PRINT_POS 5
    GNOMEUIINFO_ITEM_STOCK(N_("Print..."), N_("Print the edited message"),
			   print_message_cb, GNOME_STOCK_PIXMAP_PRINT),
    GNOMEUIINFO_SEPARATOR,

#define MENU_FILE_CLOSE_POS 7
    GNOMEUIINFO_MENU_CLOSE_ITEM(close_window, NULL),

    GNOMEUIINFO_END
};

/* Cut, Copy&Paste are in our case just a placeholders because they work 
   anyway */
static GnomeUIInfo edit_menu[] = {
    GNOMEUIINFO_MENU_CUT_ITEM(cut_cb, NULL),
    GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
    GNOMEUIINFO_MENU_PASTE_ITEM(paste_cb, NULL),
    {GNOME_APP_UI_ITEM, N_("Select All"), NULL,
     (gpointer) select_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'A', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_WRAP_BODY 5
    {GNOME_APP_UI_ITEM, N_("_Wrap Body"), N_("Wrap message lines"),
     (gpointer) wrap_body_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_z, GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_REFLOW_PARA 7
    {GNOME_APP_UI_ITEM, N_("_Reflow Paragraph"), NULL,
     (gpointer) reflow_par_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_r, GDK_CONTROL_MASK, NULL},
#define EDIT_MENU_REFLOW_MESSAGE 8
    {GNOME_APP_UI_ITEM, N_("R_eflow Message"), NULL,
     (gpointer) reflow_body_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE, NULL,
     GDK_r, GDK_CONTROL_MASK | GDK_SHIFT_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define EDIT_MENU_SPELL_CHECK 10
    {GNOME_APP_UI_ITEM, N_("Check Spelling"), NULL,
     (gpointer) spell_check_cb, NULL, NULL,
     GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_SPELLCHECK,
     GDK_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, NULL},
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

#if MENU_TOGGLE_KEYWORDS_POS+1 != VIEW_MENU_LENGTH
#error Inconsistency in defined lengths.
#endif

static void lang_brazilian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_catalan_cb(GtkWidget *, BalsaSendmsg *);
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
static void lang_baltic_cb(GtkWidget *, BalsaSendmsg *);
static void lang_norwegian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_polish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_portugese_cb(GtkWidget *, BalsaSendmsg *);
static void lang_russian_cb(GtkWidget *, BalsaSendmsg *);
static void lang_slovak_cb(GtkWidget *, BalsaSendmsg *);
static void lang_spanish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_swedish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_turkish_cb(GtkWidget *, BalsaSendmsg *);
static void lang_ukrainian_cb(GtkWidget *, BalsaSendmsg *);

static GnomeUIInfo locale_ah_menu[] = {
    GNOMEUIINFO_ITEM_NONE(N_("Baltic"), NULL, lang_baltic_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Brazilian"), NULL, lang_brazilian_cb),
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
    GNOMEUIINFO_END
};

static GnomeUIInfo locale_iz_menu[] = {
    GNOMEUIINFO_ITEM_NONE(N_("Italian"), NULL, lang_italian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Japanese"), NULL, lang_japanese_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Korean"), NULL, lang_korean_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Norwegian"), NULL, lang_norwegian_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Polish"), NULL, lang_polish_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Portugese"), NULL, lang_portugese_cb),
    GNOMEUIINFO_ITEM_NONE(N_("Russian"), NULL, lang_russian_cb),
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
    GNOMEUIINFO_SUBTREE(N_("_A-H"), locale_ah_menu),
    GNOMEUIINFO_SUBTREE(N_("_I-Z"), locale_iz_menu),
    GNOMEUIINFO_SEPARATOR,
#define LANG_CURRENT_POS 3
    GNOMEUIINFO_ITEM_NONE(NULL, NULL, NULL),
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


#define MAIN_MENUS_COUNT 4
static GnomeUIInfo main_menu[] = {
#define MAIN_FILE_MENU 0
    GNOMEUIINFO_MENU_FILE_TREE(file_menu),
#define MAIN_EDIT_MENU 1
    GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
#define MAIN_VIEW_MENU 2
    GNOMEUIINFO_SUBTREE(N_("_Show"), view_menu),
#define MAIN_CHARSET_MENU 3
    GNOMEUIINFO_SUBTREE(N_("_Language"), lang_menu),
    GNOMEUIINFO_END
};

/* the array of locale names and charset names included in the MIME
   type information.  
*/
struct {
    const gchar *locale, *charset, *lang_name;
    gboolean use_fontset;
} locales[] = {
#define LOC_BRAZILIAN_POS 0
    {
    "pt_BR", "ISO-8859-1", N_("Brazilian"), FALSE},
#define LOC_CATALAN_POS   1
    {
    "ca_ES", "ISO-8859-1", N_("Catalan"), FALSE},
#define LOC_DANISH_POS    2
    {
    "da_DK", "ISO-8859-1", N_("Danish"), FALSE},
#define LOC_GERMAN_POS    3
    {
    "de_DE", "ISO-8859-1", N_("German"), FALSE},
#define LOC_DUTCH_POS     4
    {
    "nl_NL", "ISO-8859-1", N_("Dutch"), FALSE},
#define LOC_ENGLISH_POS   5
    {
    "en_GB", "ISO-8859-1", N_("English"), FALSE},
#define LOC_ESTONIAN_POS  6
    {
    "et_EE", "ISO-8859-1", N_("Estonian"), FALSE},
#define LOC_FINNISH_POS   7
    {
    "fi_FI", "ISO-8859-1", N_("Finnish"), FALSE},
#define LOC_FRENCH_POS    8
    {
    "fr_FR", "ISO-8859-1", N_("French"), FALSE},
#define LOC_GREEK_POS     9
    {
    "el_GR", "ISO-8859-7", N_("Greek"), FALSE},
#define LOC_HUNGARIAN_POS 10
    {
    "hu_HU", "ISO-8859-2", N_("Hungarian"), FALSE},
#define LOC_ITALIAN_POS   11
    {
    "it_IT", "ISO-8859-1", N_("Italian"), FALSE},
#define LOC_JAPANESE_POS  12
    {
    "ja_JP", "euc-jp", N_("Japanese"), TRUE},
#define LOC_KOREAN_POS    13
    {
    "ko_OK", "euc-kr", N_("Korean"), TRUE},
#define LOC_BALTIC_POS    14
    {
    "lt_LT", "ISO-8859-13", N_("Baltic"), FALSE},
#define LOC_NORWEGIAN_POS 15
    {
    "no_NO", "ISO-8859-1", N_("Norwegian"), FALSE},
#define LOC_POLISH_POS    16
    {
    "pl_PL", "ISO-8859-2", N_("Polish"), FALSE},
#define LOC_PORTUGESE_POS 17
    {
    "pt_PT", "ISO-8859-1", N_("Portugese"), FALSE},
#define LOC_RUSSIAN_POS   18
    {
    "ru_RU", "ISO-8859-5", N_("Russian"), FALSE},
#define LOC_SLOVAK_POS    19
    {
    "sl_SI", "ISO-8859-2", N_("Slovak"), FALSE},
#define LOC_SPANISH_POS   20
    {
    "es_ES", "ISO-8859-1", N_("Spanish"), FALSE},
#define LOC_SWEDISH_POS   21
    {
    "sv_SE", "ISO-8859-1", N_("Swedish"), FALSE},
#define LOC_TURKISH_POS   22
    {
    "tr_TR", "ISO-8859-9", N_("Turkish"), FALSE},
#define LOC_UKRAINIAN_POS 23
    {
    "uk_UK", "KOI-8-U", N_("Ukrainian"), FALSE}
};

static gint mail_headers_page;
static gint spell_check_page;


/* the callback handlers */
static gint
close_window(GtkWidget * widget, gpointer data)
{
    balsa_sendmsg_destroy((BalsaSendmsg *) data);
    return TRUE;
}

static gint
delete_event_cb(GtkWidget * widget, GdkEvent * e, gpointer data)
{
    balsa_sendmsg_destroy((BalsaSendmsg *) data);
    if (balsa_app.debug)
	g_message
	    ("delete_event_cb(): Calling alias_free_addressbook().\n");
    alias_free_addressbook();
    return TRUE;
}

/* the balsa_sendmsg destructor; copies first the shown headers setting
   to the balsa_app structure.
*/
static void
balsa_sendmsg_destroy(BalsaSendmsg * bsm)
{
    int i;
    gchar newStr[ELEMENTS(headerDescs) * 20];	/* assumes that longest header ID
						   has no more than 19 characters */

    g_assert(bsm != NULL);
    g_assert(ELEMENTS(headerDescs) == ELEMENTS(bsm->view_checkitems));

    newStr[0] = '\0';

    for (i = 0; i < ELEMENTS(headerDescs); i++)
	if (GTK_CHECK_MENU_ITEM(bsm->view_checkitems[i])->active) {
	    strcat(newStr, headerDescs[i].name);
	    strcat(newStr, " ");
	}
    if (balsa_app.compose_headers)	/* should never fail... */
	g_free(balsa_app.compose_headers);

    balsa_app.compose_headers = g_strdup(newStr);

    if (bsm->orig_message) {
	if (bsm->orig_message->mailbox)
	    libbalsa_mailbox_close(bsm->orig_message->mailbox);
	gtk_object_unref(GTK_OBJECT(bsm->orig_message));
    }

    if (balsa_app.debug)
	printf("balsa_sendmsg_destroy: Freeing bsm\n");
    gtk_widget_destroy(bsm->window);
    g_list_free(bsm->spell_check_disable_list);
    if (bsm->font) {
	gdk_font_unref(bsm->font);
	bsm->font = NULL;
    }
    g_free(bsm);

#ifdef BALSA_USE_THREADS
    if (balsa_app.compose_email && !sending_mail)
	balsa_exit();
#else
    if (balsa_app.compose_email)
	balsa_exit();
#endif
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
    int i, j, maxfit = -1, maxpos;

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

/* remove_attachment - right mouse button callback */
static void
remove_attachment(GtkWidget * widget, GnomeIconList * ilist)
{
    gint num = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(ilist),
						   "selectednumbertoremove"));
    gnome_icon_list_remove(ilist, num);
    gtk_object_remove_data(GTK_OBJECT(ilist), "selectednumbertoremove");
}

/* the menu is created on right-button click on an attachement */
static GtkWidget *
create_popup_menu(GnomeIconList * ilist, gint num)
{
    GtkWidget *menu, *menuitem;
    menu = gtk_menu_new();
    menuitem = gtk_menu_item_new_with_label(_("Remove"));
    gtk_object_set_data(GTK_OBJECT(ilist), "selectednumbertoremove",
			GINT_TO_POINTER(num));
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(remove_attachment), ilist);
    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);

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

/* add_attachment:
   adds given filename to the list.
   takes over the ownership of filename.
*/
static void
add_attachment(GnomeIconList * iconlist, char *filename)
{
    /* FIXME: the path to the file must not be hardcoded */
    /* gchar *pix = gnome_pixmap_file ("balsa/attachment.png"); */
    gchar *pix = balsa_pixmap_finder("balsa/attachment.png");

    if (balsa_app.debug)
	fprintf(stderr, "Trying to attach '%s'\n", filename);
    if (!check_if_regular_file(filename)) {
	/*c_i_r_f() will pop up an error dialog for us, so we need do nothing. */
	return;
    }

    if (pix && check_if_regular_file(pix)) {
	gint pos;
	pos = gnome_icon_list_append(iconlist, pix, g_basename(filename));
	gnome_icon_list_set_icon_data(iconlist, pos, filename);
    } else
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _
			  ("The attachment pixmap (balsa/attachment.png) cannot be found.\n"
			   "This means you cannot attach any files.\n"));
}

static gint
check_if_regular_file(const gchar * filename)
{
    struct stat s;
    GtkWidget *msgbox;
    gchar *ptr = NULL;
    gint result = TRUE;

    if (stat(filename, &s)) {
	ptr = g_strdup_printf(_("Cannot get info on file '%s': %s\n"),
			      filename, strerror(errno));
	result = FALSE;
    } else if (!S_ISREG(s.st_mode)) {
	ptr =
	    g_strdup_printf(_("Attachment is not a regular file: '%s'\n"),
			    filename);
	result = FALSE;
    }
    if (ptr) {
	msgbox = gnome_message_box_new(ptr, GNOME_MESSAGE_BOX_ERROR,
				       _("Cancel"), NULL);
	g_free(ptr);
	gtk_window_set_modal(GTK_WINDOW(msgbox), TRUE);
	gnome_dialog_run(GNOME_DIALOG(msgbox));
    }
    return result;
}

static void
attach_dialog_ok(GtkWidget * widget, gpointer data)
{
    GtkFileSelection *fs;
    GnomeIconList *iconlist;
    gchar *filename, *dir, *p, *sel_file;
    GList *node;

    fs = GTK_FILE_SELECTION(data);
    iconlist = GNOME_ICON_LIST(gtk_object_get_user_data(GTK_OBJECT(fs)));

    sel_file = g_strdup(gtk_file_selection_get_filename(fs));
    dir = g_strdup(sel_file);
    p = strrchr(dir, '/');
    if (p)
	*(p + 1) = '\0';

    add_attachment(iconlist, sel_file);
    for (node = GTK_CLIST(fs->file_list)->selection; node;
	 node = g_list_next(node)) {
	gtk_clist_get_text(GTK_CLIST(fs->file_list),
			   GPOINTER_TO_INT(node->data), 0, &p);
	filename = g_strconcat(dir, p, NULL);
	if (strcmp(filename, sel_file) != 0)
	    add_attachment(iconlist, filename);
	/* do not g_free(filename) - the add_attachment arg is not const */
	/* g_free(filename); */
    }

    gtk_widget_destroy(GTK_WIDGET(fs));
    if (balsa_app.attach_dir)
	g_free(balsa_app.attach_dir);

    balsa_app.attach_dir = dir;	/* steal the reference to the string */

    /* FIXME: show attachment list */
}

/* attach_clicked - menu and toolbar callback */
static gint
attach_clicked(GtkWidget * widget, gpointer data)
{
    GtkWidget *fsw;
    GnomeIconList *iconlist;
    GtkFileSelection *fs;
    BalsaSendmsg *bsm;

    bsm = data;

    iconlist = GNOME_ICON_LIST(bsm->attachments[1]);

    fsw = gtk_file_selection_new(_("Attach file"));
    gtk_object_set_user_data(GTK_OBJECT(fsw), iconlist);

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

    gtk_window_set_wmclass(GTK_WINDOW(fsw), "file", "Balsa");
    gtk_widget_show(fsw);

    return TRUE;
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
    GList *names, *l;

    names = gnome_uri_list_extract_filenames(selection_data->data);

    for (l = names; l; l = l->next)
	add_attachment(GNOME_ICON_LIST(bsmsg->attachments[1]),
		       g_strdup((char *) l->data));

    gnome_uri_list_free_strings(names);

    /* show attachment list */
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
				   (bsmsg->view_checkitems
				    [MENU_TOGGLE_ATTACHMENTS_POS]), TRUE);
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

    if (strlen(gtk_entry_get_text(GTK_ENTRY(widget))) == 0) {
	gtk_entry_set_text(GTK_ENTRY(widget), selection_data->data);
	return;
    } else {
	gtk_entry_append_text(GTK_ENTRY(widget), ",");
	gtk_entry_append_text(GTK_ENTRY(widget), selection_data->data);
    }
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
    gtk_signal_connect(GTK_OBJECT(arr[1]), "activate",
		       GTK_SIGNAL_FUNC(next_entrybox), arr[1]);
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
 * 
 * Output: GtkWidget *arr[]   - An array of GtkWidgets, as follows:
 *            arr[0]          - the label.
 *            arr[1]          - the entrybox.
 *            arr[2]          - the button.
 */
static void
create_email_entry(GtkWidget * table, const gchar * label, int y_pos,
		   const gchar * icon, GtkWidget * arr[])
{

    gint *focus_counter;

    create_string_entry(table, label, y_pos, arr);

    arr[2] = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(arr[2]), GTK_RELIEF_NONE);
    GTK_WIDGET_UNSET_FLAGS(arr[2], GTK_CAN_FOCUS);
    gtk_container_add(GTK_CONTAINER(arr[2]),
		      gnome_stock_pixmap_widget(NULL, icon));
    gtk_table_attach(GTK_TABLE(table), arr[2], 2, 3, y_pos, y_pos + 1,
		     0, 0, 0, 0);
    gtk_signal_connect(GTK_OBJECT(arr[2]), "clicked",
		       GTK_SIGNAL_FUNC(address_book_cb),
		       (gpointer) arr[1]);
    gtk_signal_connect(GTK_OBJECT(arr[1]), "drag_data_received",
		       GTK_SIGNAL_FUNC(to_add), NULL);
    gtk_drag_dest_set(GTK_WIDGET(arr[1]), GTK_DEST_DEFAULT_ALL,
		      email_field_drop_types,
		      ELEMENTS(email_field_drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    gtk_signal_connect(GTK_OBJECT(arr[1]), "key-press-event",
		       GTK_SIGNAL_FUNC(key_pressed_cb), arr[1]);
    gtk_signal_connect(GTK_OBJECT(arr[1]), "button-press-event",
		       GTK_SIGNAL_FUNC(button_pressed_cb), arr[1]);
    /*
     * And these make sure we rescan the input if the user plays
     * around.
     */
    gtk_signal_connect(GTK_OBJECT(arr[1]), "focus-out-event",
		       GTK_SIGNAL_FUNC(lost_focus_cb), arr[1]);
    gtk_signal_connect(GTK_OBJECT(arr[1]), "destroy",
		       GTK_SIGNAL_FUNC(destroy_cb), arr[1]);
    focus_counter = g_malloc(sizeof(gint));
    *focus_counter = 1;
    gtk_object_set_data(GTK_OBJECT(arr[1]), "focus_c", focus_counter);
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


    table = gtk_table_new(10, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);

    /* From: */
    create_email_entry(table, _("From:"), 0, GNOME_STOCK_MENU_BOOK_BLUE,
		       msg->from);
    /* To: */
    create_email_entry(table, _("To:"), 1, GNOME_STOCK_MENU_BOOK_RED,
		       msg->to);
    gtk_signal_connect(GTK_OBJECT(msg->to[1]), "changed",
		       GTK_SIGNAL_FUNC(check_readiness), msg);

    /* Subject: */
    create_string_entry(table, _("Subject:"), 2, msg->subject);
    gtk_object_set_data(GTK_OBJECT(msg->to[1]), "next_in_line",
			msg->subject[1]);

    /* cc: */
    create_email_entry(table, _("cc:"), 3, GNOME_STOCK_MENU_BOOK_YELLOW,
		       msg->cc);
    gtk_object_set_data(GTK_OBJECT(msg->subject[1]), "next_in_line",
			msg->cc[1]);

    /* bcc: */
    create_email_entry(table, _("bcc:"), 4, GNOME_STOCK_MENU_BOOK_GREEN,
		       msg->bcc);
    gtk_object_set_data(GTK_OBJECT(msg->cc[1]), "next_in_line",
			msg->bcc[1]);

    /* fcc: */
    msg->fcc[0] = gtk_label_new(_("fcc:"));
    gtk_misc_set_alignment(GTK_MISC(msg->fcc[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(msg->fcc[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    gtk_table_attach(GTK_TABLE(table), msg->fcc[0], 0, 1, 5, 6, GTK_FILL,
		     GTK_FILL | GTK_SHRINK, 0, 0);

    msg->fcc[1] = gtk_combo_new();
    gtk_combo_set_use_arrows(GTK_COMBO(msg->fcc[1]), 0);
    gtk_combo_set_use_arrows_always(GTK_COMBO(msg->fcc[1]), 0);
    gtk_object_set_data(GTK_OBJECT(msg->bcc[1]), "next_in_line",
			msg->fcc[1]);

    if (balsa_app.mailbox_nodes) {
	GNode *walk;
	GList *glist = NULL;

	glist = g_list_append(glist, balsa_app.sentbox->name);
	glist = g_list_append(glist, balsa_app.draftbox->name);
	glist = g_list_append(glist, balsa_app.outbox->name);
	glist = g_list_append(glist, balsa_app.trash->name);
	walk = g_node_last_child(balsa_app.mailbox_nodes);
	while (walk) {
	    glist =
		g_list_append(glist,
			      ((MailboxNode *) ((walk)->data))->name);
	    walk = walk->prev;
	}
	gtk_combo_set_popdown_strings(GTK_COMBO(msg->fcc[1]), glist);
    }
    gtk_table_attach(GTK_TABLE(table), msg->fcc[1], 1, 3, 5, 6,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 0, 0);

    /* Reply To: */
    create_email_entry(table, _("Reply To:"), 6, GNOME_STOCK_MENU_BOOK_RED,
		       msg->reply_to);
    gtk_object_set_data(GTK_OBJECT(msg->fcc[1]), "next_in_line",
			msg->reply_to[1]);

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

    gtk_widget_set_usize(msg->attachments[1], -1, 50);

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


/* create_text_area 
   Creates the text entry part of the compose window.
*/
static GtkWidget *
create_text_area(BalsaSendmsg * msg)
{
    GtkWidget *table;
    GtkWidget *hscrollbar;
    GtkWidget *vscrollbar;

    table = gtk_table_new(2, 2, FALSE);

    msg->text = gtk_text_new(NULL, NULL);
    gtk_text_set_editable(GTK_TEXT(msg->text), TRUE);
    gtk_text_set_word_wrap(GTK_TEXT(msg->text), TRUE);
    balsa_spell_check_set_text(BALSA_SPELL_CHECK(msg->spell_checker),
			       GTK_TEXT(msg->text));

    /*gtk_widget_set_usize (msg->text, 
       (82 * 7) + (2 * msg->text->style->klass->xthickness), 
       -1); */
    gtk_widget_show(msg->text);
    gtk_table_attach_defaults(GTK_TABLE(table), msg->text, 0, 1, 0, 1);
    hscrollbar = gtk_hscrollbar_new(GTK_TEXT(msg->text)->hadj);
    GTK_WIDGET_UNSET_FLAGS(hscrollbar, GTK_CAN_FOCUS);
    gtk_table_attach(GTK_TABLE(table), hscrollbar, 0, 1, 1, 2,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

    vscrollbar = gtk_vscrollbar_new(GTK_TEXT(msg->text)->vadj);
    GTK_WIDGET_UNSET_FLAGS(vscrollbar, GTK_CAN_FOCUS);
    gtk_table_attach(GTK_TABLE(table), vscrollbar, 1, 2, 0, 1,
		     GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    gtk_widget_show_all(GTK_WIDGET(table));

    gtk_object_set_data(GTK_OBJECT(msg->reply_to[1]), "next_in_line",
			msg->text);

    return table;
}

/* continueBody ---------------------------------------------------------
   a short-circuit procedure for the 'Continue action'
   basically copies the text over to the entry field.
   NOTE that rbdy == NULL if message has no text parts.
*/
static void
continueBody(BalsaSendmsg * msg, LibBalsaMessage * message)
{
    GString *rbdy;

    libbalsa_message_body_ref(message);
    rbdy = content2reply(message, NULL, -1);
    if (rbdy) {
	gtk_text_insert(GTK_TEXT(msg->text), NULL, NULL, NULL, rbdy->str,
			strlen(rbdy->str));
	g_string_free(rbdy, TRUE);
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
    gchar *str, *date;
    const gchar *personStr;

    libbalsa_message_body_ref(message);

    personStr = libbalsa_address_get_name(message->from);
    if (!personStr)
	personStr = _("you");

    if (message->date) {
	date =
	    libbalsa_message_date_to_gchar(message, balsa_app.date_string);
	str = g_strdup_printf(_("\nOn %s %s wrote:\n"), date, personStr);
	g_free(date);
    } else
	str = g_strdup_printf(_("\n%s wrote:\n"), personStr);

    body = content2reply(message,
			 (type == SEND_REPLY || type == SEND_REPLY_ALL) ?
			 balsa_app.quote_str : NULL,
			 balsa_app.wordwrap ? balsa_app.wraplength : -1);

    if (body)
	body = g_string_prepend(body, str);
    else
	body = g_string_new(str);
    g_free(str);

    if (!msg->charset)
	msg->charset = libbalsa_message_charset(message);
    libbalsa_message_body_unref(message);
    return body;
}

/* fillBody --------------------------------------------------------------
   fills the body of the message to be composed based on the given message.
   First quotes the original one and then adds the signature
*/
static void
fillBody(BalsaSendmsg * msg, LibBalsaMessage * message, SendType type)
{
    GString *body = NULL;
    gchar *signature;
    gint pos = 0;

    if (type != SEND_NORMAL && message)
	body = quoteBody(msg, message, type);
    else
	body = g_string_new("");

    if ((signature = read_signature()) != NULL) {
	if (((type == SEND_REPLY || type == SEND_REPLY_ALL) &&
	     balsa_app.sig_whenreply) ||
	    ((type == SEND_FORWARD) && balsa_app.sig_whenforward) ||
	    ((type == SEND_NORMAL) && balsa_app.sig_sending)) {
	    body = g_string_append_c(body, '\n');

	    if (balsa_app.sig_separator
		&& g_strncasecmp(signature, "--\n", 3)
		&& g_strncasecmp(signature, "-- \n", 4))
		body = g_string_append(body, "-- \n");
	    body = g_string_append(body, signature);
	}
	g_free(signature);
    }

    gtk_editable_insert_text(GTK_EDITABLE(msg->text), body->str, body->len,
			     &pos);
    gtk_editable_set_position(GTK_EDITABLE(msg->text), 0);
    g_string_free(body, TRUE);
}


BalsaSendmsg *
sendmsg_window_new(GtkWidget * widget, LibBalsaMessage * message,
		   SendType type)
{
    GtkWidget *window;
    GtkWidget *paned = gtk_vpaned_new();
    gchar *newsubject = NULL, *tmp;
    BalsaSendmsg *msg = NULL;
    GList *list;
    gint i;

    msg = g_malloc(sizeof(BalsaSendmsg));
    msg->font = NULL;
    msg->charset = NULL;
    msg->locale = NULL;


    alias_load_addressbook();

    switch (type) {
    case SEND_REPLY:
    case SEND_REPLY_ALL:
	window = gnome_app_new("balsa", _("Reply to "));
	msg->orig_message = message;
	break;

    case SEND_FORWARD:
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
    if (message) {		/* ref message so we don't loose it ieven if it is deleted */
	gtk_object_ref(GTK_OBJECT(message));
	/* reference the original mailbox so we don't loose the
	   mail even if the mailbox is closed */
	if (message->mailbox)
	    libbalsa_mailbox_open(message->mailbox, FALSE);
    }
    msg->window = window;
    msg->type = type;

    gtk_signal_connect(GTK_OBJECT(msg->window), "delete_event",
		       GTK_SIGNAL_FUNC(delete_event_cb), msg);
    gtk_signal_connect(GTK_OBJECT(msg->window), "destroy_event",
		       GTK_SIGNAL_FUNC(delete_event_cb), msg);

    fill_language_menu();
    gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, msg);
    gnome_app_create_toolbar_with_data(GNOME_APP(window), main_toolbar,
				       msg);

    msg->ready_widgets[0] = file_menu[MENU_FILE_SEND_POS].widget;
    msg->ready_widgets[1] = main_toolbar[TOOL_SEND_POS].widget;
    msg->current_language_menu = lang_menu[LANG_CURRENT_POS].widget;

    /* create spell checking disable widget list */
    list = NULL;

    for (i = 0; i < MAIN_MENUS_COUNT; ++i) {
	if (i != MAIN_FILE_MENU)
	    list = g_list_append(list, (gpointer) main_menu[i].widget);
    }

    for (i = 0; i < MENU_FILE_CLOSE_POS; ++i) {
	if (i != 2 && i != 6)
	    list = g_list_append(list, (gpointer) file_menu[i].widget);
    }

    for (i = 0; i <= TOOL_PRINT_POS; i += 2) {
	list = g_list_append(list, (gpointer) main_toolbar[i].widget);
    }

    msg->spell_check_disable_list = list;

    /* create the top portion with the to, from, etc in it */
    gtk_paned_add1(GTK_PANED(paned), create_info_pane(msg, type));

    /* create text area for the message */
    gtk_paned_add2(GTK_PANED(paned), create_text_area(msg));

    /* fill in that info: */

    /* To: */
    if (type == SEND_REPLY || type == SEND_REPLY_ALL) {
	LibBalsaAddress *addr = NULL;

	if (message->reply_to)
	    addr = message->reply_to;
	else
	    addr = message->from;

	tmp = libbalsa_address_to_gchar(addr);
	gtk_entry_set_text(GTK_ENTRY(msg->to[1]), tmp);
	g_free(tmp);
    }

    /* From: */
    {
	gchar *from;
	from = g_strdup_printf("%s <%s>", balsa_app.address->full_name,
			       (gchar *) balsa_app.address->
			       address_list->data);
	gtk_entry_set_text(GTK_ENTRY(msg->from[1]), from);
	g_free(from);
    }

    /* Reply To */
    if (balsa_app.replyto)
	gtk_entry_set_text(GTK_ENTRY(msg->reply_to[1]), balsa_app.replyto);

    /* Bcc: */
    {
	if (balsa_app.bcc)
	    gtk_entry_set_text(GTK_ENTRY(msg->bcc[1]), balsa_app.bcc);
    }

    /* Fcc: */
    {
	if (type == SEND_CONTINUE && message->fcc_mailbox != NULL)
	    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(msg->fcc[1])->entry),
			       message->fcc_mailbox);
    }

    /* Subject: */
    switch (type) {
    case SEND_REPLY:
    case SEND_REPLY_ALL:
	if (!message->subject) {
	    newsubject = g_strdup("Re: ");
	    break;
	}

	tmp = message->subject;
	if (strlen(tmp) > 2 &&
	    toupper(tmp[0]) == 'R' &&
	    toupper(tmp[1]) == 'E' && tmp[2] == ':') {
	    newsubject = g_strdup(message->subject);
	    break;
	}
	newsubject = g_strdup_printf("Re: %s", message->subject);
	break;

    case SEND_FORWARD:
	if (!message->subject) {
	    if (message->from && message->from->address_list)
		newsubject = g_strdup_printf("Forwarded message from %s",
					     (gchar *) message->
					     from->address_list->data);
	    else
		newsubject = g_strdup("Forwarded message");
	} else {
	    if (message->from && message->from->address_list)
		newsubject = g_strdup_printf("[%s: %s]",
					     (gchar *) message->
					     from->address_list->data,
					     message->subject);
	    else
		newsubject = g_strdup_printf("Fwd: %s", message->subject);
	}
	break;
    default:
	break;
    }

    if (type == SEND_REPLY ||
	type == SEND_REPLY_ALL || type == SEND_FORWARD) {
	gtk_entry_set_text(GTK_ENTRY(msg->subject[1]), newsubject);
	g_free(newsubject);
	newsubject = NULL;
    }

    if (type == SEND_CONTINUE) {
	if (message->to_list) {
	    tmp = libbalsa_make_string_from_list(message->to_list);
	    gtk_entry_set_text(GTK_ENTRY(msg->to[1]), tmp);
	    g_free(tmp);
	}
	if (message->cc_list) {
	    tmp = libbalsa_make_string_from_list(message->cc_list);
	    gtk_entry_set_text(GTK_ENTRY(msg->cc[1]), tmp);
	    g_free(tmp);
	}
	if (message->bcc_list) {
	    tmp = libbalsa_make_string_from_list(message->bcc_list);
	    gtk_entry_set_text(GTK_ENTRY(msg->bcc[1]), tmp);
	    g_free(tmp);
	}
	if (message->subject)
	    gtk_entry_set_text(GTK_ENTRY(msg->subject[1]),
			       message->subject);
    }

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
    gtk_window_set_default_size(GTK_WINDOW(window),
				(82 * 7) +
				(2 * msg->text->style->klass->xthickness),
				35 * 12);
    gtk_window_set_wmclass(GTK_WINDOW(window), "compose", "Balsa");
    gtk_widget_show(window);


    if (type == SEND_NORMAL || type == SEND_FORWARD)
	gtk_widget_grab_focus(msg->to[1]);
    else
	gtk_widget_grab_focus(msg->text);

    return msg;
}

static gchar *
read_signature(void)
{
    FILE *fp;
    size_t len;
    gchar *ret;

    if (balsa_app.signature_path == NULL
	|| !(fp = fopen(balsa_app.signature_path, "r")))
	return NULL;
    len = libbalsa_readfile(fp, &ret);
    fclose(fp);
    if (len > 0 && ret[len - 1] == '\n')
	ret[len - 1] = '\0';

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
    gtk_window_set_wmclass(GTK_WINDOW(file_selector), "file", "Balsa");
    gtk_widget_show(file_selector);

    return TRUE;
}


/* is_ready_to_send returns TRUE if the message is ready to send or 
   postpone. It tests currently only the "To" field
*/
static gboolean
is_ready_to_send(BalsaSendmsg * bsmsg)
{
    gchar *tmp;
    size_t len;

    tmp = gtk_entry_get_text(GTK_ENTRY(bsmsg->to[1]));
    len = strlen(tmp);

    if (len < 1)		/* empty */
	return FALSE;

    if (tmp[len - 1] == '@')	/* this shouldn't happen */
	return FALSE;

    if (len < 4) {
	if (strchr(tmp, '@'))	/* you won't have an @ in an
				   address less than 4 characters */
	    return FALSE;

	/* assume they are mailing it to someone in their local domain */
    }
    return TRUE;
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

    message->from = libbalsa_address_new_from_string(gtk_entry_get_text
						     (GTK_ENTRY(bsmsg->from
								[1])));

    message->subject = g_strdup(gtk_entry_get_text
				(GTK_ENTRY(bsmsg->subject[1])));
    strip_chars(message->subject, "\r\n");

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
	&& strlen(tmp) > 0)
	message->reply_to = libbalsa_address_new_from_string(tmp);

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
	message->references = g_list_prepend(message->references,
					     g_strdup(bsmsg->
						      orig_message->message_id));

	footime = localtime(&bsmsg->orig_message->date);
	strftime(recvtime, sizeof(recvtime),
		 "%a, %b %d, %Y at %H:%M:%S %z", footime);

	if (bsmsg->orig_message->message_id)
	    message->in_reply_to =
		g_strconcat(bsmsg->orig_message->message_id, "; from ",
			    (gchar *) bsmsg->orig_message->
			    from->address_list->data, " on ", recvtime,
			    NULL);
    }

    body = libbalsa_message_body_new(message);
    body->buffer = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->text), 0,
					  gtk_text_get_length(GTK_TEXT
							      (bsmsg->text)));
    if (balsa_app.wordwrap)
	libbalsa_wrap_string(body->buffer, balsa_app.wraplength);
    body->charset = g_strdup(bsmsg->charset);
    libbalsa_message_append_part(message, body);

    {				/* handle attachments */
	gint i;
	for (i = 0; i < GNOME_ICON_LIST(bsmsg->attachments[1])->icons; i++) {
	    body = libbalsa_message_body_new(message);
	    /* PKGW: This used to be g_strdup'ed. However, the original pointer 
	       was strduped and never freed, so we'll take it. */
	    body->filename = (gchar *)
		gnome_icon_list_get_icon_data(GNOME_ICON_LIST
					      (bsmsg->attachments[1]), i);

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
send_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message;
    if (!is_ready_to_send(bsmsg))
	return FALSE;

    libbalsa_set_charset(bsmsg->charset);
    if (balsa_app.debug)
	fprintf(stderr, "sending with charset: %s\n", bsmsg->charset);

    message = bsmsg2message(bsmsg);

    if (libbalsa_message_send(message, balsa_app.outbox,
			      balsa_app.encoding_style)) {
	if (bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL) {
	    if (bsmsg->orig_message)
		libbalsa_message_reply(bsmsg->orig_message);
	} else if (bsmsg->type == SEND_CONTINUE) {
	    if (bsmsg->orig_message) {
		libbalsa_message_delete(bsmsg->orig_message);
		libbalsa_mailbox_commit_changes(bsmsg->
						orig_message->mailbox);
	    }
	}
    }

    gtk_object_destroy(GTK_OBJECT(message));
    balsa_sendmsg_destroy(bsmsg);

    return TRUE;
}

/* "postpone message" menu and toolbar callback */
static gint
postpone_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message;

    message = bsmsg2message(bsmsg);

    if ((bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL))
	libbalsa_message_postpone(message, balsa_app.draftbox,
				  bsmsg->orig_message,
				  message->fcc_mailbox,
				  balsa_app.encoding_style);
    else
	libbalsa_message_postpone(message, balsa_app.draftbox, NULL,
				  message->fcc_mailbox,
				  balsa_app.encoding_style);

    if (bsmsg->type == SEND_CONTINUE && bsmsg->orig_message) {
	libbalsa_message_delete(bsmsg->orig_message);
	libbalsa_mailbox_commit_changes(bsmsg->orig_message->mailbox);
    }

    gtk_object_destroy(GTK_OBJECT(message));
    balsa_sendmsg_destroy(bsmsg);

    return TRUE;
}

#ifndef HAVE_GNOME_PRINT
/* very harsh print handler. Prints headers and the body only, as raw text
*/
static gint
print_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    gchar *str;
    gchar *dest;
    FILE *lpr;

    dest = g_strdup_printf(balsa_app.PrintCommand.PrintCommand, "-");
    lpr = popen(dest, "w");
    g_free(dest);

    if (!lpr) {
	GtkWidget *msgbox =
	    gnome_message_box_new(_("Cannot execute print command."),
				  GNOME_MESSAGE_BOX_ERROR,
				  _("Cancel"), NULL);
	gtk_window_set_modal(GTK_WINDOW(msgbox), TRUE);
	gnome_dialog_run(GNOME_DIALOG(msgbox));
    }

    str = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->from[1]), 0, -1);
    fprintf(lpr, "From   : %s\n", str);
    g_free(str);
    str = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->to[1]), 0, -1);
    fprintf(lpr, "To     : %s\n", str);
    g_free(str);
    str = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->subject[1]), 0, -1);
    fprintf(lpr, "Subject: %s\n", str);
    g_free(str);

    str = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->text), 0, -1);
    fputs(str, lpr);
    g_free(str);
    fputs("\n\f", lpr);

    if (pclose(lpr) != 0) {
	GtkWidget *msgbox = gnome_message_box_new(_("Error executing lpr"),
						  GNOME_MESSAGE_BOX_ERROR,
						  _("Cancel"), NULL);
	gtk_window_set_modal(GTK_WINDOW(msgbox), TRUE);
	gnome_dialog_run(GNOME_DIALOG(msgbox));
    }
    return TRUE;
}
#else
static gint
print_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *msg = bsmsg2message(bsmsg);
    message_print(msg);
    gtk_object_destroy(GTK_OBJECT(msg));
    return TRUE;
}
#endif

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
    libbalsa_wrap_string(the_text, balsa_app.wraplength);

    gtk_text_freeze(GTK_TEXT(bsmsg->text));
    gtk_editable_delete_text(GTK_EDITABLE(bsmsg->text), 0, -1);
    dummy = 0;
    gtk_editable_insert_text(GTK_EDITABLE(bsmsg->text), the_text,
			     strlen(the_text), &dummy);
    gtk_editable_set_position(GTK_EDITABLE(bsmsg->text), pos);
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
check_readiness(GtkEditable * w, BalsaSendmsg * msg)
{
    gint i;
    gboolean state = is_ready_to_send(msg);

    for (i = 0; i < ELEMENTS(msg->ready_widgets); i++)
	gtk_widget_set_sensitive(msg->ready_widgets[i], state);
}

/* toggle_entry auxiliary function for "header show/hide" toggle menu entries.
 */
static gint
toggle_entry(BalsaSendmsg * bmsg, GtkWidget * entry[], int pos, int cnt)
{
    GtkWidget *parent;

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
			2);}

/* init_menus:
   performs the initial menu setup: shown headers as well as correct
   message charset. Copies also the the menu pointers for further usage
   at the message close  - they would be overwritten if another compose
   window was opened simultaneously.
*/
static void
init_menus(BalsaSendmsg * msg)
{
    int i;

    g_assert(ELEMENTS(headerDescs) == ELEMENTS(msg->view_checkitems));

    for (i = 0; i < ELEMENTS(headerDescs); i++) {
	msg->view_checkitems[i] = view_menu[i].widget;
	if (libbalsa_find_word
	    (headerDescs[i].name, balsa_app.compose_headers)) {
	    /* show... (well, it has already been shown). */
	    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
					   (view_menu[i].widget), TRUE);
	} else {
	    /* or hide... */
	    GTK_SIGNAL_FUNC(view_menu[i].moreinfo) (view_menu[i].widget,
						    msg);
	}
    }

    /* set the charset... */
    i = find_locale_index_by_locale(setlocale(LC_CTYPE, NULL));
    if (msg->charset
	&& g_strcasecmp(locales[i].charset, msg->charset) != 0) {
	i = ELEMENTS(locales) - 1;
	while (i >= 0
	       && g_strcasecmp(locales[i].charset, msg->charset) != 0) i--;
	if (i < 0)
	    i = LOC_ENGLISH_POS;
    }

    set_locale(NULL, msg, i);

    /* gray 'send' and 'postpone' */
    check_readiness(GTK_EDITABLE(msg->to[1]), msg);
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
    gchar *font_name, *tmp;

    if (msg->font)
	gdk_font_unref(msg->font);
    msg->charset = locales[idx].charset;
    msg->locale = locales[idx].locale;
    tmp = g_strdup_printf("%s (%s, %s)", _(locales[idx].lang_name),
			  locales[idx].locale, locales[idx].charset);
    gtk_label_set_text(GTK_LABEL
		       (GTK_BIN(msg->current_language_menu)->child), tmp);
    g_free(tmp);

    font_name = get_font_name(balsa_app.message_font, msg->charset);
    msg->font = locales[idx].use_fontset ?
	gdk_fontset_load(font_name) : gdk_font_load(font_name);
    printf("find font: %s for locale %s (%d)\n", font_name, msg->locale,
	   locales[idx].use_fontset);
    if (!msg->font) {
	printf("Cannot find font: %s for locale %s\n", font_name,
	       msg->locale);
    } else {
	gdk_font_ref(msg->font);
	/* Set the new message style */
#if 0
	{
	    GtkStyle *style;
	    style =
		gtk_style_copy(gtk_widget_get_style
			       (GTK_WIDGET(msg->text)));
	    /* gdk_font_unref(style->font); */
	    style->font = msg->font;
	    gtk_widget_set_style(GTK_WIDGET(msg->text), style);
	    gtk_style_unref(style);
	}
#else
	{
	    gchar *str;
	    gint txt_len, point;
	    gtk_text_freeze(GTK_TEXT(msg->text));
	    point = gtk_editable_get_position(GTK_EDITABLE(msg->text));
	    txt_len = gtk_text_get_length(GTK_TEXT(msg->text));
	    str =
		gtk_editable_get_chars(GTK_EDITABLE(msg->text), 0,
				       txt_len);

	    if (str) {
		gtk_text_set_point(GTK_TEXT(msg->text), 0);
		gtk_text_forward_delete(GTK_TEXT(msg->text), txt_len);
		gtk_text_insert(GTK_TEXT(msg->text), msg->font, NULL, NULL,
				str, txt_len);
		g_free(str);
	    }
	    gtk_text_thaw(GTK_TEXT(msg->text));
	}
#endif
    }				/* endif: font found */
    g_free(font_name);

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

    list = msg->spell_check_disable_list;

    while (list) {
	gtk_widget_set_sensitive(GTK_WIDGET(list->data), state);
	list = list->next;
    }

    if (state)
	check_readiness(NULL, msg);
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
lang_baltic_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_BALTIC_POS);
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
lang_russian_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    set_locale(w, bsmsg, LOC_RUSSIAN_POS);
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
