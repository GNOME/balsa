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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "libbalsa.h"
#include "rfc2445.h"


struct _LibBalsaVCal {
    GObject parent;

    /* method */
    icalproperty_method method;

    /* VCALENDAR object */
    icalcomponent *vcalendar;

    /* linked list of VEVENT entries */
    GList *vevent;
};

G_DEFINE_TYPE(LibBalsaVCal, libbalsa_vcal, G_TYPE_OBJECT)


struct _LibBalsaVEvent {
    GObject parent;

    LibBalsaAddress *organizer;
    GList *attendee;
    icaltimetype stamp;
    icaltimetype start;
    icaltimetype end;
    icaltimetype recurrence_id;
    struct icaldurationtype duration;
    struct icalrecurrencetype rrule;
    icalproperty_status status;
    gchar *uid;
    int sequence;
    gchar *summary;
    gchar *location;
    gchar *description;
    GList *categories;
};

G_DEFINE_TYPE(LibBalsaVEvent, libbalsa_vevent, G_TYPE_OBJECT)


/* LibBalsaAddress extra object data */
typedef struct {
	icalparameter_role role;
    icalparameter_partstat part_stat;
    icalparameter_rsvp rsvp;
} LibBalsaVCalInfo;
#define RFC2445_INFO            "RFC2445:Info"


static void libbalsa_vcal_finalize(GObject *self);
static void libbalsa_vevent_finalize(GObject *self);

/* conversion helpers */
static LibBalsaVCal *vcalendar_extract(const gchar *vcal_buf);
static LibBalsaAddress *cal_address_5545_to_lbaddress(icalproperty *prop,
													  gboolean      is_organizer);
static const gchar *vcal_role_to_str(icalparameter_role role);
static gchar *icaltime_str(icaltimetype  ical_time,
						   const gchar  *format_str);


/* --- VCal GObject stuff --- */

static void
libbalsa_vcal_class_init(LibBalsaVCalClass *klass)
{
    GObjectClass *gobject_klass = G_OBJECT_CLASS(klass);

    gobject_klass->finalize = libbalsa_vcal_finalize;
}


static void
libbalsa_vcal_init(LibBalsaVCal *self)
{
    self->method = ICAL_METHOD_NONE;
}


static void
libbalsa_vcal_finalize(GObject *self)
{
	LibBalsaVCal *vcal = LIBBALSA_VCAL(self);
	const GObjectClass *parent_class = G_OBJECT_CLASS(libbalsa_vcal_parent_class);

    if (vcal->vevent != NULL) {
    	g_list_free_full(vcal->vevent, g_object_unref);
    }
    if (vcal->vcalendar != NULL) {
    	icalcomponent_free(vcal->vcalendar);
    }

    (*parent_class->finalize)(self);
}


/* --- VEvent GObject stuff --- */
static void
libbalsa_vevent_class_init(LibBalsaVEventClass * klass)
{
    GObjectClass *gobject_klass = G_OBJECT_CLASS(klass);

    gobject_klass->finalize = libbalsa_vevent_finalize;
}


static void
libbalsa_vevent_init(LibBalsaVEvent *self)
{
    self->start = icaltime_null_time();
    self->end = icaltime_null_time();
    self->stamp = icaltime_null_time();
    self->duration = icaldurationtype_null_duration();
    self->rrule.freq = ICAL_NO_RECURRENCE;
    self->status = ICAL_STATUS_NONE;
}


static void
libbalsa_vevent_finalize(GObject *self)
{
	LibBalsaVEvent *vevent = LIBBALSA_VEVENT(self);
	const GObjectClass *parent_class = G_OBJECT_CLASS(libbalsa_vevent_parent_class);

	if (vevent->organizer) {
		g_object_unref(vevent->organizer);
	}
	if (vevent->attendee) {
		g_list_free_full(vevent->attendee, g_object_unref);
	}
	g_free(vevent->uid);
	g_free(vevent->summary);
	g_free(vevent->location);
	g_free(vevent->description);
	g_list_free_full(vevent->categories, g_free);

	(*parent_class->finalize)(self);
}


/* parse a text/calendar part and convert it into a LibBalsaVCal object */
LibBalsaVCal *
libbalsa_vcal_new_from_body(LibBalsaMessageBody * body)
{
    LibBalsaVCal *retval = NULL;
    gchar *vcal_buf;

    g_return_val_if_fail(body != NULL, NULL);

    /* get the body buffer */
    if (libbalsa_message_body_get_content(body, &vcal_buf, NULL) > 0) {
    	gchar *charset;

    	/* check if the body has a charset (default is utf-8, see '2445, sect 4.1.4) */
    	charset = libbalsa_message_body_get_parameter(body, "charset");
    	if ((charset != NULL) && (g_ascii_strcasecmp(charset, "utf-8") != 0)) {
    		gchar *conv_buf;

    		conv_buf = g_convert(vcal_buf, -1, "utf-8", charset, NULL, NULL, NULL);
    		g_free(vcal_buf);
    		vcal_buf = conv_buf;
    	}
    	g_free(charset);

    	if (vcal_buf != NULL) {
    		/* o.k., create a new object */
    		retval = vcalendar_extract(vcal_buf);
    		g_free(vcal_buf);
    	}
    }

    return retval;
}


