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

/* MAKE SURE YOU USE THE HELPER FUNCTIONS, like create_table(, page), etc. */
#include "config.h"

#include <gnome.h>
#include "balsa-app.h"
#include "pref-manager.h"
#include "mailbox-conf.h"
#include "folder-conf.h"
#include "main-window.h"
#include "save-restore.h"
#include "spell-check.h"
#include "address-book-config.h"
#include "quote-color.h"
#include "misc.h"
#include "imap-server.h"

#if ENABLE_ESMTP
#include <libesmtp.h>
#include <string.h>
#include "smtp-server.h"
#include "libbalsa-conf.h"
#endif                          /* ENABLE_ESMTP */

#include <glib/gi18n.h>

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

#define NUM_ENCODING_MODES 3
#define NUM_PWINDOW_MODES 3
#define NUM_THREADING_STYLES 3
#define NUM_CONVERT_8BIT_MODES 2

/* Spacing suggestions from
 * http://developer.gnome.org/projects/gup/hig/1.0/layout.html#window-layout-spacing
 */
#ifdef ENABLE_TOUCH_UI
#define HIG_PADDING     3
#else  /* ENABLE_TOUCH_UI */
#define HIG_PADDING     6
#endif /* ENABLE_TOUCH_UI */
#define BORDER_WIDTH    (2 * HIG_PADDING)
#define GROUP_SPACING   (3 * HIG_PADDING)
#define HEADER_SPACING  (2 * HIG_PADDING)
#define ROW_SPACING     (1 * HIG_PADDING)
#define COL_SPACING     (1 * HIG_PADDING)

#define BALSA_PAGE_SIZE_GROUP_KEY  "balsa-page-size-group"
#define BALSA_TABLE_PAGE_KEY  "balsa-table-page"
typedef struct _PropertyUI {
    /* The page index: */
    GtkWidget *view;

    GtkWidget *address_books;

    GtkWidget *mail_servers;
#if ENABLE_ESMTP
    GtkWidget *smtp_servers;
    GtkWidget *smtp_server_edit_button;
    GtkWidget *smtp_server_del_button;
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    GtkWidget *smtp_certificate_passphrase;
#endif
#endif                          /* ENABLE_ESMTP */
    GtkWidget *mail_directory;
    GtkWidget *encoding_menu;
    GtkWidget *check_mail_auto;
    GtkWidget *check_mail_minutes;
    GtkWidget *quiet_background_check;
    GtkWidget *msg_size_limit;
    GtkWidget *check_imap;
    GtkWidget *check_imap_inbox;
    GtkWidget *notify_new_mail_dialog;
    GtkWidget *notify_new_mail_sound;
#if GTK_CHECK_VERSION(2, 10, 0)
    GtkWidget *notify_new_mail_icon;
#endif                          /* GTK_CHECK_VERSION(2, 10, 0) */
    GtkWidget *mdn_reply_clean_menu, *mdn_reply_notclean_menu;

    GtkWidget *close_mailbox_auto;
    GtkWidget *close_mailbox_minutes;
    GtkWidget *hide_deleted;
    gint filter;
    GtkWidget *expunge_on_close;
    GtkWidget *expunge_auto;
    GtkWidget *expunge_minutes;
    GtkWidget *action_after_move_menu;

    GtkWidget *previewpane;
    GtkWidget *alternative_layout;
    GtkWidget *view_message_on_open;
    GtkWidget *pgdownmod;
    GtkWidget *pgdown_percent;
    GtkWidget *view_allheaders;
    GtkWidget *debug;           /* enable/disable debugging */
    GtkWidget *empty_trash;
    GtkRadioButton *pwindow_type[NUM_PWINDOW_MODES];
    GtkWidget *wordwrap;
    GtkWidget *wraplength;
    GtkWidget *open_inbox_upon_startup;
    GtkWidget *check_mail_upon_startup;
    GtkWidget *remember_open_mboxes;
    GtkWidget *mblist_show_mb_content_info;
    GtkWidget *always_queue_sent_mail;
    GtkWidget *copy_to_sentbox;
    GtkWidget *autoquote;
    GtkWidget *reply_include_html_parts;
    GtkWidget *forward_attached;

    /* Information messages */
    GtkWidget *information_message_menu;
    GtkWidget *warning_message_menu;
    GtkWidget *error_message_menu;
    GtkWidget *debug_message_menu;
    GtkWidget *fatal_message_menu;

    /* External editor preferences */
    GtkWidget *edit_headers;

    /* arp */
    GtkWidget *quote_str;

    GtkWidget *message_font_button;     /* font used to display messages */
    GtkWidget *subject_font_button;     /* font used to display messages */

    GtkWidget *date_format;

    GtkWidget *selected_headers;

    /* colours */
    GtkWidget *quoted_color[MAX_QUOTED_COLOR];
    GtkWidget *url_color;
    GtkWidget *bad_address_color;

    /* sorting and threading prefs */
    GtkWidget *tree_expand_check;
    GtkWidget *default_sort_field;
    gint sort_field_index;
    GtkWidget *default_threading_type;
    gint threading_type_index;

    /* quote regex */
    GtkWidget *quote_pattern;

    /* wrap incoming text/plain */
    GtkWidget *browse_wrap;
    GtkWidget *browse_wrap_length;

    /* how to display multipart/alternative */
    GtkWidget *display_alt_plain;

    /* how to handle broken mails with 8-bit chars */
    GtkRadioButton *convert_unknown_8bit[NUM_CONVERT_8BIT_MODES];
    GtkWidget *convert_unknown_8bit_codeset;

#if !HAVE_GTKSPELL
    /* spell checking */
    GtkWidget *module;
    gint module_index;
    GtkWidget *suggestion_mode;
    gint suggestion_mode_index;
    GtkWidget *ignore_length;
    GtkWidget *spell_check_sig;
    GtkWidget *spell_check_quoted;
#endif                          /* HAVE_GTKSPELL */

    /* folder scanning */
    GtkWidget *local_scan_depth;
    GtkWidget *imap_scan_depth;

} PropertyUI;


static PropertyUI *pui = NULL;
static GtkWidget *property_box;
static gboolean already_open;

    /* Mail Options page */
static GtkWidget *create_mail_options_page(GtkTreeStore * store);

static GtkWidget *mailserver_subpage(void);

static GtkWidget *remote_mailbox_servers_group(GtkWidget * page);
static GtkWidget *local_mail_group(GtkWidget * page);
#if ENABLE_ESMTP
static GtkWidget *outgoing_mail_group(GtkWidget * page);
#endif                          /* ENABLE_ESMTP */

static GtkWidget *incoming_subpage(void);

static GtkWidget *checking_group(GtkWidget * page);
static GtkWidget *mdn_group(GtkWidget * page);

static GtkWidget *outgoing_subpage(void);

static GtkWidget *word_wrap_group(GtkWidget * page);
static GtkWidget *other_options_group(GtkWidget * page);
    /* End of Mail Options page */

    /* Display Options page */
static GtkWidget *create_display_page(GtkTreeStore * store);

static GtkWidget *display_subpage(void);

static GtkWidget *main_window_group(GtkWidget * page);
static GtkWidget *message_window_group(GtkWidget * page);

static GtkWidget *threading_subpage(void);

static GtkWidget *threading_group(GtkWidget * page);

static GtkWidget *message_subpage(void);

static GtkWidget *preview_font_group(GtkWidget * page);
static GtkWidget *quoted_group(GtkWidget * page);
static GtkWidget *alternative_group(GtkWidget * page);

static GtkWidget *colors_subpage(void);

static GtkWidget *message_colors_group(GtkWidget * page);
static GtkWidget *link_color_group(GtkWidget * page);
static GtkWidget *composition_window_group(GtkWidget * page);

static GtkWidget *format_subpage(void);

static GtkWidget *display_formats_group(GtkWidget * page);
static GtkWidget *broken_8bit_codeset_group(GtkWidget * page);

static GtkWidget *status_messages_subpage(void);

static GtkWidget *information_messages_group(GtkWidget * page);
static GtkWidget *progress_group(GtkWidget * page);
    /* End of Display Options page */

    /* Address Books page */
static GtkWidget *create_address_book_page(GtkTreeStore * store);

static GtkWidget *address_books_group(GtkWidget * page);
    /* End of Address Books page */

    /* Startup page */
static GtkWidget *create_startup_page(GtkTreeStore * store);

static GtkWidget *options_group(GtkWidget * page);
static GtkWidget *folder_scanning_group(GtkWidget * page);
    /* End of Startup page */

    /* Misc page */
static GtkWidget *create_misc_page(GtkTreeStore * store);

static GtkWidget *misc_group(GtkWidget * page);
static GtkWidget *deleting_messages_group(GtkWidget * page);
    /* End of Misc page */

#if !HAVE_GTKSPELL
    /* Spelling page */
static GtkWidget *create_spelling_page(GtkTreeStore * store);
static GtkWidget *pspell_settings_group(GtkWidget * page);
static GtkWidget *misc_spelling_group(GtkWidget * page);
#endif                          /* HAVE_GTKSPELL */

    /* general helpers */
static GtkWidget *create_table(gint rows, gint cols, GtkWidget * page);
static GtkWidget *add_pref_menu(const gchar * label, const gchar * names[],
                                gint size, gint * index, GtkBox * parent,
                                gint padding, GtkWidget * page);
static void add_show_menu(const char *label, gint level, GtkWidget * menu);
#if !HAVE_GTKSPELL
static GtkWidget *attach_pref_menu(const gchar * label, gint row,
                                   GtkTable * table, const gchar * names[],
                                   gint size, gint * index);
#endif                          /* HAVE_GTKSPELL */
static GtkWidget *attach_entry(const gchar * label, gint row,
                               GtkWidget * table);
static GtkWidget *attach_entry_full(const gchar * label, gint row,
                                    GtkWidget * table, gint col_left,
                                    gint col_middle, gint col_right);
static GtkWidget *create_pref_option_menu(const gchar * names[], gint size,
                                          gint * index);

    /* page and group object methods */
static GtkWidget *pm_page_new(void);
static void pm_page_add(GtkWidget * page, GtkWidget * child,
                        gboolean expand);
static GtkSizeGroup *pm_page_get_size_group(GtkWidget * page);
static void pm_page_add_to_size_group(GtkWidget * page, GtkWidget * child);
static GtkWidget *pm_group_new(const gchar * text);
static void pm_group_add(GtkWidget * group, GtkWidget * child,
                         gboolean expand);
static GtkWidget *pm_group_get_vbox(GtkWidget * group);
static GtkWidget *pm_group_add_check(GtkWidget * group,
                                     const gchar * text);
static void pm_append_page(GtkWidget * notebook, GtkWidget * widget,
                           const gchar * text, GtkTreeStore * store,
                           GtkTreeIter * parent_iter);

    /* combo boxes */
struct pm_combo_box_info {
    GSList *levels;
};
#define PM_COMBO_BOX_INFO "balsa-pref-manager-combo-box-info"

static GtkWidget *pm_combo_box_new(void);
static void pm_combo_box_set_level(GtkWidget * combo_box, gint level);
static gint pm_combo_box_get_level(GtkWidget * combo_box);

    /* special helpers */
static GtkWidget *create_action_after_move_menu(void);
static GtkWidget *create_information_message_menu(void);
static GtkWidget *create_mdn_reply_menu(void);
static void balsa_help_pbox_display(void);

    /* updaters */
static void set_prefs(void);
static void apply_prefs(GtkDialog * dialog);
void update_mail_servers(void); /* public; in pref-manager.h */
#if ENABLE_ESMTP
static void smtp_server_update(LibBalsaSmtpServer *, GtkResponseType,
		               const gchar *);
static void update_smtp_servers(void);
#endif                          /* ENABLE_ESMTP */

    /* callbacks */
static void response_cb(GtkDialog * dialog, gint response, gpointer data);
static void destroy_pref_window_cb(void);
static void update_address_books(void);
static void properties_modified_cb(GtkWidget * widget, GtkWidget * pbox);

static void server_edit_cb(GtkTreeView * tree_view);
static void pop3_add_cb(void);
static void server_add_cb(void);
static void server_del_cb(GtkTreeView * tree_view);

#if ENABLE_ESMTP
static void smtp_server_edit_cb(GtkTreeView * tree_view);
static void smtp_server_add_cb(void);
static void smtp_server_del_cb(GtkTreeView * tree_view);
static void smtp_server_changed (GtkTreeSelection * selection,
				 gpointer user_data);
#endif                          /* ENABLE_ESMTP */

static void address_book_edit_cb(GtkTreeView * tree_view);
static void address_book_add_cb(void);
static void address_book_delete_cb(GtkTreeView * tree_view);
static void address_book_set_default_cb(GtkTreeView * tree_view);
static void timer_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void mailbox_close_timer_modified_cb(GtkWidget * widget,
                                            GtkWidget * pbox);
static void browse_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void wrap_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void pgdown_modified_cb(GtkWidget * widget, GtkWidget * pbox);

static void option_menu_cb(GtkItem * menuitem, gpointer data);
static void imap_toggled_cb(GtkWidget * widget, GtkWidget * pbox);

static void convert_8bit_cb(GtkWidget * widget, GtkWidget * pbox);

static void filter_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void expunge_on_close_cb(GtkWidget * widget, GtkWidget * pbox);
static void expunge_auto_cb(GtkWidget * widget, GtkWidget * pbox);

