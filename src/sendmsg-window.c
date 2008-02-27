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
#include <glib/gi18n.h>
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
#include "html.h"

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
#include "address-view.h"
#include "print.h"
#if HAVE_GTKSPELL
#include "gtkspell/gtkspell.h"
#else                           /* HAVE_GTKSPELL */
#include "spell-check.h"
#endif                          /* HAVE_GTKSPELL */
#include "toolbar-factory.h"
#if HAVE_GTKSOURCEVIEW
#include <gtksourceview/gtksourceview.h>
#endif                          /* HAVE_GTKSOURCEVIEW */

#define GNOME_MIME_BUG_WORKAROUND 1
typedef struct {
    pid_t pid_editor;
    gchar *filename;
    BalsaSendmsg *bsmsg;
} balsa_edit_with_gnome_data;

typedef enum { QUOTE_HEADERS, QUOTE_ALL, QUOTE_NOPREFIX } QuoteType;

static void include_file_cb    (GtkAction * action, BalsaSendmsg * bsmsg);
static void toolbar_send_message_cb
                               (GtkAction * action, BalsaSendmsg * bsmsg);
static void send_message_cb    (GtkAction * action, BalsaSendmsg * bsmsg);
static void queue_message_cb   (GtkAction * action, BalsaSendmsg * bsmsg);
static void save_message_cb    (GtkAction * action, BalsaSendmsg * bsmsg);

static gint message_postpone(BalsaSendmsg * bsmsg);
static void postpone_message_cb(GtkAction * action, BalsaSendmsg * bsmsg);

#ifdef HAVE_GTK_PRINT
#if !defined(ENABLE_TOUCH_UI)
static void page_setup_cb      (GtkAction * action, BalsaSendmsg * bsmsg);
#endif /* ENABLE_TOUCH_UI */
#endif
static void print_message_cb   (GtkAction * action, BalsaSendmsg * bsmsg);
static void attach_clicked     (GtkAction * action, gpointer data);

static gboolean attach_message(BalsaSendmsg *bsmsg, LibBalsaMessage *message);
static void insert_selected_messages(BalsaSendmsg * bsmsg, QuoteType type);
static void attach_message_cb  (GtkAction * action, BalsaSendmsg * bsmsg);
static void include_message_cb (GtkAction * action, BalsaSendmsg * bsmsg);

static void close_window_cb    (GtkAction * action, gpointer data);

static gchar* check_if_regular_file(const gchar *);
static void balsa_sendmsg_destroy_handler(BalsaSendmsg * bsmsg);
static void check_readiness(BalsaSendmsg * bsmsg);
static void init_menus(BalsaSendmsg *);
static void toggle_from_cb         (GtkToggleAction * toggle_action,
                                    BalsaSendmsg * bsmsg);
static void toggle_recipients_cb   (GtkToggleAction * toggle_action,
                                    BalsaSendmsg * bsmsg);
static void toggle_replyto_cb      (GtkToggleAction * toggle_action,
                                    BalsaSendmsg * bsmsg);
static void toggle_fcc_cb          (GtkToggleAction * toggle_action,
                                    BalsaSendmsg * bsmsg);
static void toggle_reqdispnotify_cb(GtkToggleAction * toggle_action,
                                    BalsaSendmsg * bsmsg);
static void toggle_format_cb       (GtkToggleAction * toggle_action,
                                    BalsaSendmsg * bsmsg);
#ifdef HAVE_GPGME
static void toggle_sign_cb         (GtkToggleAction * toggle_action,
                                    BalsaSendmsg * bsmsg);
static void toggle_encrypt_cb      (GtkToggleAction * toggle_action,
                                    BalsaSendmsg * bsmsg);
#if !defined(ENABLE_TOUCH_UI)
static void gpg_mode_radio_cb(GtkRadioAction * action,
                              GtkRadioAction * current,
                              BalsaSendmsg * bsmsg);
#endif                          /* ENABLE_TOUCH_UI */
static void bsmsg_setup_gpg_ui(BalsaSendmsg *bsmsg);
static void bsmsg_update_gpg_ui_on_ident_change(BalsaSendmsg *bsmsg,
                                                LibBalsaIdentity *new_ident);
static void bsmsg_setup_gpg_ui_by_mode(BalsaSendmsg *bsmsg, gint mode);
#endif

#if defined(ENABLE_TOUCH_UI)
static gboolean bsmsg_check_format_compatibility(GtkWindow *parent,
                                                 const char *filename);
#endif /* ENABLE_TOUCH_UI */

#if !HAVE_GTKSPELL
static void spell_check_cb(GtkAction * action, BalsaSendmsg * bsmsg);
static void sw_spell_check_response(BalsaSpellCheck * spell_check,
                                    gint response, BalsaSendmsg * bsmsg);
#else
static void spell_check_menu_cb(GtkToggleAction * action,
                                BalsaSendmsg * bsmsg);
#endif                          /* HAVE_GTKSPELL */

static void address_book_cb(LibBalsaAddressView * address_view,
                            GtkTreeRowReference * row_ref,
                            BalsaSendmsg * bsmsg);
static void address_book_response(GtkWidget * ab, gint response,
                                  LibBalsaAddressView * address_view);

static void set_locale(BalsaSendmsg * bsmsg, gint idx);

#if !defined(ENABLE_TOUCH_UI)
static void edit_with_gnome(GtkAction * action, BalsaSendmsg* bsmsg);
#endif
static void change_identity_dialog_cb(GtkAction * action,
                                      BalsaSendmsg * bsmsg);
static void repl_identity_signature(BalsaSendmsg* bsmsg, 
                                    LibBalsaIdentity* new_ident,
                                    LibBalsaIdentity* old_ident,
                                    gint* replace_offset, gint siglen, 
                                    gchar* new_sig);
static void update_bsmsg_identity(BalsaSendmsg*, LibBalsaIdentity*);

static void sw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc);
static GString *quote_message_body(BalsaSendmsg * bsmsg,
                                   LibBalsaMessage * message,
                                   QuoteType type);
static void set_list_post_address(BalsaSendmsg * bsmsg);
static gboolean set_list_post_rfc2369(BalsaSendmsg * bsmsg,
                                      const gchar * url);
static const gchar *rfc2822_skip_comments(const gchar * str);
static void sendmsg_window_set_title(BalsaSendmsg * bsmsg);

#if !HAVE_GTKSOURCEVIEW
/* Undo/Redo buffer helpers. */
static void sw_buffer_save(BalsaSendmsg * bsmsg);
static void sw_buffer_swap(BalsaSendmsg * bsmsg, gboolean undo);
#endif                          /* HAVE_GTKSOURCEVIEW */
static void sw_buffer_signals_connect(BalsaSendmsg * bsmsg);
#if !HAVE_GTKSOURCEVIEW || !HAVE_GTKSPELL
static void sw_buffer_signals_disconnect(BalsaSendmsg * bsmsg);
#endif                          /* !HAVE_GTKSOURCEVIEW || !HAVE_GTKSPELL */
#if !HAVE_GTKSOURCEVIEW
static void sw_buffer_set_undo(BalsaSendmsg * bsmsg, gboolean undo,
			       gboolean redo);
#endif                          /* HAVE_GTKSOURCEVIEW */

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

static void sw_undo_cb         (GtkAction * action, BalsaSendmsg * bsmsg);
static void sw_redo_cb         (GtkAction * action, BalsaSendmsg * bsmsg);
static void cut_cb             (GtkAction * action, BalsaSendmsg * bsmsg);
static void copy_cb            (GtkAction * action, BalsaSendmsg * bsmsg);
static void paste_cb           (GtkAction * action, BalsaSendmsg * bsmsg);
static void select_all_cb      (GtkAction * action, BalsaSendmsg * bsmsg);
static void wrap_body_cb       (GtkAction * action, BalsaSendmsg * bsmsg);
static void reflow_selected_cb (GtkAction * action, BalsaSendmsg * bsmsg);
static void insert_signature_cb(GtkAction * action, BalsaSendmsg * bsmsg);
static void quote_messages_cb  (GtkAction * action, BalsaSendmsg * bsmsg);
static void lang_set_cb(GtkWidget *widget, BalsaSendmsg *bsmsg);

static void set_entry_to_subject(GtkEntry* entry, LibBalsaMessageBody *body,
                                 SendType p, LibBalsaIdentity* ident);

/* the array of locale names and charset names included in the MIME
   type information.  
   if you add a new encoding here add to SendCharset in libbalsa.c 
*/
struct SendLocales {
    const gchar *locale, *charset, *lang_name;
} locales[] = {
    /* Translators: please use the initial letter of each language as
     * its accelerator; this is a long list, and unique accelerators
     * cannot be found. */
    {"pt_BR", "ISO-8859-1",    N_("_Brazilian Portuguese")},
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
    {"de_AT", "ISO-8859-15",   N_("_German (Austrian)")},
    {"de_CH", "ISO-8859-1",    N_("_German (Swiss)")},
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

static const gchar *
sw_preferred_charset(BalsaSendmsg * bsmsg)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(locales); i++)
        if (bsmsg->spell_check_lang && locales[i].locale
            && strcmp(bsmsg->spell_check_lang, locales[i].locale) == 0)
            return locales[i].charset;

    return NULL;
}

/* ===================================================================
   Balsa menus. Touchpad has some simplified menus which do not
   overlap very much with the default balsa menus. They are here
   because they represent an alternative probably appealing to the all
   proponents of GNOME2 dumbify approach (OK, I am bit unfair here).
*/

static const GtkActionEntry entries[] = {
    {"FileMenu", NULL, N_("_File")},
    {"EditMenu", NULL, N_("_Edit")},
    {"ShowMenu", NULL, N_("_Show")},
    {"LanguageMenu", NULL, N_("_Language")},
    {"OptionsMenu", NULL, N_("_Options")},
#if defined(ENABLE_TOUCH_UI)
    {"ToolsMenu", NULL, N_("_Tools")},
    {"FileMoreMenu", NULL, N_("_More")},
    {"EditMoreMenu", NULL, N_("_More")},
    {"ToolsMoreMenu", NULL, N_("_More")},
#endif                          /* ENABLE_TOUCH_UI */
    {"IncludeFile", GTK_STOCK_OPEN, N_("_Include File..."), NULL,
     N_("Include a file"), G_CALLBACK(include_file_cb)},
    {"AttachFile", BALSA_PIXMAP_ATTACHMENT, N_("_Attach File..."), NULL,
     N_("Attach a file"), G_CALLBACK(attach_clicked)},
    {"IncludeMessages", NULL, N_("I_nclude Message(s)"), NULL,
     N_("Include selected message(s)"), G_CALLBACK(include_message_cb)},
    {"AttachMessages", NULL, N_("Attach _Message(s)"), NULL,
     N_("Attach selected message(s)"), G_CALLBACK(attach_message_cb)},
    {"Save", GTK_STOCK_SAVE, N_("_Save"), "<control>S",
     N_("Save this message"), G_CALLBACK(save_message_cb)},
#ifdef HAVE_GTK_PRINT
#if !defined(ENABLE_TOUCH_UI)
    {"PageSetup", NULL, N_("Page _Setup"), NULL,
     N_("Set up page for printing"), G_CALLBACK(page_setup_cb)},
#endif                          /* ENABLE_TOUCH_UI */
#endif                          /* HAVE_GTK_PRINT */
    {"Print", GTK_STOCK_PRINT, N_("_Print..."), "<control>P",
     N_("Print the edited message"), G_CALLBACK(print_message_cb)},
    {"Close", GTK_STOCK_CLOSE, N_("_Close"), "<control>W",
     NULL, G_CALLBACK(close_window_cb)},
    {"Undo", GTK_STOCK_UNDO, N_("_Undo"), "<control>Z",
     N_("Undo most recent change"), G_CALLBACK(sw_undo_cb)},
    {"Redo", GTK_STOCK_REDO, N_("_Redo"), "<shift><control>Z",
     N_("Redo most recent change"), G_CALLBACK(sw_redo_cb)},
    {"Cut", GTK_STOCK_CUT, N_("Cu_t"), "<control>X",
     N_("Cut the selected text"), G_CALLBACK(cut_cb)},
    {"Copy", GTK_STOCK_COPY, N_("_Copy"), "<control>C",
     N_("Copy to the clipboard"), G_CALLBACK(copy_cb)},
    {"Paste", GTK_STOCK_PASTE, N_("_Paste"), "<control>V",
     N_("Paste from the clipboard"), G_CALLBACK(paste_cb)},
    {"SelectAll", NULL, N_("Select _All"), "<control>A",
     NULL, G_CALLBACK(select_all_cb)},
    {"WrapBody", NULL, N_("_Wrap Body"), "<control>B",
     N_("Wrap message lines"), G_CALLBACK(wrap_body_cb)},
    {"Reflow", NULL, N_("_Reflow Selected Text"), "<control>R",
     NULL, G_CALLBACK(reflow_selected_cb)},
    {"InsertSignature", NULL, N_("Insert Si_gnature"), "<control>G",
     NULL, G_CALLBACK(insert_signature_cb)},
    {"QuoteMessages", NULL, N_("_Quote Message(s)"), NULL,
     NULL, G_CALLBACK(quote_messages_cb)},
#if !HAVE_GTKSPELL
    {"CheckSpelling", GTK_STOCK_SPELL_CHECK, N_("C_heck Spelling"), NULL,
     N_("Check the spelling of the message"),
     G_CALLBACK(spell_check_cb)},
#endif                          /* HAVE_GTKSPELL */
    {"SelectIdentity", BALSA_PIXMAP_IDENTITY, N_("Select _Identity..."),
     NULL, N_("Select the Identity to use for the message"),
     G_CALLBACK(change_identity_dialog_cb)},
#if !defined(ENABLE_TOUCH_UI)
    {"EditWithGnome", GTK_STOCK_EXECUTE, N_("_Edit with Gnome-Editor"),
     NULL, N_("Edit the current message with the default Gnome editor"),
     G_CALLBACK(edit_with_gnome)},
#endif                          /* ENABLE_TOUCH_UI */
};

/* Actions that are sensitive only when the message is ready to send */
static const GtkActionEntry ready_entries[] = {
    /* All three "Send" and "Queue" actions have the same
     * stock_id; the first in this list defines the action tied to the
     * toolbar's "Send" button, so "ToolbarSend" must come before
     * the others. */
    {"ToolbarSend", BALSA_PIXMAP_SEND, N_("Sen_d"), "<control>Return",
     N_("Send this message"), G_CALLBACK(toolbar_send_message_cb)},
    {"Send", BALSA_PIXMAP_SEND, N_("Sen_d"), "<control>Return",
     N_("Send this message"), G_CALLBACK(send_message_cb)},
#if !defined(ENABLE_TOUCH_UI)
    {"Queue", BALSA_PIXMAP_SEND, N_("_Queue"), "<control>Q",
     N_("Queue this message in Outbox for sending"),
     G_CALLBACK(queue_message_cb)},
    {"Postpone", BALSA_PIXMAP_POSTPONE, N_("_Postpone"), NULL,
     N_("Save this message and close"), G_CALLBACK(postpone_message_cb)},
#else                           /* ENABLE_TOUCH_UI */
    {"Queue", BALSA_PIXMAP_SEND, N_("Send _Later"), "<control>Q",
     N_("Queue this message in Outbox for sending"),
     G_CALLBACK(queue_message_cb)},
    {"Postpone", BALSA_PIXMAP_POSTPONE, N_("Sa_ve and Close"), NULL,
     NULL, G_CALLBACK(postpone_message_cb)},
#endif                          /* ENABLE_TOUCH_UI */
};

/* Toggle items */
static const GtkToggleActionEntry toggle_entries[] = {
#if HAVE_GTKSPELL
    {"CheckSpelling", GTK_STOCK_SPELL_CHECK, N_("C_heck Spelling"), NULL,
     N_("Check the spelling of the message"),
     G_CALLBACK(spell_check_menu_cb), FALSE},
#endif                          /* HAVE_GTKSPELL */
    {"From", NULL, N_("F_rom"), NULL, NULL,
     G_CALLBACK(toggle_from_cb), TRUE},
    {"Recipients", NULL, N_("Rec_ipients"), NULL, NULL,
     G_CALLBACK(toggle_recipients_cb), TRUE},
    {"ReplyTo", NULL, N_("R_eply To"), NULL, NULL,
     G_CALLBACK(toggle_replyto_cb), TRUE},
    {"Fcc", NULL, N_("F_cc"), NULL, NULL,
     G_CALLBACK(toggle_fcc_cb), TRUE},
    {"RequestMDN", NULL, N_("_Request Disposition Notification"), NULL,
     NULL, G_CALLBACK(toggle_reqdispnotify_cb), FALSE},
    {"Flowed", NULL, N_("_Format = Flowed"), NULL,
     NULL, G_CALLBACK(toggle_format_cb), FALSE},
#ifdef HAVE_GPGME
#if !defined(ENABLE_TOUCH_UI)
    {"SignMessage", BALSA_PIXMAP_GPG_SIGN, N_("_Sign Message"), NULL,
     N_("Sign message using GPG"), G_CALLBACK(toggle_sign_cb), FALSE},
    {"EncryptMessage", BALSA_PIXMAP_GPG_ENCRYPT, N_("_Encrypt Message"),
     NULL, N_("Encrypt message using GPG"), G_CALLBACK(toggle_encrypt_cb),
     FALSE},
#else                           /* ENABLE_TOUCH_UI */
    {"SignMessage", BALSA_PIXMAP_GPG_SIGN, N_("_Sign Message"), NULL,
     N_("signs the message using GnuPG"),
     G_CALLBACK(toggle_sign_cb), FALSE},
    {"EncryptMessage", BALSA_PIXMAP_GPG_ENCRYPT, N_("_Encrypt Message"),
     NULL,
     N_("signs the message using GnuPG for all To: and CC: recipients"),
     G_CALLBACK(toggle_encrypt_cb), FALSE},
#endif                          /* ENABLE_TOUCH_UI */
#endif                          /* HAVE_GPGME */
};

#if !defined(ENABLE_TOUCH_UI)
/* Radio items */
#ifdef HAVE_GPGME
static const GtkRadioActionEntry gpg_mode_radio_entries[] = {
    {"MimeMode", NULL, N_("_GnuPG uses MIME mode"),
     NULL, NULL, LIBBALSA_PROTECT_RFC3156},
    {"OldOpenPgpMode", NULL, N_("_GnuPG uses old OpenPGP mode"),
     NULL, NULL, LIBBALSA_PROTECT_OPENPGP},
#ifdef HAVE_SMIME
    {"SMimeMode", NULL, N_("_S/MIME mode (GpgSM)"),
     NULL, NULL, LIBBALSA_PROTECT_SMIMEV3}
#endif                          /* HAVE_SMIME */
};
#endif                          /* HAVE_GPGME */
#endif                          /* ENABLE_TOUCH_UI */

