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
#include "macosx-helpers.h"
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
    pid_t pid_editor;
    gchar *filename;
    BalsaSendmsg *bsmsg;
} balsa_edit_with_gnome_data;

static gint message_postpone(BalsaSendmsg * bsmsg);

static void balsa_sendmsg_destroy_handler(BalsaSendmsg * bsmsg);
static void check_readiness(BalsaSendmsg * bsmsg);
static void init_menus(BalsaSendmsg *);
static void bsmsg_setup_gpg_ui(BalsaSendmsg *bsmsg);
static void bsmsg_update_gpg_ui_on_ident_change(BalsaSendmsg *bsmsg,
                                                LibBalsaIdentity *new_ident);
static void bsmsg_setup_gpg_ui_by_mode(BalsaSendmsg *bsmsg, gint mode);

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
    "send", "queue", "postpone"
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

    BalsaSendmsg *bsmsg;              /* send message back reference */

    GtkWidget *popup_menu;            /* popup menu */
    LibbalsaVfs *file_uri;            /* file uri of the attachment */
    char *uri_ref;                    /* external body URI reference */
    char *force_mime_type;            /* force using this particular mime type */
    char *charset;                    /* forced character set */
    gboolean is_a_temp_file;          /* destroy the file when not used any more */
    int mode;                         /* LIBBALSA_ATTACH_AS_ATTACHMENT etc. */
    LibBalsaMessageHeaders *headers;  /* information about a forwarded message */

    /* passed to a response handler: */
    GtkTreeIter iter;
    GtkTreeModel *model;
    GSimpleAction *action;
    GVariant      *parameter;

    /* passed to a thread: */
    const char *filename;

    /* passed to an idle handler */
    gboolean is_fwd_message;
    char *content_type;
    char *icon_name;
};

static BalsaAttachInfo* balsa_attach_info_new();
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
    info->is_a_temp_file = FALSE;
    info->mode = LIBBALSA_ATTACH_AS_ATTACHMENT;
    info->headers = NULL;
}

static BalsaAttachInfo*
balsa_attach_info_new(BalsaSendmsg *bsmsg)
{
    BalsaAttachInfo * info = g_object_new(BALSA_TYPE_ATTACH_INFO, NULL);

    info->bsmsg = bsmsg;
    return info;
}

static void
balsa_attach_info_finalize(GObject * object)
{
    BalsaAttachInfo * info;

    info = BALSA_ATTACH_INFO(object);

    /* unlink the file if necessary */
    if (info->is_a_temp_file && info->file_uri != NULL) {
        char *folder_name;

        /* unlink the file */
	g_debug("%s:%s: unlink `%s'", __FILE__, __func__,
		     libbalsa_vfs_get_uri_utf8(info->file_uri));
	libbalsa_vfs_file_unlink(info->file_uri, NULL);

        /* remove the folder if possible */
        folder_name = g_filename_from_uri(libbalsa_vfs_get_folder(info->file_uri),
                                          NULL, NULL);
        if (folder_name != NULL) {
            g_debug("%s:%s: rmdir `%s'", __FILE__, __func__, folder_name);
            g_rmdir(folder_name);
            g_free(folder_name);
        }
    }

    /* clean up memory */
    if (info->file_uri != NULL)
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
        gtk_window_present(GTK_WINDOW(ab));
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
    gtk_widget_show(ab);
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

    gtk_window_destroy(GTK_WINDOW(ab));
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

static void
delete_handler_response(GtkDialog *dialog,
                        int        reply,
                        gpointer   user_data)
{
    BalsaSendmsg *bsmsg = user_data;
    gboolean keep_open = FALSE;

    gtk_window_destroy(GTK_WINDOW(dialog));

    switch (reply) {
    case GTK_RESPONSE_YES:
        if (bsmsg->state == SENDMSG_STATE_MODIFIED)
            if (!message_postpone(bsmsg))
                keep_open = TRUE;
        break;
    case GTK_RESPONSE_NO:
        if (!bsmsg->is_continue)
            sw_delete_draft(bsmsg);
        break;
    default:
        keep_open = TRUE;
    }

    if (!keep_open)
        gtk_window_destroy(GTK_WINDOW(bsmsg->window));
}

static void
delete_handler(BalsaSendmsg * bsmsg)
{
    InternetAddressList *list;
    InternetAddress *ia;
    const gchar *tmp = NULL;
    gchar *free_me = NULL;
    GtkWidget *dialog;

    g_debug("%s", __func__);

    if (bsmsg->state == SENDMSG_STATE_CLEAN) {
        gtk_window_destroy(GTK_WINDOW(bsmsg->window));
        return;
    }

    list = libbalsa_address_view_get_list(bsmsg->recipient_view, "To:");
    ia = internet_address_list_get_address(list, 0);
    if (ia) {
        tmp = ia->name;
        if (!tmp || !*tmp)
            tmp = free_me = internet_address_to_string(ia, NULL, FALSE);
    }
    if (!tmp || !*tmp)
        tmp = _("(No name)");

    dialog = gtk_message_dialog_new(GTK_WINDOW(bsmsg->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_YES_NO,
                                    _("The message to “%s” is modified.\n"
                                      "Save message to Draftbox?"), tmp);
    g_free(free_me);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(bsmsg->window));
#endif
    g_object_unref(list);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          _("_Cancel"), GTK_RESPONSE_CANCEL);

    g_signal_connect(dialog, "response", G_CALLBACK(delete_handler_response), bsmsg);
    gtk_widget_show(dialog);
}