guint pwindow_type[NUM_PWINDOW_MODES] = {
    WHILERETR,
    UNTILCLOSED,
    NEVER
};

gchar *pwindow_type_label[NUM_PWINDOW_MODES] = {
    N_("While Retrieving Messages"),
    N_("Until Closed"),
    N_("Never")
};

#if !HAVE_GTKSPELL
const gchar *spell_check_suggest_mode_label[NUM_SUGGEST_MODES] = {
    N_("Fast"),
    N_("Normal"),
    N_("Bad Spellers")
};
#endif                          /* HAVE_GTKSPELL */

    /* These labels must match the LibBalsaMailboxSortFields enum. */
const gchar *sort_field_label[] = {
    N_("Message number"),
    N_("Subject"),
    N_("Date"),
    N_("Size"),
    N_("Sender")
};

const gchar *threading_type_label[NUM_THREADING_STYLES] = {
    N_("Flat"),
    N_("Simple"),
    N_("JWZ")
};

    /* and now the important stuff: */
#if GTK_CHECK_VERSION(2, 6, 0)
static gboolean
open_preferences_manager_idle(void)
{
    gchar *name;

    gdk_threads_enter();

    if (pui == NULL) {
        gdk_threads_leave();
        return FALSE;
    }

    name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                         (pui->mail_directory));
    if (!name || strcmp(name, balsa_app.local_mail_directory) != 0) {
        /* Chooser still hasn't been initialized. */
        g_free(name);
        gdk_threads_leave();
        return TRUE;
    }
    g_free(name);

    g_signal_connect(pui->mail_directory, "selection-changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    gdk_threads_leave();
    return FALSE;
}                               /* open_preferences_manager_idle */
#endif                          /* GTK_CHECK_VERSION(2, 6, 0) */

enum {
    PM_TEXT_COL,
    PM_HELP_COL,
    PM_NOTEBOOK_COL,
    PM_CHILD_COL,
    PM_PAGE_COL,
    PM_NUM_COLS
};

static void
pm_selection_changed(GtkTreeSelection * selection, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter iter, child;
    GtkNotebook *notebook;
    guint page;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter,
                       PM_CHILD_COL, &notebook,
                       -1);
    if (notebook) {
        gtk_notebook_set_current_page(notebook, 0);
        g_object_unref(notebook);
    }

    do {
        gtk_tree_model_get(model, &iter,
                           PM_NOTEBOOK_COL, &notebook,
                           PM_PAGE_COL, &page,
                           -1);
        if (notebook) {
            gtk_notebook_set_current_page(notebook, page);
            g_object_unref(notebook);
        }
        child = iter;
    } while (gtk_tree_model_iter_parent(model, &iter, &child));
}

void
open_preferences_manager(GtkWidget * widget, gpointer data)
{
    GtkWidget *hbox;
    GtkTreeStore *store;
    GtkWidget *view;
    GtkTreeSelection * selection;
    GtkWidget *notebook;
    GtkWidget *active_win = data;
    gint i;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    /* only one preferences manager window */
    if (already_open) {
        gdk_window_raise(GTK_WIDGET(property_box)->window);
        return;
    }

    pui = g_malloc(sizeof(PropertyUI));

    property_box =              /* must NOT be modal */
        gtk_dialog_new_with_buttons(_("Balsa Preferences"),
                                    GTK_WINDOW(active_win),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                    GTK_STOCK_APPLY, GTK_RESPONSE_APPLY,
                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                    GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                                    NULL);

    hbox = gtk_hbox_new(FALSE, 12);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(property_box)->vbox), hbox);

    store = gtk_tree_store_new(PM_NUM_COLS,
                               G_TYPE_STRING,   /* PM_TEXT_COL     */
                               G_TYPE_STRING,   /* PM_HELP_COL     */
                               GTK_TYPE_WIDGET, /* PM_NOTEBOOK_COL */
                               GTK_TYPE_WIDGET, /* PM_CHILD_COL    */
                               G_TYPE_INT       /* PM_PAGE_COL     */
            );
    pui->view = view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_container_add(GTK_CONTAINER(hbox), view);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                 "text", PM_TEXT_COL,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    g_signal_connect(selection, "changed",
                     G_CALLBACK(pm_selection_changed), NULL);

    notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
    gtk_container_add(GTK_CONTAINER(hbox), notebook);
    g_object_set_data(G_OBJECT(property_box), "notebook", notebook);

    already_open = TRUE;

    gtk_window_set_wmclass(GTK_WINDOW(property_box), "preferences",
                           "Balsa");
    gtk_window_set_resizable(GTK_WINDOW(property_box), FALSE);
    g_object_set_data(G_OBJECT(property_box), "balsawindow", active_win);

    /* Create the pages */
    pm_append_page(notebook, create_mail_options_page(store),
                   N_("Mail Options"), store, NULL);
    pm_append_page(notebook, create_display_page(store),
                   N_("Display Options"), store, NULL);
    pm_append_page(notebook, create_address_book_page(store),
                   N_("Address Books"), store, NULL);

#if !HAVE_GTKSPELL
    pm_append_page(notebook, create_spelling_page(store),
                   N_("Spelling"), store, NULL);
#endif                          /* HAVE_GTKSPELL */

    pm_append_page(notebook, create_startup_page(store),
                   N_("Startup"), store, NULL);
    pm_append_page(notebook, create_misc_page(store),
                   N_("Miscellaneous"), store, NULL);

    gtk_tree_view_expand_all(GTK_TREE_VIEW(view));

    set_prefs();
    /* Now that all the prefs have been set, we must desensitize the
     * buttons. */
    gtk_dialog_set_response_sensitive(GTK_DIALOG(property_box),
                                      GTK_RESPONSE_OK, FALSE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(property_box),
                                      GTK_RESPONSE_APPLY, FALSE);

    for (i = 0; i < NUM_PWINDOW_MODES; i++) {
        g_signal_connect(G_OBJECT(pui->pwindow_type[i]), "clicked",
                         G_CALLBACK(properties_modified_cb), property_box);
    }

    g_signal_connect(G_OBJECT(pui->previewpane), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->alternative_layout), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->view_message_on_open), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->pgdownmod), "toggled",
                     G_CALLBACK(pgdown_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->pgdown_percent), "changed",
                     G_CALLBACK(pgdown_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->debug), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->mblist_show_mb_content_info), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
#if !HAVE_GTKSPELL
    g_signal_connect(G_OBJECT(pui->ignore_length), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->spell_check_sig), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->spell_check_quoted), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
#endif                          /* HAVE_GTKSPELL */

#if GTK_CHECK_VERSION(2, 6, 0)
    /* Connect signal in an idle handler, after the file chooser has
     * been initialized. */
    g_idle_add_full(G_PRIORITY_LOW,
                    (GSourceFunc) open_preferences_manager_idle,
                    NULL, NULL);
#else                           /* GTK_CHECK_VERSION(2, 6, 0) */
    g_signal_connect(G_OBJECT(pui->mail_directory), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
#endif                          /* GTK_CHECK_VERSION(2, 6, 0) */
    g_signal_connect(G_OBJECT(pui->check_mail_auto), "toggled",
                     G_CALLBACK(timer_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->check_mail_minutes), "changed",
                     G_CALLBACK(timer_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->quiet_background_check), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->msg_size_limit), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->check_imap), "toggled",
                     G_CALLBACK(imap_toggled_cb), property_box);

    g_signal_connect(G_OBJECT(pui->check_imap_inbox), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->notify_new_mail_dialog), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->notify_new_mail_sound), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

#if GTK_CHECK_VERSION(2, 10, 0)
    g_signal_connect(G_OBJECT(pui->notify_new_mail_icon), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
#endif                          /* GTK_CHECK_VERSION(2, 10, 0) */

    g_signal_connect(G_OBJECT(pui->close_mailbox_auto), "toggled",
                     G_CALLBACK(mailbox_close_timer_modified_cb),
                     property_box);
    g_signal_connect(G_OBJECT(pui->close_mailbox_minutes), "changed",
                     G_CALLBACK(mailbox_close_timer_modified_cb),
                     property_box);

    g_signal_connect(G_OBJECT(pui->hide_deleted), "toggled",
                     G_CALLBACK(filter_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->expunge_on_close), "toggled",
                     G_CALLBACK(expunge_on_close_cb), property_box);
    g_signal_connect(G_OBJECT(pui->expunge_auto), "toggled",
                     G_CALLBACK(expunge_auto_cb), property_box);
    g_signal_connect(G_OBJECT(pui->expunge_minutes), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->browse_wrap), "toggled",
                     G_CALLBACK(browse_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->browse_wrap_length), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->wordwrap), "toggled",
                     G_CALLBACK(wrap_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->wraplength), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->always_queue_sent_mail), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->copy_to_sentbox), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->autoquote), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->reply_include_html_parts), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->forward_attached), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* external editor */
    g_signal_connect(G_OBJECT(pui->edit_headers), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* arp */
    g_signal_connect(G_OBJECT(pui->quote_str), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->quote_pattern), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* multipart/alternative */
    g_signal_connect(G_OBJECT(pui->display_alt_plain), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* convert 8-bit text with no charset header */
    g_signal_connect(G_OBJECT(pui->convert_unknown_8bit_codeset),
                     "changed", G_CALLBACK(properties_modified_cb),
                     property_box);

    /* message font */
    g_signal_connect(G_OBJECT(pui->message_font_button), "font-set",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->subject_font_button), "font-set",
                     G_CALLBACK(properties_modified_cb), property_box);


    g_signal_connect(G_OBJECT(pui->open_inbox_upon_startup), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->check_mail_upon_startup), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->remember_open_mboxes), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->local_scan_depth), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->imap_scan_depth), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->empty_trash), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* threading */
    g_signal_connect(G_OBJECT(pui->tree_expand_check), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);


    /* Date format */
    g_signal_connect(G_OBJECT(pui->date_format), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* Selected headers */
    g_signal_connect(G_OBJECT(pui->selected_headers), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* Colour */
    for (i = 0; i < MAX_QUOTED_COLOR; i++)
        g_signal_connect(G_OBJECT(pui->quoted_color[i]), "released",
                         G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->url_color), "released",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->bad_address_color), "released",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* handling of message parts with 8-bit chars without codeset headers */
    for (i = 0; i < NUM_CONVERT_8BIT_MODES; i++)
        g_signal_connect(G_OBJECT(pui->convert_unknown_8bit[i]), "toggled",
                         G_CALLBACK(convert_8bit_cb), property_box);

    /* Gnome Property Box Signals */
    g_signal_connect(G_OBJECT(property_box), "response",
                     G_CALLBACK(response_cb), NULL);

    gtk_widget_show_all(GTK_WIDGET(property_box));

}                               /* open_preferences_manager */

static void
response_cb(GtkDialog * dialog, gint response, gpointer data)
{
    switch (response) {
    case GTK_RESPONSE_APPLY:
        apply_prefs(dialog);
        break;
    case GTK_RESPONSE_HELP:
        balsa_help_pbox_display();
        break;
    case GTK_RESPONSE_OK:
        apply_prefs(dialog);
        /* and fall through to... */
    default:
        destroy_pref_window_cb();
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

    /*
     * update data from the preferences window
     */

static void
destroy_pref_window_cb(void)
{
    g_free(pui);
    pui = NULL;
    already_open = FALSE;
}

    /* GHFunc callback; update any view that is using the current default
     * value to the new default value. */
static void
update_view_defaults(const gchar * url, LibBalsaMailboxView * view,
                     gpointer data)
{
    if (view->filter == libbalsa_mailbox_get_filter(NULL))
        view->filter = pui->filter;
    if (view->sort_field == libbalsa_mailbox_get_sort_field(NULL))
        view->sort_field = pui->sort_field_index;
    if (view->threading_type == libbalsa_mailbox_get_threading_type(NULL))
        view->threading_type = pui->threading_type_index;
}

static void
apply_prefs(GtkDialog * pbox)
{
    gint i;
    GtkWidget *balsa_window;
    const gchar *tmp;
    gboolean save_setting;

    /*
     * Before changing the default mailbox view, update any current
     * views that have default values.
     */
    g_hash_table_foreach(libbalsa_mailbox_view_table,
                         (GHFunc) update_view_defaults, NULL);

    g_free(balsa_app.local_mail_directory);
#if GTK_CHECK_VERSION(2, 6, 0)
    balsa_app.local_mail_directory =
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                      (pui->mail_directory));
#else                           /* GTK_CHECK_VERSION(2, 6, 0) */
    balsa_app.local_mail_directory =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->mail_directory)));