static const char *ui_description =
#if !defined(ENABLE_TOUCH_UI)
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='IncludeFile'/>"
"      <menuitem action='AttachFile'/>"
"      <menuitem action='IncludeMessages'/>"
"      <menuitem action='AttachMessages'/>"
"      <separator/>"
"      <menuitem action='Send'/>"
"      <menuitem action='Queue'/>"
"      <menuitem action='Postpone'/>"
"      <menuitem action='Save'/>"
"      <separator/>"
#ifdef HAVE_GTK_PRINT
"      <menuitem action='PageSetup'/>"
#endif                          /* HAVE_GTK_PRINT */
"      <menuitem action='Print'/>"
"      <separator/>"
"      <menuitem action='Close'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='Undo'/>"
"      <menuitem action='Redo'/>"
"      <separator/>"
"      <menuitem action='Cut'/>"
"      <menuitem action='Copy'/>"
"      <menuitem action='Paste'/>"
"      <menuitem action='SelectAll'/>"
"      <separator/>"
"      <menuitem action='WrapBody'/>"
"      <menuitem action='Reflow'/>"
"      <separator/>"
"      <menuitem action='InsertSignature'/>"
"      <menuitem action='QuoteMessages'/>"
"      <separator/>"
"      <menuitem action='CheckSpelling'/>"
"      <separator/>"
"      <menuitem action='SelectIdentity'/>"
"      <separator/>"
"      <menuitem action='EditWithGnome'/>"
"    </menu>"
"    <menu action='ShowMenu'>"
"      <menuitem action='From'/>"
"      <menuitem action='Recipients'/>"
"      <menuitem action='ReplyTo'/>"
"      <menuitem action='Fcc'/>"
"    </menu>"
"    <menu action='LanguageMenu'>"
"    </menu>"
"    <menu action='OptionsMenu'>"
"      <menuitem action='RequestMDN'/>"
"      <menuitem action='Flowed'/>"
#ifdef HAVE_GPGME
"      <separator/>"
"      <menuitem action='SignMessage'/>"
"      <menuitem action='EncryptMessage'/>"
"      <menuitem action='MimeMode'/>"
"      <menuitem action='OldOpenPgpMode'/>"
#ifdef HAVE_SMIME
"      <menuitem action='SMimeMode'/>"
#endif                          /* HAVE_SMIME */
#endif                          /* HAVE_GPGME */
"    </menu>"
"  </menubar>"
"  <toolbar name='Toolbar'>"
"  </toolbar>"
"</ui>";
#else                           /* ENABLE_TOUCH_UI */
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='AttachFile'/>"
"      <separator/>"
"      <menuitem action='Save'/>"
"      <menuitem action='Print'/>"
"      <menu action='FileMoreMenu'>"
"        <menuitem action='IncludeFile'/>"
"        <menuitem action='IncludeMessages'/>"
"        <menuitem action='AttachMessages'/>"
"      </menu>"
"      <menuitem action='Postpone'/>"
"      <separator/>"
"      <menuitem action='Send'/>"
"      <menuitem action='Queue'/>"
"      <separator/>"
"      <menuitem action='Close'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='Undo'/>"
"      <menuitem action='Redo'/>"
"      <separator/>"
"      <menuitem action='Cut'/>"
"      <menuitem action='Copy'/>"
"      <menuitem action='Paste'/>"
"      <menuitem action='SelectAll'/>"
"      <separator/>"
"      <separator/>"
"      <menuitem action='InsertSignature'/>"
"      <menu action='EditMoreMenu'>"
"        <menuitem action='WrapBody'/>"
"        <menuitem action='Reflow'/>"
"        <separator/>"
"        <menuitem action='QuoteMessages'/>"
"      </menu>"
"    </menu>"
"    <menu action='ShowMenu'>"
"      <menuitem action='From'/>"
"      <menuitem action='Addresses'/>"
"      <menuitem action='Fcc'/>"
"    </menu>"
"    <menu action='ToolsMenu'>"
"      <menuitem action='CheckSpelling'/>"
"      <menu action='LanguageMenu'>"
"      </menu>"
"      <separator/>"
"      <menuitem action='SelectIdentity'/>"
"      <menuitem action='RequestMDN'/>"
"      <menu action='ToolsMoreMenu'>"
"        <menuitem action='Flowed'/>"
#ifdef HAVE_GPGME
"        <separator/>"
"        <menuitem action='SignMessage'/>"
"        <menuitem action='EncryptMessage'/>"
#endif                          /* HAVE_GPGME */
"      </menu>"
"    </menu>"
"  </menubar>"
"  <toolbar name='Toolbar'>"
"  </toolbar>"
"</ui>";
#endif                          /* ENABLE_TOUCH_UI */

/* Create a GtkUIManager for a compose window, with all the actions, but
 * no ui.
 */
static GtkUIManager *
sw_get_ui_manager(BalsaSendmsg * bsmsg)
{
    GtkUIManager *ui_manager;
    GtkActionGroup *action_group;

    ui_manager = gtk_ui_manager_new();

    action_group = gtk_action_group_new("ComposeWindow");
    gtk_action_group_set_translation_domain(action_group, NULL);
    if (bsmsg)
        bsmsg->action_group = action_group;
    gtk_action_group_add_actions(action_group, entries,
                                 G_N_ELEMENTS(entries), bsmsg);
    gtk_action_group_add_toggle_actions(action_group, toggle_entries,
                                        G_N_ELEMENTS(toggle_entries),
                                        bsmsg);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

    action_group = gtk_action_group_new("ComposeWindow");
    gtk_action_group_set_translation_domain(action_group, NULL);
    if (bsmsg)
        bsmsg->ready_action_group = action_group;
    gtk_action_group_add_actions(action_group, ready_entries,
                                 G_N_ELEMENTS(ready_entries), bsmsg);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

#ifdef HAVE_GPGME
#if !defined(ENABLE_TOUCH_UI)
    action_group = gtk_action_group_new("ComposeWindow");
    gtk_action_group_set_translation_domain(action_group, NULL);
    if (bsmsg)
        bsmsg->gpg_action_group = action_group;
    gtk_action_group_add_radio_actions(action_group,
                                       gpg_mode_radio_entries,
                                       G_N_ELEMENTS
                                       (gpg_mode_radio_entries), 0,
                                       G_CALLBACK(gpg_mode_radio_cb),
                                       bsmsg);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);
#endif                          /* ENABLE_TOUCH_UI */
#endif                          /* HAVE_GPGME */

    return ui_manager;
}

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

static const gchar * const attach_modes[] =
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
#define BALSA_SENDMSG_ROW_REF_KEY      "balsa-sendmsg-row-ref"
static void
address_book_cb(LibBalsaAddressView * address_view,
                GtkTreeRowReference * row_ref, 
                BalsaSendmsg * bsmsg)
{
    GtkWidget *ab;
    GtkTreeRowReference *row_ref_copy;

    /* Show only one dialog per window. */
    ab = g_object_get_data(G_OBJECT(bsmsg->window),
                           BALSA_SENDMSG_ADDRESS_BOOK_KEY);
    if (ab) {
        gdk_window_raise(ab->window);
        return;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(address_view), FALSE);

    ab = balsa_ab_window_new(TRUE, GTK_WINDOW(bsmsg->window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(ab), TRUE);
    g_signal_connect(G_OBJECT(ab), "response",
                     G_CALLBACK(address_book_response), address_view);
    row_ref_copy = gtk_tree_row_reference_copy(row_ref);
    g_object_set_data_full(G_OBJECT(ab), BALSA_SENDMSG_ROW_REF_KEY,
                           row_ref_copy,
                           (GDestroyNotify) gtk_tree_row_reference_free);
    g_object_set_data(G_OBJECT(bsmsg->window),
                      BALSA_SENDMSG_ADDRESS_BOOK_KEY, ab);
    gtk_widget_show_all(ab);
}

/* Callback for the "response" signal for the address book dialog. */
static void
address_book_response(GtkWidget * ab, gint response,
                      LibBalsaAddressView * address_view)
{
    GtkWindow *parent = gtk_window_get_transient_for(GTK_WINDOW(ab));
    GtkTreeRowReference *row_ref =
        g_object_get_data(G_OBJECT(ab), BALSA_SENDMSG_ROW_REF_KEY);

    if (response == GTK_RESPONSE_OK) {
        gchar *t = balsa_ab_window_get_recipients(BALSA_AB_WINDOW(ab));
        libbalsa_address_view_add_to_row(address_view, row_ref, t);
        g_free(t);
    }

    gtk_widget_destroy(ab);
    g_object_set_data(G_OBJECT(parent), BALSA_SENDMSG_ADDRESS_BOOK_KEY,
                      NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(address_view), TRUE);
}

static void
sw_delete_draft(BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message = bsmsg->draft_message;
    if (message && message->mailbox && !message->mailbox->readonly)
        libbalsa_message_change_flags(message,
                                      LIBBALSA_MESSAGE_FLAG_DELETED, 0);
}

static gint
delete_handler(BalsaSendmsg * bsmsg)
{
    InternetAddressList *l =
        libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");
    const gchar *tmp = l && l->address && l->address->name ?
        l->address->name : _("(No name)");
    gint reply;
    GtkWidget *d;

    if (balsa_app.debug)
        printf("delete_event_cb\n");

    if (bsmsg->state == SENDMSG_STATE_CLEAN)
        return FALSE;

    d = gtk_message_dialog_new(GTK_WINDOW(bsmsg->window),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("The message to '%s' is modified.\n"
                                 "Save message to Draftbox?"), tmp);
    internet_address_list_destroy(l);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_YES);
    gtk_dialog_add_button(GTK_DIALOG(d),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    reply = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);

    switch (reply) {
    case GTK_RESPONSE_YES:
        if (bsmsg->state == SENDMSG_STATE_MODIFIED)
            if (!message_postpone(bsmsg))
                return TRUE;
        break;
    case GTK_RESPONSE_NO:
        if (bsmsg->type != SEND_CONTINUE)
            sw_delete_draft(bsmsg);
        break;
    default:
        return TRUE;
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
close_window_cb(GtkAction * action, gpointer data)
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

    g_signal_handler_disconnect(G_OBJECT(balsa_app.main_window),
                                bsmsg->delete_sig_id);
    g_signal_handler_disconnect(G_OBJECT(balsa_app.main_window),
                                bsmsg->identities_changed_id);
    if(balsa_app.debug) g_message("balsa_sendmsg_destroy()_handler: Start.");

    if (bsmsg->parent_message) {
	if (bsmsg->parent_message->mailbox)
	    libbalsa_mailbox_close(bsmsg->parent_message->mailbox,
		    /* Respect pref setting: */
				   balsa_app.expunge_on_close);
	g_object_unref(G_OBJECT(bsmsg->parent_message));
        bsmsg->parent_message = NULL;
    }

    if (bsmsg->draft_message) {
	if (bsmsg->draft_message->mailbox)
	    libbalsa_mailbox_close(bsmsg->draft_message->mailbox,
		    /* Respect pref setting: */
				   balsa_app.expunge_on_close);
	g_object_unref(G_OBJECT(bsmsg->draft_message));
        bsmsg->draft_message = NULL;
    }

    if (balsa_app.debug)
	printf("balsa_sendmsg_destroy_handler: Freeing bsmsg\n");
    gtk_widget_destroy(bsmsg->window);
    if (bsmsg->bad_address_style)
        g_object_unref(G_OBJECT(bsmsg->bad_address_style));
    quit_on_close = bsmsg->quit_on_close;
    g_free(bsmsg->fcc_url);
    g_free(bsmsg->in_reply_to);
    if(bsmsg->references) {
        g_list_foreach(bsmsg->references, (GFunc) g_free, NULL);
        g_list_free(bsmsg->references);
        bsmsg->references = NULL;
    }

#if !HAVE_GTKSPELL
    if (bsmsg->spell_checker)
        gtk_widget_destroy(bsmsg->spell_checker);
#endif                          /* HAVE_GTKSPELL */
    if (bsmsg->wrap_timeout_id) {
        g_source_remove(bsmsg->wrap_timeout_id);
        bsmsg->wrap_timeout_id = 0;
    }
    if (bsmsg->autosave_timeout_id) {
        g_source_remove(bsmsg->autosave_timeout_id);
        bsmsg->autosave_timeout_id = 0;
    }

#if !HAVE_GTKSOURCEVIEW
    g_object_unref(bsmsg->buffer2);
#endif                          /* HAVE_GTKSOURCEVIEW */

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

static void
sw_buffer_signals_block(BalsaSendmsg * bsmsg, GtkTextBuffer * buffer)
{
    g_signal_handler_block(buffer, bsmsg->changed_sig_id);
#if !HAVE_GTKSOURCEVIEW
    g_signal_handler_block(buffer, bsmsg->delete_range_sig_id);
#endif                          /* HAVE_GTKSOURCEVIEW */
    g_signal_handler_block(buffer, bsmsg->insert_text_sig_id);
}

static void
sw_buffer_signals_unblock(BalsaSendmsg * bsmsg, GtkTextBuffer * buffer)
{
    g_signal_handler_unblock(buffer, bsmsg->changed_sig_id);
#if !HAVE_GTKSOURCEVIEW
    g_signal_handler_unblock(buffer, bsmsg->delete_range_sig_id);
#endif                          /* HAVE_GTKSOURCEVIEW */
    g_signal_handler_unblock(buffer, bsmsg->insert_text_sig_id);
}

static const gchar *const address_types[] =
    { N_("To:"), N_("Cc:"), N_("Bcc:") };

#if !defined(ENABLE_TOUCH_UI)
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
            guint type;

            if (line[strlen(line) - 1] == '\n')
                line[strlen(line) - 1] = '\0';

            if (libbalsa_str_has_prefix(line, _("Subject:")) == 0) {
                gtk_entry_set_text(GTK_ENTRY(data_real->bsmsg->subject[1]),
                                   line + strlen(_("Subject:")) + 1);
                continue;
            }

            for (type = 0;
                 type < G_N_ELEMENTS(address_types);
                 type++) {
                const gchar *type_string = _(address_types[type]);
                if (libbalsa_str_has_prefix(line, type_string))
                    libbalsa_address_view_set_from_string
                        (data_real->bsmsg->recipient_view, 
                         address_types[type],
                         line + strlen(type_string) + 1);
            }
        }
    }
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data_real->bsmsg->text));

#if !HAVE_GTKSOURCEVIEW
    sw_buffer_save(data_real->bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */
    sw_buffer_signals_block(data_real->bsmsg, buffer);
    gtk_text_buffer_set_text(buffer, "", 0);
    curposition = 0;
    while(fgets(line, sizeof(line), tmp))
        libbalsa_insert_with_url(buffer, line, NULL, NULL, NULL);
    sw_buffer_signals_unblock(data_real->bsmsg, buffer);

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
edit_with_gnome(GtkAction * action, BalsaSendmsg* bsmsg)
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


    tmp = fdopen(tmpfd, "w+");
    
    if(balsa_app.edit_headers) {
        guint type;

        fprintf(tmp, "%s %s\n", _("Subject:"),
                gtk_entry_get_text(GTK_ENTRY(bsmsg->subject[1])));
        for (type = 0; type < G_N_ELEMENTS(address_types); type++) {
            InternetAddressList *list =
                libbalsa_address_view_get_list(bsmsg->recipient_view,
                                               address_types[type]);
            gchar *p = internet_address_list_to_string(list, FALSE);
            internet_address_list_destroy(list);
            fprintf(tmp, "%s %s\n", _(address_types[type]), p);
            g_free(p);
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
change_identity_dialog_cb(GtkAction * action, BalsaSendmsg* bsmsg)
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

/*
 * GtkAction helpers
 */

static GtkAction *
sw_get_action(BalsaSendmsg * bsmsg, const gchar * action_name)
{
    GtkAction *action =
        gtk_action_group_get_action(bsmsg->action_group, action_name);

#ifdef HAVE_GPGME
#if !defined(ENABLE_TOUCH_UI)
    if (!action)
        action =
            gtk_action_group_get_action(bsmsg->gpg_action_group,
                                        action_name);
#endif                          /* ENABLE_TOUCH_UI */
#endif                          /* HAVE_GPGME */

    return action;
}

static void
sw_set_sensitive(BalsaSendmsg * bsmsg, const gchar * action_name,
                 gboolean sensitive)
{
    GtkAction *action = sw_get_action(bsmsg, action_name);
    gtk_action_set_sensitive(action, sensitive);
}

#if !HAVE_GTKSOURCEVIEW
static gboolean
sw_get_sensitive(BalsaSendmsg * bsmsg, const gchar * action_name)
{
    GtkAction *action = sw_get_action(bsmsg, action_name);
    return gtk_action_get_sensitive(action);
}
#endif                          /* HAVE_GTKSOURCEVIEW */

/* Set the state of a GtkToggleAction.
 * Note: most calls expect the corresponding action to be taken, so we
 * do not block any handlers.
 */
static void
sw_set_active(BalsaSendmsg * bsmsg, const gchar * action_name,
              gboolean active)
{
    GtkAction *action = sw_get_action(bsmsg, action_name);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), active);
}

static gboolean
sw_get_active(BalsaSendmsg * bsmsg, const gchar * action_name)
{
    GtkAction *action = sw_get_action(bsmsg, action_name);
    return gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action));
}

/*
 * end of GtkAction helpers
 */

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
    gboolean reply_type = (bsmsg->type == SEND_REPLY || 
                           bsmsg->type == SEND_REPLY_ALL ||
                           bsmsg->type == SEND_REPLY_GROUP);
    gboolean forward_type = (bsmsg->type == SEND_FORWARD_ATTACH || 
                             bsmsg->type == SEND_FORWARD_INLINE);
    
    g_return_if_fail(ident != NULL);


    /* change entries to reflect new identity */
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsmsg->from[1]),
                             g_list_index(balsa_app.identities, ident));

#if !defined(ENABLE_TOUCH_UI)
    if (ident->replyto && *ident->replyto) {
        libbalsa_address_view_set_from_string(bsmsg->replyto_view,
                                              "Reply To:",
                                              ident->replyto);
        gtk_widget_show(bsmsg->replyto[0]);
        gtk_widget_show(bsmsg->replyto[1]);
    } else if (!sw_get_active(bsmsg, "ReplyTo")) {
        gtk_widget_hide(bsmsg->replyto[0]);
        gtk_widget_hide(bsmsg->replyto[1]);
    }
#endif

    if (bsmsg->ident->bcc) {
        InternetAddressList *l,
                            *old_ident_list, *new_ident_list,
                            *old_list, *new_list = NULL;

        /* Copy the old list of Bcc addresses, omitting any that came
         * from the old identity: */
        old_ident_list = internet_address_parse_string(bsmsg->ident->bcc);
        old_list =
            libbalsa_address_view_get_list(bsmsg->recipient_view,
                                           "Bcc:");
        for (l = old_list; l; l = l->next) {
            InternetAddress *ia = l->address;
            InternetAddressList *m;

            for (m = old_ident_list; m; m = m->next)
                if (libbalsa_ia_rfc2821_equal(ia, m->address))
                    break;
            if (!m)     /* We didn't find this address. */
                new_list = internet_address_list_append(new_list, ia);
        }
        internet_address_list_destroy(old_list);
        internet_address_list_destroy(old_ident_list);

        /* Add the new Bcc addresses, if any: */
        new_ident_list = internet_address_parse_string(ident->bcc);
        new_list = internet_address_list_concat(new_list, new_ident_list);
        internet_address_list_destroy(new_ident_list);

        /* Set the resulting list: */
        libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                            "Bcc:",
                                            new_list);
        internet_address_list_destroy(new_list);
    }
    
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
    } else {
        if ( (replen == 0 && reply_type) ||
             (fwdlen == 0 && forward_type) ) {
            LibBalsaMessage *msg = bsmsg->parent_message 
            ? bsmsg->parent_message : bsmsg->draft_message;
        set_entry_to_subject(GTK_ENTRY(bsmsg->subject[1]),
                             msg->body_list, bsmsg->type, ident);
        }
    }

    /* -----------------------------------------------------------
     * remove/add the signature depending on the new settings, change
     * the signature if path changed */

    /* reconstruct the old signature to search with */
    old_sig = libbalsa_identity_get_signature(old_ident,
                                              GTK_WINDOW(bsmsg->window));

    /* switch identities in bsmsg here so we can use read_signature
     * again */
    bsmsg->ident = ident;
    if ( (reply_type && ident->sig_whenreply)
         || (forward_type && ident->sig_whenforward)
         || (bsmsg->type == SEND_NORMAL && ident->sig_sending))
        new_sig = libbalsa_identity_get_signature(ident,
                                                  GTK_WINDOW(bsmsg->window));
    else
        new_sig = NULL;
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
        /* if no sig seperators found, do a slower brute force
         * approach.  We could have stopped earlier if the message was
         * empty, but we didn't. Now, it is really time to do
         * that... */
        if (*message_text && !found_sig) {
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

    libbalsa_address_view_set_domain(bsmsg->recipient_view, ident->domain);

    sw_set_active(bsmsg, "RequestMDN", ident->request_mdn);
}


