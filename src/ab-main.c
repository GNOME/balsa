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
#ifdef GTKHTML_HAVE_GCONF
# include <gconf/gconf.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

/* #include "address-book.h" */

static void bab_cleanup(void);

static gint bab_save_session(GnomeClient * client, gint phase,
                             GnomeSaveStyle save_style, gint is_shutdown,
                             GnomeInteractStyle interact_style, gint is_fast,
                             gpointer client_data);
static gint bab_kill_session(GnomeClient * client, gpointer client_data);

static void
bab_config_init(void)
{
}

static GtkWidget*
bab_window_new()
{
    GtkWidget *wnd = gnome_app_new("Contacts", "Contacts");
    gnome_app_set_contents(GNOME_APP(wnd), gtk_label_new("LAbel"));
    return wnd;
}

static gboolean
bab_delete_ok(void)
{
    return FALSE;
}
/* -------------------------- main --------------------------------- */
int
main(int argc, char *argv[])
{
    GtkWidget *window;
    GnomeClient *client;
#ifdef GTKHTML_HAVE_GCONF
    GError *gconf_error;
#endif

#ifdef ENABLE_NLS
    /* Initialize the i18n stuff */
    bindtextdomain(PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");
    textdomain(PACKAGE);
    /* FIXME: gnome_i18n_get_language seems to have gone away; 
     * is this a reasonable replacement? */
    setlocale(LC_CTYPE,
              (const char *) gnome_i18n_get_language_list(LC_CTYPE)->data);
#endif

    /* FIXME: do we need to allow a non-GUI mode? */
    gtk_init_check(&argc, &argv);
    gnome_program_init(PACKAGE, VERSION, LIBGNOMEUI_MODULE, argc, argv,
                       GNOME_PARAM_POPT_TABLE, NULL,
                       GNOME_PARAM_APP_PREFIX,  BALSA_STD_PREFIX,
                       GNOME_PARAM_APP_DATADIR, BALSA_STD_PREFIX "/share",
                       NULL);

#ifdef GTKHTML_HAVE_GCONF
    if (!gconf_init(argc, argv, &gconf_error))
	g_error_free(gconf_error);
    gconf_error = NULL;
#endif

    /* Initialize libbalsa */
    /* libbalsa_init((LibBalsaInformationFunc) balsa_information); */

    /* load address book data */
    bab_config_init();

    window = bab_window_new();
    g_signal_connect(G_OBJECT(window), "destroy",
                     G_CALLBACK(bab_cleanup), NULL);
    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(bab_delete_ok), NULL);

    /* session management */
    client = gnome_master_client();
    g_signal_connect(G_OBJECT(client), "save_yourself",
		     G_CALLBACK(bab_save_session), argv[0]);
    g_signal_connect(G_OBJECT(client), "die",
		     G_CALLBACK(bab_kill_session), NULL);

    gtk_widget_show_all(window);

    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();
    
    return 0;
}


static void
bab_cleanup(void)
{
    gnome_sound_shutdown();
    gtk_main_quit();
}

static gint
bab_kill_session(GnomeClient * client, gpointer client_data)
{
    /* save data here */
    gtk_main_quit(); 
    return TRUE;
}


static gint
bab_save_session(GnomeClient * client, gint phase,
                 GnomeSaveStyle save_style, gint is_shutdown,
                 GnomeInteractStyle interact_style, gint is_fast,
                 gpointer client_data)
{
    gchar **argv;
    guint argc;

    /* allocate 0-filled so it will be NULL terminated */
    argv = g_malloc0(sizeof(gchar *) * 2);

    argc = 1;
    argv[0] = client_data;

    gnome_client_set_clone_command(client, argc, argv);
    gnome_client_set_restart_command(client, argc, argv);

    return TRUE;
}
