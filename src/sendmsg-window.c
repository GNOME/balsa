/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-2016 Stuart Parmenter and others, see AUTHORS file.
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* FONT SELECTION DISCUSSION:
   We use pango now.
   Locale data is then used exclusively for the spelling checking.
*/


#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "sendmsg-window.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define GNOME_PAD_SMALL    4
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <ctype.h>
#include <glib.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include "application-helpers.h"
#include "libbalsa.h"
#include "misc.h"
#include "send.h"
#include "html.h"

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "balsa-icons.h"

#include "missing.h"
#include "ab-window.h"
#include "address-view.h"
#include "print.h"
#include "geometry-manager.h"

#if HAVE_GTKSPELL
#include <gtkspell/gtkspell.h>
#elif HAVE_GSPELL
#include <gspell/gspell.h>
#else                           /* HAVE_GTKSPELL */
#include <enchant.h>
#include "spell-check.h"
#endif                          /* HAVE_GTKSPELL */
#if HAVE_GTKSOURCEVIEW
#include <gtksourceview/gtksource.h>
#endif                          /* HAVE_GTKSOURCEVIEW */
#include "libbalsa-gpgme.h"
#ifdef ENABLE_AUTOCRYPT
#include "autocrypt.h"
#include "libbalsa-gpgme-keys.h"
#endif							/* ENABLE_AUTOCRYPT */

typedef struct {
    gchar *filename;
    BalsaSendmsg *bsmsg;
} balsa_edit_with_gnome_data;

typedef enum { QUOTE_HEADERS, QUOTE_ALL, QUOTE_NOPREFIX } QuoteType;

static gint message_postpone(BalsaSendmsg * bsmsg);

static void balsa_sendmsg_destroy_handler(BalsaSendmsg * bsmsg);
static void check_readiness(BalsaSendmsg * bsmsg);
static void init_menus(BalsaSendmsg *);
static void bsmsg_setup_gpg_ui(BalsaSendmsg *bsmsg);
static void bsmsg_update_gpg_ui_on_ident_change(BalsaSendmsg *bsmsg,
                                                LibBalsaIdentity *new_ident);
static void bsmsg_setup_gpg_ui_by_mode(BalsaSendmsg *bsmsg, guint mode);

#if !HAVE_GSPELL && !HAVE_GTKSPELL
static void sw_spell_check_weak_notify(BalsaSendmsg * bsmsg);
#endif                          /* !HAVE_GSPELL && !HAVE_GTKSPELL */

static void address_book_cb(LibBalsaAddressView * address_view,
                            GtkTreeRowReference * row_ref,
                            BalsaSendmsg * bsmsg);
static void address_book_response(GtkWidget * ab, gint response,
                                  LibBalsaAddressView * address_view);

static void set_locale(BalsaSendmsg * bsmsg, const gchar * locale);

static void replace_identity_signature(BalsaSendmsg* bsmsg,
                                       LibBalsaIdentity* new_ident,
                                       LibBalsaIdentity* old_ident,
                                       gint* replace_offset, gint siglen,
                                       const gchar* new_sig);
static void update_bsmsg_identity(BalsaSendmsg*, LibBalsaIdentity*);

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
#if !HAVE_GTKSOURCEVIEW || !(HAVE_GSPELL || HAVE_GTKSPELL)
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
    { "STRING",     0, TARGET_STRING },
    { "text/plain", 0, TARGET_STRING }
};

static void lang_set_cb(GtkWidget *widget, BalsaSendmsg *bsmsg);

static void bsmsg_set_subject_from_body(BalsaSendmsg * bsmsg,
                                        LibBalsaMessageBody * body,
                                        LibBalsaIdentity * ident);

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
    {"kk_KZ", "UTF-8",         N_("_Kazakh")},
    {"ko_KR", "euc-kr",        N_("_Korean")},
    {"lv_LV", "ISO-8859-13",   N_("_Latvian")},
    {"lt_LT", "ISO-8859-13",   N_("_Lithuanian")},
    {"no_NO", "ISO-8859-1",    N_("_Norwegian")},
    {"pl_PL", "ISO-8859-2",    N_("_Polish")},
    {"pt_PT", "ISO-8859-15",   N_("_Portuguese")},
    {"ro_RO", "ISO-8859-2",    N_("_Romanian")},
    {"ru_RU", "KOI8-R",        N_("_Russian")},
    {"sr_Cyrl", "ISO-8859-5",  N_("_Serbian")},
    {"sr_Latn", "ISO-8859-2",  N_("_Serbian (Latin)")},
    {"sk_SK", "ISO-8859-2",    N_("_Slovak")},
    {"es_ES", "ISO-8859-15",   N_("_Spanish")},
    {"sv_SE", "ISO-8859-1",    N_("_Swedish")},
    {"tt_RU", "UTF-8",         N_("_Tatar")},
    {"tr_TR", "ISO-8859-9",    N_("_Turkish")},
    {"uk_UK", "KOI8-U",        N_("_Ukrainian")},
    {"", "UTF-8",              N_("_Generic UTF-8")}
};

static const gchar *
sw_preferred_charset(BalsaSendmsg * bsmsg)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(locales); i++)
        if (g_strcmp0(bsmsg->spell_check_lang, locales[i].locale) == 0)
            return locales[i].charset;

    return NULL;
}

/* ===================================================================
   Balsa menus. Touchpad has some simplified menus which do not
   overlap very much with the default balsa menus. They are here
   because they represent an alternative probably appealing to the all
   proponents of GNOME2 dumbify approach (OK, I am bit unfair here).
*/

/*
 * lists of actions that are enabled or disabled as groups
 */
static const gchar *const ready_actions[] = {
    "send", "toolbar-send", "queue", "postpone"
};

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

static const gchar * const attach_modes[] =
    {NULL, N_("Attachment"), N_("Inline"), N_("Reference") };

#define BALSA_TYPE_ATTACH_INFO (balsa_attach_info_get_type())
G_DECLARE_FINAL_TYPE(BalsaAttachInfo,
                     balsa_attach_info,
                     BALSA,
                     ATTACH_INFO,
                     GObject)

struct _BalsaAttachInfo {
    GObject parent_object;

    BalsaSendmsg *bm;                 /* send message back reference */

    GtkWidget *popup_menu;            /* popup menu */
    LibbalsaVfs *file_uri;            /* file uri of the attachment */
    gchar *uri_ref;                   /* external body URI reference */
    gchar *force_mime_type;           /* force using this particular mime type */
    gchar *charset;                   /* forced character set */
    gboolean delete_on_destroy;       /* destroy the file when not used any more */
    gint mode;                        /* LIBBALSA_ATTACH_AS_ATTACHMENT etc. */
    LibBalsaMessageHeaders *headers;  /* information about a forwarded message */
};

static BalsaAttachInfo* balsa_attach_info_new(BalsaSendmsg *bm);
static void balsa_attach_info_finalize(GObject * object);


#define BALSA_MSG_ATTACH_MODEL(x)   gtk_tree_view_get_model(GTK_TREE_VIEW((x)->tree_view))

G_DEFINE_TYPE(BalsaAttachInfo, balsa_attach_info, G_TYPE_OBJECT)

static void
balsa_attach_info_class_init(BalsaAttachInfoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = balsa_attach_info_finalize;
}

static void
balsa_attach_info_init(BalsaAttachInfo *info)
{
    info->popup_menu = NULL;
    info->file_uri = NULL;
    info->force_mime_type = NULL;
    info->charset = NULL;
    info->delete_on_destroy = FALSE;
    info->mode = LIBBALSA_ATTACH_AS_ATTACHMENT;
    info->headers = NULL;
}

static BalsaAttachInfo*
balsa_attach_info_new(BalsaSendmsg *bm)
{
    BalsaAttachInfo * info = g_object_new(BALSA_TYPE_ATTACH_INFO, NULL);

    info->bm = bm;
    return info;
}

static void
balsa_attach_info_finalize(GObject * object)
{
    BalsaAttachInfo * info;

    info = BALSA_ATTACH_INFO(object);

    /* unlink the file if necessary */
    if (info->delete_on_destroy && info->file_uri) {
        gchar * folder_name;

        /* unlink the file */
	g_debug("%s:%s: unlink `%s'", __FILE__, __func__,
		     libbalsa_vfs_get_uri_utf8(info->file_uri));
	libbalsa_vfs_file_unlink(info->file_uri, NULL);

        /* remove the folder if possible */
        folder_name = g_filename_from_uri(libbalsa_vfs_get_folder(info->file_uri),
                                          NULL, NULL);
        if (folder_name) {
            g_debug("%s:%s: rmdir `%s'", __FILE__, __func__,
                         folder_name);
            g_rmdir(folder_name);
            g_free(folder_name);
        }
    }

    /* clean up memory */
    if (info->popup_menu)
        gtk_widget_destroy(info->popup_menu);
    if (info->file_uri)
        g_object_unref(info->file_uri);
    g_free(info->force_mime_type);
    g_free(info->charset);
    libbalsa_message_headers_destroy(info->headers);

    G_OBJECT_CLASS(balsa_attach_info_parent_class)->finalize(object);
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
        gtk_window_present_with_time(GTK_WINDOW(ab),
                                     gtk_get_current_event_time());
        return;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(address_view), FALSE);

    ab = balsa_ab_window_new(TRUE, GTK_WINDOW(bsmsg->window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(ab), TRUE);
    g_signal_connect(ab, "response",
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
    LibBalsaMessage *message;
    LibBalsaMailbox *mailbox;

    message = bsmsg->draft_message;
    if (message != NULL &&
        (mailbox = libbalsa_message_get_mailbox(message)) != NULL &&
        !libbalsa_mailbox_get_readonly(mailbox))
        libbalsa_message_change_flags(message,
                                      LIBBALSA_MESSAGE_FLAG_DELETED, 0);
}

static gint
delete_handler(BalsaSendmsg * bsmsg)
{
    InternetAddressList *list;
    InternetAddress *ia;
    const gchar *tmp = NULL;
    gchar *free_me = NULL;
    gint reply;
    GtkWidget *d;

    g_debug("%s", __func__);

    if (bsmsg->state == SENDMSG_STATE_CLEAN)
        return FALSE;

    list = libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");
    ia = internet_address_list_get_address(list, 0);
    if (ia) {
        tmp = ia->name;
        if (!tmp || !*tmp)
            tmp = free_me = internet_address_to_string(ia, NULL, FALSE);
    }
    if (!tmp || !*tmp)
        tmp = _("(No name)");

    d = gtk_message_dialog_new(GTK_WINDOW(bsmsg->window),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("The message to “%s” is modified.\n"
                                 "Save message to Draftbox?"), tmp);
    g_free(free_me);
    g_object_unref(list);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_YES);
    gtk_dialog_add_button(GTK_DIALOG(d),
                          _("_Cancel"), GTK_RESPONSE_CANCEL);
    reply = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);

    switch (reply) {
    case GTK_RESPONSE_YES:
        if (bsmsg->state == SENDMSG_STATE_MODIFIED)
            if (!message_postpone(bsmsg))
                return TRUE;
        break;
    case GTK_RESPONSE_NO:
        if (!bsmsg->is_continue)
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
    BalsaSendmsg *bsmsg = data;

    return delete_handler(bsmsg);
}

static void
sw_close_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        data)
{
    BalsaSendmsg *bsmsg = data;

    g_debug("close_window_cb: start");
    g_object_set_data(G_OBJECT(bsmsg->window), "destroying",
                      GINT_TO_POINTER(TRUE));
    if(!delete_handler(bsmsg))
	gtk_widget_destroy(bsmsg->window);
    g_debug("close_window_cb: end");
}

static gint
destroy_event_cb(GtkWidget * widget, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    balsa_sendmsg_destroy_handler(bsmsg);
    return TRUE;
}

/* the balsa_sendmsg destructor; copies first the shown headers setting
 * to the balsa_app structure. The BalsaSendmsg is deallocated after
 * freeing up all resources, so we do not clear its members; if it were
 * to become a GObject::dispose method, we would need to be more
 * careful, to protect against repeated calls.
 */
#define BALSA_SENDMSG_WINDOW_KEY "balsa-sendmsg-window-key"
static void
balsa_sendmsg_destroy_handler(BalsaSendmsg * bsmsg)
{
    g_assert(bsmsg != NULL);

    if (balsa_app.main_window) {
        g_signal_handler_disconnect(balsa_app.main_window,
                                    bsmsg->delete_sig_id);
        g_signal_handler_disconnect(balsa_app.main_window,
                                    bsmsg->identities_changed_id);
        g_object_weak_unref(G_OBJECT(balsa_app.main_window),
                            (GWeakNotify) gtk_widget_destroy, bsmsg->window);
    }
    g_debug("balsa_sendmsg_destroy()_handler: Start.");

    if (bsmsg->parent_message != NULL) {
        LibBalsaMailbox *mailbox;

        mailbox = libbalsa_message_get_mailbox(bsmsg->parent_message);
	if (mailbox != NULL)
	    libbalsa_mailbox_close(mailbox,
		    /* Respect pref setting: */
				   balsa_app.expunge_on_close);
	g_object_unref(bsmsg->parent_message);
    }

    if (bsmsg->draft_message != NULL) {
        LibBalsaMailbox *mailbox;

        mailbox = libbalsa_message_get_mailbox(bsmsg->draft_message);
        g_object_set_data(G_OBJECT(bsmsg->draft_message),
                          BALSA_SENDMSG_WINDOW_KEY, NULL);
	if (mailbox != NULL)
	    libbalsa_mailbox_close(mailbox,
		    /* Respect pref setting: */
				   balsa_app.expunge_on_close);
	g_object_unref(bsmsg->draft_message);
    }

    g_debug("balsa_sendmsg_destroy_handler: Freeing bsmsg");
    gtk_widget_destroy(bsmsg->window);
    g_free(bsmsg->fcc_url);
    g_free(bsmsg->in_reply_to);
    g_list_free_full(bsmsg->references, g_free);

#if !(HAVE_GSPELL || HAVE_GTKSPELL)
    if (bsmsg->spell_checker)
        gtk_widget_destroy(bsmsg->spell_checker);
#endif                          /* HAVE_GTKSPELL */
    if (bsmsg->autosave_timeout_id != 0)
        g_source_remove(bsmsg->autosave_timeout_id);

#if !HAVE_GTKSOURCEVIEW
    g_object_unref(bsmsg->buffer2);
#endif                          /* HAVE_GTKSOURCEVIEW */

    if (g_list_find(balsa_app.identities, bsmsg->ident) != NULL) {
        /* Move the current identity to the start of the list */
        balsa_app.identities =
            g_list_remove(balsa_app.identities, bsmsg->ident);
        balsa_app.identities =
            g_list_prepend(balsa_app.identities, bsmsg->ident);

    }
    g_object_unref(bsmsg->ident);

    g_free(bsmsg->spell_check_lang);

    g_free(bsmsg);

    g_debug("balsa_sendmsg_destroy(): Stop.");
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
    unsigned i, j, maxfit = 0;
    gint maxpos = -1;

    if (locale == NULL || strcmp(locale, "C") == 0)
        locale = "en_US";
    for (i = 0; i < G_N_ELEMENTS(locales); i++) {
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
    { N_("To:"), N_("CC:"), N_("BCC:") };

static void
edit_with_gnome_check(GPid     pid,
                      gint     wait_status,
                      gpointer user_data)
{
    balsa_edit_with_gnome_data *data_real = (balsa_edit_with_gnome_data *) user_data;
    BalsaSendmsg *bsmsg = data_real->bsmsg;
    char *filename = data_real->filename;
    GError *error = NULL;
    gchar *filebuf;
    gsize filebuf_len;
    gchar **lines, **line_p;
    GtkTextBuffer *buffer;

    g_free(data_real);

    if (!g_spawn_check_wait_status(wait_status, &error)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Editing failed: %s"),
                                   error->message);
        g_error_free(error);
        g_unlink(filename);
        g_free(filename);
        return;
    }

    if (!g_file_get_contents(filename, &filebuf, &filebuf_len, &error)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Cannot read the file “%s”: %s"),
                                   filename,
                                   error->message);
        g_error_free(error);
        g_unlink(filename);
        g_free(filename);
        return;
    }

    if (filebuf_len > 0) {
        /* Delete a trailing '\n' to avoid a last empty line from g_strsplit(). */
        gchar *last_char = &filebuf[filebuf_len - 1];
        if (*last_char == '\n')
            *last_char = '\0';
    }

    line_p = lines = g_strsplit(filebuf, "\n", 0);
    g_free(filebuf);

    if (balsa_app.edit_headers) {
        for (; *line_p != NULL; line_p++) {
            char *line = *line_p;
            guint type;

            if (line[0] == '\0') {
                /* Blank line terminates headers; skip it: */
                line_p++;
                break;
            }

            if (g_str_has_prefix(line, _("Subject:"))) {
                gtk_entry_set_text(GTK_ENTRY(bsmsg->subject.body),
                                   line + strlen(_("Subject:")) + 1);
                continue;
            }

            for (type = 0;
                 type < G_N_ELEMENTS(address_types);
                 type++) {
                const gchar *type_string = _(address_types[type]);
                if (g_str_has_prefix(line, type_string)) {
                    libbalsa_address_view_set_from_string
                        (bsmsg->recipient_view,
                         address_types[type],
                         line + strlen(type_string) + 1);
                }
            }
        }
    }

#if !HAVE_GTKSOURCEVIEW
    sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    sw_buffer_signals_block(bsmsg, buffer);
    gtk_text_buffer_set_text(buffer, "", 0);

    for (; *line_p != NULL; line_p++) {
        gtk_text_buffer_insert_at_cursor(buffer, *line_p, -1);
        gtk_text_buffer_insert_at_cursor(buffer, "\n", -1);
    }
    g_strfreev(lines);

    sw_buffer_signals_unblock(bsmsg, buffer);

    /* We do not know whether the message has been modified, but we mark
     * it as such, to be on the safe side: */
    bsmsg->state = SENDMSG_STATE_MODIFIED;
    gtk_widget_set_sensitive(bsmsg->text, TRUE);

    g_unlink(filename);
    g_free(filename);
}

/* Edit the current file with an external editor. */
static void
sw_edit_activated(GSimpleAction * action,
                  GVariant      * parameter,
                  gpointer        data)
{
    BalsaSendmsg *bsmsg = data;
    GFile *tmp_file;
    static const char TMP_PATTERN[] = "balsa-edit-XXXXXX";
    gchar *filename;
    GFileIOStream *iostream;
    GOutputStream *stream;
    balsa_edit_with_gnome_data *edit_data;
    pid_t pid;
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    gchar *p;
    GAppInfo *app;
    char **argv;
    GError *error = NULL;

    app = g_app_info_get_default_for_type("text/plain", FALSE);
    if (!app) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("GNOME editor is not defined"
                                     " in your preferred applications."));
        return;
    }

    tmp_file = g_file_new_tmp(TMP_PATTERN, &iostream, &error);
    if (tmp_file == NULL) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("Could not create temporary file: %s"),
                                   error->message);
        g_error_free(error);
        g_object_unref(app);
        return;
    }
    filename = g_file_get_path(tmp_file);
    g_object_unref(tmp_file);

    argv = g_new(char *, 3);
    argv[0] = g_strdup(g_app_info_get_executable(app));
    argv[1] =
        g_strdup_printf("%s%s",
                        g_app_info_supports_uris(app) ? "file://" : "",
                        filename);
    argv[2] = NULL;
    /* FIXME: how can I detect if the called application needs the
     * terminal??? */
    g_object_unref(app);

    stream = g_io_stream_get_output_stream(G_IO_STREAM(iostream));

    if (balsa_app.edit_headers) {
        guint type;

        g_output_stream_printf(stream, NULL, NULL, NULL, 
                               "%s %s\n", _("Subject:"),
                               gtk_entry_get_text(GTK_ENTRY(bsmsg->subject.body)));
        for (type = 0; type < G_N_ELEMENTS(address_types); type++) {
            InternetAddressList *list =
                libbalsa_address_view_get_list(bsmsg->recipient_view,
                                               address_types[type]);
            gchar *addr_string = internet_address_list_to_string(list, NULL, FALSE);
            g_object_unref(list);
            g_output_stream_printf(stream, NULL, NULL, NULL,
                                   "%s %s\n", _(address_types[type]), addr_string);
            g_free(addr_string);
        }
        /* Blank line terminates headers: */
        g_output_stream_write_all(stream, "\n", 1, NULL, NULL, NULL);
    }

    gtk_widget_set_sensitive(GTK_WIDGET(bsmsg->text), FALSE);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    gtk_text_buffer_get_bounds(buffer, &start, &end);

    p = gtk_text_iter_get_text(&start, &end);
    g_output_stream_write_all(stream, p, strlen(p), NULL, NULL, NULL);
    g_free(p);

    g_output_stream_close(stream, NULL, NULL);
    g_object_unref(iostream);

    if (g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                      NULL, NULL, &pid, &error)) {
        /* Return immediately. We don't want balsa to 'hang' */
        edit_data = g_malloc(sizeof(balsa_edit_with_gnome_data));
        edit_data->filename = filename;
        edit_data->bsmsg = bsmsg;
        g_child_watch_add(pid, edit_with_gnome_check, edit_data);
    } else {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not launch application: %s"),
                                  error->message);
        g_error_free(error);
        g_free(filename);
    }

    g_strfreev(argv);
}