#endif                          /* GTK_CHECK_VERSION(2, 6, 0) */

    /* 
     * display page 
     */
    for (i = 0; i < NUM_PWINDOW_MODES; i++)
        if (GTK_TOGGLE_BUTTON(pui->pwindow_type[i])->active) {
            balsa_app.pwindow_option = pwindow_type[i];
            break;
        }

    balsa_app.debug = GTK_TOGGLE_BUTTON(pui->debug)->active;
    balsa_app.previewpane = GTK_TOGGLE_BUTTON(pui->previewpane)->active;

    save_setting = balsa_app.alternative_layout;
    balsa_app.alternative_layout =
        GTK_TOGGLE_BUTTON(pui->alternative_layout)->active;
    if (balsa_app.alternative_layout != save_setting)
        balsa_change_window_layout(balsa_app.main_window);

    balsa_app.view_message_on_open =
        GTK_TOGGLE_BUTTON(pui->view_message_on_open)->active;
    balsa_app.pgdownmod = GTK_TOGGLE_BUTTON(pui->pgdownmod)->active;
    balsa_app.pgdown_percent =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->pgdown_percent));

    if (balsa_app.mblist_show_mb_content_info !=
        GTK_TOGGLE_BUTTON(pui->mblist_show_mb_content_info)->active) {
        balsa_app.mblist_show_mb_content_info =
            !balsa_app.mblist_show_mb_content_info;
        g_object_set(G_OBJECT(balsa_app.mblist), "show_content_info",
                     balsa_app.mblist_show_mb_content_info, NULL);
    }

    balsa_app.check_mail_auto =
        GTK_TOGGLE_BUTTON(pui->check_mail_auto)->active;
    balsa_app.check_mail_timer =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->check_mail_minutes));
    balsa_app.quiet_background_check =
        GTK_TOGGLE_BUTTON(pui->quiet_background_check)->active;
    balsa_app.msg_size_limit =
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(pui->msg_size_limit)) *
        1024;
    balsa_app.check_imap = GTK_TOGGLE_BUTTON(pui->check_imap)->active;
    balsa_app.check_imap_inbox =
        GTK_TOGGLE_BUTTON(pui->check_imap_inbox)->active;
    balsa_app.notify_new_mail_dialog =
        GTK_TOGGLE_BUTTON(pui->notify_new_mail_dialog)->active;
    balsa_app.notify_new_mail_sound =
        GTK_TOGGLE_BUTTON(pui->notify_new_mail_sound)->active;
#if GTK_CHECK_VERSION(2, 10, 0)
    balsa_app.notify_new_mail_icon =
        GTK_TOGGLE_BUTTON(pui->notify_new_mail_icon)->active;
#endif                          /* GTK_CHECK_VERSION(2, 10, 0) */
    balsa_app.mdn_reply_clean =
        pm_combo_box_get_level(pui->mdn_reply_clean_menu);
    balsa_app.mdn_reply_notclean =
        pm_combo_box_get_level(pui->mdn_reply_notclean_menu);

    if (balsa_app.check_mail_auto)
        update_timer(TRUE, balsa_app.check_mail_timer);
    else
        update_timer(FALSE, 0);

    balsa_app.wordwrap = GTK_TOGGLE_BUTTON(pui->wordwrap)->active;
    balsa_app.wraplength =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->wraplength));
    balsa_app.autoquote = GTK_TOGGLE_BUTTON(pui->autoquote)->active;
    balsa_app.reply_strip_html =
        !GTK_TOGGLE_BUTTON(pui->reply_include_html_parts)->active;
    balsa_app.forward_attached =
        GTK_TOGGLE_BUTTON(pui->forward_attached)->active;

    save_setting = balsa_app.always_queue_sent_mail;
    balsa_app.always_queue_sent_mail =
        GTK_TOGGLE_BUTTON(pui->always_queue_sent_mail)->active;
    if (balsa_app.always_queue_sent_mail != save_setting)
        balsa_toolbar_model_changed(balsa_window_get_toolbar_model());

    balsa_app.copy_to_sentbox =
        GTK_TOGGLE_BUTTON(pui->copy_to_sentbox)->active;

    balsa_app.close_mailbox_auto =
        GTK_TOGGLE_BUTTON(pui->close_mailbox_auto)->active;
    balsa_app.close_mailbox_timeout =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->close_mailbox_minutes)) *
        60;

    libbalsa_mailbox_set_filter(NULL, pui->filter);
    balsa_app.expunge_on_close =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->expunge_on_close));
    balsa_app.expunge_auto = GTK_TOGGLE_BUTTON(pui->expunge_auto)->active;
    balsa_app.expunge_timeout =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->expunge_minutes)) * 60;
    balsa_app.mw_action_after_move =
        pm_combo_box_get_level(pui->action_after_move_menu);

    /* external editor */
    balsa_app.edit_headers = GTK_TOGGLE_BUTTON(pui->edit_headers)->active;

    /* arp */
    g_free(balsa_app.quote_str);
    balsa_app.quote_str =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->quote_str)));

    g_free(balsa_app.message_font);
    balsa_app.message_font =
        g_strdup(gtk_font_button_get_font_name
                 (GTK_FONT_BUTTON(pui->message_font_button)));
    g_free(balsa_app.subject_font);
    balsa_app.subject_font =
        g_strdup(gtk_font_button_get_font_name
                 (GTK_FONT_BUTTON(pui->subject_font_button)));

    g_free(balsa_app.quote_regex);
    tmp = gtk_entry_get_text(GTK_ENTRY(pui->quote_pattern));
    balsa_app.quote_regex = g_strcompress(tmp);

    balsa_app.browse_wrap = GTK_TOGGLE_BUTTON(pui->browse_wrap)->active;
    /* main window view menu can also toggle balsa_app.browse_wrap
     * update_view_menu lets it know we've made a change */
    update_view_menu(balsa_app.main_window);
    balsa_app.browse_wrap_length =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->browse_wrap_length));

    balsa_app.display_alt_plain =
        GTK_TOGGLE_BUTTON(pui->display_alt_plain)->active;

    balsa_app.open_inbox_upon_startup =
        GTK_TOGGLE_BUTTON(pui->open_inbox_upon_startup)->active;
    balsa_app.check_mail_upon_startup =
        GTK_TOGGLE_BUTTON(pui->check_mail_upon_startup)->active;
    balsa_app.remember_open_mboxes =
        GTK_TOGGLE_BUTTON(pui->remember_open_mboxes)->active;
    balsa_app.local_scan_depth =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->local_scan_depth));
    balsa_app.imap_scan_depth =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->imap_scan_depth));
    balsa_app.empty_trash_on_exit =
        GTK_TOGGLE_BUTTON(pui->empty_trash)->active;

#if !HAVE_GTKSPELL
    /* spell checking */
    balsa_app.module = pui->module_index;
    balsa_app.suggestion_mode = pui->suggestion_mode_index;
    balsa_app.ignore_size =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->ignore_length));
    balsa_app.check_sig = GTK_TOGGLE_BUTTON(pui->spell_check_sig)->active;
    balsa_app.check_quoted =
        GTK_TOGGLE_BUTTON(pui->spell_check_quoted)->active;
#endif                          /* HAVE_GTKSPELL */

    /* date format */
    g_free(balsa_app.date_string);
    balsa_app.date_string =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->date_format)));

    /* selected headers */
    g_free(balsa_app.selected_headers);
    balsa_app.selected_headers =
        g_ascii_strdown(gtk_entry_get_text
                        (GTK_ENTRY(pui->selected_headers)), -1);

    /* quoted text color */
    for (i = 0; i < MAX_QUOTED_COLOR; i++) {
        gdk_colormap_free_colors(gdk_drawable_get_colormap
                                 (GTK_WIDGET(pbox)->window),
                                 &balsa_app.quoted_color[i], 1);
        gtk_color_button_get_color(GTK_COLOR_BUTTON(pui->quoted_color[i]),
                                   &balsa_app.quoted_color[i]);
    }

    /* url color */
    gdk_colormap_free_colors(gdk_drawable_get_colormap
                             (GTK_WIDGET(pbox)->window),
                             &balsa_app.url_color, 1);
    gtk_color_button_get_color(GTK_COLOR_BUTTON(pui->url_color),
                               &balsa_app.url_color);

    /* bad address color */
    gdk_colormap_free_colors(gdk_drawable_get_colormap
                             (GTK_WIDGET(pbox)->window),
                             &balsa_app.bad_address_color, 1);
    gtk_color_button_get_color(GTK_COLOR_BUTTON(pui->bad_address_color),
                               &balsa_app.bad_address_color);

    /* sorting and threading */
    libbalsa_mailbox_set_sort_field(NULL, pui->sort_field_index);
    libbalsa_mailbox_set_threading_type(NULL, pui->threading_type_index);
    balsa_app.expand_tree =
        GTK_TOGGLE_BUTTON(pui->tree_expand_check)->active;

    /* Information dialogs */
    balsa_app.information_message =
        pm_combo_box_get_level(pui->information_message_menu);
    balsa_app.warning_message =
        pm_combo_box_get_level(pui->warning_message_menu);
    balsa_app.error_message =
        pm_combo_box_get_level(pui->error_message_menu);
    balsa_app.debug_message =
        pm_combo_box_get_level(pui->debug_message_menu);

    /* handling of 8-bit message parts without codeset header */
    balsa_app.convert_unknown_8bit =
        GTK_TOGGLE_BUTTON(pui->convert_unknown_8bit[1])->active;
    balsa_app.convert_unknown_8bit_codeset =
        gtk_combo_box_get_active(GTK_COMBO_BOX
                                 (pui->convert_unknown_8bit_codeset));
    libbalsa_set_fallback_codeset(balsa_app.convert_unknown_8bit_codeset);

    /*
     * close window and free memory
     */
    config_save();
    balsa_window =
        GTK_WIDGET(g_object_get_data(G_OBJECT(pbox), "balsawindow"));
    balsa_window_refresh(BALSA_WINDOW(balsa_window));
}


/*
 * refresh data in the preferences window
 */
void
set_prefs(void)
{
    unsigned i;
    gchar *tmp;

    for (i = 0; i < NUM_PWINDOW_MODES; i++)
        if (balsa_app.pwindow_option == pwindow_type[i]) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (pui->pwindow_type[i]), TRUE);
            break;
        }

#if GTK_CHECK_VERSION(2, 6, 0)
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER
                                        (pui->mail_directory),
                                  balsa_app.local_mail_directory);
#else                           /* GTK_CHECK_VERSION(2, 6, 0) */
    gtk_entry_set_text(GTK_ENTRY(pui->mail_directory),
                       balsa_app.local_mail_directory);
#endif                          /* GTK_CHECK_VERSION(2, 6, 0) */

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->previewpane),
                                 balsa_app.previewpane);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->alternative_layout),
                                 balsa_app.alternative_layout);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->view_message_on_open),
                                 balsa_app.view_message_on_open);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->pgdownmod),
                                 balsa_app.pgdownmod);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->pgdown_percent),
                              (float) balsa_app.pgdown_percent);
    gtk_widget_set_sensitive(pui->pgdown_percent,
                             GTK_TOGGLE_BUTTON(pui->pgdownmod)->active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->debug),
                                 balsa_app.debug);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->mblist_show_mb_content_info),
                                 balsa_app.mblist_show_mb_content_info);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->check_mail_auto),
                                 balsa_app.check_mail_auto);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->check_mail_minutes),
                              (float) balsa_app.check_mail_timer);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->quiet_background_check),
                                 balsa_app.quiet_background_check);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->msg_size_limit),
                              ((float) balsa_app.msg_size_limit) / 1024);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->check_imap),
                                 balsa_app.check_imap);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->check_imap_inbox),
                                 balsa_app.check_imap_inbox);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->notify_new_mail_dialog),
                                 balsa_app.notify_new_mail_dialog);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->notify_new_mail_sound),
                                 balsa_app.notify_new_mail_sound);
#if GTK_CHECK_VERSION(2, 10, 0)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->notify_new_mail_icon),
                                 balsa_app.notify_new_mail_icon);
#endif                          /* GTK_CHECK_VERSION(2, 10, 0) */
    if (!balsa_app.check_imap)
        gtk_widget_set_sensitive(GTK_WIDGET(pui->check_imap_inbox), FALSE);

    pm_combo_box_set_level(pui->mdn_reply_clean_menu,
                           balsa_app.mdn_reply_clean);
    pm_combo_box_set_level(pui->mdn_reply_notclean_menu,
                           balsa_app.mdn_reply_notclean);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->close_mailbox_auto),
                                 balsa_app.close_mailbox_auto);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->close_mailbox_minutes),
                              (float) balsa_app.close_mailbox_timeout /
                              60);
    gtk_widget_set_sensitive(pui->close_mailbox_minutes,
                             GTK_TOGGLE_BUTTON(pui->close_mailbox_auto)->
                             active);

    pui->filter = libbalsa_mailbox_get_filter(NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->hide_deleted),
                                 pui->filter & (1 << 0));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->expunge_on_close),
                                 balsa_app.expunge_on_close);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->expunge_auto),
                                 balsa_app.expunge_auto);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->expunge_minutes),
                              (float) balsa_app.expunge_timeout / 60);
    gtk_widget_set_sensitive(pui->expunge_minutes,
                             GTK_TOGGLE_BUTTON(pui->expunge_auto)->active);
    gtk_widget_set_sensitive(pui->check_mail_minutes,
                             GTK_TOGGLE_BUTTON(pui->check_mail_auto)->
                             active);
    pm_combo_box_set_level(pui->action_after_move_menu,
                           balsa_app.mw_action_after_move);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->wordwrap),
                                 balsa_app.wordwrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->wraplength),
                              (float) balsa_app.wraplength);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->always_queue_sent_mail),
                                 balsa_app.always_queue_sent_mail);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->copy_to_sentbox),
                                 balsa_app.copy_to_sentbox);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->autoquote),
                                 balsa_app.autoquote);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->reply_include_html_parts),
                                 !balsa_app.reply_strip_html);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->forward_attached),
                                 balsa_app.forward_attached);

    gtk_widget_set_sensitive(pui->wraplength,
                             GTK_TOGGLE_BUTTON(pui->wordwrap)->active);

    /* external editor */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->edit_headers),
                                 balsa_app.edit_headers);

    /* arp */
    gtk_entry_set_text(GTK_ENTRY(pui->quote_str), balsa_app.quote_str);
    tmp = g_strescape(balsa_app.quote_regex, NULL);
    gtk_entry_set_text(GTK_ENTRY(pui->quote_pattern), tmp);
    g_free(tmp);

    /* wrap incoming text/plain */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->browse_wrap),
                                 balsa_app.browse_wrap);
    gtk_widget_set_sensitive(pui->browse_wrap_length,
                             balsa_app.browse_wrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->browse_wrap_length),
                              (float) balsa_app.browse_wrap_length);

    /* how to treat multipart/alternative */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->display_alt_plain),
                                 balsa_app.display_alt_plain);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->open_inbox_upon_startup),
                                 balsa_app.open_inbox_upon_startup);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->check_mail_upon_startup),
                                 balsa_app.check_mail_upon_startup);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->remember_open_mboxes),
                                 balsa_app.remember_open_mboxes);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->local_scan_depth),
                              balsa_app.local_scan_depth);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->imap_scan_depth),
                              balsa_app.imap_scan_depth);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->empty_trash),
                                 balsa_app.empty_trash_on_exit);

    /* threading */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->tree_expand_check),
                                 balsa_app.expand_tree);
    pui->sort_field_index = libbalsa_mailbox_get_sort_field(NULL);
    pm_combo_box_set_level(pui->default_sort_field, pui->sort_field_index);
    pui->threading_type_index = libbalsa_mailbox_get_threading_type(NULL);
    pm_combo_box_set_level(pui->default_threading_type,
                           pui->threading_type_index);