static void
sw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc)
{
    if (!GTK_WIDGET_REALIZED(window))
        return;

    if (!(balsa_app.sw_maximized = gdk_window_get_state(window->window)
          & GDK_WINDOW_STATE_MAXIMIZED)) {
        balsa_app.sw_height = alloc->height;
        balsa_app.sw_width  = alloc->width;
    }
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
#if HAVE_GMIME_2_2_5
	    headers->subject = g_mime_utils_header_decode_text(subject);
#else  /* HAVE_GMIME_2_2_5 */
	    headers->subject =
		g_mime_utils_header_decode_text((guchar *) subject);
#endif /* HAVE_GMIME_2_2_5 */
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
        balsa_information(LIBBALSA_INFORMATION_ERROR, "%s", err_bsmsg);
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
	    g_object_unref(attach_data);
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
	    gtk_menu_item_new_with_label(_(attach_modes
                                           [LIBBALSA_ATTACH_AS_INLINE]));
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
	    gtk_menu_item_new_with_label(_(attach_modes
                                           [LIBBALSA_ATTACH_AS_ATTACHMENT]));
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
	    gtk_menu_item_new_with_label(_(attach_modes
                                           [LIBBALSA_ATTACH_AS_EXTBODY]));
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
		       ATTACH_SIZE_COLUMN, (gfloat) attach_stat.st_size,
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

    g_object_set_data(G_OBJECT(bsmsg->window),
                      "balsa-sendmsg-window-attach-dialog", NULL);

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

static GtkFileChooser *
sw_attach_dialog(BalsaSendmsg * bsmsg)
{
    GtkWidget *fsw;
    GtkFileChooser *fc;

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

    return fc;
}

/* attach_clicked - menu callback */
static void
attach_clicked(GtkAction * action, gpointer data)
{
    sw_attach_dialog((BalsaSendmsg *) data);
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

static void
insert_selected_messages(BalsaSendmsg *bsmsg, QuoteType type)
{
    GtkTextBuffer *buffer = 
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkWidget *index =
	balsa_window_find_current_index(balsa_app.main_window);
    GList *l;
    
    if (index && (l = balsa_index_selected_list(BALSA_INDEX(index)))) {
	GList *node;
    
	for (node = l; node; node = g_list_next(node)) {
	    LibBalsaMessage *message = node->data;
            GString *body = quote_message_body(bsmsg, message, type);
            libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
            g_string_free(body, TRUE);
	}
	g_list_foreach(l, (GFunc)g_object_unref, NULL);
        g_list_free(l);
    }
}

static void
include_message_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    insert_selected_messages(bsmsg, QUOTE_ALL);
}


static void
attach_message_cb(GtkAction * action, BalsaSendmsg *bsmsg) 
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
}


#if 0
static gint include_messages_cb(GtkWidget *widget, BalsaSendmsg *bsmsg)
{
    return insert_selected_messages(bsmsg, TRUE);
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
        GArray *selected = balsa_index_selected_msgnos_new(index);
	guint i;
        
        for (i = 0; i < selected->len; i++) {
	    guint msgno = g_array_index(selected, guint, i);
	    LibBalsaMessage *message =
		libbalsa_mailbox_get_message(mailbox, msgno);
            if (!message)
                continue;

            if(!attach_message(bsmsg, message))
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Attaching message failed.\n"
                                       "Possible reason: not enough temporary space"));
	    g_object_unref(message);
        }
        balsa_index_selected_msgnos_free(index, selected);
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
#if 0 /* FIXME */
    append_comma_separated(GTK_EDITABLE(widget),
	                   (gchar *) selection_data->data);
#endif
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
    GtkWidget *mnemonic_widget;

    mnemonic_widget = arr[1];
    if (GTK_IS_FRAME(mnemonic_widget))
        mnemonic_widget = gtk_bin_get_child(GTK_BIN(mnemonic_widget));
    arr[0] = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(arr[0]), mnemonic_widget);
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
 * Creates a gtk_label()/libbalsa_address_view() and button in a table for
 * e-mail entries, eg. To:.  It also sets up some callbacks in gtk.
 *
 * Input:  GtkWidget *table   - table to insert the widgets into.
 *         int y_pos          - How far down in the table to put label.
 *         BalsaSendmsg *bsmsg  - The send message window
 * On return, bsmsg->address_view and bsmsg->addresses[1] have been set.
 */

static void
sw_scroll_size_request(GtkWidget * widget, GtkRequisition * requisition)
{
    gint focus_width;
    gint focus_pad;
    gint border_width;
    GtkPolicyType type = GTK_POLICY_NEVER;

    gtk_widget_size_request(GTK_BIN(widget)->child, requisition);
    gtk_widget_style_get(widget, "focus-line-width", &focus_width,
                         "focus-padding", &focus_pad, NULL);

    border_width =
        (GTK_CONTAINER(widget)->border_width + focus_width +
         focus_pad) * 2;
    requisition->width += border_width;
    requisition->height += border_width;
    if (requisition->width > balsa_app.sw_width * 3 / 4) {
        requisition->width = balsa_app.sw_width * 3 / 4;
        type = GTK_POLICY_AUTOMATIC;
        requisition->height += 50;
    }
    if (requisition->height > 100) {
        requisition->height = 100;
        type = GTK_POLICY_AUTOMATIC;
        requisition->width += 50;
    }
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget), type, type);
}

