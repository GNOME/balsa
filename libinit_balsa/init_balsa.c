/*
 * Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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
#include "helper.h"
#include "balsa-initdruid.h"
#include "balsa-druid-page.h"
#include "balsa-druid-page-welcome.h"

void balsa_init_begin( void );

void balsa_init_begin( void )
{
    GtkWidget *window;
    GtkWidget *druid;

    window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW( window ), _("Configure Balsa") );

    druid = GTK_WIDGET( balsa_initdruid_new( GTK_WINDOW( window ) ) );

    gtk_widget_show_all( GTK_WIDGET( window ) );

    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();
}