static gboolean
sw_close_request_cb(GtkWidget * widget, GdkEvent * e, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    delete_handler(bsmsg);

    /* Block closing; delete_handler will close if appropriate */
    return TRUE;
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
    delete_handler(bsmsg);
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

    if (balsa_app.main_window != NULL) {
        g_signal_handler_disconnect(balsa_app.main_window,
                                    bsmsg->delete_sig_id);
        g_signal_handler_disconnect(balsa_app.main_window,
                                    bsmsg->identities_changed_id);
        g_object_weak_unref(G_OBJECT(balsa_app.main_window),
                            (GWeakNotify) gtk_window_destroy, bsmsg->window);
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
    gtk_window_destroy(GTK_WINDOW(bsmsg->window));
    g_free(bsmsg->fcc_url);
    g_free(bsmsg->in_reply_to);
    g_list_free_full(bsmsg->references, g_free);

#if !(HAVE_GSPELL || HAVE_GTKSPELL)
    if (bsmsg->spell_checker)
        gtk_window_destroy(GTK_WINDOW(bsmsg->spell_checker));
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

static gboolean
edit_with_gnome_check(gpointer data) {
    FILE *tmp;
    balsa_edit_with_gnome_data *data_real = (balsa_edit_with_gnome_data *)data;
    GtkTextBuffer *buffer;

    pid_t pid;
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
    if (balsa_app.edit_headers) {
        while (fgets(line, sizeof(line), tmp)) {
            guint type;

            if (line[strlen(line) - 1] == '\n')
                line[strlen(line) - 1] = '\0';

            if (libbalsa_str_has_prefix(line, _("Subject:")) == 0) {
                gtk_editable_set_text(GTK_EDITABLE(data_real->bsmsg->subject[1]),
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
    while(fgets(line, sizeof(line), tmp))
        gtk_text_buffer_insert_at_cursor(buffer, line, -1);
    sw_buffer_signals_unblock(data_real->bsmsg, buffer);

    fclose(tmp);
    unlink(data_real->filename);
    g_free(data_real->filename);
    gtk_widget_set_sensitive(data_real->bsmsg->text, TRUE);
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
sw_edit_activated(GSimpleAction * action,
                  GVariant      * parameter,
                  gpointer        data)
{
    BalsaSendmsg *bsmsg = data;
    static const char TMP_PATTERN[] = "/tmp/balsa-edit-XXXXXX";
    gchar filename[sizeof(TMP_PATTERN)];
    balsa_edit_with_gnome_data *edit_data;
    pid_t pid;
    FILE *tmp;
    int tmpfd;
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    gchar *p;
    GAppInfo *app;
    char **argv;
    int argc;

    app = g_app_info_get_default_for_type("text/plain", FALSE);
    if (!app) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("GNOME editor is not defined"
                                     " in your preferred applications."));
        return;
    }

    argc = 2;
    argv = g_new0 (char *, argc + 1);
    argv[0] = g_strdup(g_app_info_get_executable(app));
    strcpy(filename, TMP_PATTERN);
    argv[1] =
        g_strdup_printf("%s%s",
                        g_app_info_supports_uris(app) ? "file://" : "",
                        filename);
    /* FIXME: how can I detect if the called application needs the
     * terminal??? */
    g_object_unref(app);

    tmpfd = mkstemp(filename);
    tmp = fdopen(tmpfd, "w+");

    if(balsa_app.edit_headers) {
        guint type;

        fprintf(tmp, "%s %s\n", _("Subject:"),
                gtk_editable_get_text(GTK_EDITABLE(bsmsg->subject[1])));
        for (type = 0; type < G_N_ELEMENTS(address_types); type++) {
            InternetAddressList *list =
                libbalsa_address_view_get_list(bsmsg->recipient_view,
                                               address_types[type]);
            gchar *addr_string = internet_address_list_to_string(list, NULL, FALSE);
            g_object_unref(list);
            fprintf(tmp, "%s %s\n", _(address_types[type]), addr_string);
            g_free(addr_string);
        }
        fprintf(tmp, "\n");
    }

    gtk_widget_set_sensitive(GTK_WIDGET(bsmsg->text), FALSE);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    p = gtk_text_iter_get_text(&start, &end);
    fputs(p, tmp);
    g_free(p);
    fclose(tmp);
    if ((pid = fork()) < 0) {
        perror ("fork");
        g_strfreev(argv);
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
    edit_data = g_malloc(sizeof(balsa_edit_with_gnome_data));
    edit_data->pid_editor = pid;
    edit_data->filename = g_strdup(filename);
    edit_data->bsmsg = bsmsg;
    g_timeout_add(200, (GSourceFunc)edit_with_gnome_check, edit_data);
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
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsmsg->from[1]),
                             g_list_index(balsa_app.identities, ident));

    addr = libbalsa_identity_get_replyto(ident);
    if (addr != NULL && addr[0] != '\0') {
        libbalsa_address_view_set_from_string(bsmsg->replyto_view,
                                              "Reply To:",
                                              addr);
        gtk_widget_show(bsmsg->replyto[0]);
        gtk_widget_show(bsmsg->replyto[1]);
    } else if (!sw_action_get_active(bsmsg, "reply-to")) {
        gtk_widget_hide(bsmsg->replyto[0]);
        gtk_widget_hide(bsmsg->replyto[1]);
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
    subject = gtk_editable_get_text(GTK_EDITABLE(bsmsg->subject[1]));

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
	gtk_editable_set_text(GTK_EDITABLE(bsmsg->subject[1]), tmpstr);
	g_free(tmpstr);
    } else if (((fwdlen = strlen(old_forward_string)) > 0) &&
	       (strncmp(subject, old_forward_string, fwdlen) == 0)) {
	tmpstr = g_strconcat(forward_string, &(subject[fwdlen]), NULL);
	gtk_editable_set_text(GTK_EDITABLE(bsmsg->subject[1]), tmpstr);
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
remove_attachment(GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
    BalsaAttachInfo *info = user_data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    BalsaAttachInfo *test_info;

    /* get the selected element */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(info->bsmsg->tree_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    /* make sure we got the right element */
    gtk_tree_model_get(model, &iter, ATTACH_INFO_COLUMN, &test_info, -1);

    if (test_info == info) {
        /* remove the attachment */
        gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
    }

    g_object_unref(test_info);
}

static void
change_attach_mode_response(GtkDialog *dialog,
                            int        response_id,
                            gpointer   user_data)
{
    BalsaAttachInfo *info = user_data;

    if (dialog != NULL)
        gtk_window_destroy(GTK_WINDOW(dialog));

    if (response_id == GTK_RESPONSE_YES) {
        /* change the attachment mode */
        gtk_list_store_set(GTK_LIST_STORE(info->model), &info->iter,
                           ATTACH_MODE_COLUMN, info->mode, -1);
        g_simple_action_set_state(info->action, info->parameter);
    }

    g_variant_unref(info->parameter);
    gtk_popover_popdown((GtkPopover *) info->popup_menu);
}

/* change attachment mode - right mouse button callback */
static void
change_attach_mode(GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
    BalsaAttachInfo *info = user_data;
    int new_mode;
    GtkTreeSelection *selection;
    BalsaAttachInfo *test_info;

    new_mode = g_variant_get_int32(parameter);
    if (new_mode == info->mode)
        return;

    /* get the selected element */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(info->bsmsg->tree_view));
    if (!gtk_tree_selection_get_selected(selection, &info->model, &info->iter))
	return;

    /* make sure we got the right element */
    gtk_tree_model_get(info->model, &info->iter, ATTACH_INFO_COLUMN, &test_info, -1);
    if (test_info != info) {
        g_object_unref(test_info);

	return;
    }
    g_object_unref(test_info);

    info->mode = new_mode;
    info->action = action;
    info->parameter = g_variant_ref(parameter);
    if (new_mode == LIBBALSA_ATTACH_AS_EXTBODY) {
        /* verify that the user *really* wants to attach as reference */
	GtkWidget *extbody_dialog;
	GtkRoot *parent;

	parent = gtk_widget_get_root(info->bsmsg->window);
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
#if HAVE_MACOSX_DESKTOP
	libbalsa_macosx_menu_for_parent(extbody_dialog, GTK_WINDOW(parent));
#endif
	gtk_window_set_title(GTK_WINDOW(extbody_dialog),
			     _("Attach as Reference?"));

        g_signal_connect(extbody_dialog, "response", G_CALLBACK(change_attach_mode_response), info);
        gtk_widget_show(extbody_dialog);
    } else {
        change_attach_mode_response(NULL, GTK_RESPONSE_YES, info);
    }
}


/* attachment vfs menu - right mouse button callback */
static void
launch_app_activated(GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
    const gchar *app_name = g_variant_get_string(parameter, NULL);
    BalsaAttachInfo *info = user_data;
    GError *err = NULL;
    gboolean result;

    result = libbalsa_vfs_launch_app(info->file_uri, app_name, &err);
    if (!result)
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not launch application: %s"),
                          err ? err->message : _("Unknown error"));
    g_clear_error(&err);

    if (GTK_IS_POPOVER(info->popup_menu))
        gtk_popover_popdown((GtkPopover *) info->popup_menu);
}


/* URL external body - right mouse button callback */
static void
open_attachment_finish(GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
    GtkWindow *parent = GTK_WINDOW(source_object);
    char *url = user_data;
    GError *error = NULL;

    if (!gtk_show_uri_full_finish(parent, res, &error)) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Error showing %s: %s\n"), url, error->message);
        g_error_free(error);
    }

    g_free(url);
}

static void
open_attachment(GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
    BalsaAttachInfo *info = user_data;
    GtkRoot *root;
    const char * uri;

    uri = libbalsa_vfs_get_uri(info->file_uri);

    g_debug("open URL %s", uri);
    root = gtk_widget_get_root(info->bsmsg->window);
    gtk_show_uri_full(GTK_WINDOW(root), uri, GDK_CURRENT_TIME, NULL,
                      open_attachment_finish, g_strdup(uri));
}

static GtkWidget * sw_attachment_list(BalsaSendmsg *bsmsg);
static void
show_attachment_widget(BalsaSendmsg *bsmsg)
{
    GtkPaned *outer_paned;
    GtkWidget *child;

    outer_paned = GTK_PANED(bsmsg->paned);
    child = gtk_paned_get_start_child(outer_paned);

    if (!GTK_IS_PANED(child)) {
        int position;
        GtkRequisition minimum_size;
        GtkWidget *paned;
        GtkPaned *inner_paned;

        position = gtk_paned_get_position(outer_paned);
        if (position <= 0) {
            gtk_widget_get_preferred_size(child, &minimum_size, NULL);
            position = minimum_size.height;
        }
        gtk_widget_unparent(g_object_ref(child));

        paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);

        inner_paned = GTK_PANED(paned);
        gtk_paned_set_start_child(inner_paned, child);
        g_object_unref(child);

        child = sw_attachment_list(bsmsg);
        gtk_paned_set_end_child(inner_paned, child);
        gtk_paned_set_position(inner_paned, position);

        gtk_widget_get_preferred_size(child, &minimum_size, NULL);
        gtk_paned_set_start_child(outer_paned, paned);
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

typedef struct {
    BalsaSendmsg   *bsmsg;
    GMutex          lock;
    GCond           cond;

    const char     *filename;
    const char     *content_type;
    gboolean       *change_type;
    GtkWidget      *combo_box;
    GtkWidget      *charset_button;

    LibBalsaCodeset codeset;
    gboolean        done;
} user_codeset_data;

static void
sw_get_user_codeset_response(GtkDialog *dialog,
                             int        response_id,
                             gpointer   user_data)
{
    user_codeset_data *data = user_data;

    g_mutex_lock(&data->lock);

    if (response_id == GTK_RESPONSE_OK) {
        if (data->change_type != NULL)
            *data->change_type =
                gtk_combo_box_get_active(GTK_COMBO_BOX(data->combo_box)) != 0;
        if (data->change_type == NULL || !*data->change_type)
	    data->codeset = gtk_combo_box_get_active(GTK_COMBO_BOX(data->charset_button));
    }

    data->done = TRUE;

    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->lock);

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static gboolean
sw_get_user_codeset(gpointer user_data)
{
    user_codeset_data *data = user_data;

    BalsaSendmsg *bsmsg   = data->bsmsg;
    gboolean *change_type = data->change_type;
    const char *mime_type = data->content_type;
    const char *fname     = data->filename;

    GtkWidget *combo_box = NULL;
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(_("Choose character set"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    _("_OK"), GTK_RESPONSE_OK,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    NULL);
    char *msg = g_strdup_printf(_("File\n%s\nis not encoded in US-ASCII or UTF-8.\n"
                                  "Please choose the character set used to encode the file."),
                                fname);
    GtkWidget *info = gtk_label_new(msg);
    GtkWidget *charset_button = libbalsa_charset_button_new();
    GtkWidget *content_box;

#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(bsmsg->window));
#endif

    g_free(msg);
    content_box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    gtk_widget_set_margin_top(info, 5);
    gtk_widget_set_margin_bottom(info, 5);
    gtk_box_append(GTK_BOX(content_box), info);

    gtk_widget_set_vexpand(charset_button, TRUE);
    gtk_widget_set_valign(charset_button, GTK_ALIGN_FILL);
    gtk_widget_set_margin_top(charset_button, 5);
    gtk_widget_set_margin_bottom(charset_button, 5);
    gtk_box_append(GTK_BOX(content_box), charset_button);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    if (change_type != NULL) {
        GtkWidget *label = gtk_label_new(_("Attach as MIME type:"));
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        combo_box = gtk_combo_box_text_new();

        gtk_widget_set_vexpand(hbox, TRUE);
        gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
        gtk_widget_set_margin_top(hbox, 5);
        gtk_widget_set_margin_bottom(hbox, 5);
        gtk_box_append(GTK_BOX(content_box), hbox);

        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_halign(label, GTK_ALIGN_FILL);
        gtk_box_append(GTK_BOX(hbox), label);

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
        gtk_box_append(GTK_BOX(hbox), combo_box);
    }

    data->change_type = change_type;
    data->combo_box = combo_box;
    data->charset_button = charset_button;

    g_signal_connect(dialog, "response", G_CALLBACK(sw_get_user_codeset_response), data);
    gtk_widget_show(dialog);

    return G_SOURCE_REMOVE;
}

static gboolean
sw_set_charset(BalsaSendmsg *bsmsg,
               const char   *filename,
               const char   *content_type,
               gboolean     *change_type,
               char        **attach_charset)
{
    const char *charset;
    LibBalsaTextAttribute attr;

    g_assert(libbalsa_am_i_subthread());

    attr = libbalsa_text_attr_file(filename);
    if ((int) attr < 0)
        return FALSE;

    if ((int) attr == 0)
        charset = "us-ascii";
    else if ((int) (attr & LIBBALSA_TEXT_HI_UTF8) != 0)
        charset = "UTF-8";
    else {
        user_codeset_data data;
        LibBalsaCodesetInfo *info;

        g_mutex_init(&data.lock);
        g_cond_init(&data.cond);

        data.bsmsg = bsmsg;
        data.change_type = change_type;
        data.content_type = content_type;
        data.filename = filename;
        data.codeset = (LibBalsaCodeset) (-1);

        g_mutex_lock(&data.lock);
        data.done = FALSE;
        g_idle_add(sw_get_user_codeset, &data);
        while (!data.done)
            g_cond_wait(&data.cond, &data.lock);
        g_mutex_unlock(&data.lock);

        g_mutex_clear(&data.lock);
        g_cond_clear(&data.cond);

        if (*change_type)
            return TRUE;
        if (data.codeset == (LibBalsaCodeset) (-1))
            return FALSE;

        info = &libbalsa_codeset_info[data.codeset];
        charset = info->std;
        if (info->win != NULL && (attr & LIBBALSA_TEXT_HI_CTRL) != 0) {
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


/*
 * add_attachment:
 * adds given filename (uri format) to the list.
 */

static gboolean
add_attachment_idle(gpointer user_data)
{
    BalsaAttachInfo *attach_data = user_data;
    char *utf8name;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean can_inline;
    GSimpleActionGroup *simple;
    static GActionEntry attachment_entries[] = {
        {"new-mode", NULL, "i", "1", change_attach_mode},
        {"remove", remove_attachment},
        {"launch-app", launch_app_activated, "s"}
    };
    GMenu *menu;
    GMenu *section;
    static int attachment_number = 0;
    char *attachment_namespace;
    char *content_desc;

    if (--attach_data->bsmsg->n_attachments == 0)
        gtk_window_destroy(GTK_WINDOW(attach_data->bsmsg->attach_dialog));

    if (attach_data->is_fwd_message) {
	attach_data->headers = get_fwd_mail_headers(attach_data->filename);
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
        const char *uri_utf8 = libbalsa_vfs_get_uri_utf8(attach_data->file_uri);
	const char *home = g_getenv("HOME");

	if (home && !strncmp(uri_utf8, "file://", 7) &&
            !strncmp(uri_utf8 + 7, home, strlen(home))) {
	    utf8name = g_strdup_printf("~%s", uri_utf8 + 7 + strlen(home));
	} else
	    utf8name = g_strdup(uri_utf8);
    }

    show_attachment_widget(attach_data->bsmsg);

    model = BALSA_MSG_ATTACH_MODEL(attach_data->bsmsg);
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);

    can_inline = !attach_data->is_a_temp_file &&
	(!g_ascii_strncasecmp(attach_data->content_type, "text/", 5) ||
	 !g_ascii_strncasecmp(attach_data->content_type, "image/", 6));
    attach_data->mode = LIBBALSA_ATTACH_AS_ATTACHMENT;

    /* build the attachment's popup menu */
    simple = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(simple),
                                    attachment_entries,
                                    G_N_ELEMENTS(attachment_entries),
                                    attach_data);

    attachment_namespace = g_strdup_printf("attachment-%d", ++attachment_number);
    gtk_widget_insert_action_group(attach_data->bsmsg->window,
                                   attachment_namespace,
                                   G_ACTION_GROUP(simple));
    g_object_unref(simple);

    menu = g_menu_new();

    /* only real text/... and image/... parts may be inlined */
    if (can_inline) {
        GMenuItem *menu_item =
            g_menu_item_new(_(attach_modes[LIBBALSA_ATTACH_AS_INLINE]), NULL);
        g_menu_item_set_action_and_target(menu_item, "new-mode", "i",
                                          LIBBALSA_ATTACH_AS_INLINE);
        g_menu_append_item(menu, menu_item);
        g_object_unref(menu_item);
    }

    /* all real files can be attachments */
    if (can_inline || !attach_data->is_a_temp_file) {
        GMenuItem *menu_item =
            g_menu_item_new(_(attach_modes[LIBBALSA_ATTACH_AS_ATTACHMENT]), NULL);
        g_menu_item_set_action_and_target(menu_item, "new-mode", "i",
                                          LIBBALSA_ATTACH_AS_ATTACHMENT);
        g_menu_append_item(menu, menu_item);
        g_object_unref(menu_item);
    }

    /* real files may be references (external body) */
    if (!attach_data->is_a_temp_file) {
        GMenuItem *menu_item =
            g_menu_item_new(_(attach_modes[LIBBALSA_ATTACH_AS_EXTBODY]), NULL);
        g_menu_item_set_action_and_target(menu_item, "new-mode", "i",
                                          LIBBALSA_ATTACH_AS_EXTBODY);
        g_menu_append_item(menu, menu_item);
        g_object_unref(menu_item);
    }

    /* an attachment can be removed */
    section = g_menu_new();
    g_menu_append(section, _("Remove"), "remove");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    /* add the usual vfs menu so the user can inspect what (s)he actually
       attached... (only for non-message attachments) */
    if (!attach_data->is_fwd_message) {
        section = g_menu_new();
        libbalsa_vfs_fill_menu_by_content_type(section, attach_data->content_type, "launch-app");
        g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
        g_object_unref(section);
    }

    attach_data->popup_menu =
        libbalsa_popup_widget_new(attach_data->bsmsg->tree_view,
                                  G_MENU_MODEL(menu),
                                  attachment_namespace);

    g_object_unref(menu);
    g_free(attachment_namespace);

    /* append to the list store */
    content_desc = libbalsa_vfs_content_description(attach_data->content_type);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
		       ATTACH_INFO_COLUMN, attach_data,
		       ATTACH_ICON_COLUMN, attach_data->icon_name,
		       ATTACH_TYPE_COLUMN, content_desc,
		       ATTACH_MODE_COLUMN, attach_data->mode,
		       ATTACH_SIZE_COLUMN, libbalsa_vfs_get_size(attach_data->file_uri),
		       ATTACH_DESC_COLUMN, utf8name,
		       -1);
    g_object_unref(attach_data);
    g_free(attach_data->icon_name);
    g_free(utf8name);
    g_free(attach_data->content_type);
    g_free(content_desc);

    return G_SOURCE_REMOVE;
}

static gpointer
add_attachment_thread(gpointer user_data)
{
    BalsaAttachInfo *attach_data = user_data;

    /* get the icon for the attachment's content type */
    attach_data->is_fwd_message = attach_data->force_mime_type != NULL &&
	g_ascii_strncasecmp(attach_data->force_mime_type, "message/", 8) == 0 &&
        attach_data->is_a_temp_file;

    if (attach_data->is_fwd_message)
	attach_data->content_type = g_strdup(attach_data->force_mime_type);
    else
        attach_data->content_type = NULL;

    attach_data->icon_name = libbalsa_icon_name_finder(attach_data->force_mime_type,
                                                       attach_data->file_uri,
                                                       &attach_data->content_type);

    if (attach_data->content_type == NULL) {
	/* Last ditch. */
	attach_data->content_type = g_strdup("application/octet-stream");
    }

    attach_data->charset = NULL;
    if (g_ascii_strncasecmp(attach_data->content_type, "text/", 5) == 0) {
	gboolean change_type = FALSE;

	if (!sw_set_charset(attach_data->bsmsg, attach_data->filename, attach_data->content_type,
			    &change_type, &attach_data->charset)) {
	    g_free(attach_data->content_type);
	    g_object_unref(attach_data);
	    return NULL;
	}

	if (change_type) {
            g_free(attach_data->force_mime_type);
	    attach_data->force_mime_type = g_strdup("application/octet-stream");
	    g_free(attach_data->content_type);
	    attach_data->content_type = g_strdup(attach_data->force_mime_type);
	}
    }

    g_idle_add(add_attachment_idle, attach_data);

    return NULL;
}

void
add_attachment(BalsaSendmsg *bsmsg,
               const char   *filename,
               gboolean      is_a_temp_file,
               const char   *forced_mime_type)
{
    BalsaAttachInfo *attach_data;
    LibbalsaVfs * file_uri;
    GError *err = NULL;

    g_debug("Trying to attach '%s'", filename);
    if (!(file_uri = libbalsa_vfs_new_from_uri(filename))) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("Cannot create file URI object for %s"),
                                   filename);
        return;
    }
    if (!libbalsa_vfs_is_regular_file(file_uri, &err)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_ERROR,
                                   "%s: %s", filename,
                                   err && err->message ? err->message : _("Unknown error"));
	g_error_free(err);
	g_object_unref(file_uri);
	return;
    }

    /* create a new attachment info block */
    attach_data = balsa_attach_info_new(bsmsg);

    attach_data->is_a_temp_file  = is_a_temp_file;
    attach_data->force_mime_type = g_strdup(forced_mime_type);
    attach_data->filename        = filename;
    attach_data->file_uri        = file_uri;

    g_thread_unref(g_thread_new("add-attachment-thread", add_attachment_thread, attach_data));
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
    GSimpleActionGroup *simple;
    static GActionEntry attachment_entries[] = {
        {"remove", remove_attachment},
        {"open", open_attachment, "s"}
    };
    GMenu *menu;
    GMenu *section;

    g_debug("Trying to attach '%s'", url);

    /* create a new attachment info block */
    attach_data = balsa_attach_info_new(bsmsg);
    attach_data->charset = NULL;

    show_attachment_widget(bsmsg);

    model = BALSA_MSG_ATTACH_MODEL(bsmsg);
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);

    attach_data->uri_ref = g_strconcat("URL:", url, NULL);
    attach_data->force_mime_type = g_strdup("message/external-body");
    attach_data->is_a_temp_file = FALSE;
    attach_data->mode = LIBBALSA_ATTACH_AS_EXTBODY;
    attach_data->file_uri = libbalsa_vfs_new_from_uri(url);

    /* build the attachment's popup menu - may only be removed */
    simple = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(simple),
                                    attachment_entries,
                                    G_N_ELEMENTS(attachment_entries),
                                    attach_data);
    gtk_widget_insert_action_group(bsmsg->window,
                                   "urlref-attachment",
                                   G_ACTION_GROUP(simple));
    g_object_unref(simple);

    menu = g_menu_new();

    section = g_menu_new();
    g_menu_append(section, _("Remove"), "remove");

    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    /* add a separator and the usual vfs menu so the user can inspect what
       (s)he actually attached... (only for non-message attachments) */
    section = g_menu_new();
    g_menu_append(section, _("Open…"), "open");

    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    attach_data->popup_menu =
        libbalsa_popup_widget_new(bsmsg->window, G_MENU_MODEL(menu), "urlref-attachment");
    g_object_unref(menu);

    /* append to the list store */
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
		       ATTACH_INFO_COLUMN, attach_data,
		       ATTACH_ICON_COLUMN, "go-jump",
		       ATTACH_TYPE_COLUMN, _("(URL)"),
		       ATTACH_MODE_COLUMN, attach_data->mode,
		       ATTACH_SIZE_COLUMN, 0,
		       ATTACH_DESC_COLUMN, url,
		       -1);
    g_object_unref(attach_data);

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
    GtkFileChooser * chooser;
    GListModel *list;
    unsigned n_items;
    unsigned position;
    GFile *file;

    g_object_set_data(G_OBJECT(bsmsg->window),
                      "balsa-sendmsg-window-attach-dialog", NULL);

    if (response != GTK_RESPONSE_OK) {
	gtk_window_destroy(GTK_WINDOW(dialog));
	return;
    }

    chooser = GTK_FILE_CHOOSER(dialog);
    list = gtk_file_chooser_get_files(chooser);

    bsmsg->attach_dialog = dialog;
    bsmsg->n_attachments = n_items = g_list_model_get_n_items(list);
    for (position = 0; position < n_items; position++) {
        char *path;

        file = g_list_model_get_item(list, position);
        path = g_file_get_path(file);
        g_object_unref(file);

        add_attachment(bsmsg, path, FALSE, NULL);
        g_free(path);
    }

    g_object_unref(list);

    g_free(balsa_app.attach_dir);
    file = gtk_file_chooser_get_current_folder(chooser);
    balsa_app.attach_dir = g_file_get_path(file);
    g_object_unref(file);
}