#if !HAVE_GTKSPELL
    /* spelling */
    pui->module_index = balsa_app.module;
    pm_combo_box_set_level(pui->module, balsa_app.module);
    pui->suggestion_mode_index = balsa_app.suggestion_mode;
    pm_combo_box_set_level(pui->suggestion_mode,
                           balsa_app.suggestion_mode);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->ignore_length),
                              balsa_app.ignore_size);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->spell_check_sig),
                                 balsa_app.check_sig);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->spell_check_quoted),
                                 balsa_app.check_quoted);
#endif                          /* HAVE_GTKSPELL */


    /* date format */
    if (balsa_app.date_string)
        gtk_entry_set_text(GTK_ENTRY(pui->date_format),
                           balsa_app.date_string);

    /* selected headers */
    if (balsa_app.selected_headers)
        gtk_entry_set_text(GTK_ENTRY(pui->selected_headers),
                           balsa_app.selected_headers);

    /* Colour */
    for (i = 0; i < MAX_QUOTED_COLOR; i++)
        gtk_color_button_set_color(GTK_COLOR_BUTTON(pui->quoted_color[i]),
                                   &balsa_app.quoted_color[i]);
    gtk_color_button_set_color(GTK_COLOR_BUTTON(pui->url_color),
                               &balsa_app.url_color);
    gtk_color_button_set_color(GTK_COLOR_BUTTON(pui->bad_address_color),
                               &balsa_app.bad_address_color);

    /* Information Message */
    pm_combo_box_set_level(pui->information_message_menu,
                           balsa_app.information_message);
    pm_combo_box_set_level(pui->warning_message_menu,
                           balsa_app.warning_message);
    pm_combo_box_set_level(pui->error_message_menu,
                           balsa_app.error_message);
    pm_combo_box_set_level(pui->debug_message_menu,
                           balsa_app.debug_message);

    /* handling of 8-bit message parts without codeset header */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->convert_unknown_8bit[1]),
                                 balsa_app.convert_unknown_8bit);
    gtk_widget_set_sensitive(pui->convert_unknown_8bit_codeset,
                             balsa_app.convert_unknown_8bit);
}

enum {
    AB_TYPE_COLUMN,
    AB_NAME_COLUMN,
    AB_XPND_COLUMN,
    AB_DATA_COLUMN,
    AB_N_COLUMNS
};

static void
update_address_books(void)
{
    gchar *type, *name;
    GList *list = balsa_app.address_book_list;
    LibBalsaAddressBook *address_book;
    GtkTreeView *tree_view;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!pui)
        /* Pref window was closed while the address book dialog was
         * open. */
        return;

    tree_view = GTK_TREE_VIEW(pui->address_books);
    model = gtk_tree_view_get_model(tree_view);
    gtk_list_store_clear(GTK_LIST_STORE(model));

    while (list) {
        address_book = LIBBALSA_ADDRESS_BOOK(list->data);

        g_assert(address_book != NULL);

        if (LIBBALSA_IS_ADDRESS_BOOK_VCARD(address_book))
            type = "VCARD";
        else if (LIBBALSA_IS_ADDRESS_BOOK_LDIF(address_book))
            type = "LDIF";
        else if (LIBBALSA_IS_ADDRESS_BOOK_EXTERN(address_book))
            type = "Extern";
#if ENABLE_LDAP
        else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book))
            type = "LDAP";
#endif
#if HAVE_SQLITE
        else if (LIBBALSA_IS_ADDRESS_BOOK_GPE(address_book))
            type = "GPE";
#endif
#if HAVE_RUBRICA
        else if (LIBBALSA_IS_ADDRESS_BOOK_RUBRICA(address_book))
            type = "Rubrica";
#endif
        else
            type = _("Unknown");

        if (address_book == balsa_app.default_address_book) {
            name = g_strdup_printf(_("%s (default)"), address_book->name);
        } else {
            name = g_strdup(address_book->name);
        }
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           AB_TYPE_COLUMN, type,
                           AB_NAME_COLUMN, name,
                           AB_XPND_COLUMN, (address_book->expand_aliases
                                            && !address_book->is_expensive),
                           AB_DATA_COLUMN, address_book, -1);

        g_free(name);
        list = g_list_next(list);
    }

    if (gtk_tree_model_get_iter_first(model, &iter))
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection
                                       (tree_view), &iter);
}

enum {
    MS_PROT_COLUMN,
    MS_NAME_COLUMN,
    MS_DATA_COLUMN,
    MS_N_COLUMNS
};

static void
add_other_server(BalsaMailboxNode * mbnode, GtkTreeModel * model)
{
    gchar *protocol = NULL;
    gchar *name = NULL;
    gboolean append = FALSE;

    if (mbnode) {
        LibBalsaMailbox *mailbox = mbnode->mailbox;
        if (mailbox) {
            if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
                protocol = "IMAP";
                name = mailbox->name;
                append = TRUE;
            }
        } else
            if (LIBBALSA_IS_IMAP_SERVER(mbnode->server)) {
            protocol = "IMAP";
            name = mbnode->name;
            append = TRUE;
        }
        if (append) {
            GtkTreeIter iter;

            gtk_list_store_append(GTK_LIST_STORE(model), &iter);
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               MS_PROT_COLUMN, protocol,
                               MS_NAME_COLUMN, name,
                               MS_DATA_COLUMN, mbnode, -1);
        }
    }
}

/* update_mail_servers:
   update mail server list in the preferences window.
   NOTE: it can be called even when the preferences window is closed (via
   mailbox context menu) - and it should check for it.
 */
void
update_mail_servers(void)
{
    GtkTreeView *tree_view;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GList *list;
    gchar *protocol;
    GtkTreeModel *app_model;
    gboolean valid;
    BalsaMailboxNode *mbnode;

    if (pui == NULL)
        return;

    tree_view = GTK_TREE_VIEW(pui->mail_servers);
    model = gtk_tree_view_get_model(tree_view);

    gtk_list_store_clear(GTK_LIST_STORE(model));
    for (list = balsa_app.inbox_input; list; list = list->next) {
        if (!(mbnode = list->data))
            continue;
        if (LIBBALSA_IS_MAILBOX_POP3(mbnode->mailbox))
            protocol = "POP3";
        else if (LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox))
            protocol = "IMAP";
        else
            protocol = _("Unknown");

        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           MS_PROT_COLUMN, protocol,
                           MS_NAME_COLUMN, mbnode->mailbox->name,
                           MS_DATA_COLUMN, mbnode, -1);
    }
    /*
     * add other remote servers
     *
     * we'll check everything at the top level in the mailbox_nodes
     * list:
     */
    app_model = GTK_TREE_MODEL(balsa_app.mblist_tree_store);
    for (valid = gtk_tree_model_get_iter_first(app_model, &iter);
         valid; valid = gtk_tree_model_iter_next(app_model, &iter)) {
        gtk_tree_model_get(app_model, &iter, 0, &mbnode, -1);
        add_other_server(mbnode, model);
        g_object_unref(mbnode);
    }

    if (gtk_tree_model_get_iter_first(model, &iter))
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection
                                       (tree_view), &iter);
}

/* helper functions that simplify often performed actions */
static GtkWidget *
attach_entry(const gchar * label, gint row, GtkWidget * table)
{
    return attach_entry_full(label, row, table, 0, 1, 2);
}

static GtkWidget *
attach_entry_full(const gchar * label, gint row, GtkWidget * table,
                  gint col_left, gint col_middle, gint col_right)
{
    GtkWidget *res, *lw;
    GtkWidget *page;

    res = gtk_entry_new();
    lw = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(lw), 0, 0.5);

    page = g_object_get_data(G_OBJECT(table), BALSA_TABLE_PAGE_KEY);
    pm_page_add_to_size_group(page, lw);

    gtk_table_attach(GTK_TABLE(table), lw,
                     col_left, col_middle,
                     row, row + 1,
                     (GtkAttachOptions) (GTK_FILL),
                     (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify(GTK_LABEL(lw), GTK_JUSTIFY_RIGHT);

    gtk_table_attach(GTK_TABLE(table), res,
                     col_middle, col_right,
                     row, row + 1,
                     GTK_EXPAND | GTK_FILL,
                     (GtkAttachOptions) (0), 0, 0);
    return res;
}

static GtkWidget *
attach_information_menu(const gchar * label, gint row, GtkTable * table,
                        gint defval)
{
    GtkWidget *w, *combo_box;
    w = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(w), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), w, 0, 1, row, row + 1,
                     GTK_FILL, 0, 0, 0);

    combo_box = create_information_message_menu();
    pm_combo_box_set_level(combo_box, defval);
    gtk_table_attach(GTK_TABLE(table), combo_box, 1, 2, row, row + 1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    return combo_box;
}

static GtkWidget *
attach_label(const gchar * text, GtkWidget * table, gint row,
             GtkWidget * page)
{
    GtkWidget *label;

    label = gtk_label_new(text);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row + 1,
                     GTK_FILL, 0, 0, 0);
    if (page)
        pm_page_add_to_size_group(page, label);

    return label;
}

static GtkWidget *
box_start_check(const gchar * label, GtkWidget * box)
{
    GtkWidget *res = gtk_check_button_new_with_mnemonic(label);
    gtk_box_pack_start(GTK_BOX(box), res, FALSE, TRUE, 0);
    return res;
}

static GtkWidget *
add_button_to_box(const gchar * label, GCallback cb, gpointer cb_data,
                  GtkWidget * box)
{
    GtkWidget *button = gtk_button_new_with_mnemonic(label);
    g_signal_connect_swapped(button, "clicked", cb, cb_data);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);

    return button;
}

static GtkWidget *
vbox_in_container(GtkWidget * container)
{
    GtkWidget *res = gtk_vbox_new(FALSE, ROW_SPACING);
    gtk_container_add(GTK_CONTAINER(container), res);
    return res;
}

static GtkWidget *
color_box(GtkBox * parent, const gchar * title)
{
    GtkWidget *box, *picker;
    box = gtk_hbox_new(FALSE, COL_SPACING);
    gtk_box_pack_start(parent, box, FALSE, FALSE, 0);

    picker = gtk_color_button_new();
    gtk_color_button_set_title(GTK_COLOR_BUTTON(picker), title);
    gtk_box_pack_start(GTK_BOX(box), picker, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(title), FALSE, FALSE,
                       0);
    return picker;
}

static GtkWidget *
mailserver_subpage()
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, remote_mailbox_servers_group(page), TRUE);
    pm_page_add(page, local_mail_group(page), FALSE);
#if ENABLE_ESMTP
    pm_page_add(page, outgoing_mail_group(page), TRUE);
#endif                          /* ENABLE_ESMTP */

    return page;
}

static GtkWidget *
remote_mailbox_servers_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *hbox;
    GtkWidget *vbox;
    GtkWidget *scrolledwindow;
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    group = pm_group_new(_("Remote Mailbox Servers"));
    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox, TRUE);

    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), scrolledwindow, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolledwindow, -1, 100);
    pm_page_add_to_size_group(page, scrolledwindow);

    store = gtk_list_store_new(MS_N_COLUMNS,
                               G_TYPE_STRING,   /* MS_PROT_COLUMN */
                               G_TYPE_STRING,   /* MS_NAME_COLUMN */
                               G_TYPE_POINTER); /* MS_DATA_COLUMN */
    pui->mail_servers = tree_view =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Type"),
                                                 renderer,
                                                 "text", MS_PROT_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Mailbox Name"),
                                                 renderer,
                                                 "text", MS_NAME_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    gtk_container_add(GTK_CONTAINER(scrolledwindow), tree_view);

    g_signal_connect(G_OBJECT(pui->mail_servers), "row-activated",
                     G_CALLBACK(server_edit_cb), NULL);

    vbox = vbox_in_container(hbox);
    add_button_to_box(_("_Add"), G_CALLBACK(server_add_cb),
                      NULL, vbox);
    add_button_to_box(_("_Modify"), G_CALLBACK(server_edit_cb),
                      tree_view, vbox);
    add_button_to_box(_("_Delete"), G_CALLBACK(server_del_cb),
                      tree_view, vbox);

    /* fill in data */
    update_mail_servers();

    return group;
}