static void
sw_select_ident_activated(GSimpleAction * action,
                          GVariant      * parameter,
                          gpointer        data)
{
    BalsaSendmsg *bsmsg = data;

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
replace_identity_signature(BalsaSendmsg* bsmsg, LibBalsaIdentity* new_ident,
                           LibBalsaIdentity* old_ident, gint* replace_offset,
                           gint siglen, const gchar* new_sig)
{
    gint newsiglen;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter ins, end;
    GtkTextMark *mark;
    gboolean insert_signature;

    /* Save cursor */
    gtk_text_buffer_get_iter_at_mark(buffer, &ins,
                                     gtk_text_buffer_get_insert(buffer));
    mark = gtk_text_buffer_create_mark(buffer, NULL, &ins, TRUE);

    gtk_text_buffer_get_iter_at_offset(buffer, &ins,
                                       *replace_offset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end,
                                       *replace_offset + siglen);
    gtk_text_buffer_delete(buffer, &ins, &end);

    newsiglen = strlen(new_sig);

    switch (bsmsg->type) {
    case SEND_NORMAL:
    default:
        insert_signature = TRUE;
        break;
    case SEND_REPLY:
    case SEND_REPLY_ALL:
    case SEND_REPLY_GROUP:
        insert_signature = libbalsa_identity_get_sig_whenreply(new_ident);
        break;
    case SEND_FORWARD_ATTACH:
    case SEND_FORWARD_INLINE:
        insert_signature = libbalsa_identity_get_sig_whenforward(new_ident);
        break;
    }
    if (insert_signature) {
        gboolean new_sig_prepend = libbalsa_identity_get_sig_prepend(new_ident);
        gboolean old_sig_prepend = libbalsa_identity_get_sig_prepend(old_ident);

        /* see if sig location is probably going to be the same */
        if (new_sig_prepend == old_sig_prepend) {
            /* account for sig length difference in replacement offset */
            *replace_offset += newsiglen - siglen;
        } else if (new_sig_prepend) {
            /* sig location not the same between idents, take a WAG and
             * put it at the start of the message */
            gtk_text_buffer_get_start_iter(buffer, &ins);
            *replace_offset += newsiglen;
        } else {
            /* put it at the end of the message */
            gtk_text_buffer_get_end_iter(buffer, &ins);
        }

        gtk_text_buffer_place_cursor(buffer, &ins);
        gtk_text_buffer_insert_at_cursor(buffer, new_sig, -1);
    }

    /* Restore cursor */
    gtk_text_buffer_get_iter_at_mark(buffer, &ins, mark);
    gtk_text_buffer_place_cursor(buffer, &ins);
    gtk_text_buffer_delete_mark(buffer, mark);
}

/*
 * GAction helpers
 */

static GAction *
sw_get_action(BalsaSendmsg * bsmsg, const gchar * action_name)
{
    GAction *action;

    if (g_object_get_data(G_OBJECT(bsmsg->window), "destroying"))
        return NULL;

    action = g_action_map_lookup_action(G_ACTION_MAP(bsmsg->window),
                                        action_name);
    if (!action)
        g_warning("%s %s not found", __func__, action_name);

    return action;
}

static void
sw_action_set_enabled(BalsaSendmsg * bsmsg,
                      const gchar  * action_name,
                      gboolean       enabled)
{
    GAction *action;

    action = sw_get_action(bsmsg, action_name);
    if (action)
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
}

/*
 * Enable or disable a group of actions
 */

static void
sw_actions_set_enabled(BalsaSendmsg        * bsmsg,
                       const gchar * const * actions,
                       guint                 n_actions,
                       gboolean              enabled)
{
    guint i;

    for (i = 0; i < n_actions; i++)
        sw_action_set_enabled(bsmsg, *actions++, enabled);
}

#if !HAVE_GTKSOURCEVIEW || HAVE_GSPELL || HAVE_GTKSPELL
static gboolean
sw_action_get_enabled(BalsaSendmsg * bsmsg,
                      const gchar  * action_name)
{
    GAction *action;

    action = sw_get_action(bsmsg, action_name);
    return action ? g_action_get_enabled(action) : FALSE;
}
#endif                          /* HAVE_GTKSOURCEVIEW */

/* Set the state of a toggle-type GAction. */
static void
sw_action_set_active(BalsaSendmsg * bsmsg,
                     const gchar  * action_name,
                     gboolean       state)
{
    GAction *action;

    action = sw_get_action(bsmsg, action_name);
    if (action)
        g_action_change_state(action, g_variant_new_boolean(state));
}

static gboolean
sw_action_get_active(BalsaSendmsg * bsmsg,
                     const gchar  * action_name)
{
    GAction *action;
    gboolean retval = FALSE;

    action = sw_get_action(bsmsg, action_name);
    if (action) {
        GVariant *state;

        state = g_action_get_state(action);
        retval = g_variant_get_boolean(state);
        g_variant_unref(state);
    }

    return retval;
}

/*
 * end of GAction helpers
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

    gboolean found_sig = FALSE;
    gchar* old_sig;
    gchar* new_sig;
    gchar* message_text;
    gchar* compare_str;
    gchar** message_split;
    gchar* tmpstr;
    const gchar* subject;
    gint replen, fwdlen;
    const gchar *addr;
    const gchar *reply_string;
    const gchar *old_reply_string;
    const gchar *forward_string;
    const gchar *old_forward_string;

    LibBalsaIdentity* old_ident;
    gboolean reply_type = (bsmsg->type == SEND_REPLY ||
                           bsmsg->type == SEND_REPLY_ALL ||
                           bsmsg->type == SEND_REPLY_GROUP);
    gboolean forward_type = (bsmsg->type == SEND_FORWARD_ATTACH ||
                             bsmsg->type == SEND_FORWARD_INLINE);

    g_return_if_fail(ident != NULL);


    /* change entries to reflect new identity */
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsmsg->from.body),
                             g_list_index(balsa_app.identities, ident));

    addr = libbalsa_identity_get_replyto(ident);
    if (addr != NULL && addr[0] != '\0') {
        libbalsa_address_view_set_from_string(bsmsg->replyto_view,
                                              "Reply To:",
                                              addr);
        gtk_widget_show(bsmsg->replyto.name);
        gtk_widget_show(bsmsg->replyto.body);
    } else if (!sw_action_get_active(bsmsg, "reply-to")) {
        gtk_widget_hide(bsmsg->replyto.name);
        gtk_widget_hide(bsmsg->replyto.body);
    }

    addr = libbalsa_identity_get_bcc(bsmsg->ident);
    if (addr != NULL) {
        InternetAddressList *bcc_list, *ident_list;

        bcc_list =
            libbalsa_address_view_get_list(bsmsg->recipient_view, "BCC:");

        ident_list = internet_address_list_parse(libbalsa_parser_options(), addr);
        if (ident_list) {
            /* Remove any Bcc addresses that came from the old identity
             * from the list. */
            gint ident_list_len = internet_address_list_length(ident_list);
            gint i;

            for (i = 0; i < internet_address_list_length(bcc_list); i++) {
                InternetAddress *ia =
                    internet_address_list_get_address (bcc_list, i);
                gint j;

                for (j = 0; j < ident_list_len; j++) {
                    InternetAddress *ia2 =
                        internet_address_list_get_address(ident_list, j);
                    if (libbalsa_ia_rfc2821_equal(ia, ia2))
                        break;
                }

                if (j < ident_list_len) {
                    /* This address was found in the identity. */
                    internet_address_list_remove_at(bcc_list, i);
                    --i;
                }
            }
            g_object_unref(ident_list);
        }

        /* Add the new Bcc addresses, if any: */
        addr = libbalsa_identity_get_bcc(ident);
        ident_list = internet_address_list_parse(libbalsa_parser_options(), addr);
        if (ident_list) {
            internet_address_list_append(bcc_list, ident_list);
            g_object_unref(ident_list);
        }

        /* Set the resulting list: */
        libbalsa_address_view_set_from_list(bsmsg->recipient_view, "BCC:",
                                            bcc_list);
        g_object_unref(bcc_list);
    }

    /* change the subject to use the reply/forward strings */
    subject = gtk_entry_get_text(GTK_ENTRY(bsmsg->subject.body));

    /*
     * If the subject begins with the old reply string
     *    Then replace it with the new reply string.
     * Else, if the subject begins with the old forward string
     *    Then replace it with the new forward string.
     * Else, if the old reply string was empty, and the message
     *    is a reply, OR the old forward string was empty, and the
     *    message is a forward
     *    Then call bsmsg_set_subject_from_body()
     * Else assume the user hand edited the subject and does
     *    not want it altered
     */

    reply_string = libbalsa_identity_get_reply_string(ident);
    forward_string = libbalsa_identity_get_forward_string(ident);

    old_ident = bsmsg->ident;
    old_reply_string = libbalsa_identity_get_reply_string(old_ident);
    old_forward_string = libbalsa_identity_get_forward_string(old_ident);

    if (((replen = strlen(old_forward_string)) > 0) &&
	(strncmp(subject, old_reply_string, replen) == 0)) {
	tmpstr = g_strconcat(reply_string, &(subject[replen]), NULL);
	gtk_entry_set_text(GTK_ENTRY(bsmsg->subject.body), tmpstr);
	g_free(tmpstr);
    } else if (((fwdlen = strlen(old_forward_string)) > 0) &&
	       (strncmp(subject, old_forward_string, fwdlen) == 0)) {
	tmpstr = g_strconcat(forward_string, &(subject[fwdlen]), NULL);
	gtk_entry_set_text(GTK_ENTRY(bsmsg->subject.body), tmpstr);
	g_free(tmpstr);
    } else {
        if ((replen == 0 && reply_type) ||
            (fwdlen == 0 && forward_type) ) {
            LibBalsaMessage *msg = bsmsg->parent_message ?
                bsmsg->parent_message : bsmsg->draft_message;
            bsmsg_set_subject_from_body(bsmsg, libbalsa_message_get_body_list(msg), ident);
        }
    }

    /* -----------------------------------------------------------
     * remove/add the signature depending on the new settings, change
     * the signature if path changed */

    /* reconstruct the old signature to search with */
    old_sig = libbalsa_identity_get_signature(old_ident, NULL);

    /* switch identities in bsmsg here so we can use read_signature
     * again */
    g_set_object(&bsmsg->ident, ident);
    if ( (reply_type && libbalsa_identity_get_sig_whenreply(ident))
         || (forward_type && libbalsa_identity_get_sig_whenforward(ident))
         || (bsmsg->type == SEND_NORMAL && libbalsa_identity_get_sig_sending(ident)))
        new_sig = libbalsa_identity_get_signature(ident, NULL);
    else
        new_sig = NULL;
    if(!new_sig) new_sig = g_strdup("");

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    message_text = gtk_text_iter_get_text(&start, &end);
    if (!old_sig) {
        replace_offset = libbalsa_identity_get_sig_prepend(bsmsg->ident)
            ? 0 : g_utf8_strlen(message_text, -1);
        replace_identity_signature(bsmsg, ident, old_ident, &replace_offset,
                                   0, new_sig);
    } else {
        /* split on sig separator */
        message_split = g_strsplit(message_text, "\n-- \n", 0);
        siglen = g_utf8_strlen(old_sig, -1);

	/* check the special case of starting a message with a sig */
	compare_str = g_strconcat("\n", message_split[0], NULL);

	if (g_ascii_strncasecmp(old_sig, compare_str, siglen) == 0) {
	    g_free(compare_str);
	    replace_identity_signature(bsmsg, ident, old_ident,
                                       &replace_offset, siglen - 1, new_sig);
	    found_sig = TRUE;
	} else {
            gint i;

            g_free(compare_str);
            for (i = 0; message_split[i] != NULL; i++) {
		/* put sig separator back to search */
		compare_str = g_strconcat("\n-- \n", message_split[i], NULL);

		/* try to find occurance of old signature */
		if (g_ascii_strncasecmp(old_sig, compare_str, siglen) == 0) {
		    replace_identity_signature(bsmsg, ident, old_ident,
                                               &replace_offset, siglen,
                                               new_sig);
		    found_sig = TRUE;
		}

		replace_offset +=
		    g_utf8_strlen(i ? compare_str : message_split[i], -1);
		g_free(compare_str);
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
		replace_identity_signature(bsmsg, ident, old_ident,
                                           &replace_offset, siglen - 1,
                                           new_sig);
	    } else {
		g_free(tmpstr);
		replace_offset++;
		compare_str = g_utf8_next_char(compare_str);
		while (*compare_str) {
		    if (g_ascii_strncasecmp(old_sig, compare_str, siglen) == 0) {
			replace_identity_signature(bsmsg, ident, old_ident,
                                                   &replace_offset, siglen,
                                                   new_sig);
		    }
		    replace_offset++;
		    compare_str = g_utf8_next_char(compare_str);
		}
	    }
        }
        g_strfreev(message_split);
    }
    sw_action_set_active(bsmsg, "send-html",
                         libbalsa_identity_get_send_mp_alternative(bsmsg->ident));

    bsmsg_update_gpg_ui_on_ident_change(bsmsg, ident);

    g_free(old_sig);
    g_free(new_sig);
    g_free(message_text);

    libbalsa_address_view_set_domain(bsmsg->recipient_view, libbalsa_identity_get_domain(ident));

    sw_action_set_active(bsmsg, "request-mdn", libbalsa_identity_get_request_mdn(ident));
    sw_action_set_active(bsmsg, "request-dsn", libbalsa_identity_get_request_dsn(ident));
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
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(info->bm->tree_view));
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
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(info->bm->tree_view));
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
	gint result;

	parent = gtk_widget_get_toplevel(menu_item);
	extbody_dialog =
	    gtk_message_dialog_new(GTK_WINDOW(parent),
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_QUESTION,
				   GTK_BUTTONS_YES_NO,
				   _("Saying yes will not send the file "
				     "“%s” itself, but just a MIME "
				     "message/external-body reference. "
				     "Note that the recipient must "
				     "have proper permissions to see the "
				     "“real” file.\n\n"
				     "Do you really want to attach "
				     "this file as reference?"),
				   libbalsa_vfs_get_uri_utf8(info->file_uri));
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
    GError *err = NULL;
    gboolean result;

    g_return_if_fail(info != NULL);

    result = libbalsa_vfs_launch_app(info->file_uri,
                                     G_OBJECT(menu_item),
                                     &err);
    if (!result)
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not launch application: %s"),
                          err ? err->message : _("Unknown error"));
    g_clear_error(&err);
}


/* URL external body - right mouse button callback */
static void
on_open_url_cb(GtkWidget * menu_item, BalsaAttachInfo * info)
{
    GtkWidget *toplevel;
    GError *err = NULL;
    const gchar * uri;

    g_return_if_fail(info != NULL);
    uri = libbalsa_vfs_get_uri(info->file_uri);
    g_return_if_fail(uri != NULL);

    g_debug("open URL %s", uri);
    toplevel = gtk_widget_get_toplevel(info->bm->window);
    if (gtk_widget_is_toplevel(toplevel)) {
        gtk_show_uri_on_window(GTK_WINDOW(toplevel), uri,
                               gtk_get_current_event_time(), &err);
    }
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Error showing %s: %s\n"),
			  uri, err->message);
        g_error_free(err);
    }
}

static GtkWidget * sw_attachment_list(BalsaSendmsg *bsmsg);
static void
show_attachment_widget(BalsaSendmsg *bsmsg)
{
    GtkPaned *outer_paned;
    GtkWidget *child;

    outer_paned = GTK_PANED(bsmsg->paned);
    child = gtk_paned_get_child1(outer_paned);

    if (!GTK_IS_PANED(child)) {
        gint position;
        GtkRequisition minimum_size;
        GtkWidget *paned;
        GtkPaned *inner_paned;

        position = gtk_paned_get_position(outer_paned);
        if (position <= 0) {
            gtk_widget_get_preferred_size(child, &minimum_size, NULL);
            position = minimum_size.height;
        }
        gtk_container_remove(GTK_CONTAINER(bsmsg->paned),
                             g_object_ref(child));

        paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
        gtk_widget_show(paned);

        inner_paned = GTK_PANED(paned);
        gtk_paned_add1(inner_paned, child);
        g_object_unref(child);

        child = sw_attachment_list(bsmsg);
        gtk_widget_show_all(child);
        gtk_paned_add2(inner_paned, child);
        gtk_paned_set_position(inner_paned, position);

        gtk_widget_get_preferred_size(child, &minimum_size, NULL);
        gtk_paned_add1(outer_paned, paned);
        gtk_paned_set_position(outer_paned,
                               position + minimum_size.height);
    }
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
        gtk_dialog_new_with_buttons(_("Choose character set"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    _("_OK"), GTK_RESPONSE_OK,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    NULL);
    gchar *msg = g_strdup_printf
        (_("File\n%s\nis not encoded in US-ASCII or UTF-8.\n"
           "Please choose the character set used to encode the file."),
         fname);
    GtkWidget *info = gtk_label_new(msg);
    GtkWidget *charset_button = libbalsa_charset_button_new();
    GtkWidget *content_box;

    g_free(msg);
    content_box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    libbalsa_set_vmargins(info, HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(content_box), info);

    gtk_widget_set_vexpand(charset_button, TRUE);
    gtk_widget_set_valign(charset_button, GTK_ALIGN_FILL);
    libbalsa_set_vmargins(charset_button, HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(content_box), charset_button);

    gtk_widget_show(info);
    gtk_widget_show(charset_button);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    if (change_type) {
        GtkWidget *label = gtk_label_new(_("Attach as MIME type:"));
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, HIG_PADDING);
        combo_box = gtk_combo_box_text_new();

        gtk_widget_set_vexpand(hbox, TRUE);
        gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
        libbalsa_set_vmargins(hbox, HIG_PADDING);
        gtk_container_add(GTK_CONTAINER(content_box), hbox);

        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_halign(label, GTK_ALIGN_FILL);
        gtk_container_add(GTK_CONTAINER(hbox), label);

        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box),
                                       mime_type);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box),
                                       "application/octet-stream");
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), 0);
        g_signal_connect(combo_box, "changed",
                         G_CALLBACK(sw_charset_combo_box_changed),
                         charset_button);

        gtk_widget_set_hexpand(combo_box, TRUE);
        gtk_widget_set_halign(combo_box, GTK_ALIGN_FILL);
        gtk_container_add(GTK_CONTAINER(hbox), combo_box);

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
    LibBalsaTextAttribute attr;

    attr = libbalsa_text_attr_file(filename);
    if ((gint) attr < 0)
        return FALSE;

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
            balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                       LIBBALSA_INFORMATION_WARNING,
                                       _("Character set for file %s changed "
                                         "from “%s” to “%s”."), filename,
                                       info->std, info->win);
        }
    }
    *attach_charset = g_strdup(charset);

    return TRUE;
}


static LibBalsaMessageHeaders *
get_fwd_mail_headers(const gchar *mailfile_uri)
{
    GFile *msg_file;
    GMimeStream *stream;
    GMimeParser *parser;
    GMimeMessage *message;
    LibBalsaMessageHeaders *headers = NULL;

    /* create a stream from the mail file uri
     * note: the next call will never fail, but... */
    msg_file = g_file_new_for_uri(mailfile_uri);
    if (!g_file_query_exists(msg_file, NULL)) {
    	g_object_unref(msg_file);	/* ...we need a paranoia check for file existence */
    	return NULL;
    }
    stream = g_mime_stream_gio_new(msg_file);	/* consumes the GFil */

    /* try to parse the file */
    parser = g_mime_parser_new_with_stream(stream);
    g_mime_parser_set_format(parser, GMIME_FORMAT_MESSAGE);
    message = g_mime_parser_construct_message (parser, libbalsa_parser_options());
    g_object_unref(parser);
    g_object_unref(stream);
    if (message == NULL) {
    	return NULL;
    }

    /* get the headers from the gmime message */
    headers = g_new0(LibBalsaMessageHeaders, 1);
    libbalsa_message_headers_from_gmime(headers, message);
    if (!headers->subject) {
	const gchar * subject = g_mime_message_get_subject(message);

	if (!subject)
	    headers->subject = g_strdup(_("(no subject)"));
	else
	    headers->subject = g_mime_utils_header_decode_text(libbalsa_parser_options(), subject);
    }
    libbalsa_utf8_sanitize(&headers->subject,
			   balsa_app.convert_unknown_8bit,
			   NULL);

    /* unref the gmime message and return the information */
    g_object_unref(message);
    return headers;
}


/* add_attachment:
   adds given filename (uri format) to the list.
*/
gboolean
add_attachment(BalsaSendmsg * bsmsg, const gchar *filename,
               gboolean is_a_temp_file, const gchar *forced_mime_type)
{
    LibbalsaVfs * file_uri;
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaAttachInfo *attach_data;
    gboolean can_inline, is_fwd_message;
    gchar *content_type = NULL;
    gchar *utf8name;
    GError *err = NULL;
    GdkPixbuf *pixbuf;
    GtkWidget *menu_item;
    gchar *content_desc;

    g_debug("Trying to attach '%s'", filename);
    file_uri = libbalsa_vfs_new_from_uri(filename);
    if (!libbalsa_vfs_is_regular_file(file_uri, &err)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   "%s: %s", filename,
                                   err && err->message ? err->message : _("unknown error"));
	g_error_free(err);
	g_object_unref(file_uri);
	return FALSE;
    }

    /* get the pixbuf for the attachment's content type */
    is_fwd_message = forced_mime_type &&
	!g_ascii_strncasecmp(forced_mime_type, "message/", 8) && is_a_temp_file;
    if (is_fwd_message)
	content_type = g_strdup(forced_mime_type);
    pixbuf =
        libbalsa_icon_finder(GTK_WIDGET(bsmsg->window), forced_mime_type,
                             file_uri, &content_type,
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
                internet_address_list_to_string(attach_data->headers->from, NULL,
                                                FALSE);
	    utf8name = g_strdup_printf(_("Message from %s, subject: “%s”"),
				       tmp,
				       attach_data->headers->subject);
	    g_free(tmp);
	}
    } else {
        const gchar *uri_utf8 = libbalsa_vfs_get_uri_utf8(file_uri);
	const gchar *home = g_getenv("HOME");

	if (home && !strncmp(uri_utf8, "file://", 7) &&
            !strncmp(uri_utf8 + 7, home, strlen(home))) {
	    utf8name = g_strdup_printf("~%s", uri_utf8 + 7 + strlen(home));
	} else
	    utf8name = g_strdup(uri_utf8);
    }

    show_attachment_widget(bsmsg);

    model = BALSA_MSG_ATTACH_MODEL(bsmsg);
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);

    attach_data->file_uri = file_uri;
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
	g_signal_connect(menu_item, "activate",
			 G_CALLBACK(change_attach_mode),
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
	g_signal_connect(menu_item, "activate",
			 G_CALLBACK(change_attach_mode),
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
	g_signal_connect(menu_item, "activate",
			 G_CALLBACK(change_attach_mode),
			 (gpointer)attach_data);
	gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			      menu_item);
    }

    if (can_inline || !is_a_temp_file) {
        /* Need a separator */
        menu_item = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu), menu_item);
    }

    /* an attachment can be removed */
    menu_item =
	gtk_menu_item_new_with_label(_("Remove"));
    g_signal_connect(menu_item, "activate",
		     G_CALLBACK(remove_attachment),
		     (gpointer)attach_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			  menu_item);

    /* Insert another separator */
    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu), menu_item);

    /* add the usual vfs menu so the user can inspect what (s)he actually
       attached... (only for non-message attachments) */
    if (!is_fwd_message)
	libbalsa_vfs_fill_menu_by_content_type(GTK_MENU(attach_data->popup_menu),
					       content_type,
					       G_CALLBACK(attachment_menu_vfs_cb),
					       (gpointer)attach_data);
    gtk_widget_show_all(attach_data->popup_menu);

    /* append to the list store */
    content_desc =libbalsa_vfs_content_description(content_type);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
		       ATTACH_INFO_COLUMN, attach_data,
		       ATTACH_ICON_COLUMN, pixbuf,
		       ATTACH_TYPE_COLUMN, content_desc,
		       ATTACH_MODE_COLUMN, attach_data->mode,
		       ATTACH_SIZE_COLUMN, libbalsa_vfs_get_size(file_uri),
		       ATTACH_DESC_COLUMN, utf8name,
		       -1);
    g_object_unref(attach_data);
    g_object_unref(pixbuf);
    g_free(utf8name);
    g_free(content_type);
    g_free(content_desc);

    return TRUE;
}