static GtkFileChooser *
sw_attach_dialog(BalsaSendmsg * bsmsg)
{
    GtkWidget *dialog;
    GtkFileChooser *chooser;

    dialog =
        gtk_file_chooser_dialog_new(_("Attach file"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(bsmsg->window));
#endif
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

    chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_select_multiple(chooser, TRUE);
    if (balsa_app.attach_dir != NULL) {
        GFile *file;

        file = g_file_new_for_path(balsa_app.attach_dir);
	gtk_file_chooser_set_current_folder(chooser, file, NULL);
        g_object_unref(file);
    }

    g_signal_connect(dialog, "response", G_CALLBACK(attach_dialog_response), bsmsg);

    gtk_widget_show(dialog);

    return chooser;
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

static gboolean
sw_insert_message_idle(gpointer data)
{
    BalsaSendmsg *bsmsg = data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GString *body = bsmsg->body;

    gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);

    g_string_free(body, TRUE);

    return G_SOURCE_REMOVE;
}

static gpointer
sw_insert_message_thread(gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    if (bsmsg->message != NULL) {
        bsmsg->body = quote_message_body(bsmsg, bsmsg->message, bsmsg->quote_type);
        g_idle_add(sw_insert_message_idle, bsmsg);
        g_object_remove_weak_pointer(G_OBJECT(bsmsg->message), (gpointer *) &bsmsg->message);
    }

    return NULL;
}

static void
sw_insert_message(BalsaSendmsg    *bsmsg,
                  LibBalsaMessage *message,
                  QuoteType        qtype)
{
    bsmsg->message = message;
    g_object_add_weak_pointer(G_OBJECT(message), (gpointer *) &bsmsg->message);
    bsmsg->quote_type = qtype;

    g_thread_unref(g_thread_new("insert-message", sw_insert_message_thread, bsmsg));
}

static void
insert_selected_messages(BalsaSendmsg *bsmsg, QuoteType type)
{
    GtkWidget *bindex =
	balsa_window_find_current_index(balsa_app.main_window);
    GList *selected_list;

    if (bindex != NULL &&
        (selected_list = balsa_index_selected_list(BALSA_INDEX(bindex))) != NULL) {
	GList *node;

	for (node = selected_list; node != NULL; node = node->next) {
	    LibBalsaMessage *message = node->data;

            sw_insert_message(bsmsg, message, type);
	}
	g_list_free_full(selected_list, g_object_unref);
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


/* sw_attachment_drop - attachments field D&D callback */

/* Helper: check if the passed parameter contains a valid RFC 2396 URI (leading
 * & trailing whitespaces allowed). Return a newly allocated string with the
 * spaces stripped on success or NULL on fail. Note that the URI may still be
 * malformed. */
static gchar *
rfc2396_uri(const char *instr)
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

static gboolean
sw_attachment_drop(GtkDropTarget *drop_target,
                   GValue        *value,
                   double         x,
                   double         y,
                   gpointer       user_data)
{
    BalsaSendmsg *bsmsg = user_data;
    gboolean drag_result = FALSE;

    g_debug("sw_attachment_drop");

    if (G_VALUE_HOLDS(value, BALSA_TYPE_INDEX)) {
	BalsaIndex *bindex = g_value_get_object(value);
	LibBalsaMailbox *mailbox = balsa_index_get_mailbox(bindex);
        GArray *selected = balsa_index_selected_msgnos_new(bindex);
	unsigned i;

        for (i = 0; i < selected->len; i++) {
	    unsigned msgno = g_array_index(selected, guint, i);
	    LibBalsaMessage *message =
		libbalsa_mailbox_get_message(mailbox, msgno);
            if (message == NULL)
                continue;

            if(!attach_message(bsmsg, message))
                balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                           LIBBALSA_INFORMATION_WARNING,
                                           _("Attaching message failed.\n"
                                             "Possible reason: not enough temporary space"));
	    g_object_unref(message);
        }
        balsa_index_selected_msgnos_free(bindex, selected);

        drag_result = TRUE;
    } else if (G_VALUE_HOLDS(value, G_TYPE_STRV)) {
        const char **uri;

        for (uri = g_value_get_pointer(value); uri != NULL; ++uri)
	    add_attachment(bsmsg, *uri, FALSE, NULL);

        drag_result = TRUE;
    } else if (G_VALUE_HOLDS_STRING(value)) {
        const char *url_string;
	char *url;

        url_string = g_value_get_string(value);
        url = rfc2396_uri(url_string);

	if (url != NULL) {
	    add_urlref_attachment(bsmsg, url);
            g_free(url);
            drag_result = TRUE;
        }
    }

    return drag_result;
}

/* sw_address_view_drop - address-view D&D callback; we assume it's a To: address */
static gboolean
sw_address_view_drop(GtkDropTarget *drop_target,
                     GValue        *value,
                     double         x,
                     double         y,
                     gpointer       user_data)
{
    GtkWidget *address_view = user_data;
    GdkDrop *drop;
    const char *address;

    drop = gtk_drop_target_get_drop(drop_target);

    if (drop == NULL || !G_VALUE_HOLDS_STRING(value))
        return FALSE;

    address = g_value_get_string(value);
    libbalsa_address_view_add_from_string(LIBBALSA_ADDRESS_VIEW(address_view), "To:", address);

    return TRUE;
}

/*
 * static void create_email_or_string_entry()
 *
 * Creates a gtk_label()/entry pair.
 *
 * Input: GtkWidget* grid       - Grid to attach to.
 *        const gchar* label     - Label string.
 *        int y_pos              - position in the grid.
 *        arr                    - arr[1] is the entry widget.
 *
 * Output: GtkWidget* arr[] - arr[0] will be the label widget.
 */

#define BALSA_COMPOSE_ENTRY "balsa-compose-entry"

static void
create_email_or_string_entry(BalsaSendmsg * bsmsg,
                             GtkWidget    * grid,
                             const gchar  * label,
                             int            y_pos,
                             GtkWidget    * arr[])
{
    GtkWidget *mnemonic_widget;

    mnemonic_widget = arr[1];
    if (GTK_IS_FRAME(mnemonic_widget))
        mnemonic_widget = gtk_frame_get_child(GTK_FRAME(mnemonic_widget));
    arr[0] = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(arr[0]), mnemonic_widget);
    gtk_widget_set_halign(arr[0], GTK_ALIGN_START);

    gtk_widget_set_margin_top(arr[0], GNOME_PAD_SMALL);
    gtk_widget_set_margin_bottom(arr[0], GNOME_PAD_SMALL);
    gtk_widget_set_margin_start(arr[0], GNOME_PAD_SMALL);
    gtk_widget_set_margin_end(arr[0], GNOME_PAD_SMALL);

    gtk_grid_attach(GTK_GRID(grid), arr[0], 0, y_pos, 1, 1);

    if (!balsa_app.use_system_fonts) {
        gchar *css;
        GtkCssProvider *css_provider;

        gtk_widget_set_name(arr[1], BALSA_COMPOSE_ENTRY);
        css = libbalsa_font_string_to_css(balsa_app.message_font,
                                          BALSA_COMPOSE_ENTRY);

        css_provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css_provider, css, -1);
        g_free(css);

        gtk_style_context_add_provider(gtk_widget_get_style_context(arr[1]) ,
                                       GTK_STYLE_PROVIDER(css_provider),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css_provider);
    }

    gtk_widget_set_hexpand(arr[1], TRUE);
    gtk_grid_attach(GTK_GRID(grid), arr[1], 1, y_pos, 1, 1);
}


