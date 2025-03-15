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


#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "assistant_page_directory.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include "balsa-app.h"
#include "save-restore.h"
#include "misc.h"
#include "server.h"

#define INBOX_NAME    "Inbox"
#define OUTBOX_NAME   "Outbox"
#define SENTBOX_NAME  "Sentbox"
#define DRAFTBOX_NAME "Draftbox"
#define TRASH_NAME    "Trash"

static const gchar * const init_mbnames[NUM_EDs] = {
    N_("_Inbox:"), N_("_Outbox:"), N_("_Sentbox:"), N_("_Draftbox:"),
    N_("_Trash:")
};

static void balsa_druid_page_directory_prepare(GtkAssistant * druid,
                                               GtkWidget * page,
                                               BalsaDruidPageDirectory * dir);
static void balsa_druid_page_directory_back(GtkAssistant * druid,
                                            GtkWidget * page,
                                            BalsaDruidPageDirectory * dir);
static void balsa_druid_page_directory_next(GtkAssistant * druid,
                                            GtkWidget * page,
                                            BalsaDruidPageDirectory * dir);
static void unconditional_mailbox(const gchar * path,
                                  const gchar * prettyname,
                                  LibBalsaMailbox ** box, gchar ** error);

static void
unconditional_mailbox(const gchar * path, const gchar * prettyname,
                      LibBalsaMailbox ** box, gchar ** error)
{
    gchar *folder;

    if ((*error) != NULL)
        return;

    folder = g_path_get_dirname(path);
    if (balsa_init_create_to_directory(folder, error)) {
        /*TRUE->error */
        g_free(folder);
        return;
    }
    g_free(folder);

    *box = libbalsa_mailbox_local_new(path, TRUE);
    if (*box == NULL) {
        *error = g_strdup_printf(_("Could not create mailbox at path “%s”."), path);
        return;
    }

    libbalsa_mailbox_set_name(*box, gettext(prettyname));
    config_mailbox_add(*box, (char *) prettyname);
    if (box == &balsa_app.outbox)
        libbalsa_mailbox_set_no_reassemble(*box, TRUE);
}

static void
verify_mailbox_entry(GtkWidget * entry, const gchar * name,
                     LibBalsaMailbox ** mailbox, gboolean * verify)
{
    const gchar *text;
    gchar *error;

    if (!*verify)
        return;

    text = gtk_entry_get_text(GTK_ENTRY(entry));
    error = NULL;
    unconditional_mailbox(text, name, mailbox, &error);

    if (error) {
        GtkWidget *dlg =
            gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_ancestor
                                              (GTK_WIDGET(entry),
                                               GTK_TYPE_WINDOW)),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_OK,
                                   _("Problem verifying path “%s”:\n%s"),
                                   text, error);
        g_free(error);
        *verify = FALSE;
        g_signal_connect(dlg, "response",
                         G_CALLBACK(gtk_widget_destroy), NULL);
        gtk_widget_show_all(dlg);
    }
}

static void
verify_button_clicked_cb(GtkWidget * button, gpointer data)
{
    BalsaDruidPageDirectory *dir = data;
    gboolean verify = TRUE;

    verify_mailbox_entry(dir->inbox, INBOX_NAME, &balsa_app.inbox,
                         &verify);
    verify_mailbox_entry(dir->outbox, OUTBOX_NAME, &balsa_app.outbox,
                         &verify);
    verify_mailbox_entry(dir->sentbox, SENTBOX_NAME, &balsa_app.sentbox,
                         &verify);
    verify_mailbox_entry(dir->draftbox, DRAFTBOX_NAME, &balsa_app.draftbox,
                         &verify);
    verify_mailbox_entry(dir->trash, TRASH_NAME, &balsa_app.trash,
                         &verify);
    gtk_assistant_set_page_complete(dir->druid, dir->page, verify);
}

static GtkWidget *
verify_button(BalsaDruidPageDirectory * dir)
{
    GtkWidget *button;

    button = gtk_button_new_with_mnemonic(_("_Verify locations"));
    g_signal_connect(button, "clicked",
                     G_CALLBACK(verify_button_clicked_cb), dir);
    gtk_widget_show(button);
    return button;
}

static void
entry_changed_cb(GtkEditable * editable, gpointer data)
{
    BalsaDruidPageDirectory *dir = data;

    gtk_assistant_set_page_complete(dir->druid, dir->page, FALSE);
}