static void
create_email_entry(GtkWidget * table, int y_pos, BalsaSendmsg * bsmsg,
                   LibBalsaAddressView ** view, GtkWidget ** widget,
                   const gchar * label, const gchar * const *types,
                   guint n_types)
{
    GtkWidget *scroll;

    *view = libbalsa_address_view_new(types, n_types,
                                      balsa_app.address_book_list,
                                      balsa_app.convert_unknown_8bit);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    g_signal_connect(scroll, "size-request",
                     G_CALLBACK(sw_scroll_size_request), NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(*view));

    widget[1] = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(widget[1]), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(widget[1]), scroll);

    create_email_or_string_entry(table, _(label), y_pos, widget);

    g_signal_connect(*view, "drag_data_received",
                     G_CALLBACK(to_add), NULL);
    g_signal_connect(*view, "open-address-book",
		     G_CALLBACK(address_book_cb), bsmsg);
    gtk_drag_dest_set(GTK_WIDGET(*view), GTK_DEST_DEFAULT_ALL,
		      email_field_drop_types,
		      ELEMENTS(email_field_drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    libbalsa_address_view_set_domain(*view, bsmsg->ident->domain);
    g_signal_connect_swapped(gtk_tree_view_get_model(GTK_TREE_VIEW(*view)),
                             "row-changed", G_CALLBACK(check_readiness),
                             bsmsg);
    g_signal_connect_swapped(gtk_tree_view_get_model(GTK_TREE_VIEW(*view)),
                             "row-deleted", G_CALLBACK(check_readiness),
                             bsmsg);
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
    g_object_set(cell, "text", _(attach_modes[mode]), NULL);
}


static void
render_attach_size(GtkTreeViewColumn *column, GtkCellRenderer *cell,
		   GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gint mode;
    gfloat size;
    gchar *sstr;

    gtk_tree_model_get(model, iter, ATTACH_MODE_COLUMN, &mode,
		       ATTACH_SIZE_COLUMN, &size, -1);
    if (mode == LIBBALSA_ATTACH_AS_EXTBODY)
        sstr = g_strdup("-");
    else if (size > 1.2e6)
	sstr = g_strdup_printf("%.2fMB", size / (1024 * 1024));
    else if (size > 1.2e3)
	sstr = g_strdup_printf("%.2fkB", size / 1024);
    else
	sstr = g_strdup_printf("%dB", (gint) size);
    g_object_set(cell, "text", sstr, NULL);
    g_free(sstr);
}


/* create_info_pane 
   creates upper panel with the message headers: From, To, ... and 
   returns it.
*/
static GtkWidget *
create_info_pane(BalsaSendmsg * bsmsg)
{
    guint row = 0;
    GtkWidget *sw;
    GtkWidget *table;
    GtkWidget *frame;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeView *view;
    GtkTreeViewColumn *column;

    bsmsg->header_table = table = gtk_table_new(5, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 6);

    /* bsmsg->bad_address_style will be set in create_email_entry: */
    bsmsg->bad_address_style = NULL;

    /* From: */
    create_from_entry(table, bsmsg);

    /* To:, Cc:, and Bcc: */
    create_email_entry(table, ++row, bsmsg, &bsmsg->recipient_view,
                       bsmsg->recipients, "Rec_ipients", address_types,
                       G_N_ELEMENTS(address_types));
    g_signal_connect_swapped(gtk_tree_view_get_model
                             (GTK_TREE_VIEW(bsmsg->recipient_view)),
                             "row-changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);
    g_signal_connect_swapped(gtk_tree_view_get_model
                             (GTK_TREE_VIEW(bsmsg->recipient_view)),
                             "row-deleted",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);

    /* Subject: */
    create_string_entry(table, _("S_ubject:"), ++row, bsmsg->subject);
    g_signal_connect_swapped(G_OBJECT(bsmsg->subject[1]), "changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);

#if !defined(ENABLE_TOUCH_UI)
    /* Reply To: */
    create_email_entry(table, ++row, bsmsg, &bsmsg->replyto_view,
                       bsmsg->replyto, "R_eply To:", NULL, 0);
#endif

    /* fcc: mailbox folder where the message copy will be written to */
    bsmsg->fcc[0] = gtk_label_new_with_mnemonic(_("F_cc:"));
    gtk_misc_set_alignment(GTK_MISC(bsmsg->fcc[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(bsmsg->fcc[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    ++row;
    gtk_table_attach(GTK_TABLE(table), bsmsg->fcc[0], 0, 1, row, row + 1,
                     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

    if (!balsa_app.fcc_mru)
        balsa_mblist_mru_add(&balsa_app.fcc_mru, balsa_app.sentbox->url);
    balsa_mblist_mru_add(&balsa_app.fcc_mru, "");
    if (balsa_app.copy_to_sentbox) {
        /* move the NULL option to the bottom */
        balsa_app.fcc_mru = g_list_reverse(balsa_app.fcc_mru);
        balsa_mblist_mru_add(&balsa_app.fcc_mru, "");
        balsa_app.fcc_mru = g_list_reverse(balsa_app.fcc_mru);
    }
    if (bsmsg->draft_message && bsmsg->draft_message->headers &&
	bsmsg->draft_message->headers->fcc_url)
        balsa_mblist_mru_add(&balsa_app.fcc_mru,
                             bsmsg->draft_message->headers->fcc_url);
    bsmsg->fcc[1] =
        balsa_mblist_mru_option_menu(GTK_WINDOW(bsmsg->window),
                                     &balsa_app.fcc_mru);
    gtk_label_set_mnemonic_widget(GTK_LABEL(bsmsg->fcc[0]), bsmsg->fcc[1]);
    gtk_table_attach(GTK_TABLE(table), bsmsg->fcc[1] , 1, 2, row, row + 1,
		     GTK_FILL, GTK_FILL, 0, 0);

    /* Attachment list */
    bsmsg->attachments[0] = gtk_label_new_with_mnemonic(_("_Attachments:"));
    gtk_misc_set_alignment(GTK_MISC(bsmsg->attachments[0]), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(bsmsg->attachments[0]), GNOME_PAD_SMALL,
			 GNOME_PAD_SMALL);
    ++row;
    gtk_table_attach(GTK_TABLE(table), bsmsg->attachments[0], 0, 1, row, row + 1,
		     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    g_signal_connect(sw, "size-request",
                     G_CALLBACK(sw_scroll_size_request), NULL);

    store = gtk_list_store_new(ATTACH_NUM_COLUMNS,
			       TYPE_BALSA_ATTACH_INFO,
			       GDK_TYPE_PIXBUF,
			       G_TYPE_STRING,
			       G_TYPE_INT,
			       G_TYPE_FLOAT,
			       G_TYPE_STRING);

    bsmsg->attachments[1] = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    view = GTK_TREE_VIEW(bsmsg->attachments[1]);
    gtk_tree_view_set_headers_visible(view, TRUE);
    gtk_tree_view_set_rules_hint(view, TRUE);
    g_object_unref(store);
    bsmsg->attachments[2] = NULL;

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

    gtk_table_attach(GTK_TABLE(table), frame, 1, 2, row, row + 1,
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
    GArray *selected;
    guint i;

    if (context->action == GDK_ACTION_ASK)
        context->action = GDK_ACTION_COPY;

    switch(info) {
    case TARGET_MESSAGES:
	index = *(BalsaIndex **) selection_data->data;
	mailbox = index->mailbox_node->mailbox;
        selected = balsa_index_selected_msgnos_new(index);
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
       
        for (i = 0; i < selected->len; i++) {
	    guint msgno = g_array_index(selected, guint, i);
	    LibBalsaMessage *message;
            GString *body;

	    message = libbalsa_mailbox_get_message(mailbox, msgno);
            if (!message)
                continue;

            body = quote_message_body(bsmsg, message, QUOTE_ALL);
	    g_object_unref(message);
            libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
            g_string_free(body, TRUE);
        }
        balsa_index_selected_msgnos_free(index, selected);
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
#if (HAVE_GTKSOURCEVIEW == 1)

static void
sw_can_undo_cb(GtkSourceBuffer * source_buffer, gboolean can_undo,
               BalsaSendmsg * bsmsg)
{
    sw_set_sensitive(bsmsg, "Undo", can_undo);
}

static void
sw_can_redo_cb(GtkSourceBuffer * source_buffer, gboolean can_redo,
               BalsaSendmsg * bsmsg)
{
    sw_set_sensitive(bsmsg, "Redo", can_redo);
}

#elif (HAVE_GTKSOURCEVIEW == 2)

static void
sw_can_undo_cb(GtkSourceBuffer * source_buffer, GParamSpec *arg1,
	       BalsaSendmsg * bsmsg)
{
    gboolean can_undo;

    g_object_get(G_OBJECT(source_buffer), "can-undo", &can_undo, NULL);
    sw_set_sensitive(bsmsg, "Undo", can_undo);
}

static void
sw_can_redo_cb(GtkSourceBuffer * source_buffer, GParamSpec *arg1,
	       BalsaSendmsg * bsmsg)
{
    gboolean can_redo;

    g_object_get(G_OBJECT(source_buffer), "can-redo", &can_redo, NULL);
    sw_set_sensitive(bsmsg, "Redo", can_redo);
}

#endif                          /* HAVE_GTKSOURCEVIEW */

static GtkWidget *
create_text_area(BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view;
    PangoFontDescription *desc;
    GtkTextBuffer *buffer;
    GtkWidget *table;

#if HAVE_GTKSOURCEVIEW
    bsmsg->text = libbalsa_source_view_new(TRUE, balsa_app.quoted_color);
#else                           /* HAVE_GTKSOURCEVIEW */
    bsmsg->text = gtk_text_view_new();
#endif                          /* HAVE_GTKSOURCEVIEW */
    text_view = GTK_TEXT_VIEW(bsmsg->text);
    gtk_text_view_set_left_margin(text_view, 2);
    gtk_text_view_set_right_margin(text_view, 2);

    /* set the message font */
    desc = pango_font_description_from_string(balsa_app.message_font);
    gtk_widget_modify_font(bsmsg->text, desc);
    pango_font_description_free(desc);

    buffer = gtk_text_view_get_buffer(text_view);
#if (HAVE_GTKSOURCEVIEW == 1)
    g_signal_connect(buffer, "can-undo",
                     G_CALLBACK(sw_can_undo_cb), bsmsg);
    g_signal_connect(buffer, "can-redo",
                     G_CALLBACK(sw_can_redo_cb), bsmsg);
#elif (HAVE_GTKSOURCEVIEW == 2)
    g_signal_connect(G_OBJECT(buffer), "notify::can-undo",
                     G_CALLBACK(sw_can_undo_cb), bsmsg);
    g_signal_connect(G_OBJECT(buffer), "notify::can-redo",
                     G_CALLBACK(sw_can_redo_cb), bsmsg);
#else                           /* HAVE_GTKSOURCEVIEW */
    bsmsg->buffer2 =
         gtk_text_buffer_new(gtk_text_buffer_get_tag_table(buffer));
#endif                          /* HAVE_GTKSOURCEVIEW */
    gtk_text_buffer_create_tag(buffer, "soft", NULL, NULL);
    gtk_text_buffer_create_tag(buffer, "url", NULL, NULL);
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

    if (!(to_codeset && from_codeset))
        return FALSE;

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

/* continue_body --------------------------------------------------------
   a short-circuit procedure for the 'Continue action'
   basically copies the first text/plain part over to the entry field.
   Attachments (if any) are saved temporarily in subfolders to preserve
   their original names and then attached again.
   NOTE that rbdy == NULL if message has no text parts.
*/
static void
continue_body(BalsaSendmsg * bsmsg, LibBalsaMessage * message)
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
            GError *err = NULL;
            gboolean res;
	    if (body->filename) {
		libbalsa_mktempdir(&tmp_file_name);
		name = g_strdup_printf("%s/%s", tmp_file_name, body->filename);
		g_free(tmp_file_name);
		res = libbalsa_message_body_save(body, name,
                                                 LIBBALSA_MESSAGE_BODY_SAFE,
                                                 FALSE, &err);
	    } else {
		fd = g_file_open_tmp("balsa-continue-XXXXXX", &name, NULL);
		res = libbalsa_message_body_save_fd(body, fd, FALSE, &err);
	    }
            if(!res) {
                balsa_information(LIBBALSA_INFORMATION_ERROR,
                                  _("Could not save attachment: %s"),
                                  err ? err->message : "Unknown error");
                g_clear_error(&err);
                /* FIXME: do not try any further? */
            }
	    body_type = libbalsa_message_body_get_mime_type(body);
	    add_attachment(bsmsg, name, TRUE, body_type);
	    g_free(body_type);
	    body = body->next;
	}
    }
}

static gchar*
message_part_get_subject(LibBalsaMessageBody *part)
{
    gchar *subject = NULL;
    if(part->embhdrs && part->embhdrs->subject)
        subject = g_strdup(part->embhdrs->subject);
    else if(part->message && part->message->subj)
        subject = g_strdup(part->message->subj);
    else subject = g_strdup(_("No subject"));
    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit,
			   NULL);
    return subject;
}

/* --- stuff for collecting parts for a reply --- */

enum {
    QUOTE_INCLUDE,
    QUOTE_DESCRIPTION,
    QUOTE_BODY,
    QOUTE_NUM_ELEMS
};

static void
tree_add_quote_body(LibBalsaMessageBody * body, GtkTreeStore * store, GtkTreeIter * parent)
{
    GtkTreeIter iter;
    gchar * mime_type = libbalsa_message_body_get_mime_type(body);
    const gchar * disp_type;
    static gboolean preselect;
    gchar * description;

    gtk_tree_store_append(store, &iter, parent);
    if (body->mime_part)
	disp_type = g_mime_part_get_content_disposition(GMIME_PART(body->mime_part));
    else
	disp_type = NULL;
    preselect = !disp_type || *disp_type == '\0' ||
	!g_ascii_strcasecmp(disp_type, "inline");
    if (body->filename && *body->filename) {
        if (preselect)
            description = g_strdup_printf(_("inlined file \"%s\" (%s)"),
                                          body->filename, mime_type);
        else
            description = g_strdup_printf(_("attached file \"%s\" (%s)"),
                                          body->filename, mime_type);
    } else {
        if (preselect)
            description = g_strdup_printf(_("inlined %s part"), mime_type);
        else
            description = g_strdup_printf(_("attached %s part"), mime_type);
    }
    g_free(mime_type);
    gtk_tree_store_set(store, &iter,
		       QUOTE_INCLUDE, preselect,
		       QUOTE_DESCRIPTION, description,
		       QUOTE_BODY, body,
		       -1);
    g_free(description);
}

static gint
scan_bodies(GtkTreeStore * bodies, GtkTreeIter * parent, LibBalsaMessageBody * body,
	    gboolean ignore_html, gboolean container_mp_alt)
{
    gchar * mime_type;
    gint count = 0;

    while (body) {
	switch (libbalsa_message_body_type(body)) {
	case LIBBALSA_MESSAGE_BODY_TYPE_TEXT:
	    {
		gchar *mime_type;
		LibBalsaHTMLType html_type;

		mime_type = libbalsa_message_body_get_mime_type(body);
		html_type = libbalsa_html_type(mime_type);
		g_free(mime_type);

		/* On a multipart/alternative, ignore_html defines if html or
		 * non-html parts will be added. Eject from the container when
		 * the first part has been found.
		 * Otherwise, select all text parts. */
		if (container_mp_alt) {
		    if ((ignore_html && html_type == LIBBALSA_HTML_TYPE_NONE) ||
			(!ignore_html && html_type != LIBBALSA_HTML_TYPE_NONE)) {
			tree_add_quote_body(body, bodies, parent);
			return count + 1;
		    }
		} else {
		    tree_add_quote_body(body, bodies, parent);
		    count++;
		}
		break;
	    }

	case LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART:
	    mime_type = libbalsa_message_body_get_mime_type(body);
	    count += scan_bodies(bodies, parent, body->parts, ignore_html,
				 !g_ascii_strcasecmp(mime_type, "multipart/alternative"));
	    g_free(mime_type);
	    break;

	case LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE:
	    {
		GtkTreeIter iter;
		gchar * description = NULL;

		mime_type = libbalsa_message_body_get_mime_type(body);
		if (g_ascii_strcasecmp(mime_type, "message/rfc822") == 0 &&
		    body->embhdrs) {
		    gchar *from = balsa_message_sender_to_gchar(body->embhdrs->from, 0);
		    gchar *subj = g_strdup(body->embhdrs->subject);
		

		    libbalsa_utf8_sanitize(&from, balsa_app.convert_unknown_8bit, NULL);
		    libbalsa_utf8_sanitize(&subj, balsa_app.convert_unknown_8bit, NULL);
		    description = 
			g_strdup_printf(_("message from %s, subject \"%s\""),
					from, subj);
		    g_free(from);
		    g_free(subj);
		} else
		    description = g_strdup(mime_type);
	    
		gtk_tree_store_append(bodies, &iter, parent);
		gtk_tree_store_set(bodies, &iter,
				   QUOTE_INCLUDE, FALSE,
				   QUOTE_DESCRIPTION, description,
				   QUOTE_BODY, NULL,
				   -1);
		g_free(mime_type);
		g_free(description);
		count += scan_bodies(bodies, &iter, body->parts, ignore_html, 0);
	    }
	    
	default:
	    break;
	}

	body = body->next;
    }

    return count;
}

static void
set_all_cells(GtkTreeModel * model, GtkTreeIter * iter, const gboolean value)
{
    do {
	GtkTreeIter children;
	
	if (gtk_tree_model_iter_children(model, &children, iter))
	    set_all_cells(model, &children, value);
	gtk_tree_store_set(GTK_TREE_STORE(model), iter, QUOTE_INCLUDE, value, -1);
    } while (gtk_tree_model_iter_next(model, iter));
}

static gboolean
calculate_expander_toggles(GtkTreeModel * model, GtkTreeIter * iter)
{
    gint count, on;

    count = on = 0;
    do {
	GtkTreeIter children;
	gboolean value;

	if (gtk_tree_model_iter_children(model, &children, iter)) {
	    value = calculate_expander_toggles(model, &children);
	    gtk_tree_store_set(GTK_TREE_STORE(model), iter, QUOTE_INCLUDE, value, -1);
	} else
	    gtk_tree_model_get(model, iter, QUOTE_INCLUDE, &value, -1);
	if (value)
	    on++;
	count++;
    } while (gtk_tree_model_iter_next(model, iter));
    
    return count == on;
}

static void
cell_toggled_cb(GtkCellRendererToggle *cell, gchar *path_str, GtkTreeView *treeview)
{
    GtkTreeModel *model = NULL;
    GtkTreePath *path;
    GtkTreeIter iter;
    GtkTreeIter children;
    gboolean active;
  
    g_return_if_fail (GTK_IS_TREE_VIEW (treeview));
    if (!(model = gtk_tree_view_get_model(treeview)))
	return;

    path = gtk_tree_path_new_from_string(path_str);
    if (!gtk_tree_model_get_iter(model, &iter, path))
	return;
    gtk_tree_path_free(path);
  
    gtk_tree_model_get(model, &iter,
		       QUOTE_INCLUDE, &active,
		       -1);
    gtk_tree_store_set(GTK_TREE_STORE (model), &iter,
		       QUOTE_INCLUDE, !active,
		       -1);
    if (gtk_tree_model_iter_children(model, &children, &iter))
	set_all_cells(model, &children, !active);
    gtk_tree_model_get_iter_first(model, &children);
    calculate_expander_toggles(model, &children);
}

static void
append_parts(GString * q_body, LibBalsaMessage *message, GtkTreeModel * model,
	     GtkTreeIter * iter, const gchar * from_msg, gchar * reply_prefix_str,
	     gint llen, gboolean flow)
{
    gboolean used_from_msg = FALSE;

    do {
	GtkTreeIter children;

	if (gtk_tree_model_iter_children(model, &children, iter)) {
	    gchar * description;

	    gtk_tree_model_get(model, iter, QUOTE_DESCRIPTION, &description, -1);
	    append_parts(q_body, message, model, &children, description,
			 reply_prefix_str, llen, flow);
	    g_free(description);
	} else {
	    gboolean do_include;

	    gtk_tree_model_get(model, iter, QUOTE_INCLUDE, &do_include, -1);
	    if (do_include) {
		LibBalsaMessageBody *this_body;

		gtk_tree_model_get(model, iter, QUOTE_BODY, &this_body, -1);
		if (this_body) {
		    GString * this_part;
		    this_part= process_mime_part(message, this_body,
                                                 reply_prefix_str, llen,
                                                 FALSE, flow);
		    
		    if (q_body->len > 0 && q_body->str[q_body->len - 1] != '\n')
			g_string_append_c(q_body, '\n');
		    if (!used_from_msg && from_msg) {
			g_string_append_printf(q_body, "\n======%s %s======\n", _("quoted"), from_msg);
			used_from_msg = TRUE;
		    } else if (q_body->len > 0) {
			if (this_body->filename)
			    g_string_append_printf(q_body, "\n------%s \"%s\"------\n",
						   _("quoted attachment"), this_body->filename);
			else
			    g_string_append_printf(q_body, "\n------%s------\n",
						   _("quoted attachment"));
		    }
		    g_string_append(q_body, this_part->str);
		    g_string_free(this_part, TRUE);
		}
	    }
	}
    } while (gtk_tree_model_iter_next(model, iter));
}

static gboolean
quote_parts_select_dlg(GtkTreeStore *tree_store, GtkWindow * parent)
{
    GtkWidget *dialog;
    GtkWidget *label;
    GtkWidget *image;
    GtkWidget *hbox;
    GtkWidget *vbox;
    GtkWidget *scroll;
    GtkWidget *tree_view;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeIter iter;
    gboolean result;

    dialog = gtk_dialog_new_with_buttons(_("Select parts for quotation"),
					 parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 NULL);

    label = gtk_label_new(_("Select the parts of the message which shall be quoted in the reply"));
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);

    image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_QUESTION,
				     GTK_ICON_SIZE_DIALOG);
    gtk_misc_set_alignment(GTK_MISC(image), 0.5, 0.0);

    /* stolen form gtk/gtkmessagedialog.c */
    hbox = gtk_hbox_new (FALSE, 12);
    vbox = gtk_vbox_new (FALSE, 12);

    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE,
		       TRUE, 0);

    gtk_container_set_border_width(GTK_CONTAINER(dialog), 5);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 14);

    /* scrolled window for the tree view */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* add the tree view */
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(tree_store));
    gtk_widget_set_size_request(tree_view, -1, 100);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);
    renderer = gtk_cell_renderer_toggle_new();
    g_signal_connect(renderer, "toggled", G_CALLBACK(cell_toggled_cb),
		     tree_view);
    column = gtk_tree_view_column_new_with_attributes(NULL, renderer,
						      "active", QUOTE_INCLUDE,
                                                      NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    gtk_tree_view_set_expander_column(GTK_TREE_VIEW(tree_view), column);
    column = gtk_tree_view_column_new_with_attributes(NULL, gtk_cell_renderer_text_new(),
						      "text", QUOTE_DESCRIPTION,
                                                      NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    gtk_tree_view_expand_all(GTK_TREE_VIEW(tree_view));
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tree_store), &iter);
    calculate_expander_toggles(GTK_TREE_MODEL(tree_store), &iter);
    
    /* add, show & run */
    gtk_container_add(GTK_CONTAINER(scroll), tree_view);
    gtk_widget_show_all(hbox);
    result = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
    gtk_widget_destroy(dialog);
    return result;
}

static gboolean
tree_find_single_part(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
		      gpointer data)
{
    LibBalsaMessageBody ** this_body = (LibBalsaMessageBody **) data;

    gtk_tree_model_get(model, iter, QUOTE_BODY, this_body, -1);
    if (*this_body)
	return TRUE;
    else
	return FALSE;
}

static GString *
collect_for_quote(LibBalsaMessageBody *root, gchar * reply_prefix_str,
		  gint llen, gboolean ignore_html, gboolean flow)
{
    GtkTreeStore * tree_store;
    gint text_bodies;
    LibBalsaMessage *message = root->message;
    GString *q_body = NULL;

    libbalsa_message_body_ref(message, FALSE, FALSE);

    /* scan the message and collect text parts which might be included
     * in the reply, and if there is only one return this part */
    tree_store = gtk_tree_store_new(QOUTE_NUM_ELEMS,
				    G_TYPE_BOOLEAN, G_TYPE_STRING,
				    G_TYPE_POINTER);
    text_bodies = scan_bodies(tree_store, NULL, root, ignore_html, FALSE);
    if (text_bodies == 1) {
	/* note: the only text body may be buried in an attached message, so
	 * we have to search the tree store... */
	LibBalsaMessageBody *this_body;

	gtk_tree_model_foreach(GTK_TREE_MODEL(tree_store), tree_find_single_part,
			       &this_body);
	if (this_body)
	    q_body = process_mime_part(message, this_body, reply_prefix_str,
				       llen, FALSE, flow);
    } else if (text_bodies > 1) {
	if (quote_parts_select_dlg(tree_store, NULL)) {
	    GtkTreeIter iter;

	    q_body = g_string_new("");
	    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tree_store), &iter);
	    append_parts(q_body, message, GTK_TREE_MODEL(tree_store), &iter, NULL,
			 reply_prefix_str, llen, flow);
	}
    }

    /* clean up */
    g_object_unref(G_OBJECT(tree_store));
    libbalsa_message_body_unref(message);
    return q_body;
}


/* quote_body -----------------------------------------------------------
   quotes properly the body of the message.
   Use GString to optimize memory usage.
   Specifying type explicitly allows for later message quoting when
   eg. a new message is composed.
*/
static GString *
quote_body(BalsaSendmsg * bsmsg, LibBalsaMessageHeaders *headers,
           const gchar *message_id, GList *references,
           LibBalsaMessageBody *root, QuoteType qtype)
{
    GString *body;
    gchar *str, *date = NULL;
    gchar *personStr;
    const gchar *orig_address;

    g_return_val_if_fail(headers, NULL);

    if (headers->from && 
	(orig_address =
	 libbalsa_address_get_name_from_list(headers->from))) {
        personStr = g_strdup(orig_address);
        libbalsa_utf8_sanitize(&personStr,
                               balsa_app.convert_unknown_8bit,
                               NULL);
    } else
        personStr = g_strdup(_("you"));

    if (headers->date)
        date = libbalsa_message_headers_date_to_utf8(headers,
                                                     balsa_app.date_string);

    if (qtype == QUOTE_HEADERS) {
	gchar *subject;

	str = g_strdup_printf(_("------forwarded message from %s------\n"), 
			      personStr);
	body = g_string_new(str);
	g_free(str);

	if (date)
	    g_string_append_printf(body, "%s %s\n", _("Date:"), date);

	subject = message_part_get_subject(root);
	if (subject)
	    g_string_append_printf(body, "%s %s\n", _("Subject:"), subject);
	g_free(subject);

	if (headers->from) {
	    gchar *from =
		internet_address_list_to_string(headers->from,
			                        FALSE);
	    g_string_append_printf(body, "%s %s\n", _("From:"), from);
	    g_free(from);
	}

	if (headers->to_list) {
	    gchar *to_list =
		internet_address_list_to_string(headers->to_list,
			                        FALSE);
	    g_string_append_printf(body, "%s %s\n", _("To:"), to_list);
	    g_free(to_list);
	}

	if (headers->cc_list) {
	    gchar *cc_list = 
		internet_address_list_to_string(headers->cc_list,
			                        FALSE);
	    g_string_append_printf(body, "%s %s\n", _("Cc:"), cc_list);
	    g_free(cc_list);
	}

	g_string_append_printf(body, _("Message-ID: %s\n"),
                               message_id);

	if (references) {
	    GList *ref_list;

	    g_string_append(body, _("References:"));

	    for (ref_list = references; ref_list;
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

	/* scan the message and collect text parts which might be included
	 * in the reply */
	body = collect_for_quote(root,
				 qtype == QUOTE_ALL ? balsa_app.quote_str : NULL,
				 bsmsg->flow ? -1 : balsa_app.wraplength,
				 balsa_app.reply_strip_html, bsmsg->flow);
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

/* fill_body -------------------------------------------------------------
   fills the body of the message to be composed based on the given message.
   First quotes the original one, if autoquote is set,
   and then adds the signature.
   Optionally prepends the signature to quoted text.
*/
static void
fill_body_from_part(BalsaSendmsg * bsmsg, LibBalsaMessageHeaders *headers,
                    const gchar *message_id, GList *references,
                    LibBalsaMessageBody *root, QuoteType qtype)
{
    GString *body;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start;

    g_assert(headers);

    body = quote_body(bsmsg, headers, message_id, references,
                      root, qtype);
    if(body->len && body->str[body->len] != '\n')
        g_string_append_c(body, '\n');
    libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
    
    if(qtype == QUOTE_HEADERS)
        gtk_text_buffer_get_end_iter(buffer, &start);
    else
        gtk_text_buffer_get_start_iter(buffer, &start);

    gtk_text_buffer_place_cursor(buffer, &start);
    g_string_free(body, TRUE);
}

static GString*
quote_message_body(BalsaSendmsg * bsmsg,
                   LibBalsaMessage * message,
                   QuoteType qtype)
{
    GString *res;
    if(libbalsa_message_body_ref(message, FALSE, FALSE)) {
        res = quote_body(bsmsg, message->headers, message->message_id,
                         message->references, message->body_list, qtype);
        libbalsa_message_body_unref(message);
    } else res = g_string_new("");
    return res;
}

static void
fill_body_from_message(BalsaSendmsg *bsmsg, LibBalsaMessage *message,
                       QuoteType qtype)
{
    fill_body_from_part(bsmsg, message->headers, message->message_id,
                        message->references, message->body_list, qtype);
}


static void
insert_signature_cb(GtkAction * action, BalsaSendmsg *bsmsg)
{
    gchar *signature;
    
    if(!bsmsg->ident->signature_path || !bsmsg->ident->signature_path[0])
        return;
    signature = libbalsa_identity_get_signature(bsmsg->ident,
                                                GTK_WINDOW(bsmsg->window));
    if (signature != NULL) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
#if !HAVE_GTKSOURCEVIEW
        sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */	
        sw_buffer_signals_block(bsmsg, buffer);
        libbalsa_insert_with_url(buffer, signature, NULL, NULL, NULL);
        sw_buffer_signals_unblock(bsmsg, buffer);
	
	g_free(signature);
    } else
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("No signature found!"));
}


static void
quote_messages_cb(GtkAction * action, BalsaSendmsg *bsmsg)
{
    insert_selected_messages(bsmsg, QUOTE_ALL);
}


/** Generates a new subject for forwarded messages based on a message
    being responded to and identity.
 */
static char*
generate_forwarded_subject(const char *orig_subject,
                           LibBalsaMessageHeaders *headers,
                           LibBalsaIdentity       *ident)
{
    char *newsubject;

    if (!orig_subject) {
        if (headers && headers->from)
            newsubject = g_strdup_printf("%s from %s",
                                         ident->forward_string,
                                         libbalsa_address_get_mailbox_from_list
                                         (headers->from));
        else
            newsubject = g_strdup(ident->forward_string);
    } else {
        const char *tmp = orig_subject;
        if (g_ascii_strncasecmp(tmp, "fwd:", 4) == 0) {
            tmp += 4;
        } else if (g_ascii_strncasecmp(tmp, _("Fwd:"),
                                       strlen(_("Fwd:"))) == 0) {
            tmp += strlen(_("Fwd:"));
        } else {
            size_t i = strlen(ident->forward_string);
            if (g_ascii_strncasecmp(tmp, ident->forward_string, i) == 0) {
                tmp += i;
            }
        }
        while( *tmp && isspace((int)*tmp) ) tmp++;
        if (headers && headers->from)
            newsubject = 
                g_strdup_printf("%s %s [%s]",
                                ident->forward_string, 
                                tmp,
                                libbalsa_address_get_mailbox_from_list
                                (headers->from));
        else {
            newsubject = 
                g_strdup_printf("%s %s", 
                                ident->forward_string, 
                                tmp);
            g_strchomp(newsubject);
        }
    }
    return newsubject;
}
/* set_entry_to_subject:
   set subject entry based on given replied/forwarded/continued message
   and the compose type.
*/
static void
set_entry_to_subject(GtkEntry* entry, LibBalsaMessageBody *part,
                     SendType type, LibBalsaIdentity* ident)
{
    const gchar *tmp;
    gchar *subject, *newsubject = NULL;
    gint i;
    LibBalsaMessageHeaders *headers;

    if(!part) return;
    subject = message_part_get_subject(part);
    headers = part->embhdrs ? part->embhdrs : part->message->headers;
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
	g_strdelimit(newsubject, "\r\n", ' ');
	break;

    case SEND_FORWARD_ATTACH:
    case SEND_FORWARD_INLINE:
        newsubject = generate_forwarded_subject(subject, headers, ident);
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

#if HAVE_GTKSOURCEVIEW
#define BALSA_FIRST_WRAP "balsa-first-wrap"
#endif                          /* HAVE_GTKSOURCEVIEW */

static gboolean
sw_wrap_timeout_cb(BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
#if HAVE_GTKSOURCEVIEW
    GtkSourceBuffer *source_buffer;
    gboolean first_wrap;
#endif                          /* HAVE_GTKSOURCEVIEW */
    GtkTextIter now;

    gdk_threads_enter();

    text_view = GTK_TEXT_VIEW(bsmsg->text);
    buffer = gtk_text_view_get_buffer(text_view);
    gtk_text_buffer_get_iter_at_mark(buffer, &now,
                                     gtk_text_buffer_get_insert(buffer));

    bsmsg->wrap_timeout_id = 0;
    sw_buffer_signals_block(bsmsg, buffer);

#if HAVE_GTKSOURCEVIEW
    first_wrap = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer),
                                                   BALSA_FIRST_WRAP));
    source_buffer = GTK_SOURCE_BUFFER(buffer);
    if (first_wrap)
        gtk_source_buffer_begin_not_undoable_action(source_buffer);
#endif                          /* HAVE_GTKSOURCEVIEW */
    libbalsa_unwrap_buffer(buffer, &now, 1);
    libbalsa_wrap_view(text_view, balsa_app.wraplength);
#if HAVE_GTKSOURCEVIEW
    if (first_wrap) {
        gtk_source_buffer_end_not_undoable_action(source_buffer);
        g_object_set_data(G_OBJECT(buffer), BALSA_FIRST_WRAP,
                          GINT_TO_POINTER(FALSE));
    }
#endif                          /* HAVE_GTKSOURCEVIEW */
    sw_buffer_signals_unblock(bsmsg, buffer);
    gtk_text_view_scroll_to_mark(text_view,
                                 gtk_text_buffer_get_insert(buffer),
                                 0, FALSE, 0, 0);

    gdk_threads_leave();

    return FALSE;
}

static gboolean
sw_save_draft(BalsaSendmsg * bsmsg)
{
    GError *err = NULL;

    if (!message_postpone(bsmsg)) {
	balsa_information_parented(GTK_WINDOW(bsmsg->window),
				   LIBBALSA_INFORMATION_MESSAGE,
                                   _("Could not save message."));
        return FALSE;
    }

    if(!libbalsa_mailbox_open(balsa_app.draftbox, &err)) {
	balsa_information_parented(GTK_WINDOW(bsmsg->window),
				   LIBBALSA_INFORMATION_WARNING,
				   _("Could not open draftbox: %s"),
				   err ? err->message : _("Unknown error"));
	g_clear_error(&err);
	return FALSE;
    }

    if (bsmsg->draft_message) {
	if (bsmsg->draft_message->mailbox)
	    libbalsa_mailbox_close(bsmsg->draft_message->mailbox,
		    /* Respect pref setting: */
				   balsa_app.expunge_on_close);
	g_object_unref(G_OBJECT(bsmsg->draft_message));
    }
    bsmsg->state = SENDMSG_STATE_CLEAN;

    bsmsg->draft_message =
	libbalsa_mailbox_get_message(balsa_app.draftbox,
				     libbalsa_mailbox_total_messages
				     (balsa_app.draftbox));
    balsa_information_parented(GTK_WINDOW(bsmsg->window),
                               LIBBALSA_INFORMATION_MESSAGE,
                               _("Message saved."));

    return TRUE;
}

static gboolean
sw_autosave_timeout_cb(BalsaSendmsg * bsmsg)
{
    gdk_threads_enter();

    if (bsmsg->state == SENDMSG_STATE_MODIFIED) {
        if (sw_save_draft(bsmsg))
            bsmsg->state = SENDMSG_STATE_AUTO_SAVED;
    }

    gdk_threads_leave();

    return TRUE;                /* do repeat it */
}

static void
setup_headers_from_message(BalsaSendmsg* bsmsg, LibBalsaMessage *message)
{
    g_return_if_fail(message->headers);

    libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                        "To:", message->headers->to_list);
    libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                        "Cc:", message->headers->cc_list);
    libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                        "Bcc:", message->headers->bcc_list);
}