/*
 * static void create_string_entry()
 *
 * Creates a gtk_label()/gtk_entry() pair.
 *
 * Input: GtkWidget* grid       - Grid to attach to.
 *        const gchar* label     - Label string.
 *        int y_pos              - position in the grid.
 *
 * Output: GtkWidget* arr[] - arr[0] will be the label widget.
 *                          - arr[1] will be the entry widget.
 */
static void
create_string_entry(BalsaSendmsg * bsmsg,
                    GtkWidget    * grid,
                    const gchar  * label,
                    int            y_pos,
                    GtkWidget    * arr[])
{
    arr[1] = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(arr[1]), 2048);
    create_email_or_string_entry(bsmsg, grid, label, y_pos, arr);
}

/*
 * static void create_email_entry()
 *
 * Creates a gtk_label()/libbalsa_address_view() and button in a grid for
 * e-mail entries, eg. To:.  It also sets up some callbacks in gtk.
 *
 * Input:
 *         BalsaSendmsg *bsmsg  - The send message window
 *         GtkWidget *grid   - grid to insert the widgets into.
 *         int y_pos          - How far down in the grid to put label.
 * On return, bsmsg->address_view and bsmsg->addresses[1] have been set.
 */

static void
create_email_entry(BalsaSendmsg         * bsmsg,
                   GtkWidget            * grid,
                   int                    y_pos,
                   LibBalsaAddressView ** view,
                   GtkWidget           ** widget,
                   const gchar          * label,
                   const gchar * const  * types,
                   guint                  n_types)
{
    GtkWidget *scroll;
    GtkDropTarget *drop_target;

    *view = libbalsa_address_view_new(types, n_types,
                                      balsa_app.address_book_list,
                                      balsa_app.convert_unknown_8bit);

    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    /* This is a horrible hack, but we need to make sure that the
     * recipient list is more than one line high: */
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll),
                                               60);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(*view));

    widget[1] = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(widget[1]), scroll);

    create_email_or_string_entry(bsmsg, grid, _(label), y_pos, widget);

    g_signal_connect(*view, "open-address-book",
		     G_CALLBACK(address_book_cb), bsmsg);

    /* Drag and drop */
    drop_target = gtk_drop_target_new(G_TYPE_STRING, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    gtk_widget_add_controller(GTK_WIDGET(*view), GTK_EVENT_CONTROLLER(drop_target));
    g_signal_connect(drop_target, "drop", G_CALLBACK(sw_address_view_drop), *view);

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
create_from_entry(GtkWidget * grid, BalsaSendmsg * bsmsg)
{
    bsmsg->from[1] =
        libbalsa_identity_combo_box(balsa_app.identities, NULL,
                                    G_CALLBACK(sw_combo_box_changed), bsmsg);
    create_email_or_string_entry(bsmsg, grid, _("F_rom:"), 0, bsmsg->from);
}

static void
attachment_button_press_cb(GtkGestureClick *click,
                           int              n_press,
                           double           x,
                           double           y,
                           gpointer         user_data)
{
    GtkTreeView *tree_view = user_data;
    GdkEvent *event;
    GtkTreePath *path;
    int bx, by;

    event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(click));

    if (!gdk_event_triggers_context_menu(event))
        return;

    gtk_tree_view_convert_widget_to_bin_window_coords(tree_view, (int) x, (int) y,
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
	    if (attach_info != NULL) {
		if (attach_info->popup_menu != NULL)
                    libbalsa_popup_widget_popup(attach_info->popup_menu, event);
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
    if (attach_info != NULL) {
	if (attach_info->popup_menu != NULL)
            libbalsa_popup_widget_popup(attach_info->popup_menu, NULL);
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
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);

    gtk_widget_set_margin_top(grid, 6);
    gtk_widget_set_margin_bottom(grid, 6);
    gtk_widget_set_margin_start(grid, 6);
    gtk_widget_set_margin_end(grid, 6);

    /* From: */
    create_from_entry(grid, bsmsg);

    /* Create the 'Reply To:' entry before the regular recipients, to
     * get the initial focus in the regular recipients*/
#define REPLY_TO_ROW 3
    create_email_entry(bsmsg, grid, REPLY_TO_ROW, &bsmsg->replyto_view,
                       bsmsg->replyto, "R_eply To:", NULL, 0);

    /* To:, Cc:, and Bcc: */
    create_email_entry(bsmsg, grid, ++row, &bsmsg->recipient_view,
                       bsmsg->recipients, "Rec_ipients:", address_types,
                       G_N_ELEMENTS(address_types));
    gtk_widget_set_vexpand(bsmsg->recipients[1], TRUE);
    g_signal_connect_swapped(gtk_tree_view_get_model
                             (GTK_TREE_VIEW(bsmsg->recipient_view)),
                             "row-changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);
    g_signal_connect_swapped(gtk_tree_view_get_model
                             (GTK_TREE_VIEW(bsmsg->recipient_view)),
                             "row-deleted",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);

    /* Subject: */
    create_string_entry(bsmsg, grid, _("S_ubject:"), ++row,
                        bsmsg->subject);
    g_signal_connect_swapped(bsmsg->subject[1], "changed",
                             G_CALLBACK(sendmsg_window_set_title), bsmsg);

    /* Reply To: */
    /* We already created it, so just increment row: */
    g_assert(++row == REPLY_TO_ROW);
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
    bsmsg->fcc[1] =
        balsa_mblist_mru_option_menu(GTK_WINDOW(bsmsg->window),
                                     &balsa_app.fcc_mru);
    create_email_or_string_entry(bsmsg, grid, _("F_CC:"), ++row,
                                 bsmsg->fcc);

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
    GtkDropTarget *drop_target;
    GType drop_types[] = {
        BALSA_TYPE_INDEX,
        G_TYPE_STRV,
        G_TYPE_STRING
    };

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);

    gtk_widget_set_margin_top(grid, 6);
    gtk_widget_set_margin_bottom(grid, 6);
    gtk_widget_set_margin_start(grid, 6);
    gtk_widget_set_margin_end(grid, 6);

    /* Attachment list */
    label = gtk_label_new_with_mnemonic(_("_Attachments:"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);

    gtk_widget_set_margin_top(label, GNOME_PAD_SMALL);
    gtk_widget_set_margin_bottom(label, GNOME_PAD_SMALL);
    gtk_widget_set_margin_start(label, GNOME_PAD_SMALL);
    gtk_widget_set_margin_end(label, GNOME_PAD_SMALL);

    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);

    sw = gtk_scrolled_window_new();
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
						"icon-name", ATTACH_ICON_COLUMN,
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

    gesture = gtk_gesture_click_new();
    gtk_widget_add_controller(tree_view, GTK_EVENT_CONTROLLER(gesture));
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_signal_connect(gesture, "pressed",
                     G_CALLBACK(attachment_button_press_cb), view);

    /* Drag and drop */
    drop_target =
        gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
    gtk_drop_target_set_gtypes(drop_target, drop_types, G_N_ELEMENTS(drop_types));
    gtk_widget_add_controller(GTK_WIDGET(bsmsg->window), GTK_EVENT_CONTROLLER(drop_target));
    g_signal_connect(drop_target, "drop", G_CALLBACK(sw_attachment_drop), bsmsg);

    frame = gtk_frame_new(NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), tree_view);
    gtk_frame_set_child(GTK_FRAME(frame), sw);

    gtk_widget_set_hexpand(frame, TRUE);
    gtk_grid_attach(GTK_GRID(grid), frame, 1, 0, 1, 1);

    return grid;
}

typedef struct {
    const char * name;
    gboolean found;
} has_file_attached_t;

static gboolean
has_file_attached(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
		  gpointer data)
{
    has_file_attached_t *find_file = (has_file_attached_t *)data;
    BalsaAttachInfo *info;
    const char * uri;

    gtk_tree_model_get(model, iter, ATTACH_INFO_COLUMN, &info, -1);
    if (info == NULL)
	return FALSE;

    uri = libbalsa_vfs_get_uri(info->file_uri);
    if (g_strcmp0(find_file->name, uri) == 0)
	find_file->found = TRUE;
    g_object_unref(info);

    return find_file->found;
}

/* sw_quote_drop - text area D&D callback */
static gboolean
sw_quote_drop(GtkDropTarget *drop_target,
              GValue        *value,
              double         x,
              double         y,
              gpointer       user_data)
{
    BalsaSendmsg *bsmsg = user_data;
    gboolean drag_result = FALSE;

    if (G_VALUE_HOLDS(value, BALSA_TYPE_INDEX)) {
        BalsaIndex *bindex;
        LibBalsaMailbox *mailbox;
        GArray *selected;
        unsigned i;

	bindex = g_value_get_object(value);
	mailbox = balsa_index_get_mailbox(bindex);
        selected = balsa_index_selected_msgnos_new(bindex);

        for (i = 0; i < selected->len; i++) {
	    unsigned msgno = g_array_index(selected, unsigned, i);
	    LibBalsaMessage *message;

	    message = libbalsa_mailbox_get_message(mailbox, msgno);
            if (message == NULL)
                continue;

            sw_insert_message(bsmsg, message, QUOTE_ALL);
	    g_object_unref(message);
        }
        balsa_index_selected_msgnos_free(bindex, selected);
        drag_result = TRUE;
    } else if (G_VALUE_HOLDS(value, G_TYPE_STRV)) {
        const char **uri;

        for (uri = g_value_get_pointer(value); uri != NULL; ++uri) {
            /* Since current GtkTextView gets this signal twice for
             * every action (#150141) we need to check for duplicates,
             * which is a good idea anyway. */
	    has_file_attached_t find_file = {*uri, FALSE};

            if (bsmsg->tree_view)
                gtk_tree_model_foreach(BALSA_MSG_ATTACH_MODEL(bsmsg),
                                       has_file_attached, &find_file);
            if (!find_file.found) {
                add_attachment(bsmsg, *uri, FALSE, NULL);
                drag_result = TRUE;
            }
        }
    }
    /* case TARGET_EMAIL:
     * case TARGET_STRING: perhaps we should allow dropping in these, too? */

    return drag_result;
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
    GtkDropTarget *drop_target;
    GType drop_types[] = {
        BALSA_TYPE_INDEX,
        G_TYPE_STRV,
        G_TYPE_STRING
    };

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
        gtk_css_provider_load_from_data(css_provider, css, -1);
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

    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
    				   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), bsmsg->text);

    /* Drag and drop */
    drop_target =
        gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
    gtk_drop_target_set_gtypes(drop_target, drop_types, G_N_ELEMENTS(drop_types));
    gtk_widget_add_controller(GTK_WIDGET(bsmsg->text), GTK_EVENT_CONTROLLER(drop_target));
    g_signal_connect(drop_target, "drop", G_CALLBACK(sw_quote_drop), bsmsg);

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

        if ((fd = g_file_open_tmp("balsa-continue-XXXXXX", &name, NULL)) > 0) {
            GMimeStream * tmp_stream;

            if ((tmp_stream = g_mime_stream_fs_new(fd)) != NULL)
                res = libbalsa_message_body_save_stream(body, tmp_stream, FALSE, &err);
            else
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

/*
 * Quote message parts
 */

typedef struct {
    GMutex lock;
    GCond cond;

    GtkTreeStore               *tree_store;
    GtkWindow                  *parent;
    const reply_collect_stat_t *stats;

    gboolean ok;
    gboolean done;
} quote_parts_data;

static void
quote_parts_select_dlg_response(GtkDialog *dialog,
                                int        response_id,
                                gpointer   user_data)
{
    quote_parts_data *data = user_data;

    gtk_window_destroy(GTK_WINDOW(dialog));

    g_mutex_lock(&data->lock);
    data->ok = response_id == GTK_RESPONSE_OK;
    data->done = TRUE;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->lock);
}

