/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* MAKE SURE YOU USE THE HELPER FUNCTIONS, like create_grid(etc. */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "pref-manager.h"

#include "balsa-app.h"
#include "mailbox-conf.h"
#include "folder-conf.h"
#include "main-window.h"
#include "save-restore.h"
#include "spell-check.h"
#include "address-book-config.h"
#include "quote-color.h"
#include "misc.h"
#include "send.h"
#include "html.h"
#include "imap-server.h"

#include "smtp-server.h"
#include "libbalsa-conf.h"

#ifdef ENABLE_SYSTRAY
#include "system-tray.h"
#endif

#include <glib/gi18n.h>

#define NUM_ENCODING_MODES 3
#define NUM_PWINDOW_MODES 3
#define NUM_THREADING_STYLES 3
#define NUM_CONVERT_8BIT_MODES 2

/* Spacing suggestions from an old GNOME HIG
 */
#define BORDER_WIDTH    (2 * HIG_PADDING)
#define GROUP_SPACING   (3 * HIG_PADDING)
#define HEADER_SPACING  (2 * HIG_PADDING)
#define ROW_SPACING     (1 * HIG_PADDING)
#define COL_SPACING     (1 * HIG_PADDING)
#define INDENT_WIDTH    (2 * HIG_PADDING)

#define BALSA_PAGE_SIZE_GROUP_KEY  "balsa-page-size-group"
#define BALSA_GRID_PAGE_KEY  "balsa-grid-page"
#define BALSA_MAX_WIDTH_CHARS 60
#define BALSA_MAX_WIDTH_CHARS_MDN 30

typedef struct _PropertyUI {
    /* The page index: */
    GtkWidget *view;

    GtkWidget *address_books;

    GtkWidget *mail_servers;
    GtkWidget *smtp_servers;
    GtkWidget *smtp_server_edit_button;
    GtkWidget *smtp_server_del_button;
    GtkWidget *smtp_certificate_passphrase;
    GtkWidget *mail_directory;
    GtkWidget *encoding_menu;
    GtkWidget *check_mail_auto;
    GtkWidget *check_mail_minutes;
    GtkWidget *quiet_background_check;
    GtkWidget *msg_size_limit;
    GtkWidget *check_imap;
    GtkWidget *check_imap_inbox;
    GtkWidget *notify_new_mail_dialog;
#ifdef HAVE_CANBERRA
    GtkWidget *notify_new_mail_sound;
#endif
    GtkWidget *mdn_reply_clean_menu, *mdn_reply_notclean_menu;

    GtkWidget *close_mailbox_auto;
    GtkWidget *close_mailbox_minutes;
    GtkWidget *hide_deleted;
    gint filter;
    GtkWidget *expunge_on_close;
    GtkWidget *expunge_auto;
    GtkWidget *expunge_minutes;
    GtkWidget *action_after_move_menu;
#ifdef ENABLE_SYSTRAY
    GtkWidget *enable_systray_icon;
#endif
    GtkWidget *enable_dkim_checks;

    GtkWidget *previewpane;
    GtkWidget *layout_type;
    GtkWidget *view_message_on_open;
    GtkWidget *ask_before_select;
    GtkWidget *pgdownmod;
    GtkWidget *pgdown_percent;
    GtkWidget *view_allheaders;
    GtkWidget *empty_trash;
    GtkWidget *recv_progress_dlg;
    GtkWidget *send_progress_dlg;
    GtkWidget *wordwrap;
    GtkWidget *wraplength;
    GtkWidget *open_inbox_upon_startup;
    GtkWidget *check_mail_upon_startup;
    GtkWidget *remember_open_mboxes;
    GtkWidget *mblist_show_mb_content_info;
    GtkWidget *always_queue_sent_mail;
    GtkWidget *send_mail_auto;
    GtkWidget *send_mail_minutes;
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

    GtkWidget *use_system_fonts;        /* toggle button */
    GtkWidget *message_font_label;      /* label */
    GtkWidget *message_font_button;     /* font used to display messages */
    GtkWidget *subject_font_label;      /* label */
    GtkWidget *subject_font_button;     /* font used to display subjects */
    GtkWidget *use_default_font_size;   /* toggle button */

    GtkWidget *date_format;

    GtkWidget *selected_headers;

    /* colours */
    GtkWidget *quoted_color[MAX_QUOTED_COLOR];
    GtkWidget *url_color;

    /* sorting and threading prefs */
    GtkWidget *default_sort_field;
    gint sort_field_index;
    GtkWidget *thread_messages_check;
    GtkWidget *tree_expand_check;

    /* quote regex */
    GtkWidget *mark_quoted;
    GtkWidget *quote_pattern;

    /* wrap incoming text/plain */
    GtkWidget *browse_wrap;
    GtkWidget *browse_wrap_length;

    /* how to display multipart/alternative */
    GtkWidget *display_alt_plain;

    /* how to handle broken mails with 8-bit chars */
    GtkRadioButton *convert_unknown_8bit[NUM_CONVERT_8BIT_MODES];
    GtkWidget *convert_unknown_8bit_codeset;

#if !(HAVE_GSPELL || HAVE_GTKSPELL)
    /* spell checking */
    GtkWidget *spell_check_sig;
    GtkWidget *spell_check_quoted;
#endif                          /* !(HAVE_GSPELL || HAVE_GTKSPELL) */

    /* folder scanning */
    GtkWidget *local_scan_depth;
    GtkWidget *imap_scan_depth;

} PropertyUI;


static PropertyUI *pui = NULL;
static GtkWidget *property_box;
static gboolean already_open;

    /* combo boxes */
struct pm_combo_box_info {
    GSList *levels;
};
#define PM_COMBO_BOX_INFO "balsa-pref-manager-combo-box-info"

    /* callbacks */
static void properties_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void address_book_change(LibBalsaAddressBook * address_book, gboolean append);

    /* These labels must match the LibBalsaMailboxSortFields enum. */
const gchar *sort_field_label[] = {
    N_("Message number"),
    N_("Subject"),
    N_("Date"),
    N_("Size"),
    N_("Sender")
};

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
    GtkWidget *combo_box = gtk_combo_box_text_new();
    struct pm_combo_box_info *info = g_new0(struct pm_combo_box_info, 1);

    gtk_widget_set_hexpand(combo_box, TRUE);

    g_object_set_data_full(G_OBJECT(combo_box), PM_COMBO_BOX_INFO, info,
                           (GDestroyNotify) pm_combo_box_info_free);
    g_signal_connect(combo_box, "changed",
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

    /* and now the important stuff: */

enum {
    PM_TEXT_COL,
    PM_HELP_COL,
    PM_CHILD_COL,
    PM_NUM_COLS
};

static void
pm_selection_changed(GtkTreeSelection * selection, gpointer user_data)
{
    GtkStack *stack = user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkWidget *child;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter,
                       PM_CHILD_COL, &child,
                       -1);
    if (child != NULL) {
        gtk_stack_set_visible_child(stack, child);
        g_object_unref(child);
    } else {
        g_warning("%s no child", G_STRLOC);
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

    /* LibBalsaConfForeachFunc callback;
     * update any view that is using the current default
     * value to the new default value. */
static gboolean
update_view_defaults(const gchar * group, const gchar * url,
                     gpointer data)
{
    LibBalsaMailbox *mailbox;
    LibBalsaMailboxView *view;

    mailbox = balsa_find_mailbox_by_url(url);
    view =
        mailbox !=
        NULL ? libbalsa_mailbox_get_view(mailbox) :
        config_load_mailbox_view(url);

    if (!view)
        return FALSE;

    if (view->filter == libbalsa_mailbox_get_filter(NULL))
        view->filter = pui->filter;
    if (view->sort_field == libbalsa_mailbox_get_sort_field(NULL))
        view->sort_field = pui->sort_field_index;
    if (view->threading_type == libbalsa_mailbox_get_threading_type(NULL)) {
        if (gtk_toggle_button_get_active
            (GTK_TOGGLE_BUTTON(pui->thread_messages_check))) {
            view->threading_type = LB_MAILBOX_THREADING_SIMPLE;
        } else {
            view->threading_type = LB_MAILBOX_THREADING_FLAT;
        }
    }

    if (!mailbox) {
        config_save_mailbox_view(url, view);
        libbalsa_mailbox_view_free(view);
    }

    return FALSE;
}

static void
check_font_button(GtkWidget * button, gchar ** font)
{
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "font-modified"))) {
        gchar *fontname;

        fontname = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(button));

        g_free(*font);
        if (!gtk_toggle_button_get_active
            (GTK_TOGGLE_BUTTON(pui->use_default_font_size))) {
            *font = fontname;
        } else {
            PangoFontDescription *desc;

            desc = pango_font_description_from_string(fontname);
            g_free(fontname);

            pango_font_description_unset_fields(desc, PANGO_FONT_MASK_SIZE);
            *font = pango_font_description_to_string(desc);
            pango_font_description_free(desc);
        }
    }
}

static void
apply_prefs(GtkDialog * pbox)
{
    gint i;
    GtkWidget *balsa_window;
    const gchar *tmp;
    guint save_enum; /* FIXME: assumes that enums are unsigned */
    gboolean save_setting;

    /*
     * Before changing the default mailbox view, update any current
     * views that have default values.
     */
    libbalsa_conf_foreach_group(VIEW_BY_URL_SECTION_PREFIX,
                                (LibBalsaConfForeachFunc)
                                update_view_defaults, NULL);


    g_free(balsa_app.local_mail_directory);
    balsa_app.local_mail_directory =
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                      (pui->mail_directory));

    /* 
     * display page 
     */
    balsa_app.recv_progress_dialog = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->recv_progress_dlg));
    balsa_app.send_progress_dialog = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->send_progress_dlg));

    balsa_app.previewpane =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->previewpane));

    save_enum = balsa_app.layout_type;
    balsa_app.layout_type =
        pm_combo_box_get_level(pui->layout_type);
    if (balsa_app.layout_type != save_enum)
        balsa_change_window_layout(balsa_app.main_window);

    balsa_app.view_message_on_open =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->view_message_on_open));
    balsa_app.ask_before_select =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->ask_before_select));
    balsa_app.pgdownmod =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->pgdownmod));
    balsa_app.pgdown_percent =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->pgdown_percent));

    if (balsa_app.mblist_show_mb_content_info !=
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->mblist_show_mb_content_info))) {
        balsa_app.mblist_show_mb_content_info =
            !balsa_app.mblist_show_mb_content_info;
        g_object_set(balsa_app.mblist, "show_content_info",
                     balsa_app.mblist_show_mb_content_info, NULL);
    }

    balsa_app.check_mail_auto =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->check_mail_auto));
    balsa_app.check_mail_timer =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->check_mail_minutes));
    balsa_app.quiet_background_check =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->quiet_background_check));
    balsa_app.msg_size_limit =
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(pui->msg_size_limit)) *
        1024;
    balsa_app.check_imap =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->check_imap));
    balsa_app.check_imap_inbox =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->check_imap_inbox));
    balsa_app.notify_new_mail_dialog =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->notify_new_mail_dialog));