/* 
 * set_identity_from_mailbox
 * 
 * Attempt to determine the default identity from the mailbox containing
 * the message.
 **/
static gboolean
set_identity_from_mailbox(BalsaSendmsg* bsmsg, LibBalsaMessage * message)
{
    const gchar *identity;

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
guess_identity(BalsaSendmsg* bsmsg, LibBalsaMessage * message)
{
    const gchar *address_string;
    GList *ilist;
    LibBalsaIdentity *ident;
    const gchar *tmp;


    if (!message  || !message->headers || !balsa_app.identities)
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
        libbalsa_address_view_set_from_string(bsmsg->replyto_view,
                                              "Reply To:",
                                              ident->replyto);
#endif
    if(ident->bcc)
        libbalsa_address_view_set_from_string(bsmsg->recipient_view,
                                              "Bcc:",
                                              ident->bcc);
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
#define BALSA_LANGUAGE_MENU_POS "balsa-language-menu-pos"
static void
create_lang_menu(GtkWidget * parent, BalsaSendmsg * bsmsg)
{
    unsigned i, selected_pos;
    GtkWidget *langs = gtk_menu_new();
    static gboolean locales_sorted = FALSE;
    GSList *group = NULL;

    if (!locales_sorted) {
        for (i = 0; i < ELEMENTS(locales); i++)
            locales[i].lang_name = _(locales[i].lang_name);
        qsort(locales, ELEMENTS(locales), sizeof(struct SendLocales),
              comp_send_locales);
        locales_sorted = TRUE;
    }

    /* find the preferred charset... */
#if HAVE_GTKSPELL
    selected_pos = 
	find_locale_index_by_locale(balsa_app.spell_check_lang
				    ? balsa_app.spell_check_lang
				    : setlocale(LC_CTYPE, NULL));
#else                           /* HAVE_GTKSPELL */
    selected_pos = find_locale_index_by_locale(setlocale(LC_CTYPE, NULL));
#endif                          /* HAVE_GTKSPELL */
    set_locale(bsmsg, selected_pos);

    for (i = 0; i < ELEMENTS(locales); i++) {
#if HAVE_GTKSPELL
        GtkSpell *spell;

        spell = gtkspell_new_attach(GTK_TEXT_VIEW(bsmsg->text),
                                    locales[i].locale, NULL);
        if (spell) {
            GtkWidget *w;

            gtkspell_detach(spell);

            w = gtk_radio_menu_item_new_with_mnemonic(group,
                                                      locales[i].
                                                      lang_name);
            group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(w));
            if (i == selected_pos)
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w),
                                               TRUE);

            g_signal_connect(G_OBJECT(w), "activate",
                             G_CALLBACK(lang_set_cb), bsmsg);
            g_object_set_data(G_OBJECT(w), BALSA_LANGUAGE_MENU_POS,
                              GINT_TO_POINTER(i));
            gtk_widget_show(w);
            gtk_menu_shell_append(GTK_MENU_SHELL(langs), w);
        }
#else                           /* HAVE_GTKSPELL */
        GtkWidget *w =
            gtk_radio_menu_item_new_with_mnemonic(group,
                                                  locales[i].lang_name);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(w));
        if (i == selected_pos)
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), TRUE);

        g_signal_connect(G_OBJECT(w), "activate",
                         G_CALLBACK(lang_set_cb), bsmsg);
        g_object_set_data(G_OBJECT(w), BALSA_LANGUAGE_MENU_POS,
                          GINT_TO_POINTER(i));
        gtk_widget_show(w);
        gtk_menu_shell_append(GTK_MENU_SHELL(langs), w);
#endif                          /* HAVE_GTKSPELL */
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), langs);
    gtk_widget_show(parent);
}
        
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
static BalsaToolbarModel *
sw_get_toolbar_model(void)
{
    static BalsaToolbarModel *model = NULL;
    GSList *standard;
    guint i;

    if (model)
        return model;

    standard = NULL;
    for (i = 0; i < ELEMENTS(compose_toolbar); i++)
        standard = g_slist_append(standard, g_strdup(compose_toolbar[i]));

    model =
        balsa_toolbar_model_new(BALSA_TOOLBAR_TYPE_COMPOSE_WINDOW,
                                standard);
    balsa_toolbar_model_add_actions(model, entries,
                                    G_N_ELEMENTS(entries));
    balsa_toolbar_model_add_actions(model, ready_entries,
                                    G_N_ELEMENTS(ready_entries));
    balsa_toolbar_model_add_toggle_actions(model, toggle_entries,
                                           G_N_ELEMENTS(toggle_entries));

    return model;
}

static BalsaToolbarModel *
sw_get_toolbar_model_and_ui_manager(BalsaSendmsg * bsmsg,
                                    GtkUIManager ** ui_manager)
{
    BalsaToolbarModel *model = sw_get_toolbar_model();

    if (ui_manager)
        *ui_manager = sw_get_ui_manager(bsmsg);

    return model;
}

BalsaToolbarModel *
sendmsg_window_get_toolbar_model(GtkUIManager ** ui_manager)
{
    return sw_get_toolbar_model_and_ui_manager(NULL, ui_manager);
}

static void
bsmsg_identities_changed_cb(BalsaSendmsg * bsmsg)
{
    sw_set_sensitive(bsmsg, "SelectIdentity",
                     balsa_app.identities->next != NULL);
}

static void
sw_cc_add_list(InternetAddressList **new_cc, InternetAddressList * list)
{
    for (; list; list = list->next) {
        InternetAddress *ia;

        if ((ia = list->address)) {
            GList *ident;

            /* do not insert any of my identities into the cc: list */
            for (ident = balsa_app.identities; ident; ident = ident->next)
                if (libbalsa_ia_rfc2821_equal
                    (ia, LIBBALSA_IDENTITY(ident->data)->ia))
                    break;
            if (!ident)
                *new_cc = internet_address_list_append(*new_cc, ia);
        }
    }
}

static BalsaSendmsg*
sendmsg_window_new()
{
    BalsaToolbarModel *model;
    GtkWidget *toolbar;
    GtkWidget *window;
    GtkWidget *main_box = gtk_vbox_new(FALSE, 0);
    BalsaSendmsg *bsmsg = NULL;
#if HAVE_GTKSOURCEVIEW
    GtkSourceBuffer *source_buffer;
#endif                          /* HAVE_GTKSOURCEVIEW */
    GtkUIManager *ui_manager;
    GtkAccelGroup *accel_group;
    GError *error;
    GtkWidget *menubar;

    bsmsg = g_malloc(sizeof(BalsaSendmsg));
    bsmsg->in_reply_to = NULL;
    bsmsg->references = NULL;
    bsmsg->spell_check_lang = NULL;
    bsmsg->fcc_url  = NULL;
    bsmsg->insert_mark = NULL;
    bsmsg->ident = balsa_app.current_ident;
    bsmsg->update_config = FALSE;
    bsmsg->quit_on_close = FALSE;
    bsmsg->state = SENDMSG_STATE_CLEAN;

    bsmsg->window = window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    /*
     * restore the SendMsg window size
     */
    gtk_window_set_default_size(GTK_WINDOW(window), 
                                balsa_app.sw_width,
                                balsa_app.sw_height);
    if (balsa_app.sw_maximized)
        gtk_window_maximize(GTK_WINDOW(window));

    gtk_window_set_wmclass(GTK_WINDOW(window), "compose", "Balsa");

    gtk_container_add(GTK_CONTAINER(window), main_box);
    gtk_widget_show_all(window);

    bsmsg->type = SEND_NORMAL;
#if !HAVE_GTKSPELL
    bsmsg->spell_checker = NULL;
#endif                          /* HAVE_GTKSPELL */
#ifdef HAVE_GPGME
    bsmsg->gpg_mode = LIBBALSA_PROTECT_RFC3156;
#endif
    bsmsg->wrap_timeout_id = 0;
    bsmsg->autosave_timeout_id = /* autosave every 5 minutes */
        g_timeout_add(1000*60*5, (GSourceFunc)sw_autosave_timeout_cb, bsmsg);

    bsmsg->draft_message = NULL;
    bsmsg->parent_message = NULL;
    g_signal_connect(G_OBJECT(window), "delete-event",
		     G_CALLBACK(delete_event_cb), bsmsg);
    g_signal_connect(G_OBJECT(window), "destroy",
		     G_CALLBACK(destroy_event_cb), bsmsg);
    g_signal_connect(G_OBJECT(window), "size_allocate",
		     G_CALLBACK(sw_size_alloc_cb), bsmsg);

    model = sw_get_toolbar_model_and_ui_manager(bsmsg, &ui_manager);

    accel_group = gtk_ui_manager_get_accel_group(ui_manager);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
    g_object_unref(accel_group);

    error = NULL;
    if (!gtk_ui_manager_add_ui_from_string
        (ui_manager, ui_description, -1, &error)) {
        g_message("building menus failed: %s", error->message);
        g_error_free(error);
        g_object_unref(ui_manager);
        g_object_unref(window);
        g_free(bsmsg);
        return NULL;
    }

    menubar = gtk_ui_manager_get_widget(ui_manager, "/MainMenu");
    gtk_box_pack_start(GTK_BOX(main_box), menubar, FALSE, FALSE, 0);

    toolbar = balsa_toolbar_new(model, ui_manager);
    gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 0);

    /* Now that we have installed the menubar and toolbar, we no longer
     * need the UIManager. */
    g_object_unref(ui_manager);

    bsmsg->flow = !balsa_app.wordwrap;
    sw_set_sensitive(bsmsg, "Reflow", bsmsg->flow);

    sw_set_sensitive(bsmsg, "SelectIdentity",
                     balsa_app.identities->next != NULL);
    bsmsg->identities_changed_id = 
        g_signal_connect_swapped(balsa_app.main_window, "identities-changed",
                                 (GCallback)bsmsg_identities_changed_cb,
                                 bsmsg);
#if !HAVE_GTKSOURCEVIEW
    sw_buffer_set_undo(bsmsg, TRUE, FALSE);
#endif                          /* HAVE_GTKSOURCEVIEW */

    /* set options */
    bsmsg->req_dispnotify = FALSE;

    sw_set_active(bsmsg, "Flowed", bsmsg->flow);

#ifdef HAVE_GPGME
    bsmsg_setup_gpg_ui(bsmsg);
#endif

    /* create the top portion with the to, from, etc in it */
    gtk_box_pack_start(GTK_BOX(main_box), create_info_pane(bsmsg),
                       FALSE, FALSE, 0);

    /* create text area for the message */
    gtk_box_pack_start(GTK_BOX(main_box), create_text_area(bsmsg),
                       TRUE, TRUE, 0);

    /* set the menus - and language index */
    init_menus(bsmsg);

    /* Connect to "text-changed" here, so that we catch the initial text
     * and wrap it... */
    sw_buffer_signals_connect(bsmsg);

#if HAVE_GTKSOURCEVIEW
    source_buffer = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer
                                      (GTK_TEXT_VIEW(bsmsg->text)));
    g_object_set_data(G_OBJECT(source_buffer), BALSA_FIRST_WRAP,
                      GINT_TO_POINTER(TRUE));
    gtk_source_buffer_begin_not_undoable_action(source_buffer);
    gtk_source_buffer_end_not_undoable_action(source_buffer);
    sw_set_sensitive(bsmsg, "Undo", FALSE);
    sw_set_sensitive(bsmsg, "Redo", FALSE);
#else                           /* HAVE_GTKSOURCEVIEW */
    sw_buffer_set_undo(bsmsg, FALSE, FALSE);
#endif                          /* HAVE_GTKSOURCEVIEW */

    bsmsg->update_config = TRUE;
 
    bsmsg->delete_sig_id = 
	g_signal_connect(G_OBJECT(balsa_app.main_window), "delete-event",
			 G_CALLBACK(delete_event_cb), bsmsg);
    
    bsmsg->current_language_menu =
#if !defined(ENABLE_TOUCH_UI)
        gtk_ui_manager_get_widget(ui_manager, "/MainMenu/LanguageMenu");
#else                           /* ENABLE_TOUCH_UI */
        gtk_ui_manager_get_widget(ui_manager,
                                  "/MainMenu/ToolsMenu/LanguageMenu");
#endif                          /* ENABLE_TOUCH_UI */
    create_lang_menu(bsmsg->current_language_menu, bsmsg);

#if HAVE_GTKSPELL
    sw_set_active(bsmsg, "CheckSpelling", balsa_app.spell_check_active);
#endif
    setup_headers_from_identity(bsmsg, bsmsg->ident);

    return bsmsg;
}

static void
insert_initial_sig(BalsaSendmsg *bsmsg)
{
    GtkTextIter sig_pos;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextMark *m;

    if(bsmsg->ident->sig_prepend)
        gtk_text_buffer_get_start_iter(buffer, &sig_pos);
    else
        gtk_text_buffer_get_end_iter(buffer, &sig_pos);
    m = gtk_text_buffer_create_mark (buffer, "pos", &sig_pos, TRUE);
    gtk_text_buffer_insert(buffer, &sig_pos, "\n", 1);
    insert_signature_cb(NULL, bsmsg);
    gtk_text_buffer_get_iter_at_mark(buffer, &sig_pos, m);
    gtk_text_buffer_place_cursor(buffer, &sig_pos);
    gtk_text_buffer_delete_mark(buffer, m);
}

BalsaSendmsg*
sendmsg_window_compose(void)
{
    BalsaSendmsg *bsmsg = sendmsg_window_new();

    /* set the initial window title */
    bsmsg->type = SEND_NORMAL;
    sendmsg_window_set_title(bsmsg);
    if(bsmsg->ident->sig_sending)
        insert_initial_sig(bsmsg);
    bsmsg->state = SENDMSG_STATE_CLEAN;
    return bsmsg;
}

BalsaSendmsg*
sendmsg_window_compose_with_address(const gchar * address)
{
    BalsaSendmsg *bsmsg = sendmsg_window_compose();
    libbalsa_address_view_add_from_string(bsmsg->recipient_view,
                                          "To:", address);
    return bsmsg;
}