static gboolean
quote_parts_select_dlg_idle(gpointer user_data)
{
    quote_parts_data *data = user_data;
    GtkTreeStore *tree_store = data->tree_store;
    GtkWindow *parent = data->parent;
    const reply_collect_stat_t *stats = data->stats;
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
    GtkWidget *content_box;

    dialog = gtk_dialog_new_with_buttons(_("Select parts for quotation"),
					 parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT |
                                         libbalsa_dialog_flags(),
					 _("_OK"), GTK_RESPONSE_OK,
					 _("_Cancel"), GTK_RESPONSE_CANCEL,
					 NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif
    geometry_manager_attach(GTK_WINDOW(dialog), "SelectReplyParts");

    label = libbalsa_create_wrap_label(_("Select the parts of the message"
                            " which shall be quoted in the reply"), FALSE);
    gtk_widget_set_valign(label, GTK_ALIGN_START);

    image = gtk_image_new_from_icon_name("dialog-question");
    gtk_widget_set_valign(image, GTK_ALIGN_START);

    /* stolen form gtk/gtkmessagedialog.c */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    gtk_box_append(GTK_BOX(vbox), label);
    gtk_box_append(GTK_BOX(hbox), image);

    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_halign(vbox, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(hbox), vbox);

    content_box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(content_box), hbox);

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
        gtk_box_append(GTK_BOX(vbox), warning);
    }

    gtk_widget_set_margin_top(dialog, 6);
    gtk_widget_set_margin_bottom(dialog, 6);
    gtk_widget_set_margin_start(dialog, 6);
    gtk_widget_set_margin_end(dialog, 6);

    gtk_widget_set_margin_top(hbox, 6);
    gtk_widget_set_margin_bottom(hbox, 6);
    gtk_widget_set_margin_start(hbox, 6);
    gtk_widget_set_margin_end(hbox, 6);

    gtk_box_set_spacing(GTK_BOX(content_box), 14);

    /* scrolled window for the tree view */
    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_valign(scroll, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(vbox), scroll);

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
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tree_view);

    g_signal_connect(dialog, "response", G_CALLBACK(quote_parts_select_dlg_response), data);
    gtk_widget_show(dialog);

    return G_SOURCE_REMOVE;
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
show_decrypted_warning_response(GtkDialog *dialog,
                                int        response_id,
                                gpointer   user_data)
{
    GtkWidget *remind_btn = user_data;

    balsa_app.warn_reply_decrypted = !gtk_check_button_get_active(GTK_CHECK_BUTTON(remind_btn));

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void
show_decrypted_warning(GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkWidget *message_area;
    GtkWidget *remind_btn;

    dialog =
        gtk_message_dialog_new(parent,
                               GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
                               GTK_MESSAGE_WARNING,
                               GTK_BUTTONS_CLOSE,
                               _("You reply to an encrypted message. The reply will contain "
                                 "the decrypted contents of the original message.\n"
                                 "Consider to encrypt the reply, and verify that you do not "
                                 "unintentionally leak sensitive information."));
    message_area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));

    remind_btn = gtk_check_button_new_with_label(_("Do not remind me again."));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(remind_btn), FALSE);
    gtk_box_append(GTK_BOX(message_area), remind_btn);

    g_signal_connect(dialog, "response", G_CALLBACK(show_decrypted_warning_response), remind_btn);
    gtk_widget_show(dialog);
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

    g_assert(libbalsa_am_i_subthread());

    if (root == NULL)
        return NULL;

    message = root->message;
    libbalsa_message_body_ref(message, FALSE, FALSE);

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
        quote_parts_data data;

        g_mutex_init(&data.lock);
        g_cond_init(&data.cond);

        data.tree_store = tree_store;
        data.parent = GTK_WINDOW(bsmsg->window);
        data.stats = &stats;

        g_mutex_lock(&data.lock);
        g_idle_add(quote_parts_select_dlg_idle, &data);
        data.done = FALSE;
        while (!data.done)
            g_cond_wait(&data.cond, &data.lock);
        g_mutex_unlock(&data.lock);

        g_mutex_clear(&data.lock);
        g_cond_clear(&data.cond);

	if (data.ok) {
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
quote_body(BalsaSendmsg           *bsmsg,
           LibBalsaMessageHeaders *headers,
           const char             *message_id,
           GList                  *references,
           LibBalsaMessageBody    *root,
           QuoteType               qtype)
{
    GString *body;
    char *date = NULL;
    char *personStr;
    const char *orig_address;

    g_assert(libbalsa_am_i_subthread());

    if (headers->from != NULL &&
	(orig_address = libbalsa_address_get_name_from_list(headers->from)) != NULL) {
        personStr = g_strdup(orig_address);
        libbalsa_utf8_sanitize(&personStr, balsa_app.convert_unknown_8bit, NULL);
    } else {
        personStr = g_strdup(_("you"));
    }

    if (headers->date != 0)
        date = libbalsa_message_headers_date_to_utf8(headers, balsa_app.date_string);

    if (qtype == QUOTE_HEADERS) {
        char *str;
	char *subject;

	str = g_strdup_printf(_("------forwarded message from %s------\n"), personStr);
	body = g_string_new(str);
	g_free(str);

	if (date != NULL)
	    g_string_append_printf(body, "%s %s\n", _("Date:"), date);

	subject = message_part_get_subject(root);
	if (subject != NULL)
	    g_string_append_printf(body, "%s %s\n", _("Subject:"), subject);
	g_free(subject);

	if (headers->from != NULL) {
	    char *from = internet_address_list_to_string(headers->from, NULL, FALSE);
	    g_string_append_printf(body, "%s %s\n", _("From:"), from);
	    g_free(from);
	}

	if (internet_address_list_length(headers->to_list) > 0) {
	    char *to_list = internet_address_list_to_string(headers->to_list, NULL, FALSE);
	    g_string_append_printf(body, "%s %s\n", _("To:"), to_list);
	    g_free(to_list);
	}

	if (internet_address_list_length(headers->cc_list) > 0) {
	    char *cc_list = internet_address_list_to_string(headers->cc_list, NULL, FALSE);
	    g_string_append_printf(body, "%s %s\n", _("CC:"), cc_list);
	    g_free(cc_list);
	}

	g_string_append_printf(body, _("Message-ID: %s\n"), message_id);

	if (references != NULL) {
	    GList *ref_list;

	    g_string_append(body, _("References:"));

	    for (ref_list = references; ref_list != NULL; ref_list = ref_list->next)
		g_string_append_printf(body, " <%s>", (char *) ref_list->data);

	    g_string_append_c(body, '\n');
	}
    } else {
        char *str;

	if (date != NULL)
	    str = g_strdup_printf(_("On %s, %s wrote:\n"), date, personStr);
	else
	    str = g_strdup_printf(_("%s wrote:\n"), personStr);

	/* scan the message and collect text parts which might be included
	 * in the reply */
	body = collect_for_quote(bsmsg, root,
				 qtype == QUOTE_ALL ? balsa_app.quote_str : NULL,
				 bsmsg->flow ? -1 : balsa_app.wraplength,
				 balsa_app.reply_strip_html, bsmsg->flow);
	if (body != NULL) {
	    char *buf;

	    buf = g_string_free(body, FALSE);
	    libbalsa_utf8_sanitize(&buf, balsa_app.convert_unknown_8bit, NULL);
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

static gpointer
fill_body_from_part_thread(gpointer data)
{
    BalsaSendmsg *bsmsg = data;
    GString *body;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
    GtkTextIter start;

    g_assert(bsmsg->headers != NULL);

    body = quote_body(bsmsg, bsmsg->headers, bsmsg->message_id, bsmsg->fill_references,
                      bsmsg->root, bsmsg->quote_type);
    g_free(bsmsg->message_id);

    if (body->len > 0 && body->str[body->len - 1] != '\n')
        g_string_append_c(body, '\n');
    gtk_text_buffer_insert_at_cursor(buffer, body->str, body->len);
    g_string_free(body, TRUE);

    if (bsmsg->quote_type == QUOTE_HEADERS)
        gtk_text_buffer_get_end_iter(buffer, &start);
    else
        gtk_text_buffer_get_start_iter(buffer, &start);

    gtk_text_buffer_place_cursor(buffer, &start);

    return NULL;
}

static void
fill_body_from_part(BalsaSendmsg           *bsmsg,
                    LibBalsaMessageHeaders *headers,
                    const char             *message_id,
                    GList                  *references,
                    LibBalsaMessageBody    *root,
                    QuoteType               qtype)
{
    bsmsg->headers = headers;
    bsmsg->message_id = g_strdup(message_id);
    bsmsg->fill_references = references;
    bsmsg->root = root;
    bsmsg->quote_type = qtype;

    g_thread_unref(g_thread_new("fill-body-from-part", fill_body_from_part_thread, bsmsg));
}

static GString*
quote_message_body(BalsaSendmsg * bsmsg,
                   LibBalsaMessage * message,
                   QuoteType qtype)
{
    GString *res;

    g_assert(libbalsa_am_i_subthread());

    if (libbalsa_message_body_ref(message, FALSE, FALSE)) {
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

    gtk_editable_set_text(GTK_EDITABLE(bsmsg->subject[1]), subject);
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

    gtk_combo_box_set_active(GTK_COMBO_BOX(bsmsg->from[1]),
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

static void
sw_set_language_change_state(GSimpleAction  * action,
                             GVariant       * state,
                             gpointer         data)
{
    BalsaSendmsg *bsmsg = data;
    const gchar *lang;

    lang = g_variant_get_string(state, NULL);
    set_locale(bsmsg, lang);
    g_free(balsa_app.spell_check_lang);
    balsa_app.spell_check_lang = g_strdup(lang);
#if HAVE_GSPELL || HAVE_GTKSPELL
    sw_action_set_active(bsmsg, "spell-check", TRUE);
#endif                          /* HAVE_GTKSPELL */

    g_simple_action_set_state(action, state);
}

static GMenu *
create_lang_menu(BalsaSendmsg *bsmsg, const char **active_lang)
{
    GSimpleActionGroup *simple;
    static const GActionEntry entries[] = {
        {"set", NULL, "s", "''", sw_set_language_change_state}
    };
    unsigned i;
    static gboolean locales_sorted = FALSE;
#if HAVE_GSPELL
    const GList *lang_list, *l;
#else
    GList *lang_list, *l;
#endif                          /* HAVE_GSPELL */
#if !HAVE_GSPELL && !HAVE_GTKSPELL
    EnchantBroker *broker;
#endif                          /* !HAVE_GSPELL && !HAVE_GTKSPELL */
    const char *preferred_lang;
    GMenu *menu;

    *active_lang = NULL;

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

    simple = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(simple),
                                    entries,
                                    G_N_ELEMENTS(entries),
                                    bsmsg);
    bsmsg->set_language_action =
        g_action_map_lookup_action(G_ACTION_MAP(simple), "set");
    gtk_widget_insert_action_group(bsmsg->window,
                                   "language",
                                   G_ACTION_GROUP(simple));
    g_object_unref(simple);

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

    menu = g_menu_new();
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
            GMenuItem *item;

            item = g_menu_item_new(locales[i].lang_name, NULL);
            g_menu_item_set_action_and_target(item, "language.set",
                                              "s", locales[i].locale);
            g_menu_append_item(menu, item);

            if (*active_lang == NULL ||
                strcmp(preferred_lang, locales[i].locale) == 0)
                *active_lang = locales[i].locale;
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
            GMenuItem *item;

            item = g_menu_item_new(lang, NULL);
            g_menu_item_set_action_and_target(item, "language.set", "s", lang);
            g_menu_append_item(menu, item);
            g_object_unref(item);

            if (*active_lang == NULL || strcmp(preferred_lang, lang) == 0)
                *active_lang = lang;
        }
    }
#if !HAVE_GSPELL
    g_list_free_full(lang_list, (GDestroyNotify) g_free);
#endif                          /* HAVE_GSPELL */

    g_action_change_state(bsmsg->set_language_action, g_variant_new_string(*active_lang));

    return menu;
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
    libbalsa_message_body_ref(message, TRUE, TRUE);
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
sw_attach_file(BalsaSendmsg * bsmsg, const char * val)
{
    GtkFileChooser *attach;
    GFile *file;

    if (!g_path_is_absolute(val)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not attach the file %s: %s."), val,
                                   _("not an absolute path"));
        return;
    }

    if (!g_file_test(val, G_FILE_TEST_EXISTS)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not attach the file %s: %s."), val,
                                   _("does not exist"));
        return;
    }
    if (!g_file_test(val, G_FILE_TEST_IS_REGULAR)) {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Could not attach the file %s: %s."), val,
                                   _("not a regular file"));
        return;
    }
    attach = g_object_get_data(G_OBJECT(bsmsg->window),
                               "balsa-sendmsg-window-attach-dialog");
    if (attach == NULL) {
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
            balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                       LIBBALSA_INFORMATION_WARNING,
                                       _("Could not attach the file %s: %s."), val,
                                       _("not in current directory"));
            return;
        }
    }

    file = g_file_new_for_path(val);
    gtk_file_chooser_set_file(attach, file, NULL);
    g_object_unref(file);
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
        append_comma_separated(GTK_EDITABLE(bsmsg->subject[1]), val);
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
#if HAVE_MACOSX_DESKTOP
            libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(bsmsg->window));