#ifdef HAVE_CANBERRA
    balsa_app.notify_new_mail_sound =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->notify_new_mail_sound));
#endif

    balsa_app.mdn_reply_clean =
        pm_combo_box_get_level(pui->mdn_reply_clean_menu);
    balsa_app.mdn_reply_notclean =
        pm_combo_box_get_level(pui->mdn_reply_notclean_menu);

    if (balsa_app.check_mail_auto)
        update_timer(TRUE, balsa_app.check_mail_timer);
    else
        update_timer(FALSE, 0);

    balsa_app.wordwrap =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->wordwrap));
    balsa_app.wraplength =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->wraplength));
    balsa_app.autoquote =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->autoquote));
    balsa_app.reply_strip_html =
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                      (pui->reply_include_html_parts));
    balsa_app.forward_attached =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->forward_attached));

    save_setting = balsa_app.always_queue_sent_mail;
    balsa_app.always_queue_sent_mail =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->always_queue_sent_mail));
    if (balsa_app.always_queue_sent_mail != save_setting) {
        balsa_toolbar_model_changed(balsa_window_get_toolbar_model());
    }

	balsa_app.send_mail_auto =
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->send_mail_auto));
	balsa_app.send_mail_timer =
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->send_mail_minutes));
	libbalsa_auto_send_config(balsa_app.send_mail_auto, balsa_app.send_mail_timer);

    balsa_app.copy_to_sentbox =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->copy_to_sentbox));

    balsa_app.close_mailbox_auto =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->close_mailbox_auto));
    balsa_app.close_mailbox_timeout =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->close_mailbox_minutes)) *
        60;

#ifdef ENABLE_SYSTRAY
    balsa_app.enable_systray_icon =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->enable_systray_icon));
    libbalsa_systray_icon_enable(balsa_app.enable_systray_icon != 0);
#endif

    balsa_app.enable_dkim_checks =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->enable_dkim_checks));

    libbalsa_mailbox_set_filter(NULL, pui->filter);
    balsa_app.expunge_on_close =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->expunge_on_close));
    balsa_app.expunge_auto =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->expunge_auto));
    balsa_app.expunge_timeout =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->expunge_minutes)) * 60;
    balsa_app.mw_action_after_move =
        pm_combo_box_get_level(pui->action_after_move_menu);

    /* external editor */
    balsa_app.edit_headers =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->edit_headers));

    /* arp */
    g_free(balsa_app.quote_str);
    balsa_app.quote_str =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->quote_str)));

    /* fonts */
    balsa_app.use_system_fonts =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->use_system_fonts));
    check_font_button(pui->message_font_button, &balsa_app.message_font);
    check_font_button(pui->subject_font_button, &balsa_app.subject_font);

    balsa_app.mark_quoted =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->mark_quoted));
    g_free(balsa_app.quote_regex);
    tmp = gtk_entry_get_text(GTK_ENTRY(pui->quote_pattern));
    balsa_app.quote_regex = g_strcompress(tmp);

    balsa_app.browse_wrap =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->browse_wrap));
    /* main window view menu can also toggle balsa_app.browse_wrap
     * update_view_menu lets it know we've made a change */
    update_view_menu(balsa_app.main_window);
    balsa_app.browse_wrap_length =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->browse_wrap_length));

    balsa_app.display_alt_plain =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->display_alt_plain));

    balsa_app.open_inbox_upon_startup =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->open_inbox_upon_startup));
    balsa_app.check_mail_upon_startup =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->check_mail_upon_startup));
    balsa_app.remember_open_mboxes =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->remember_open_mboxes));
    balsa_app.local_scan_depth =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->local_scan_depth));
    balsa_app.imap_scan_depth =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (pui->imap_scan_depth));
    balsa_app.empty_trash_on_exit =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->empty_trash));

#if !(HAVE_GSPELL || HAVE_GTKSPELL)
    /* spell checking */
    balsa_app.check_sig =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->spell_check_sig));
    balsa_app.check_quoted =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->spell_check_quoted));
#endif                          /* !(HAVE_GSPELL || HAVE_GTKSPELL) */

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
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(pui->quoted_color[i]),
                                  &balsa_app.quoted_color[i]);
    }

    /* url color */
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(pui->url_color),
                              &balsa_app.url_color);

    /* sorting and threading */
    libbalsa_mailbox_set_sort_field(NULL, pui->sort_field_index);

    { /* Scope */
        gboolean thread_messages =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->thread_messages_check));
        libbalsa_mailbox_set_threading_type(NULL, thread_messages ?
                                            LB_MAILBOX_THREADING_SIMPLE :
                                            LB_MAILBOX_THREADING_FLAT);
    }

    balsa_app.expand_tree =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->tree_expand_check));

    /* Information dialogs */
    balsa_app.information_message =
        pm_combo_box_get_level(pui->information_message_menu);
    balsa_app.warning_message =
        pm_combo_box_get_level(pui->warning_message_menu);
    balsa_app.error_message =
        pm_combo_box_get_level(pui->error_message_menu);
    balsa_app.fatal_message =
        pm_combo_box_get_level(pui->fatal_message_menu);
    balsa_app.debug_message =
        pm_combo_box_get_level(pui->debug_message_menu);

    /* handling of 8-bit message parts without codeset header */
    balsa_app.convert_unknown_8bit =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->convert_unknown_8bit[1]));
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
static void
set_prefs(void)
{
    unsigned i;
    gchar *tmp;

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->recv_progress_dlg), balsa_app.recv_progress_dialog);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->send_progress_dlg), balsa_app.send_progress_dialog);

    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER
                                        (pui->mail_directory),
                                  balsa_app.local_mail_directory);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->previewpane),
                                 balsa_app.previewpane);
    pm_combo_box_set_level(pui->layout_type, balsa_app.layout_type);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->view_message_on_open),
                                 balsa_app.view_message_on_open);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->ask_before_select),
                                 balsa_app.ask_before_select);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->pgdownmod),
                                 balsa_app.pgdownmod);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->pgdown_percent),
                              (float) balsa_app.pgdown_percent);
    gtk_widget_set_sensitive(pui->pgdown_percent,
                             gtk_toggle_button_get_active
                             (GTK_TOGGLE_BUTTON(pui->pgdownmod)));

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
#ifdef HAVE_CANBERRA
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->notify_new_mail_sound),
                                 balsa_app.notify_new_mail_sound);
#endif

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
                             gtk_toggle_button_get_active
                             (GTK_TOGGLE_BUTTON(pui->close_mailbox_auto)));

#ifdef ENABLE_SYSTRAY
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->enable_systray_icon),
                                 balsa_app.enable_systray_icon);
#endif

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->enable_dkim_checks),
                                 balsa_app.enable_dkim_checks);

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
                             gtk_toggle_button_get_active
                             (GTK_TOGGLE_BUTTON(pui->expunge_auto)));
    gtk_widget_set_sensitive(pui->check_mail_minutes,
                             gtk_toggle_button_get_active
                             (GTK_TOGGLE_BUTTON(pui->check_mail_auto)));
    pm_combo_box_set_level(pui->action_after_move_menu,
                           balsa_app.mw_action_after_move);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->wordwrap),
                                 balsa_app.wordwrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->wraplength),
                              (float) balsa_app.wraplength);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->always_queue_sent_mail),
                                 balsa_app.always_queue_sent_mail);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->send_mail_auto),
								 balsa_app.send_mail_auto);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->send_mail_minutes),
							  (float) balsa_app.send_mail_timer);
	gtk_widget_set_sensitive(pui->send_mail_minutes,
							 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->send_mail_auto)));
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
                             gtk_toggle_button_get_active
                             (GTK_TOGGLE_BUTTON(pui->wordwrap)));

    /* external editor */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->edit_headers),
                                 balsa_app.edit_headers);

    /* arp */
    gtk_entry_set_text(GTK_ENTRY(pui->quote_str), balsa_app.quote_str);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->mark_quoted),
                                 balsa_app.mark_quoted);
    gtk_widget_set_sensitive(pui->quote_pattern, balsa_app.mark_quoted);
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

    /* sorting and threading */
    pui->sort_field_index = libbalsa_mailbox_get_sort_field(NULL);
    pm_combo_box_set_level(pui->default_sort_field, pui->sort_field_index);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->thread_messages_check),
                                 libbalsa_mailbox_get_threading_type(NULL)
                                 != LB_MAILBOX_THREADING_FLAT);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->tree_expand_check),
                                 balsa_app.expand_tree);

#if !(HAVE_GSPELL || HAVE_GTKSPELL)
    /* spelling */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->spell_check_sig),
                                 balsa_app.check_sig);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->spell_check_quoted),
                                 balsa_app.check_quoted);
#endif                          /* !(HAVE_GSPELL || HAVE_GTKSPELL) */


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
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(pui->quoted_color[i]),
                                  &balsa_app.quoted_color[i]);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(pui->url_color),
                              &balsa_app.url_color);

    /* Information Message */
    pm_combo_box_set_level(pui->information_message_menu,
                           balsa_app.information_message);
    pm_combo_box_set_level(pui->warning_message_menu,
                           balsa_app.warning_message);
    pm_combo_box_set_level(pui->error_message_menu,
                           balsa_app.error_message);
    pm_combo_box_set_level(pui->fatal_message_menu,
                           balsa_app.fatal_message);
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
        else if (LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(address_book))
            type = "Externq";
#if ENABLE_LDAP
        else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book))
            type = "LDAP";
