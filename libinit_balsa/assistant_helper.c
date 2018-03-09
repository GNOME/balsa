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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "assistant_helper.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include <glib/gi18n.h>
#include "libbalsa.h"
#include "url.h"

/*
 * #ifdef BALSA_LOCAL_INSTALL
 * #define gnome_pixmap_file(s) g_strconcat(BALSA_RESOURCE_PREFIX, "/pixmaps/", s, NULL)
 * #define gnome_unconditional_pixmap_file(s) g_strconcat(BALSA_RESOURCE_PREFIX, "/pixmaps", s, NULL)
 * #endif
 */

/* ************************************************************************** */

static void entry_changed_cb(GtkEntry * entry, EntryData * ed);

/* ************************************************************************** */

GtkWidget *
balsa_init_add_grid_entry(GtkGrid * grid, guint num, const gchar * ltext,
                          const gchar * etext, EntryData * ed,
                          GtkAssistant * druid, GtkWidget *page,
                          GtkWidget ** dest)
{
    GtkWidget *l, *e;

    l = gtk_label_new_with_mnemonic(ltext);
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_RIGHT);
    gtk_widget_set_halign(l, GTK_ALIGN_END);
    gtk_grid_attach(grid, l, 0, num + 1, 1, 1);

    e = gtk_entry_new();
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), e);
    gtk_widget_set_hexpand(e, TRUE);
    gtk_widget_set_vexpand(e, TRUE);
    gtk_grid_attach(grid, e, 1, num + 1, 1, 1);
    (*dest) = e;
    if(ed) {
        g_signal_connect(G_OBJECT(e), "changed",
                         G_CALLBACK(entry_changed_cb), ed);
        ed->num = ed->master->numentries++;
        ed->druid = druid;
        ed->page = page;
        if (etext && etext[0] != '\0')
            ed->master->setbits |= (1 << num);

        ed->master->donemask = (ed->master->donemask << 1) | 1;
    }
    gtk_entry_set_text(GTK_ENTRY(e), etext);
    return e;
}

static void
entry_changed_cb(GtkEntry * entry, EntryData * ed)
{
    g_assert(ed != NULL);

    if (gtk_entry_get_text_length(entry)) {
        ed->master->setbits |= (1 << ed->num);
    } else {
        ed->master->setbits &= ~(1 << ed->num);
    }

    /* The stuff below is only when we are displayed... which is not
     * always the case.
     */
    if (!gtk_widget_get_visible(GTK_WIDGET(entry)))
        return;

    if (GTK_IS_ASSISTANT(ed->druid)) {
        /* Don't let them continue unless all entries have something. */
        if (ENTRY_MASTER_P_DONE(ed->master)) {
            gtk_assistant_set_page_complete(ed->druid, ed->page, TRUE);
        } else {
            gtk_assistant_set_page_complete(ed->druid, ed->page, FALSE);
        }
    }
}


void
balsa_init_add_grid_option(GtkGrid *grid, guint num,
                            const gchar *ltext, const gchar **optns,
                            GtkAssistant *druid, GtkWidget **dest)
{
    GtkWidget *l, *om;
    int i;
    l = gtk_label_new_with_mnemonic(ltext);
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_RIGHT);
    gtk_widget_set_halign(l, GTK_ALIGN_END);
    gtk_grid_attach(grid, l, 0, num + 1, 1, 1);

    *dest = om = gtk_combo_box_text_new();
    for(i=0; optns[i]; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(om), _(optns[i]));
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), om);
    gtk_combo_box_set_active(GTK_COMBO_BOX(om), 0);
    gtk_widget_set_hexpand(om, TRUE);
    gtk_widget_set_vexpand(om, TRUE);
    gtk_grid_attach(grid, om, 1, num + 1, 1, 1);
}

gint
balsa_option_get_active(GtkWidget *option_widget)
{
    return gtk_combo_box_get_active(GTK_COMBO_BOX(option_widget));
}

void
balsa_init_add_grid_checkbox(GtkGrid *grid, guint num,
                             const gchar *ltext, gboolean defval,
                             GtkAssistant *druid, GtkWidget **dest)
{
    GtkWidget *l;

    l = gtk_label_new_with_mnemonic(ltext);
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_RIGHT);
    gtk_widget_set_halign(l, GTK_ALIGN_END);
    gtk_grid_attach(grid, l, 0, num + 1, 1, 1);

    *dest = gtk_check_button_new();
    gtk_grid_attach(grid, *dest, 1, num + 1, 1, 1);
    if(defval)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(*dest), TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), *dest);
}

gboolean
balsa_init_create_to_directory(const gchar * dir, gchar ** complaint)
{
    /* Security. Well, we could create some weird directories, but
       a) that's not very destructive and b) unless we have root
       privileges (which would be so, so, wrong) we can't do any
       damage. */
    struct stat sb;
    gchar *sofar;
    guint32 i;
    url_scheme_t scheme = url_check_scheme(dir);

    if (scheme == U_IMAP || scheme == U_POP)
        return FALSE;           /* *** For now */

    if (dir[0] != '/') {
        (*complaint) =
            g_strdup_printf(_
                            ("The path %s must be relative to the filesystem root (start with /)."),
                            dir);
        return TRUE;
    }

    for (i = 1; dir[i] != '\0'; i++) {
        if (dir[i] == '/') {
            sofar = g_strndup(dir, i);

            if (stat(sofar, &sb) < 0) {
                if (mkdir(sofar, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
                    (*complaint) =
                        g_strdup_printf(_("Couldn’t create a directory:"
                                          " mkdir() failed on pathname “%s”,"
                                          " with error “%s”."),
                                        sofar, g_strerror(errno));
                    g_free(sofar);
                    return TRUE;
                }
            }

            if (!S_ISDIR(sb.st_mode)) {
                (*complaint) =
                    g_strdup_printf(_
                                    ("The file with pathname “%s” is not a directory."),
                                    sofar);
                g_free(sofar);
                return TRUE;
            }

            g_free(sofar);
        }
    }

    if (stat(dir, &sb) < 0) {
        if (mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
            (*complaint) =
                g_strdup_printf(_
                                ("Couldn’t create a directory: mkdir() failed on pathname “%s”."),
                                dir);
            return TRUE;
        }
    }

    if (!S_ISDIR(sb.st_mode)) {
        (*complaint) =
            g_strdup_printf(_
                            ("The file with pathname “%s” is not a directory."),
                            dir);
        return TRUE;
    }

    return FALSE;
}
