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

#include "config.h"
#include <gnome.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "helper.h"
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

GdkPixbuf *
balsa_init_get_png(const gchar * fname)
{
    GdkPixbuf *img;
    GError *err = NULL;
    gchar *fullpath;

    g_return_val_if_fail(fname != NULL, NULL);

    fullpath = balsa_pixmap_finder(fname);

    if (!fullpath)
        return NULL;

    img = gdk_pixbuf_new_from_file(fullpath, &err);
    if (err) {
        g_print(_("Error loading %s: %s\n"), fullpath, err->message);
        g_error_free(err);
    }
    g_free(fullpath);

    return img;
}

void
balsa_init_add_table_entry(GtkTable * table, guint num, gchar * ltext,
                           const gchar * etext, EntryData * ed,
                           GnomeDruid * druid, GtkWidget ** dest)
{
    GtkWidget *l, *e;

    l = gtk_label_new_with_mnemonic(ltext);
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_RIGHT);
    gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
    gtk_table_attach(table, GTK_WIDGET(l), 0, 1, num + 1, num + 2,
                     GTK_FILL, GTK_FILL, 8, 4);

    e = gtk_entry_new();
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), e);
    gtk_table_attach(table, GTK_WIDGET(e), 1, 2, num + 1, num + 2,
                     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 8, 4);
    (*dest) = e;
    if(ed) {
        g_signal_connect(G_OBJECT(e), "changed",
                         G_CALLBACK(entry_changed_cb), ed);
        ed->num = ed->master->numentries++;
        ed->druid = druid;
        if (etext && etext[0] != '\0')
            ed->master->setbits |= (1 << num);
        
        ed->master->donemask = (ed->master->donemask << 1) | 1;
    }
    gtk_entry_set_text(GTK_ENTRY(e), etext);
}

static void
entry_changed_cb(GtkEntry * entry, EntryData * ed)
{
    g_assert(ed != NULL);

    if (entry->text_length) {
        ed->master->setbits |= (1 << ed->num);
    } else {
        ed->master->setbits &= ~(1 << ed->num);
    }

    /* The stuff below is only when we are displayed... which is not
     * always the case.
     */
    if (!GTK_WIDGET_VISIBLE(GTK_WIDGET(entry)))
        return;

    if (GNOME_IS_DRUID(ed->druid)) {
        /* Don't let them continue unless all entries have something. */
        if (ENTRY_MASTER_P_DONE(ed->master)) {
            gnome_druid_set_buttons_sensitive(ed->druid, TRUE, TRUE, TRUE,
                                              FALSE);
        } else {
            gnome_druid_set_buttons_sensitive(ed->druid, TRUE, FALSE, TRUE,
                                              FALSE);
        }
    }
}


void
balsa_init_add_table_option(GtkTable *table, guint num,
                            const gchar *ltext, const gchar **optns,
                            GnomeDruid *druid, GtkWidget **dest)
{
    GtkWidget *l, *om;
    int i;
    l = gtk_label_new_with_mnemonic(ltext);
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_RIGHT);
    gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
    gtk_table_attach(table, l, 0, 1, num + 1, num + 2,
                     GTK_FILL, GTK_FILL, 8, 4);

    *dest = om = gtk_combo_box_new_text();
    for(i=0; optns[i]; i++)
        gtk_combo_box_append_text(GTK_COMBO_BOX(om), _(optns[i]));
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), om);
    gtk_combo_box_set_active(GTK_COMBO_BOX(om), 0);
    gtk_table_attach(table, om, 1, 2, num + 1, num + 2,
                     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 8, 4);
}

gint
balsa_option_get_active(GtkWidget *option_widget)
{
    return gtk_combo_box_get_active(GTK_COMBO_BOX(option_widget));
}

void
balsa_init_add_table_checkbox(GtkTable *table, guint num,
                              const gchar *ltext, gboolean defval,
                              GnomeDruid *druid, GtkWidget **dest)
{
    GtkWidget *l;

    l = gtk_label_new_with_mnemonic(ltext);
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_RIGHT);
    gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
    gtk_table_attach(table, l, 0, 1, num + 1, num + 2,
                     GTK_FILL, GTK_FILL, 8, 4);

    *dest = gtk_check_button_new();
    gtk_table_attach(table, *dest, 1, 2, num + 1, num + 2,
                     GTK_FILL, GTK_FILL, 8, 4);
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
                        g_strdup_printf(_
                                        ("Couldn't create a directory: mkdir() failed on pathname \"%s\"."),
                                        sofar);
                    g_free(sofar);
                    return TRUE;
                }
            }

            if (!S_ISDIR(sb.st_mode)) {
                (*complaint) =
                    g_strdup_printf(_
                                    ("The file with pathname \"%s\" is not a directory."),
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
                                ("Couldn't create a directory: mkdir() failed on pathname \"%s\"."),
                                dir);
            return TRUE;
        }
    }

    if (!S_ISDIR(sb.st_mode)) {
        (*complaint) =
            g_strdup_printf(_
                            ("The file with pathname \"%s\" is not a directory."),
                            dir);
        return TRUE;
    }

    return FALSE;
}
