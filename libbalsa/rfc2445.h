/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * VCalendar (RFC 5545 and 5546) stuff
 * Copyright (C) 2009-2020 Albrecht Dreß <albrecht.dress@arcor.de>
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

#ifndef __RFC2445_H__
#define __RFC2445_H__


#include <glib.h>
#include <glib-object.h>
#include <libical/ical.h>
#include <time.h>
#include "body.h"
#include "address.h"


G_BEGIN_DECLS

/* a VCalendar object description as GObject */
#define LIBBALSA_TYPE_VCAL			(libbalsa_vcal_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaVCal, libbalsa_vcal, LIBBALSA, VCAL, GObject)


/* a VEvent object description as GObject */
#define LIBBALSA_TYPE_VEVENT		(libbalsa_vevent_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaVEvent, libbalsa_vevent, LIBBALSA, VEVENT, GObject)


typedef enum {
	VEVENT_DATETIME_STAMP,
	VEVENT_DATETIME_START,
	VEVENT_DATETIME_END
} LibBalsaVEventTimestamp;


LibBalsaVCal *libbalsa_vcal_new_from_body(LibBalsaMessageBody * body);

gchar *libbalsa_vevent_reply(const LibBalsaVEvent   *event,
							 InternetAddress        *sender,
							 icalparameter_partstat  new_stat);

gchar *libbalsa_vcal_attendee_to_str(LibBalsaAddress *person);
gboolean libbalsa_vcal_attendee_rsvp(LibBalsaAddress *person);
icalproperty_method libbalsa_vcal_method(LibBalsaVCal *vcal);
const gchar *libbalsa_vcal_method_str(LibBalsaVCal *vcal);
const gchar *libbalsa_vcal_part_stat_to_str(icalparameter_partstat pstat);
guint libbalsa_vcal_vevent_count(LibBalsaVCal *vcal);
LibBalsaVEvent *libbalsa_vcal_vevent(LibBalsaVCal *vcal,
									 guint         nth_event);

LibBalsaAddress *libbalsa_vevent_organizer(LibBalsaVEvent *event);
const gchar *libbalsa_vevent_summary(LibBalsaVEvent *event);
const gchar *libbalsa_vevent_location(LibBalsaVEvent *event);
const gchar *libbalsa_vevent_description(LibBalsaVEvent *event);
icalproperty_status libbalsa_vevent_status(LibBalsaVEvent *event);
gchar *libbalsa_vevent_time_str(LibBalsaVEvent          *event,
								LibBalsaVEventTimestamp  which,
								const gchar             *format_str);
guint libbalsa_vevent_attendees(LibBalsaVEvent *event);
LibBalsaAddress *libbalsa_vevent_attendee(LibBalsaVEvent *event,
										  guint           nth_attendee);
guint libbalsa_vevent_category_count(LibBalsaVEvent *event);
gchar *libbalsa_vevent_category_str(LibBalsaVEvent *event);
gchar *libbalsa_vevent_duration_str(LibBalsaVEvent *event);
const gchar *libbalsa_vevent_status_str(LibBalsaVEvent *event);
gchar *libbalsa_vevent_recurrence_str(LibBalsaVEvent *event,
									  const gchar    *format_str);

G_END_DECLS

#endif				/* __RFC2445_H__ */