/* return a rfc 2445 attendee (i.e. a LibBalsaAddress w/ extra information)
 * as a human-readable string */
gchar *
libbalsa_vcal_attendee_to_str(LibBalsaAddress *person)
{
    GString *retval;
    gchar *str;
    LibBalsaVCalInfo *info;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(person), NULL);

    retval = g_string_new("");

    info = g_object_get_data(G_OBJECT(person), RFC2445_INFO);
    if (info->role != ICAL_ROLE_NONE) {
    	g_string_printf(retval, "%s ", vcal_role_to_str(info->role));
    }

    str = libbalsa_address_to_gchar(person, -1);
    retval = g_string_append(retval, str);
    g_free(str);

    if (info->part_stat != ICAL_PARTSTAT_NONE) {
    	g_string_append_printf(retval, " (%s)", libbalsa_vcal_part_stat_to_str(info->part_stat));
    }

    return g_string_free(retval, FALSE);
}


/* check if a rfc 2445 attendee (i.e. a LibBalsaAddress w/ extra information)
 * has the RSVP flag ("please reply") set */
gboolean
libbalsa_vcal_attendee_rsvp(LibBalsaAddress * person)
{
    LibBalsaVCalInfo *info;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(person), FALSE);
    info = g_object_get_data(G_OBJECT(person), RFC2445_INFO);
    return info->rsvp == ICAL_RSVP_TRUE;
}


LibBalsaAddress *
libbalsa_vevent_organizer(LibBalsaVEvent *event)
{
	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);
	return event->organizer;
}


const gchar *
libbalsa_vevent_summary(LibBalsaVEvent *event)
{
	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);
	return event->summary;
}


const gchar *
libbalsa_vevent_location(LibBalsaVEvent *event)
{
	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);
	return event->location;
}


const gchar *
libbalsa_vevent_description(LibBalsaVEvent *event)
{
	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);
	return event->description;
}


gchar *
libbalsa_vevent_time_str(LibBalsaVEvent *event, LibBalsaVEventTimestamp which, const gchar *format_str)
{
	gchar *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);

	switch (which) {
	case VEVENT_DATETIME_STAMP:
		result = icaltime_str(event->stamp, format_str);
		break;
	case VEVENT_DATETIME_START:
		result = icaltime_str(event->start, format_str);
		break;
	case VEVENT_DATETIME_END:
		result = icaltime_str(event->end, format_str);
		break;
	default:
		g_assert_not_reached();
	}

	return result;
}


guint
libbalsa_vevent_attendees(LibBalsaVEvent *event)
{
	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), 0U);
	return g_list_length(event->attendee);
}


LibBalsaAddress *
libbalsa_vevent_attendee(LibBalsaVEvent *event, guint nth_attendee)
{
	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event) && (nth_attendee < g_list_length(event->attendee)), NULL);
	return (LibBalsaAddress *) g_list_nth_data(event->attendee, nth_attendee);
}


guint
libbalsa_vevent_category_count(LibBalsaVEvent *event)
{
	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), 0U);
	return g_list_length(event->categories);
}


gchar *
libbalsa_vevent_category_str(LibBalsaVEvent *event)
{
	gchar *res = NULL;

	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);
	if (event->categories != NULL) {
		GString *buffer;
		GList *p;

		buffer = g_string_new((const gchar *) event->categories->data);
		for (p = event->categories->next; p != NULL; p = p->next) {
			g_string_append_c(buffer, '\n');
			g_string_append(buffer, (const gchar *) p->data);
		}
		res = g_string_free(buffer, FALSE);
	}
	return res;
}


gchar *
libbalsa_vevent_duration_str(LibBalsaVEvent *event)
{
	gchar *res = NULL;

	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);
	if (icaldurationtype_is_null_duration(event->duration) == 0) {
		GString *buffer = g_string_new(NULL);

		if (event->duration.weeks > 0) {
			g_string_append_printf(buffer, "%d %s", event->duration.weeks,
				ngettext("week", "weeks", event->duration.weeks));
		}
		if (event->duration.days > 0) {
			if (buffer->len > 0U) {
				g_string_append(buffer, ", ");
			}
			g_string_append_printf(buffer, "%d %s", event->duration.days,
				ngettext("day", "days", event->duration.days));
		}
		if (event->duration.hours > 0) {
			if (buffer->len > 0U) {
				g_string_append(buffer, ", ");
			}
			g_string_append_printf(buffer, "%d %s", event->duration.hours,
				ngettext("hour", "hours", event->duration.hours));
		}
		if (event->duration.minutes > 0) {
			if (buffer->len > 0U) {
				g_string_append_c(buffer, ' ');
			}
			g_string_append_printf(buffer, "%d %s", event->duration.minutes,
				ngettext("minute", "minutes", event->duration.minutes));
		}
		if (event->duration.seconds > 0) {
			if (buffer->len > 0U) {
				g_string_append_c(buffer, ' ');
			}
			g_string_append_printf(buffer, "%d %s", event->duration.seconds,
				ngettext("second", "seconds", event->duration.seconds));
		}
		res = g_string_free(buffer, FALSE);
	}
	return res;
}


icalproperty_status
libbalsa_vevent_status(LibBalsaVEvent *event)
{
	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), ICAL_STATUS_NONE);
	return event->status;
}