#endif
#if HAVE_GPE
        else if (LIBBALSA_IS_ADDRESS_BOOK_GPE(address_book))
            type = "GPE";
#endif
#if HAVE_OSMO
        else if (LIBBALSA_IS_ADDRESS_BOOK_OSMO(address_book))
        	type = "Osmo";
#endif
#if HAVE_WEBDAV
        else if (LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(address_book))
        	type = "CardDAV";
#endif
        else
            type = _("Unknown");

        if (address_book == balsa_app.default_address_book) {
            /* Translators: #1 address book name */
            name = g_strdup_printf(_("%s (default)"), libbalsa_address_book_get_name(address_book));
        } else {
            name = g_strdup(libbalsa_address_book_get_name(address_book));
        }
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           AB_TYPE_COLUMN, type,
                           AB_NAME_COLUMN, name,
                           AB_XPND_COLUMN, (libbalsa_address_book_get_expand_aliases(address_book)
                                            && !libbalsa_address_book_get_is_expensive(address_book)),
                           AB_DATA_COLUMN, address_book, -1);

        g_free(name);
        list = g_list_next(list);
    }

    if (gtk_tree_model_get_iter_first(model, &iter))
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection
                                       (tree_view), &iter);

    properties_modified_cb(NULL, property_box);
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
    const gchar *name = NULL;
    gboolean append = FALSE;

    if (mbnode != NULL) {
        LibBalsaMailbox *mailbox = balsa_mailbox_node_get_mailbox(mbnode);
        if (mailbox != NULL) {
            if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
                protocol = "IMAP";
                name = libbalsa_mailbox_get_name(mailbox);
                append = TRUE;
            }
        } else
            if (LIBBALSA_IS_IMAP_SERVER(balsa_mailbox_node_get_server(mbnode))) {
            protocol = "IMAP";
            name = balsa_mailbox_node_get_name(mbnode);
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

/* helper functions that simplify often performed actions */

static GtkWidget *
add_button_to_box(const gchar * label, GCallback cb, gpointer cb_data,
                  GtkWidget * box)
{
    GtkWidget *button = gtk_button_new_with_mnemonic(label);
    g_signal_connect_swapped(button, "clicked", cb, cb_data);
    gtk_container_add(GTK_CONTAINER(box), button);

    return button;
}

static void
add_show_menu(const char* label, gint level, GtkWidget* menu)
{
    struct pm_combo_box_info *info =
        g_object_get_data(G_OBJECT(menu), PM_COMBO_BOX_INFO);

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(menu), label);
    info->levels = g_slist_append(info->levels, GINT_TO_POINTER(level));
}

/*
 * callback for create_pref_option_menu
 */

static void
option_menu_cb(GtkMenuItem * widget, gpointer data)
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
    g_signal_connect(combo_box, "changed",
                     G_CALLBACK(option_menu_cb), index);

    for (i = 0; i < size; i++)
	add_show_menu(_(names[i]), i, combo_box);

    return combo_box;
}

static GtkWidget *
create_layout_types_menu(void)
{
    GtkWidget *combo_box = pm_combo_box_new();
    add_show_menu(_("Default layout"), LAYOUT_DEFAULT, combo_box);
    add_show_menu(_("Wide message layout"), LAYOUT_WIDE_MSG, combo_box);
    add_show_menu(_("Wide screen layout"), LAYOUT_WIDE_SCREEN, combo_box);
    return combo_box;
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

static void
balsa_help_pbox_display(void)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeIter parent;
    gchar *text, *p;
    GError *err = NULL;
    gchar *uri;
    GtkWidget *toplevel;
    GString *string;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(pui->view));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    string = g_string_new("help:balsa/preferences-");

    if (gtk_tree_model_iter_parent(model, &parent, &iter)) {
        gtk_tree_model_get(model, &parent, PM_HELP_COL, &text, -1);
        for (p = text; *p; p++)
            *p = (*p == ' ') ? '-' : g_ascii_tolower(*p);
        g_string_append(string, text);
        g_free(text);
        g_string_append_c(string, '#');
    }
    gtk_tree_model_get(model, &iter, PM_HELP_COL, &text, -1);
    for (p = text; *p; p++)
        *p = (*p == ' ') ? '-' : g_ascii_tolower(*p);
    g_string_append(string, text);
    g_free(text);

    uri = g_string_free(string, FALSE);
    toplevel = gtk_widget_get_toplevel(GTK_WIDGET(pui->view));
    if (gtk_widget_is_toplevel(toplevel)) {
        gtk_show_uri_on_window(GTK_WINDOW(toplevel), uri,
                               gtk_get_current_event_time(), &err);
    }
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
		_("Error displaying %s: %s\n"),
		uri, err->message);
        g_error_free(err);
    }

    g_free(uri);
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

/***************************
 *
 * Helpers for GtkGrid pages
 *
 **************************/

#define PM_GRID_NEXT_ROW "pref-manager-grid-next-row"
#define pm_grid_get_next_row(grid) \
    GPOINTER_TO_INT(g_object_get_data((GObject *) grid, PM_GRID_NEXT_ROW))
#define pm_grid_set_next_row(grid, row) \
    g_object_set_data((GObject *) grid, PM_GRID_NEXT_ROW, (GINT_TO_POINTER(row)))

static GtkWidget *
pm_grid_new(void)
{
    GtkWidget *grid;

    grid = gtk_grid_new();

    gtk_grid_set_column_spacing((GtkGrid *) grid, COL_SPACING);
    gtk_grid_set_row_spacing((GtkGrid *) grid, ROW_SPACING);
    pm_grid_set_next_row(grid, 0);
    libbalsa_set_margins(grid, BORDER_WIDTH);

    return grid;
}

static void
pm_grid_attach(GtkGrid   * grid,
               GtkWidget * child,
               gint        left,
               gint        top,
               gint        width,
               gint        height)
{
    if (left == 0) {
        /* Group label */
        if (top > 0) {
            gtk_widget_set_margin_top(child, GROUP_SPACING - ROW_SPACING);
        }
        gtk_widget_set_margin_bottom(child, HEADER_SPACING - ROW_SPACING);
    } else if (left == 1) {
        gtk_widget_set_margin_start(child, INDENT_WIDTH);
        gtk_widget_set_hexpand(child, TRUE);
    }
    if (GTK_IS_LABEL(child))
        gtk_label_set_xalign((GtkLabel *) child, 0.0);

    gtk_grid_attach(grid, child, left, top, width, height);
}

static GtkWidget *
pm_group_label(const gchar * text)
{
    GtkWidget *label;
    gchar *markup;

    markup = g_strdup_printf("<b>%s</b>", text);
    label = libbalsa_create_wrap_label(markup, TRUE);
    g_free(markup);

    return label;
}

static GtkWidget *
pm_grid_attach_check(GtkGrid     * grid,
                     gint          left,
                     gint          top,
                     gint          width,
                     gint          height,
                     const gchar * text)
{
    GtkWidget *res;

    res = gtk_check_button_new_with_mnemonic(text);
    pm_grid_attach(grid, res, left, top, width, height);

    return res;
}

static GtkWidget *
pm_grid_attach_label(GtkGrid     * grid,
                     gint          left,
                     gint          top,
                     gint          width,
                     gint          height,
                     const gchar * text)
{
    GtkWidget *label;

    label = libbalsa_create_wrap_label(text, FALSE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), BALSA_MAX_WIDTH_CHARS);

    pm_grid_attach(grid, label, left, top, width, height);

    return label;
}

static GtkWidget*
pm_grid_attach_pref_menu(GtkGrid     * grid,
                         gint          left,
                         gint          top,
                         gint          width,
                         gint          height,
                         const gchar * names[],
                         gint          size,
                         gint        * index)
{
    GtkWidget *option_menu;

    option_menu = create_pref_option_menu(names, size, index);

    pm_grid_attach(grid, option_menu, left, top, width, height);

    return option_menu;
}

static GtkWidget *
pm_grid_attach_entry(GtkGrid     * grid,
                     gint          left,
                     gint          top,
                     gint          width,
                     gint          height,
                     const gchar * text)
{
    GtkWidget *entry;

    pm_grid_attach_label(grid, left, top, width, height, text);

    entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    pm_grid_attach(grid, entry, ++left, top, width, height);

    return entry;
}

static GtkWidget *
pm_grid_attach_color_box(GtkGrid     * grid,
                         gint          left,
                         gint          top,
                         gint          width,
                         gint          height,
                         const gchar * title)
{
    GtkWidget *picker;

    picker = gtk_color_button_new();
    gtk_color_button_set_title(GTK_COLOR_BUTTON(picker), title);
    pm_grid_attach(grid, picker, left, top, width, height);
    gtk_widget_set_hexpand(picker, FALSE);

    pm_grid_attach_label(grid, ++left, top, width, height, title);

    return picker;
}

/*
 * If the font button shows zero size, set it to the default size and
 * return TRUE.
 */
static gboolean
font_button_check_font_size(GtkWidget * button, GtkWidget * widget)
{
    GtkFontChooser *chooser = GTK_FONT_CHOOSER(button);
    gchar *fontname;
    PangoFontDescription *desc;
    gboolean retval = FALSE;

    fontname = gtk_font_chooser_get_font(chooser);
    desc = pango_font_description_from_string(fontname);
    g_free(fontname);

    if (pango_font_description_get_size(desc) <= 0) {
        PangoContext *context;
        PangoFontDescription *desc2;
        gint size;
        gchar *desc_string;

        context = gtk_widget_get_pango_context(widget);
        desc2 = pango_context_get_font_description(context);
        size = pango_font_description_get_size(desc2);

        pango_font_description_set_size(desc, size);
        desc_string = pango_font_description_to_string(desc);
        gtk_font_chooser_set_font(chooser, desc_string);
        g_free(desc_string);
        retval = TRUE;
    }
    pango_font_description_free(desc);

    return retval;
}

/*
 * Create a font button from a font string and attach it; return TRUE if
 * the string does not specify a point size.
 */