/* add_urlref_attachment:
 * adds given url as reference to the to the list.
 */
static gboolean
add_urlref_attachment(BalsaSendmsg * bsmsg, const gchar *url)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaAttachInfo *attach_data;
    GdkPixbuf * pixbuf;
    GtkWidget *menu_item;

    g_debug("Trying to attach '%s'", url);

    /* get the pixbuf for the attachment's content type */
    pixbuf =
        gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                 "go-jump", GTK_ICON_SIZE_MENU, 0, NULL);

    /* create a new attachment info block */
    attach_data = balsa_attach_info_new(bsmsg);
    attach_data->charset = NULL;

    show_attachment_widget(bsmsg);

    model = BALSA_MSG_ATTACH_MODEL(bsmsg);
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);

    attach_data->uri_ref = g_strconcat("URL:", url, NULL);
    attach_data->force_mime_type = g_strdup("message/external-body");
    attach_data->delete_on_destroy = FALSE;
    attach_data->mode = LIBBALSA_ATTACH_AS_EXTBODY;
    attach_data->file_uri = libbalsa_vfs_new_from_uri(url);

    /* build the attachment's popup menu - may only be removed */
    attach_data->popup_menu = gtk_menu_new();
    menu_item =
	gtk_menu_item_new_with_label(_("Remove"));
    g_signal_connect(menu_item, "activate",
		     G_CALLBACK(remove_attachment),
		     (gpointer)attach_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			  menu_item);

    /* add a separator and the usual vfs menu so the user can inspect what
       (s)he actually attached... (only for non-message attachments) */
    gtk_menu_shell_append(GTK_MENU_SHELL(attach_data->popup_menu),
			  gtk_separator_menu_item_new());
    menu_item =
	gtk_menu_item_new_with_label(_("Open…"));
    g_signal_connect(menu_item, "activate",
		     G_CALLBACK(on_open_url_cb),
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

    return TRUE;
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
    files = gtk_file_chooser_get_uris(fc);
    for (list = files; list; list = list->next) {
        if(!add_attachment(bsmsg, list->data, FALSE, NULL))
	    res++;
        g_free(list->data);
    }

    g_slist_free(files);

    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir = gtk_file_chooser_get_current_folder_uri(fc);

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
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    NULL);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(fsw),
                                    libbalsa_vfs_local_only());
    gtk_window_set_destroy_with_parent(GTK_WINDOW(fsw), TRUE);

    fc = GTK_FILE_CHOOSER(fsw);
    gtk_file_chooser_set_select_multiple(fc, TRUE);
    if (balsa_app.attach_dir)
	gtk_file_chooser_set_current_folder_uri(fc, balsa_app.attach_dir);

    g_signal_connect(fc, "response",
		     G_CALLBACK(attach_dialog_response), bsmsg);

    gtk_widget_show(fsw);

    return fc;
}

/* attach_clicked - menu callback */
static void
sw_attach_file_activated(GSimpleAction * action,
                         GVariant      * parameter,
                         gpointer        data)
{
    BalsaSendmsg *bsmsg = data;

    sw_attach_dialog(bsmsg);
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
    tmp_file_name = g_filename_to_uri(name, NULL, NULL);
    g_free(name);
    add_attachment(bsmsg, tmp_file_name, TRUE, "message/rfc822");
    g_free(tmp_file_name);
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
            gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);
            g_string_free(body, TRUE);
	}
	g_list_free_full(l, g_object_unref);
    }
}

static void
sw_include_messages_activated(GSimpleAction * action,
                              GVariant      * parameter,
                              gpointer        data)
{
    BalsaSendmsg *bsmsg = data;

    insert_selected_messages(bsmsg, QUOTE_ALL);
}


static void
sw_attach_messages_activated(GSimpleAction * action,
                             GVariant      * parameter,
                             gpointer        data)
{
    BalsaSendmsg *bsmsg = data;
    GtkWidget *index =
	balsa_window_find_current_index(balsa_app.main_window);

    if (index) {
	GList *node, *l = balsa_index_selected_list(BALSA_INDEX(index));

	for (node = l; node; node = g_list_next(node)) {
	    LibBalsaMessage *message = node->data;

	    if(!attach_message(bsmsg, message)) {
                balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                           LIBBALSA_INFORMATION_WARNING,
                                           _("Attaching message failed.\n"
                                             "Possible reason: not enough temporary space"));
                break;
            }
	}
	g_list_free_full(l, g_object_unref);
    }
}


/* attachments_add - attachments field D&D callback */

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
		guint info, guint time, BalsaSendmsg * bsmsg)
{
    gboolean drag_result = TRUE;

    g_debug("attachments_add: info %d", info);
    if (info == TARGET_MESSAGES) {
	BalsaIndex *index =
            *(BalsaIndex **) gtk_selection_data_get_data(selection_data);
	LibBalsaMailbox *mailbox = balsa_index_get_mailbox(index);
        GArray *selected = balsa_index_selected_msgnos_new(index);
	guint i;

        for (i = 0; i < selected->len; i++) {
	    guint msgno = g_array_index(selected, guint, i);
	    LibBalsaMessage *message =
		libbalsa_mailbox_get_message(mailbox, msgno);
            if (!message)
                continue;

            if(!attach_message(bsmsg, message))
                balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                           LIBBALSA_INFORMATION_WARNING,
                                           _("Attaching message failed.\n"
                                             "Possible reason: not enough temporary space"));
	    g_object_unref(message);
        }
        balsa_index_selected_msgnos_free(index, selected);
    } else if (info == TARGET_URI_LIST) {
        gchar **uris;

        uris = gtk_selection_data_get_uris(selection_data);
        if (uris != NULL) {
            guint n;

            for (n = 0U; uris[n] != NULL; n++)
                add_attachment(bsmsg, uris[n], FALSE, NULL);
            g_strfreev(uris);
        }
    } else if( info == TARGET_STRING) {
	gchar *url =
            rfc2396_uri((gchar *)
                        gtk_selection_data_get_data(selection_data));

	if (url != NULL) {
	    add_urlref_attachment(bsmsg, url);
            g_free(url);
        } else {
	    drag_result = FALSE;
        }
    }

    gtk_drag_finish(context, drag_result, FALSE, time);
}

/* to_add - address-view D&D callback; we assume it's a To: address */
static void
to_add(GtkWidget * widget,
       GdkDragContext * context,
       gint x,
       gint y,
       GtkSelectionData * selection_data,
       guint info, guint time, gpointer user_data)
{
    gboolean drag_result = FALSE;

    if (info == TARGET_STRING) {
        const gchar *address;

        address =
            (const gchar *) gtk_selection_data_get_data(selection_data);
        libbalsa_address_view_add_from_string(LIBBALSA_ADDRESS_VIEW
                                              (widget), "To:", address);
        drag_result = TRUE;
    }
    gtk_drag_finish(context, drag_result, FALSE, time);
}

/*
 * static void create_email_or_string_header()
 *
 * Creates a gtk_label()/entry pair.
 * Creates a BalsaSendmsgHeader
 *
 * Input: GtkWidget* grid       - Grid to attach to.
 *        const gchar* label     - Label string.
 *        int y_pos              - position in the grid.
 *        arr                    - arr[1] is the entry widget.
 *
 * Output: BalsaSendmsgHeader* header - header->name will be the label widget.
 */

#define BALSA_COMPOSE_ENTRY "balsa-compose-entry"

static void
create_email_or_string_header(BalsaSendmsg       *bsmsg,
                              GtkWidget          *grid,
                              const gchar        *label,
                              int                 y_pos,
                              BalsaSendmsgHeader *header)
{
    GtkWidget *mnemonic_widget;

    mnemonic_widget = header->body;
    if (GTK_IS_FRAME(mnemonic_widget))
        mnemonic_widget = gtk_bin_get_child(GTK_BIN(mnemonic_widget));
    header->name = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(header->name), mnemonic_widget);
    gtk_widget_set_halign(header->name, GTK_ALIGN_START);
    libbalsa_set_margins(header->name, GNOME_PAD_SMALL);
    gtk_grid_attach(GTK_GRID(grid), header->name, 0, y_pos, 1, 1);

    if (!balsa_app.use_system_fonts) {
        gchar *css;
        GtkCssProvider *css_provider;

        gtk_widget_set_name(header->body, BALSA_COMPOSE_ENTRY);
        css = libbalsa_font_string_to_css(balsa_app.message_font,
                                          BALSA_COMPOSE_ENTRY);

        css_provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css_provider, css, -1, NULL);
        g_free(css);

        gtk_style_context_add_provider(gtk_widget_get_style_context(header->body) ,
                                       GTK_STYLE_PROVIDER(css_provider),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css_provider);
    }

    gtk_widget_set_hexpand(header->body, TRUE);
    gtk_grid_attach(GTK_GRID(grid), header->body, 1, y_pos, 1, 1);
}


/*
 * static void create_string_header()
 *
 * Creates a BalsaSendmsgHeader
 *
 * Input: GtkWidget* grid       - Grid to attach to.
 *        const gchar* label     - Label string.
 *        int y_pos              - position in the grid.
 *
 * Output: BalsaSendmsgHeader* header - header->name will be the label widget.
 *                                    - header->body will be the entry widget.
 */
static void
create_string_header(BalsaSendmsg       *bsmsg,
                     GtkWidget          *grid,
                     const gchar        *label,
                     int                 y_pos,
                     BalsaSendmsgHeader *header)
{
    header->body = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(header->body), 2048);
    create_email_or_string_header(bsmsg, grid, label, y_pos, header);
}

/*
 * static void create_email_header()
 *
 * Creates a gtk_label()/libbalsa_address_view() and button in a grid for
 * e-mail entries, eg. To:.  It also sets up some callbacks in gtk.
 *
 * Input:
 *         BalsaSendmsg *bsmsg  - The send message window
 *         GtkWidget    *grid   - grid to insert the widgets into.
 *         int           y_pos  - How far down in the grid to put label.
 * On return, the LibBalsaAddressView** has been set
 * and the BalsaSendmsgHeader* has been populated.
 */

static void
create_email_header(BalsaSendmsg         *bsmsg,
                    GtkWidget            *grid,
                    int                   y_pos,
                    LibBalsaAddressView **view,
                    BalsaSendmsgHeader   *header,
                    const gchar          *label,
                    const gchar * const  *types,
                    guint                 n_types)
{
    GtkWidget *scroll;

    *view = libbalsa_address_view_new(types, n_types,
                                      balsa_app.address_book_list,
                                      balsa_app.convert_unknown_8bit);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    /* This is a horrible hack, but we need to make sure that the
     * recipient list is more than one line high: */
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll),
                                               60);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(*view));

    header->body = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(header->body), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(header->body), scroll);

    create_email_or_string_header(bsmsg, grid, _(label), y_pos, header);

    g_signal_connect(*view, "drag-data-received",
                     G_CALLBACK(to_add), NULL);
    g_signal_connect(*view, "open-address-book",
		     G_CALLBACK(address_book_cb), bsmsg);
    gtk_drag_dest_set(GTK_WIDGET(*view), GTK_DEST_DEFAULT_ALL,
		      email_field_drop_types,
			  G_N_ELEMENTS(email_field_drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE);

    libbalsa_address_view_set_domain(*view, libbalsa_identity_get_domain(bsmsg->ident));
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
    GtkTreeIter iter;

    if (gtk_combo_box_get_active_iter(combo_box, &iter)) {
        LibBalsaIdentity *ident;

        gtk_tree_model_get(gtk_combo_box_get_model(combo_box), &iter,
                           2, &ident, -1);
        update_bsmsg_identity(bsmsg, ident);
        g_object_unref(ident);
    }
}

static void
create_from_header(GtkWidget * grid, BalsaSendmsg * bsmsg)
{
    bsmsg->from.body =
        libbalsa_identity_combo_box(balsa_app.identities, NULL,
                                    G_CALLBACK(sw_combo_box_changed), bsmsg);
    create_email_or_string_header(bsmsg, grid, _("F_rom:"), 0, &bsmsg->from);
}

static void
attachment_button_press_cb(GtkGestureMultiPress *multi_press,
                           gint                  n_press,
                           gdouble               x,
                           gdouble               y,
                           gpointer              user_data)
{
    GtkTreeView *tree_view = user_data;
    GtkGesture *gesture;
    const GdkEvent *event;
    GtkTreePath *path;
    gint bx, by;

    gesture = GTK_GESTURE(multi_press);
    event = gtk_gesture_get_last_event(gesture, gtk_gesture_get_last_updated_sequence(gesture));

    if (!gdk_event_triggers_context_menu(event) ||
        gdk_event_get_window(event) != gtk_tree_view_get_bin_window(tree_view)) {
        return;
    }

    gtk_tree_view_convert_widget_to_bin_window_coords(tree_view, (gint) x, (gint) y,
                                                      &bx, &by);

    if (gtk_tree_view_get_path_at_pos(tree_view, bx, by,
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
		if (attach_info->popup_menu) {
                    gtk_menu_popup_at_pointer(GTK_MENU(attach_info->popup_menu),
                                              (GdkEvent *) event);
                }
		g_object_unref(attach_info);
	    }
        }
        gtk_tree_path_free(path);
    }
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
	if (attach_info->popup_menu) {
            gtk_menu_popup_at_widget(GTK_MENU(attach_info->popup_menu),
                                     GTK_WIDGET(widget),
                                     GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                     NULL);
        }
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
    guint64 size;
    gchar *sstr;

    gtk_tree_model_get(model, iter, ATTACH_MODE_COLUMN, &mode,
		       ATTACH_SIZE_COLUMN, &size, -1);
    if (mode == LIBBALSA_ATTACH_AS_EXTBODY)
        sstr = g_strdup("-");
    else
        sstr = g_format_size(size);
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
    GtkWidget *grid;

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), HIG_PADDING);
    gtk_grid_set_column_spacing(GTK_GRID(grid), HIG_PADDING);
    gtk_container_set_border_width(GTK_CONTAINER(grid), HIG_PADDING);

    /* From: */
    create_from_header(grid, bsmsg);

    /* Create the 'Reply To:' entry before the regular recipients, to
     * get the initial focus in the regular recipients*/
#define REPLY_TO_ROW 3
    create_email_header(bsmsg, grid, REPLY_TO_ROW, &bsmsg->replyto_view,
                        &bsmsg->replyto, "R_eply To:", NULL, 0);

    /* To:, Cc:, and Bcc: */
    create_email_header(bsmsg, grid, ++row, &bsmsg->recipient_view,
                        &bsmsg->recipients, "Rec_ipients:", address_types,
                        G_N_ELEMENTS(address_types));
    gtk_widget_set_vexpand(bsmsg->recipients.body, TRUE);
    g_signal_connect_swapped(gtk_tree_view_get_model
                             (GTK_TREE_VIEW(bsmsg->recipient_view)),
                             "row-changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);
    g_signal_connect_swapped(gtk_tree_view_get_model
                             (GTK_TREE_VIEW(bsmsg->recipient_view)),
                             "row-deleted",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);

    /* Subject: */
    create_string_header(bsmsg, grid, _("S_ubject:"), ++row,
                         &bsmsg->subject);
    g_signal_connect_swapped(bsmsg->subject.body, "changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);

    /* Reply To: */
    /* We already created it, so just increment row: */
    ++row;
    g_assert(row == REPLY_TO_ROW);
#undef REPLY_TO_ROW

    /* fcc: mailbox folder where the message copy will be written to */
    if (balsa_app.fcc_mru == NULL)
        balsa_mblist_mru_add(&balsa_app.fcc_mru,
                             libbalsa_mailbox_get_url(balsa_app.sentbox));
    balsa_mblist_mru_add(&balsa_app.fcc_mru, "");
    if (balsa_app.copy_to_sentbox) {
        /* move the NULL option to the bottom */
        balsa_app.fcc_mru = g_list_reverse(balsa_app.fcc_mru);
        balsa_mblist_mru_add(&balsa_app.fcc_mru, "");
        balsa_app.fcc_mru = g_list_reverse(balsa_app.fcc_mru);
    }
    if (bsmsg->draft_message != NULL) {
        LibBalsaMessageHeaders *headers;

        headers = libbalsa_message_get_headers(bsmsg->draft_message);
        if (headers != NULL && headers->fcc_url != NULL)
            balsa_mblist_mru_add(&balsa_app.fcc_mru, headers->fcc_url);
    }
    bsmsg->fcc.body =
        balsa_mblist_mru_option_menu(GTK_WINDOW(bsmsg->window),
                                     &balsa_app.fcc_mru);
    create_email_or_string_header(bsmsg, grid, _("F_CC:"), ++row,
                                  &bsmsg->fcc);

    gtk_widget_show_all(grid);
    return grid;
}

static GtkWidget *
sw_attachment_list(BalsaSendmsg *bsmsg)
{
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *sw;
    GtkListStore *store;
    GtkWidget *tree_view;
    GtkCellRenderer *renderer;
    GtkTreeView *view;
    GtkTreeViewColumn *column;
    GtkWidget *frame;
    GtkGesture *gesture;

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), HIG_PADDING);
    gtk_grid_set_column_spacing(GTK_GRID(grid), HIG_PADDING);
    gtk_container_set_border_width(GTK_CONTAINER(grid), HIG_PADDING);

    /* Attachment list */
    label = gtk_label_new_with_mnemonic(_("_Attachments:"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    libbalsa_set_margins(label, GNOME_PAD_SMALL);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

    store = gtk_list_store_new(ATTACH_NUM_COLUMNS,
			       BALSA_TYPE_ATTACH_INFO,
			       GDK_TYPE_PIXBUF,
			       G_TYPE_STRING,
			       G_TYPE_INT,
			       G_TYPE_UINT64,
			       G_TYPE_STRING);

    bsmsg->tree_view = tree_view =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_widget_set_vexpand(tree_view, TRUE);
    view = GTK_TREE_VIEW(tree_view);
    gtk_tree_view_set_headers_visible(view, TRUE);
    g_object_unref(store);

    /* column for type icon */
    renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes(view,
						-1, NULL, renderer,
						"pixbuf", ATTACH_ICON_COLUMN,
						NULL);

    /* column for the mime type */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes(view,
						-1, _("Type"), renderer,
						"text",	ATTACH_TYPE_COLUMN,
						NULL);

    /* column for the attachment mode */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    column = gtk_tree_view_column_new_with_attributes(_("Mode"), renderer,
						      "text", ATTACH_MODE_COLUMN,
						      NULL);
    gtk_tree_view_column_set_cell_data_func(column,
					    renderer, render_attach_mode,
                                            NULL, NULL);
    gtk_tree_view_append_column(view, column);

    /* column for the attachment size */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    column = gtk_tree_view_column_new_with_attributes(_("Size"), renderer,
						      "text", ATTACH_SIZE_COLUMN,
						      NULL);
    gtk_tree_view_column_set_cell_data_func(column,
					    renderer, render_attach_size,
                                            NULL, NULL);
    gtk_tree_view_append_column(view, column);

    /* column for the file type/description */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes(view,
						-1, _("Description"), renderer,
						"text", ATTACH_DESC_COLUMN,
						NULL);

    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(view),
				GTK_SELECTION_SINGLE);
    g_signal_connect(view, "popup-menu",
                     G_CALLBACK(attachment_popup_cb), NULL);

    gesture = gtk_gesture_multi_press_new(tree_view);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_signal_connect(gesture, "pressed",
                     G_CALLBACK(attachment_button_press_cb), view);

    g_signal_connect(bsmsg->window, "drag-data-received",
		     G_CALLBACK(attachments_add), bsmsg);
    gtk_drag_dest_set(GTK_WIDGET(bsmsg->window), GTK_DEST_DEFAULT_ALL,
		      drop_types, G_N_ELEMENTS(drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), tree_view);
    gtk_container_add(GTK_CONTAINER(frame), sw);

    gtk_widget_set_hexpand(frame, TRUE);
    gtk_grid_attach(GTK_GRID(grid), frame, 1, 0, 1, 1);

    return grid;
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
    const gchar * uri;

    gtk_tree_model_get(model, iter, ATTACH_INFO_COLUMN, &info, -1);
    if (!info)
	return FALSE;
    uri = libbalsa_vfs_get_uri(info->file_uri);
    if (g_strcmp0(find_file->name, uri) == 0)
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
                guint info, guint time, BalsaSendmsg * bsmsg)
{
    GtkTextBuffer *buffer;
    BalsaIndex *index;
    LibBalsaMailbox *mailbox;
    GArray *selected;
    guint i;

    switch(info) {
    case TARGET_MESSAGES:
	index =
            *(BalsaIndex **) gtk_selection_data_get_data(selection_data);
	mailbox = balsa_index_get_mailbox(index);
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
            gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);
            g_string_free(body, TRUE);
        }
        balsa_index_selected_msgnos_free(index, selected);
        break;
    case TARGET_URI_LIST: {
        gchar **uris;

        uris = gtk_selection_data_get_uris(selection_data);
        if (uris != NULL) {
            guint n;

            for (n = 0U; uris[n] != NULL; n++) {
                /* Since current GtkTextView gets this signal twice for
                 * every action (#150141) we need to check for duplicates,
                 * which is a good idea anyway. */
                has_file_attached_t find_file = {uris[n], FALSE};

                if (bsmsg->tree_view)
                    gtk_tree_model_foreach(BALSA_MSG_ATTACH_MODEL(bsmsg),
                                           has_file_attached, &find_file);
                if (!find_file.found)
                    add_attachment(bsmsg, uris[n], FALSE, NULL);
            }
            g_strfreev(uris);
        }
    }
        break;
    case TARGET_EMAIL:
    case TARGET_STRING: /* perhaps we should allow dropping in these, too? */
    default: break;
    }
    gtk_drag_finish(context, TRUE, FALSE, time);
}

/* create_text_area
   Creates the text entry part of the compose window.
*/
#ifdef HAVE_GTKSOURCEVIEW

static void
sw_can_undo_cb(GtkSourceBuffer * source_buffer, GParamSpec *arg1,
	       BalsaSendmsg * bsmsg)
{
    gboolean can_undo;

    g_object_get(source_buffer, "can-undo", &can_undo, NULL);
    sw_action_set_enabled(bsmsg, "undo", can_undo);
}