static void
balsa_druid_page_directory_init(BalsaDruidPageDirectory * dir,
                                GtkWidget * page,
                                GtkAssistant * druid)
{
    GtkGrid *grid;
    GtkWidget *label_widget;
    int i;
    GtkWidget **init_widgets[NUM_EDs];
    gchar *init_presets[NUM_EDs] = { NULL, NULL, NULL, NULL, NULL };

    dir->druid = druid;
    dir->paths_locked = FALSE;

    grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_column_spacing(grid, HIG_PADDING);

    label_widget = gtk_label_new(_("Please verify the locations "
                                   "of your default mail files. "
                                   "These will be created if necessary."));
    gtk_label_set_line_wrap(GTK_LABEL(label_widget), TRUE);
    gtk_widget_set_hexpand(label_widget, TRUE);
    gtk_widget_set_vexpand(label_widget, FALSE);
    gtk_widget_set_margin_bottom(label_widget, 12);

    gtk_grid_attach(grid, label_widget, 0, 0, 2, 1);

    init_presets[INBOX] = libbalsa_guess_mail_spool();

    init_widgets[INBOX] = &(dir->inbox);
    init_widgets[OUTBOX] = &(dir->outbox);
    init_widgets[SENTBOX] = &(dir->sentbox);
    init_widgets[DRAFTBOX] = &(dir->draftbox);
    init_widgets[TRASH] = &(dir->trash);

    for (i = 0; i < NUM_EDs; i++) {
        gchar *preset;
        GtkWidget *entry;

        if (init_presets[i])
            preset = init_presets[i];
        else
            preset = g_strdup("[Dummy value]");

        entry = balsa_init_add_grid_entry(grid, i, _(init_mbnames[i]),
                                          preset, NULL, NULL, NULL,
                                          init_widgets[i]);
        g_signal_connect(entry, "changed",
                         G_CALLBACK(entry_changed_cb), dir);

        g_free(preset);
    }

    gtk_widget_set_vexpand(GTK_WIDGET(grid), FALSE);
    gtk_widget_set_valign(GTK_WIDGET(grid), GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(page), GTK_WIDGET(grid));
    gtk_widget_show_all(GTK_WIDGET(grid));

    gtk_container_add(GTK_CONTAINER(page), verify_button(dir));

    g_signal_connect(druid, "prepare",
                     G_CALLBACK(balsa_druid_page_directory_prepare),
                     dir);
    dir->my_num = 98765;
    dir->need_set = FALSE;
}


void
balsa_druid_page_directory(GtkAssistant * druid)
{
    BalsaDruidPageDirectory *dir;

    dir = g_new0(BalsaDruidPageDirectory, 1);
    dir->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_assistant_append_page(druid, dir->page);
    gtk_assistant_set_page_title(druid, dir->page, _("Mail Files"));
    balsa_druid_page_directory_init(dir, dir->page, druid);
    g_object_weak_ref(G_OBJECT(druid), (GWeakNotify)g_free, dir);
}

static void
balsa_druid_page_directory_prepare(GtkAssistant * druid,
                                   GtkWidget * page,
                                   BalsaDruidPageDirectory * dir)
{
    gchar *buf;
    gint current_page_no = gtk_assistant_get_current_page(druid);

    if(page != dir->page) { /* This is not the page to be prepared. */
        if(dir->need_set) {
            if(current_page_no > dir->my_num)
                balsa_druid_page_directory_next(druid, page, dir);
            else
                balsa_druid_page_directory_back(druid, page, dir);
            dir->need_set = FALSE;
        }
        return;
    }
    dir->my_num = current_page_no;
    /* We want a change in the local mailroot to be reflected in the
     * directories here, but we don't want to trash user's custom
     * settings if needed. Hence the paths_locked variable; it should
     * work pretty well, because only a movement backwards should
     * change the mailroot; going forward should not lock the paths:
     * envision an error occurring; upon return to the Dir page the
     * entries should be the same.
     */

    if (!dir->paths_locked) {
        buf = g_build_filename(balsa_app.local_mail_directory, "outbox",
                               NULL);
        gtk_entry_set_text(GTK_ENTRY(dir->outbox), buf);
        g_free(buf);

        buf = g_build_filename(balsa_app.local_mail_directory, "sentbox",
                               NULL);
        gtk_entry_set_text(GTK_ENTRY(dir->sentbox), buf);
        g_free(buf);

        buf = g_build_filename(balsa_app.local_mail_directory, "draftbox",
                               NULL);
        gtk_entry_set_text(GTK_ENTRY(dir->draftbox), buf);
        g_free(buf);

        buf = g_build_filename(balsa_app.local_mail_directory, "trash",
                               NULL);
        gtk_entry_set_text(GTK_ENTRY(dir->trash), buf);
        g_free(buf);
    }

    /* Don't let them continue unless all entries have something. */
    gtk_assistant_set_page_complete(druid, page, FALSE);

    dir->need_set = TRUE;
}


static void
balsa_druid_page_directory_back(GtkAssistant *druid, GtkWidget *page,
                                BalsaDruidPageDirectory * dir)
{
    dir->paths_locked = FALSE;
}

static void
balsa_druid_page_directory_next(GtkAssistant *druid, GtkWidget *page,
                                BalsaDruidPageDirectory * dir)
{
    dir->paths_locked = TRUE;
}