static GtkWidget *
local_mail_group(GtkWidget * page)
{
    GtkWidget *group = pm_group_new(_("Local Mail Directory"));
#if GTK_CHECK_VERSION(2, 6, 0)
    pui->mail_directory =
        gtk_file_chooser_button_new(_("Select your local mail directory"),
                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    pm_group_add(group, pui->mail_directory, FALSE);
#else                           /* GTK_CHECK_VERSION(2, 6, 0) */
    GtkWidget *fileentry;

    fileentry = gnome_file_entry_new("MAIL-DIR",
                                     _
                                     ("Select your local mail directory"));
    pm_group_add(group, fileentry, FALSE);

    gnome_file_entry_set_directory_entry(GNOME_FILE_ENTRY(fileentry),
                                         TRUE);
    gnome_file_entry_set_modal(GNOME_FILE_ENTRY(fileentry), TRUE);

    pui->mail_directory =
        gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(fileentry));
#endif                          /* GTK_CHECK_VERSION(2, 6, 0) */

    return group;
}

#if ENABLE_ESMTP
static GtkWidget *
outgoing_mail_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *hbox;
    GtkWidget *scrolled_window;
    GtkListStore *store;
    GtkWidget *tree_view;
    GtkTreeSelection *selection;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkWidget *vbox;

    group = pm_group_new(_("Outgoing Mail Servers"));
    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox, TRUE);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), scrolled_window, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, -1, 100);
    pm_page_add_to_size_group(page, scrolled_window);

    store = gtk_list_store_new(2, G_TYPE_STRING,        /* Server name    */
                               G_TYPE_POINTER); /* Object address */
    pui->smtp_servers = tree_view =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    g_signal_connect(selection, "changed",
                     G_CALLBACK(smtp_server_changed), NULL);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(_("Server Name"),
                                                      renderer,
                                                      "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);

    g_signal_connect(G_OBJECT(pui->smtp_servers), "row-activated",
                     G_CALLBACK(smtp_server_edit_cb), NULL);

    vbox = vbox_in_container(hbox);
    add_button_to_box(_("_Add"), G_CALLBACK(smtp_server_add_cb),
                      NULL, vbox);
    pui->smtp_server_edit_button =
        add_button_to_box(_("_Modify"), G_CALLBACK(smtp_server_edit_cb),
                          tree_view, vbox);
    gtk_widget_set_sensitive(pui->smtp_server_edit_button, FALSE);
    pui->smtp_server_del_button =
        add_button_to_box(_("_Delete"), G_CALLBACK(smtp_server_del_cb),
                          tree_view, vbox);
    gtk_widget_set_sensitive(pui->smtp_server_del_button, FALSE);

    /* fill in data */
    update_smtp_servers();

    return group;
}
#endif                          /* ENABLE_ESMTP */

static GtkWidget *
create_mail_options_page(GtkTreeStore * store)
{
    GtkWidget *notebook;
    GtkTreeIter iter;

    notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);

    gtk_tree_store_append(store, &iter, NULL);
    pm_append_page(notebook, mailserver_subpage(), _("Mail Servers"),
                   store, &iter);
    pm_append_page(notebook, incoming_subpage(), _("Incoming"),
                   store, &iter);
    pm_append_page(notebook, outgoing_subpage(), _("Outgoing"),
                   store, &iter);

    return notebook;
}

static GtkWidget *
incoming_subpage(void)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, checking_group(page), FALSE);
    pm_page_add(page, mdn_group(page), FALSE);

    return page;
}

static GtkWidget *
checking_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    guint row;
    GtkObject *spinbutton_adj;
    GtkWidget *label;
    GtkWidget *hbox;

    group = pm_group_new(_("Checking"));
    table = create_table(6, 3, page);
    pm_group_add(group, table, FALSE);

    row = 0;
    pui->check_mail_auto = gtk_check_button_new_with_mnemonic(
	_("_Check mail automatically every"));
    gtk_table_attach(GTK_TABLE(table), pui->check_mail_auto,
                     0, 1, row, row + 1, GTK_FILL, 0, 0, 0);
    pm_page_add_to_size_group(page, pui->check_mail_auto);

    spinbutton_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 10);
    pui->check_mail_minutes =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table), pui->check_mail_minutes,
                     1, 2, row, row + 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("minutes"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     2, 3, row, row + 1, GTK_FILL, 0, 0, 0);

    ++row;
    pui->check_imap = gtk_check_button_new_with_mnemonic(
	_("Check _IMAP mailboxes"));
    gtk_table_attach(GTK_TABLE(table), pui->check_imap,
                     0, 1, row, row + 1, GTK_FILL, 0, 0, 0);
    pm_page_add_to_size_group(page, pui->check_imap);
    
    pui->check_imap_inbox = gtk_check_button_new_with_mnemonic(
	_("Check INBOX _only"));
    gtk_table_attach(GTK_TABLE(table), pui->check_imap_inbox,
                     1, 3, row, row + 1, GTK_FILL, 0, 0, 0);
    
    ++row;
    hbox = gtk_hbox_new(FALSE, COL_SPACING);

    label = gtk_label_new(_("When mail arrives:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    pui->notify_new_mail_dialog =
        gtk_check_button_new_with_label(_("Display message"));
    gtk_box_pack_start(GTK_BOX(hbox), pui->notify_new_mail_dialog,
                       FALSE, FALSE, 0);

    pui->notify_new_mail_sound =
        gtk_check_button_new_with_label(_("Play sound"));
    gtk_box_pack_start(GTK_BOX(hbox), pui->notify_new_mail_sound,
                       FALSE, FALSE, 0);

#if GTK_CHECK_VERSION(2, 10, 0)
    pui->notify_new_mail_icon =
        gtk_check_button_new_with_label(_("Show icon"));
    gtk_box_pack_start(GTK_BOX(hbox), pui->notify_new_mail_icon,
                       FALSE, FALSE, 0);
#endif                          /* GTK_CHECK_VERSION(2, 10, 0) */

    gtk_table_attach(GTK_TABLE(table), hbox,
                     0, 3, row, row + 1, GTK_FILL, 0, 0, 0);
    
    ++row;
    pui->quiet_background_check = gtk_check_button_new_with_label(
	_("Do background check quietly (no messages in status bar)"));
    gtk_table_attach(GTK_TABLE(table), pui->quiet_background_check,
                     0, 3, row, row + 1, GTK_FILL, 0, 0, 0);

    ++row;
    label = gtk_label_new_with_mnemonic(_("_POP message size limit:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row + 1, GTK_FILL, 0, 0, 0);
    pui->msg_size_limit = gtk_spin_button_new_with_range(0.1, 100, 0.1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), pui->msg_size_limit);
    gtk_table_attach(GTK_TABLE(table), pui->msg_size_limit,
                     1, 2, row, row + 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new(_("MB"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     2, 3, row, row + 1, GTK_FILL, 0, 0, 0);
    
    return group;
}

static GtkWidget *
quoted_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GtkObject *spinbutton_adj;
    GtkWidget *label;

    /* Quoted text regular expression */
    /* and RFC2646-style flowed text  */

    group = pm_group_new(_("Quoted and Flowed Text"));
    table = create_table(2, 3, page);
    pm_group_add(group, table, FALSE);

    attach_label(_("Quoted Text\n" "Regular Expression:"), table, 0, page);

    pui->quote_pattern = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), pui->quote_pattern,
                     1, 3, 0, 1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    pui->browse_wrap =
	gtk_check_button_new_with_label(_("Wrap Text at"));
    gtk_table_attach(GTK_TABLE(table), pui->browse_wrap,
                     0, 1, 1, 2, GTK_FILL, 0, 0, 0);
    pm_page_add_to_size_group(page, pui->browse_wrap);

    spinbutton_adj = gtk_adjustment_new(1.0, 40.0, 200.0, 1.0, 5.0, 0.0);
    pui->browse_wrap_length =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table), pui->browse_wrap_length,
                     1, 2, 1, 2,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_widget_set_sensitive(pui->browse_wrap_length, FALSE);
    label = gtk_label_new(_("characters"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 1, 2,
                     GTK_FILL, 0, 0, 0);

    return group;
}

static GtkWidget *
alternative_group(GtkWidget * page)
{
    GtkWidget *group;

    /* handling of multipart/alternative */

    group = pm_group_new(_("Display of Multipart/Alternative Parts"));

    pui->display_alt_plain =
	gtk_check_button_new_with_label(_("Prefer text/plain over html"));
    pm_group_add(group, pui->display_alt_plain, FALSE);

    return group;
}

static GtkWidget *
broken_8bit_codeset_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GSList *radio_group = NULL;
    
    /* treatment of messages with 8-bit chars, but without proper MIME encoding */

    group =
        pm_group_new(_("National (8-bit) characters in broken messages "
                       "without codeset header"));
    table = create_table(2, 2, page);
    pm_group_add(group, table, FALSE);
    
    pui->convert_unknown_8bit[0] =
	GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(radio_group,
							 _("display as \"?\"")));
    gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(pui->convert_unknown_8bit[0]),
		     0, 2, 0, 1,
		     (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
    radio_group = 
	gtk_radio_button_get_group(GTK_RADIO_BUTTON(pui->convert_unknown_8bit[0]));
    
    pui->convert_unknown_8bit[1] =
	GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(radio_group,
							 _("display in codeset")));
    gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(pui->convert_unknown_8bit[1]),
		     0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
    
    pui->convert_unknown_8bit_codeset = libbalsa_charset_button_new();
    gtk_combo_box_set_active(GTK_COMBO_BOX
                             (pui->convert_unknown_8bit_codeset),
                             balsa_app.convert_unknown_8bit_codeset);
    gtk_table_attach(GTK_TABLE(table), pui->convert_unknown_8bit_codeset,
                     1, 2, 1, 2,
                     GTK_EXPAND | GTK_FILL,
                     (GtkAttachOptions) (0), 0, 0);

    pm_page_add_to_size_group(page,
                              GTK_WIDGET(pui->convert_unknown_8bit[1]));
    
    return group;
}


static GtkWidget *
mdn_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *label;
    GtkWidget *table;

    /* How to handle received MDN requests */

    group = pm_group_new(_("Message Disposition Notification Requests"));

    label = gtk_label_new(_("When I receive a message whose sender "
                            "requested a "
                            "Message Disposition Notification (MDN), "
                            "send it if:"));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    pm_group_add(group, label, FALSE);

    table = create_table(2, 2, page);
    pm_group_add(group, table, FALSE);

    label = gtk_label_new(_("The message header looks clean\n"
                            "(the notify-to address is the return path,\n"
                            "and I am in the \"To:\" or \"Cc:\" list)."));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
                     GTK_FILL, 0, 0, 0);
    pm_page_add_to_size_group(page, label);

    pui->mdn_reply_clean_menu = create_mdn_reply_menu();
    pm_combo_box_set_level(pui->mdn_reply_clean_menu,
                           balsa_app.mdn_reply_clean);
    gtk_table_attach(GTK_TABLE(table), pui->mdn_reply_clean_menu,
                     1, 2, 0, 1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("The message header looks suspicious."));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
                     GTK_FILL, (GtkAttachOptions) (0), 0, 0);
    pm_page_add_to_size_group(page, label);

    pui->mdn_reply_notclean_menu = create_mdn_reply_menu();
    pm_combo_box_set_level(pui->mdn_reply_notclean_menu,
                           balsa_app.mdn_reply_notclean);
    gtk_table_attach(GTK_TABLE(table), pui->mdn_reply_notclean_menu,
                     1, 2, 1, 2,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    return group;
}

static GtkWidget *
outgoing_subpage(void)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, word_wrap_group(page), FALSE);
    pm_page_add(page, other_options_group(page), FALSE);

    return page;
}

static GtkWidget *
word_wrap_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GtkObject *spinbutton_adj;
    GtkWidget *label;

    group = pm_group_new(_("Word Wrap"));
    table = create_table(1, 3, page);
    pm_group_add(group, table, FALSE);

    pui->wordwrap =
	gtk_check_button_new_with_label(_("Wrap Outgoing Text at"));
    gtk_table_attach(GTK_TABLE(table), pui->wordwrap, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    pm_page_add_to_size_group(page, pui->wordwrap);

    spinbutton_adj =
        gtk_adjustment_new(1.0, 40.0, 79.0, 1.0, 5.0, 0.0);
    pui->wraplength =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table), pui->wraplength, 1, 2, 0, 1,
		     GTK_EXPAND | GTK_FILL, (GtkAttachOptions) (0), 0, 0);
    gtk_widget_set_sensitive(pui->wraplength, FALSE);

    label = gtk_label_new(_("characters"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1,
		     GTK_FILL, (GtkAttachOptions) (0), 0, 0);

    return group;
}