static void
bsm_prepare_for_setup(LibBalsaMessage *message)
{
    if (message->mailbox)
        libbalsa_mailbox_open(message->mailbox, NULL);
    /* fill in that info:
     * ref the message so that we have all needed headers */
    libbalsa_message_body_ref(message, TRUE, TRUE);
#ifdef HAVE_GPGME
    /* scan the message for encrypted parts - this is only possible if
       there is *no* other ref to it */
    balsa_message_perform_crypto(message, LB_MAILBOX_CHK_CRYPT_NEVER,
                                 TRUE, 1);
#endif
}

/* libbalsa_message_body_unref() may destroy the @param part - this is
   why body_unref() is done at the end. */
static void
bsm_finish_setup(BalsaSendmsg *bsmsg, LibBalsaMessageBody *part)
{
    g_return_if_fail(part->message);
    if (part->message->mailbox &&
        !bsmsg->parent_message && !bsmsg->draft_message)
        libbalsa_mailbox_close(part->message->mailbox, FALSE);
    /* ...but mark it as unmodified. */
    bsmsg->state = SENDMSG_STATE_CLEAN;
    set_entry_to_subject(GTK_ENTRY(bsmsg->subject[1]), part, bsmsg->type,
                         bsmsg->ident);
    libbalsa_message_body_unref(part->message);
}

static void
set_cc_from_all_recipients(BalsaSendmsg* bsmsg,
                           LibBalsaMessageHeaders *headers)
{
    InternetAddressList *new_cc = NULL;

    sw_cc_add_list(&new_cc, headers->to_list);
    sw_cc_add_list(&new_cc, headers->cc_list);

    libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                        "Cc:",
                                        new_cc);
    internet_address_list_destroy(new_cc);
}

static void
set_in_reply_to(BalsaSendmsg *bsmsg, const gchar *message_id,
                LibBalsaMessageHeaders *headers)
{
    gchar *tmp;

    g_assert(message_id);
    if(message_id[0] == '<')
        tmp = g_strdup(message_id);
    else
        tmp = g_strconcat("<", message_id, ">", NULL);
    if (headers && headers->from) {
        gchar recvtime[50];

        ctime_r(&headers->date, recvtime);
        if (recvtime[0]) /* safety check; remove trailing '\n' */
            recvtime[strlen(recvtime)-1] = '\0';
        bsmsg->in_reply_to =
            g_strconcat(tmp, " (from ",
                        libbalsa_address_get_mailbox_from_list
                        (headers->from),
                        " on ", recvtime, ")", NULL);
        g_free(tmp);
    } else
        bsmsg->in_reply_to = tmp;
}

static void
set_to(BalsaSendmsg *bsmsg, LibBalsaMessageHeaders *headers)
{
    if (bsmsg->type == SEND_REPLY_GROUP) {
        set_list_post_address(bsmsg);
    } else {
        InternetAddressList *addr = headers->reply_to ?
            headers->reply_to : headers->from;

        libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                            "To:", addr);
    }
}

static void
set_references_reply(BalsaSendmsg *bsmsg, GList *references,
                     const gchar *in_reply_to, const gchar *message_id)
{
    GList *refs = NULL, *list;
 
    for (list = references; list; list = list->next)
        refs = g_list_prepend(refs, g_strdup(list->data));

    /* We're replying to parent_message, so construct the
     * references according to RFC 2822. */
    if (!references
        /* Parent message has no References header... */
        && in_reply_to)
            /* ...but it has an In-Reply-To header with a single
             * message identifier. */
        refs = g_list_prepend(refs, g_strdup(in_reply_to));
    if (message_id)
        refs = g_list_prepend(refs, g_strdup(message_id));

    bsmsg->references = g_list_reverse(refs);
}

static void
set_identity(BalsaSendmsg * bsmsg, LibBalsaMessage * message)
{
    /* Set up the default identity */
    if(!set_identity_from_mailbox(bsmsg, message))
        /* Get the identity from the To: field of the original message */
        guess_identity(bsmsg, message);
    /* From: */
    setup_headers_from_identity(bsmsg, bsmsg->ident);
}

static gboolean
sw_grab_focus_to_text(GtkWidget * text)
{
    gdk_threads_enter();
    gtk_widget_grab_focus(text);
    g_object_unref(text);
    gdk_threads_leave();
    return FALSE;
}

BalsaSendmsg *
sendmsg_window_reply(LibBalsaMailbox * mailbox, guint msgno,
                     SendType reply_type)
{
    LibBalsaMessage *message =
        libbalsa_mailbox_get_message(mailbox, msgno);
    BalsaSendmsg *bsmsg = sendmsg_window_new();

    g_assert(message);
    switch(reply_type) {
    case SEND_REPLY: 
    case SEND_REPLY_ALL:
    case SEND_REPLY_GROUP:
        bsmsg->type = reply_type;       break;
    default: printf("reply_type: %d\n", reply_type); g_assert_not_reached();
    }
    bsmsg->parent_message = message;
    set_identity(bsmsg, message);

    bsm_prepare_for_setup(message);

    set_to(bsmsg, message->headers);

    if (message->message_id)
        set_in_reply_to(bsmsg, message->message_id, message->headers);
    if (reply_type == SEND_REPLY_ALL)
        set_cc_from_all_recipients(bsmsg, message->headers);
    set_references_reply(bsmsg, message->references,
                         message->in_reply_to 
                         ? message->in_reply_to->data : NULL,
                         message->message_id);
    if(balsa_app.autoquote)
        fill_body_from_message(bsmsg, message, QUOTE_ALL);
    if(bsmsg->ident->sig_whenreply)
        insert_initial_sig(bsmsg);
    bsm_finish_setup(bsmsg, message->body_list);
    g_idle_add((GSourceFunc) sw_grab_focus_to_text,
               g_object_ref(bsmsg->text));
    return bsmsg;
}

BalsaSendmsg*
sendmsg_window_reply_embedded(LibBalsaMessageBody *part,
                              SendType reply_type)
{
    BalsaSendmsg *bsmsg = sendmsg_window_new();
    LibBalsaMessageHeaders *headers;

    g_assert(part);
    g_return_val_if_fail(part->embhdrs, bsmsg);

    switch(reply_type) {
    case SEND_REPLY: 
    case SEND_REPLY_ALL:
    case SEND_REPLY_GROUP:
        bsmsg->type = reply_type;       break;
    default: printf("reply_type: %d\n", reply_type); g_assert_not_reached();
    }
    bsm_prepare_for_setup(g_object_ref(part->message));
    headers = part->embhdrs;
    /* To: */
    set_to(bsmsg, headers);

    if(part->embhdrs) {
        const gchar *message_id = 
            libbalsa_message_header_get_one(part->embhdrs, "Message-Id");
        const gchar *in_reply_to =
            libbalsa_message_header_get_one(part->embhdrs, "In-Reply-To");
        GList *references = 
            libbalsa_message_header_get_all(part->embhdrs, "References");
        if (message_id)
            set_in_reply_to(bsmsg, message_id, headers);
        set_references_reply(bsmsg, references,
                             in_reply_to, message_id);
        fill_body_from_part(bsmsg, part->embhdrs, message_id, references,
                            part->parts, QUOTE_ALL);
        g_list_foreach(references, (GFunc) g_free, NULL);
        g_list_free(references);
    }

    if (reply_type == SEND_REPLY_ALL)
        set_cc_from_all_recipients(bsmsg, part->embhdrs);

    bsm_finish_setup(bsmsg, part);
    if(bsmsg->ident->sig_whenreply)
        insert_initial_sig(bsmsg);
    g_idle_add((GSourceFunc) sw_grab_focus_to_text,
               g_object_ref(bsmsg->text));
    return bsmsg;
}

BalsaSendmsg*
sendmsg_window_forward(LibBalsaMailbox *mailbox, guint msgno,
                       gboolean attach)
{
    LibBalsaMessage *message =
        libbalsa_mailbox_get_message(mailbox, msgno);
    BalsaSendmsg *bsmsg = sendmsg_window_new();
    g_assert(message);
    
    bsmsg->type = attach ? SEND_FORWARD_ATTACH : SEND_FORWARD_INLINE;
    if (attach) {
	if(!attach_message(bsmsg, message))
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Attaching message failed.\n"
                                   "Possible reason: not enough temporary space"));
        bsmsg->state = SENDMSG_STATE_CLEAN;
        set_entry_to_subject(GTK_ENTRY(bsmsg->subject[1]), message->body_list,
                             bsmsg->type, bsmsg->ident);
    } else {
        bsm_prepare_for_setup(message);
        fill_body_from_message(bsmsg, message, QUOTE_NOPREFIX);
        bsm_finish_setup(bsmsg, message->body_list);
    }
    if(bsmsg->ident->sig_whenforward)
        insert_initial_sig(bsmsg);
    if(!attach) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
        GtkTextIter pos;
        gtk_text_buffer_get_start_iter(buffer, &pos);
        gtk_text_buffer_place_cursor(buffer, &pos);
        gtk_text_buffer_insert_at_cursor(buffer, "\n", 1);
        gtk_text_buffer_get_start_iter(buffer, &pos);
        gtk_text_buffer_place_cursor(buffer, &pos);
     }
    return bsmsg;
}

BalsaSendmsg*
sendmsg_window_continue(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *message =
        libbalsa_mailbox_get_message(mailbox, msgno);
    BalsaSendmsg *bsmsg = sendmsg_window_new();
    const gchar *postpone_hdr;
    GList *list, *refs = NULL;

    g_assert(message);
    bsmsg->type = SEND_CONTINUE;
    bsm_prepare_for_setup(message);
    bsmsg->draft_message = message;
    set_identity(bsmsg, message);
    setup_headers_from_message(bsmsg, message);

#if !defined(ENABLE_TOUCH_UI)
    libbalsa_address_view_set_from_list(bsmsg->replyto_view,
                                        "Reply To:",
                                        message->headers->reply_to);
#endif
    if (message->in_reply_to)
        bsmsg->in_reply_to =
            g_strconcat("<", message->in_reply_to->data, ">", NULL);

#ifdef HAVE_GPGME
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Crypto")))
        bsmsg_setup_gpg_ui_by_mode(bsmsg, atoi(postpone_hdr));
#endif
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-MDN")))
        sw_set_active(bsmsg, "RequestMDN", atoi(postpone_hdr) != 0);
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Lang"))) {
        GtkWidget *langs =
            gtk_menu_item_get_submenu(GTK_MENU_ITEM
                                      (bsmsg->current_language_menu));
        GList *list, *children =
            gtk_container_get_children(GTK_CONTAINER(langs));
        unsigned selected_pos = find_locale_index_by_locale(postpone_hdr);
        set_locale(bsmsg, selected_pos);
        for (list = children; list; list = list->next) {
            GtkCheckMenuItem *menu_item = list->data;
            if (GPOINTER_TO_UINT
                (g_object_get_data(G_OBJECT(menu_item),
                                   BALSA_LANGUAGE_MENU_POS)) ==
                selected_pos)
                gtk_check_menu_item_set_active(menu_item, TRUE);
        }
        g_list_free(children);
    }
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Format")))
        sw_set_active(bsmsg, "Flowed", strcmp(postpone_hdr, "Fixed"));

    for (list = message->references; list; list = list->next)
        refs = g_list_prepend(refs, g_strdup(list->data));
    bsmsg->references = g_list_reverse(refs);

    continue_body(bsmsg, message);
    bsm_finish_setup(bsmsg, message->body_list);
    g_idle_add((GSourceFunc) sw_grab_focus_to_text,
               g_object_ref(bsmsg->text));
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

#define NO_SECURITY_ISSUES_WITH_ATTACHMENTS TRUE
#if defined(NO_SECURITY_ISSUES_WITH_ATTACHMENTS)
static void
sw_attach_file(BalsaSendmsg * bsmsg, const gchar * val)
{
    GtkFileChooser *attach;

    if (!g_path_is_absolute(val)) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not attach the file %s: %s."), val,
                          _("not an absolute path"));
        return;
    }
    if (!(g_str_has_prefix(val, g_get_home_dir())
          || g_str_has_prefix(val, g_get_tmp_dir()))) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not attach the file %s: %s."), val,
                          _("not in your directory"));
        return;
    }
    if (!g_file_test(val, G_FILE_TEST_EXISTS)) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not attach the file %s: %s."), val,
                          _("does not exist"));
        return;
    }
    if (!g_file_test(val, G_FILE_TEST_IS_REGULAR)) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not attach the file %s: %s."), val,
                          _("not a regular file"));
        return;
    }
    attach = g_object_get_data(G_OBJECT(bsmsg->window),
                               "balsa-sendmsg-window-attach-dialog");
    if (!attach) {
        attach = sw_attach_dialog(bsmsg);
        g_object_set_data(G_OBJECT(bsmsg->window),
                          "balsa-sendmsg-window-attach-dialog", attach);
        g_object_set_data_full(G_OBJECT(attach),
                               "balsa-sendmsg-window-attach-dir",
                               g_path_get_dirname(val), g_free);
    } else {
        gchar *dirname = g_object_get_data(G_OBJECT(attach),
                                           "balsa-sendmsg-window-attach-dir");
        gchar *valdir = g_path_get_dirname(val);
        gboolean good = (strcmp(dirname, valdir) == 0);

        g_free(valdir);
        if (!good) {
            /* gtk_file_chooser_select_filename will crash */
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                              _("Could not attach the file %s: %s."), val,
                              _("not in current directory"));
            return;
        }
    }
    gtk_file_chooser_select_filename(attach, val);
}
#endif

void
sendmsg_window_set_field(BalsaSendmsg * bsmsg, const gchar * key,
                         const gchar * val)
{
    const gchar *type;
    g_return_if_fail(bsmsg);

    if (g_ascii_strcasecmp(key, "body") == 0) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

        libbalsa_insert_with_url(buffer, val, NULL, NULL, NULL);

        return;
    }
#if defined(NO_SECURITY_ISSUES_WITH_ATTACHMENTS)
    if (g_ascii_strcasecmp(key, "attach") == 0) {
        sw_attach_file(bsmsg, val);
        return;
    }
#endif
    if(g_ascii_strcasecmp(key, "subject") == 0) {
        append_comma_separated(GTK_EDITABLE(bsmsg->subject[1]), val);
        gtk_widget_show_all(bsmsg->subject[0]);
        gtk_widget_show_all(bsmsg->subject[1]);
        return;
    }
    
    if (g_ascii_strcasecmp(key, "to") == 0)
        type = "To:";
    else if(g_ascii_strcasecmp(key, "cc") == 0)
        type = "Cc:";
    else if(g_ascii_strcasecmp(key, "bcc") == 0) {
        type = "Bcc:";
        if (!g_object_get_data(G_OBJECT(bsmsg->window),
                               "balsa-sendmsg-window-url-bcc")) {
            GtkWidget *dialog =
                gtk_message_dialog_new
                (GTK_WINDOW(bsmsg->window),
                 GTK_DIALOG_DESTROY_WITH_PARENT,
                 GTK_MESSAGE_INFO,
                 GTK_BUTTONS_OK,
                 _("The link that you selected created\n"
                   "a \"Blind copy\" (Bcc) address.\n"
                   "Please check that the address\n"
                   "is appropriate."));
            g_object_set_data(G_OBJECT(bsmsg->window),
                              "balsa-sendmsg-window-url-bcc", dialog);
            g_signal_connect(G_OBJECT(dialog), "response",
                             G_CALLBACK(gtk_widget_destroy), NULL);
            gtk_widget_show_all(dialog);
        }
    }
#if !defined(ENABLE_TOUCH_UI)
    else if(g_ascii_strcasecmp(key, "replyto") == 0) {
        libbalsa_address_view_add_from_string(bsmsg->replyto_view,
                                              "Reply To:",
                                              val);
        return;
    }
#endif
    else return;

    libbalsa_address_view_add_from_string(bsmsg->recipient_view, type, val);
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
    string = NULL;
    len = libbalsa_readfile(fl, &string);
    fclose(fl);

    if (string) {
        LibBalsaTextAttribute attr;

        attr = libbalsa_text_attr_string(string);
        if (!attr || attr & LIBBALSA_TEXT_HI_UTF8)
            /* Ascii or utf-8 */
            libbalsa_insert_with_url(buffer, string, NULL, NULL, NULL);
        else {
            /* Neither ascii nor utf-8... */
            gchar *s = NULL;
            const gchar *charset = sw_preferred_charset(bsmsg);

            if (sw_can_convert(string, -1, "UTF-8", charset, &s)) {
                /* ...but seems to be in current charset. */
                libbalsa_insert_with_url(buffer, s, NULL, NULL, NULL);
                g_free(s);
            } else
                /* ...and can't be decoded from current charset. */
                do_insert_string_select_ch(bsmsg, buffer, string, len,
                                           fname);
        }
        g_free(string);
    }

    /* Use the same folder as for attachments. */
    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir = gtk_file_chooser_get_current_folder(fc);

    gtk_widget_destroy(selector);
    g_free(fname);
}

static void
include_file_cb(GtkAction * action, BalsaSendmsg * bsmsg)
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
    bsmsg->state = SENDMSG_STATE_MODIFIED;
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

static void
sw_set_header_from_path(LibBalsaMessage * message, const gchar * header,
                        const gchar * path, const gchar * error_format)
{
    gchar *content = NULL;
    GError *err = NULL;

    if (path && !(content =
                  libbalsa_get_header_from_path(header, path, NULL,
                                                &err))) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             error_format, path, err->message);
        g_error_free(err);
    }

    libbalsa_message_set_user_header(message, header, content);
    g_free(content);
}

static const gchar *
sw_required_charset(BalsaSendmsg * bsmsg, const gchar * text)
{
    const gchar *charset = "us-ascii";

    if (libbalsa_text_attr_string(text)) {
        charset = sw_preferred_charset(bsmsg);
        if (!sw_can_convert(text, -1, charset, "UTF-8", NULL))
            charset = "UTF-8";
    }

    return charset;
}

static LibBalsaMessage *
bsmsg2message(BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;
    gchar *tmp;
    GtkTextIter start, end;
    LibBalsaIdentity *ident = bsmsg->ident;
#if HAVE_GTKSOURCEVIEW
    GtkTextBuffer *buffer, *buffer2;
    GtkTextIter iter;
#endif                          /* HAVE_GTKSOURCEVIEW */

    message = libbalsa_message_new();

    message->headers->from = internet_address_list_prepend(NULL, ident->ia);

    tmp = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->subject[1]), 0, -1);
    strip_chars(tmp, "\r\n");
    libbalsa_message_set_subject(message, tmp);
    g_free(tmp);

    message->headers->to_list =
        libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");

    message->headers->cc_list =
        libbalsa_address_view_get_list(bsmsg->recipient_view, "Cc:");
    
    message->headers->bcc_list =
        libbalsa_address_view_get_list(bsmsg->recipient_view, "Bcc:");


    /* get the fcc-box from the option menu widget */
    bsmsg->fcc_url =
        g_strdup(balsa_mblist_mru_option_menu_get(bsmsg->fcc[1]));

#if !defined(ENABLE_TOUCH_UI)
    message->headers->reply_to =
        libbalsa_address_view_get_list(bsmsg->replyto_view, "Reply To:");