static void
sw_can_redo_cb(GtkSourceBuffer * source_buffer, GParamSpec *arg1,
	       BalsaSendmsg * bsmsg)
{
    gboolean can_redo;

    g_object_get(source_buffer, "can-redo", &can_redo, NULL);
    sw_action_set_enabled(bsmsg, "redo", can_redo);
}

#endif                          /* HAVE_GTKSOURCEVIEW */

static GtkWidget *
create_text_area(BalsaSendmsg * bsmsg)
{
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
#if HAVE_GSPELL
    GspellTextBuffer *gspell_buffer;
    GspellChecker *checker;
    GspellTextView *gspell_view;
#endif                          /* HAVE_GSPELL */
    GtkWidget *scroll;

#if HAVE_GTKSOURCEVIEW
    bsmsg->text = libbalsa_source_view_new(TRUE);
#else                           /* HAVE_GTKSOURCEVIEW */
    bsmsg->text = gtk_text_view_new();
#endif                          /* HAVE_GTKSOURCEVIEW */
    text_view = GTK_TEXT_VIEW(bsmsg->text);
    gtk_text_view_set_left_margin(text_view, 2);
    gtk_text_view_set_right_margin(text_view, 2);

    /* set the message font */
    if (!balsa_app.use_system_fonts) {
        gchar *css;
        GtkCssProvider *css_provider;

        css = libbalsa_font_string_to_css(balsa_app.message_font,
                                          BALSA_COMPOSE_ENTRY);

        css_provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css_provider, css, -1, NULL);
        g_free(css);

        gtk_widget_set_name(bsmsg->text, BALSA_COMPOSE_ENTRY);
        gtk_style_context_add_provider(gtk_widget_get_style_context(bsmsg->text) ,
                                       GTK_STYLE_PROVIDER(css_provider),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css_provider);
    }

    buffer = gtk_text_view_get_buffer(text_view);
#ifdef HAVE_GTKSOURCEVIEW
    g_signal_connect(buffer, "notify::can-undo",
                     G_CALLBACK(sw_can_undo_cb), bsmsg);
    g_signal_connect(buffer, "notify::can-redo",
                     G_CALLBACK(sw_can_redo_cb), bsmsg);
#else                           /* HAVE_GTKSOURCEVIEW */
    bsmsg->buffer2 =
         gtk_text_buffer_new(gtk_text_buffer_get_tag_table(buffer));
#endif                          /* HAVE_GTKSOURCEVIEW */
    gtk_text_buffer_create_tag(buffer, "url", NULL, NULL);
    gtk_text_view_set_editable(text_view, TRUE);
    gtk_text_view_set_wrap_mode(text_view, GTK_WRAP_WORD_CHAR);

#if HAVE_GSPELL
    if (sw_action_get_enabled(bsmsg, "spell-check")) {
        gspell_buffer = gspell_text_buffer_get_from_gtk_text_buffer(buffer);
        checker = gspell_checker_new(NULL);
        gspell_text_buffer_set_spell_checker(gspell_buffer, checker);
        g_object_unref(checker);

        gspell_view = gspell_text_view_get_from_gtk_text_view(text_view);
        gspell_text_view_set_enable_language_menu(gspell_view, TRUE);
    }
#endif                          /* HAVE_GSPELL */

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
    				   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(scroll), bsmsg->text);
    g_signal_connect(bsmsg->text, "drag-data-received",
		     G_CALLBACK(drag_data_quote), bsmsg);
    /* GTK_DEST_DEFAULT_ALL in drag_set would trigger bug 150141 */
    gtk_drag_dest_set(GTK_WIDGET(bsmsg->text), 0,
		      drop_types, G_N_ELEMENTS(drop_types),
		      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

    gtk_widget_show_all(scroll);

    return scroll;
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
add_file_attachment(BalsaSendmsg        *bsmsg,
                    LibBalsaMessageBody *body,
                    const gchar         *body_type)
{
    gchar *name, *tmp_file_name;
    GError *err = NULL;
    gboolean res = FALSE;

    if (body->filename != NULL) {
        gchar *tempdir;

        libbalsa_mktempdir(&tempdir);
        name = g_build_filename(tempdir, body->filename, NULL);
        g_free(tempdir);

        res = libbalsa_message_body_save(body, name,
                                         LIBBALSA_MESSAGE_BODY_SAFE,
                                         FALSE, &err);
    } else {
        int fd;

        if ((fd = g_file_open_tmp("balsa-continue-XXXXXX", &name, NULL)) >= 0) {
            res = libbalsa_message_body_save_fs(body, fd, FALSE, NULL, &err);
            close(fd);
        }
    }

    if (!res) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("Could not save attachment: %s"),
                                   err ? err->message : _("Unknown error"));
        g_clear_error(&err);
        /* FIXME: do not try any further? */
    }

    tmp_file_name = g_filename_to_uri(name, NULL, NULL);
    g_free(name);

    add_attachment(bsmsg, tmp_file_name, TRUE, body_type);
    g_free(tmp_file_name);
}

static void
continue_body(BalsaSendmsg * bsmsg, LibBalsaMessage * message)
{
    LibBalsaMessageBody *body;

    body = libbalsa_message_get_body_list(message);

    if (body == NULL)
        return;

    if (libbalsa_message_body_type(body) == LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART)
        body = body->parts;

    /* if the first part is of type text/plain with a NULL filename, it
       was the message... */
    if (body != NULL && body->filename == NULL) {
        GString *rbdy;
        gchar *body_type = libbalsa_message_body_get_mime_type(body);
        gint llen = -1;

        if (bsmsg->flow && libbalsa_message_body_is_flowed(body))
            llen = balsa_app.wraplength;

        if (strcmp(body_type, "text/plain") == 0 &&
            (rbdy = process_mime_part(message, body, NULL, llen, FALSE,
                                      bsmsg->flow)) != NULL) {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

            gtk_text_buffer_insert_at_cursor(buffer, rbdy->str, rbdy->len);
            g_string_free(rbdy, TRUE);
        }

        g_free(body_type);
        body = body->next;
    }

    while (body != NULL) {
        gchar *body_type = libbalsa_message_body_get_mime_type(body);

        if (strcmp(body_type, "message/external-body") == 0)
            add_urlref_attachment(bsmsg, body->filename);
        else
            add_file_attachment(bsmsg, body, body_type);

        g_free(body_type);
        body = body->next;
    }
}

static gchar*
message_part_get_subject(LibBalsaMessageBody *part)
{
    gchar *subject = NULL;

    if (part->embhdrs != NULL && part->embhdrs->subject != NULL)
        subject = g_strdup(part->embhdrs->subject);

    if (subject == NULL && part->message != NULL)
        subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(part->message));

    if (subject == NULL)
        subject = g_strdup(_("No subject"));

    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit,
			   NULL);

    return subject;
}

/* --- stuff for collecting parts for a reply --- */

enum {
    QUOTE_INCLUDE,
    QUOTE_DESCRIPTION,
    QUOTE_BODY,
	QUOTE_STYLE,
	QUOTE_DECRYPTED,
    QOUTE_NUM_ELEMS
};

typedef struct {
	guint parts;
	guint decrypted;
} reply_collect_stat_t;

static void
tree_add_quote_body(LibBalsaMessageBody  *body,
					GtkTreeStore         *store,
					GtkTreeIter          *parent,
					gboolean              decrypted,
					reply_collect_stat_t *stats)
{
	GtkTreeIter iter;
	gchar *mime_type;
	gchar *mime_desc;
	static gboolean preselect;
	GString *description;

	/* preselect inline parts which includes all parts without Content-Disposition */
	if (body->mime_part) {
		const gchar *disp_type;

		disp_type = g_mime_object_get_disposition(body->mime_part);
		preselect = (disp_type == NULL) || (disp_type[0] == '\0') || (g_ascii_strcasecmp(disp_type, "inline") == 0);
	} else {
		preselect = TRUE;
	}

	/* mark decrypted parts, including those in a decrypted container */
	decrypted |= body->was_encrypted;
	description = g_string_new(decrypted ? _("decrypted: ") : NULL);

	/* attachment information */
	mime_type = libbalsa_message_body_get_mime_type(body);
	mime_desc = libbalsa_vfs_content_description(mime_type);
	g_free(mime_type);
	if ((body->filename != NULL) && (body->filename[0] != '\0')) {
		if (preselect) {
			g_string_append_printf(description, _("inlined file “%s” (%s)"),
					body->filename, mime_desc);
		} else {
			g_string_append_printf(description, _("attached file “%s” (%s)"),
					body->filename, mime_desc);
		}
	} else {
		if (preselect) {
			g_string_append_printf(description, _("inlined %s part"), mime_desc);
		} else {
			g_string_append_printf(description, _("attached %s part"), mime_desc);
		}
	}
	g_free(mime_desc);

	/* append to tree */
	gtk_tree_store_append(store, &iter, parent);
	gtk_tree_store_set(store, &iter,
			QUOTE_INCLUDE, preselect,
			QUOTE_DESCRIPTION, description->str,
			QUOTE_BODY, body,
			QUOTE_STYLE, decrypted ? PANGO_STYLE_OBLIQUE : PANGO_STYLE_NORMAL,
			QUOTE_DECRYPTED, decrypted,
			-1);
	(void) g_string_free(description, TRUE);
	stats->parts++;
	if (decrypted) {
		stats->decrypted++;
	}
}

static void
scan_bodies(GtkTreeStore         *bodies,
			GtkTreeIter          *parent,
			LibBalsaMessageBody  *body,
			gboolean              ignore_html,
			gboolean              container_mp_alt,
			gboolean              decrypted,
			reply_collect_stat_t *stats)
{
    gchar * mime_type;

    while (body) {
	switch (libbalsa_message_body_type(body)) {
	case LIBBALSA_MESSAGE_BODY_TYPE_TEXT:
	    {
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
			tree_add_quote_body(body, bodies, parent, decrypted, stats);
			return;
		    }
		} else {
		    tree_add_quote_body(body, bodies, parent, decrypted, stats);
		}
		break;
	    }

	case LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART:
	    mime_type = libbalsa_message_body_get_mime_type(body);
	    scan_bodies(bodies, parent, body->parts, ignore_html,
				 !g_ascii_strcasecmp(mime_type, "multipart/alternative"),
				 body->was_encrypted | decrypted, stats);
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
			g_strdup_printf(_("message from %s, subject “%s”"),
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
				   QUOTE_STYLE, decrypted ? PANGO_STYLE_OBLIQUE : PANGO_STYLE_NORMAL,
				   QUOTE_DECRYPTED, decrypted,
				   -1);
		g_free(mime_type);
		g_free(description);
		scan_bodies(bodies, &iter, body->parts, ignore_html, 0, decrypted, stats);
	    }
	    break;

	default:
	    break;
	}

	body = body->next;
    }
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
			    g_string_append_printf(q_body, "\n------%s “%s”------\n",
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
unselect_decrypted(GtkTreeModel              *model,
				   GtkTreePath G_GNUC_UNUSED *path,
				   GtkTreeIter               *iter,
				   gpointer G_GNUC_UNUSED     data)
{
    gboolean decrypted;

    gtk_tree_model_get(model, iter, QUOTE_DECRYPTED, &decrypted, -1);
    if (decrypted) {
    	gtk_tree_store_set(GTK_TREE_STORE(model), iter,
		   QUOTE_INCLUDE, FALSE, -1);
    }
	return FALSE;
}

static gboolean
quote_parts_select_dlg(GtkTreeStore               *tree_store,
					   GtkWindow                  *parent,
					   const reply_collect_stat_t *stats)
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
    GtkWidget *content_box;

    dialog = gtk_dialog_new_with_buttons(_("Select parts for quotation"),
					 parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT |
                                         libbalsa_dialog_flags(),
					 _("_OK"), GTK_RESPONSE_OK,
					 _("_Cancel"), GTK_RESPONSE_CANCEL,
					 NULL);
    geometry_manager_attach(GTK_WINDOW(dialog), "SelectReplyParts");

    label = libbalsa_create_wrap_label(_("Select the parts of the message"
                            " which shall be quoted in the reply"), FALSE);
    gtk_widget_set_valign(label, GTK_ALIGN_START);

    image = gtk_image_new_from_icon_name("dialog-question",
                                         GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_valign(image, GTK_ALIGN_START);

    /* stolen form gtk/gtkmessagedialog.c */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2 * HIG_PADDING);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2 * HIG_PADDING);

    gtk_container_add(GTK_CONTAINER(vbox), label);
    gtk_container_add(GTK_CONTAINER(hbox), image);

    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_halign(vbox, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);

    content_box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(content_box), hbox);

    if (stats->decrypted > 0U) {
    	GtkWidget *warning;

    	if (stats->decrypted != stats->parts) {
    		warning = libbalsa_create_wrap_label(
    			_("<b>Warning:</b> The original message contains an abnormal "
    			  "mixture of encrypted and unencrypted parts. This "
    			  "<i>might</i> indicate an attack.\nDouble-check the contents "
    			  "of the reply before sending."), TRUE);
    		gtk_tree_model_foreach(GTK_TREE_MODEL(tree_store), unselect_decrypted, NULL);
    	} else  {
    		warning = libbalsa_create_wrap_label(
    			_("You reply to an encrypted message. The reply will contain "
    			  "the decrypted contents of the original message.\n"
    			  "Consider to encrypt the reply, and verify that you do not "
    			  "unintentionally leak sensitive information."), FALSE);
    	}
        gtk_widget_set_valign(warning, GTK_ALIGN_START);
        gtk_container_add(GTK_CONTAINER(vbox), warning);
    }

    gtk_container_set_border_width(GTK_CONTAINER(dialog), HIG_PADDING);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), HIG_PADDING);
    gtk_box_set_spacing(GTK_BOX(content_box), 14);

    /* scrolled window for the tree view */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_valign(scroll, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(vbox), scroll);

    /* add the tree view */
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(tree_store));
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
							  "style", QUOTE_STYLE,
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

static void
show_decrypted_warning(GtkWindow *parent)
{
	GtkWidget *dialog;
	GtkWidget *message_area;
	GtkWidget *remind_btn;

	dialog = gtk_message_dialog_new(parent,
		GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
		GTK_MESSAGE_WARNING,
		GTK_BUTTONS_CLOSE,
		_("You reply to an encrypted message. The reply will contain "
		  "the decrypted contents of the original message.\n"
		  "Consider to encrypt the reply, and verify that you do not "
		  "unintentionally leak sensitive information."));
        message_area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));

	remind_btn = gtk_check_button_new_with_label(_("Do not remind me again."));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remind_btn), FALSE);
	gtk_widget_show(remind_btn);
	gtk_container_add(GTK_CONTAINER(message_area), remind_btn);
	gtk_dialog_run(GTK_DIALOG(dialog));
	balsa_app.warn_reply_decrypted = !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remind_btn));
	gtk_widget_destroy(dialog);
}

static GString *
collect_for_quote(BalsaSendmsg        * bsmsg,
                  LibBalsaMessageBody * root,
                  gchar               * reply_prefix_str,
                  gint                  llen,
                  gboolean              ignore_html,
                  gboolean              flow)
{
    GtkTreeStore * tree_store;
    LibBalsaMessage *message;
    GString *q_body = NULL;
    reply_collect_stat_t stats;

    if (!root)
        return q_body;

    message = root->message;
    libbalsa_message_body_ref(message, FALSE);

    /* scan the message and collect text parts which might be included
     * in the reply, and if there is only one return this part */
    tree_store = gtk_tree_store_new(QOUTE_NUM_ELEMS,
				    G_TYPE_BOOLEAN, G_TYPE_STRING,
				    G_TYPE_POINTER, PANGO_TYPE_STYLE,
					G_TYPE_BOOLEAN);
    stats.parts = 0U;
    stats.decrypted = 0U;
    scan_bodies(tree_store, NULL, root, ignore_html, FALSE, FALSE, &stats);
    if (stats.parts == 1U) {
	/* note: the only text body may be buried in an attached message, so
	 * we have to search the tree store... */
	LibBalsaMessageBody *this_body;

	if ((stats.decrypted > 0U) && balsa_app.warn_reply_decrypted) {
		show_decrypted_warning(GTK_WINDOW(bsmsg->window));
	}
	gtk_tree_model_foreach(GTK_TREE_MODEL(tree_store), tree_find_single_part,
			       &this_body);
	if (this_body)
	    q_body = process_mime_part(message, this_body, reply_prefix_str,
				       llen, FALSE, flow);
    } else if (stats.parts > 1U) {
	if (quote_parts_select_dlg(tree_store, GTK_WINDOW(bsmsg->window), &stats)) {
	    GtkTreeIter iter;

	    q_body = g_string_new("");
	    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tree_store), &iter);
	    append_parts(q_body, message, GTK_TREE_MODEL(tree_store), &iter, NULL,
			 reply_prefix_str, llen, flow);
	}
    }

    /* clean up */
    g_object_unref(tree_store);
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
		internet_address_list_to_string(headers->from, NULL,
			                        FALSE);
	    g_string_append_printf(body, "%s %s\n", _("From:"), from);
	    g_free(from);
	}

	if (internet_address_list_length(headers->to_list) > 0) {
	    gchar *to_list =
		internet_address_list_to_string(headers->to_list, NULL,
			                        FALSE);
	    g_string_append_printf(body, "%s %s\n", _("To:"), to_list);
	    g_free(to_list);
	}

	if (internet_address_list_length(headers->cc_list) > 0) {
	    gchar *cc_list =
		internet_address_list_to_string(headers->cc_list, NULL,
			                        FALSE);
	    g_string_append_printf(body, "%s %s\n", _("CC:"), cc_list);
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
	body = collect_for_quote(bsmsg, root,
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

    g_return_if_fail(body != NULL);

    if(body->len && body->str[body->len] != '\n')
        g_string_append_c(body, '\n');
    gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);

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

    if (libbalsa_message_body_ref(message, FALSE)) {
        res = quote_body(bsmsg,
                         libbalsa_message_get_headers(message),
                         libbalsa_message_get_message_id(message),
                         libbalsa_message_get_references(message),
                         libbalsa_message_get_body_list(message),
                         qtype);
        libbalsa_message_body_unref(message);
    } else res = g_string_new("");
    return res;
}

static void
fill_body_from_message(BalsaSendmsg *bsmsg, LibBalsaMessage *message,
                       QuoteType qtype)
{
    fill_body_from_part(bsmsg,
                        libbalsa_message_get_headers(message),
                        libbalsa_message_get_message_id(message),
                        libbalsa_message_get_references(message),
                        libbalsa_message_get_body_list(message),
                        qtype);
}


static void
sw_insert_sig_activated(GSimpleAction * action,
                        GVariant      * parameter,
                        gpointer        data)
{
    BalsaSendmsg *bsmsg = data;
    gchar *signature;
    GError *error = NULL;

    signature = libbalsa_identity_get_signature(bsmsg->ident, &error);

    if (signature != NULL) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
#if !HAVE_GTKSOURCEVIEW
        sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */
        sw_buffer_signals_block(bsmsg, buffer);
        gtk_text_buffer_insert_at_cursor(buffer, signature, -1);
        sw_buffer_signals_unblock(bsmsg, buffer);

	g_free(signature);
    } else if (error != NULL) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   "%s", error->message);
        g_error_free(error);
    }
}


static void
sw_quote_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        data)
{
    BalsaSendmsg *bsmsg = data;

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
    const gchar *forward_string = libbalsa_identity_get_forward_string(ident);

    if (!orig_subject) {
        if (headers && headers->from)
            newsubject = g_strdup_printf("%s from %s",
                                         forward_string,
                                         libbalsa_address_get_mailbox_from_list
                                         (headers->from));
        else
            newsubject = g_strdup(forward_string);
    } else {
        const char *tmp = orig_subject;
        if (g_ascii_strncasecmp(tmp, "fwd:", 4) == 0) {
            tmp += 4;
        } else if (g_ascii_strncasecmp(tmp, _("Fwd:"),
                                       strlen(_("Fwd:"))) == 0) {
            tmp += strlen(_("Fwd:"));
        } else {
            size_t i = strlen(forward_string);
            if (g_ascii_strncasecmp(tmp, forward_string, i) == 0) {
                tmp += i;
            }
        }
        while( *tmp && isspace((int)*tmp) ) tmp++;
        if (headers && headers->from)
            newsubject =
                g_strdup_printf("%s %s [%s]",
                                forward_string,
                                tmp,
                                libbalsa_address_get_mailbox_from_list
                                (headers->from));
        else {
            newsubject =
                g_strdup_printf("%s %s",
                                forward_string,
                                tmp);
            g_strchomp(newsubject);
        }
    }
    return newsubject;
}
/* bsmsg_set_subject_from_body:
   set subject entry based on given replied/forwarded/continued message
   and the compose type.
*/
static void
bsmsg_set_subject_from_body(BalsaSendmsg * bsmsg,
                            LibBalsaMessageBody * part,
                            LibBalsaIdentity * ident)
{
    const gchar *reply_string = libbalsa_identity_get_reply_string(ident);
    gchar *subject;

    if (!part)
        return;
    subject = message_part_get_subject(part);

    if (!bsmsg->is_continue) {
        gchar *newsubject = NULL;
        const gchar *tmp;
        LibBalsaMessageHeaders *headers;

        switch (bsmsg->type) {
        case SEND_REPLY:
        case SEND_REPLY_ALL:
        case SEND_REPLY_GROUP:
            if (!subject) {
                subject = g_strdup(reply_string);
                break;
            }

            tmp = subject;
            if (g_ascii_strncasecmp(tmp, "re:", 3) == 0 ||
                g_ascii_strncasecmp(tmp, "aw:", 3) == 0)
                tmp += 3;
            else if (g_ascii_strncasecmp(tmp, _("Re:"), strlen(_("Re:")))
                       == 0)
                tmp += strlen(_("Re:"));
            else {
                gint len = strlen(reply_string);
                if (g_ascii_strncasecmp(tmp, reply_string, len) == 0)
                    tmp += len;
            }
            while (*tmp && isspace((int) *tmp))
                tmp++;
            newsubject = g_strdup_printf("%s %s", reply_string, tmp);
            g_strchomp(newsubject);
            g_strdelimit(newsubject, "\r\n", ' ');
            break;

        case SEND_FORWARD_ATTACH:
        case SEND_FORWARD_INLINE:
            headers =
                part->embhdrs ? part->embhdrs : libbalsa_message_get_headers(part->message);
            newsubject =
                generate_forwarded_subject(subject, headers, ident);
            break;
        default:
            break;
        }

        if (newsubject) {
            g_free(subject);
            subject = newsubject;
        }
    }

    gtk_entry_set_text(GTK_ENTRY(bsmsg->subject.body), subject);
    g_free(subject);
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

    if (bsmsg->draft_message != NULL) {
        LibBalsaMailbox *mailbox;

        g_object_set_data(G_OBJECT(bsmsg->draft_message),
                          BALSA_SENDMSG_WINDOW_KEY, NULL);
	mailbox = libbalsa_message_get_mailbox(bsmsg->draft_message);
	if (mailbox != NULL) {
	    libbalsa_mailbox_close(mailbox,
		    /* Respect pref setting: */
				   balsa_app.expunge_on_close);
        }
	g_object_unref(bsmsg->draft_message);
    }
    bsmsg->state = SENDMSG_STATE_CLEAN;

    bsmsg->draft_message =
	libbalsa_mailbox_get_message(balsa_app.draftbox,
				     libbalsa_mailbox_total_messages
				     (balsa_app.draftbox));
    g_object_set_data(G_OBJECT(bsmsg->draft_message),
                      BALSA_SENDMSG_WINDOW_KEY, bsmsg);
    balsa_information_parented(GTK_WINDOW(bsmsg->window),
                               LIBBALSA_INFORMATION_MESSAGE,
                               _("Message saved."));

    return TRUE;
}