const gchar *
libbalsa_vevent_status_str(LibBalsaVEvent *event)
{
	static struct {
		icalproperty_status status;
		gchar *status_str;
	} status[] = {
		{ ICAL_STATUS_NONE, NC_("ical_status", "unknown") },
		{ ICAL_STATUS_TENTATIVE, NC_("ical_status", "event is tentative") },
		{ ICAL_STATUS_CONFIRMED, NC_("ical_status", "event is definite") },
		{ ICAL_STATUS_CANCELLED, NC_("ical_status", "event was cancelled") }
	};
	guint n;

	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);
    for (n = 0U; (n < G_N_ELEMENTS(status)) && (status[n].status != event->status); n++) {
    	/* nothing to do */
    }
    return (n < G_N_ELEMENTS(status)) ? status[n].status_str : status[0].status_str;
}

icalproperty_method
libbalsa_vcal_method(LibBalsaVCal *vcal)
{
    g_return_val_if_fail(LIBBALSA_IS_VCAL(vcal), ICAL_METHOD_NONE);
    return vcal->method;
}


const gchar *
libbalsa_vcal_method_str(LibBalsaVCal *vcal)
{
    static struct {
    	icalproperty_method method;
		gchar *meth_str;
    } methods[] = {
		{ ICAL_METHOD_NONE, NC_("ical_method", "unknown") },
		{ ICAL_METHOD_PUBLISH, NC_("ical_method", "Event Notification") },
		{ ICAL_METHOD_REQUEST, NC_("ical_method", "Event Request") },
		{ ICAL_METHOD_REPLY, NC_("ical_method", "Reply to Event Request") },
		{ ICAL_METHOD_CANCEL, NC_("ical_method", "Event Cancellation") }
	};
    guint n;

    for (n = 0U; (n < G_N_ELEMENTS(methods)) && (methods[n].method != vcal->method); n++) {
    	/* nothing to do */
    }
    return (n < G_N_ELEMENTS(methods)) ? methods[n].meth_str : methods[0].meth_str;
}


/* return a rfc 2445 participant status as human-readable string */
const gchar *
libbalsa_vcal_part_stat_to_str(icalparameter_partstat pstat)
{
    static struct {
    	icalparameter_partstat partstat;
    	gchar *partstat_str;
    } partstats[] = {
		{ ICAL_PARTSTAT_NONE, NC_("ical_partstat", "unknown") },
		{ ICAL_PARTSTAT_NEEDSACTION, NC_("ical_partstat", "needs action") },
		{ ICAL_PARTSTAT_ACCEPTED, NC_("ical_partstat", "accepted") },
		{ ICAL_PARTSTAT_DECLINED, NC_("ical_partstat", "declined") },
		{ ICAL_PARTSTAT_TENTATIVE, NC_("ical_partstat", "tentatively accepted") },
		{ ICAL_PARTSTAT_DELEGATED, NC_("ical_partstat", "delegated") }
    };
    guint n;

    for (n = 0U; (n < G_N_ELEMENTS(partstats)) && (partstats[n].partstat != pstat); n++) {
    	/* nothing to do */
    }
    return (n < G_N_ELEMENTS(partstats)) ? partstats[n].partstat_str : partstats[0].partstat_str;
}


guint
libbalsa_vcal_vevent_count(LibBalsaVCal *vcal)
{
	g_return_val_if_fail(LIBBALSA_IS_VCAL(vcal), 0U);
	return g_list_length(vcal->vevent);
}


LibBalsaVEvent *
libbalsa_vcal_vevent(LibBalsaVCal *vcal, guint nth_event)
{
	g_return_val_if_fail(LIBBALSA_IS_VCAL(vcal) && (nth_event < g_list_length(vcal->vevent)), NULL);
	return (LibBalsaVEvent *) g_list_nth_data(vcal->vevent, nth_event);
}


/* -- recurrence strings --
 * According to RFC 5545, Sect. 3.3.1, these rules can be extremely complex.  The following functions basically implement the same
 * features als Thunderbird's "Lightning" extension, which should be fine for the majority of cases in the wild.  However, there
 * /may/ (and will) be cases where these function just fail... */

static inline gboolean
ical_ar_empty(const short *array)
{
	return array[0] == ICAL_RECURRENCE_ARRAY_MAX;
}


static inline const gchar *
day_name(gint day)
{
	static const gchar * day_names[7] = {
		NC_("day_of_week", "Sunday"),
		NC_("day_of_week", "Monday"),
		NC_("day_of_week", "Tuesday"),
		NC_("day_of_week", "Wednesday"),
		NC_("day_of_week", "Thursday"),
		NC_("day_of_week", "Friday"),
		NC_("day_of_week", "Saturday")
	};

	return ((day >= 0) && (day < 7)) ? day_names[day] : "???";
}


/** \brief Check for "BYDAY" items in any order
 *
 * \param rrule recurrence rule
 * \param want_days bit mask of expected items in "BYDAY" (bit 1 = Sunday, bit 7 = Saturday)
 * \return TRUE if the "BYDAY" item contains exactly all requested week days in any order
 */