static GtkWidget *
other_options_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;

    group = pm_group_new(_("Other Options"));

    table = create_table(1, 2, page);
    pm_group_add(group, table, FALSE);

    pui->quote_str = attach_entry(_("Reply Prefix:"), 0, table);

    pui->autoquote =
        pm_group_add_check(group, _("Automatically quote original "
                                    "when replying"));
    pui->forward_attached =
        pm_group_add_check(group, _("Forward a mail as attachment "
                                    "instead of quoting it"));
    pui->copy_to_sentbox =
        pm_group_add_check(group, _("Copy outgoing messages to sentbox"));
    pui->always_queue_sent_mail =
        pm_group_add_check(group, _("Send button always queues "
                                    "outgoing mail in outbox"));
    pui->edit_headers =
        pm_group_add_check(group, _("Edit headers in external editor"));
    pui->reply_include_html_parts =
        pm_group_add_check(group, _("Include HTML parts as text "
                                    "when replying or forwarding"));

    return group;
}

static GtkWidget *
create_display_page(GtkTreeStore * store)
{
    GtkWidget *notebook;
    GtkTreeIter iter;

    notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);

    gtk_tree_store_append(store, &iter, NULL);
    pm_append_page(notebook, display_subpage(), _("Layout"),
                   store, &iter);
    pm_append_page(notebook, threading_subpage(), _("Sort and Thread"),
                   store, &iter);
    pm_append_page(notebook, message_subpage(), _("Message"),
                   store, &iter);
    pm_append_page(notebook, colors_subpage(), _("Colors"),
                   store, &iter);
    pm_append_page(notebook, format_subpage(), _("Format"),
                   store, &iter);
    pm_append_page(notebook, status_messages_subpage(), _("Status Messages"),
                   store, &iter);

    return notebook;
}

static GtkWidget *
display_subpage(void)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, main_window_group(page), FALSE);
    pm_page_add(page, message_window_group(page), FALSE);

    return page;
}

static GtkWidget *
main_window_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GtkObject *scroll_adj;
    GtkWidget *label;

    group = pm_group_new(_("Main Window"));

    pui->previewpane =
        pm_group_add_check(group, _("Use preview pane"));
    pui->mblist_show_mb_content_info =
        pm_group_add_check(group, _("Show mailbox statistics in left pane"));
    pui->alternative_layout =
        pm_group_add_check(group, _("Use alternative main window layout"));
    pui->view_message_on_open =
        pm_group_add_check(group, _("Automatically view message "
                                    "when mailbox opened"));

    table = create_table(1, 3, page);
    pm_group_add(group, table, FALSE);
    pui->pgdownmod =
        gtk_check_button_new_with_label(_("PageUp/PageDown keys "
                                          "scroll text by"));
    gtk_table_attach(GTK_TABLE(table), pui->pgdownmod, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    scroll_adj = gtk_adjustment_new(50.0, 10.0, 100.0, 5.0, 10.0, 0.0);
    pui->pgdown_percent =
	 gtk_spin_button_new(GTK_ADJUSTMENT(scroll_adj), 1, 0);
    gtk_widget_set_sensitive(pui->pgdown_percent, FALSE);
    gtk_table_attach(GTK_TABLE(table), pui->pgdown_percent, 1, 2, 0, 1,
		     GTK_EXPAND | GTK_FILL, (GtkAttachOptions) (0), 0, 0);
    label = gtk_label_new(_("percent"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1,
		     GTK_FILL, (GtkAttachOptions) (0), 0, 0);

    return group;
}

static GtkWidget *
progress_group(GtkWidget * page)
{
    GtkWidget *group;
    GSList *radio_group;
    gint i;

    group = pm_group_new(_("Display Progress Dialog"));

    radio_group = NULL;
    for (i = 0; i < NUM_PWINDOW_MODES; i++) {
	pui->pwindow_type[i] =
	    GTK_RADIO_BUTTON(gtk_radio_button_new_with_label
			     (radio_group, _(pwindow_type_label[i])));
	pm_group_add(group, GTK_WIDGET(pui->pwindow_type[i]), FALSE);
	radio_group = gtk_radio_button_get_group(pui->pwindow_type[i]);
    }

    return group;
}

static GtkWidget *
display_formats_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;

    group = pm_group_new(_("Format"));
    table = create_table(2, 2, page);
    pm_group_add(group, table, FALSE);
    
    pui->date_format =
        attach_entry(_("Date encoding (for strftime):"), 0, table);
    pui->selected_headers =
        attach_entry(_("Selected headers:"), 1, table);

    return group;
}

static GtkWidget *
status_messages_subpage(void)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, information_messages_group(page), FALSE);
    pm_page_add(page, progress_group(page), FALSE);

    return page;
}

static GtkWidget *
information_messages_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;

    group = pm_group_new(_("Information Messages"));
    table = create_table(5, 2, page);
    pm_group_add(group, table, FALSE);
    
    pui->information_message_menu = 
	attach_information_menu(_("Information Messages:"), 0, 
				GTK_TABLE(table),
				balsa_app.information_message);
    pui->warning_message_menu =
	attach_information_menu(_("Warning Messages:"), 1,
				GTK_TABLE(table),
				balsa_app.warning_message);
    pui->error_message_menu = 
	attach_information_menu(_("Error Messages:"), 2,
				GTK_TABLE(table),
				balsa_app.error_message);
    pui->fatal_message_menu = 
	attach_information_menu(_("Fatal Error Messages:"), 3,
				GTK_TABLE(table), 
				balsa_app.fatal_message);
    pui->debug_message_menu = 
	attach_information_menu(_("Debug Messages:"), 4,
				GTK_TABLE(table),
				balsa_app.debug_message);

    return group;
}

static GtkWidget *
colors_subpage(void)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, message_colors_group(page), FALSE);
    pm_page_add(page, link_color_group(page), FALSE);
    pm_page_add(page, composition_window_group(page), FALSE);

    return page;
}

static GtkWidget *
message_colors_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *vbox;
    gint i;
    
    group = pm_group_new(_("Message Colors"));
    vbox = gtk_vbox_new(TRUE, HIG_PADDING);
    pm_group_add(group, vbox, FALSE);

    for(i = 0; i < MAX_QUOTED_COLOR; i++) {
        gchar *text = g_strdup_printf(_("Quote level %d color"), i+1);
        pui->quoted_color[i] = color_box( GTK_BOX(vbox), text);
        g_free(text);
    }

    return group;
}

static GtkWidget *
link_color_group(GtkWidget * page)
{
    GtkWidget *group;

    group = pm_group_new(_("Link Color"));
    pui->url_color =
        color_box(GTK_BOX(pm_group_get_vbox(group)), _("Hyperlink color"));

    return group;
}

static GtkWidget *
composition_window_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *vbox;

    group = pm_group_new(_("Composition Window"));
    vbox = pm_group_get_vbox(group);
    pui->bad_address_color =
        color_box(GTK_BOX(vbox),
                  _("Invalid or incomplete address label color"));

    return group;
}

static GtkWidget *
message_subpage(void)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, preview_font_group(page), FALSE);
    pm_page_add(page, quoted_group(page), FALSE);
    pm_page_add(page, alternative_group(page), FALSE);

    return page;
}

static GtkWidget *
preview_font_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;

    group = pm_group_new(_("Fonts"));
    table = create_table(2, 2, page);
    pm_group_add(group, table, FALSE);

    attach_label(_("Message Font:"), table, 0, page);
    pui->message_font_button =
	gtk_font_button_new_with_font(balsa_app.message_font);
    gtk_table_attach(GTK_TABLE(table), pui->message_font_button, 
                     1, 2, 0, 1,
		     GTK_EXPAND | GTK_FILL,
		     (GtkAttachOptions) (GTK_FILL), 0, 0);

    attach_label(_("Subject Font:"), table, 1, page);
    pui->subject_font_button =
	gtk_font_button_new_with_font(balsa_app.subject_font);
    gtk_table_attach(GTK_TABLE(table), pui->subject_font_button, 
                     1, 2, 1, 2,
		     GTK_EXPAND | GTK_FILL,
		     (GtkAttachOptions) (GTK_FILL), 0, 0);

    return group;
}

static GtkWidget *
format_subpage(void)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, display_formats_group(page), FALSE);
    pm_page_add(page, broken_8bit_codeset_group(page), FALSE);

    return page;
}

static GtkWidget *
threading_subpage(void)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, threading_group(page), FALSE);

    return page;
}

static GtkWidget *
threading_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *vbox;

    group = pm_group_new(_("Sorting and Threading"));
    
    vbox = pm_group_get_vbox(group);
    pui->default_sort_field = 
        add_pref_menu(_("Default sort column:"), sort_field_label, 
                      ELEMENTS(sort_field_label), &pui->sort_field_index, 
                      GTK_BOX(vbox), 2 * HIG_PADDING, page);
    pui->default_threading_type = 
        add_pref_menu(_("Default threading style:"), threading_type_label, 
                      NUM_THREADING_STYLES, &pui->threading_type_index, 
                      GTK_BOX(vbox), 2 * HIG_PADDING, page);

    pui->tree_expand_check =
        pm_group_add_check(group, _("Expand threads on open"));
    
    return group;
}

static GtkWidget*
add_pref_menu(const gchar* label, const gchar *names[], gint size, 
	      gint *index, GtkBox* parent, gint padding, GtkWidget * page)
{
    GtkWidget *omenu;
    GtkWidget *hbox, *lbw;

    omenu = create_pref_option_menu(names, size, index);

    hbox = gtk_hbox_new(FALSE, padding);
    lbw = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(lbw), 0, 0.5);
    pm_page_add_to_size_group(page, lbw);
    gtk_box_pack_start(GTK_BOX(hbox), lbw,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), omenu, TRUE,  TRUE,  0);
    gtk_box_pack_start(parent,        hbox,  FALSE, FALSE, 0);
    return omenu;
}

static GtkWidget *
create_table(gint rows, gint cols, GtkWidget * page)
{
    GtkWidget *table;

    table = gtk_table_new(rows, cols, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), ROW_SPACING);
    gtk_table_set_col_spacings(GTK_TABLE(table), COL_SPACING);
    g_object_set_data(G_OBJECT(table), BALSA_TABLE_PAGE_KEY, page);

    return table;
}

#if !HAVE_GTKSPELL
static GtkWidget *
attach_pref_menu(const gchar * label, gint row, GtkTable * table,
                 const gchar * names[], gint size, gint * index)
{
    GtkWidget *w, *omenu;

    w = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(w), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), w, 0, 1, row, row + 1,
                     GTK_FILL, 0, 0, 0);

    omenu = create_pref_option_menu(names, size, index);
    gtk_table_attach(GTK_TABLE(table), omenu, 1, 2, row, row + 1,
                     GTK_FILL, 0, 0, 0);

    return omenu;
}

static GtkWidget *
create_spelling_page(GtkTreeStore * store)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, pspell_settings_group(page), FALSE);
    pm_page_add(page, misc_spelling_group(page), FALSE);

    return page;
}

static GtkWidget *
pspell_settings_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkObject *ignore_adj;
    GtkWidget *table;
    GtkWidget *hbox;

    group = pm_group_new(_("Pspell Settings"));
    table = create_table(3, 2, page);
    pm_group_add(group, table, FALSE);

    /* do the module menu */
    pui->module =
        attach_pref_menu(_("Spell Check Module"), 0, GTK_TABLE(table),
                         spell_check_modules_name, NUM_PSPELL_MODULES,
                         &pui->module_index);

    /* do the suggestion modes menu */
    pui->suggestion_mode =
        attach_pref_menu(_("Suggestion Level"), 1, GTK_TABLE(table),
                         spell_check_suggest_mode_label, NUM_SUGGEST_MODES,
                         &pui->suggestion_mode_index);

    /* do the ignore length */
    attach_label(_("Ignore words shorter than"), table, 2, NULL);
    ignore_adj = gtk_adjustment_new(0.0, 0.0, 99.0, 1.0, 5.0, 0.0);
    pui->ignore_length =
        gtk_spin_button_new(GTK_ADJUSTMENT(ignore_adj), 1, 0);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), pui->ignore_length, FALSE, FALSE, 0);
    gtk_table_attach(GTK_TABLE(table), hbox, 1, 2, 2, 3,
                     GTK_FILL, 0, 0, 0);

    return group;
}

static GtkWidget *
misc_spelling_group(GtkWidget * page)
{
    GtkWidget *group;

    group = pm_group_new(_("Miscellaneous Spelling Settings"));

    pui->spell_check_sig = pm_group_add_check(group, _("Check signature"));
    pui->spell_check_quoted = pm_group_add_check(group, _("Check quoted"));

    return group;
}
#endif                          /* HAVE_GTKSPELL */

static GtkWidget *
create_misc_page(GtkTreeStore * store)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, misc_group(page), FALSE);
    pm_page_add(page, deleting_messages_group(page), FALSE);

    return page;
}

static GtkWidget *
misc_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *label;
    GtkWidget *hbox;
    GtkObject *close_spinbutton_adj;

    group = pm_group_new(_("Miscellaneous"));

    pui->debug = pm_group_add_check(group, _("Debug"));
    pui->empty_trash = pm_group_add_check(group, _("Empty Trash on exit"));

    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox, FALSE);

    pui->close_mailbox_auto =
	gtk_check_button_new_with_label(_("Close mailbox "
                                          "if unused more than"));
    gtk_box_pack_start(GTK_BOX(hbox), pui->close_mailbox_auto,
                       FALSE, FALSE, 0);
    pm_page_add_to_size_group(page, pui->close_mailbox_auto);

    close_spinbutton_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 10);
    pui->close_mailbox_minutes =
	gtk_spin_button_new(GTK_ADJUSTMENT(close_spinbutton_adj), 1, 0);
    gtk_widget_show(pui->close_mailbox_minutes);
    gtk_widget_set_sensitive(pui->close_mailbox_minutes, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), pui->close_mailbox_minutes,
                       TRUE, TRUE, 0);

    label = gtk_label_new(_("minutes"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);

    return group;
}