static gboolean
sw_autosave_timeout_cb(BalsaSendmsg * bsmsg)
{
    if (bsmsg->state == SENDMSG_STATE_MODIFIED) {
        if (sw_save_draft(bsmsg))
            bsmsg->state = SENDMSG_STATE_AUTO_SAVED;
    }

    return TRUE;                /* do repeat it */
}

static void
setup_headers_from_message(BalsaSendmsg* bsmsg, LibBalsaMessage *message)
{
    LibBalsaMessageHeaders *headers;

    headers = libbalsa_message_get_headers(message);

    /* Try to make the blank line in the address view useful;
     * - never make it a Bcc: line;
     * - if Cc: is non-empty, make it a Cc: line;
     * - if Cc: is empty, make it a To: line
     * Note that if set-from-list is given an empty list, the blank line
     * will be a To: line */
    libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                        "BCC:",
                                        headers->bcc_list);
    libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                        "To:",
                                        headers->to_list);
    libbalsa_address_view_set_from_list(bsmsg->recipient_view,
                                        "CC:",
                                        headers->cc_list);
}


/*
 * set_identity_from_mailbox
 *
 * Attempt to determine the default identity from the mailbox containing
 * the message.
 **/
static gboolean
set_identity_from_mailbox(BalsaSendmsg *bsmsg, LibBalsaMessage *message)
{
    LibBalsaMailbox *mailbox;
    const gchar *identity;
    GList *ilist;

    if (message == NULL)
        return FALSE;

    mailbox = libbalsa_message_get_mailbox(message);
    if (mailbox == NULL)
        return FALSE;

    identity = libbalsa_mailbox_get_identity_name(mailbox);
    if (identity == NULL)
        return FALSE;

    for (ilist = balsa_app.identities; ilist != NULL; ilist = ilist->next) {
        LibBalsaIdentity *ident = LIBBALSA_IDENTITY(ilist->data);
        const gchar *name = libbalsa_identity_get_identity_name(ident);

        if (g_ascii_strcasecmp(name, identity) == 0) {
            g_set_object(&bsmsg->ident, ident);
            return TRUE;
        }
    }

    return FALSE;               /* use default */
}

/*
 * guess_identity
 *
 * Attempt to determine if a message should be associated with a
 * particular identity, other than the default.  The to_list of the
 * original message needs to be set in order for it to work.
 **/
/* First a helper; groups cannot be nested, and are not allowed in the
 * From: list. */
/* Update: RFC 6854 allows groups in "From:" and "Sender:" */
static gboolean
guess_identity_from_list(BalsaSendmsg * bsmsg, InternetAddressList * list,
                         gboolean allow_group)
{
    gint i;

    if (!list)
        return FALSE;

    allow_group = TRUE;
    for (i = 0; i < internet_address_list_length(list); i++) {
        InternetAddress *ia = internet_address_list_get_address(list, i);

        if (INTERNET_ADDRESS_IS_GROUP(ia)) {
            InternetAddressList *members =
                INTERNET_ADDRESS_GROUP(ia)->members;
            if (allow_group
                && guess_identity_from_list(bsmsg, members, FALSE))
                return TRUE;
        } else {
            GList *l;

            for (l = balsa_app.identities; l; l = l->next) {
                LibBalsaIdentity *ident = LIBBALSA_IDENTITY(l->data);
                if (libbalsa_ia_rfc2821_equal(libbalsa_identity_get_address(ident),
                                              ia)) {
                    g_set_object(&bsmsg->ident, ident);
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

static gboolean
guess_identity(BalsaSendmsg* bsmsg, LibBalsaMessage * message)
{
    LibBalsaMessageHeaders *headers;

    if (message == NULL || balsa_app.identities == NULL)
        return FALSE; /* use default */

    headers = libbalsa_message_get_headers(message);
    if (headers == NULL)
        return FALSE; /* use default */

    if (bsmsg->is_continue)
        return guess_identity_from_list(bsmsg, headers->from, FALSE);

    if (bsmsg->type != SEND_NORMAL)
	/* bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL ||
	*  bsmsg->type == SEND_REPLY_GROUP || bsmsg->type == SEND_FORWARD_ATTACH ||
	*  bsmsg->type == SEND_FORWARD_INLINE */
        return guess_identity_from_list(bsmsg, headers->to_list, TRUE)
            || guess_identity_from_list(bsmsg, headers->cc_list, TRUE);

    return FALSE;
}

static void
setup_headers_from_identity(BalsaSendmsg* bsmsg, LibBalsaIdentity *ident)
{
    const gchar *addr;

    gtk_combo_box_set_active(GTK_COMBO_BOX(bsmsg->from.body),
                             g_list_index(balsa_app.identities, ident));

    addr = libbalsa_identity_get_replyto(ident);
    if (addr != NULL)
        libbalsa_address_view_set_from_string(bsmsg->replyto_view,
                                              "Reply To:",
                                              addr);

    addr = libbalsa_identity_get_bcc(ident);
    if (addr != NULL)
        libbalsa_address_view_set_from_string(bsmsg->recipient_view,
                                              "BCC:",
                                              addr);

    /* Make sure the blank line is "To:" */
    libbalsa_address_view_add_from_string(bsmsg->recipient_view,
                                          "To:", NULL);
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

   Returns the current locale if any dictionaries were found
   and the menu was created;
   returns NULL if no dictionaries were found,
   in which case spell-checking must be disabled.
*/
#define BALSA_LANGUAGE_MENU_LANG "balsa-language-menu-lang"
#if !HAVE_GSPELL && !HAVE_GTKSPELL
static void
sw_broker_cb(const gchar * lang_tag,
             const gchar * provider_name,
             const gchar * provider_desc,
             const gchar * provider_file,
             gpointer      data)
{
    GList **lang_list = data;

    *lang_list = g_list_insert_sorted(*lang_list, g_strdup(lang_tag),
                                      (GCompareFunc) strcmp);
}
#endif                          /* !HAVE_GSPELL && !HAVE_GTKSPELL */

static const gchar *
create_lang_menu(GtkWidget * parent, BalsaSendmsg * bsmsg)
{
    guint i;
    GtkWidget *langs;
    static gboolean locales_sorted = FALSE;
    GSList *group = NULL;
#if HAVE_GSPELL
    const GList *lang_list, *l;
#else
    GList *lang_list, *l;
#endif                          /* HAVE_GSPELL */
#if !HAVE_GSPELL && !HAVE_GTKSPELL
    EnchantBroker *broker;
#endif                          /* !HAVE_GSPELL && !HAVE_GTKSPELL */
    const gchar *preferred_lang;
    GtkWidget *active_item = NULL;

#if HAVE_GTKSPELL
    lang_list = gtk_spell_checker_get_language_list();
#elif HAVE_GSPELL
    lang_list = gspell_language_get_available();
#else                           /* HAVE_GTKSPELL */
    broker = enchant_broker_init();
    lang_list = NULL;
    enchant_broker_list_dicts(broker, sw_broker_cb, &lang_list);
    enchant_broker_free(broker);
#endif                          /* HAVE_GTKSPELL */

    if (lang_list == NULL) {
        return NULL;
    }

    if (!locales_sorted) {
        for (i = 0; i < G_N_ELEMENTS(locales); i++)
            locales[i].lang_name = _(locales[i].lang_name);
        qsort(locales, G_N_ELEMENTS(locales), sizeof(struct SendLocales),
              comp_send_locales);
        locales_sorted = TRUE;
    }

    /* find the preferred charset... */
    preferred_lang = balsa_app.spell_check_lang ?
        balsa_app.spell_check_lang : setlocale(LC_CTYPE, NULL);

    langs = gtk_menu_new();
    for (i = 0; i < G_N_ELEMENTS(locales); i++) {
        gconstpointer found;

        if (locales[i].locale == NULL || locales[i].locale[0] == '\0')
            /* GtkSpell handles NULL lang, but complains about empty
             * lang; in either case, it does not go in the langs menu. */
            continue;

#if HAVE_GSPELL
        found = gspell_language_lookup(locales[i].locale);
#else                           /* HAVE_GSPELL */
        found = g_list_find_custom(lang_list, locales[i].locale,
                                   (GCompareFunc) strcmp);
#endif                          /* HAVE_GSPELL */
        if (found != NULL) {
            GtkWidget *w;

            w = gtk_radio_menu_item_new_with_mnemonic(group,
                                                      locales[i].
                                                      lang_name);
            group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(w));
            g_signal_connect(w, "activate",
                             G_CALLBACK(lang_set_cb), bsmsg);
            g_object_set_data_full(G_OBJECT(w), BALSA_LANGUAGE_MENU_LANG,
                                   g_strdup(locales[i].locale), g_free);
            gtk_widget_show(w);
            gtk_menu_shell_append(GTK_MENU_SHELL(langs), w);

            if (!active_item || strcmp(preferred_lang, locales[i].locale) == 0)
                active_item = w;
        }
    }

    /* Add to the langs menu any available languages that are
     * not listed in locales[] */
    for (l = lang_list; l; l = l->next) {
#if HAVE_GSPELL
        const GspellLanguage *language = l->data;
        const gchar *lang = gspell_language_get_code(language);
#else                           /* HAVE_GSPELL */
        const gchar *lang = l->data;
#endif                          /* HAVE_GSPELL */
        gint j;

        j = find_locale_index_by_locale(lang);
        if (j < 0 || strcmp(lang, locales[j].locale) != 0) {
            GtkWidget *w;

            w = gtk_radio_menu_item_new_with_label(group, lang);
            group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(w));
            g_signal_connect(w, "activate",
                             G_CALLBACK(lang_set_cb), bsmsg);
            g_object_set_data_full(G_OBJECT(w), BALSA_LANGUAGE_MENU_LANG,
                                   g_strdup(lang), g_free);
            gtk_widget_show(w);
            gtk_menu_shell_append(GTK_MENU_SHELL(langs), w);

            if (!active_item || strcmp(preferred_lang, lang) == 0)
                active_item = w;
        }
    }
#if !HAVE_GSPELL
    g_list_free_full(lang_list, (GDestroyNotify) g_free);
#endif                          /* HAVE_GSPELL */

    g_signal_handlers_block_by_func(active_item, lang_set_cb, bsmsg);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(active_item), TRUE);
    g_signal_handlers_unblock_by_func(active_item, lang_set_cb, bsmsg);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), langs);
    gtk_widget_show(parent);

    return g_object_get_data(G_OBJECT(active_item), BALSA_LANGUAGE_MENU_LANG);
}

/* Standard buttons; "" means a separator. */
static const BalsaToolbarEntry compose_toolbar[] = {
    { "toolbar-send", BALSA_PIXMAP_SEND       },
    { "", ""                                  },
    { "attach-file",  BALSA_PIXMAP_ATTACHMENT },
    { "", ""                                  },
    { "save",        "document-save"          },
    { "", ""                                  },
    { "undo",        "edit-undo"              },
    { "redo",        "edit-redo"              },
    { "", ""                                  },
    { "select-ident", BALSA_PIXMAP_IDENTITY   },
    { "", ""                                  },
    { "spell-check", "tools-check-spelling"   },
    { "", ""                                  },
    {"print",        "document-print"         },
    { "", ""                                  },
    {"close",        "window-close-symbolic"  }
};

/* Optional extra buttons */
static const BalsaToolbarEntry compose_toolbar_extras[] = {
    { "postpone",    BALSA_PIXMAP_POSTPONE    },
    { "request-mdn", BALSA_PIXMAP_REQUEST_MDN },
    { "sign",        BALSA_PIXMAP_GPG_SIGN    },
    { "encrypt",     BALSA_PIXMAP_GPG_ENCRYPT },
    { "edit",        "gtk-edit"               },
	{ "queue",		 BALSA_PIXMAP_QUEUE		  }
};

/* Create the toolbar model for the compose window's toolbar.
 */
BalsaToolbarModel *
sendmsg_window_get_toolbar_model(void)
{
    static BalsaToolbarModel *model = NULL;

    if (model)
        return model;

    model =
        balsa_toolbar_model_new(BALSA_TOOLBAR_TYPE_COMPOSE_WINDOW,
                                compose_toolbar,
                                G_N_ELEMENTS(compose_toolbar));
    balsa_toolbar_model_add_entries(model, compose_toolbar_extras,
                                    G_N_ELEMENTS(compose_toolbar_extras));

    return model;
}

static void
bsmsg_identities_changed_cb(BalsaSendmsg * bsmsg)
{
    sw_action_set_enabled(bsmsg, "select-ident",
                     balsa_app.identities->next != NULL);
}

static void
sw_cc_add_list(InternetAddressList **new_cc, InternetAddressList * list)
{
    int i;

    if (!list)
        return;

    for (i = 0; i < internet_address_list_length(list); i++) {
        InternetAddress *ia = internet_address_list_get_address (list, i);
	GList *ilist;

	/* do not insert any of my identities into the cc: list */
	for (ilist = balsa_app.identities; ilist != NULL; ilist = ilist->next) {
	    if (libbalsa_ia_rfc2821_equal
		(ia, libbalsa_identity_get_address(ilist->data)))
		break;
        }
	if (ilist == NULL) {
            if (*new_cc == NULL)
                *new_cc = internet_address_list_new();
	    internet_address_list_add(*new_cc, ia);
        }
    }
}

static void
insert_initial_sig(BalsaSendmsg *bsmsg)
{
    GtkTextIter sig_pos;
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    if(libbalsa_identity_get_sig_prepend(bsmsg->ident))
        gtk_text_buffer_get_start_iter(buffer, &sig_pos);
    else
        gtk_text_buffer_get_end_iter(buffer, &sig_pos);
    gtk_text_buffer_insert(buffer, &sig_pos, "\n", 1);
    sw_insert_sig_activated(NULL, NULL, bsmsg);
    gtk_text_buffer_get_start_iter(buffer, &sig_pos);
    gtk_text_buffer_place_cursor(buffer, &sig_pos);
}

static void
bsm_prepare_for_setup(LibBalsaMessage *message)
{
    LibBalsaMailbox *mailbox;

    mailbox = libbalsa_message_get_mailbox(message);
    if (mailbox != NULL)
        libbalsa_mailbox_open(mailbox, NULL);
    /* fill in that info:
     * ref the message so that we have all needed headers */
    libbalsa_message_body_ref(message, TRUE);
    /* scan the message for encrypted parts - this is only possible if
       there is *no* other ref to it */
    balsa_message_perform_crypto(message, LB_MAILBOX_CHK_CRYPT_NEVER,
                                 TRUE, 1);
}

/* libbalsa_message_body_unref() may destroy the @param part - this is
   why body_unref() is done at the end. */
static void
bsm_finish_setup(BalsaSendmsg *bsmsg, LibBalsaMessageBody *part)
{
    LibBalsaMailbox *mailbox;

    mailbox = libbalsa_message_get_mailbox(part->message);
    if (mailbox != NULL &&
        bsmsg->parent_message == NULL && bsmsg->draft_message == NULL)
        libbalsa_mailbox_close(mailbox, FALSE);
    /* ...but mark it as unmodified. */
    bsmsg->state = SENDMSG_STATE_CLEAN;
    bsmsg_set_subject_from_body(bsmsg, part, bsmsg->ident);
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
                                        "CC:",
                                        new_cc);
    if (new_cc)
        g_object_unref(new_cc);
}

static void
set_in_reply_to(BalsaSendmsg *bsmsg, const gchar *message_id)
{
    if (message_id != NULL) {
        if (message_id[0] == '<') {
            bsmsg->in_reply_to = g_strdup(message_id);
        } else {
            bsmsg->in_reply_to = g_strconcat("<", message_id, ">", NULL);
        }
    }
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
    gtk_widget_grab_focus(text);
    g_object_unref(text);
    return FALSE;
}

/* decode_and_strdup:
   decodes given URL string up to the delimiter and places the
   eos pointer in newstr if supplied (eos==NULL if end of string was reached)
*/
static gchar *
decode_and_strdup(const gchar * str, int delim, const gchar ** newstr)
{
    GString *s = g_string_new(NULL);
    /* eos points to the character after the last to parse */
    const gchar *eos = strchr(str, delim);

    if (eos == NULL)
        eos = str + strlen(str);

    while (str < eos) {
        switch (*str) {
        case '+':
            g_string_append_c(s, ' ');
            str++;
            break;
        case '%':
            if (str + 2 < eos) {
                gchar num[3] = {str[1], str[2], '\0'};

                g_string_append_c(s, strtol(num, NULL, 16));
            }
            str += 3;
            break;
        default:
            g_string_append_c(s, *str++);
        }
    }

    if (newstr != NULL)
        *newstr = *eos != '\0' ? eos + 1 : NULL;

    return g_string_free(s, FALSE);
}

/* process_url:
   extracts all characters until NUL or question mark; parse later fields
   of format 'key'='value' with ampersands as separators.
*/
void
sendmsg_window_process_url(BalsaSendmsg *bsmsg,
                           const char   *url,
                           gboolean      from_command_line)
{
    const gchar *ptr;
    gchar *to, *key, *val;

    to = decode_and_strdup(url,'?', &ptr);
    sendmsg_window_set_field(bsmsg, "to", to, from_command_line);
    g_free(to);
    while(ptr) {
	key = decode_and_strdup(ptr,'=', &ptr);
	if(ptr) {
	    val = decode_and_strdup(ptr,'&', &ptr);
	    sendmsg_window_set_field(bsmsg, key, val, from_command_line);
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
    gchar *filename;

    g_return_if_fail(val != NULL);
    if (g_ascii_strncasecmp(val, "file://", 7UL) == 0) {
        GError *error = NULL;

        filename = g_filename_from_uri(val, NULL, &error);
        if (filename == NULL) {
            balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                       LIBBALSA_INFORMATION_WARNING,
                                       _("Could not attach the file %s: %s."), val,
                                       error->message);
            g_error_free(error);
            goto cleanup;
        }
    } else {
        filename = g_strdup(val);
    }

    if (!g_path_is_absolute(filename)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not attach the file %s: %s."), filename,
                                   _("not an absolute path"));
        goto cleanup;
    }

    if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not attach the file %s: %s."), filename,
                                   _("does not exist"));
        goto cleanup;
    }

    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not attach the file %s: %s."), filename,
                                   _("not a regular file"));
        goto cleanup;
    }
    attach = g_object_get_data(G_OBJECT(bsmsg->window),
                               "balsa-sendmsg-window-attach-dialog");
    if (!attach) {
        attach = sw_attach_dialog(bsmsg);
        g_object_set_data(G_OBJECT(bsmsg->window),
                          "balsa-sendmsg-window-attach-dialog", attach);
        g_object_set_data_full(G_OBJECT(attach),
                               "balsa-sendmsg-window-attach-dir",
                               g_path_get_dirname(filename), g_free);
    } else {
        gchar *dirname = g_object_get_data(G_OBJECT(attach),
                                           "balsa-sendmsg-window-attach-dir");
        gchar *valdir = g_path_get_dirname(filename);
        gboolean good = (strcmp(dirname, valdir) == 0);

        g_free(valdir);
        if (!good) {
            /* gtk_file_chooser_select_filename will crash */
            balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                       LIBBALSA_INFORMATION_WARNING,
                                       _("Could not attach the file %s: %s."), filename,
                                       _("not in current directory"));
            goto cleanup;
        }
    }

    gtk_file_chooser_select_filename(attach, filename);

cleanup:
    g_free(filename);
}
#endif

void
sendmsg_window_set_field(BalsaSendmsg * bsmsg, const gchar * key,
                         const gchar * val, gboolean from_command_line)
{
    const gchar *type;
    g_return_if_fail(bsmsg);

    if (g_ascii_strcasecmp(key, "body") == 0) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

        gtk_text_buffer_insert_at_cursor(buffer, val, -1);

        return;
    }
#if defined(NO_SECURITY_ISSUES_WITH_ATTACHMENTS)
    if (from_command_line &&
        (g_ascii_strcasecmp(key, "attach") == 0 ||
         g_ascii_strcasecmp(key, "attachment") == 0)) {
        sw_attach_file(bsmsg, val);
        return;
    }
#endif
    if(g_ascii_strcasecmp(key, "subject") == 0) {
        append_comma_separated(GTK_EDITABLE(bsmsg->subject.body), val);
        gtk_widget_show_all(bsmsg->subject.name);
        gtk_widget_show_all(bsmsg->subject.body);
        return;
    }

    if (g_ascii_strcasecmp(key, "to") == 0)
        type = "To:";
    else if(g_ascii_strcasecmp(key, "cc") == 0)
        type = "CC:";
    else if(g_ascii_strcasecmp(key, "bcc") == 0) {
        type = "BCC:";
        if (!g_object_get_data(G_OBJECT(bsmsg->window),
                               "balsa-sendmsg-window-url-bcc")) {
            GtkWidget *dialog =
                gtk_message_dialog_new
                (GTK_WINDOW(bsmsg->window),
                 GTK_DIALOG_DESTROY_WITH_PARENT,
                 GTK_MESSAGE_INFO,
                 GTK_BUTTONS_OK,
                 _("The link that you selected created\n"
                   "a “Blind copy” (BCC) address.\n"
                   "Please check that the address\n"
                   "is appropriate."));
            g_object_set_data(G_OBJECT(bsmsg->window),
                              "balsa-sendmsg-window-url-bcc", dialog);
            g_signal_connect(dialog, "response",
                             G_CALLBACK(gtk_widget_destroy), NULL);
            gtk_widget_show_all(dialog);
        }
    }
    else if(g_ascii_strcasecmp(key, "replyto") == 0) {
        libbalsa_address_view_add_from_string(bsmsg->replyto_view,
                                              "Reply To:",
                                              val);
        return;
    }
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

        g_debug("Trying charset: %s", charset);
        if (sw_can_convert(string, len, "UTF-8", charset, &s)) {
            gtk_text_buffer_insert_at_cursor(buffer, s, -1);
            g_free(s);
            break;
        }
    } while(1);
}