static gboolean
pm_grid_attach_font_button(GtkGrid     * grid,
                           gint          left,
                           gint          top,
                           gint          width,
                           gint          height,
                           const gchar * text,
                           const gchar * font,
                           GtkWidget  ** label,
                           GtkWidget  ** button)
{
    *label = pm_grid_attach_label(grid, left, top, width, height, text);

    *button = gtk_font_button_new_with_font(font);
    gtk_widget_set_hexpand(*button, TRUE);
    pm_grid_attach(grid, *button, ++left, top, width, height);

    return font_button_check_font_size(*button, (GtkWidget *) grid);
}

static GtkWidget *
pm_grid_attach_information_menu(GtkGrid     * grid,
                                gint          left,
                                gint          top,
                                gint          width,
                                gint          height,
                                const gchar * text,
                                gint          defval)
{
    GtkWidget *label;
    GtkWidget *combo_box;

    label = pm_grid_attach_label(grid, left, top, width, height, text);
    gtk_widget_set_hexpand(label, FALSE);

    combo_box = create_information_message_menu();
    pm_combo_box_set_level(combo_box, defval);
    gtk_widget_set_hexpand(combo_box, TRUE);
    pm_grid_attach(grid, combo_box, ++left, top, width, height);

    return combo_box;
}

/*
 * End of helpers for GtkGrid pages
 */

/*
 * callbacks
 */
static void
properties_modified_cb(GtkWidget G_GNUC_UNUSED * widget, GtkWidget * pbox)
{
    gtk_dialog_set_response_sensitive(GTK_DIALOG(pbox), GTK_RESPONSE_OK,
                                      TRUE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(pbox), GTK_RESPONSE_APPLY,
                                      TRUE);
}

static void
server_edit_cb(GtkTreeView * tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
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

#define SMTP_SERVER_SECTION_PREFIX "smtp-server-"

/* Callback for the server-dialog's response handler - called iff the user selected 'OK'. */
static void
smtp_server_update(LibBalsaSmtpServer * smtp_server, const gchar * old_name)
{
	const gchar *new_name;
	gchar *group;

	new_name = libbalsa_smtp_server_get_name(smtp_server);

	if (old_name == NULL) {
		/* add a new server to the list */
		libbalsa_smtp_server_add_to_list(smtp_server, &balsa_app.smtp_servers);
	} else if (strcmp(old_name, new_name) != 0) {
		/* remove old config section if the server name changed */
		group = g_strconcat(SMTP_SERVER_SECTION_PREFIX, old_name, NULL);
		libbalsa_conf_remove_group(group);
		g_free(group);
	} else {
		/* existing server, same name - nothing to do here */
	}

	update_smtp_servers();
	group = g_strconcat(SMTP_SERVER_SECTION_PREFIX, new_name, NULL);
	libbalsa_conf_push_group(group);
	g_free(group);
	libbalsa_smtp_server_save_config(smtp_server);
	libbalsa_conf_pop_group();
	if (property_box != NULL) {
		properties_modified_cb(NULL, property_box);
	}
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
smtp_server_edit_cb(GtkTreeView * tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
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
    properties_modified_cb(NULL, property_box);
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
address_book_edit_cb(GtkTreeView * tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
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
add_menu_cb(GtkWidget * menu, GtkWidget * widget)
{
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(widget),
                             GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST,
                             NULL);
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

static GtkWidget *
server_add_menu_widget(void)
{
    GtkWidget *menu;
    GtkWidget *menuitem;

    menu = gtk_menu_new();
    menuitem = gtk_menu_item_new_with_label(_("Remote POP3 mailbox…"));
    g_signal_connect(menuitem, "activate",
                     G_CALLBACK(pop3_add_cb), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Remote IMAP account…"));
    g_signal_connect(menuitem, "activate",
		     G_CALLBACK(folder_conf_add_imap_cb), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    return menu;
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

    if (balsa_mailbox_node_get_mailbox(mbnode) != NULL)
	mailbox_conf_delete(mbnode);
    else
	folder_conf_delete(mbnode);
}

static void
timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate = 
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->check_mail_auto));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->check_mail_minutes), newstate);
    properties_modified_cb(widget, pbox);
}

static void
send_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
	gboolean newstate = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->send_mail_auto));

	gtk_widget_set_sensitive(GTK_WIDGET(pui->send_mail_minutes), newstate);
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
mark_quoted_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->mark_quoted));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->quote_pattern), newstate);
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
mailbox_close_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->close_mailbox_auto));

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

static void
imap_toggled_cb(GtkWidget * widget, GtkWidget * pbox)
{
    properties_modified_cb(widget, pbox);

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->check_imap)))
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

    gtk_widget_set_sensitive
        (pui->convert_unknown_8bit_codeset,
         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                      (pui->convert_unknown_8bit[1])));
}

/*
 * Callbacks for the font group
 */

static void
use_system_fonts_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean use_custom_fonts =
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

    properties_modified_cb(widget, pbox);

    gtk_widget_set_sensitive(pui->message_font_label, use_custom_fonts);
    gtk_widget_set_sensitive(pui->message_font_button, use_custom_fonts);
    gtk_widget_set_sensitive(pui->subject_font_label, use_custom_fonts);
    gtk_widget_set_sensitive(pui->subject_font_button, use_custom_fonts);
    gtk_widget_set_sensitive(pui->use_default_font_size, use_custom_fonts);
}

static void
font_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean show_size =
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                      (pui->use_default_font_size));

    properties_modified_cb(widget, pbox);

    gtk_font_button_set_show_size(GTK_FONT_BUTTON(widget), show_size);
    g_object_set_data(G_OBJECT(widget), "font-modified",
                      GINT_TO_POINTER(TRUE));
}

static void
default_font_size_cb(GtkWidget * widget, GtkWidget * pbox)
{
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        /* Changing from default font size to user-specified font size;
         * we make sure the font size is not initially zero. */
        font_button_check_font_size(pui->message_font_button, widget);
        font_button_check_font_size(pui->subject_font_button, widget);
    }

    font_modified_cb(pui->message_font_button, pbox);
    font_modified_cb(pui->subject_font_button, pbox);
}

/*
 * End of callbacks for the font group
 */


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
 * End of callbacks
 */

/**************************************************************
 *
 * Preference groups
 *
 * Each group is given a GtkGrid, and appends a row containing
 * the group title, followed by rows with the group's controls.
 *
 *************************************************************/

/*
 * Remote mailbox servers group
 */

static void
pm_grid_add_remote_mailbox_servers_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkWidget *vbox;
    GtkWidget *scrolledwindow;
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkWidget *server_add_menu;

    pm_grid_attach(grid, pm_group_label(_("Remote mailbox servers")), 0, row, 3, 1);

    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolledwindow, -1, 100);

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
        gtk_tree_view_column_new_with_attributes(_("Mailbox name"),
                                                 renderer,
                                                 "text", MS_NAME_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    gtk_container_add(GTK_CONTAINER(scrolledwindow), tree_view);

    g_signal_connect(pui->mail_servers, "row-activated",
                     G_CALLBACK(server_edit_cb), NULL);

    pm_grid_attach(grid, scrolledwindow, 1, ++row, 1, 1);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ROW_SPACING);

    server_add_menu = server_add_menu_widget();
    g_object_weak_ref(G_OBJECT(vbox), (GWeakNotify) g_object_unref,
                      server_add_menu);
    g_object_ref_sink(server_add_menu);
    add_button_to_box(_("_Add"), G_CALLBACK(add_menu_cb),
                      server_add_menu, vbox);

    add_button_to_box(_("_Modify"), G_CALLBACK(server_edit_cb),
                      tree_view, vbox);
    add_button_to_box(_("_Delete"), G_CALLBACK(server_del_cb),
                      tree_view, vbox);

    pm_grid_attach(grid, vbox, 2, row, 1, 1);
    pm_grid_set_next_row(grid, ++row);

    /* fill in data */
    update_mail_servers();
}

/*
 * Local mail directory group
 */

static void
pm_grid_add_local_mail_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);

    pm_grid_attach(grid, pm_group_label(_("Local mail directory")), 0, row, 3, 1);

    pui->mail_directory =
        gtk_file_chooser_button_new(_("Select your local mail directory"),
                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

    pm_grid_attach(grid, pui->mail_directory, 1, ++row, 2, 1);
    pm_grid_set_next_row(grid, ++row);
}

/*
 * Outgoing mail servers group
 */

static void
pm_grid_add_outgoing_mail_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkWidget *scrolled_window;
    GtkListStore *store;
    GtkWidget *tree_view;
    GtkTreeSelection *selection;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkWidget *vbox;

    pm_grid_attach(grid, pm_group_label(_("Outgoing mail servers")), 0, row, 3, 1);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, -1, 100);

    store = gtk_list_store_new(2, G_TYPE_STRING,        /* Server name    */
                               G_TYPE_POINTER); /* Object address */
    pui->smtp_servers = tree_view =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    g_signal_connect(selection, "changed",
                     G_CALLBACK(smtp_server_changed), NULL);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(_("Server name"),
                                                      renderer,
                                                      "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);

    g_signal_connect(pui->smtp_servers, "row-activated",
                     G_CALLBACK(smtp_server_edit_cb), NULL);

    pm_grid_attach(grid, scrolled_window, 1, ++row, 1, 1);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, HIG_PADDING);
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

    pm_grid_attach(grid, vbox, 2, row, 1, 1);
    pm_grid_set_next_row(grid, ++row);

    /* fill in data */
    update_smtp_servers();
}

/*
 * Checking group
 */