#endif

    if (bsmsg->req_dispnotify)
	libbalsa_message_set_dispnotify(message, ident->ia);

    sw_set_header_from_path(message, "Face", ident->face,
            /* Translators: please do not translate Face. */
                            _("Could not load Face header file %s: %s"));
    sw_set_header_from_path(message, "X-Face", ident->x_face,
            /* Translators: please do not translate Face. */
                            _("Could not load X-Face header file %s: %s"));

    message->references = bsmsg->references;
    bsmsg->references = NULL; /* steal it */

    if (bsmsg->in_reply_to)
        message->in_reply_to =
            g_list_prepend(NULL, g_strdup(bsmsg->in_reply_to));

    body = libbalsa_message_body_new(message);

    /* Get the text from the buffer. First make sure it's wrapped. */
    if (!bsmsg->flow)
	sw_wrap_body(bsmsg);
    /* Copy it to buffer2, so we can change it without changing the
     * display. */
#if HAVE_GTKSOURCEVIEW
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    buffer2 =
         gtk_text_buffer_new(gtk_text_buffer_get_tag_table(buffer));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_get_start_iter(buffer2, &iter);
    gtk_text_buffer_insert_range(buffer2, &iter, &start, &end);
    if (bsmsg->flow)
	libbalsa_prepare_delsp(buffer2);
    gtk_text_buffer_get_bounds(buffer2, &start, &end);
    body->buffer = gtk_text_iter_get_text(&start, &end);
    g_object_unref(buffer2);
#else                           /* HAVE_GTKSOURCEVIEW */
    sw_buffer_save(bsmsg);
    if (bsmsg->flow)
	libbalsa_prepare_delsp(bsmsg->buffer2);
    gtk_text_buffer_get_bounds(bsmsg->buffer2, &start, &end);
    body->buffer = gtk_text_iter_get_text(&start, &end);
#endif                          /* HAVE_GTKSOURCEVIEW */
    if (bsmsg->flow)
	body->buffer =
	    libbalsa_wrap_rfc2646(body->buffer, balsa_app.wraplength,
                                  TRUE, FALSE, TRUE);
#if !HAVE_GTKSOURCEVIEW
    /* Disable undo and redo, because buffer2 was changed. */
    sw_buffer_set_undo(bsmsg, FALSE, FALSE);
#endif                          /* HAVE_GTKSOURCEVIEW */

    body->charset = g_strdup(sw_required_charset(bsmsg, body->buffer));
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

/* ask the user for a subject */
static gboolean
subject_not_empty(BalsaSendmsg * bsmsg)
{
    const gchar *subj;
    GtkWidget *no_subj_dialog;
    GtkWidget *dialog_vbox;
    GtkWidget *hbox;
    GtkWidget *image;
    GtkWidget *vbox;
    gchar *text_str;
    GtkWidget *label;
    GtkWidget *subj_entry;
    GtkWidget *dialog_action_area;
    GtkWidget *cnclbutton;
    GtkWidget *okbutton;
    GtkWidget *alignment;
    gint response;

    /* read the subject widget and verify that it is contains something else
       than spaces */
    subj = gtk_entry_get_text(GTK_ENTRY(bsmsg->subject[1]));
    if (subj) {
	const gchar *p = subj;

	while (*p && g_unichar_isspace(g_utf8_get_char(p)))
	    p = g_utf8_next_char(p);
	if (*p != '\0')
	    return TRUE;
    }
	    
    /* build the dialog */
    no_subj_dialog = gtk_dialog_new ();
    gtk_container_set_border_width (GTK_CONTAINER (no_subj_dialog), 6);
    gtk_window_set_modal (GTK_WINDOW (no_subj_dialog), TRUE);
    gtk_window_set_resizable (GTK_WINDOW (no_subj_dialog), FALSE);
    gtk_window_set_type_hint (GTK_WINDOW (no_subj_dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_dialog_set_has_separator (GTK_DIALOG (no_subj_dialog), FALSE);

    dialog_vbox = GTK_DIALOG (no_subj_dialog)->vbox;

    hbox = gtk_hbox_new (FALSE, 12);
    gtk_box_pack_start (GTK_BOX (dialog_vbox), hbox, TRUE, TRUE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);

    image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_DIALOG);
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
    gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0);

    vbox = gtk_vbox_new (FALSE, 12);
    gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

    text_str = g_strdup_printf("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
			       _("You did not specify a subject for this message"),
			       _("If you would like to provide one, enter it below."));
    label = gtk_label_new (text_str);
    g_free(text_str);
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
    gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0);

    hbox = gtk_hbox_new (FALSE, 6);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new (_("Subject:"));
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    subj_entry = gtk_entry_new ();
    gtk_entry_set_text(GTK_ENTRY(subj_entry), _("(no subject)"));
    gtk_box_pack_start (GTK_BOX (hbox), subj_entry, TRUE, TRUE, 0);
    gtk_entry_set_activates_default (GTK_ENTRY (subj_entry), TRUE);

    dialog_action_area = GTK_DIALOG (no_subj_dialog)->action_area;
    gtk_button_box_set_layout (GTK_BUTTON_BOX (dialog_action_area), GTK_BUTTONBOX_END);

    cnclbutton = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
    gtk_dialog_add_action_widget (GTK_DIALOG (no_subj_dialog), cnclbutton, GTK_RESPONSE_CANCEL);
    GTK_WIDGET_SET_FLAGS (cnclbutton, GTK_CAN_DEFAULT);

    okbutton = gtk_button_new ();
    gtk_dialog_add_action_widget (GTK_DIALOG (no_subj_dialog), okbutton, GTK_RESPONSE_OK);
    GTK_WIDGET_SET_FLAGS (okbutton, GTK_CAN_DEFAULT);
    gtk_dialog_set_default_response(GTK_DIALOG (no_subj_dialog),
                                    GTK_RESPONSE_OK);

    alignment = gtk_alignment_new (0.5, 0.5, 0, 0);
    gtk_container_add (GTK_CONTAINER (okbutton), alignment);

    hbox = gtk_hbox_new (FALSE, 2);
    gtk_container_add (GTK_CONTAINER (alignment), hbox);

    image = gtk_image_new_from_stock (BALSA_PIXMAP_SEND, GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic (_("_Send"));
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
    GTK_WIDGET_SET_FLAGS (label, GTK_CAN_FOCUS);
    GTK_WIDGET_SET_FLAGS (label, GTK_CAN_DEFAULT);

    gtk_widget_grab_focus (subj_entry);
    gtk_editable_select_region(GTK_EDITABLE(subj_entry), 0, -1);
    gtk_widget_show_all(dialog_vbox);

    response = gtk_dialog_run(GTK_DIALOG(no_subj_dialog));

    /* always set the current string in the subject entry */
    gtk_entry_set_text(GTK_ENTRY(bsmsg->subject[1]),
		       gtk_entry_get_text(GTK_ENTRY(subj_entry)));
    gtk_widget_destroy(no_subj_dialog);

    return response == GTK_RESPONSE_OK;
}

#ifdef HAVE_GPGME
static gboolean
check_suggest_encryption(BalsaSendmsg * bsmsg)
{
    InternetAddressList * ia_list;
    gboolean can_encrypt;
    InternetAddressList * from_list;
    InternetAddressList * cc_list;
    gpgme_protocol_t protocol;

    /* check if the user wants to see the message */
    if (!bsmsg->ident->warn_send_plain)
	return TRUE;

    /* nothing to do if encryption is already enabled */
    if ((bsmsg->gpg_mode & LIBBALSA_PROTECT_ENCRYPT) != 0)
	return TRUE;

    /* we can not encrypt if we have bcc recipients */
    if ((ia_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "Bcc:"))) {
	internet_address_list_destroy(ia_list);
	return TRUE;
    }

    /* collect all to and cc recipients */
    ia_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");
    cc_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "Cc:");
    from_list = internet_address_list_prepend(NULL, bsmsg->ident->ia);
    protocol = bsmsg->gpg_mode & LIBBALSA_PROTECT_SMIMEV3 ?
	GPGME_PROTOCOL_CMS : GPGME_PROTOCOL_OpenPGP;
    can_encrypt = libbalsa_can_encrypt_for_all(from_list, protocol) &
	libbalsa_can_encrypt_for_all(ia_list, protocol) &
	libbalsa_can_encrypt_for_all(cc_list, protocol);
    internet_address_list_destroy(from_list);
    internet_address_list_destroy(ia_list);
    internet_address_list_destroy(cc_list);

    /* ask the user if we could encrypt this message */
    if (can_encrypt) {
	GtkWidget *dialog;
	gint choice;
	gchar * message;
	GtkWidget *dialog_action_area;
	GtkWidget *button;
	GtkWidget *alignment;
	GtkWidget *hbox;
	GtkWidget *image;
	GtkWidget *label;

	message =
	    g_strdup_printf(_("You did not select encryption for this message, although "
			      "%s public keys are available for all recipients. In order "
			      "to protect your privacy, the message could be %s encrypted."),
			    gpgme_get_protocol_name(protocol),
			    gpgme_get_protocol_name(protocol));
	dialog = gtk_message_dialog_new
	    (GTK_WINDOW(bsmsg->window),
	     GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
	     GTK_MESSAGE_QUESTION,
	     GTK_BUTTONS_NONE,
	     message);

	dialog_action_area = GTK_DIALOG(dialog)->action_area;
	gtk_button_box_set_layout(GTK_BUTTON_BOX(dialog_action_area), GTK_BUTTONBOX_END);
 
	button = gtk_button_new();
	gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, GTK_RESPONSE_YES);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_widget_grab_focus(button);
	alignment = gtk_alignment_new (0.5, 0.5, 0, 0);
	gtk_container_add(GTK_CONTAINER(button), alignment);

	hbox = gtk_hbox_new(FALSE, 2);
	gtk_container_add(GTK_CONTAINER(alignment), hbox);
	image = gtk_image_new_from_stock(BALSA_PIXMAP_GPG_ENCRYPT, GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	label = gtk_label_new_with_mnemonic(_("Send _encrypted"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show_all(button);

	button = gtk_button_new();
	gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, GTK_RESPONSE_NO);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	alignment = gtk_alignment_new (0.5, 0.5, 0, 0);
	gtk_container_add(GTK_CONTAINER(button), alignment);

	hbox = gtk_hbox_new(FALSE, 2);
	gtk_container_add(GTK_CONTAINER(alignment), hbox);
	image = gtk_image_new_from_stock(BALSA_PIXMAP_SEND, GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	label = gtk_label_new_with_mnemonic(_("Send _plain"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show_all(button);

	button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_widget_show(button);
	gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, GTK_RESPONSE_CANCEL);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);

	choice = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	if (choice == GTK_RESPONSE_YES)
	    bsmsg_setup_gpg_ui_by_mode(bsmsg, bsmsg->gpg_mode | LIBBALSA_PROTECT_ENCRYPT);
	else if (choice == GTK_RESPONSE_CANCEL || choice == GTK_RESPONSE_DELETE_EVENT)
	    return FALSE;
    }

    return TRUE;
}
#endif

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
    GError * error = NULL;

    if (!gtk_action_group_get_sensitive(bsmsg->ready_action_group))
	return FALSE;

    if(!subject_not_empty(bsmsg))
	return FALSE;

#ifdef HAVE_GPGME
    if (!check_suggest_encryption(bsmsg))
	return FALSE;

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
					bsmsg->flow, &error);
    else 
        result = libbalsa_message_send(message, balsa_app.outbox, fcc,
                                       balsa_find_sentbox_by_url,
				       bsmsg->ident->smtp_server,
                                       bsmsg->flow, balsa_app.debug, &error);
#else
    if(queue_only)
	result = libbalsa_message_queue(message, balsa_app.outbox, fcc,
					bsmsg->flow, &error);
    else 
        result = libbalsa_message_send(message, balsa_app.outbox, fcc,
                                       balsa_find_sentbox_by_url,
				       bsmsg->flow, balsa_app.debug, &error); 
#endif
    if (result == LIBBALSA_MESSAGE_CREATE_OK) {
	if (bsmsg->parent_message && bsmsg->parent_message->mailbox
            && !bsmsg->parent_message->mailbox->readonly)
	    libbalsa_message_reply(bsmsg->parent_message);
        sw_delete_draft(bsmsg);
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
#ifdef HAVE_GPGME
	case LIBBALSA_MESSAGE_SIGN_ERROR:
            msg = _("Message could not be signed"); break;
	case LIBBALSA_MESSAGE_ENCRYPT_ERROR:
            msg = _("Message could not be encrypted"); break;
#endif
        }
	if (error)
	    balsa_information_parented(GTK_WINDOW(bsmsg->window),
				       LIBBALSA_INFORMATION_WARNING,
				       _("Send failed: %s\n%s"), msg,
				       error->message);
	else
	    balsa_information_parented(GTK_WINDOW(bsmsg->window),
				       LIBBALSA_INFORMATION_WARNING,
				       _("Send failed: %s"), msg);
	return FALSE;
    }
    g_clear_error(&error);

    gtk_widget_destroy(bsmsg->window);

    return TRUE;
}


/* "send message" menu callback */
static void
toolbar_send_message_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    send_message_handler(bsmsg, balsa_app.always_queue_sent_mail);
}

static void
send_message_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    send_message_handler(bsmsg, FALSE);
}


static void
queue_message_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    send_message_handler(bsmsg, TRUE);
}

static gboolean
message_postpone(BalsaSendmsg * bsmsg)
{
    gboolean successp;
    LibBalsaMessage *message;
    GPtrArray *headers;

    /* Silent fallback to UTF-8 */
    message = bsmsg2message(bsmsg);

    /* sufficiently long for fcc, mdn, gpg */
    headers = g_ptr_array_new();
    if (bsmsg->fcc_url) {
        g_ptr_array_add(headers, g_strdup("X-Balsa-Fcc"));
        g_ptr_array_add(headers, g_strdup(bsmsg->fcc_url));
    }
    g_ptr_array_add(headers, g_strdup("X-Balsa-MDN"));
    g_ptr_array_add(headers, g_strdup_printf("%d", bsmsg->req_dispnotify));
#ifdef HAVE_GPGME
    g_ptr_array_add(headers, g_strdup("X-Balsa-Crypto"));
    g_ptr_array_add(headers, g_strdup_printf("%d", bsmsg->gpg_mode));
#endif

#if HAVE_GTKSPELL
    if (sw_get_active(bsmsg, "CheckSpelling")) {
        g_ptr_array_add(headers, g_strdup("X-Balsa-Lang"));
        g_ptr_array_add(headers, g_strdup(bsmsg->spell_check_lang));
    }
#else  /* HAVE_GTKSPELL */
    g_ptr_array_add(headers, g_strdup("X-Balsa-Lang"));
    g_ptr_array_add(headers, g_strdup(bsmsg->spell_check_lang));
#endif /* HAVE_GTKSPELL */
    g_ptr_array_add(headers, g_strdup("X-Balsa-Format"));
    g_ptr_array_add(headers, g_strdup(bsmsg->flow ? "Flowed" : "Fixed"));
    g_ptr_array_add(headers, NULL);

    if ((bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL ||
        bsmsg->type == SEND_REPLY_GROUP))
	successp = libbalsa_message_postpone(message, balsa_app.draftbox,
                                             bsmsg->parent_message,
                                             (gchar **) headers->pdata,
                                             bsmsg->flow);
    else
	successp = libbalsa_message_postpone(message, balsa_app.draftbox, 
                                             NULL,
                                             (gchar **) headers->pdata,
                                             bsmsg->flow);
    g_ptr_array_foreach(headers, (GFunc) g_free, NULL);
    g_ptr_array_free(headers, TRUE);

    if(successp)
        sw_delete_draft(bsmsg);
    else
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not postpone message."));

    g_object_unref(G_OBJECT(message));
    return successp;
}

/* "postpone message" menu callback */
static void
postpone_message_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    if (!gtk_action_group_get_sensitive(bsmsg->ready_action_group)) {
        if(message_postpone(bsmsg)) {
            balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                       LIBBALSA_INFORMATION_MESSAGE,
                                       _("Message postponed."));
            gtk_widget_destroy(bsmsg->window);
        } else {
            balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                       LIBBALSA_INFORMATION_MESSAGE,
                                       _("Could not postpone message."));
        }
    }
}

static void
save_message_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    if (sw_save_draft(bsmsg))
        bsmsg->state = SENDMSG_STATE_CLEAN;
}

#ifdef HAVE_GTK_PRINT
#if !defined(ENABLE_TOUCH_UI)
static void
page_setup_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message;

    message = bsmsg2message(bsmsg);
    message_print_page_setup(GTK_WINDOW(bsmsg->window));
    g_object_unref(message);
}
#endif /* ENABLE_TOUCH_UI */
#endif

static void
print_message_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message;

    message = bsmsg2message(bsmsg);
    message_print(message, GTK_WINDOW(bsmsg->window));
    g_object_unref(message);
}

/*
 * Signal handlers for updating the cursor when text is inserted.
 * The "insert-text" signal is emitted before the insertion, so we
 * create a mark at the insertion point. 
 * The "changed" signal is emitted after the insertion, and we move the
 * cursor to the end of the inserted text.
 * This achieves nothing if the text was typed, as the cursor is moved
 * there anyway; if the text is copied by drag and drop or center-click,
 * this deselects any selected text and places the cursor at the end of
 * the insertion.
 */
static void
sw_buffer_insert_text(GtkTextBuffer * buffer, GtkTextIter * iter,
                      const gchar * text, gint len, BalsaSendmsg * bsmsg)
{
    bsmsg->insert_mark =
        gtk_text_buffer_create_mark(buffer, "balsa-insert-mark", iter,
                                    FALSE);
#if !HAVE_GTKSOURCEVIEW
    /* If this insertion is not from the keyboard, or if we just undid
     * something, save the current buffer for undo. */
    if (len > 1 /* Not keyboard? */
        || !sw_get_sensitive(bsmsg, "Undo"))
        sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */
}

static void
sw_buffer_changed(GtkTextBuffer * buffer, BalsaSendmsg * bsmsg)
{
    if (!bsmsg->flow) {
        if (bsmsg->wrap_timeout_id)
            g_source_remove(bsmsg->wrap_timeout_id);
        bsmsg->wrap_timeout_id =
            g_timeout_add(500, (GSourceFunc) sw_wrap_timeout_cb, bsmsg);
    }

    if (bsmsg->insert_mark) {
        GtkTextIter iter;

        gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                         bsmsg->insert_mark);
        gtk_text_buffer_place_cursor(buffer, &iter);
        bsmsg->insert_mark = NULL;
    }

    bsmsg->state = SENDMSG_STATE_MODIFIED;
}

#if !HAVE_GTKSOURCEVIEW
static void
sw_buffer_delete_range(GtkTextBuffer * buffer, GtkTextIter * start,
                       GtkTextIter * end, BalsaSendmsg * bsmsg)
{
    if (gtk_text_iter_get_offset(end) >
        gtk_text_iter_get_offset(start) + 1)
        sw_buffer_save(bsmsg);
}
#endif                          /* HAVE_GTKSOURCEVIEW */

/*
 * Helpers for the undo and redo buffers.
 */