static void
insert_file_response(GtkWidget    *selector,
					 gint          response,
					 BalsaSendmsg *bsmsg)
{
    GtkFileChooser *fc;
    gchar *fname;

    if (response != GTK_RESPONSE_OK) {
    	gtk_widget_destroy(selector);
    	return;
    }

    fc = GTK_FILE_CHOOSER(selector);
    fname = gtk_file_chooser_get_filename(fc);
    if (fname != NULL) {
        gchar *string;
        gsize len;
        GError *error = NULL;

    	if (g_file_get_contents(fname, &string, &len, &error)) {
            LibBalsaTextAttribute attr;
            GtkTextBuffer *buffer;

            buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
            attr = libbalsa_text_attr_string(string);
            if (!attr || (attr & LIBBALSA_TEXT_HI_UTF8)) {
                /* Ascii or utf-8 */
                gtk_text_buffer_insert_at_cursor(buffer, string, -1);
            } else {
                /* Neither ascii nor utf-8... */
                gchar *s = NULL;
                const gchar *charset = sw_preferred_charset(bsmsg);

                if (sw_can_convert(string, -1, "UTF-8", charset, &s)) {
                    /* ...but seems to be in current charset. */
                    gtk_text_buffer_insert_at_cursor(buffer, s, -1);
                    g_free(s);
                } else
                    /* ...and can't be decoded from current charset. */
                    do_insert_string_select_ch(bsmsg, buffer, string, len, fname);
            }
            g_free(string);

            /* Use the same folder as for attachments. */
            g_free(balsa_app.attach_dir);
            balsa_app.attach_dir = gtk_file_chooser_get_current_folder(fc);
    	} else {
    		balsa_information_parented(GTK_WINDOW(bsmsg->window),
    			LIBBALSA_INFORMATION_WARNING, _("Cannot read the file “%s”: %s"), fname, error->message);
    		g_error_free(error);
    	}

        g_free(fname);
    }

    gtk_widget_destroy(selector);
}

static void
sw_include_file_activated(GSimpleAction * action,
                          GVariant      * parameter,
                          gpointer        data)
{
    BalsaSendmsg *bsmsg = data;
    GtkWidget *file_selector;

    file_selector =
	gtk_file_chooser_dialog_new(_("Include file"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    NULL);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(file_selector), TRUE);
    /* Use the same folder as for attachments. */
    if (balsa_app.attach_dir)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER
                                            (file_selector),
                                            balsa_app.attach_dir);
    g_signal_connect(file_selector, "response",
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
        GRegex *rex;
        char *wrapped;

        gtk_text_buffer_get_iter_at_mark(buffer, &now,
                                         gtk_text_buffer_get_insert(buffer));
        pos = gtk_text_iter_get_offset(&now);

        the_text = gtk_text_iter_get_text(&start, &end);
        rex = balsa_quote_regex_new();
        wrapped = libbalsa_wrap_quoted_string(the_text, balsa_app.browse_wrap_length, rex);
        g_regex_unref(rex);
        g_free(the_text);

        gtk_text_buffer_set_text(buffer, "", 0);
        gtk_text_buffer_insert_at_cursor(buffer, wrapped, -1);
        g_free(wrapped);

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
    if (attachment->file_uri != NULL)
        body->file_uri = g_object_ref(attachment->file_uri);
    body->filename = g_strdup(attachment->uri_ref);
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
        balsa_information(LIBBALSA_INFORMATION_WARNING,
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
    LibBalsaMessageHeaders *headers;
    LibBalsaMessageBody *body;
    gchar *tmp;
    GtkTextIter start, end;
    LibBalsaIdentity *ident = bsmsg->ident;
    InternetAddress *ia = libbalsa_identity_get_address(ident);
    GtkTextBuffer *buffer;
    GtkTextBuffer *new_buffer = NULL;

    message = libbalsa_message_new();
    headers = libbalsa_message_get_headers(message);

    headers->from = internet_address_list_new ();
    internet_address_list_add(headers->from, ia);

    tmp = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->subject.body), 0, -1);
    strip_chars(tmp, "\r\n");
    libbalsa_message_set_subject(message, tmp);
    g_free(tmp);

    headers->to_list =
        libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");

    headers->cc_list =
        libbalsa_address_view_get_list(bsmsg->recipient_view, "CC:");

    headers->bcc_list =
        libbalsa_address_view_get_list(bsmsg->recipient_view, "BCC:");


    /* get the fcc-box from the option menu widget */
    bsmsg->fcc_url =
        g_strdup(balsa_mblist_mru_option_menu_get(bsmsg->fcc.body));

    headers->reply_to =
        libbalsa_address_view_get_list(bsmsg->replyto_view, "Reply To:");

    if (bsmsg->req_mdn)
	libbalsa_message_set_dispnotify(message, ia);
    libbalsa_message_set_request_dsn(message, bsmsg->req_dsn);


    sw_set_header_from_path(message, "Face", libbalsa_identity_get_face_path(ident),
            /* Translators: please do not translate Face. */
                            _("Could not load Face header file %s: %s"));
    sw_set_header_from_path(message, "X-Face", libbalsa_identity_get_x_face_path(ident),
            /* Translators: please do not translate Face. */
                            _("Could not load X-Face header file %s: %s"));

    libbalsa_message_set_references(message, bsmsg->references);
    bsmsg->references = NULL; /* steal it */

    if (bsmsg->in_reply_to != NULL) {
        libbalsa_message_set_in_reply_to(message,
                                         g_list_prepend(NULL,
                                                        g_strdup(bsmsg->in_reply_to)));
    }

    body = libbalsa_message_body_new(message);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    gtk_text_buffer_get_bounds(buffer, &start, &end);

    if (bsmsg->flow) {
        /* Copy the message text to a new buffer: */
        GtkTextTagTable *table;

        table = gtk_text_buffer_get_tag_table(buffer);
        new_buffer = gtk_text_buffer_new(table);

        tmp = gtk_text_iter_get_text(&start, &end);
        gtk_text_buffer_set_text(new_buffer, tmp, -1);
        g_free(tmp);

        /* Remove spaces before a newline: */
        gtk_text_buffer_get_bounds(new_buffer, &start, &end);
        libbalsa_unwrap_buffer(new_buffer, &start, -1);
        gtk_text_buffer_get_bounds(new_buffer, &start, &end);
    }

    /* Copy the buffer text to the message: */
    body->buffer = gtk_text_iter_get_text(&start, &end);
    if (new_buffer)
        g_object_unref(new_buffer);

    if (bsmsg->send_mp_alt)
        body->html_buffer =
            libbalsa_text_to_html(LIBBALSA_MESSAGE_GET_SUBJECT(message), body->buffer,
                                  bsmsg->spell_check_lang);
    if (bsmsg->flow)
	body->buffer =
	    libbalsa_wrap_rfc2646(body->buffer, balsa_app.wraplength,
                                  TRUE, FALSE, TRUE);

    /* Ildar reports that, when a message contains both text/plain and
     * text/html parts, some broken MUAs use the charset from the
     * text/plain part to display the text/html part; the latter is
     * encoded as UTF-8 by add_mime_body_plain (send.c), so we'll use
     * the same encoding for the text/plain part.
     * https://bugzilla.gnome.org/show_bug.cgi?id=580704 */
    body->charset =
        g_strdup(bsmsg->send_mp_alt ?
                 "UTF-8" : sw_required_charset(bsmsg, body->buffer));
    libbalsa_message_append_part(message, body);

    /* add attachments */
    if (bsmsg->tree_view)
        gtk_tree_model_foreach(BALSA_MSG_ATTACH_MODEL(bsmsg),
                               attachment2message, message);

    headers->date = time(NULL);
    if (balsa_app.has_openpgp || balsa_app.has_smime) {
        libbalsa_message_set_crypt_mode(message,
            (bsmsg->crypt_mode & LIBBALSA_PROTECT_MODE) != 0 ? bsmsg->crypt_mode : LIBBALSA_PROTECT_NONE);
        libbalsa_message_set_attach_pubkey(message, bsmsg->attach_pubkey);
        libbalsa_message_set_always_trust(message, bsmsg->always_trust);
        libbalsa_message_set_identity(message, ident);
    } else {
        libbalsa_message_set_crypt_mode(message, LIBBALSA_PROTECT_NONE);
        libbalsa_message_set_attach_pubkey(message, FALSE);
        libbalsa_message_set_always_trust(message, FALSE);
    }

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
    gint response;

    /* read the subject widget and verify that it is contains something else
       than spaces */
    subj = gtk_entry_get_text(GTK_ENTRY(bsmsg->subject.body));
    if (subj) {
	const gchar *p = subj;

	while (*p && g_unichar_isspace(g_utf8_get_char(p)))
	    p = g_utf8_next_char(p);
	if (*p != '\0')
	    return TRUE;
    }

    /* build the dialog */
    no_subj_dialog =
        gtk_dialog_new_with_buttons(_("No Subject"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_DIALOG_MODAL |
                                    libbalsa_dialog_flags(),
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_Send"),   GTK_RESPONSE_OK,
                                    NULL);
    gtk_container_set_border_width (GTK_CONTAINER (no_subj_dialog), HIG_PADDING);
    gtk_window_set_resizable (GTK_WINDOW (no_subj_dialog), FALSE);
    gtk_window_set_type_hint (GTK_WINDOW (no_subj_dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

    dialog_vbox = gtk_dialog_get_content_area(GTK_DIALOG(no_subj_dialog));

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2 * HIG_PADDING);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(dialog_vbox), hbox);
    gtk_container_set_border_width (GTK_CONTAINER (hbox), HIG_PADDING);

    image = gtk_image_new_from_icon_name("dialog-question",
                                         GTK_ICON_SIZE_DIALOG);
    gtk_container_add(GTK_CONTAINER (hbox), image);
    gtk_widget_set_valign(image, GTK_ALIGN_START);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2 * HIG_PADDING);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_halign(vbox, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);

    text_str = g_strdup_printf("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
			       _("You did not specify a subject for this message"),
			       _("If you would like to provide one, enter it below."));
    label = libbalsa_create_wrap_label(text_str, TRUE);
    g_free(text_str);
    gtk_container_add(GTK_CONTAINER (vbox), label);
    gtk_widget_set_valign(label, GTK_ALIGN_START);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, HIG_PADDING);
    gtk_container_add(GTK_CONTAINER (vbox), hbox);

    label = gtk_label_new (_("Subject:"));
    gtk_container_add(GTK_CONTAINER (hbox), label);

    subj_entry = gtk_entry_new ();
    gtk_entry_set_text(GTK_ENTRY(subj_entry), _("(no subject)"));
    gtk_widget_set_hexpand(subj_entry, TRUE);
    gtk_widget_set_halign(subj_entry, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox), subj_entry);

    gtk_entry_set_activates_default (GTK_ENTRY (subj_entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG (no_subj_dialog),
                                    GTK_RESPONSE_OK);

    gtk_widget_grab_focus (subj_entry);
    gtk_editable_select_region(GTK_EDITABLE(subj_entry), 0, -1);
    gtk_widget_show_all(dialog_vbox);

    response = gtk_dialog_run(GTK_DIALOG(no_subj_dialog));

    /* always set the current string in the subject entry */
    gtk_entry_set_text(GTK_ENTRY(bsmsg->subject.body),
		       gtk_entry_get_text(GTK_ENTRY(subj_entry)));
    gtk_widget_destroy(no_subj_dialog);

    return response == GTK_RESPONSE_OK;
}

static void
config_dlg_button(GtkDialog *dialog, gint response_id, const gchar *icon_id)
{
	GtkWidget *button;

	button = gtk_dialog_get_widget_for_response(dialog, response_id);
	if (button != NULL) {
		GtkWidget *image;

		image = gtk_image_new_from_icon_name(icon_id, GTK_ICON_SIZE_BUTTON);
		gtk_button_set_image(GTK_BUTTON(button), image);
	}
}

static gboolean
run_check_encrypt_dialog(BalsaSendmsg *bsmsg, const gchar *secondary_msg, gint default_button)
{
	GtkWidget *dialog;
	gboolean result = TRUE;
	gint choice;

	dialog = gtk_message_dialog_new(GTK_WINDOW(bsmsg->window),
	     GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL | libbalsa_dialog_flags(),
	     GTK_MESSAGE_QUESTION,
	     GTK_BUTTONS_NONE,
		 _("Message could be encrypted"));
	gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(dialog), "%s", secondary_msg);
	gtk_dialog_add_buttons(GTK_DIALOG(dialog),
		_("Send _encrypted"), GTK_RESPONSE_YES,
		_("Send _unencrypted"), GTK_RESPONSE_NO,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), default_button);

	/* add button images */
	config_dlg_button(GTK_DIALOG(dialog), GTK_RESPONSE_YES, balsa_icon_id(BALSA_PIXMAP_GPG_ENCRYPT));
	config_dlg_button(GTK_DIALOG(dialog), GTK_RESPONSE_NO, balsa_icon_id(BALSA_PIXMAP_SEND));

	choice = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	if (choice == GTK_RESPONSE_YES) {
	    bsmsg_setup_gpg_ui_by_mode(bsmsg, bsmsg->crypt_mode | LIBBALSA_PROTECT_ENCRYPT);
	} else if ((choice == GTK_RESPONSE_CANCEL) || (choice == GTK_RESPONSE_DELETE_EVENT)) {
	    result = FALSE;
    } else {
    	/* nothing to do */
    }

	return result;
}

static gboolean
check_suggest_encryption(BalsaSendmsg * bsmsg)
{
    InternetAddressList * ia_list;
    gboolean can_encrypt;
    gpgme_protocol_t protocol;
    gint len;
    gboolean result = TRUE;

    /* check if the user wants to see the message */
    if (bsmsg->ident == NULL ||
        !libbalsa_identity_get_warn_send_plain(bsmsg->ident))
	return TRUE;

    /* nothing to do if encryption is already enabled */
    if ((bsmsg->crypt_mode & LIBBALSA_PROTECT_ENCRYPT) != 0)
	return TRUE;

    /* we can not encrypt if we have bcc recipients */
    ia_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "BCC:");
    len = internet_address_list_length(ia_list);
    g_object_unref(ia_list);
    if (len > 0)
        return TRUE;

    /* collect all to and cc recipients */
    protocol = (bsmsg->crypt_mode & LIBBALSA_PROTECT_SMIME) ?
	GPGME_PROTOCOL_CMS : GPGME_PROTOCOL_OpenPGP;

    ia_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");
    can_encrypt = libbalsa_can_encrypt_for_all(ia_list, protocol);
    g_object_unref(ia_list);
    if (can_encrypt) {
        ia_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "CC:");
        can_encrypt = libbalsa_can_encrypt_for_all(ia_list, protocol);
        g_object_unref(ia_list);
    }
    if (can_encrypt) {
        ia_list = internet_address_list_new();
        internet_address_list_add(ia_list, libbalsa_identity_get_address(bsmsg->ident));
        can_encrypt = libbalsa_can_encrypt_for_all(ia_list, protocol);
        g_object_unref(ia_list);
    }

    /* ask the user if we should encrypt this message */
    if (can_encrypt) {
    	gchar *message;

    	message = g_markup_printf_escaped(_("You did not select encryption for this message, although "
    		"%s public keys are available for all recipients. In order "
            "to protect your privacy, the message could be %s encrypted."),
    		libbalsa_gpgme_protocol_name(protocol), libbalsa_gpgme_protocol_name(protocol));
    	result = run_check_encrypt_dialog(bsmsg, message, GTK_RESPONSE_YES);
    	g_free(message);
    }
    return result;
}

#ifdef ENABLE_AUTOCRYPT
static gboolean
import_autocrypt_keys(GList *missing_keys, GError **error)
{
	gpgme_ctx_t ctx;
	gboolean result;

	ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, error);
	if (ctx != NULL) {
		GList *key;

		result = TRUE;
		for (key = missing_keys; result && (key != NULL); key = key->next) {
			result = libbalsa_gpgme_import_bin_key(ctx, (GBytes *) key->data, NULL, error);
		}
		gpgme_release(ctx);
	} else {
		result = FALSE;
	}

	return result;
}

static gboolean
check_autocrypt_recommendation(BalsaSendmsg *bsmsg)
{
    InternetAddressList *check_list;
    InternetAddressList *tmp_list;
    gint len;
    AutocryptRecommend autocrypt_mode;
    GList *missing_keys = NULL;
    GError *error = NULL;
    gboolean result;

    /* check if autocrypt is enabled, use the non-Autocrypt approach if not */
    if ((bsmsg->ident == NULL) ||
        (libbalsa_identity_get_autocrypt_mode(bsmsg->ident) == AUTOCRYPT_DISABLE)) {
        return check_suggest_encryption(bsmsg);
    }

    /* nothing to do if encryption is already enabled or if S/MIME mode is selected */
    if ((bsmsg->crypt_mode & (LIBBALSA_PROTECT_ENCRYPT | LIBBALSA_PROTECT_SMIME)) != 0) {
    	return TRUE;
    }

    /* we can not encrypt if we have bcc recipients */
    tmp_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "BCC:");
    len = internet_address_list_length(tmp_list);
    g_object_unref(tmp_list);
    if (len > 0) {
        return TRUE;
    }

    /* get the Autocrypt recommendation for all To: and Cc: addresses */
    check_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");
    tmp_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "CC:");
    internet_address_list_append(check_list, tmp_list);
    g_object_unref(tmp_list);
    internet_address_list_add(check_list, libbalsa_identity_get_address(bsmsg->ident));		/* validates that we have a key for the current identity */
    autocrypt_mode = autocrypt_recommendation(check_list, &missing_keys, &error);
    g_object_unref(check_list);

    /* eject on error or disabled */
    if (autocrypt_mode <= AUTOCRYPT_ENCR_DISABLE) {
    	if (autocrypt_mode == AUTOCRYPT_ENCR_ERROR) {
    		libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("error checking Autocrypt keys: %s"),
    			(error != NULL) ? error->message : _("unknown"));
    		g_clear_error(&error);
    		result = FALSE;
    	} else {
    		result = TRUE;
    	}
    } else {
    	gchar *message;
    	const gchar *protoname;
    	gint default_choice;

    	protoname = libbalsa_gpgme_protocol_name(GPGME_PROTOCOL_OpenPGP);
		message = g_markup_printf_escaped(_("You did not select encryption for this message, although "
    		"%s public keys are available for all recipients. In order "
            "to protect your privacy, the message could be %s encrypted."),
			protoname, protoname);

    	/* default to encryption if all participants have prefer-encrypt=mutual, or if we reply to an encrypted message */
    	if (((autocrypt_mode == AUTOCRYPT_ENCR_AVAIL_MUTUAL) &&
             (libbalsa_identity_get_autocrypt_mode(bsmsg->ident) == AUTOCRYPT_PREFER_ENCRYPT)) ||
            ((bsmsg->parent_message != NULL) &&
             (libbalsa_message_get_crypt_mode(bsmsg->parent_message) == LIBBALSA_PROTECT_ENCRYPT))) {
    		default_choice = GTK_RESPONSE_YES;
    	} else if (autocrypt_mode == AUTOCRYPT_ENCR_AVAIL) {
    		default_choice = GTK_RESPONSE_NO;
    	} else {			/* autocrypt_mode == AUTOCRYPT_ENCR_DISCOURAGE */
    		gchar *tmp_msg;

    		default_choice = GTK_RESPONSE_NO;
    		tmp_msg = g_strconcat(message,
    			_("\nHowever, encryption is discouraged as the Autocrypt status indicates that "
    			  "some recipients <i>might</i> not be able to read the message."), NULL);
    		g_free(message);
    		message = tmp_msg;
    	}

    	/* add a note if keys are imported into the key ring */
    	if (missing_keys != NULL) {
    		guint key_count;
    		gchar *key_msg;
    		gchar *tmp_msg;

    		key_count = g_list_length(missing_keys);
    		key_msg = g_strdup_printf(ngettext("<i>Note:</i> choosing encryption will import %u key from "
    										   "the Autocrypt database into the GnuPG key ring.",
											   "<i>Note:</i> choosing encryption will import %u keys from "
    										   "the Autocrypt database into the GnuPG key ring.", key_count), key_count);
    		tmp_msg = g_strconcat(message, "\n", key_msg, NULL);
    		g_free(message);
    		g_free(key_msg);
    		message = tmp_msg;
    	}

    	/* run the dialog */
    	result = run_check_encrypt_dialog(bsmsg, message, default_choice);

    	if (result && ((bsmsg->crypt_mode & LIBBALSA_PROTECT_ENCRYPT) != 0)) {
    		/* make sure the message is also signed as required by the Autocrypt standard, and that a protocol is selected */
    		if ((bsmsg->crypt_mode & LIBBALSA_PROTECT_PROTOCOL) == 0) {
    			bsmsg_setup_gpg_ui_by_mode(bsmsg, bsmsg->crypt_mode | (LIBBALSA_PROTECT_RFC3156 | LIBBALSA_PROTECT_SIGN));
    		} else {
    			bsmsg_setup_gpg_ui_by_mode(bsmsg, bsmsg->crypt_mode | LIBBALSA_PROTECT_SIGN);
    		}

        	/* import any missing keys */
        	if (missing_keys != NULL) {
        		result = import_autocrypt_keys(missing_keys, &error);
        		if (!result) {
        			libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Cannot import Autocrypt keys: %s"), error->message);
        			g_clear_error(&error);
        		}
        	}
    	}
    }

    /* clean up the missing keys list */
    if (missing_keys != NULL) {
    	g_list_free_full(missing_keys, (GDestroyNotify) g_bytes_unref);
    }

    return result;
}
#endif		/* ENABLE_AUTOCRYPT */


/* "send message" menu and toolbar callback.
 */
