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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

#define LIBBALSA_TYPE_VCAL libbalsa_vcal_get_type()

G_DECLARE_FINAL_TYPE(LibBalsaVCal,
                     libbalsa_vcal,
                     LIBBALSA,
                     VCAL,
                     GObject)

/* a VEvent object description as GObject */

#define LIBBALSA_TYPE_VEVENT libbalsa_vevent_get_type()

G_DECLARE_FINAL_TYPE(LibBalsaVEvent,
                     libbalsa_vevent,
                     LIBBALSA,
                     VEVENT,
                     GObject)

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



LibBalsaVCal *libbalsa_vcal_new(void);
LibBalsaVCal *libbalsa_vcal_new_from_body(LibBalsaMessageBody * body);

LibBalsaVEvent *libbalsa_vevent_new(void);
gchar *libbalsa_vevent_reply(const LibBalsaVEvent * event,
			     const gchar * sender,
			     LibBalsaVCalPartStat new_stat);

gchar *libbalsa_vcal_attendee_to_str(LibBalsaAddress * person);
gboolean libbalsa_vcal_attendee_rsvp(LibBalsaAddress * person);
const gchar *libbalsa_vcal_method_to_str(LibBalsaVCalMethod method);
const gchar *libbalsa_vcal_part_stat_to_str(LibBalsaVCalPartStat pstat);

/*
 * Getters
 */
LibBalsaVCalMethod libbalsa_vcal_get_method(LibBalsaVCal *vcal);
GList *libbalsa_vcal_get_vevent(LibBalsaVCal *vcal);
const gchar * libbalsa_vevent_get_summary(LibBalsaVEvent *vevent);
LibBalsaAddress * libbalsa_vevent_get_organizer(LibBalsaVEvent *vevent);
time_t libbalsa_vevent_get_start(LibBalsaVEvent *vevent);
time_t libbalsa_vevent_get_end(LibBalsaVEvent *vevent);
const gchar * libbalsa_vevent_get_location(LibBalsaVEvent *vevent);
GList * libbalsa_vevent_get_attendee(LibBalsaVEvent *vevent);
const gchar * libbalsa_vevent_get_description(LibBalsaVEvent *vevent);

G_END_DECLS

#endif				/* __RFC2445_H__ */