static void
pm_grid_add_checking_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkAdjustment *spinbutton_adj;
    GtkWidget *label;

    pm_grid_attach(grid, pm_group_label(_("Checking")), 0, row, 3, 1);

    pui->check_mail_auto = gtk_check_button_new_with_mnemonic(
	_("_Check mail automatically every"));
    pm_grid_attach(grid, pui->check_mail_auto, 1, ++row, 1, 1);

    spinbutton_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 0);
    pui->check_mail_minutes = gtk_spin_button_new(spinbutton_adj, 1, 0);
    gtk_widget_set_hexpand(pui->check_mail_minutes, TRUE);
    pm_grid_attach(grid, pui->check_mail_minutes, 2, row, 1, 1);

    label = gtk_label_new(_("minutes"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    pm_grid_attach(grid, label, 3, row, 1, 1);

    pui->check_imap = gtk_check_button_new_with_mnemonic(
	_("Check _IMAP mailboxes"));
    pm_grid_attach(grid, pui->check_imap, 1, ++row, 1, 1);

    pui->check_imap_inbox =
        gtk_check_button_new_with_mnemonic(_("Check Inbox _only"));
    pm_grid_attach(grid, pui->check_imap_inbox, 2, row, 2, 1);

    pui->quiet_background_check = gtk_check_button_new_with_label(
	_("Do background check quietly (no messages in status bar)"));
    pm_grid_attach(grid, pui->quiet_background_check, 1, ++row, 3, 1);

    label = gtk_label_new_with_mnemonic(_("_POP message size limit:"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    pm_grid_attach(grid, label, 1, ++row, 1, 1);

    pui->msg_size_limit = gtk_spin_button_new_with_range(0.1, 100, 0.1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), pui->msg_size_limit);
    gtk_widget_set_hexpand(pui->msg_size_limit, TRUE);
    pm_grid_attach(grid, pui->msg_size_limit, 2, row, 1, 1);
    label = gtk_label_new(_("MB"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    pm_grid_attach(grid, label, 3, row, 1, 1);

    pm_grid_set_next_row(grid, ++row);
}

/*
 * New messages notification group
 */
static void
pm_grid_add_new_mail_notify_group(GtkWidget * grid_widget)
{
	GtkGrid *grid = (GtkGrid *) grid_widget;
	gint row = pm_grid_get_next_row(grid);

	pm_grid_attach(grid, pm_group_label(_("Notification about new messages")), 0, row, 3, 1);

	pui->notify_new_mail_dialog = gtk_check_button_new_with_label(_("Display message"));
	pm_grid_attach(grid, pui->notify_new_mail_dialog, 1, ++row, 1, 1);

#ifdef HAVE_CANBERRA
	pui->notify_new_mail_sound = gtk_check_button_new_with_label(_("Play sound"));
	pm_grid_attach(grid, pui->notify_new_mail_sound, 1, ++row, 1, 1);
#endif

	pm_grid_set_next_row(grid, ++row);
}

/*
 * MDN request group
 */

static void
pm_grid_add_mdn_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkWidget *label;

    /* How to handle received MDN requests */

    pm_grid_attach(grid,
                   pm_group_label(_("Message disposition notification requests")),
                   0, row, 3, 1);

    label =
        pm_grid_attach_label(grid, 1, ++row, 2, 1,
                             _("When I receive a message whose sender "
                               "requested a "
                               "Message Disposition Notification (MDN), "
                               "send it if:"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);

    label =
        pm_grid_attach_label(grid, 1, ++row, 1, 1,
                             _("The message header looks clean "
                               "(the notify-to address is the return path, "
                               "and I am in the “To:” or “CC:” list)."));
    gtk_label_set_max_width_chars((GtkLabel *) label, BALSA_MAX_WIDTH_CHARS_MDN);
    gtk_widget_set_halign(label, GTK_ALIGN_START);

    pui->mdn_reply_clean_menu = create_mdn_reply_menu();
    pm_combo_box_set_level(pui->mdn_reply_clean_menu,
                           balsa_app.mdn_reply_clean);
    gtk_widget_set_valign(pui->mdn_reply_clean_menu, GTK_ALIGN_CENTER);
    pm_grid_attach(grid, pui->mdn_reply_clean_menu, 2, row, 1, 1);

    label =
        pm_grid_attach_label(grid, 1, ++row, 1, 1,
                             _("The message header looks suspicious."));
    gtk_label_set_max_width_chars((GtkLabel *) label, BALSA_MAX_WIDTH_CHARS_MDN);
    gtk_widget_set_halign(label, GTK_ALIGN_START);

    pui->mdn_reply_notclean_menu = create_mdn_reply_menu();
    pm_combo_box_set_level(pui->mdn_reply_notclean_menu,
                           balsa_app.mdn_reply_notclean);
    pm_grid_attach(grid, pui->mdn_reply_notclean_menu, 2, row, 1, 1);

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Word wrap group
 */

static void
pm_grid_add_word_wrap_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkAdjustment *spinbutton_adj;
    GtkWidget *label;

    pm_grid_attach(grid, pm_group_label(_("Word wrap")), 0, row, 3, 1);

    pui->wordwrap =
	gtk_check_button_new_with_label(_("Wrap outgoing text at"));
    pm_grid_attach(grid, pui->wordwrap, 1, ++row, 1, 1);

    spinbutton_adj = gtk_adjustment_new(1.0, 40.0, 998.0, 1.0, 5.0, 0.0);
    pui->wraplength = gtk_spin_button_new(spinbutton_adj, 1, 0);
    gtk_widget_set_hexpand(pui->wraplength, TRUE);
    gtk_widget_set_sensitive(pui->wraplength, FALSE);
    pm_grid_attach(grid, pui->wraplength, 2, row, 1, 1);

    label = gtk_label_new(_("characters"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    pm_grid_attach(grid, label, 3, row, 1, 1);

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Other options group
 */

static void
pm_grid_add_other_options_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
	GtkWidget *label;
	GtkAdjustment *spinbutton_adj;

    pm_grid_attach(grid, pm_group_label(_("Other options")), 0, row, 3, 1);

    pui->quote_str = pm_grid_attach_entry(grid, 1, ++row, 1, 1, _("Reply prefix:"));

    pui->autoquote =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Automatically quote original "
                                                     "when replying"));
    pui->forward_attached =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Forward a mail as attachment "
                                                     "instead of quoting it"));
    pui->copy_to_sentbox =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Copy outgoing messages to sentbox"));
    pui->always_queue_sent_mail =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Send button always queues "
                                                     "outgoing mail in outbox"));

    pui->send_mail_auto =
    	pm_grid_attach_check(grid, 1, ++row, 1, 1, _("_Send queued mail automatically every"));
	spinbutton_adj = gtk_adjustment_new(10, 1, 120, 1, 10, 0);
	pui->send_mail_minutes = gtk_spin_button_new(spinbutton_adj, 1, 0);
	gtk_widget_set_hexpand(pui->send_mail_minutes, TRUE);
	pm_grid_attach(grid, pui->send_mail_minutes, 2, row, 1, 1);
	label = gtk_label_new(_("minutes"));
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	pm_grid_attach(grid, label, 3, row, 1, 1);

    pui->edit_headers =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Edit headers in external editor"));
    pui->reply_include_html_parts =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Include HTML parts as text "
                                                     "when replying or forwarding"));

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Main window group
 */

static void
pm_grid_add_main_window_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkAdjustment *scroll_adj;
    GtkWidget *label;

    pm_grid_attach(grid, pm_group_label(_("Main window")), 0, row, 3, 1);

    pui->previewpane =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Use preview pane"));

    pui->mblist_show_mb_content_info =
        pm_grid_attach_check(grid, 1, ++row, 3, 1,
                             _("Show message counts in mailbox list"));

    pui->layout_type = create_layout_types_menu();
    pm_grid_attach(grid, pui->layout_type, 1, ++row, 2, 1);

    pui->view_message_on_open =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Automatically view message "
                                                     "when mailbox opened"));
    pui->ask_before_select =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Ask me before selecting a different "
                                                     "mailbox to show an unread message"));

    pui->pgdownmod =
        pm_grid_attach_check(grid, 1, ++row, 1, 1, _("Page Up/Page Down keys "
                                                     "scroll text by"));
    scroll_adj = gtk_adjustment_new(50.0, 10.0, 100.0, 5.0, 10.0, 0.0);
    pui->pgdown_percent = gtk_spin_button_new(scroll_adj, 1, 0);
    gtk_widget_set_sensitive(pui->pgdown_percent, FALSE);
    gtk_widget_set_hexpand(pui->pgdown_percent, TRUE);
    pm_grid_attach(grid, pui->pgdown_percent, 2, row, 1, 1);

    label = gtk_label_new(_("percent"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    pm_grid_attach(grid, label, 3, row, 1, 1);

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Message window group
 */

static void
pm_grid_add_message_window_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);

    pm_grid_attach(grid, pm_group_label(_("Message window")), 0, row, 3, 1);

    pm_grid_attach_label(grid, 1, ++row, 1, 1, _("After moving a message:"));

    pui->action_after_move_menu = create_action_after_move_menu();
    pm_combo_box_set_level(pui->action_after_move_menu,
                           balsa_app.mw_action_after_move);
    pm_grid_attach(grid, pui->action_after_move_menu, 2, row, 1, 1);

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Sorting and threading group
 */

static void
pm_grid_add_threading_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkWidget *label;

    pm_grid_attach(grid, pm_group_label(_("Sorting and threading")), 0, row, 3, 1);

    label = pm_grid_attach_label(grid, 1, ++row, 1, 1, _("Default sort column:"));
    gtk_widget_set_hexpand(label, FALSE);
    pui->default_sort_field =
        pm_grid_attach_pref_menu(grid, 2, row, 1, 1,
                                 sort_field_label, G_N_ELEMENTS(sort_field_label),
                                 &pui->sort_field_index);

    pui->thread_messages_check =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Thread messages by default"));
    pui->tree_expand_check =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Expand threads on open"));

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Fonts group
 */

/*
 * Create the group, with two font buttons and a check box for using
 * the default size; if either font does not specify a point size,
 * initially check the box.
 *
 * If the box is checked when the prefs are applied, both fonts will be
 * saved with no point size specification.
 */

static void
pm_grid_add_preview_font_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    gboolean use_default_font_size = FALSE;

    pm_grid_attach(grid, pm_group_label(_("Fonts")), 0, row, 3, 1);

    pui->use_system_fonts =
        gtk_check_button_new_with_label(_("Use system fonts"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->use_system_fonts),
                                 balsa_app.use_system_fonts);
    gtk_widget_set_hexpand(pui->use_system_fonts, TRUE);
    pm_grid_attach(grid, pui->use_system_fonts,
                   1, ++row, 1, 1);

    pui->use_default_font_size =
        gtk_check_button_new_with_label(_("Use default font size"));
    gtk_widget_set_hexpand(pui->use_default_font_size, FALSE);
    pm_grid_attach(grid, pui->use_default_font_size,
                   2, row, 1, 1);

    if (pm_grid_attach_font_button(grid, 1, ++row, 1, 1,
                                  _("Message font:"),
                                  balsa_app.message_font,
                                  &pui->message_font_label,
                                  &pui->message_font_button))
        use_default_font_size = TRUE;

    if (pm_grid_attach_font_button(grid, 1, ++row, 1, 1,
                                   _("Subject font:"),
                                   balsa_app.subject_font,
                                   &pui->subject_font_label,
                                   &pui->subject_font_button))
        use_default_font_size = TRUE;

    if (use_default_font_size) {
        gtk_font_button_set_show_size(GTK_FONT_BUTTON
                                      (pui->message_font_button), FALSE);
        gtk_font_button_set_show_size(GTK_FONT_BUTTON
                                      (pui->subject_font_button), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                     (pui->use_default_font_size), TRUE);
    }

    if (balsa_app.use_system_fonts) {
        gtk_widget_set_sensitive(pui->message_font_label, FALSE);
        gtk_widget_set_sensitive(pui->message_font_button, FALSE);
        gtk_widget_set_sensitive(pui->subject_font_label, FALSE);
        gtk_widget_set_sensitive(pui->subject_font_button, FALSE);
        gtk_widget_set_sensitive(pui->use_default_font_size, FALSE);
    }

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Quoted and flowed text group
 */

static void
pm_grid_add_quoted_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkAdjustment *spinbutton_adj;

    /* Quoted text regular expression */
    /* and RFC2646-style flowed text  */

    pm_grid_attach(grid, pm_group_label(_("Quoted and flowed text")), 0, row, 3, 1);

    pui->mark_quoted =
        pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Mark quoted text"));

    pui->quote_pattern =
        pm_grid_attach_entry(grid, 1, ++row, 1, 1, _("Quoted text regular expression:"));

    pui->browse_wrap =
        pm_grid_attach_check(grid, 1, ++row, 1, 1, _("Wrap text at"));

    spinbutton_adj = gtk_adjustment_new(1.0, 40.0, 200.0, 1.0, 5.0, 0.0);
    pui->browse_wrap_length = gtk_spin_button_new(spinbutton_adj, 1, 0);
    gtk_widget_set_hexpand(pui->browse_wrap_length, TRUE);
    pm_grid_attach(grid, pui->browse_wrap_length, 2, row, 1, 1);
    pm_grid_attach_label(grid, 3, row, 1, 1, _("characters"));

    pm_grid_set_next_row(grid, ++row);
}