static gint
send_message_handler(BalsaSendmsg * bsmsg, gboolean queue_only)
{
    LibBalsaMsgCreateResult result;
    LibBalsaMessage *message;
    LibBalsaMailbox *fcc;
    GtkTreeIter iter;
    GError * error = NULL;

    if (!bsmsg->ready_to_send)
	return FALSE;

    if(!subject_not_empty(bsmsg))
	return FALSE;

#ifdef ENABLE_AUTOCRYPT
    if (!check_autocrypt_recommendation(bsmsg)) {
    	return FALSE;
    }
#else
    if (!check_suggest_encryption(bsmsg)) {
    	return FALSE;
    }
#endif /* ENABLE_AUTOCRYPT */

    if ((bsmsg->crypt_mode & LIBBALSA_PROTECT_OPENPGP) != 0) {
        gboolean warn_mp;
        gboolean warn_html_sign;

        warn_mp = (bsmsg->crypt_mode & LIBBALSA_PROTECT_MODE) != 0 &&
            bsmsg->tree_view &&
            gtk_tree_model_get_iter_first(BALSA_MSG_ATTACH_MODEL(bsmsg), &iter);
        warn_html_sign = (bsmsg->crypt_mode & LIBBALSA_PROTECT_MODE) == LIBBALSA_PROTECT_SIGN &&
            bsmsg->send_mp_alt;

        if (warn_mp || warn_html_sign) {
            /* we are going to RFC2440 sign/encrypt a multipart, or to
             * RFC2440 sign a multipart/alternative... */
            GtkWidget *dialog;
            gint choice;
            GString * string =
                g_string_new(_("You selected OpenPGP security for this message.\n"));

            if (warn_html_sign)
                string =
                    g_string_append(string,
                        _("The message text will be sent as plain text and as "
                          "HTML, but only the plain part can be signed.\n"));
            if (warn_mp)
                string =
                    g_string_append(string,
                        _("The message contains attachments, which cannot be "
                          "signed or encrypted.\n"));
            string =
                g_string_append(string,
                    _("You should select MIME mode if the complete "
                      "message shall be protected. Do you really want to proceed?"));
            dialog = gtk_message_dialog_new
                (GTK_WINDOW(bsmsg->window),
                 GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                 GTK_MESSAGE_QUESTION,
                 GTK_BUTTONS_OK_CANCEL, "%s", string->str);
            g_string_free(string, TRUE);
            choice = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            if (choice != GTK_RESPONSE_OK)
                return FALSE;
        }
    }

    message = bsmsg2message(bsmsg);
    fcc = balsa_find_mailbox_by_url(bsmsg->fcc_url);

    balsa_information_parented(GTK_WINDOW(bsmsg->window),
                               LIBBALSA_INFORMATION_DEBUG,
                               _("sending message with GPG mode %d"),
                               libbalsa_message_get_crypt_mode(message));

    if(queue_only)
	result = libbalsa_message_queue(message, balsa_app.outbox, fcc,
					libbalsa_identity_get_smtp_server(bsmsg->ident),
					bsmsg->flow, &error);
    else
        result = libbalsa_message_send(message, balsa_app.outbox, fcc,
                                       balsa_find_sentbox_by_url,
				       libbalsa_identity_get_smtp_server(bsmsg->ident),
					   	   	   	   	   balsa_app.send_progress_dialog,
                                       GTK_WINDOW(balsa_app.main_window),
                                       bsmsg->flow, &error);
    if (result == LIBBALSA_MESSAGE_CREATE_OK) {
	if (bsmsg->parent_message != NULL) {
            LibBalsaMailbox *mailbox = libbalsa_message_get_mailbox(bsmsg->parent_message);
            if (mailbox != NULL && !libbalsa_mailbox_get_readonly(mailbox))
                libbalsa_message_reply(bsmsg->parent_message);
        }
        sw_delete_draft(bsmsg);
    }

    g_object_unref(message);

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
	case LIBBALSA_MESSAGE_SIGN_ERROR:
            msg = _("Message could not be signed"); break;
	case LIBBALSA_MESSAGE_ENCRYPT_ERROR:
            msg = _("Message could not be encrypted"); break;
        }
	if (error) {
	    balsa_information_parented(GTK_WINDOW(bsmsg->window),
				       LIBBALSA_INFORMATION_ERROR,
				       _("Send failed: %s\n%s"), msg,
				       error->message);
            g_error_free(error);
        } else {
	    balsa_information_parented(GTK_WINDOW(bsmsg->window),
				       LIBBALSA_INFORMATION_ERROR,
				       _("Send failed: %s"), msg);
        }
	return FALSE;
    }

    gtk_widget_destroy(bsmsg->window);

    return TRUE;
}


/* "send message" menu callback */
static void
sw_toolbar_send_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    send_message_handler(bsmsg, balsa_app.always_queue_sent_mail);
}

static void
sw_send_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    send_message_handler(bsmsg, FALSE);
}


static void
sw_queue_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    send_message_handler(bsmsg, TRUE);
}

static gboolean
message_postpone(BalsaSendmsg * bsmsg)
{
    gboolean successp;
    LibBalsaMessage *message;
    GPtrArray *headers;
    GError *error = NULL;

    /* Silent fallback to UTF-8 */
    message = bsmsg2message(bsmsg);

    /* sufficiently long for fcc, mdn, gpg */
    headers = g_ptr_array_new();
    if (bsmsg->fcc_url) {
        g_ptr_array_add(headers, g_strdup("X-Balsa-Fcc"));
        g_ptr_array_add(headers, g_strdup(bsmsg->fcc_url));
    }
    g_ptr_array_add(headers, g_strdup("X-Balsa-MDN"));
    g_ptr_array_add(headers, g_strdup_printf("%d", bsmsg->req_mdn));
    g_ptr_array_add(headers, g_strdup("X-Balsa-DSN"));
    g_ptr_array_add(headers, g_strdup_printf("%d", bsmsg->req_dsn));
    g_ptr_array_add(headers, g_strdup("X-Balsa-Crypto"));
    g_ptr_array_add(headers, g_strdup_printf("%u", bsmsg->crypt_mode));
    g_ptr_array_add(headers, g_strdup("X-Balsa-Att-Pubkey"));
    g_ptr_array_add(headers, g_strdup_printf("%d", bsmsg->attach_pubkey));

#if HAVE_GSPELL || HAVE_GTKSPELL
    if (sw_action_get_active(bsmsg, "spell-check")) {
        g_ptr_array_add(headers, g_strdup("X-Balsa-Lang"));
        g_ptr_array_add(headers, g_strdup(bsmsg->spell_check_lang));
    }
#else  /* HAVE_GTKSPELL */
    g_ptr_array_add(headers, g_strdup("X-Balsa-Lang"));
    g_ptr_array_add(headers, g_strdup(bsmsg->spell_check_lang));
#endif /* HAVE_GTKSPELL */
    g_ptr_array_add(headers, g_strdup("X-Balsa-Format"));
    g_ptr_array_add(headers, g_strdup(bsmsg->flow ? "Flowed" : "Fixed"));
    g_ptr_array_add(headers, g_strdup("X-Balsa-MP-Alt"));
    g_ptr_array_add(headers, g_strdup(bsmsg->send_mp_alt ? "yes" : "no"));
    g_ptr_array_add(headers, g_strdup("X-Balsa-Send-Type"));
    g_ptr_array_add(headers, g_strdup_printf("%d", bsmsg->type));
    g_ptr_array_add(headers, NULL);

    successp = libbalsa_message_postpone(message, balsa_app.draftbox,
                                         (gchar **) headers->pdata,
                                         bsmsg->flow, &error);
    g_ptr_array_foreach(headers, (GFunc) g_free, NULL);
    g_ptr_array_free(headers, TRUE);

    if(successp)
        sw_delete_draft(bsmsg);
    else {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("Could not postpone message: %s"),
				   error ? error->message : "");
	g_clear_error(&error);
    }

    g_object_unref(message);
    return successp;
}

/* "postpone message" menu callback */
static void
sw_postpone_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    if (bsmsg->ready_to_send) {
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
sw_save_activated(GSimpleAction * action,
                  GVariant      * parameter,
                  gpointer        data)
{
    BalsaSendmsg *bsmsg = data;

    if (sw_save_draft(bsmsg))
        bsmsg->state = SENDMSG_STATE_CLEAN;
}

static void
sw_page_setup_activated(GSimpleAction * action,
                        GVariant      * parameter,
                        gpointer data)
{
    BalsaSendmsg *bsmsg = data;
    LibBalsaMessage *message;

    message = bsmsg2message(bsmsg);
    message_print_page_setup(GTK_WINDOW(bsmsg->window));
    g_object_unref(message);
}

static void
sw_print_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer data)
{
    BalsaSendmsg *bsmsg = data;
    LibBalsaMessage *message;

    message = bsmsg2message(bsmsg);
    message_print(message, GTK_WINDOW(bsmsg->window), NULL);
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
        || !sw_action_get_enabled(bsmsg, "undo"))
        sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */
}

static void
sw_buffer_changed(GtkTextBuffer * buffer, BalsaSendmsg * bsmsg)
{
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

#if !HAVE_GTKSOURCEVIEW || !(HAVE_GSPELL || HAVE_GTKSPELL)
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
    sw_action_set_enabled(bsmsg, "undo", undo);
    sw_action_set_enabled(bsmsg, "redo", redo);
}
#endif                          /* HAVE_GTKSOURCEVIEW */

#ifdef HAVE_GTKSPELL
/*
 * Callback for the spell-checker's "language-changed" signal.
 *
 * The signal is emitted if the user changes the spell-checker language
 * using the context menu.  If the new language is one that we have in
 * the menu, set the appropriate item active.
 */
static void
sw_spell_language_changed_cb(GtkSpellChecker * spell,
                             const gchar     * new_lang,
                             gpointer          data)
{
    BalsaSendmsg *bsmsg = data;
    GtkWidget *langs;
    GList *list, *children;

    langs = gtk_menu_item_get_submenu(GTK_MENU_ITEM
                                      (bsmsg->current_language_menu));
    children = gtk_container_get_children(GTK_CONTAINER(langs));

    for (list = children; list; list = list->next) {
        GtkCheckMenuItem *menu_item = list->data;
        const gchar *lang;

        lang = g_object_get_data(G_OBJECT(menu_item),
                                 BALSA_LANGUAGE_MENU_LANG);
        if (strcmp(lang, new_lang) == 0) {
            g_signal_handlers_block_by_func(menu_item, lang_set_cb, bsmsg);
            gtk_check_menu_item_set_active(menu_item, TRUE);
            g_signal_handlers_unblock_by_func(menu_item, lang_set_cb,
                                              bsmsg);
            break;
        }
    }

    g_list_free(children);

    g_free(bsmsg->spell_check_lang);
    bsmsg->spell_check_lang = g_strdup(new_lang);
    g_free(balsa_app.spell_check_lang);
    balsa_app.spell_check_lang = g_strdup(new_lang);
}

static gboolean
sw_spell_detach(BalsaSendmsg * bsmsg)
{
    GtkSpellChecker *spell;

    spell = gtk_spell_checker_get_from_text_view(GTK_TEXT_VIEW(bsmsg->text));
    if (spell)
        gtk_spell_checker_detach(spell);

    return spell != NULL;
}

static void
sw_spell_attach(BalsaSendmsg * bsmsg)
{
    GtkSpellChecker *spell;
    GError *err = NULL;

    /* Detach any existing spell checker */
    sw_spell_detach(bsmsg);

    spell = gtk_spell_checker_new();
    gtk_spell_checker_set_language(spell, bsmsg->spell_check_lang, &err);
    if (err) {
        /* Should not happen, since we now check the language. */
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Error starting spell checker: %s"),
                                   err->message);
        g_error_free(err);

        /* No spell checker, so deactivate the button. */
        sw_action_set_active(bsmsg, "spell-check", FALSE);
    } else {
        gtk_spell_checker_attach(spell, GTK_TEXT_VIEW(bsmsg->text));
        g_signal_connect(spell, "language-changed",
                         G_CALLBACK(sw_spell_language_changed_cb), bsmsg);
    }
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
    g_object_ref(buffer);
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
sw_undo_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    g_signal_emit_by_name(bsmsg->text, "undo");
}

static void
sw_redo_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    g_signal_emit_by_name(bsmsg->text, "redo");
}
#else                           /* HAVE_GTKSOURCEVIEW */
static void
sw_undo_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    sw_buffer_swap(bsmsg, TRUE);
}

static void
sw_redo_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

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
sw_cut_activated(GSimpleAction * action,
                 GVariant      * parameter,
                 gpointer        data)
{
    clipboard_helper(data, "cut-clipboard");
}

static void
sw_copy_activated(GSimpleAction * action,
                  GVariant      * parameter,
                  gpointer        data)
{
    clipboard_helper(data, "copy-clipboard");
}

static void
sw_paste_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        data)
{
    clipboard_helper(data, "paste-clipboard");
}

/*
 * More menu callbacks.
 */
static void
sw_select_text_activated(GSimpleAction * action,
                         GVariant      * parameter,
                         gpointer        data)
{
    BalsaSendmsg *bsmsg = data;

    balsa_window_select_all(GTK_WINDOW(bsmsg->window));
}

static void
sw_wrap_body_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        data)
{
    BalsaSendmsg *bsmsg = data;

#if !HAVE_GTKSOURCEVIEW
    sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */
    sw_wrap_body(bsmsg);
}

static void
sw_reflow_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
    GRegex *rex;

    if (!bsmsg->flow)
	return;

    if (!(rex = balsa_quote_regex_new()))
        return;

#if !HAVE_GTKSOURCEVIEW
    sw_buffer_save(bsmsg);
#endif                          /* HAVE_GTKSOURCEVIEW */

    text_view = GTK_TEXT_VIEW(bsmsg->text);
    buffer = gtk_text_view_get_buffer(text_view);
    sw_buffer_signals_block(bsmsg, buffer);
    libbalsa_unwrap_selection(buffer, rex);
    sw_buffer_signals_unblock(bsmsg, buffer);

    bsmsg->state = SENDMSG_STATE_MODIFIED;
    gtk_text_view_scroll_to_mark(text_view,
				 gtk_text_buffer_get_insert(buffer),
				 0, FALSE, 0, 0);

    g_regex_unref(rex);
}

/* To field "changed" signal callback. */
static void
check_readiness(BalsaSendmsg * bsmsg)
{
    gboolean ready =
        libbalsa_address_view_n_addresses(bsmsg->recipient_view) > 0;
    if (ready
        && libbalsa_address_view_n_addresses(bsmsg->replyto_view) < 0)
        ready = FALSE;

    bsmsg->ready_to_send = ready;
    sw_actions_set_enabled(bsmsg, ready_actions,
                           G_N_ELEMENTS(ready_actions), ready);
}

static const gchar * const header_action_names[] = {
    "from",
    "recips",
    "reply-to",
    "fcc"
};

/* sw_entry_helper:
   auxiliary function for "header show/hide" toggle menu entries.
   saves the show header configuration.
 */
static void
sw_entry_helper(GSimpleAction      *action,
                GVariant           *state,
                BalsaSendmsg       *bsmsg,
                BalsaSendmsgHeader *header)
{
    if (g_variant_get_boolean(state)) {
        gtk_widget_show_all(header->name);
        gtk_widget_show_all(header->body);
        gtk_widget_grab_focus(header->body);
    } else {
        gtk_widget_hide(header->name);
        gtk_widget_hide(header->body);
    }

    g_simple_action_set_state(G_SIMPLE_ACTION(action), state);

    if (bsmsg->update_config) { /* then save the config */
        GString *str = g_string_new(NULL);
        unsigned i;

        for (i = 0; i < G_N_ELEMENTS(header_action_names); i++) {
            if (sw_action_get_active(bsmsg, header_action_names[i])) {
                if (str->len > 0)
                    g_string_append_c(str, ' ');
                g_string_append(str, header_action_names[i]);
            }
        }
        g_free(balsa_app.compose_headers);
        balsa_app.compose_headers = g_string_free(str, FALSE);
    }

    g_simple_action_set_state(action, state);
}

static void
sw_from_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    sw_entry_helper(action, state, bsmsg, &bsmsg->from);
}

static void
sw_recips_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    sw_entry_helper(action, state, bsmsg, &bsmsg->recipients);
}

static void
sw_reply_to_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    sw_entry_helper(action, state, bsmsg, &bsmsg->replyto);
}

static void
sw_fcc_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    sw_entry_helper(action, state, bsmsg, &bsmsg->fcc);
}

static void
sw_request_mdn_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    bsmsg->req_mdn = g_variant_get_boolean(state);

    g_simple_action_set_state(action, state);
}

static void
sw_request_dsn_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    bsmsg->req_dsn = g_variant_get_boolean(state);

    g_simple_action_set_state(action, state);
}

static void
sw_show_toolbar_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    balsa_app.show_compose_toolbar = g_variant_get_boolean(state);
    if (balsa_app.show_compose_toolbar)
        gtk_widget_show(bsmsg->toolbar);
    else
        gtk_widget_hide(bsmsg->toolbar);

    g_simple_action_set_state(action, state);
}

static void
sw_flowed_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    bsmsg->flow = g_variant_get_boolean(state);
    sw_action_set_enabled(bsmsg, "reflow", bsmsg->flow);

    g_simple_action_set_state(action, state);
}

static void
sw_send_html_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    bsmsg->send_mp_alt = g_variant_get_boolean(state);

    g_simple_action_set_state(action, state);
}

static void
sw_gpg_helper(GSimpleAction  * action,
              GVariant       * state,
              gpointer         data,
              guint            mask)
{
    BalsaSendmsg *bsmsg = data;
    gboolean butval, radio_on;

    butval = g_variant_get_boolean(state);
    if (butval)
        bsmsg->crypt_mode |= mask;
    else
        bsmsg->crypt_mode &= ~mask;

    radio_on = (bsmsg->crypt_mode & LIBBALSA_PROTECT_MODE) != 0;
    sw_action_set_enabled(bsmsg, "gpg-mode", radio_on);
    libbalsa_address_view_set_crypt_mode(bsmsg->recipient_view, bsmsg->crypt_mode);

    g_simple_action_set_state(action, state);
}

static void
sw_sign_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    sw_gpg_helper(action, state, data, LIBBALSA_PROTECT_SIGN);
}

static void
sw_encrypt_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    sw_gpg_helper(action, state, data, LIBBALSA_PROTECT_ENCRYPT);
}

static void
sw_att_pubkey_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = (BalsaSendmsg *) data;

    bsmsg->attach_pubkey = g_variant_get_boolean(state);
    g_simple_action_set_state(action, state);
}

static void
sw_gpg_mode_change_state(GSimpleAction  * action,
                         GVariant       * state,
                         gpointer         data)
{
    BalsaSendmsg *bsmsg = data;
    const gchar *mode;
    guint rfc_flag = 0;

    mode = g_variant_get_string(state, NULL);
    if (strcmp(mode, "mime") == 0)
        rfc_flag = LIBBALSA_PROTECT_RFC3156;
    else if (strcmp(mode, "open-pgp") == 0)
        rfc_flag = LIBBALSA_PROTECT_OPENPGP;
    else if (strcmp(mode, "smime") == 0)
        rfc_flag = LIBBALSA_PROTECT_SMIME;
    else {
        g_warning("%s unknown mode “%s”", __func__, mode);
        return;
    }

    bsmsg->crypt_mode =
        (bsmsg->crypt_mode & ~LIBBALSA_PROTECT_PROTOCOL) | rfc_flag;
    libbalsa_address_view_set_crypt_mode(bsmsg->recipient_view, bsmsg->crypt_mode);

    g_simple_action_set_state(action, state);
}


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
        if (!found) {
            /* Be compatible with old action-names */
            struct {
                const gchar *old_action_name;
                const gchar *new_action_name;
            } name_map[] = {
                {"From",       "from"},
                {"Recipients", "recips"},
                {"ReplyTo",    "reply-to"},
                {"Fcc",        "fcc"}
            };
            guint j;

            for (j = 0; j < G_N_ELEMENTS(name_map); j++) {
                if (strcmp(header_action_names[i],
                           name_map[j].new_action_name) == 0) {
                    found =
                        libbalsa_find_word(name_map[j].old_action_name,
                                           balsa_app.compose_headers);
                }
            }
        }
        sw_action_set_active(bsmsg, header_action_names[i], found);
    }

    /* gray 'send' and 'postpone' */
    check_readiness(bsmsg);
}

static void
set_locale(BalsaSendmsg * bsmsg, const gchar * locale)
{
#if HAVE_GSPELL
    if (sw_action_get_enabled(bsmsg, "spell-check")) {
        const GspellLanguage *language;

        language = gspell_language_lookup(locale);
        if (language != NULL) {
            GtkTextBuffer *buffer;
            GspellTextBuffer *gspell_buffer;
            GspellChecker *checker;

            buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
            gspell_buffer = gspell_text_buffer_get_from_gtk_text_buffer(buffer);
            checker = gspell_text_buffer_get_spell_checker(gspell_buffer);
            gspell_checker_set_language(checker, language);
        }
    }

#endif                          /* HAVE_GSPELL */
    g_free(bsmsg->spell_check_lang);
    bsmsg->spell_check_lang = g_strdup(locale);

#if HAVE_GTKSPELL
    if (sw_action_get_enabled(bsmsg, "spell-check")) {
        if (sw_spell_detach(bsmsg))
            sw_spell_attach(bsmsg);
    }
#endif                          /* HAVE_GTKSPELL */
}

#if HAVE_GSPELL || HAVE_GTKSPELL
/* spell_check_menu_cb
 *
 * Toggle the spell checker
 */
static void
sw_spell_check_change_state(GSimpleAction * action,
                            GVariant      * state,
                            gpointer        data)
{
    BalsaSendmsg *bsmsg = data;
#if HAVE_GSPELL
    GtkTextView *text_view;
    GspellTextView *gspell_view;

    balsa_app.spell_check_active = g_variant_get_boolean(state);
    text_view = GTK_TEXT_VIEW(bsmsg->text);
    gspell_view = gspell_text_view_get_from_gtk_text_view(text_view);
    gspell_text_view_set_inline_spell_checking(gspell_view,
                                               balsa_app.spell_check_active);
#elif HAVE_GTKSPELL

    if ((balsa_app.spell_check_active = g_variant_get_boolean(state)))
        sw_spell_attach(bsmsg);
    else
        sw_spell_detach(bsmsg);
#endif                          /* HAVE_GSPELL */

    g_simple_action_set_state(action, state);
}

#else                           /* HAVE_GTKSPELL */
/* spell_check_cb
 *
 * Start the spell check
 * */
static void
sw_spell_check_activated(GSimpleAction * action,
                         GVariant      * parameter,
                         gpointer        data)
{
    BalsaSendmsg *bsmsg = data;
    GtkTextView *text_view = GTK_TEXT_VIEW(bsmsg->text);
    BalsaSpellCheck *sc;

    if (bsmsg->spell_checker) {
        if (gtk_widget_get_window(bsmsg->spell_checker)) {
            gtk_window_present_with_time(GTK_WINDOW(bsmsg->spell_checker),
                                         gtk_get_current_event_time());
            return;
        } else
            /* A spell checker was created, but not shown because of
             * errors; we'll destroy it, and create a new one. */
            gtk_widget_destroy(bsmsg->spell_checker);
    }

    sw_buffer_signals_disconnect(bsmsg);

    bsmsg->spell_checker = balsa_spell_check_new(GTK_WINDOW(bsmsg->window));
    sc = BALSA_SPELL_CHECK(bsmsg->spell_checker);

    /* configure the spell checker */
    balsa_spell_check_set_text(sc, text_view);
    balsa_spell_check_set_language(sc, bsmsg->spell_check_lang);

    g_object_weak_ref(G_OBJECT(sc),
                     (GWeakNotify) sw_spell_check_weak_notify, bsmsg);
    gtk_text_view_set_editable(text_view, FALSE);

    balsa_spell_check_start(sc);
}