static gboolean
ical_check_bydays(const struct icalrecurrencetype *rrule, guint want_days)
{
	guint have_days = 0U;
	guint n;

	for (n = 0U; (n < ICAL_BY_DAY_SIZE) && (rrule->by_day[n] != ICAL_RECURRENCE_ARRAY_MAX); n++) {
		have_days |= (1U << rrule->by_day[n]);
	}
	return want_days == have_days;
}


static GString *
vevent_recurrence_daily(const struct icalrecurrencetype *rrule)
{
	GString *result = g_string_new(NULL);

	if (ical_check_bydays(rrule, 0x7cU)) {
		g_string_append(result, _("every weekday"));
	} else if (rrule->interval == 1) {
		g_string_append(result, _("every day"));
	} else {
		/* #1: interval */
		g_string_append_printf(result, _("every %d days"), rrule->interval);
	}
	return result;
}


static GString *
vevent_recurrence_weekly(const struct icalrecurrencetype *rrule)
{
	GString *result = g_string_new(NULL);

	if (!ical_ar_empty(rrule->by_day)) {
		gint n;

		if (rrule->interval == 1) {
			g_string_append(result, _("every "));
		} else {
			/* #1: interval */
			g_string_append_printf(result, _("every %d weeks on "), rrule->interval);
		}
		g_string_append(result, day_name(rrule->by_day[0] - 1));
		for (n = 1; (n < ICAL_BY_DAY_SIZE) && (rrule->by_day[n] != ICAL_RECURRENCE_ARRAY_MAX); n++) {
			if ((n < (ICAL_BY_DAY_SIZE - 1)) && (rrule->by_day[n + 1] != ICAL_RECURRENCE_ARRAY_MAX)) {
				g_string_append_printf(result, ", %s", day_name(rrule->by_day[n] - 1));
			} else {
				/* #1: the day of week (defined in the day_of_week context) */
				g_string_append_printf(result, _(" and %s"), day_name(rrule->by_day[n] - 1));
			}
		}
	} else {
		if (rrule->interval == 1) {
			g_string_append(result, _("every week"));
		} else {
			/* #1: interval */
			g_string_append_printf(result, _("every %d weeks"), rrule->interval);
		}
	}
	return result;
}


static void
day_ordinal_append(GString *buffer, int day, const gchar *last_append, const gchar *ordinal_append)
{
	if (day == -1) {
		g_string_append(buffer, _("the last"));
		if (last_append != NULL) {
			g_string_append_printf(buffer, " %s", last_append);
		}
	} else {
		switch (day % 10) {
		case 1:
			/* #1: the day of month */
			g_string_append_printf(buffer, _("%dst"), day);
			break;
		case 2:
			/* #1: the day of month */
			g_string_append_printf(buffer, _("%dnd"), day);
			break;
		case 3:
			/* #1: the day of month */
			g_string_append_printf(buffer, _("%drd"), day);
			break;
		default:
			/* #1: the day of month */
			g_string_append_printf(buffer, _("%dth"), day);
		}
		if (ordinal_append != NULL) {
			g_string_append_printf(buffer, " %s", ordinal_append);
		}
	}
}


static GString *
vevent_recurrence_monthly(const struct icalrecurrencetype *rrule, const icaltimetype *start)
{
	GString *result = g_string_new(NULL);

	if (!ical_ar_empty(rrule->by_day)) {
		/* we have a "BYDAY" rule */
		if (ical_check_bydays(rrule, 0xfeU)) {
			if (rrule->interval == 1) {
				g_string_append(result, _("every day of every month"));
			} else {
				/* #1: interval */
				g_string_append_printf(result, _("every day of the month every %d months"), rrule->interval);
			}
			return result;		/* eject here so we don't append the month interval again... */
		} else {
			guint n;
			guint every_mask = 0U;
			GList *days = NULL;
			GList *p;

			/* collect all days repeating every week */
			for (n = 0U; (n < ICAL_BY_DAY_SIZE) && (rrule->by_day[n] != ICAL_RECURRENCE_ARRAY_MAX); n++) {
				int day_pos;

				day_pos = icalrecurrencetype_day_position(rrule->by_day[n]);
				if ((day_pos < -1) || (day_pos > 5)) {
					return g_string_assign(result, _("rule too complex"));
				} else if (day_pos == 0) {
					int day_of_week;

					day_of_week = icalrecurrencetype_day_day_of_week(rrule->by_day[n]);
					every_mask |= 1U << day_of_week;
					/* #1: the day of week (defined in the day_of_week context) */
					days = g_list_append(days, g_strdup_printf(_("every %s"), day_name(day_of_week - 1)));
				} else {
					/* handled below... */
				}
			}

			/* collect specific ones, but avoid something like "Monday and the last Monday" */
			for (n = 0U; (n < ICAL_BY_DAY_SIZE) && (rrule->by_day[n] != ICAL_RECURRENCE_ARRAY_MAX); n++) {
				int day_of_week;

				day_of_week = icalrecurrencetype_day_day_of_week(rrule->by_day[n]);
				if ((every_mask & (1U << day_of_week)) == 0U) {
					int day_pos;
					GString *buffer = g_string_new(NULL);

					day_pos = icalrecurrencetype_day_position(rrule->by_day[n]);
					day_ordinal_append(buffer, day_pos, day_name(day_of_week - 1), day_name(day_of_week - 1));
					days = g_list_append(days, g_string_free(buffer, FALSE));
				}
			}

			/* glue the string together */
			g_string_append(result, (const gchar *) days->data);
			for (p = days->next; p != NULL; p = p->next) {
				if (p->next != NULL) {
					g_string_append_printf(result, ", %s", (const gchar *) p->data);
				} else {
					/* #1: recurrence expression */
					g_string_append_printf(result, _(" and %s"), (const gchar *) p->data);
				}
			}
			g_list_free_full(days, g_free);
		}
	} else if (!ical_ar_empty(rrule->by_month_day)) {
		/* we have a "BYMONTHDAY" rule */
		guint n;

		for (n = 0; (n < ICAL_BY_MONTHDAY_SIZE) && (rrule->by_month_day[n] != ICAL_RECURRENCE_ARRAY_MAX); n++) {
			if (rrule->by_month_day[n] < -1) {
				return g_string_assign(result, _("rule too complex"));
			} else {
				if (n > 0) {
					if (rrule->by_month_day[n + 1] != ICAL_RECURRENCE_ARRAY_MAX) {
						g_string_append(result, ", ");
					} else {
						g_string_append(result, _(" and "));
					}
				}
				day_ordinal_append(result, rrule->by_month_day[n], _("day"), NULL);
			}
		}
	} else {
		day_ordinal_append(result, start->day, _("day"), NULL);
	}

	if (rrule->interval == 1) {
		g_string_append(result, _(" of every month"));
	} else {
		/* #1: interval */
		g_string_append_printf(result, _(" of every %d months"), rrule->interval);
	}

	return result;
}