static GtkWidget *
deleting_messages_group(GtkWidget * page)
{
    GtkWidget *group;
    gchar *text;
    GtkWidget *label;
    GtkWidget *hbox;
    GtkObject *expunge_spinbutton_adj;

    group = pm_group_new(_("Deleting Messages"));

    /* Translators: this used to be "using Mailbox -> Hide messages";
     * the UTF-8 string for the right-arrow symbol is broken out to
     * avoid msgconv problems. */
    text = g_strdup_printf(_("The following setting is global, "
			     "but may be overridden "
			     "for the selected mailbox "
			     "using Mailbox %s Hide messages:"),
			   "\342\226\272");
    label = gtk_label_new(text);
    g_free(text);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
    pm_group_add(group, label, FALSE);
    pui->hide_deleted =
        pm_group_add_check(group, _("Hide messages marked as deleted"));

    label = gtk_label_new(_("The following settings are global:"));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
    pm_group_add(group, label, FALSE);
    pui->expunge_on_close =
        pm_group_add_check(group, _("Expunge deleted messages "
				    "when mailbox is closed"));

    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox, FALSE);

    pui->expunge_auto =
	gtk_check_button_new_with_label(_("...and if unused more than"));
    gtk_box_pack_start(GTK_BOX(hbox), pui->expunge_auto,
                       FALSE, FALSE, 0);
    pm_page_add_to_size_group(page, pui->expunge_auto);

    expunge_spinbutton_adj = gtk_adjustment_new(120, 1, 1440, 1, 10, 10);
    pui->expunge_minutes =
	gtk_spin_button_new(GTK_ADJUSTMENT(expunge_spinbutton_adj), 1, 0);
    gtk_widget_show(pui->expunge_minutes);
    gtk_widget_set_sensitive(pui->expunge_minutes, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), pui->expunge_minutes,
                       TRUE, TRUE, 0);

    label = gtk_label_new(_("minutes"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);

    return group;
}

static GtkWidget *
message_window_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;

    group = pm_group_new(_("Message Window"));

    table = create_table(1, 2, page);
    pm_group_add(group, table, FALSE);

    attach_label(_("After moving a message:"), table, 0, NULL);

    pui->action_after_move_menu = create_action_after_move_menu();
    pm_combo_box_set_level(pui->action_after_move_menu,
                           balsa_app.mw_action_after_move);
    gtk_table_attach(GTK_TABLE(table), pui->action_after_move_menu,
                     1, 2, 0, 1,
                     GTK_EXPAND | GTK_FILL,
		     (GtkAttachOptions) (0), 0, 0);

    return group;
}

static GtkWidget *
create_startup_page(GtkTreeStore * store)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, options_group(page), FALSE);
    pm_page_add(page, folder_scanning_group(page), FALSE);

    return page;
}

static GtkWidget *
options_group(GtkWidget * page)
{
    GtkWidget *group;

    group = pm_group_new(_("Startup Options"));

    pui->open_inbox_upon_startup =
        pm_group_add_check(group, _("Open Inbox upon startup"));
    pui->check_mail_upon_startup =
        pm_group_add_check(group, _("Check mail upon startup"));
    pui->remember_open_mboxes =
        pm_group_add_check(group, _("Remember open mailboxes "
                                    "between sessions"));

    return group;
}

static GtkWidget *
folder_scanning_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *label;
    GtkWidget *hbox;
    GtkObject *scan_adj;

    group = pm_group_new(_("Folder Scanning"));

    label = gtk_label_new(_("Choose depth 1 for fast startup; "
                            "this defers scanning some folders.  "
                            "To see more of the tree at startup, "
                            "choose a greater depth."));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
    pm_group_add(group, label, FALSE);

    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox, FALSE);
    label = gtk_label_new(_("Scan local folders to depth"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    pm_page_add_to_size_group(page, label);
    gtk_box_pack_start(GTK_BOX(hbox), label,
                       FALSE, FALSE, 0);
    scan_adj = gtk_adjustment_new(1.0, 1.0, 99.0, 1.0, 5.0, 0.0);
    pui->local_scan_depth =
        gtk_spin_button_new(GTK_ADJUSTMENT(scan_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), pui->local_scan_depth,
                       TRUE, TRUE, 0);

    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox, FALSE);
    label = gtk_label_new(_("Scan IMAP folders to depth"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    pm_page_add_to_size_group(page, label);
    gtk_box_pack_start(GTK_BOX(hbox), label,
                       FALSE, FALSE, 0);
    scan_adj = gtk_adjustment_new(1.0, 1.0, 99.0, 1.0, 5.0, 0.0);
    pui->imap_scan_depth =
        gtk_spin_button_new(GTK_ADJUSTMENT(scan_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), pui->imap_scan_depth,
                       TRUE, TRUE, 0);

    return group;
}

static GtkWidget *
create_address_book_page(GtkTreeStore * store)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, address_books_group(page), TRUE);

    return page;
}

static GtkWidget *
address_books_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkWidget *hbox;
    GtkWidget *scrolledwindow;
    GtkWidget *vbox;

    group = pm_group_new(_("Address Books"));
    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox, TRUE);

    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), scrolledwindow, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolledwindow, -1, 150);

    store = gtk_list_store_new(AB_N_COLUMNS,
                               G_TYPE_STRING,   /* AB_TYPE_COLUMN */
                               G_TYPE_STRING,   /* AB_NAME_COLUMN */
                               G_TYPE_BOOLEAN,  /* AB_XPND_COLUMN */
                               G_TYPE_POINTER); /* AB_DATA_COLUMN */
    pui->address_books = tree_view =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Type"),
                                                 renderer,
                                                 "text", AB_TYPE_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Address Book Name"),
                                                 renderer,
                                                 "text", AB_NAME_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    renderer = gtk_cell_renderer_toggle_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Auto-complete"),
                                                 renderer,
                                                 "active",
                                                 AB_XPND_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection
                                (GTK_TREE_VIEW(tree_view)),
                                GTK_SELECTION_BROWSE);

    g_signal_connect(tree_view, "row-activated", 
                     G_CALLBACK(address_book_edit_cb), NULL);

    gtk_container_add(GTK_CONTAINER(scrolledwindow), tree_view);

    vbox = vbox_in_container(hbox);
    add_button_to_box(_("_Add"),
                      G_CALLBACK(address_book_add_cb),
                      NULL, vbox);
    add_button_to_box(_("_Modify"),
                      G_CALLBACK(address_book_edit_cb),
                      tree_view, vbox);
    add_button_to_box(_("_Delete"),         
                      G_CALLBACK(address_book_delete_cb),
                      tree_view, vbox);
    add_button_to_box(_("_Set as default"), 
                      G_CALLBACK(address_book_set_default_cb),
                      tree_view, vbox);

    update_address_books();

    return group;
}


/*
 * callbacks
 */
static void
properties_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gtk_dialog_set_response_sensitive(GTK_DIALOG(pbox), GTK_RESPONSE_OK,
                                      TRUE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(pbox), GTK_RESPONSE_APPLY,
                                      TRUE);
}

static void
server_edit_cb(GtkTreeView * tree_view)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, MS_DATA_COLUMN, &mbnode, -1);
    g_return_if_fail(mbnode);
    balsa_mailbox_node_show_prop_dialog(mbnode);
}

#if ENABLE_ESMTP
/* SMTP server callbacks */

/* Clear and populate the list. */
static void
update_smtp_servers(void)
{
    GtkTreeView *tree_view;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    GtkTreePath *path = NULL;
    GtkTreeModel *model;
    GSList *list;

    if (pui == NULL)
        return;

    tree_view = GTK_TREE_VIEW(pui->smtp_servers);
    selection = gtk_tree_view_get_selection(tree_view);
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        path = gtk_tree_model_get_path(model, &iter);

    gtk_list_store_clear(GTK_LIST_STORE(model));

    for (list = balsa_app.smtp_servers; list; list = list->next) {
        LibBalsaSmtpServer *smtp_server = LIBBALSA_SMTP_SERVER(list->data);
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           0, libbalsa_smtp_server_get_name(smtp_server),
                           1, smtp_server, -1);
    }

    if (path) {
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_path_free(path);
    }
}

#define SMTP_SERVER_SECTION_PREFIX "smtp-server-"

/* Callback for the server-dialog's response handler. */
static void
smtp_server_update(LibBalsaSmtpServer * smtp_server,
                   GtkResponseType response, const gchar * old_name)
{
    gchar *group;
    const gchar *new_name;

    new_name = libbalsa_smtp_server_get_name(smtp_server);

    if (old_name) {
        /* We were editing an existing server. */
        if (strcmp(old_name, new_name) == 0)
	    return;
	else {
            /* Name was changed. */
            group =
                g_strconcat(SMTP_SERVER_SECTION_PREFIX, old_name, NULL);
            libbalsa_conf_remove_group(group);
            g_free(group);
        }
    } else {
        /* Populating a new server. */
        if (response == GTK_RESPONSE_OK)
            libbalsa_smtp_server_add_to_list(smtp_server,
                                             &balsa_app.smtp_servers);
        else {
            /*  The user killed the dialog. */
            g_object_unref(smtp_server);
            return;
        }
    }

    update_smtp_servers();

    group = g_strconcat(SMTP_SERVER_SECTION_PREFIX, new_name, NULL);
    libbalsa_conf_push_group(group);
    g_free(group);
    libbalsa_smtp_server_save_config(smtp_server);
    libbalsa_conf_pop_group();
}

static void
smtp_server_add_cb(void)
{
    LibBalsaSmtpServer *smtp_server;

    smtp_server = libbalsa_smtp_server_new();
    libbalsa_smtp_server_dialog(smtp_server,
                                GTK_WINDOW(property_box),
                                smtp_server_update);
}

static void
smtp_server_edit_cb(GtkTreeView * tree_view)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    LibBalsaSmtpServer *smtp_server;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 1, &smtp_server, -1);
    g_return_if_fail(smtp_server);
    libbalsa_smtp_server_dialog(smtp_server,
                                GTK_WINDOW(property_box),
                                smtp_server_update);
}

static void
smtp_server_del_cb(GtkTreeView * tree_view)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    LibBalsaSmtpServer *smtp_server;
    gchar *group;

    /* Nothing to do if no server is selected, or if it is the last one. */
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)
        || gtk_tree_model_iter_n_children(model, NULL) <= 1)
        return;

    gtk_tree_model_get(model, &iter, 1, &smtp_server, -1);
    g_return_if_fail(smtp_server);

    group = g_strconcat(SMTP_SERVER_SECTION_PREFIX,
                        libbalsa_smtp_server_get_name(smtp_server), NULL);
    libbalsa_conf_remove_group(group);
    g_free(group);

    balsa_app.smtp_servers =
        g_slist_remove(balsa_app.smtp_servers, smtp_server);
    g_object_unref(smtp_server);
    update_smtp_servers();
}

/* Set sensitivity of the Modify and Delete buttons; we can edit a server
 * only if one is selected, and we can delete one only if it is selected
 * and it is not the last server. */
static void
smtp_server_changed(GtkTreeSelection * selection, gpointer user_data)
{
    gboolean selected;
    GtkTreeModel *model;

    selected = gtk_tree_selection_get_selected(selection, &model, NULL);
    gtk_widget_set_sensitive(pui->smtp_server_edit_button, selected);
    gtk_widget_set_sensitive(pui->smtp_server_del_button,
                             selected
                             && gtk_tree_model_iter_n_children(model,
                                                               NULL) > 1);
}
#endif                          /* ENABLE_ESMTP */

/* Address book callbacks */

static void
address_book_change(LibBalsaAddressBook * address_book, gboolean append)
{
    if (append) {
        balsa_app.address_book_list =
            g_list_append(balsa_app.address_book_list, address_book);
	balsa_window_update_book_menus(balsa_app.main_window);
    }
    config_address_book_save(address_book);
    update_address_books();
}

static void
address_book_edit_cb(GtkTreeView * tree_view)
{
    LibBalsaAddressBook *address_book;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, AB_DATA_COLUMN, &address_book, -1);

    g_assert(address_book != NULL);

    balsa_address_book_config_new(address_book, address_book_change,
                                  GTK_WINDOW(property_box));
}

static void
address_book_set_default_cb(GtkTreeView * tree_view)
{
    LibBalsaAddressBook *address_book;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, AB_DATA_COLUMN, &address_book, -1);

    g_assert(address_book != NULL);
    balsa_app.default_address_book = address_book;

    path = gtk_tree_model_get_path(model, &iter);
    update_address_books();
    gtk_tree_selection_select_path(selection, path);
    gtk_tree_path_free(path);
}

static void
address_book_add_cb(void)
{

    GtkWidget *menu =
        balsa_address_book_add_menu(address_book_change,
                                    GTK_WINDOW(property_box));

    gtk_widget_show_all(menu);
#if GLIB_CHECK_VERSION(2, 10, 0)
    g_object_ref_sink(menu);
#else                           /* GLIB_CHECK_VERSION(2, 10, 0) */
    g_object_ref(menu);
    gtk_object_sink(GTK_OBJECT(menu));
#endif                          /* GLIB_CHECK_VERSION(2, 10, 0) */
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, 0);
    g_object_unref(menu);
}