#ifdef HAVE_HTML_WIDGET
static void
set_html_cache_label_str(GtkLabel *label)
{
	gchar *size_str;
	gchar *label_str;

	size_str = g_format_size(libbalsa_html_cache_size());
	label_str = g_strdup_printf(_("HTTP cache size: %s"), size_str);
	g_free(size_str);
	gtk_label_set_text(label, label_str);
	g_free(label_str);
}

static void
clear_html_cache_cb(GtkButton G_GNUC_UNUSED *button,
					gpointer				 user_data)
{
	libbalsa_html_clear_cache();
	g_thread_yield();               /* ...give the webkit thread a chance to do the cleanup work */
	g_usleep(2500);
	set_html_cache_label_str(GTK_LABEL(user_data));
}
#endif

/*
 * Multipart group
 */

static void
pm_grid_add_alternative_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
#ifdef HAVE_HTML_WIDGET
    GtkWidget *label;
    GtkWidget *button;
#endif

    /* handling of multipart/alternative */

    pm_grid_attach(grid, pm_group_label(_("Display of multipart/alternative parts")),
                   0, row, 3, 1);

    pui->display_alt_plain =
	pm_grid_attach_check(grid, 1, ++row, 1, 1, _("Prefer text/plain over HTML"));

#ifdef HAVE_HTML_WIDGET
    /* Translators: per-sender database of exceptions over global HTTP
     * preferences (display HTML vs. plain, auto-load external items) */
    button = gtk_button_new_with_label(_("Manage exceptions…"));
    g_signal_connect_swapped(button, "clicked",
        G_CALLBACK(libbalsa_html_pref_dialog_run), property_box);
    pm_grid_attach(grid, button, 2, row, 1, 1);

    label = gtk_label_new(NULL);
    set_html_cache_label_str(GTK_LABEL(label));
    pm_grid_attach(grid, label, 1, ++row, 1, 1);
    button = gtk_button_new_with_label(_("Clear HTTP cache…"));
    pm_grid_attach(grid, button, 2, row, 1, 1);
    g_signal_connect(button, "clicked", G_CALLBACK(clear_html_cache_cb), label);
#endif

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Message colors group
 */

static void
pm_grid_add_message_colors_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    gint i;

    pm_grid_attach(grid, pm_group_label(_("Message colors")), 0, row, 3, 1);

    for(i = 0; i < MAX_QUOTED_COLOR; i++) {
        gchar *text;

        text = g_strdup_printf(_("Quote level %d color"), i+1);
        pui->quoted_color[i] = pm_grid_attach_color_box(grid, 1, ++row, 1, 1, text);
        g_free(text);
    }

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Link color group
 */

static void
pm_grid_add_link_color_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);

    pm_grid_attach(grid, pm_group_label(_("Link color")), 0, row, 3, 1);
    pui->url_color =
        pm_grid_attach_color_box(grid, 1, ++row, 1, 1, _("Hyperlink color"));

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Format group
 */

static void
pm_grid_add_display_formats_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);

    pm_grid_attach(grid, pm_group_label(_("Format")), 0, row, 3, 1);

    pui->date_format =
        pm_grid_attach_entry(grid, 1, ++row, 1, 1, _("Date encoding (for strftime):"));
    pui->selected_headers =
        pm_grid_attach_entry(grid, 1, ++row, 1, 1, _("Selected headers:"));

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Broken 8-bit group
 */

static void
pm_grid_add_broken_8bit_codeset_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GSList *radio_group = NULL;

    /* treatment of messages with 8-bit chars, but without proper MIME encoding */

    pm_grid_attach(grid, pm_group_label(_("National (8-bit) characters in broken messages "
                                          "without codeset header")), 0, row, 3, 1);

    pui->convert_unknown_8bit[0] =
	GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(radio_group,
							 _("display as “?”")));
    pm_grid_attach(grid, GTK_WIDGET(pui->convert_unknown_8bit[0]),
                   1, ++row, 2, 1);
    radio_group =
	gtk_radio_button_get_group(GTK_RADIO_BUTTON(pui->convert_unknown_8bit[0]));

    pui->convert_unknown_8bit[1] =
	GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(radio_group,
							 _("display in codeset")));
    pm_grid_attach(grid, GTK_WIDGET(pui->convert_unknown_8bit[1]),
                    1, ++row, 1, 1);

    pui->convert_unknown_8bit_codeset = libbalsa_charset_button_new();
    gtk_combo_box_set_active(GTK_COMBO_BOX
                             (pui->convert_unknown_8bit_codeset),
                             balsa_app.convert_unknown_8bit_codeset);
    gtk_widget_set_hexpand(pui->convert_unknown_8bit_codeset, TRUE);
    pm_grid_attach(grid, pui->convert_unknown_8bit_codeset,
                   2, row, 1, 1);

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Information messages group
 */

static void
pm_grid_add_information_messages_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);

    pm_grid_attach(grid, pm_group_label(_("Information messages")), 0, row, 3, 1);

    pui->information_message_menu =
	pm_grid_attach_information_menu(grid, 1, ++row, 1, 1,
                                        _("Information messages:"),
                                        balsa_app.information_message);
    pui->warning_message_menu =
	pm_grid_attach_information_menu(grid, 1, ++row, 1, 1,
                                        _("Warning messages:"),
                                        balsa_app.warning_message);
    pui->error_message_menu =
	pm_grid_attach_information_menu(grid, 1, ++row, 1, 1,
                                        _("Error messages:"),
                                        balsa_app.error_message);
    pui->fatal_message_menu =
	pm_grid_attach_information_menu(grid, 1, ++row, 1, 1,
	                                _("Fatal error messages:"),
                                        balsa_app.fatal_message);
    pui->debug_message_menu =
	pm_grid_attach_information_menu(grid, 1, ++row, 1, 1,
                                        _("Debug messages:"),
                                        balsa_app.debug_message);

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Progress dialog group
 */

static void
pm_grid_add_progress_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);

    pm_grid_attach(grid, pm_group_label(_("Display progress dialog")), 0, row, 3, 1);
    pui->send_progress_dlg = pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Display progress dialog when sending messages"));
    pui->recv_progress_dlg = pm_grid_attach_check(grid, 1, ++row, 2, 1, _("Display progress dialog when retrieving messages"));

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Address books group
 */

static void
pm_grid_add_address_books_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkWidget *scrolledwindow;
    GtkWidget *address_book_add_menu;
    GtkWidget *vbox;

    pm_grid_attach(grid, pm_group_label(_("Address books")), 0, row, 3, 1);

    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolledwindow, TRUE);
    pm_grid_attach(grid, scrolledwindow, 1, ++row, 1, 1);
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
        gtk_tree_view_column_new_with_attributes(_("Address book name"),
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

    address_book_add_menu =
        balsa_address_book_add_menu(address_book_change,
                                    GTK_WINDOW(property_box));
    g_object_weak_ref(G_OBJECT(grid), (GWeakNotify) g_object_unref,
                      address_book_add_menu);
    g_object_ref_sink(address_book_add_menu);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, HIG_PADDING);
    add_button_to_box(_("_Add"),
                      G_CALLBACK(add_menu_cb),
                      address_book_add_menu, vbox);

    add_button_to_box(_("_Modify"),
                      G_CALLBACK(address_book_edit_cb),
                      tree_view, vbox);
    add_button_to_box(_("_Delete"),
                      G_CALLBACK(address_book_delete_cb),
                      tree_view, vbox);
    add_button_to_box(_("_Set as default"), 
                      G_CALLBACK(address_book_set_default_cb),
                      tree_view, vbox);
    pm_grid_attach(grid, vbox, 2, row, 1, 1);

    update_address_books();

    pm_grid_set_next_row(grid, ++row);
}

#if !(HAVE_GSPELL || HAVE_GTKSPELL)
static void
pm_grid_add_misc_spelling_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);

    pm_grid_attach(grid, pm_group_label(_("Miscellaneous spelling settings")),
                   0, row, 3, 1);

    pui->spell_check_sig =
        pm_grid_attach_check(grid, 1, ++row, 1, 1 , _("Check signature"));
    pui->spell_check_quoted =
        pm_grid_attach_check(grid, 1, ++row, 1, 1, _("Check quoted"));

    pm_grid_set_next_row(grid, ++row);
}
#endif                          /* !(HAVE_GSPELL || HAVE_GTKSPELL) */