static void
sw_buffer_signals_connect(BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    bsmsg->insert_text_sig_id =
        g_signal_connect(buffer, "insert-text",
                         G_CALLBACK(sw_buffer_insert_text), bsmsg);
    bsmsg->changed_sig_id =
        g_signal_connect(buffer, "changed",
                         G_CALLBACK(sw_buffer_changed), bsmsg);
#if !HAVE_GTKSOURCEVIEW
    bsmsg->delete_range_sig_id =
        g_signal_connect(buffer, "delete-range",
                         G_CALLBACK(sw_buffer_delete_range), bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */
}

#if !HAVE_GTKSOURCEVIEW || !HAVE_GTKSPELL
static void
sw_buffer_signals_disconnect(BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    g_signal_handler_disconnect(buffer, bsmsg->changed_sig_id);
#if !HAVE_GTKSOURCEVIEW
    g_signal_handler_disconnect(buffer, bsmsg->delete_range_sig_id);
#endif                          /* HAVE_GTKSOURCEVIEW */
    g_signal_handler_disconnect(buffer, bsmsg->insert_text_sig_id);
}
#endif                          /* !HAVE_GTKSOURCEVIEW || !HAVE_GTKSPELL */

#if !HAVE_GTKSOURCEVIEW
static void
sw_buffer_set_undo(BalsaSendmsg * bsmsg, gboolean undo, gboolean redo)
{
    sw_set_sensitive(bsmsg, "Undo", undo);
    sw_set_sensitive(bsmsg, "Redo", redo);
}
#endif                          /* HAVE_GTKSOURCEVIEW */

#ifdef HAVE_GTKSPELL
static void
sw_spell_attach(BalsaSendmsg * bsmsg)
{
    GtkSpell *spell;
    GError *err = NULL;

    spell = gtkspell_new_attach(GTK_TEXT_VIEW(bsmsg->text),
                                bsmsg->spell_check_lang, &err);
    if (!spell) {
        /* Should not happen, since we now check the language. */
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Error starting spell checker: %s"),
                             err->message);
        g_error_free(err);

        /* No spell checker, so deactivate the button. */
        sw_set_active(bsmsg, "CheckSpelling", FALSE);
    }
}

static gboolean
sw_spell_detach(BalsaSendmsg * bsmsg)
{
    GtkSpell *spell;

    spell = gtkspell_get_from_text_view(GTK_TEXT_VIEW(bsmsg->text));
    if (spell)
        gtkspell_detach(spell);
    
    return spell != NULL;
}
#endif                          /* HAVE_GTKSPELL */

#if !HAVE_GTKSOURCEVIEW
static void
sw_buffer_swap(BalsaSendmsg * bsmsg, gboolean undo)
{
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
#if HAVE_GTKSPELL
    gboolean had_spell;

    /* GtkSpell doesn't seem to handle setting a new buffer... */
    had_spell = sw_spell_detach(bsmsg);
#endif                          /* HAVE_GTKSPELL */

    sw_buffer_signals_disconnect(bsmsg);
    g_object_ref(G_OBJECT(buffer));
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(bsmsg->text), bsmsg->buffer2);
#if HAVE_GTKSPELL
    if (had_spell)
        sw_spell_attach(bsmsg);
#endif                          /* HAVE_GTKSPELL */
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
#endif                          /* HAVE_GTKSOURCEVIEW */

/*
 * Menu and toolbar callbacks.
 */

#if HAVE_GTKSOURCEVIEW
static void
sw_undo_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    g_signal_emit_by_name(bsmsg->text, "undo");
}

static void
sw_redo_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    g_signal_emit_by_name(bsmsg->text, "redo");
}
#else                           /* HAVE_GTKSOURCEVIEW */
static void
sw_undo_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    sw_buffer_swap(bsmsg, TRUE);
}

static void
sw_redo_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    sw_buffer_swap(bsmsg, FALSE);
}
#endif                          /* HAVE_GTKSOURCEVIEW */

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
cut_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    clipboard_helper(bsmsg, "cut-clipboard");
}

static void
copy_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    clipboard_helper(bsmsg, "copy-clipboard");
}

static void
paste_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    clipboard_helper(bsmsg, "paste-clipboard");
}

/*
 * More menu callbacks.
 */
static void
select_all_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    balsa_window_select_all(GTK_WINDOW(bsmsg->window));
}

static void
wrap_body_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
#if !HAVE_GTKSOURCEVIEW
    sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */
    sw_wrap_body(bsmsg);
}

static void
reflow_selected_cb(GtkAction * action, BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
#if GLIB_CHECK_VERSION(2, 14, 0)
    GRegex *rex;
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
    regex_t rex;
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */

    if (!bsmsg->flow)
	return;

#if GLIB_CHECK_VERSION(2, 14, 0)
    if (!(rex = balsa_quote_regex_new()))
        return;
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
    if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED)) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Could not compile %s"),
			  _("Quoted Text Regular Expression"));
	return;
    }
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */

#if !HAVE_GTKSOURCEVIEW
    sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */

    text_view = GTK_TEXT_VIEW(bsmsg->text);
    buffer = gtk_text_view_get_buffer(text_view);
    sw_buffer_signals_block(bsmsg, buffer);
#if GLIB_CHECK_VERSION(2, 14, 0)
    libbalsa_unwrap_selection(buffer, rex);
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
    libbalsa_unwrap_selection(buffer, &rex);
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
    sw_buffer_signals_unblock(bsmsg, buffer);

    bsmsg->state = SENDMSG_STATE_MODIFIED;
    gtk_text_view_scroll_to_mark(text_view,
				 gtk_text_buffer_get_insert(buffer),
				 0, FALSE, 0, 0);

#if GLIB_CHECK_VERSION(2, 14, 0)
    g_regex_unref(rex);
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
    regfree(&rex);
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
}

/* To field "changed" signal callback. */
static void
check_readiness(BalsaSendmsg * bsmsg)
{
    gboolean ready =
        libbalsa_address_view_n_addresses(bsmsg->recipient_view) > 0;
#if !defined(ENABLE_TOUCH_UI)
    if (ready
        && libbalsa_address_view_n_addresses(bsmsg->replyto_view) < 0)
        ready = FALSE;
#endif

    gtk_action_group_set_sensitive(bsmsg->ready_action_group, ready);
}

static const gchar * const header_action_names[] = {
    "From", "Recipients", "ReplyTo", "Fcc"
};

/* toggle_entry:
   auxiliary function for "header show/hide" toggle menu entries.
   saves the show header configuration.
 */
static void
toggle_entry(GtkToggleAction * toggle_action, BalsaSendmsg * bsmsg,
             GtkWidget * entry[])
{
    if (gtk_toggle_action_get_active(toggle_action)) {
        gtk_widget_show_all(entry[0]);
        gtk_widget_show_all(entry[1]);
        gtk_widget_grab_focus(entry[1]);
    } else {
        gtk_widget_hide(entry[0]);
        gtk_widget_hide(entry[1]);
    }

    if (bsmsg->update_config) { /* then save the config */
        GString *str = g_string_new(NULL);
        unsigned i;

        for (i = 0; i < G_N_ELEMENTS(header_action_names); i++) {
            if (sw_get_active(bsmsg, header_action_names[i])) {
                if (str->len > 0)
                    g_string_append_c(str, ' ');
                g_string_append(str, header_action_names[i]);
            }
        }
        g_free(balsa_app.compose_headers);
        balsa_app.compose_headers = g_string_free(str, FALSE);
    }
}

static void
toggle_from_cb(GtkToggleAction * action, BalsaSendmsg * bsmsg)
{
    toggle_entry(action, bsmsg, bsmsg->from);
}

static void
toggle_recipients_cb(GtkToggleAction * action, BalsaSendmsg * bsmsg)
{
    toggle_entry(action, bsmsg, bsmsg->recipients);
}

static void
toggle_replyto_cb(GtkToggleAction * action, BalsaSendmsg * bsmsg)
{
    toggle_entry(action, bsmsg, bsmsg->replyto);
}

static void
toggle_fcc_cb(GtkToggleAction * action, BalsaSendmsg * bsmsg)
{
    toggle_entry(action, bsmsg, bsmsg->fcc);
}

static void
toggle_reqdispnotify_cb(GtkToggleAction * action,
                        BalsaSendmsg * bsmsg)
{
    bsmsg->req_dispnotify = gtk_toggle_action_get_active(action);
}

static void
toggle_format_cb(GtkToggleAction * toggle_action, BalsaSendmsg * bsmsg)
{
    bsmsg->flow = gtk_toggle_action_get_active(toggle_action);
    sw_set_sensitive(bsmsg, "Reflow", bsmsg->flow);
}

#ifdef HAVE_GPGME
static void
toggle_gpg_helper(GtkToggleAction * action, BalsaSendmsg * bsmsg,
                  guint mask)
{
    gboolean butval, radio_on;

    butval = gtk_toggle_action_get_active(action);
    if (butval)
        bsmsg->gpg_mode |= mask;
    else
        bsmsg->gpg_mode &= ~mask;

    radio_on = (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) > 0;
#if !defined(ENABLE_TOUCH_UI)
    gtk_action_group_set_sensitive(bsmsg->gpg_action_group, radio_on);
#endif                          /* ENABLE_TOUCH_UI */
}

static void
toggle_sign_cb(GtkToggleAction * action, BalsaSendmsg * bsmsg)
{
    toggle_gpg_helper(action, bsmsg, LIBBALSA_PROTECT_SIGN);
}

static void
toggle_encrypt_cb(GtkToggleAction * action, BalsaSendmsg * bsmsg)
{
    toggle_gpg_helper(action, bsmsg, LIBBALSA_PROTECT_ENCRYPT);
}

#if !defined(ENABLE_TOUCH_UI)
static void
gpg_mode_radio_cb(GtkRadioAction * action, GtkRadioAction * current,
                  BalsaSendmsg * bsmsg)
{
    guint rfc_flag = gtk_radio_action_get_current_value(action);

    bsmsg->gpg_mode =
        (bsmsg->gpg_mode & ~LIBBALSA_PROTECT_PROTOCOL) | rfc_flag;
}
#endif                          /* ENABLE_TOUCH_UI */
#endif                          /* HAVE_GPGME */


/* init_menus:
   performs the initial menu setup: shown headers as well as correct
   message charset.
*/
static void
init_menus(BalsaSendmsg * bsmsg)
{
    unsigned i;

    for (i = 0; i < G_N_ELEMENTS(header_action_names); i++) {
        gboolean found =
            libbalsa_find_word(header_action_names[i],
                               balsa_app.compose_headers);
        /* This action is initially active, so if found is FALSE,
         * setting it inactive will trigger the callback, which will
         * hide the address line. */
        sw_set_active(bsmsg, header_action_names[i], found);
    }

    /* gray 'send' and 'postpone' */
    check_readiness(bsmsg);
}

/* set_locale:
   bsmsg is the compose window,
   idx - corresponding entry index in locales.
*/

static void
set_locale(BalsaSendmsg * bsmsg, gint idx)
{
    if (locales[idx].locale && *locales[idx].locale)
        bsmsg->spell_check_lang = locales[idx].locale;
#if HAVE_GTKSPELL

    if (sw_spell_detach(bsmsg))
        sw_spell_attach(bsmsg);
#endif                          /* HAVE_GTKSPELL */
}

#if HAVE_GTKSPELL
/* spell_check_menu_cb
 *
 * Toggle the spell checker
 */
static void
spell_check_menu_cb(GtkToggleAction * action, BalsaSendmsg * bsmsg)
{
    if ((balsa_app.spell_check_active =
         gtk_toggle_action_get_active(action)))
        sw_spell_attach(bsmsg);
    else
        sw_spell_detach(bsmsg);
}

#else                           /* HAVE_GTKSPELL */
/* spell_check_cb
 * 
 * Start the spell check
 * */
static void
spell_check_cb(GtkAction * action, BalsaSendmsg * bsmsg)
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
    balsa_spell_check_set_language(sc, bsmsg->spell_check_lang);

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

    balsa_spell_check_start(sc, GTK_WINDOW(bsmsg->window));
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
#endif                          /* HAVE_GTKSPELL */

static void
lang_set_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w))) {
        gint i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),
                                                   BALSA_LANGUAGE_MENU_POS));
        set_locale(bsmsg, i);
#if HAVE_GTKSPELL
        g_free(balsa_app.spell_check_lang);
        balsa_app.spell_check_lang = g_strdup(bsmsg->spell_check_lang);
        sw_set_active(bsmsg, "CheckSpelling", TRUE);
#endif                          /* HAVE_GTKSPELL */
    }
}

/* sendmsg_window_new_from_list:
 * like sendmsg_window_new, but takes a GList of messages, instead of a
 * single message;
 * called by compose_from_list (balsa-index.c)
 */
BalsaSendmsg *
sendmsg_window_new_from_list(LibBalsaMailbox * mailbox,
                             GArray * selected, SendType type)
{
    BalsaSendmsg *bsmsg;
    LibBalsaMessage *message;
    GtkTextBuffer *buffer;
    guint i;
    guint msgno = g_array_index(selected, guint, 0);

    g_return_val_if_fail(selected->len > 0, NULL);

    message = libbalsa_mailbox_get_message(mailbox, msgno);
    if (!message)
        return NULL;

    switch(type) {
    case SEND_FORWARD_ATTACH:
    case SEND_FORWARD_INLINE:
        bsmsg = sendmsg_window_forward(mailbox, msgno,
                                       type == SEND_FORWARD_ATTACH);
        break;
    default:
        g_assert_not_reached(); /* since it hardly makes sense... */
        bsmsg = NULL; /** silence invalid warnings */

    }
    g_object_unref(message);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    for (i = 1; i < selected->len; i++) {
        LibBalsaMessage *message;

	msgno = g_array_index(selected, guint, i);
        message = libbalsa_mailbox_get_message(mailbox, msgno);
        if (!message)
            continue;

        if (type == SEND_FORWARD_ATTACH)
            attach_message(bsmsg, message);
        else if (type == SEND_FORWARD_INLINE) {
            GString *body =
                quote_message_body(bsmsg, message, QUOTE_NOPREFIX);
            libbalsa_insert_with_url(buffer, body->str, NULL, NULL, NULL);
            g_string_free(body, TRUE);
        }
        g_object_unref(message);
    }

    bsmsg->state = SENDMSG_STATE_CLEAN;

    return bsmsg;
}

/* set_list_post_address:
 * look for the address for posting messages to a list */
static void
set_list_post_address(BalsaSendmsg * bsmsg)
{
    LibBalsaMessage *message =
        bsmsg->parent_message ?
        bsmsg->parent_message : bsmsg->draft_message;
    InternetAddressList *mailing_list_address;
    const gchar *header;

    mailing_list_address =
	libbalsa_mailbox_get_mailing_list_address(message->mailbox);
    if (mailing_list_address) {
        libbalsa_address_view_set_from_list(bsmsg->recipient_view, "To:",
                                            mailing_list_address);
        internet_address_list_destroy(mailing_list_address);
        return;
    }

    if ((header = libbalsa_message_get_user_header(message, "list-post"))
	&& set_list_post_rfc2369(bsmsg, header))
	return;

    /* we didn't find "list-post", so try some nonstandard
     * alternatives: */

    if ((header = libbalsa_message_get_user_header(message, "x-beenthere"))
        || (header =
            libbalsa_message_get_user_header(message, "x-mailing-list")))
        libbalsa_address_view_set_from_string(bsmsg->recipient_view, "To:",
                                              header);
}

/* set_list_post_rfc2369:
 * look for "List-Post:" header, and get the address */
static gboolean
set_list_post_rfc2369(BalsaSendmsg * bsmsg, const gchar * url)
{
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
	const gchar *close = strchr(++url, '>');
	if (!close)
	    /* broken syntax--break and return FALSE */
	    break;
	if (g_ascii_strncasecmp(url, "mailto:", 7) == 0) {
	    /* we support mailto! */
            gchar *field = g_strndup(&url[7], close - &url[7]);
	    sendmsg_window_process_url(field, sendmsg_window_set_field,
                                       bsmsg);
            g_free(field);
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
static const gchar *
rfc2822_skip_comments(const gchar * str)
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
    InternetAddressList *list;
    gchar *to_string;
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

    list = libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");
    to_string = internet_address_list_to_string(list, FALSE);
    title = g_strdup_printf(title_format, to_string,
                            gtk_entry_get_text(GTK_ENTRY(bsmsg->subject[1])));
    g_free(to_string);
    gtk_window_set_title(GTK_WINDOW(bsmsg->window), title);
    g_free(title);
}

#ifdef HAVE_GPGME
static void
bsmsg_update_gpg_ui_on_ident_change(BalsaSendmsg * bsmsg,
                                    LibBalsaIdentity * ident)
{
    /* do nothing if we don't support crypto */
    if (!balsa_app.has_openpgp && !balsa_app.has_smime)
        return;

    /* preset according to identity */
    bsmsg->gpg_mode = 0;
    if (ident->always_trust)
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_ALWAYS_TRUST;

    sw_set_active(bsmsg, "SignMessage", ident->gpg_sign);
    if (ident->gpg_sign)
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_SIGN;

    sw_set_active(bsmsg, "EncryptMessage", ident->gpg_encrypt);
    if (ident->gpg_encrypt)
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_ENCRYPT;

#if !defined(ENABLE_TOUCH_UI)
    switch (ident->crypt_protocol) {
    case LIBBALSA_PROTECT_OPENPGP:
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_OPENPGP;
        sw_set_active(bsmsg, "OldOpenPgpMode", TRUE);
        break;
#ifdef HAVE_SMIME
    case LIBBALSA_PROTECT_SMIMEV3:
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_SMIMEV3;
        sw_set_active(bsmsg, "SMimeMode", TRUE);
        break;
#endif
    case LIBBALSA_PROTECT_RFC3156:
    default:
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_RFC3156;
        sw_set_active(bsmsg, "MimeMode", TRUE);
    }
#endif                          /* ENABLE_TOUCH_UI */
}

static void
bsmsg_setup_gpg_ui(BalsaSendmsg *bsmsg)
{
#if !defined(ENABLE_TOUCH_UI)
    gtk_action_group_set_sensitive(bsmsg->gpg_action_group, FALSE);
#endif                          /* ENABLE_TOUCH_UI */

    /* make everything insensitive if we don't have crypto support */
    if (!balsa_app.has_openpgp && !balsa_app.has_smime) {
        sw_set_active(bsmsg, "SignMessage", FALSE);
        sw_set_sensitive(bsmsg, "SignMessage", FALSE);
        sw_set_active(bsmsg, "EncryptMessage", FALSE);
        sw_set_sensitive(bsmsg, "EncryptMessage", FALSE);
    }
}

static void
bsmsg_setup_gpg_ui_by_mode(BalsaSendmsg *bsmsg, gint mode)
{
    /* do nothing if we don't support crypto */
    if (!balsa_app.has_openpgp && !balsa_app.has_smime)
	return;

    bsmsg->gpg_mode = mode;
    sw_set_active(bsmsg, "SignMessage", mode & LIBBALSA_PROTECT_SIGN);
    sw_set_active(bsmsg, "EncryptMessage", mode & LIBBALSA_PROTECT_ENCRYPT);

#ifdef HAVE_SMIME
    if (mode & LIBBALSA_PROTECT_SMIMEV3)
        sw_set_active(bsmsg, "SMimeMode", TRUE);
    else 
#endif
    if (mode & LIBBALSA_PROTECT_OPENPGP)
        sw_set_active(bsmsg, "OldOpenPgpMode", TRUE);
    else
        sw_set_active(bsmsg, "MimeMode", TRUE);
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
