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

#include "balsa-druid-page-directory.h"

#include "balsa-app.h"
#include "save-restore.h"
#include "misc.h"
#include "mutt.h"
#include "url.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static gchar *init_mbnames[NUM_EDs] =
    { N_("_Inbox:"), N_("_Outbox:"), N_("_Sentbox:"), N_("_Draftbox:"),
    N_("_Trash:")
};

static void unconditional_mailbox(const gchar * path,
                                  const gchar * prettyname,
                                  LibBalsaMailbox ** box, gchar ** error);

static void
unconditional_mailbox(const gchar * path, const gchar * prettyname,
                      LibBalsaMailbox ** box, gchar ** error)
{
    gchar *dup;
    gchar *index;
    ciss_url_t url;
    gboolean ssl = FALSE;

    if ((*error) != NULL)
        return;

    dup = g_strdup(path);
    index = strrchr(dup, G_DIR_SEPARATOR);

    if (index == NULL) {
        (*error) =
            g_strdup_printf(_
                            ("The pathname \"%s\" must be specified"
                             " canonically -- it must start with a \'/\'."),
                            dup);
        g_free(dup);
        return;
    }

    *index = '\0';           /*Split off the dirs from the file. */

    if (balsa_init_create_to_directory(dup, error)) {
        /*TRUE->error */
        g_free(dup);
        return;
    }

    *index = G_DIR_SEPARATOR;

    url_parse_ciss(&url, dup);

    switch (url.scheme) {
    case U_IMAPS:
        ssl = TRUE;
    case U_IMAP:
        *box = (LibBalsaMailbox *) libbalsa_mailbox_imap_new();
        libbalsa_mailbox_imap_set_path((LibBalsaMailboxImap *) * box,
                                       url.path);
        break;
    case U_POPS:
        ssl = TRUE;
    case U_POP:
        *box = (LibBalsaMailbox *) libbalsa_mailbox_pop3_new();
        break;
    case U_FILE:
        *box =
            (LibBalsaMailbox *) libbalsa_mailbox_local_new(url.path, TRUE);
        break;
    default:
        *box = (LibBalsaMailbox *) libbalsa_mailbox_local_new(path, TRUE);
    }

    if (url.host && LIBBALSA_IS_MAILBOX_REMOTE(*box)) {
        libbalsa_server_set_host(LIBBALSA_MAILBOX_REMOTE_SERVER(*box),
                                 url.host
#ifdef USE_SSL
                                 , ssl
#endif
            );
        libbalsa_server_set_username(LIBBALSA_MAILBOX_REMOTE_SERVER(*box),
                                     getenv("USER"));
    }
    g_free(dup);


    if (*box == NULL) {
        if (strcmp("/var/spool/mail/", path)) {
            (*error) =
                g_strdup_printf(_
                                ("The mailbox \"%s\" does not appear to be valid.\n"
                                 "Your system does not allow for creation of mailboxes\n"
                                 "in /var/spool/mail. Balsa wouldn't function properly\n"
                                 "until the system created the mailboxes. Please change\n"
                                 "the mailbox path or check your system configuration."),
                                path);
        } else {
            (*error) =
                g_strdup_printf(_
                                ("The mailbox \"%s\" does not appear to be valid."),
                                path);
        }
        return;
    }

    (*box)->name = g_strdup(gettext(prettyname));

    config_mailbox_add(*box, (char *) prettyname);
}

/* here are local prototypes */
static void balsa_druid_page_directory_init(BalsaDruidPageDirectory * dir,
                                            GnomeDruidPageStandard * page,
                                            GnomeDruid * druid);
static void balsa_druid_page_directory_prepare(GnomeDruidPage * page,
                                               GnomeDruid * druid,
                                               BalsaDruidPageDirectory *
                                               dir);
static gboolean balsa_druid_page_directory_next(GnomeDruidPage * page,
                                                GtkWidget * druid,
                                                BalsaDruidPageDirectory *
                                                dir);
static gboolean balsa_druid_page_directory_back(GnomeDruidPage * page,
                                                GtkWidget * druid,
                                                BalsaDruidPageDirectory *
                                                dir);

static void
balsa_druid_page_directory_init(BalsaDruidPageDirectory * dir,
                                GnomeDruidPageStandard * page,
                                GnomeDruid * druid)
{
    GtkTable *table;
    GtkLabel *label;
    int i;
    GtkWidget **init_widgets[NUM_EDs];
    gchar *imap_inbox = libbalsa_guess_imap_inbox();
    gchar *init_presets[NUM_EDs] = { NULL, NULL, NULL, NULL, NULL };

    dir->paths_locked = FALSE;

    dir->emaster.setbits = 0;
    dir->emaster.numentries = 0;
    dir->emaster.donemask = 0;

    table = GTK_TABLE(gtk_table_new(NUM_EDs + 1, 2, FALSE));

    label =
        GTK_LABEL(gtk_label_new
                  (_
                   ("Please verify the locations of your default mail files.\n"
                    "These will be created if necessary.")));
    gtk_label_set_justify(label, GTK_JUSTIFY_RIGHT);
    gtk_label_set_line_wrap(label, TRUE);

    gtk_table_attach(table, GTK_WIDGET(label), 0, 2, 0, 1,
                     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 8, 4);

    if (0 /* FIXME: libbalsa_mailbox_exists(imap_inbox) */ )
        init_presets[INBOX] = imap_inbox;
    else {
        g_free(imap_inbox);
        init_presets[INBOX] = libbalsa_guess_mail_spool();
    }

    init_widgets[INBOX] = &(dir->inbox);
    init_widgets[OUTBOX] = &(dir->outbox);
    init_widgets[SENTBOX] = &(dir->sentbox);
    init_widgets[DRAFTBOX] = &(dir->draftbox);
    init_widgets[TRASH] = &(dir->trash);

    for (i = 0; i < NUM_EDs; i++) {
        gchar *preset;

        dir->ed[i].master = &(dir->emaster);

        init_mbnames[i] = _(init_mbnames[i]);

        if (init_presets[i])
            preset = init_presets[i];
        else
            preset = g_strdup("[Dummy value]");

        balsa_init_add_table_entry(table, i, init_mbnames[i], preset,
                                   &(dir->ed[i]), druid, init_widgets[i]);

        g_free(preset);
    }

    gtk_box_pack_start(GTK_BOX(page->vbox), GTK_WIDGET(table), TRUE, TRUE,
                       8);
    gtk_widget_show_all(GTK_WIDGET(table));

    return;
}