static GString *
vevent_recurrence_yearly(const struct icalrecurrencetype *rrule, const icaltimetype *start)
{
	static const gchar *mon_name[12] = {
		NC_("name_of_month", "January"),
		NC_("name_of_month", "February"),
		NC_("name_of_month", "March"),
		NC_("name_of_month", "April"),
		NC_("name_of_month", "May"),
		NC_("name_of_month", "June"),
		NC_("name_of_month", "July"),
		NC_("name_of_month", "August"),
		NC_("name_of_month", "September"),
		NC_("name_of_month", "October"),
		NC_("name_of_month", "November"),
		NC_("name_of_month", "December")
	};
	GString *result = g_string_new(NULL);

	/* rules which are too complex for Lightning, so ignore them here, too... */
	if ((rrule->by_month[1] != ICAL_RECURRENCE_ARRAY_MAX) || (rrule->by_month_day[1] != ICAL_RECURRENCE_ARRAY_MAX) ||
		(rrule->by_month_day[0] < -1)) {
		return g_string_assign(result, _("rule too complex"));
	}

	if (ical_ar_empty(rrule->by_day)) {
        /* RRULE:FREQ=YEARLY;BYMONTH=x;BYMONTHDAY=y.
         * RRULE:FREQ=YEARLY;BYMONTHDAY=x (takes the month from the start date)
         * RRULE:FREQ=YEARLY;BYMONTH=x (takes the day from the start date)
         * RRULE:FREQ=YEARLY (takes month and day from the start date) */
		const gchar *month;
		GString *day = g_string_new(NULL);

		if (ical_ar_empty(rrule->by_month)) {
			month = mon_name[start->month - 1];
		} else {
			month = mon_name[rrule->by_month[0] - 1];
		}
		if (ical_ar_empty(rrule->by_month_day)) {
			day_ordinal_append(day, start->day, NULL, NULL);
		} else {
			day_ordinal_append(day, rrule->by_month_day[0], _("day"), NULL);
		}
		if (rrule->interval == 1) {
			/* #1: name of month (defined in the name_of_month context) */
			/* #2: day of week (defined in the day_of_week context) */
			g_string_append_printf(result, _("every %s %s"), month, day->str);
		} else {
			/* #1: interval */
			/* #2: name of month (defined in the name_of_month context) */
			/* #3: day of week (defined in the day_of_week context) */
			g_string_append_printf(result, _("every %d years on %s %s"), rrule->interval, month, day->str);
		}
		g_string_free(day, TRUE);
	} else if (!ical_ar_empty(rrule->by_month) && !ical_ar_empty(rrule->by_day)) {
		/* RRULE:FREQ=YEARLY;BYMONTH=x;BYDAY=y1,y2,... */
		const gchar *month = mon_name[rrule->by_month[0] - 1];

		if (ical_check_bydays(rrule, 0x7cU)) {
			/* every day of the month */
			if (rrule->interval == 1) {
				/* #1: name of month (defined in the name_of_month context) */
				g_string_append_printf(result, _("every day of %s"), month);
			} else {
				/* #1: interval */
				/* #2: name of month (defined in the name_of_month context) */
				g_string_append_printf(result, _("every %d years every day of %s"), rrule->interval, month);
			}
		} else if (rrule->by_day[1] == ICAL_RECURRENCE_ARRAY_MAX) {
			int day_pos;
			int day_of_week;

			day_pos = icalrecurrencetype_day_position(rrule->by_day[0]);
			day_of_week = icalrecurrencetype_day_day_of_week(rrule->by_day[0]);
			if (day_pos == 0) {
				if (rrule->interval == 1) {
					/* #1: day of week (defined in the day_of_week context) */
					/* #2: name of month (defined in the name_of_month context) */
					g_string_append_printf(result, _("every %s of %s"), day_name(day_of_week - 1), month);
				} else {
					/* #1: interval */
					/* #2: day of week (defined in the day_of_week context) */
					/* #3: name of month (defined in the name_of_month context) */
					g_string_append_printf(result, _("every %d years on every %s of %s"), rrule->interval,
						day_name(day_of_week - 1), month);
				}
			} else if ((day_pos >= -1) && (day_pos <= 5)) {
				GString *day = g_string_new(NULL);

				day_ordinal_append(day, day_pos, day_name(day_of_week - 1), day_name(day_of_week - 1));
				if (rrule->interval == 1) {
					/* #1: day of week (defined in the day_of_week context) */
					/* #2: name of month (defined in the name_of_month context) */
					g_string_append_printf(result, _("the %s of every %s"), day->str, month);
				} else {
					/* #1: interval */
					/* #2: day of week (defined in the day_of_week context) */
					/* #3: name of month (defined in the name_of_month context) */
					g_string_append_printf(result, _("every %d years on the %s of %s"), rrule->interval, day->str, month);
				}
				g_string_free(day, TRUE);
			} else {
				g_string_assign(result, _("rule too complex"));
			}
		} else {
			/* currently we don't support yearly rules with more than one BYDAY element or exactly 7 elements with all the weekdays
			 * (the "every day" case) */
			g_string_assign(result, _("rule too complex"));
		}
	} else {
		return g_string_assign(result, _("rule too complex"));
	}

	return result;
}