static void
address_book_delete_cb(GtkTreeView * tree_view)
{
    LibBalsaAddressBook *address_book;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, AB_DATA_COLUMN, &address_book, -1);

    g_assert(address_book != NULL);

    config_address_book_delete(address_book);
    balsa_app.address_book_list =
	g_list_remove(balsa_app.address_book_list, address_book);
    balsa_window_update_book_menus(balsa_app.main_window);
    if (balsa_app.default_address_book == address_book)
	balsa_app.default_address_book = NULL;

    g_object_unref(address_book);

    update_address_books();
}

static void
pop3_add_cb(void)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_POP3);
}

static void
server_add_cb(void)
{
    GtkWidget *menu;
    GtkWidget *menuitem;

    menu = gtk_menu_new();
    menuitem = gtk_menu_item_new_with_label(_("Remote POP3 mailbox..."));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(pop3_add_cb), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Remote IMAP mailbox..."));
    g_signal_connect(G_OBJECT(menuitem), "activate",
		     G_CALLBACK(mailbox_conf_add_imap_cb), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Remote IMAP folder..."));
    g_signal_connect(G_OBJECT(menuitem), "activate",
		     G_CALLBACK(folder_conf_add_imap_cb), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show(menuitem);
    gtk_widget_show(menu);
#if GLIB_CHECK_VERSION(2, 10, 0)
    g_object_ref_sink(menu);
#else                           /* GLIB_CHECK_VERSION(2, 10, 0) */
    g_object_ref(menu);
    gtk_object_sink(GTK_OBJECT(menu));
#endif                          /* GLIB_CHECK_VERSION(2, 10, 0) */
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, 0);
    g_object_unref(menu);
}

static void
server_del_cb(GtkTreeView * tree_view)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, MS_DATA_COLUMN, &mbnode, -1);
    g_return_if_fail(mbnode);

    if (mbnode->mailbox)
	mailbox_conf_delete(mbnode);
    else
	folder_conf_delete(mbnode);
}

void
timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate = 
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->check_mail_auto));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->check_mail_minutes), newstate);
    properties_modified_cb(widget, pbox);
}

static void
browse_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->browse_wrap));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->browse_wrap_length), newstate);
    properties_modified_cb(widget, pbox);
}

static void
wrap_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->wordwrap));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->wraplength), newstate);
    properties_modified_cb(widget, pbox);
}

static void
pgdown_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->pgdownmod));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->pgdown_percent), newstate);
    properties_modified_cb(widget, pbox);
}

static void
option_menu_cb(GtkItem * widget, gpointer data)
{
    /* update the index number */
    gint *index = (gint *) data;
    *index = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
}

static GtkWidget *
create_pref_option_menu(const gchar * names[], gint size, gint * index)
{
    GtkWidget *combo_box;
    gint i;

    combo_box = pm_combo_box_new();
    g_signal_connect(G_OBJECT(combo_box), "changed",
                     G_CALLBACK(option_menu_cb), index);

    for (i = 0; i < size; i++)
	add_show_menu(_(names[i]), i, combo_box);

    return combo_box;
}


static void
add_show_menu(const char* label, gint level, GtkWidget* menu)
{
    struct pm_combo_box_info *info =
        g_object_get_data(G_OBJECT(menu), PM_COMBO_BOX_INFO);

    gtk_combo_box_append_text(GTK_COMBO_BOX(menu), label);
    info->levels = g_slist_append(info->levels, GINT_TO_POINTER(level));
}

static GtkWidget *
create_information_message_menu(void)
{
    GtkWidget *combo_box = pm_combo_box_new();

    add_show_menu(_("Show nothing"),       BALSA_INFORMATION_SHOW_NONE,
                  combo_box);
    add_show_menu(_("Show dialog"),        BALSA_INFORMATION_SHOW_DIALOG,
                  combo_box);
    add_show_menu(_("Show in list"),       BALSA_INFORMATION_SHOW_LIST,
                  combo_box);
    add_show_menu(_("Show in status bar"), BALSA_INFORMATION_SHOW_BAR,
                  combo_box);
    add_show_menu(_("Print to console"),   BALSA_INFORMATION_SHOW_STDERR,
                  combo_box);

    return combo_box;
}

static GtkWidget *
create_mdn_reply_menu(void)
{
    GtkWidget *combo_box = pm_combo_box_new();
    add_show_menu(_("Never"),  BALSA_MDN_REPLY_NEVER,  combo_box);
    add_show_menu(_("Ask me"), BALSA_MDN_REPLY_ASKME,  combo_box);
    add_show_menu(_("Always"), BALSA_MDN_REPLY_ALWAYS, combo_box);
    return combo_box;
}

void
mailbox_close_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
	    pui->close_mailbox_auto));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->close_mailbox_minutes),
			     newstate);

    properties_modified_cb(widget, pbox);
}

static void
filter_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->hide_deleted)))
	pui->filter |= (1 << 0);
    else
	pui->filter &= ~(1 << 0);

    properties_modified_cb(widget, pbox);
}

static void
expunge_on_close_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->expunge_on_close));
    gtk_widget_set_sensitive(GTK_WIDGET(pui->expunge_auto), newstate);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->expunge_auto),
                                 newstate);

    properties_modified_cb(widget, pbox);
}

static void
expunge_auto_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->expunge_auto));
    gtk_widget_set_sensitive(GTK_WIDGET(pui->expunge_minutes), newstate);

    properties_modified_cb(widget, pbox);
}

static void imap_toggled_cb(GtkWidget * widget, GtkWidget * pbox)
{
    properties_modified_cb(widget, pbox);

    if(GTK_TOGGLE_BUTTON(pui->check_imap)->active) 
	gtk_widget_set_sensitive(GTK_WIDGET(pui->check_imap_inbox), TRUE);
    else {
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(pui->check_imap_inbox), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(pui->check_imap_inbox), FALSE);
    }
}

static void 
convert_8bit_cb(GtkWidget * widget, GtkWidget * pbox)
{
    properties_modified_cb(widget, pbox);

    gtk_widget_set_sensitive(pui->convert_unknown_8bit_codeset,
			     GTK_TOGGLE_BUTTON(pui->convert_unknown_8bit[1])->active);
}

static GtkWidget *
create_action_after_move_menu(void)
{
    GtkWidget *combo_box = pm_combo_box_new();
    add_show_menu(_("Show next unread message"), NEXT_UNREAD, combo_box);
    add_show_menu(_("Show next message"), NEXT, combo_box);
    add_show_menu(_("Close message window"), CLOSE, combo_box);
    return combo_box;
}

/* refresh any data displayed in the preferences manager
 * window in case it has changed */
void
refresh_preferences_manager(void)
{
    if (pui == NULL)
        return;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->browse_wrap),
                                 balsa_app.browse_wrap);
}

static void
balsa_help_pbox_display(void)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *text, *p;
    gchar *link_id;
    GError *err = NULL;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(pui->view));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, PM_HELP_COL, &text, -1);
    for (p = text; *p; p++)
        *p = (*p == ' ') ? '-' : g_ascii_tolower(*p);
    link_id = g_strconcat("preferences-", text, NULL);
    g_free(text);

    gnome_help_display("balsa", link_id, &err);
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
		_("Error displaying link_id %s: %s\n"),
		link_id, err->message);
        g_error_free(err);
    }

    g_free(link_id);
}

/* pm_page: methods for making the contents of a notebook page
 *
 * pm_page_new:            creates a vbox with the desired border width
 *                         and inter-group spacing
 *
 * pm_page_add:            adds a child to the contents
 *
 * pm_page_get_size_group: get the GtkSizeGroup for the page; this is
 *                         used to make all left-most widgets the same
 *                         size, to line up the second widget in each row
 *
 * because we use size-groups to align widgets, we could pack each row
 * in an hbox instead of using tables, but the tables are convenient
 */
static GtkWidget *
pm_page_new(void)
{
    GtkWidget *page;
    GtkSizeGroup *size_group;

    page = gtk_vbox_new(FALSE, GROUP_SPACING);
    gtk_container_set_border_width(GTK_CONTAINER(page), BORDER_WIDTH);

    size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    g_object_set_data_full(G_OBJECT(page), BALSA_PAGE_SIZE_GROUP_KEY,
                           size_group, g_object_unref);

    return page;
}

static void
pm_page_add(GtkWidget * page, GtkWidget * child, gboolean expand)
{
    gtk_box_pack_start(GTK_BOX(page), child, expand, TRUE, 0);
}

static GtkSizeGroup *
pm_page_get_size_group(GtkWidget * page)
{
    return (GtkSizeGroup *) g_object_get_data(G_OBJECT(page),
                                              BALSA_PAGE_SIZE_GROUP_KEY);
}

static void
pm_page_add_to_size_group(GtkWidget * page, GtkWidget * child)
{
    GtkSizeGroup *size_group;

    size_group = pm_page_get_size_group(page);
    gtk_size_group_add_widget(size_group, child);
}

/* pm_group: methods for making groups of controls, to be added to a
 * page
 *
 * pm_group_new:       creates a box containing a bold title,
 *                     and an inner vbox that indents its contents
 *
 * pm_group_get_vbox:  returns the inner vbox
 *
 * pm_group_add:       adds a child to the inner vbox
 *
 * pm_group_add_check: uses box_start_check to create a check-box
 *                     with the given title, and adds it
 *                     to the group's vbox
 */
#define BALSA_GROUP_VBOX_KEY "balsa-group-vbox"
static GtkWidget *
pm_group_new(const gchar * text)
{
    GtkWidget *group;
    GtkWidget *label;
    gchar *markup;
    GtkWidget *hbox;
    GtkWidget *vbox;

    group = gtk_vbox_new(FALSE, HEADER_SPACING);

    label = gtk_label_new(NULL);
    markup = g_strdup_printf("<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(group), label, FALSE, FALSE, 0);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(group), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("    "),
                       FALSE, FALSE, 0);
    vbox = gtk_vbox_new(FALSE, ROW_SPACING);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(group), BALSA_GROUP_VBOX_KEY, vbox);

    return group;
}

static GtkWidget *
pm_group_get_vbox(GtkWidget * group)
{
    return GTK_WIDGET(g_object_get_data(G_OBJECT(group),
                                        BALSA_GROUP_VBOX_KEY));
}

static void
pm_group_add(GtkWidget * group, GtkWidget * child, gboolean expand)
{
    gtk_box_pack_start(GTK_BOX(pm_group_get_vbox(group)), child,
                       expand, TRUE, 0);
}

static GtkWidget *
pm_group_add_check(GtkWidget * group, const gchar * text)
{
    return box_start_check(text, pm_group_get_vbox(group));
}

/* combo boxes */

static void
pm_combo_box_info_free(struct pm_combo_box_info * info)
{
    g_slist_free(info->levels);
    g_free(info);
}

static GtkWidget *
pm_combo_box_new(void)
{
    GtkWidget *combo_box = gtk_combo_box_new_text();
    struct pm_combo_box_info *info = g_new0(struct pm_combo_box_info, 1);

    g_object_set_data_full(G_OBJECT(combo_box), PM_COMBO_BOX_INFO, info,
                           (GDestroyNotify) pm_combo_box_info_free);
    g_signal_connect(G_OBJECT(combo_box), "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    return combo_box;
}

static void
pm_combo_box_set_level(GtkWidget * combo_box, gint level)
{
    struct pm_combo_box_info *info =
        g_object_get_data(G_OBJECT(combo_box), PM_COMBO_BOX_INFO);
    GSList *list;
    guint i;

    for (list = info->levels, i = 0; list; list = list->next, ++i)
	if (GPOINTER_TO_INT(list->data) == level) {
	    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), i);
	    break;
	}
}

static gint
pm_combo_box_get_level(GtkWidget * combo_box)
{
    struct pm_combo_box_info *info =
        g_object_get_data(G_OBJECT(combo_box), PM_COMBO_BOX_INFO);
    gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_box));

    return GPOINTER_TO_INT(g_slist_nth_data(info->levels, active));
}

static void
pm_append_page(GtkWidget * notebook, GtkWidget * widget,
               const gchar * text, GtkTreeStore * store,
               GtkTreeIter * parent_iter)
{
    GtkTreeIter iter;
    guint page = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             widget, gtk_label_new(text));

    if (parent_iter && page == 0) {
        /* We'll show the first child when the parent row is selected,
         * so we don't create a row for it. */
        gtk_tree_store_set(store, parent_iter,
                           PM_CHILD_COL, notebook,
                           -1);
        return;
    }

    if (GTK_IS_NOTEBOOK(widget)) {
        /* The row for this widget was already created, to be the parent
         * of the notebook's pages. */
        GtkTreeModel *model = GTK_TREE_MODEL(store);
        gint n_children =
            gtk_tree_model_iter_n_children(model, parent_iter);
        gtk_tree_model_iter_nth_child(model, &iter, parent_iter,
                                      n_children - 1);
    } else
        gtk_tree_store_append(store, &iter, parent_iter);
    gtk_tree_store_set(store, &iter,
                       PM_TEXT_COL, _(text),
                       PM_HELP_COL, text,
                       PM_NOTEBOOK_COL, notebook,
                       PM_PAGE_COL, page,
                       -1);
}