/*
 * Startup options group
 */

static void
pm_grid_add_startup_options_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);

    pm_grid_attach(grid, pm_group_label(_("Start-up options")), 0, row, 3, 1);

    pui->open_inbox_upon_startup =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Open Inbox upon start-up"));
    pui->check_mail_upon_startup =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Check mail upon start-up"));
    pui->remember_open_mboxes =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Remember open mailboxes "
                                    "between sessions"));

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Folder scanning group
 */

static void
pm_grid_add_folder_scanning_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkWidget *label;
    GtkAdjustment *scan_adj;

    pm_grid_attach(grid, pm_group_label(_("Folder scanning")), 0, row, 3, 1);

    pm_grid_attach_label(grid, 1, ++row, 3, 1,
                        _("Choose depth 1 for fast start-up; "
                          "this defers scanning some folders. "
                          "To see more of the tree at start-up, "
                          "choose a greater depth."));

    label =
        pm_grid_attach_label(grid, 1, ++row, 1, 1,
                             _("Scan local folders to depth"));
    gtk_widget_set_hexpand(label, FALSE);
    scan_adj = gtk_adjustment_new(1.0, 1.0, 99.0, 1.0, 5.0, 0.0);
    pui->local_scan_depth = gtk_spin_button_new(scan_adj, 1, 0);
    gtk_widget_set_hexpand(pui->local_scan_depth, TRUE);
    pm_grid_attach(grid, pui->local_scan_depth, 2, row, 1, 1);

    label =
        pm_grid_attach_label(grid, 1, ++row, 1, 1,
                             _("Scan IMAP folders to depth"));
    gtk_widget_set_hexpand(label, FALSE);

    scan_adj = gtk_adjustment_new(1.0, 1.0, 99.0, 1.0, 5.0, 0.0);
    pui->imap_scan_depth = gtk_spin_button_new(scan_adj, 1, 0);
    gtk_widget_set_hexpand(pui->imap_scan_depth, TRUE);
    pm_grid_attach(grid, pui->imap_scan_depth, 2, row, 1, 1);

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Miscellaneous group
 */

static void
pm_grid_add_misc_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    GtkAdjustment *close_spinbutton_adj;

    pm_grid_attach(grid, pm_group_label(_("Miscellaneous")), 0, row, 3, 1);

    pui->empty_trash =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Empty trash on exit"));

    pui->close_mailbox_auto =
        pm_grid_attach_check(grid, 1, ++row, 1, 1,
                             _("Close mailbox if unused more than"));
    gtk_widget_set_hexpand(pui->close_mailbox_auto, FALSE);

    close_spinbutton_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 0);
    pui->close_mailbox_minutes =
	gtk_spin_button_new(close_spinbutton_adj, 1, 0);
    gtk_widget_set_hexpand(pui->close_mailbox_minutes, TRUE);
    gtk_widget_show(pui->close_mailbox_minutes);
    gtk_widget_set_sensitive(pui->close_mailbox_minutes, FALSE);
    pm_grid_attach(grid, pui->close_mailbox_minutes, 2, row, 1, 1);

    pm_grid_attach_label(grid, 3, row, 1, 1, _("minutes"));

#ifdef ENABLE_SYSTRAY
    pui->enable_systray_icon =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Enable System Tray Icon support"));
#endif

    pui->enable_dkim_checks =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Enable checking DKIM signatures and DMARC compliance"));

    pm_grid_set_next_row(grid, ++row);
}

/*
 * Message deleting group
 */

static void
pm_grid_add_deleting_messages_group(GtkWidget * grid_widget)
{
    GtkGrid *grid = (GtkGrid *) grid_widget;
    gint row = pm_grid_get_next_row(grid);
    gchar *text;
    GtkAdjustment *expunge_spinbutton_adj;

    pm_grid_attach(grid, pm_group_label(_("Deleting messages")), 0, row, 3, 1);

    /* Translators: this used to be "using Mailbox -> Hide messages";
     * the UTF-8 string for the right-arrow symbol is broken out to
     * avoid msgconv problems. */
    text = g_strdup_printf(_("The following setting is global, "
			     "but may be overridden "
			     "for the selected mailbox "
			     "using Mailbox %s Hide messages:"),
			   "\342\226\272");
    pm_grid_attach_label(grid, 1, ++row, 3, 1, text);
    g_free(text);
    pui->hide_deleted =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Hide messages marked as deleted"));

    pm_grid_attach_label(grid, 1, ++row, 3, 1, _("The following settings are global:"));

    pui->expunge_on_close =
        pm_grid_attach_check(grid, 1, ++row, 3, 1, _("Expunge deleted messages "
                                                     "when mailbox is closed"));

    pui->expunge_auto =
        pm_grid_attach_check(grid, 1, ++row, 1, 1, _("…and if unused more than"));
    gtk_widget_set_hexpand(pui->expunge_auto, FALSE);

    expunge_spinbutton_adj = gtk_adjustment_new(120, 1, 1440, 1, 10, 0);
    pui->expunge_minutes = gtk_spin_button_new(expunge_spinbutton_adj, 1, 0);
    gtk_widget_set_hexpand(pui->expunge_minutes, TRUE);
    gtk_widget_show(pui->expunge_minutes);
    gtk_widget_set_sensitive(pui->expunge_minutes, FALSE);
    pm_grid_attach(grid, pui->expunge_minutes, 2, row, 1, 1);

    pm_grid_attach_label(grid, 3, row, 1, 1, _("minutes"));

    pm_grid_set_next_row(grid, ++row);
}

/***************************
 *
 * End of preferences groups
 *
 **************************/

/*
 * pm_append_page
 *
 * Put the child on the stack, and populate the tree store data
 */

static void
pm_append_page(GtkWidget    * stack,
               GtkWidget    * child,
               const gchar  * text,
               GtkTreeStore * store,
               GtkTreeIter  * iter)
{
    gtk_stack_add_named((GtkStack *) stack, child, text);

    gtk_tree_store_set(store, iter,
                       PM_TEXT_COL, gettext(text),
                       PM_HELP_COL, text,
                       PM_CHILD_COL, child,
                       -1);
}

/********************************************************
 *
 * The pages
 *
 * Each page consists of its own GtkGrid, which it passes
 * to the various groups that belong on that page.
 *
 *******************************************************/

static GtkWidget *
pm_mailserver_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_remote_mailbox_servers_group(grid);
    pm_grid_add_local_mail_group(grid);
    pm_grid_add_outgoing_mail_group(grid);

    return grid;
}

static GtkWidget *
pm_incoming_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_checking_group(grid);
    pm_grid_add_new_mail_notify_group(grid);
    pm_grid_add_mdn_group(grid);

    return grid;
}

static GtkWidget *
pm_outgoing_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_word_wrap_group(grid);
    pm_grid_add_other_options_group(grid);

    return grid;
}

static GtkWidget *
pm_display_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_main_window_group(grid);
    pm_grid_add_message_window_group(grid);

    return grid;
}

static GtkWidget *
pm_threading_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_threading_group(grid);

    return grid;
}

static GtkWidget *
pm_message_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_preview_font_group(grid);
    pm_grid_add_quoted_group(grid);
    pm_grid_add_alternative_group(grid);

    return grid;
}

static GtkWidget *
pm_colors_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_message_colors_group(grid);
    pm_grid_add_link_color_group(grid);

    return grid;
}

static GtkWidget *
pm_format_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_display_formats_group(grid);
    pm_grid_add_broken_8bit_codeset_group(grid);

    return grid;
}

static GtkWidget *
pm_status_messages_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_information_messages_group(grid);
    pm_grid_add_progress_group(grid);

    return grid;
}

static GtkWidget *
create_address_book_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_address_books_group(grid);

    return grid;
}

#if !(HAVE_GSPELL || HAVE_GTKSPELL)
static GtkWidget *
pm_spelling_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_misc_spelling_group(grid);

    return grid;
}
#endif                          /* !(HAVE_GSPELL || HAVE_GTKSPELL) */

static GtkWidget *
pm_startup_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_startup_options_group(grid);
    pm_grid_add_folder_scanning_group(grid);

    return grid;
}

static GtkWidget *
pm_misc_page(void)
{
    GtkWidget *grid = pm_grid_new();

    pm_grid_add_misc_group(grid);
    pm_grid_add_deleting_messages_group(grid);

    return grid;
}

/*
 * End of pages
 */

/*********************************************************************
 *
 * Sections
 *
 * A section consists of several pages (that is, more than one)
 * covering a given part of the UI. It is given an iter for the parent
 * node in the GtkTreeView, and creates child iters for the children.
 *
 ********************************************************************/

static void
create_mail_options_section(GtkTreeStore * store,
                            GtkTreeIter  * parent,
                            GtkWidget    * stack)
{
    GtkTreeIter iter;

    pm_append_page(stack, pm_mailserver_page(), N_("Mail options"),
                   store, parent);

    gtk_tree_store_append(store, &iter, parent);
    pm_append_page(stack, pm_incoming_page(), N_("Incoming"),
                   store, &iter);

    gtk_tree_store_append(store, &iter, parent);
    pm_append_page(stack, pm_outgoing_page(), N_("Outgoing"),
                   store, &iter);
}

static void
create_display_section(GtkTreeStore * store,
                       GtkTreeIter  * parent,
                       GtkWidget    * stack)
{
    GtkTreeIter iter;

    pm_append_page(stack, pm_display_page(), N_("Display options"),
                   store, parent);

    gtk_tree_store_append(store, &iter, parent);
    pm_append_page(stack, pm_threading_page(), N_("Sort and thread"),
                   store, &iter);

    gtk_tree_store_append(store, &iter, parent);
    pm_append_page(stack, pm_message_page(), N_("Message"),
                   store, &iter);

    gtk_tree_store_append(store, &iter, parent);
    pm_append_page(stack, pm_colors_page(), N_("Colors"),
                   store, &iter);

    gtk_tree_store_append(store, &iter, parent);
    pm_append_page(stack, pm_format_page(), N_("Format"),
                   store, &iter);

    gtk_tree_store_append(store, &iter, parent);
    pm_append_page(stack, pm_status_messages_page(), N_("Status messages"),
                   store, &iter);
}