#endif
            g_object_set_data(G_OBJECT(bsmsg->window),
                              "balsa-sendmsg-window-url-bcc", dialog);
            g_signal_connect(dialog, "response",
                             G_CALLBACK(gtk_window_destroy), NULL);
            gtk_widget_show(dialog);
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

typedef struct {
    BalsaSendmsg *bsmsg;
    char         *string;
    size_t        len;
    char         *charset;
    char         *fname;
    char         *converted_string;
} insert_string_data;

static void
insert_string_data_free(insert_string_data *insert_data)
{
    g_object_unref(insert_data->bsmsg);
    g_free(insert_data->string);
    g_free(insert_data->charset);
    g_free(insert_data->fname);
    g_free(insert_data->converted_string);

    g_free(insert_data);
}

static gboolean
insert_string_idle(gpointer user_data)
{
    insert_string_data *insert_data = user_data;
    BalsaSendmsg  *bsmsg = insert_data->bsmsg;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));

    gtk_text_buffer_insert_at_cursor(buffer, insert_data->converted_string, -1);

    insert_string_data_free(insert_data);

    return G_SOURCE_REMOVE;
}

static gpointer
do_insert_string_thread(gpointer user_data)
{
    insert_string_data *insert_data = user_data;
    LibBalsaTextAttribute attr = libbalsa_text_attr_string(insert_data->string);
    user_codeset_data data;

    /* Make sure that len can be passed as gssize: */
    g_assert(insert_data->len <= G_MAXSIZE / 2);

    data.bsmsg = insert_data->bsmsg;
    data.change_type = NULL;
    data.content_type = NULL;
    data.filename = insert_data->fname;

    g_mutex_init(&data.lock);
    g_cond_init(&data.cond);

    do {
	LibBalsaCodesetInfo *info;
        const char *new_charset;

        g_debug("Trying charset: %s", insert_data->charset);

        insert_data->converted_string = NULL;
        if (sw_can_convert(insert_data->string, insert_data->len, "UTF-8", insert_data->charset,
                           &insert_data->converted_string)) {
            g_idle_add(insert_string_idle, insert_data);
            insert_data = NULL;
            break;
        }

        g_mutex_lock(&data.lock);
        data.codeset = (LibBalsaCodeset) (-1);
        data.done = FALSE;
        g_idle_add(sw_get_user_codeset, &data);
        while (!data.done)
            g_cond_wait(&data.cond, &data.lock);
        g_mutex_unlock(&data.lock);

        if (data.codeset == (LibBalsaCodeset) (-1))
            break;
        info = &libbalsa_codeset_info[data.codeset];

	new_charset = info->std;
        if (info->win != NULL && (attr & LIBBALSA_TEXT_HI_CTRL) != 0)
            new_charset = info->win;

        g_free(insert_data->charset);
        insert_data->charset = g_strdup(new_charset);
    } while (1);

    g_mutex_clear(&data.lock);
    g_cond_clear(&data.cond);

    if (insert_data != NULL)
        insert_string_data_free(insert_data);

    return NULL;
}

static void
insert_file_response(GtkDialog *selector,
                     int        response,
                     gpointer   user_data)
{
    BalsaSendmsg *bsmsg = user_data;
    GtkFileChooser *chooser;
    GFile *file;
    char *path;
    char *string;
    size_t len;
    GError *error = NULL;

    if (response != GTK_RESPONSE_OK) {
    	gtk_window_destroy(GTK_WINDOW(selector));
    	return;
    }

    chooser = GTK_FILE_CHOOSER(selector);
    file = gtk_file_chooser_get_file(chooser);
    path = g_file_get_path(file);
    g_object_unref(file);

    if (path == NULL) {
    	gtk_window_destroy(GTK_WINDOW(selector));
    	return;
    }

    if (g_file_get_contents(path, &string, &len, &error)) {
        LibBalsaTextAttribute attr;
        GtkTextBuffer *buffer;

        buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bsmsg->text));
        attr = libbalsa_text_attr_string(string);
        if (attr == 0 || (attr & LIBBALSA_TEXT_HI_UTF8) != 0) {
            /* Ascii or utf-8 */
            gtk_text_buffer_insert_at_cursor(buffer, string, -1);
        } else {
            /* Neither ascii nor utf-8... */
            const char *charset = sw_preferred_charset(bsmsg);
            insert_string_data *insert_data = g_new(insert_string_data, 1);

            insert_data->bsmsg   = g_object_ref(bsmsg);
            insert_data->string  = g_strdup(string);
            insert_data->len     = len;
            insert_data->charset = g_strdup(charset);
            insert_data->fname   = g_strdup(path);

            g_thread_unref(g_thread_new("insert-string", do_insert_string_thread, insert_data));
        }
        g_free(string);

        /* Use the same folder as for attachments. */
        g_free(balsa_app.attach_dir);
        file = gtk_file_chooser_get_current_folder(chooser);
        balsa_app.attach_dir = g_file_get_path(file);
        g_object_unref(file);
    } else {
        balsa_information_parented(GTK_WINDOW(bsmsg->window),
                                   LIBBALSA_INFORMATION_WARNING,
                                   _("Cannot read the file “%s”: %s"),
                                   path, error->message);
        g_error_free(error);
    }

    g_free(path);

    gtk_window_destroy(GTK_WINDOW(selector));
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
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(file_selector, GTK_WINDOW(bsmsg->window));
#endif
    gtk_window_set_destroy_with_parent(GTK_WINDOW(file_selector), TRUE);

    /* Use the same folder as for attachments. */
    if (balsa_app.attach_dir != NULL) {
        GFile *file;

        file = g_file_new_for_path(balsa_app.attach_dir);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(file_selector), file, NULL);
        g_object_unref(file);
    }

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

        gtk_text_buffer_get_iter_at_mark(buffer, &now,
                                         gtk_text_buffer_get_insert(buffer));
        pos = gtk_text_iter_get_offset(&now);

        the_text = gtk_text_iter_get_text(&start, &end);
        libbalsa_wrap_string(the_text, balsa_app.wraplength);
        gtk_text_buffer_set_text(buffer, "", 0);
        gtk_text_buffer_insert_at_cursor(buffer, the_text, -1);
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

    tmp = gtk_editable_get_chars(GTK_EDITABLE(bsmsg->subject[1]), 0, -1);
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
        g_strdup(balsa_mblist_mru_option_menu_get(bsmsg->fcc[1]));

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
        libbalsa_message_set_gpg_mode(message,
            (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) != 0 ? bsmsg->gpg_mode : 0);
        libbalsa_message_set_attach_pubkey(message, bsmsg->attach_pubkey);
        libbalsa_message_set_identity(message, ident);
    } else {
        libbalsa_message_set_gpg_mode(message, 0);
        libbalsa_message_set_attach_pubkey(message, FALSE);
    }

    /* remember the parent window */
    g_object_set_data(G_OBJECT(message), "parent-window",
		      GTK_WINDOW(bsmsg->window));

    return message;
}

/* ask the user for a subject */

typedef struct {
    BalsaSendmsg *bsmsg;
    GMutex lock;
    GCond cond;

    int choice;
    gboolean warn_mp;
    gboolean warn_html_sign;
    GtkWidget *subj_entry;
} send_message_data;

static void
send_message_thread_subject_response(GtkDialog *dialog,
                                     int        response_id,
                                     gpointer   user_data)
{
    send_message_data *data = user_data;

    /* always set the current string in the subject entry */
    gtk_editable_set_text(GTK_EDITABLE(data->bsmsg->subject[1]),
                          gtk_editable_get_text(GTK_EDITABLE(data->subj_entry)));

    g_mutex_lock(&data->lock);
    data->choice = response_id;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->lock);

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static gboolean
send_message_thread_subject_idle(gpointer user_data)
{
    send_message_data *data = user_data;
    BalsaSendmsg *bsmsg = data->bsmsg;
    GtkWidget *no_subj_dialog;
    GtkWidget *dialog_vbox;
    GtkWidget *hbox;
    GtkWidget *image;
    GtkWidget *vbox;
    char *text_str;
    GtkWidget *label;

    /* build the dialog */
    no_subj_dialog =
        gtk_dialog_new_with_buttons(_("No Subject"),
                                    GTK_WINDOW(bsmsg->window),
                                    GTK_DIALOG_MODAL |
                                    libbalsa_dialog_flags(),
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_Send"),   GTK_RESPONSE_OK,
                                    NULL);

    gtk_widget_set_margin_top(no_subj_dialog, 6);
    gtk_widget_set_margin_bottom(no_subj_dialog, 6);
    gtk_widget_set_margin_start(no_subj_dialog, 6);
    gtk_widget_set_margin_end(no_subj_dialog, 6);

    gtk_window_set_resizable(GTK_WINDOW(no_subj_dialog), FALSE);

    dialog_vbox = gtk_dialog_get_content_area(GTK_DIALOG(no_subj_dialog));

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(dialog_vbox), hbox);

    gtk_widget_set_margin_top(hbox, 6);
    gtk_widget_set_margin_bottom(hbox, 6);
    gtk_widget_set_margin_start(hbox, 6);
    gtk_widget_set_margin_end(hbox, 6);

    image = gtk_image_new_from_icon_name("dialog-question");
    gtk_box_append(GTK_BOX (hbox), image);
    gtk_widget_set_valign(image, GTK_ALIGN_START);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_halign(vbox, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(hbox), vbox);

    text_str = g_strdup_printf("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
			       _("You did not specify a subject for this message"),
			       _("If you would like to provide one, enter it below."));
    label = libbalsa_create_wrap_label(text_str, TRUE);
    g_free(text_str);
    gtk_box_append(GTK_BOX (vbox), label);
    gtk_widget_set_valign(label, GTK_ALIGN_START);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX (vbox), hbox);

    label = gtk_label_new (_("Subject:"));
    gtk_box_append(GTK_BOX (hbox), label);

    data->subj_entry = gtk_entry_new ();
    gtk_editable_set_text(GTK_EDITABLE(data->subj_entry), _("(no subject)"));
    gtk_widget_set_hexpand(data->subj_entry, TRUE);
    gtk_widget_set_halign(data->subj_entry, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(hbox), data->subj_entry);

    gtk_entry_set_activates_default(GTK_ENTRY(data->subj_entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG (no_subj_dialog),
                                    GTK_RESPONSE_OK);

    gtk_widget_grab_focus (data->subj_entry);
    gtk_editable_select_region(GTK_EDITABLE(data->subj_entry), 0, -1);

    g_signal_connect(no_subj_dialog, "response",
                     G_CALLBACK(send_message_thread_subject_response), data);
    gtk_widget_show(no_subj_dialog);

    return G_SOURCE_REMOVE;
}