gchar *
libbalsa_vevent_recurrence_str(LibBalsaVEvent *event, const gchar *format_str)
{
	gchar *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event), NULL);

	if ((event->rrule.freq >= ICAL_SECONDLY_RECURRENCE) && (event->rrule.freq <= ICAL_YEARLY_RECURRENCE)) {
		GString *buffer;

		switch (event->rrule.freq) {
		case ICAL_SECONDLY_RECURRENCE:
			buffer = g_string_new(C_("ical_recurrence", "secondly"));
			break;
		case ICAL_MINUTELY_RECURRENCE:
			buffer = g_string_new(C_("ical_recurrence", "minutely"));
			break;
		case ICAL_HOURLY_RECURRENCE:
			buffer = g_string_new(C_("ical_recurrence", "hourly"));
			break;
		case ICAL_DAILY_RECURRENCE:
	    	buffer = vevent_recurrence_daily(&event->rrule);
			break;
		case ICAL_WEEKLY_RECURRENCE:
	    	buffer = vevent_recurrence_weekly(&event->rrule);
			break;
		case ICAL_MONTHLY_RECURRENCE:
	    	buffer = vevent_recurrence_monthly(&event->rrule, &event->start);
			break;
		case ICAL_YEARLY_RECURRENCE:
			buffer = vevent_recurrence_yearly(&event->rrule, &event->start);
			break;
		default:
			g_assert_not_reached();
		}

		/* end timestamp or count */
		if (event->rrule.count > 0) {
			g_string_append(buffer, ngettext(", %d occurrence", ", %d occurrences", event->rrule.count));
		} else if (icaltime_is_null_time(event->rrule.until) == 0) {
			gchar *timestr;

			timestr = icaltime_str(event->rrule.until, format_str);
			/* #1: time string */
			g_string_append_printf(buffer, _(" until %s"), timestr);
			g_free(timestr);
		} else {
			/* nothing to do */
		}

		result = g_string_free(buffer, FALSE);
	}
	return result;
}


/* return a new buffer containing a proper reply to an event for a new
 * participant status
 *
 * According to RFC 5546, Sect. 3.2.3., the REPLY shall include the following components and properties:
 * METHOD: MUST be REPLY
 * VEVENT: ATTENDEE, DTSTAMP, ORGANIZER, RECURRENCE-ID (if present in the request), UID, SEQUENCE (if present in the request)
 *
 * However, M$ Outlook/Exchange is broken and does not process these standard-compliant parts correctly.  The trivial fix is to add
 * several optional fields (I did not figure out which are needed to work around the issue, so we just add them all...).
 */