void
balsa_druid_page_directory(GnomeDruid * druid, GdkPixbuf * default_logo)
{
    BalsaDruidPageDirectory *dir;
    GnomeDruidPageStandard *page;

    dir = g_new0(BalsaDruidPageDirectory, 1);
    page = GNOME_DRUID_PAGE_STANDARD(gnome_druid_page_standard_new());
    gnome_druid_page_standard_set_title(page, _("Mail Files"));
    gnome_druid_page_standard_set_logo(page, default_logo);
    balsa_druid_page_directory_init(dir, page, druid);
    gnome_druid_append_page(druid, GNOME_DRUID_PAGE(page));
    g_signal_connect(G_OBJECT(page), "prepare",
                     G_CALLBACK(balsa_druid_page_directory_prepare), dir);
    g_signal_connect(G_OBJECT(page), "next",
                     G_CALLBACK(balsa_druid_page_directory_next), dir);
    g_signal_connect(G_OBJECT(page), "back",
                     G_CALLBACK(balsa_druid_page_directory_back), dir);
}

static void
balsa_druid_page_directory_prepare(GnomeDruidPage * page,
                                   GnomeDruid * druid,
                                   BalsaDruidPageDirectory * dir)
{
    gchar *buf;

    /* We want a change in the local mailroot to be reflected in the directories
     * here, but we don't want to trash user's custom settings if needed. Hence
     * the paths_locked variable; it should work pretty well, because only a movement
     * backwards should change the mailroot; going forward should not lock the paths:
     * envision an error occurring; upon return to the Dir page the entries should be
     * the same. 
     */

    if (!dir->paths_locked) {
        buf = g_strconcat(balsa_app.local_mail_directory, "/outbox", NULL);
        gtk_entry_set_text(GTK_ENTRY(dir->outbox), buf);
        g_free(buf);

        buf =
            g_strconcat(balsa_app.local_mail_directory, "/sentbox", NULL);
        gtk_entry_set_text(GTK_ENTRY(dir->sentbox), buf);
        g_free(buf);

        buf =
            g_strconcat(balsa_app.local_mail_directory, "/draftbox", NULL);
        gtk_entry_set_text(GTK_ENTRY(dir->draftbox), buf);
        g_free(buf);

        buf = g_strconcat(balsa_app.local_mail_directory, "/trash", NULL);
        gtk_entry_set_text(GTK_ENTRY(dir->trash), buf);
        g_free(buf);
    }

    /* Don't let them continue unless all entries have something. */
    if (ENTRY_MASTER_DONE(dir->emaster)) {
        gnome_druid_set_buttons_sensitive(druid, TRUE, TRUE, TRUE, FALSE);
    } else {
        gnome_druid_set_buttons_sensitive(druid, TRUE, FALSE, TRUE, FALSE);
    }

    gnome_druid_set_show_finish(druid, FALSE);
}

static gboolean
balsa_druid_page_directory_next(GnomeDruidPage * page, GtkWidget * druid,
                                BalsaDruidPageDirectory * dir)
{
    gchar *error = NULL;

    unconditional_mailbox(gtk_entry_get_text
                          (GTK_ENTRY(dir->inbox)), "Inbox",
                          &balsa_app.inbox, &error);
    unconditional_mailbox(gtk_entry_get_text
                          (GTK_ENTRY(dir->outbox)), "Outbox",
                          &balsa_app.outbox, &error);
    unconditional_mailbox(gtk_entry_get_text
                          (GTK_ENTRY(dir->sentbox)), "Sentbox",
                          &balsa_app.sentbox, &error);
    unconditional_mailbox(gtk_entry_get_text
                          (GTK_ENTRY(dir->draftbox)),
                          "Draftbox", &balsa_app.draftbox, &error);
    unconditional_mailbox(gtk_entry_get_text
                          (GTK_ENTRY(dir->trash)), "Trash",
                          &balsa_app.trash, &error);

    dir->paths_locked = TRUE;

    if (error) {
        GtkWidget *dlg =
            gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_ancestor
                                          (GTK_WIDGET(druid), 
                                           GTK_TYPE_WINDOW)),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_OK,
                                   _("Problem Creating Mailboxes\n%s"),
                                   error);
        g_free(error);
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return TRUE;
    }

    return FALSE;
}

static gboolean
balsa_druid_page_directory_back(GnomeDruidPage * page, GtkWidget * druid,
                                BalsaDruidPageDirectory * dir)
{
    dir->paths_locked = FALSE;
    return FALSE;
}