/*
 * Encryption and autocrypt suggestions
 */

typedef struct {
    BalsaSendmsg *bsmsg;
    GMutex lock;
    GCond cond;

    int choice;
    const char *secondary_msg;
    int default_button;
} check_encrypt_data;

static void
config_dlg_button(GtkDialog *dialog, int response_id, const char *icon_id)
{
    GtkWidget *button;

    button = gtk_dialog_get_widget_for_response(dialog, response_id);
    if (button != NULL)
        gtk_button_set_icon_name(GTK_BUTTON(button), icon_id);
}

static void
run_check_encrypt_dialog_response(GtkDialog *dialog,
                                  int        response_id,
                                  gpointer   user_data)
{
    check_encrypt_data *data = user_data;

    if (response_id == GTK_RESPONSE_YES)
        bsmsg_setup_gpg_ui_by_mode(data->bsmsg, data->bsmsg->gpg_mode | LIBBALSA_PROTECT_ENCRYPT);

    g_mutex_lock(&data->lock);
    data->choice = response_id;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->lock);

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static gboolean
run_check_encrypt_dialog_idle(gpointer user_data)
{
    check_encrypt_data *data = user_data;
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new(GTK_WINDOW(data->bsmsg->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL |
                                    libbalsa_dialog_flags(), GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                    _("Message could be encrypted"));
    gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(dialog), "%s", data->secondary_msg);
    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                           _("Send _encrypted"), GTK_RESPONSE_YES,
                           _("Send _unencrypted"), GTK_RESPONSE_NO,
                           _("_Cancel"), GTK_RESPONSE_CANCEL, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), data->default_button);

    /* add button images */
    config_dlg_button(GTK_DIALOG(dialog), GTK_RESPONSE_YES,
                      balsa_icon_id(BALSA_PIXMAP_GPG_ENCRYPT));
    config_dlg_button(GTK_DIALOG(dialog), GTK_RESPONSE_NO, balsa_icon_id(BALSA_PIXMAP_SEND));

    g_signal_connect(dialog, "response", G_CALLBACK(run_check_encrypt_dialog_response), data);
    gtk_widget_show(dialog);

    return G_SOURCE_REMOVE;
}

static gboolean
run_check_encrypt_dialog(BalsaSendmsg *bsmsg, const char *secondary_msg, int default_button)
{
    check_encrypt_data data;
    gboolean result = TRUE;

    g_mutex_init(&data.lock);
    g_cond_init(&data.cond);

    data.bsmsg = bsmsg;
    data.secondary_msg = secondary_msg;
    data.default_button = default_button;

    g_mutex_lock(&data.lock);
    g_idle_add(run_check_encrypt_dialog_idle, &data);
    data.choice = 0;
    while (data.choice == 0)
        g_cond_wait(&data.cond, &data.lock);
    g_mutex_unlock(&data.lock);

    g_mutex_clear(&data.lock);
    g_cond_clear(&data.cond);

    if ((data.choice == GTK_RESPONSE_CANCEL) || (data.choice == GTK_RESPONSE_DELETE_EVENT))
        result = FALSE;

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

    g_assert(libbalsa_am_i_subthread());

    /* check if the user wants to see the message */
    if (bsmsg->ident == NULL ||
        !libbalsa_identity_get_warn_send_plain(bsmsg->ident))
	return TRUE;

    /* nothing to do if encryption is already enabled */
    if ((bsmsg->gpg_mode & LIBBALSA_PROTECT_ENCRYPT) != 0)
	return TRUE;

    /* we can not encrypt if we have bcc recipients */
    ia_list = libbalsa_address_view_get_list(bsmsg->recipient_view, "BCC:");
    len = internet_address_list_length(ia_list);
    g_object_unref(ia_list);
    if (len > 0)
        return TRUE;

    /* collect all to and cc recipients */
    protocol = bsmsg->gpg_mode & LIBBALSA_PROTECT_SMIMEV3 ?
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

	ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, NULL, NULL, error);
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

    g_assert(libbalsa_am_i_subthread());

    /* check if autocrypt is enabled, use the non-Autocrypt approach if not */
    if ((bsmsg->ident == NULL) ||
        (libbalsa_identity_get_autocrypt_mode(bsmsg->ident) == AUTOCRYPT_DISABLE)) {
        return check_suggest_encryption(bsmsg);
    }

    /* nothing to do if encryption is already enabled or if S/MIME mode is selected */
    if ((bsmsg->gpg_mode & (LIBBALSA_PROTECT_ENCRYPT | LIBBALSA_PROTECT_SMIMEV3)) != 0) {
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
             (libbalsa_message_get_protect_state(bsmsg->parent_message) == LIBBALSA_MSG_PROTECT_CRYPT))) {
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

    	if (result && ((bsmsg->gpg_mode & LIBBALSA_PROTECT_ENCRYPT) != 0)) {
    		/* make sure the message is also signed as required by the Autocrypt standard, and that a protocol is selected */
    		if ((bsmsg->gpg_mode & LIBBALSA_PROTECT_PROTOCOL) == 0) {
    			bsmsg_setup_gpg_ui_by_mode(bsmsg, bsmsg->gpg_mode | (LIBBALSA_PROTECT_RFC3156 + LIBBALSA_PROTECT_SIGN));
    		} else {
    			bsmsg_setup_gpg_ui_by_mode(bsmsg, bsmsg->gpg_mode | LIBBALSA_PROTECT_SIGN);
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
 *
 * Sending or queuing a message may require user interaction in the form
 * of a response to some information. The process is carried out in a
 * subthread, which can block pending the user's response, to avoid
 * blocking the main thread.
 */

static void
send_message_thread_response(GtkDialog *dialog,
                             int        response_id,
                             gpointer   user_data)
{
    send_message_data *data = user_data;

    g_mutex_lock(&data->lock);
    data->choice = response_id;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->lock);

    gtk_window_destroy(GTK_WINDOW(dialog));
}


static gboolean
send_message_thread_warn_idle(gpointer user_data)
{
    send_message_data *data = user_data;
    GtkWidget *dialog;
    GString *string =
        g_string_new(_("You selected OpenPGP security for this message.\n"));

    if (data->warn_html_sign)
        g_string_append(string,
                        _("The message text will be sent as plain text and as "
                          "HTML, but only the plain part can be signed.\n"));
    if (data->warn_mp)
        g_string_append(string,
                        _("The message contains attachments, which cannot be "
                          "signed or encrypted.\n"));
    g_string_append(string,
                    _("You should select MIME mode if the complete "
                      "message shall be protected. Do you really want to proceed?"));
    dialog =
        gtk_message_dialog_new(GTK_WINDOW(data->bsmsg->window),
                               GTK_DIALOG_DESTROY_WITH_PARENT |
                               GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_OK_CANCEL, "%s", string->str);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(data->bsmsg->window));
#endif
    g_string_free(string, TRUE);

    g_signal_connect(dialog, "response", G_CALLBACK(send_message_thread_response), NULL);
    gtk_widget_show(dialog);

    return G_SOURCE_REMOVE;
}

static gpointer
send_message_thread(gpointer data)
{
    BalsaSendmsg *bsmsg = data;
    send_message_data send_data;
    LibBalsaMsgCreateResult result;
    LibBalsaMessage *message;
    LibBalsaMailbox *fcc;
    GtkTreeIter iter;
    GError * error = NULL;

#ifdef ENABLE_AUTOCRYPT
    if (!check_autocrypt_recommendation(bsmsg)) {
        return NULL;
    }
#else
    if (!check_suggest_encryption(bsmsg)) {
        return NULL;
    }
#endif /* ENABLE_AUTOCRYPT */

    send_data.choice = GTK_RESPONSE_OK;

    g_mutex_init(&send_data.lock);
    g_cond_init(&send_data.cond);

    send_data.bsmsg = bsmsg;

    if (!bsmsg->has_subject) {
        g_mutex_lock(&send_data.lock);
        g_idle_add(send_message_thread_subject_idle, &send_data);
        send_data.choice = 0;
        while (send_data.choice == 0)
            g_cond_wait(&send_data.cond, &send_data.lock);
        g_mutex_unlock(&send_data.lock);
    }

    if (send_data.choice == GTK_RESPONSE_OK &&
        (bsmsg->gpg_mode & LIBBALSA_PROTECT_OPENPGP) != 0) {
        gboolean warn_mp;
        gboolean warn_html_sign;

        warn_mp = (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) != 0 &&
            bsmsg->tree_view &&
            gtk_tree_model_get_iter_first(BALSA_MSG_ATTACH_MODEL(bsmsg), &iter);
        warn_html_sign = (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) == LIBBALSA_PROTECT_SIGN &&
            bsmsg->send_mp_alt;

        if (warn_mp || warn_html_sign) {
            /* we are going to RFC2440 sign/encrypt a multipart, or to
             * RFC2440 sign a multipart/alternative... */
            send_data.warn_mp = warn_mp;
            send_data.warn_html_sign = warn_html_sign;

            g_mutex_lock(&send_data.lock);
            g_idle_add(send_message_thread_warn_idle, &send_data);
            send_data.choice = 0;
            while (send_data.choice == 0)
                g_cond_wait(&send_data.cond, &send_data.lock);
            g_mutex_unlock(&send_data.lock);
        }
    }

    g_mutex_clear(&send_data.lock);
    g_cond_clear(&send_data.cond);

    if (send_data.choice != GTK_RESPONSE_OK)
        return NULL;

    message = bsmsg2message(bsmsg);
    fcc = balsa_find_mailbox_by_url(bsmsg->fcc_url);

    balsa_information_parented(GTK_WINDOW(bsmsg->window),
                               LIBBALSA_INFORMATION_DEBUG,
                               _("sending message with GPG mode %d"),
                               libbalsa_message_get_gpg_mode(message));

    if(bsmsg->queue_only)
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
	return NULL;
    }

    gtk_window_destroy(GTK_WINDOW(bsmsg->window));

    return NULL;
}

static void
send_message_handler(BalsaSendmsg * bsmsg)
{
    const char *subj;

    if (!bsmsg->ready_to_send)
	return;

    /* read the subject widget and verify that it is contains something else
       than spaces */
    subj = gtk_editable_get_text(GTK_EDITABLE(bsmsg->subject[1]));
    bsmsg->has_subject = FALSE;
    if (subj != NULL) {
	const gchar *p = subj;
	const gchar *end = subj + strlen(subj);

	while (p < end && g_unichar_isspace(g_utf8_get_char(p)))
	    p = g_utf8_next_char(p);
	if (p < end)
	    bsmsg->has_subject = TRUE;
    }

    g_thread_unref(g_thread_new("send-message", send_message_thread, bsmsg));
}


/* "send message" menu callback */
static void
sw_toolbar_send_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    bsmsg->queue_only = balsa_app.always_queue_sent_mail;
    send_message_handler(bsmsg);
}

