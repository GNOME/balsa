/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * VCalendar (RFC 2445) stuff
 * Copyright (C) 2009 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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

#ifndef __RFC2445_H__
#define __RFC2445_H__


#include <glib.h>
#include <glib-object.h>
#include <time.h>
#include "body.h"
#include "address.h"


G_BEGIN_DECLS

/* a VCalendar object description as GObject */
#define LIBBALSA_TYPE_VCAL            (libbalsa_vcal_get_type())
#define LIBBALSA_VCAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), LIBBALSA_TYPE_VCAL, LibBalsaVCal))
#define LIBBALSA_VCAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), LIBBALSA_TYPE_VCAL, LibBalsaVCalClass))
#define LIBBALSA_IS_VCAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), LIBBALSA_TYPE_VCAL))
#define LIBBALSA_IS_VCAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), LIBBALSA_TYPE_VCAL))
#define LIBBALSA_VCAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), LIBBALSA_TYPE_VCAL, LibBalsaVCalClass))

typedef struct _LibBalsaVCal LibBalsaVCal;
typedef struct _LibBalsaVCalClass LibBalsaVCalClass;


/* a VEvent object description as GObject */
#define LIBBALSA_TYPE_VEVENT            (libbalsa_vevent_get_type())
#define LIBBALSA_VEVENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), LIBBALSA_TYPE_VEVENT, LibBalsaVEvent))
#define LIBBALSA_VEVENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), LIBBALSA_TYPE_VEVENT, LibBalsaVEventClass))
#define LIBBALSA_IS_VEVENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), LIBBALSA_TYPE_VEVENT))
#define LIBBALSA_IS_VEVENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), LIBBALSA_TYPE_VEVENT))
#define LIBBALSA_VEVENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), LIBBALSA_TYPE_VEVENT, LibBalsaVEventClass))

typedef struct _LibBalsaVEvent LibBalsaVEvent;
typedef struct _LibBalsaVEventClass LibBalsaVEventClass;


/* methods as defined by RFC 2446 */
typedef enum {
    ITIP_UNKNOWN = 0,
    ITIP_PUBLISH,
    ITIP_REQUEST,
    ITIP_REPLY,
    ITIP_CANCEL
} LibBalsaVCalMethod;

/* participation status as defined by RFC 2445
 * (note: includes constants for VTODO) */
typedef enum {
    VCAL_PSTAT_UNKNOWN = 0,
    VCAL_PSTAT_NEEDS_ACTION,
    VCAL_PSTAT_ACCEPTED,
    VCAL_PSTAT_DECLINED,
    VCAL_PSTAT_TENTATIVE,
    VCAL_PSTAT_DELEGATED,
    VCAL_PSTAT_COMPLETED,
    VCAL_PSTAT_IN_PROCESS
} LibBalsaVCalPartStat;


struct _LibBalsaVCal {
    GObject parent;

    /* method */
    LibBalsaVCalMethod method;

    /* linked list of VEVENT entries */
    GList *vevent;
};


struct _LibBalsaVEvent {
    GObject parent;

    LibBalsaAddress *organizer;
    GList *attendee;
    time_t stamp;
    time_t start;
    time_t end;
    gchar *uid;
    gchar *summary;
    gchar *location;
    gchar *description;
};


struct _LibBalsaVCalClass {
    GObjectClass parent;
};


struct _LibBalsaVEventClass {
    GObjectClass parent;
};


GType libbalsa_vcal_get_type(void);
LibBalsaVCal *libbalsa_vcal_new(void);
LibBalsaVCal *libbalsa_vcal_new_from_body(LibBalsaMessageBody * body);

GType libbalsa_vevent_get_type(void);
LibBalsaVEvent *libbalsa_vevent_new(void);
gchar *libbalsa_vevent_reply(const LibBalsaVEvent * event,
			     const gchar * sender,
			     LibBalsaVCalPartStat new_stat);

gchar *libbalsa_vcal_attendee_to_str(LibBalsaAddress * person);
gboolean libbalsa_vcal_attendee_rsvp(LibBalsaAddress * person);
const gchar *libbalsa_vcal_method_to_str(LibBalsaVCalMethod method);
const gchar *libbalsa_vcal_part_stat_to_str(LibBalsaVCalPartStat pstat);

G_END_DECLS

#endif				/* __RFC2445_H__ */