gchar *
libbalsa_vevent_reply(const LibBalsaVEvent   *event,
					  InternetAddress        *sender,
					  icalparameter_partstat  new_stat)
{
	icalcomponent *reply;
	icalcomponent *ev_data;
	icalproperty *prop;
	gchar *buf;
	gchar *reply_str;

    /* check for allowed new state and sender */
    g_return_val_if_fail((new_stat == ICAL_PARTSTAT_ACCEPTED ||
			  	  	  	  new_stat == ICAL_PARTSTAT_DECLINED ||
						  new_stat == ICAL_PARTSTAT_TENTATIVE) &&
			 sender != NULL, NULL);

    reply = icalcomponent_new(ICAL_VCALENDAR_COMPONENT);

    icalcomponent_add_property(reply, icalproperty_new_version("2.0"));
    icalcomponent_add_property(reply, icalproperty_new_prodid("-//GNOME//Balsa " BALSA_VERSION "//EN"));
    icalcomponent_add_property(reply, icalproperty_new_method(ICAL_METHOD_REPLY));

    /* create the VEVENT */
    ev_data = icalcomponent_new(ICAL_VEVENT_COMPONENT);
    icalcomponent_add_component(reply, ev_data);

    /* add ATTENDEE */
    buf = g_strconcat("mailto:", internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(sender)), NULL);
    prop = icalproperty_new_attendee(buf);
    g_free(buf);
    icalproperty_add_parameter(prop, icalparameter_new_cn(internet_address_get_name(sender)));
    icalproperty_add_parameter(prop, icalparameter_new_partstat(new_stat));
    icalcomponent_add_property(ev_data, prop);

    /* add DTSTAMP */
    icalcomponent_add_property(ev_data, icalproperty_new_dtstamp(icaltime_current_time_with_zone(NULL)));

    /* add ORGANIZER - /should/ be present */
    if ((event->organizer != NULL) && (libbalsa_address_get_addr(event->organizer) != NULL)) {
    	prop = icalproperty_new_organizer(libbalsa_address_get_addr(event->organizer));
    	if (libbalsa_address_get_full_name(event->organizer) != NULL) {
    		icalproperty_add_parameter(prop, icalparameter_new_cn(libbalsa_address_get_full_name(event->organizer)));
        }
    	icalcomponent_add_property(ev_data, prop);
    }

    /* add RECURRENCE-ID (if present) */
    if (icaltime_is_null_time(event->recurrence_id) == 0) {
    	icalcomponent_add_property(ev_data, icalproperty_new_recurrenceid(event->recurrence_id));
    }

    /* add UID - /should/ be present */
    if (event->uid != NULL) {
    	icalcomponent_add_property(ev_data, icalproperty_new_uid(event->uid));
    }

    /* add SEQUENCE (if present) */
    if (event->sequence > 0) {
    	icalcomponent_add_property(ev_data, icalproperty_new_sequence(event->sequence));
    }

    /* the following fields are *optional* in a reply according to RFC 5546, Sect. 3.2.3, but are apparently required by broken
     * software like Exchange/Outlook */
    if (icaltime_is_null_time(event->start) == 0) {
    	icalcomponent_add_property(ev_data, icalproperty_new_dtstart(event->start));
    }
    if (icaltime_is_null_time(event->end) == 0) {
    	icalcomponent_add_property(ev_data, icalproperty_new_dtend(event->end));
    }
    if (event->summary != NULL) {
    	icalcomponent_add_property(ev_data, icalproperty_new_summary(event->summary));
    }
    if (event->description != NULL) {
    	icalcomponent_add_property(ev_data, icalproperty_new_description(event->description));
    }
    if (event->status != ICAL_STATUS_NONE) {
    	icalcomponent_add_property(ev_data, icalproperty_new_status(event->status));
    }
    if (event->location != NULL) {
    	icalcomponent_add_property(ev_data, icalproperty_new_location(event->location));
    }

    reply_str = icalcomponent_as_ical_string_r(reply);
    icalcomponent_free(reply);
    return reply_str;
}


/* -- rfc 2445 parser helper functions -- */

static LibBalsaVCal *
vcalendar_extract(const gchar *vcal_buf)
{
    LibBalsaVCal *retval = NULL;
	icalcomponent *component;

	component = icalparser_parse_string(vcal_buf);
	if (component == NULL) {
		return NULL;
	}

	/* verify - icalrestriction_check() seems to produce False Positives for some Thunderbird events, so do *not* use it here */
	if (icalcomponent_isa(component) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *item;

		retval = LIBBALSA_VCAL(g_object_new(LIBBALSA_TYPE_VCAL, NULL));
		retval->method = icalcomponent_get_method(component);
		retval->vcalendar = component;

		/* loop over all VEVENT items (if any) */
		for (item = icalcomponent_get_first_component(component, ICAL_VEVENT_COMPONENT);
			 item != NULL;
			 item = icalcomponent_get_next_component(component, ICAL_VEVENT_COMPONENT)) {
			LibBalsaVEvent *event;
			icalproperty *prop;

			event = LIBBALSA_VEVENT(g_object_new(LIBBALSA_TYPE_VEVENT, NULL));
			retval->vevent = g_list_append(retval->vevent, event);

			/* extract properties - DTSTAMP, DTSTART, DTEND
			 * note: there is no need to get the DURATION, as libical calculates the proper DTEND if necessary. However, in
			 * particular for all-day events, the duration is sometimes easier to understand than the (exclusive) end. */
			event->stamp = icalcomponent_get_dtstamp(item);
			event->start = icalcomponent_get_dtstart(item);
			event->end = icalcomponent_get_dtend(item);
			event->duration = icalcomponent_get_duration(item);
			if (icaldurationtype_is_bad_duration(event->duration)) {
				event->duration = icaldurationtype_null_duration();
			}

			/* RECURRENCE-ID */
			event->recurrence_id = icalcomponent_get_recurrenceid(item);

			/* STATUS */
			event->status = icalcomponent_get_status(item);
			if (event->status == (icalproperty_status) 0) {
				event->status = ICAL_STATUS_NONE;
			}

			/* RRULE (only one is allowed, see RFC 5546, Sect. 3.2.2) */
			prop = icalcomponent_get_first_property(item, ICAL_RRULE_PROPERTY);
			if (prop != NULL) {
				event->rrule = icalproperty_get_rrule(prop);
			}

			/* UID, SUMMARY, LOCATION, DESCRIPTION */
			event->uid = g_strdup(icalcomponent_get_uid(item));
			event->summary = g_strdup(icalcomponent_get_summary(item));
			event->location = g_strdup(icalcomponent_get_location(item));
			event->description = g_strdup(icalcomponent_get_description(item));

			/* ORGANIZER */
			prop = icalcomponent_get_first_property(item, ICAL_ORGANIZER_PROPERTY);
			if (prop != NULL) {
				event->organizer = cal_address_5545_to_lbaddress(prop, TRUE);
			}

			/* ATTENDEE */
			for (prop = icalcomponent_get_first_property(item, ICAL_ATTENDEE_PROPERTY);
				 prop != NULL;
				 prop = icalcomponent_get_next_property(item, ICAL_ATTENDEE_PROPERTY)) {
				LibBalsaAddress *attendee;

				attendee = cal_address_5545_to_lbaddress(prop, FALSE);
				if (attendee != NULL) {
					event->attendee = g_list_prepend(event->attendee, attendee);
				}
			}

		    /* SEQUENCE (if present) */
			event->sequence = icalcomponent_get_sequence(item);

			/* CATEGORIES */
			for (prop = icalcomponent_get_first_property(item, ICAL_CATEGORIES_PROPERTY);
				 prop != NULL;
				 prop = icalcomponent_get_next_property(item, ICAL_CATEGORIES_PROPERTY)) {
				event->categories = g_list_append(event->categories, g_strdup(icalproperty_get_categories(prop)));
			}
		}
	}

	return retval;
}