/*
 * End of sections
 */

/*
 * Idle handler for open_preferences_manager
 */

static gboolean
open_preferences_manager_idle(void)
{
    gchar *name;

    if (pui == NULL) {
        return FALSE;
    }

    name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                         (pui->mail_directory));
    if (g_strcmp0(name, balsa_app.local_mail_directory) != 0) {
        /* Chooser still hasn't been initialized. */
        g_free(name);
        return TRUE;
    }
    g_free(name);

    g_signal_connect(pui->mail_directory, "selection-changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    return FALSE;
}                               /* open_preferences_manager_idle */

/****************
 *
 * Public methods
 *
 ***************/

void
open_preferences_manager(GtkWidget * widget, gpointer data)
{
    GtkWidget *hbox;
    GtkWidget *content_area;
    GtkTreeStore *store;
    GtkWidget *view;
    GtkTreeSelection * selection;
    GtkWidget *stack;
    GtkWidget *active_win = data;
    gint i;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeIter iter;

    /* only one preferences manager window */
    if (already_open) {
        gtk_window_present_with_time(GTK_WINDOW(property_box),
                                     gtk_get_current_event_time());
        return;
    }

    pui = g_malloc(sizeof(PropertyUI));

    property_box =              /* must NOT be modal */
        gtk_dialog_new_with_buttons(_("Balsa Preferences"),
                                    GTK_WINDOW(active_win),
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    _("_OK"), GTK_RESPONSE_OK,
                                    _("_Apply"), GTK_RESPONSE_APPLY,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_Help"), GTK_RESPONSE_HELP,
                                    NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	content_area = gtk_dialog_get_content_area(GTK_DIALOG(property_box));
	gtk_container_add(GTK_CONTAINER(content_area), hbox);

    store = gtk_tree_store_new(PM_NUM_COLS,
                               G_TYPE_STRING,   /* PM_TEXT_COL     */
                               G_TYPE_STRING,   /* PM_HELP_COL     */
                               GTK_TYPE_WIDGET  /* PM_CHILD_COL    */
            );
    pui->view = view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    libbalsa_set_margins(view , BORDER_WIDTH);
    gtk_container_add(GTK_CONTAINER(hbox), view);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                 "text", PM_TEXT_COL,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);

    stack = gtk_stack_new();
    gtk_stack_set_transition_type((GtkStack *) stack,
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN);
    gtk_stack_set_transition_duration((GtkStack *) stack, 400);
    gtk_container_add(GTK_CONTAINER(hbox), stack);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    g_signal_connect(selection, "changed",
                     G_CALLBACK(pm_selection_changed), stack);

    already_open = TRUE;

    gtk_window_set_resizable(GTK_WINDOW(property_box), FALSE);
    g_object_set_data(G_OBJECT(property_box), "balsawindow", active_win);

    /* Create the pages */
    gtk_tree_store_append(store, &iter, NULL);
    create_mail_options_section(store, &iter, stack);

    gtk_tree_store_append(store, &iter, NULL);
    create_display_section(store, &iter, stack);

    gtk_tree_store_append(store, &iter, NULL);
    pm_append_page(stack, create_address_book_page(),
                   N_("Address books"), store, &iter);

#if !(HAVE_GSPELL || HAVE_GTKSPELL)
    gtk_tree_store_append(store, &iter, NULL);
    pm_append_page(stack, pm_spelling_page(),
                   N_("Spelling"), store, &iter);
#endif                          /* !(HAVE_GSPELL || HAVE_GTKSPELL) */

    gtk_tree_store_append(store, &iter, NULL);
    pm_append_page(stack, pm_startup_page(),
                   N_("Start-up"), store, &iter);

    gtk_tree_store_append(store, &iter, NULL);
    pm_append_page(stack, pm_misc_page(),
                   N_("Miscellaneous"), store, &iter);

    gtk_tree_view_expand_all(GTK_TREE_VIEW(view));

    set_prefs();
    /* Now that all the prefs have been set, we must desensitize the
     * buttons. */
    gtk_dialog_set_response_sensitive(GTK_DIALOG(property_box),
                                      GTK_RESPONSE_OK, FALSE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(property_box),
                                      GTK_RESPONSE_APPLY, FALSE);

    g_signal_connect(pui->recv_progress_dlg, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->send_progress_dlg, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->previewpane, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->layout_type, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->view_message_on_open, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->ask_before_select, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->pgdownmod, "toggled",
                     G_CALLBACK(pgdown_modified_cb), property_box);
    g_signal_connect(pui->pgdown_percent, "changed",
                     G_CALLBACK(pgdown_modified_cb), property_box);

    g_signal_connect(pui->mblist_show_mb_content_info, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
#if !(HAVE_GSPELL || HAVE_GTKSPELL)
    g_signal_connect(pui->spell_check_sig, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->spell_check_quoted, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
#endif                          /* !(HAVE_GSPELL || HAVE_GTKSPELL) */

    /* Connect signal in an idle handler, after the file chooser has
     * been initialized. */
    g_idle_add_full(G_PRIORITY_LOW,
                    (GSourceFunc) open_preferences_manager_idle,
                    NULL, NULL);
    g_signal_connect(pui->check_mail_auto, "toggled",
                     G_CALLBACK(timer_modified_cb), property_box);

    g_signal_connect(pui->check_mail_minutes, "changed",
                     G_CALLBACK(timer_modified_cb), property_box);

    g_signal_connect(pui->quiet_background_check, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->msg_size_limit, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(pui->check_imap, "toggled",
                     G_CALLBACK(imap_toggled_cb), property_box);

    g_signal_connect(pui->check_imap_inbox, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(pui->notify_new_mail_dialog, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

#ifdef HAVE_CANBERRA
    g_signal_connect(pui->notify_new_mail_sound, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
#endif

    g_signal_connect(pui->close_mailbox_auto, "toggled",
                     G_CALLBACK(mailbox_close_timer_modified_cb),
                     property_box);
    g_signal_connect(pui->close_mailbox_minutes, "changed",
                     G_CALLBACK(mailbox_close_timer_modified_cb),
                     property_box);

#ifdef ENABLE_SYSTRAY
    g_signal_connect(pui->enable_systray_icon, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
#endif

    g_signal_connect(pui->enable_dkim_checks, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(pui->hide_deleted, "toggled",
                     G_CALLBACK(filter_modified_cb), property_box);
    g_signal_connect(pui->expunge_on_close, "toggled",
                     G_CALLBACK(expunge_on_close_cb), property_box);
    g_signal_connect(pui->expunge_auto, "toggled",
                     G_CALLBACK(expunge_auto_cb), property_box);
    g_signal_connect(pui->expunge_minutes, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(pui->browse_wrap, "toggled",
                     G_CALLBACK(browse_modified_cb), property_box);
    g_signal_connect(pui->browse_wrap_length, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->wordwrap, "toggled",
                     G_CALLBACK(wrap_modified_cb), property_box);
    g_signal_connect(pui->wraplength, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->always_queue_sent_mail, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->send_mail_auto, "toggled",
                     G_CALLBACK(send_timer_modified_cb), property_box);
    g_signal_connect(pui->send_mail_minutes, "changed",
                     G_CALLBACK(send_timer_modified_cb), property_box);
    g_signal_connect(pui->copy_to_sentbox, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->autoquote, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->reply_include_html_parts, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->forward_attached, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* external editor */
    g_signal_connect(pui->edit_headers, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* arp */
    g_signal_connect(pui->quote_str, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->mark_quoted, "toggled",
                     G_CALLBACK(mark_quoted_modified_cb),
                     property_box);
    g_signal_connect(pui->quote_pattern, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* multipart/alternative */
    g_signal_connect(pui->display_alt_plain, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* convert 8-bit text with no charset header */
    g_signal_connect(pui->convert_unknown_8bit_codeset,
                     "changed", G_CALLBACK(properties_modified_cb),
                     property_box);

    /* message font */
    g_signal_connect(pui->use_system_fonts, "toggled",
                     G_CALLBACK(use_system_fonts_cb), property_box);
    g_signal_connect(pui->message_font_button, "font-set",
                     G_CALLBACK(font_modified_cb), property_box);
    g_signal_connect(pui->subject_font_button, "font-set",
                     G_CALLBACK(font_modified_cb), property_box);
    g_signal_connect(pui->use_default_font_size, "toggled",
                     G_CALLBACK(default_font_size_cb), property_box);


    g_signal_connect(pui->open_inbox_upon_startup, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->check_mail_upon_startup, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->remember_open_mboxes, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(pui->local_scan_depth, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->imap_scan_depth, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(pui->empty_trash, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* threading */
    g_signal_connect(pui->thread_messages_check, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(pui->tree_expand_check, "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);


    /* Date format */
    g_signal_connect(pui->date_format, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* Selected headers */
    g_signal_connect(pui->selected_headers, "changed",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* Colour */
    for (i = 0; i < MAX_QUOTED_COLOR; i++)
        g_signal_connect(pui->quoted_color[i], "color-set",
                         G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(pui->url_color, "color-set",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* handling of message parts with 8-bit chars without codeset headers */
    for (i = 0; i < NUM_CONVERT_8BIT_MODES; i++)
        g_signal_connect(pui->convert_unknown_8bit[i], "toggled",
                         G_CALLBACK(convert_8bit_cb), property_box);

    /* Gnome Property Box Signals */
    g_signal_connect(property_box, "response",
                     G_CALLBACK(response_cb), NULL);

    gtk_widget_show_all(GTK_WIDGET(property_box));

}                               /* open_preferences_manager */

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
        LibBalsaMailbox *mailbox;

        if (!(mbnode = list->data))
            continue;

        mailbox = balsa_mailbox_node_get_mailbox(mbnode);
        if (LIBBALSA_IS_MAILBOX_POP3(mailbox))
            protocol = "POP3";
        else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox))
            protocol = "IMAP";
        else
            protocol = _("Unknown");

        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           MS_PROT_COLUMN, protocol,
                           MS_NAME_COLUMN,
                           libbalsa_mailbox_get_name(mailbox),
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
    properties_modified_cb(NULL, property_box);
}