static void
sw_spell_check_weak_notify(BalsaSendmsg * bsmsg)
{
    bsmsg->spell_checker = NULL;
    gtk_text_view_set_editable(GTK_TEXT_VIEW(bsmsg->text), TRUE);
    sw_buffer_signals_connect(bsmsg);
}
#endif                          /* HAVE_GTKSPELL */

static void
lang_set_cb(GtkWidget * w, BalsaSendmsg * bsmsg)
{
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w))) {
        const gchar *lang;

        lang = g_object_get_data(G_OBJECT(w), BALSA_LANGUAGE_MENU_LANG);
        set_locale(bsmsg, lang);
        g_free(balsa_app.spell_check_lang);
        balsa_app.spell_check_lang = g_strdup(lang);
#if HAVE_GSPELL || HAVE_GTKSPELL
        sw_action_set_active(bsmsg, "spell-check", TRUE);
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
    guint msgno;

    g_return_val_if_fail(selected->len > 0, NULL);

    msgno = g_array_index(selected, guint, 0);
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
        return NULL; /* silence invalid warnings */
    }
    g_object_unref(message);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    for (i = 1; i < selected->len; i++) {
	msgno = g_array_index(selected, guint, i);
        message = libbalsa_mailbox_get_message(mailbox, msgno);
        if (!message)
            continue;

        if (type == SEND_FORWARD_ATTACH)
            attach_message(bsmsg, message);
        else if (type == SEND_FORWARD_INLINE) {
            GString *body =
                quote_message_body(bsmsg, message, QUOTE_NOPREFIX);
            gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);
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
    const gchar *header;

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
            gchar *field;

            url += 7;
            field = g_strndup(url, close - url);
            sendmsg_window_process_url(bsmsg, field, FALSE);
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

    default:
        title_format = _("New message to %s: %s");
        break;
    }

    list = libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");
    to_string = internet_address_list_to_string(list, NULL, FALSE);
    g_object_unref(list);

    title = g_strdup_printf(title_format, to_string ? to_string : "",
                            gtk_entry_get_text(GTK_ENTRY(bsmsg->subject.body)));
    g_free(to_string);
    gtk_window_set_title(GTK_WINDOW(bsmsg->window), title);
    g_free(title);
}

static void
bsmsg_update_gpg_ui_on_ident_change(BalsaSendmsg * bsmsg,
                                    LibBalsaIdentity * ident)
{
    GAction *action;

    /* do nothing if we don't support crypto */
    if (!balsa_app.has_openpgp && !balsa_app.has_smime)
        return;

    action = sw_get_action(bsmsg, "gpg-mode");

    /* preset according to identity */
    bsmsg->crypt_mode = LIBBALSA_PROTECT_NONE;
    bsmsg->always_trust = libbalsa_identity_get_always_trust(ident);

    sw_action_set_active(bsmsg, "sign", libbalsa_identity_get_gpg_sign(ident));
    if (libbalsa_identity_get_gpg_sign(ident))
        bsmsg->crypt_mode |= LIBBALSA_PROTECT_SIGN;

    sw_action_set_active(bsmsg, "encrypt", libbalsa_identity_get_gpg_encrypt(ident));
    if (libbalsa_identity_get_gpg_encrypt(ident))
        bsmsg->crypt_mode |= LIBBALSA_PROTECT_ENCRYPT;

    switch (libbalsa_identity_get_crypt_protocol(ident)) {
    case LIBBALSA_PROTECT_OPENPGP:
        bsmsg->crypt_mode |= LIBBALSA_PROTECT_OPENPGP;
        g_action_change_state(action, g_variant_new_string("open-pgp"));
        break;
    case LIBBALSA_PROTECT_SMIME:
        bsmsg->crypt_mode |= LIBBALSA_PROTECT_SMIME;
        g_action_change_state(action, g_variant_new_string("smime"));
        break;
    case LIBBALSA_PROTECT_RFC3156:
    default:
        bsmsg->crypt_mode |= LIBBALSA_PROTECT_RFC3156;
        g_action_change_state(action, g_variant_new_string("mime"));
    }
}

static void
bsmsg_setup_gpg_ui(BalsaSendmsg *bsmsg)
{
    /* make everything insensitive if we don't have crypto support */
    sw_action_set_enabled(bsmsg, "gpg-mode", balsa_app.has_openpgp ||
                          balsa_app.has_smime);
    sw_action_set_enabled(bsmsg, "attpubkey", balsa_app.has_openpgp);
}

static void
bsmsg_setup_gpg_ui_by_mode(BalsaSendmsg *bsmsg, guint mode)
{
    GAction *action;

    /* do nothing if we don't support crypto */
    if (!balsa_app.has_openpgp && !balsa_app.has_smime)
	return;

    bsmsg->crypt_mode = mode;
    sw_action_set_active(bsmsg, "sign", mode & LIBBALSA_PROTECT_SIGN);
    sw_action_set_active(bsmsg, "encrypt", mode & LIBBALSA_PROTECT_ENCRYPT);

    action = sw_get_action(bsmsg, "gpg-mode");
    if (mode & LIBBALSA_PROTECT_SMIME)
        g_action_change_state(action, g_variant_new_string("smime"));
    else if (mode & LIBBALSA_PROTECT_OPENPGP)
        g_action_change_state(action, g_variant_new_string("open-pgp"));
    else
        g_action_change_state(action, g_variant_new_string("mime"));
}

static GActionEntry win_entries[] = {
    {"include-file",     sw_include_file_activated      },
    {"attach-file",      sw_attach_file_activated       },
    {"include-messages", sw_include_messages_activated  },
    {"attach-messages",  sw_attach_messages_activated   },
    {"send",             sw_send_activated              },
    {"queue",            sw_queue_activated             },
    {"postpone",         sw_postpone_activated          },
    {"save",             sw_save_activated              },
    {"page-setup",       sw_page_setup_activated        },
    {"print",            sw_print_activated             },
    {"close",            sw_close_activated             },
    {"undo",             sw_undo_activated              },
    {"redo",             sw_redo_activated              },
    {"cut",              sw_cut_activated               },
    {"copy",             sw_copy_activated              },
    {"paste",            sw_paste_activated             },
    {"select-all",       sw_select_text_activated       },
    {"wrap-body",        sw_wrap_body_activated         },
    {"reflow",           sw_reflow_activated            },
    {"insert-sig",       sw_insert_sig_activated        },
    {"quote",            sw_quote_activated             },
#if HAVE_GSPELL || HAVE_GTKSPELL
    {"spell-check",      NULL, NULL, "false",     sw_spell_check_change_state},
#else                           /* HAVE_GTKSPELL */
    {"spell-check",      sw_spell_check_activated                             },
#endif                          /* HAVE_GTKSPELL */
    {"select-ident",     sw_select_ident_activated                            },
    {"edit",             sw_edit_activated                                    },
    {"show-toolbar",     NULL, NULL, "false",     sw_show_toolbar_change_state},
    {"from",             NULL, NULL, "false",     sw_from_change_state        },
    {"recips",           NULL, NULL, "false",     sw_recips_change_state      },
    {"reply-to",         NULL, NULL, "false",     sw_reply_to_change_state    },
    {"fcc",              NULL, NULL, "false",     sw_fcc_change_state         },
    {"request-mdn",      NULL, NULL, "false",     sw_request_mdn_change_state },
    {"request-dsn",      NULL, NULL, "false",     sw_request_dsn_change_state },
    {"flowed",           NULL, NULL, "false",     sw_flowed_change_state      },
    {"send-html",        NULL, NULL, "false",     sw_send_html_change_state   },
    {"sign",             NULL, NULL, "false",     sw_sign_change_state        },
    {"encrypt",          NULL, NULL, "false",     sw_encrypt_change_state     },
    {"gpg-mode",         NULL, "s", "'mime'",     sw_gpg_mode_change_state    },
    {"gpg-mode",         NULL, "s", "'open-pgp'", sw_gpg_mode_change_state    },
    {"gpg-mode",         NULL, "s", "'smime'",    sw_gpg_mode_change_state    },
    {"attpubkey",        NULL, NULL, "false",     sw_att_pubkey_change_state  },
    /* Only a toolbar button: */
    {"toolbar-send",     sw_toolbar_send_activated      }
};

void
sendmsg_window_add_action_entries(GActionMap * action_map)
{
    g_action_map_add_action_entries(action_map, win_entries,
                                    G_N_ELEMENTS(win_entries), action_map);
}

static void
sw_menubar_foreach(GtkWidget *widget, gpointer data)
{
    GtkWidget **lang_menu = data;
    GtkMenuItem *item = GTK_MENU_ITEM(widget);

    if (strcmp(gtk_menu_item_get_label(item), _("_Language")) == 0)
        *lang_menu = widget;
}

static BalsaSendmsg*
sendmsg_window_new()
{
    BalsaToolbarModel *model;
    GtkWidget *window;
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    BalsaSendmsg *bsmsg = NULL;
#if HAVE_GTKSOURCEVIEW
    GtkSourceBuffer *source_buffer;
#endif                          /* HAVE_GTKSOURCEVIEW */
    GError *error = NULL;
    GtkWidget *menubar;
    GtkWidget *paned;
    const gchar resource_path[] = "/org/desktop/Balsa/sendmsg-window.ui";
    const gchar *current_locale;

    bsmsg = g_malloc(sizeof(BalsaSendmsg));
    bsmsg->in_reply_to = NULL;
    bsmsg->references = NULL;
    bsmsg->spell_check_lang = NULL;
    bsmsg->fcc_url  = NULL;
    bsmsg->insert_mark = NULL;

    bsmsg->ident = g_object_ref(balsa_app.current_ident);

    bsmsg->update_config = FALSE;
    bsmsg->state = SENDMSG_STATE_CLEAN;

    bsmsg->window = window =
        gtk_application_window_new(balsa_app.application);
    geometry_manager_attach(GTK_WINDOW(window), "SendMsgWindow");

    gtk_window_set_role(GTK_WINDOW(window), "compose");

    gtk_container_add(GTK_CONTAINER(window), main_box);
    gtk_widget_show_all(window);

    bsmsg->type = SEND_NORMAL;
    bsmsg->is_continue = FALSE;
#if !HAVE_GTKSPELL && !HAVE_GSPELL
    bsmsg->spell_checker = NULL;
#endif                          /* HAVE_GTKSPELL */
    bsmsg->crypt_mode = LIBBALSA_PROTECT_RFC3156;
    bsmsg->attach_pubkey = FALSE;
    bsmsg->autosave_timeout_id = /* autosave every 5 minutes */
        g_timeout_add_seconds(60*5, (GSourceFunc)sw_autosave_timeout_cb, bsmsg);

    bsmsg->draft_message = NULL;
    bsmsg->parent_message = NULL;
    g_signal_connect(window, "delete-event",
		     G_CALLBACK(delete_event_cb), bsmsg);
    g_signal_connect(window, "destroy",
		     G_CALLBACK(destroy_event_cb), bsmsg);
    /* If any compose windows are open when Balsa is closed, we want
     * them also to be closed. */
    g_object_weak_ref(G_OBJECT(balsa_app.main_window),
                      (GWeakNotify) gtk_widget_destroy, window);

    /* Set up the GMenu structures */
    menubar = libbalsa_window_get_menu_bar(GTK_APPLICATION_WINDOW(window),
                                           win_entries,
                                           G_N_ELEMENTS(win_entries),
                                           resource_path, &error, bsmsg);
    if (error) {
        g_warning("%s %s", __func__, error->message);
        g_error_free(error);
        return NULL;
    }
    gtk_widget_show(menubar);

    gtk_container_add(GTK_CONTAINER(main_box), menubar);

    /*
     * Set up the spell-checker language menu
     */
    gtk_container_foreach(GTK_CONTAINER(menubar), sw_menubar_foreach,
                          &bsmsg->current_language_menu);
    current_locale = create_lang_menu(bsmsg->current_language_menu, bsmsg);
    if (current_locale == NULL) {
        sw_action_set_enabled(bsmsg, "spell-check", FALSE);
    }

    model = sendmsg_window_get_toolbar_model();
    bsmsg->toolbar = balsa_toolbar_new(model, G_ACTION_MAP(window));
    gtk_container_add(GTK_CONTAINER(main_box), bsmsg->toolbar);

    bsmsg->flow = !balsa_app.wordwrap;
    sw_action_set_enabled(bsmsg, "reflow", bsmsg->flow);
    bsmsg->send_mp_alt = FALSE;

    sw_action_set_enabled(bsmsg, "select-ident",
                     balsa_app.identities->next != NULL);
    bsmsg->identities_changed_id =
        g_signal_connect_swapped(balsa_app.main_window, "identities-changed",
                                 G_CALLBACK(bsmsg_identities_changed_cb),
                                 bsmsg);
#if !HAVE_GTKSOURCEVIEW
    sw_buffer_set_undo(bsmsg, TRUE, FALSE);
#endif                          /* HAVE_GTKSOURCEVIEW */

    /* set options */
    bsmsg->req_mdn = FALSE;
    bsmsg->req_dsn = FALSE;

    sw_action_set_active(bsmsg, "flowed", bsmsg->flow);
    sw_action_set_active(bsmsg, "send-html",
                         libbalsa_identity_get_send_mp_alternative(bsmsg->ident));
    sw_action_set_active(bsmsg, "show-toolbar", balsa_app.show_compose_toolbar);

    bsmsg_setup_gpg_ui(bsmsg);

    /* Paned window for the addresses at the top, and the content at the
     * bottom: */
    bsmsg->paned = paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_widget_set_valign(paned, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(main_box), paned);
    gtk_widget_show(paned);

    /* create the top portion with the to, from, etc in it */
    gtk_paned_add1(GTK_PANED(paned), create_info_pane(bsmsg));
    bsmsg->tree_view = NULL;

    /* create text area for the message */
    gtk_paned_add2(GTK_PANED(paned), create_text_area(bsmsg));

    /* set the menus - and language index */
    init_menus(bsmsg);

    /* Connect to "text-changed" here, so that we catch the initial text
     * and wrap it... */
    sw_buffer_signals_connect(bsmsg);

#if HAVE_GTKSOURCEVIEW
    source_buffer = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer
                                      (GTK_TEXT_VIEW(bsmsg->text)));
    gtk_source_buffer_begin_not_undoable_action(source_buffer);
    gtk_source_buffer_end_not_undoable_action(source_buffer);
    sw_action_set_enabled(bsmsg, "undo", FALSE);
    sw_action_set_enabled(bsmsg, "redo", FALSE);
#else                           /* HAVE_GTKSOURCEVIEW */
    sw_buffer_set_undo(bsmsg, FALSE, FALSE);
#endif                          /* HAVE_GTKSOURCEVIEW */

    bsmsg->update_config = TRUE;

    bsmsg->delete_sig_id =
	g_signal_connect(balsa_app.main_window, "delete-event",
			 G_CALLBACK(delete_event_cb), bsmsg);

    setup_headers_from_identity(bsmsg, bsmsg->ident);

    /* Finish setting up the spell checker */
#if HAVE_GSPELL || HAVE_GTKSPELL
    if (current_locale != NULL) {
        sw_action_set_active(bsmsg, "spell-check", balsa_app.spell_check_active);
    }
#endif
    set_locale(bsmsg, current_locale);

    return bsmsg;
}

BalsaSendmsg*
sendmsg_window_compose(void)
{
    BalsaSendmsg *bsmsg = sendmsg_window_new();

    /* set the initial window title */
    bsmsg->type = SEND_NORMAL;
    sendmsg_window_set_title(bsmsg);
    if (libbalsa_identity_get_sig_sending(bsmsg->ident))
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

BalsaSendmsg*
sendmsg_window_new_to_sender(LibBalsaMailbox * mailbox, guint msgno)
{
	LibBalsaMessage *message = libbalsa_mailbox_get_message(mailbox, msgno);
	BalsaSendmsg *bsmsg;

	g_assert(message);
	bsmsg = sendmsg_window_compose();
	set_identity(bsmsg, message);
	set_to(bsmsg, libbalsa_message_get_headers(message));
	return bsmsg;
}

BalsaSendmsg *
sendmsg_window_reply(LibBalsaMailbox * mailbox, guint msgno,
                     SendType reply_type)
{
    LibBalsaMessage *message =
        libbalsa_mailbox_get_message(mailbox, msgno);
    BalsaSendmsg *bsmsg;
    LibBalsaMessageHeaders *headers;
    const gchar *message_id;
    GList *in_reply_to;

    g_assert(message);
    switch(reply_type) {
    case SEND_REPLY_GROUP:
        if (libbalsa_message_get_user_header(message, "list-post") == NULL) {
            g_object_unref(message);
            return NULL;
        }
    case SEND_REPLY:
    case SEND_REPLY_ALL:
        bsmsg = sendmsg_window_new();
        bsmsg->type = reply_type;       break;
    default: g_error("reply_type: %d", reply_type);
    }
    bsmsg->parent_message = message;
    set_identity(bsmsg, message);

    bsm_prepare_for_setup(message);

    headers = libbalsa_message_get_headers(message);
    set_to(bsmsg, headers);

    message_id = libbalsa_message_get_message_id(message);
    set_in_reply_to(bsmsg, message_id);
    if (reply_type == SEND_REPLY_ALL)
        set_cc_from_all_recipients(bsmsg, headers);

    in_reply_to = libbalsa_message_get_in_reply_to(message);
    set_references_reply(bsmsg, libbalsa_message_get_references(message),
                         in_reply_to != NULL ? in_reply_to->data : NULL,
                         message_id);
    if(balsa_app.autoquote)
        fill_body_from_message(bsmsg, message, QUOTE_ALL);
    if (libbalsa_identity_get_sig_whenreply(bsmsg->ident))
        insert_initial_sig(bsmsg);
    bsm_finish_setup(bsmsg, libbalsa_message_get_body_list(message));
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
    default: g_error("reply_type: %d", reply_type);
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
        set_in_reply_to(bsmsg, message_id);
        set_references_reply(bsmsg, references,
                             in_reply_to, message_id);
        fill_body_from_part(bsmsg, part->embhdrs, message_id, references,
                            part->parts, QUOTE_ALL);
        g_list_free_full(references, g_free);
    }

    if (reply_type == SEND_REPLY_ALL)
        set_cc_from_all_recipients(bsmsg, part->embhdrs);

    bsm_finish_setup(bsmsg, part);
    if (libbalsa_identity_get_sig_whenreply(bsmsg->ident))
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
            balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                       LIBBALSA_INFORMATION_WARNING,
                                       _("Attaching message failed.\n"
                                         "Possible reason: not enough temporary space"));
        bsmsg->state = SENDMSG_STATE_CLEAN;
        bsmsg_set_subject_from_body(bsmsg, libbalsa_message_get_body_list(message), bsmsg->ident);
    } else {
        bsm_prepare_for_setup(message);
        fill_body_from_message(bsmsg, message, QUOTE_NOPREFIX);
        bsm_finish_setup(bsmsg, libbalsa_message_get_body_list(message));
    }
    if (libbalsa_identity_get_sig_whenforward(bsmsg->ident))
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
    BalsaSendmsg *bsmsg;
    const gchar *postpone_hdr;
    GList *list, *refs = NULL;
    GList *in_reply_to;

    g_assert(message);

    if ((bsmsg = g_object_get_data(G_OBJECT(message),
                                   BALSA_SENDMSG_WINDOW_KEY))) {
        gtk_window_present_with_time(GTK_WINDOW(bsmsg->window),
                                     gtk_get_current_event_time());
        return NULL;
    }

    bsmsg = sendmsg_window_new();
    bsmsg->is_continue = TRUE;
    bsm_prepare_for_setup(message);
    bsmsg->draft_message = message;
    g_object_set_data(G_OBJECT(bsmsg->draft_message),
                      BALSA_SENDMSG_WINDOW_KEY, bsmsg);
    set_identity(bsmsg, message);
    setup_headers_from_message(bsmsg, message);

    libbalsa_address_view_set_from_list(bsmsg->replyto_view,
                                        "Reply To:",
                                        libbalsa_message_get_headers(message)->reply_to);
    in_reply_to = libbalsa_message_get_in_reply_to(message);
    if (in_reply_to != NULL)
        bsmsg->in_reply_to =
            g_strconcat("<", in_reply_to->data, ">", NULL);

    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Crypto")))
        bsmsg_setup_gpg_ui_by_mode(bsmsg, atoi(postpone_hdr));
    postpone_hdr = libbalsa_message_get_user_header(message, "X-Balsa-Att-Pubkey");
    if (postpone_hdr != NULL) {
    	sw_action_set_active(bsmsg, "attpubkey", atoi(postpone_hdr) != 0);
    }
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-MDN")))
        sw_action_set_active(bsmsg, "request-mdn", atoi(postpone_hdr) != 0);
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-DSN")))
        sw_action_set_active(bsmsg, "request-dsn", atoi(postpone_hdr) != 0);
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Lang"))) {
        GtkWidget *langs =
            gtk_menu_item_get_submenu(GTK_MENU_ITEM
                                      (bsmsg->current_language_menu));
        GList *children =
            gtk_container_get_children(GTK_CONTAINER(langs));
        set_locale(bsmsg, postpone_hdr);
        for (list = children; list; list = list->next) {
            GtkCheckMenuItem *menu_item = list->data;
            const gchar *lang;

            lang = g_object_get_data(G_OBJECT(menu_item),
                                     BALSA_LANGUAGE_MENU_LANG);
            if (strcmp(lang, postpone_hdr) == 0)
                gtk_check_menu_item_set_active(menu_item, TRUE);
        }
        g_list_free(children);
    }
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Format")))
        sw_action_set_active(bsmsg, "flowed", strcmp(postpone_hdr, "Fixed"));
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-MP-Alt")))
        sw_action_set_active(bsmsg, "send-html", !strcmp(postpone_hdr, "yes"));
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Send-Type")))
        bsmsg->type = atoi(postpone_hdr);

    for (list = libbalsa_message_get_references(message); list != NULL; list = list->next)
        refs = g_list_prepend(refs, g_strdup(list->data));
    bsmsg->references = g_list_reverse(refs);

    continue_body(bsmsg, message);
    bsm_finish_setup(bsmsg, libbalsa_message_get_body_list(message));
    g_idle_add((GSourceFunc) sw_grab_focus_to_text,
               g_object_ref(bsmsg->text));
    return bsmsg;
}