/* extract a rfc 5545 mailto address and into a LibBalsaAddress and add the
 * rfc 5545 attributes as data to the returned object. */
static LibBalsaAddress *
cal_address_5545_to_lbaddress(icalproperty *prop, gboolean is_organizer)
{
	const gchar *uri;
    LibBalsaAddress *retval = NULL;

    if (is_organizer) {
    	uri = icalproperty_get_organizer(prop);
    } else {
    	uri = icalproperty_get_attendee(prop);
    }

    /* must be a mailto: uri */
    if (g_ascii_strncasecmp("mailto:", uri, 7) == 0) {
        LibBalsaVCalInfo *info;
		icalparameter *param;

        retval = libbalsa_address_new();
        libbalsa_address_append_addr(retval, &uri[7]);

        info = g_new0(LibBalsaVCalInfo, 1);
        g_object_set_data_full(G_OBJECT(retval), RFC2445_INFO, info, g_free);
        if (!is_organizer) {
            info->role = ICAL_ROLE_REQPARTICIPANT;
        } else {
        	info->role = ICAL_ROLE_NONE;
        }
        info->part_stat = ICAL_PARTSTAT_NONE;
        info->rsvp = ICAL_RSVP_NONE;

		for (param = icalproperty_get_first_parameter(prop, ICAL_ANY_PARAMETER);
			 param != NULL;
			 param = icalproperty_get_next_parameter(prop, ICAL_ANY_PARAMETER)) {
			switch (icalparameter_isa(param)) {
			case ICAL_CN_PARAMETER:
				libbalsa_address_set_full_name(retval, icalparameter_get_cn(param));
				break;
			case ICAL_PARTSTAT_PARAMETER:
				info->part_stat = icalparameter_get_partstat(param);
				break;
			case ICAL_ROLE_PARAMETER:
				info->role = icalparameter_get_role(param);
				break;
			case ICAL_RSVP_PARAMETER:
				info->rsvp = icalparameter_get_rsvp(param);
				break;
			default:
				/* nothing to do, make gcc happy */
				break;
			}
		}
    }

    return retval;
}


/* return a rfc 2445 role as human-readable string */
static const gchar *
vcal_role_to_str(icalparameter_role role)
{
    static struct {
    	icalparameter_role role;
    	gchar *role_str;
    } roles[] = {
		{ ICAL_ROLE_NONE, NC_("ical_role", "unknown") },
		{ ICAL_ROLE_CHAIR, NC_("ical_role", "chair") },
		{ ICAL_ROLE_REQPARTICIPANT, NC_("ical_role", "required participant") },
		{ ICAL_ROLE_OPTPARTICIPANT, NC_("ical_role", "optional participant") },
		{ ICAL_ROLE_NONPARTICIPANT, NC_("ical_role", "non-participant, information only") }
    };
    guint n;

    for (n = 0U; (n < G_N_ELEMENTS(roles)) && (roles[n].role != role); n++) {
    	/* nothing to do */
    }
    return (n < G_N_ELEMENTS(roles)) ? roles[n].role_str : roles[0].role_str;
}


static gchar *
icaltime_str(icaltimetype ical_time, const gchar *format_str)
{
	gchar *result = NULL;

	if (icaltime_is_null_time(ical_time) == 0) {
		time_t unix_time;

		unix_time = icaltime_as_timet_with_zone(ical_time, ical_time.zone);
		if (ical_time.is_date == 0) {
			result = libbalsa_date_to_utf8(unix_time, format_str);
		} else {
			result = libbalsa_date_to_utf8(unix_time, "%x");
		}
	}

	return result;
}