static void
sw_send_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    bsmsg->queue_only = FALSE;
    send_message_handler(bsmsg);
}


static void
sw_queue_activated(GSimpleAction * action, GVariant * parameter, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    bsmsg->queue_only = TRUE;
    send_message_handler(bsmsg);
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
    g_ptr_array_add(headers, g_strdup_printf("%d", bsmsg->gpg_mode));
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

    if ((bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL ||
        bsmsg->type == SEND_REPLY_GROUP))
	successp = libbalsa_message_postpone(message, balsa_app.draftbox,
                                             bsmsg->parent_message,
                                             (gchar **) headers->pdata,
                                             bsmsg->flow, &error);
    else
	successp = libbalsa_message_postpone(message, balsa_app.draftbox,
                                             NULL,
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
            gtk_window_destroy(GTK_WINDOW(bsmsg->window));
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

    g_action_change_state(bsmsg->set_language_action, g_variant_new_string(new_lang));

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
sw_entry_helper(GSimpleAction      * action,
                GVariant     * state,
                BalsaSendmsg * bsmsg,
                GtkWidget    * entry[])
{
    if (g_variant_get_boolean(state)) {
        gtk_widget_show(entry[0]);
        gtk_widget_show(entry[1]);
        gtk_widget_grab_focus(entry[1]);
    } else {
        gtk_widget_hide(entry[0]);
        gtk_widget_hide(entry[1]);
    }

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

    sw_entry_helper(action, state, bsmsg, bsmsg->from);
}

static void
sw_recips_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    sw_entry_helper(action, state, bsmsg, bsmsg->recipients);
}

static void
sw_reply_to_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    sw_entry_helper(action, state, bsmsg, bsmsg->replyto);
}

static void
sw_fcc_change_state(GSimpleAction * action, GVariant * state, gpointer data)
{
    BalsaSendmsg *bsmsg = data;

    sw_entry_helper(action, state, bsmsg, bsmsg->fcc);
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
        bsmsg->gpg_mode |= mask;
    else
        bsmsg->gpg_mode &= ~mask;

    radio_on = (bsmsg->gpg_mode & LIBBALSA_PROTECT_MODE) > 0;
    sw_action_set_enabled(bsmsg, "gpg-mode", radio_on);

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
        rfc_flag = LIBBALSA_PROTECT_SMIMEV3;
    else {
        g_warning("%s unknown mode “%s”", __func__, mode);
        return;
    }

    bsmsg->gpg_mode =
        (bsmsg->gpg_mode & ~LIBBALSA_PROTECT_PROTOCOL) | rfc_flag;

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
        if (bsmsg->text != NULL && language != NULL) {
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

    balsa_app.spell_check_active = g_variant_get_boolean(state);

    if (bsmsg->text != NULL) {
        GtkTextView *text_view;
        GspellTextView *gspell_view;

        text_view = GTK_TEXT_VIEW(bsmsg->text);
        gspell_view = gspell_text_view_get_from_gtk_text_view(text_view);
        gspell_text_view_set_inline_spell_checking(gspell_view,
                                                   balsa_app.spell_check_active);
    }
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

    if (bsmsg->spell_checker != NULL) {
        if (gtk_widget_get_realized(bsmsg->spell_checker)) {
            gtk_window_present(GTK_WINDOW(bsmsg->spell_checker));
            return;
        } else {
            /* A spell checker was created, but not shown because of
             * errors; we'll destroy it, and create a new one. */
            gtk_window_destroy(GTK_WINDOW(bsmsg->spell_checker));
        }
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

    for (i = 1; i < selected->len; i++) {
	msgno = g_array_index(selected, guint, i);
        message = libbalsa_mailbox_get_message(mailbox, msgno);
        if (!message)
            continue;

        if (type == SEND_FORWARD_ATTACH)
            attach_message(bsmsg, message);
        else if (type == SEND_FORWARD_INLINE)
            sw_insert_message(bsmsg, message, QUOTE_NOPREFIX);

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
                            gtk_editable_get_text(GTK_EDITABLE(bsmsg->subject[1])));
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
    bsmsg->gpg_mode = 0;
    if (libbalsa_identity_get_always_trust(ident))
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_ALWAYS_TRUST;

    sw_action_set_active(bsmsg, "sign", libbalsa_identity_get_gpg_sign(ident));
    if (libbalsa_identity_get_gpg_sign(ident))
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_SIGN;

    sw_action_set_active(bsmsg, "encrypt", libbalsa_identity_get_gpg_encrypt(ident));
    if (libbalsa_identity_get_gpg_encrypt(ident))
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_ENCRYPT;

    switch (libbalsa_identity_get_crypt_protocol(ident)) {
    case LIBBALSA_PROTECT_OPENPGP:
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_OPENPGP;
        g_action_change_state(action, g_variant_new_string("open-pgp"));
        break;
    case LIBBALSA_PROTECT_SMIMEV3:
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_SMIMEV3;
        g_action_change_state(action, g_variant_new_string("smime"));
        break;
    case LIBBALSA_PROTECT_RFC3156:
    default:
        bsmsg->gpg_mode |= LIBBALSA_PROTECT_RFC3156;
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
bsmsg_setup_gpg_ui_by_mode(BalsaSendmsg *bsmsg, gint mode)
{
    GAction *action;

    /* do nothing if we don't support crypto */
    if (!balsa_app.has_openpgp && !balsa_app.has_smime)
	return;

    bsmsg->gpg_mode = mode;
    sw_action_set_active(bsmsg, "sign", mode & LIBBALSA_PROTECT_SIGN);
    sw_action_set_active(bsmsg, "encrypt", mode & LIBBALSA_PROTECT_ENCRYPT);

    action = sw_get_action(bsmsg, "gpg-mode");
    if (mode & LIBBALSA_PROTECT_SMIMEV3)
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
set_language_menu(GtkWidget *menubar, GMenu *language_menu)
{
    GMenuModel *model;
    int i;
    int n;
    int lang_position = -1;
    GMenuItem *item;

    model = gtk_popover_menu_bar_get_menu_model(GTK_POPOVER_MENU_BAR(menubar));
    n = g_menu_model_get_n_items(model);
    for (i = 0; i < n; i++) {
        const char *label;

        if (g_menu_model_get_item_attribute(model, i, "label", "s", &label) &&
            strcmp(label, "_Language") == 0) {
            lang_position = i;
            break;
        }
    }

    g_return_if_fail(lang_position >= 0);

    item = g_menu_item_new_from_model(model, lang_position);

    g_menu_item_set_submenu(item, G_MENU_MODEL(language_menu));

    /* Replace the existing submenu */
    g_menu_remove(G_MENU(model), lang_position);
    g_menu_insert_item(G_MENU(model), lang_position, item);
    g_object_unref(item);
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
    GMenu *language_menu;
    const gchar *current_locale;

    bsmsg = g_new0(BalsaSendmsg, 1);

    bsmsg->ident = g_object_ref(balsa_app.current_ident);

    bsmsg->window = window =
        gtk_application_window_new(balsa_app.application);
    geometry_manager_attach(GTK_WINDOW(window), "SendMsgWindow");

    gtk_window_set_child(GTK_WINDOW(window), main_box);
    gtk_widget_show(window);

    bsmsg->gpg_mode = LIBBALSA_PROTECT_RFC3156;
    bsmsg->autosave_timeout_id = /* autosave every 5 minutes */
        g_timeout_add_seconds(60*5, (GSourceFunc)sw_autosave_timeout_cb, bsmsg);

    g_signal_connect(window, "close-request",
		     G_CALLBACK(sw_close_request_cb), bsmsg);
    g_signal_connect(window, "destroy",
		     G_CALLBACK(destroy_event_cb), bsmsg);
    /* If any compose windows are open when Balsa is closed, we want
     * them also to be closed. */
    g_object_weak_ref(G_OBJECT(balsa_app.main_window),
                      (GWeakNotify) gtk_window_destroy, window);

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

#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu(window, GTK_MENU_SHELL(menubar));
#else
    gtk_box_append(GTK_BOX(main_box), menubar);
#endif

    /*
     * Set up the spell-checker language menu
     */
    language_menu = create_lang_menu(bsmsg, &current_locale);
    set_language_menu(menubar, language_menu);
    g_object_unref(language_menu);

    if (current_locale == NULL)
        sw_action_set_enabled(bsmsg, "spell-check", FALSE);

    model = sendmsg_window_get_toolbar_model();
    bsmsg->toolbar = balsa_toolbar_new(model, G_ACTION_MAP(window));
    gtk_box_append(GTK_BOX(main_box), bsmsg->toolbar);

    bsmsg->flow = !balsa_app.wordwrap;
    sw_action_set_enabled(bsmsg, "reflow", bsmsg->flow);

    sw_action_set_enabled(bsmsg, "select-ident",
                     balsa_app.identities->next != NULL);
    bsmsg->identities_changed_id =
        g_signal_connect_swapped(balsa_app.main_window, "identities-changed",
                                 (GCallback)bsmsg_identities_changed_cb,
                                 bsmsg);
#if !HAVE_GTKSOURCEVIEW
    sw_buffer_set_undo(bsmsg, TRUE, FALSE);
#endif                          /* HAVE_GTKSOURCEVIEW */

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
    gtk_box_append(GTK_BOX(main_box), paned);

    /* create the top portion with the to, from, etc in it */
    gtk_paned_set_start_child(GTK_PANED(paned), create_info_pane(bsmsg));

    /* create text area for the message */
    gtk_paned_set_end_child(GTK_PANED(paned), create_text_area(bsmsg));

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
	g_signal_connect(balsa_app.main_window, "close-request",
			 G_CALLBACK(sw_close_request_cb), bsmsg);

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
    if (message_id != NULL)
        set_in_reply_to(bsmsg, message_id, headers);
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

    g_assert(libbalsa_am_i_subthread());

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

    if (part->embhdrs != NULL) {
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
    LibBalsaMessage *message = libbalsa_mailbox_get_message(mailbox, msgno);
    BalsaSendmsg *bsmsg;
    const gchar *postpone_hdr;
    GList *list, *refs = NULL;
    GList *in_reply_to;

    bsmsg = g_object_get_data(G_OBJECT(message), BALSA_SENDMSG_WINDOW_KEY);
    if (bsmsg != NULL) {
        gtk_window_present(GTK_WINDOW(bsmsg->window));
        return NULL;
    }

    bsmsg = sendmsg_window_new();
    bsmsg->is_continue = TRUE;
    bsm_prepare_for_setup(message);
    bsmsg->draft_message = message;
    g_object_set_data(G_OBJECT(bsmsg->draft_message), BALSA_SENDMSG_WINDOW_KEY, bsmsg);
    set_identity(bsmsg, message);
    setup_headers_from_message(bsmsg, message);

    libbalsa_address_view_set_from_list(bsmsg->replyto_view,
                                        "Reply To:",
                                        libbalsa_message_get_headers(message)->reply_to);
    in_reply_to = libbalsa_message_get_in_reply_to(message);
    if (in_reply_to != NULL)
        bsmsg->in_reply_to = g_strconcat("<", in_reply_to->data, ">", NULL);

    if ((postpone_hdr = libbalsa_message_get_user_header(message, "X-Balsa-Crypto")) != NULL)
        bsmsg_setup_gpg_ui_by_mode(bsmsg, atoi(postpone_hdr));
    postpone_hdr = libbalsa_message_get_user_header(message, "X-Balsa-Att-Pubkey");
    if (postpone_hdr != NULL) {
    	sw_action_set_active(bsmsg, "attpubkey", atoi(postpone_hdr) != 0);
    }
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-MDN")) != NULL)
        sw_action_set_active(bsmsg, "request-mdn", atoi(postpone_hdr) != 0);
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-DSN")) != NULL)
        sw_action_set_active(bsmsg, "request-dsn", atoi(postpone_hdr) != 0);
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Lang")) != NULL) {
        g_action_change_state(bsmsg->set_language_action, g_variant_new_string(postpone_hdr));
    }
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Format")) != NULL)
        sw_action_set_active(bsmsg, "flowed", strcmp(postpone_hdr, "Fixed"));
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-MP-Alt")) != NULL)
        sw_action_set_active(bsmsg, "send-html", !strcmp(postpone_hdr, "yes"));
    if ((postpone_hdr =
         libbalsa_message_get_user_header(message, "X-Balsa-Send-Type")) != NULL)
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
