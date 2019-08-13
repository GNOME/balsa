/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * VCalendar (RFC 2445) stuff
 * Copyright (C) 2009-2019 Albrecht Dreß <albrecht.dress@arcor.de>
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
#include "rfc2445.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "libbalsa.h"
#include "missing.h"


/* participant roles as defined by RFC 2446 */
typedef enum {
    VCAL_ROLE_UNKNOWN = 0,
    VCAL_ROLE_CHAIR,
    VCAL_ROLE_REQ_PART,
    VCAL_ROLE_OPT_PART,
    VCAL_ROLE_NON_PART
} LibBalsaVCalRole;


struct _LibBalsaVCal {
    GObject parent;

    /* method */
    LibBalsaVCalMethod method;

    /* linked list of VEVENT entries */
    GList *vevent;
};

G_DEFINE_TYPE(LibBalsaVCal, libbalsa_vcal, G_TYPE_OBJECT)


struct _LibBalsaVEvent {
    GObject parent;

    LibBalsaAddress *organizer;
    GList *attendee;
    time_t stamp;
    time_t start;
    gboolean start_date_only;
    time_t end;
    gboolean end_date_only;
    gchar *uid;
    gchar *summary;
    gchar *location;
    gchar *description;
};

G_DEFINE_TYPE(LibBalsaVEvent, libbalsa_vevent, G_TYPE_OBJECT)


/* LibBalsaAddress extra object data */
typedef struct {
    LibBalsaVCalRole role;
    LibBalsaVCalPartStat part_stat;
    gboolean rsvp;
} LibBalsaVCalInfo;
#define RFC2445_INFO            "RFC2445:Info"


static void libbalsa_vcal_finalize(GObject *self);

static void libbalsa_vevent_finalize(GObject *self);

static LibBalsaAddress *cal_address_2445_to_lbaddress(const gchar * uri,
						      gchar ** attributes,
						      gboolean
						      is_organizer);

/* conversion helpers */
static time_t date_time_2445_to_time_t(const gchar *date_time, const gchar *modifier, gboolean *date_only);
static gchar *time_t_to_date_time_2445(time_t ttime);
static gchar *text_2445_unescape(const gchar * text);
static gchar *text_2445_escape(const gchar * text);
static const gchar *vcal_role_to_str(LibBalsaVCalRole role);
static LibBalsaVCalMethod vcal_str_to_method(const gchar * method);
static LibBalsaVCalRole vcal_str_to_role(const gchar * role);
static LibBalsaVCalPartStat vcal_str_to_part_stat(const gchar * pstat);


static struct {
    gchar *str_2445;
    gchar *hr_text;
} pstats[] = {
    { NULL, N_("unknown") },
    { "NEEDS-ACTION", N_("needs action") },
    { "ACCEPTED", N_("accepted") },
    { "DECLINED", N_("declined") },
    { "TENTATIVE", N_("tentatively accepted") },
    { "DELEGATED", N_("delegated") },
    { "COMPLETED", N_("completed") },
    { "IN-PROCESS", N_("in process") }
};


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
    self->method = ITIP_UNKNOWN;
    self->vevent = NULL;
}


static void
libbalsa_vcal_finalize(GObject *self)
{
	LibBalsaVCal *vcal = LIBBALSA_VCAL(self);
	const GObjectClass *parent_class = G_OBJECT_CLASS(libbalsa_vcal_parent_class);

    if (vcal->vevent != NULL) {
    	g_list_free_full(vcal->vevent, g_object_unref);
    }

    (*parent_class->finalize)(self);
}


LibBalsaVCal *
libbalsa_vcal_new(void)
{
    return LIBBALSA_VCAL(g_object_new(LIBBALSA_TYPE_VCAL, NULL));
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
    self->start = self->end = self->stamp = (time_t) (-1);
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

	(*parent_class->finalize)(self);
}


LibBalsaVEvent *
libbalsa_vevent_new(void)
{
    return LIBBALSA_VEVENT(g_object_new(LIBBALSA_TYPE_VEVENT, NULL));
}


#define STR_REPL_2445_TXT(s, v)                 \
    do {                                        \
        g_free(s);                              \
        s = text_2445_unescape(v);              \
    } while (0)

/*
 * Find the first occurrence of c in s that is not in a double-quoted
 * string.
 *
 * Note that if c is '\0', the return value is NULL, *not* a pointer to
 * the terminating '\0' (c.f. strchr).
 */
static gchar *
vcal_strchr(gchar * s, gint c)
{
    gboolean is_quoted = FALSE;

    while (*s) {
        if (*s == '"')
            is_quoted = !is_quoted;
        else if (*s == c && !is_quoted)
            return s;
        ++s;
    }

    return NULL;
}

/*
 * Split the string s at unquoted occurrences of c.
 *
 * Note that c is a single character, not a string, as in g_strsplit,
 * and that there is no limit on the number of splits.
 *
 * Returns a newly-allocated NULL-terminated array of strings. Use
 * g_strfreev() to free it.
 */
static gchar **
vcal_strsplit_at_char(gchar * s, gint c)
{
    GPtrArray *array = g_ptr_array_new();
    gchar *p;

    while ((p = vcal_strchr(s, c))) {
        g_ptr_array_add(array, g_strndup(s, p - s));
        s = p + 1;
    }
    g_ptr_array_add(array, g_strdup(s));
    g_ptr_array_add(array, NULL);

    return (gchar **) g_ptr_array_free(array, FALSE);
}

/* parse a text/calendar part and convert it into a LibBalsaVCal object */
LibBalsaVCal *
libbalsa_vcal_new_from_body(LibBalsaMessageBody * body)
{
    LibBalsaVCal *retval;
    gchar *charset;
    gchar *method;
    gchar *vcal_buf;
    gchar *p;
    gchar **lines;
    LibBalsaVEvent *event;
    gboolean in_embedded;
    int k;

    g_return_val_if_fail(body != NULL, NULL);

    /* get the body buffer */
    if (libbalsa_message_body_get_content(body, &vcal_buf, NULL) <= 0)
	return NULL;

    /* check if the body has a charset (default is utf-8, see '2445,
     * sect 4.1.4) */
    charset = libbalsa_message_body_get_parameter(body, "charset");
    if (charset && g_ascii_strcasecmp(charset, "utf-8")) {
	gsize written;
	gchar *conv_buf;

	conv_buf =
	    g_convert(vcal_buf, -1, "utf-8", charset, NULL, &written,
		      NULL);
	g_free(vcal_buf);
	vcal_buf = conv_buf;
    }
    g_free(charset);
    if (!vcal_buf)
	return NULL;

    /* o.k., create a new object */
    retval = libbalsa_vcal_new();

    /* replace \r\n by \n */
    p = strchr(vcal_buf, '\r');
    while (p) {
        if (p[1] =='\n')
	    memmove(p, p + 1, strlen(p + 1) + 1);
	p = strchr(p + 1, '\r');
    }

    /* unfold the body and split into lines */
    p = strchr(vcal_buf, '\n');
    while (p) {
	if (p[1] == '\t' || p[1] == ' ')
	    memmove(p, p + 2, strlen(p + 2) + 1);
	p = strchr(p + 1, '\n');
    }
    lines = g_strsplit(vcal_buf, "\n", -1);
    g_free(vcal_buf);

    /* scan lines to extract vevent(s) */
    event = NULL;
    method = NULL;
    in_embedded = FALSE;
    for (k = 0; lines[k]; k++) {
	if (!event) {
            if (!method && !g_ascii_strncasecmp("METHOD:", lines[k], 7))
                method = g_strdup(lines[k] + 7);
	    if (!g_ascii_strcasecmp("BEGIN:VEVENT", lines[k]))
		event = libbalsa_vevent_new();
	} else if (strlen(lines[k])) {
	    gchar *value = vcal_strchr(lines[k], ':');
	    gchar **entry;

	    if (value)
		*value++ = '\0';
	    entry = vcal_strsplit_at_char(lines[k], ';');
	    if (!g_ascii_strcasecmp(entry[0], "END")) {
                if (value && !g_ascii_strcasecmp(value, "VEVENT")) {
                    retval->vevent = g_list_append(retval->vevent, event);
                    event = NULL;
                } else {
                    in_embedded = FALSE;
                }
            } else if (!g_ascii_strcasecmp(entry[0], "BEGIN"))
                in_embedded = TRUE;
            else if (!in_embedded) {
                if (!g_ascii_strcasecmp(entry[0], "DTSTART"))
                    event->start = date_time_2445_to_time_t(value, entry[1], &event->start_date_only);
                else if (!g_ascii_strcasecmp(entry[0], "DTEND"))
                    event->end = date_time_2445_to_time_t(value, entry[1], &event->end_date_only);
                else if (!g_ascii_strcasecmp(entry[0], "DTSTAMP"))
                    event->stamp = date_time_2445_to_time_t(value, entry[1], NULL);
                else if (!g_ascii_strcasecmp(entry[0], "UID"))
                    STR_REPL_2445_TXT(event->uid, value);
                else if (!g_ascii_strcasecmp(entry[0], "SUMMARY"))
                    STR_REPL_2445_TXT(event->summary, value);
                else if (!g_ascii_strcasecmp(entry[0], "LOCATION"))
                    STR_REPL_2445_TXT(event->location, value);
                else if (!g_ascii_strcasecmp(entry[0], "DESCRIPTION"))
                    STR_REPL_2445_TXT(event->description, value);
                else if (!g_ascii_strcasecmp(entry[0], "ORGANIZER")) {
                    if (event->organizer)
                        g_object_unref(event->organizer);
                    event->organizer =
                        cal_address_2445_to_lbaddress(value, entry + 1, TRUE);
                } else if (!g_ascii_strcasecmp(entry[0], "ATTENDEE"))
                    event->attendee =
                        g_list_prepend(event->attendee,
                                       cal_address_2445_to_lbaddress(value,
                                                                     entry + 1,
                                                                     FALSE));
            }
	    g_strfreev(entry);
	}
    }
    g_strfreev(lines);
    if (event)
	g_object_unref(event);

    /* set the method */
    retval->method = vcal_str_to_method(method);
    g_free(method);

    return retval;
}


/* return a rfc 2445 attendee (i.e. a LibBalsaAddress w/ extra information)
 * as a human-readable string */
gchar *
libbalsa_vcal_attendee_to_str(LibBalsaAddress * person)
{
    GString *retval;
    gchar *str;
    LibBalsaVCalInfo *info;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(person), NULL);

    retval = g_string_new("");

    info = g_object_get_data(G_OBJECT(person), RFC2445_INFO);
    if (info->role != VCAL_ROLE_UNKNOWN)
	g_string_printf(retval, "%s ", vcal_role_to_str(info->role));

    str = libbalsa_address_to_gchar(person, -1);
    retval = g_string_append(retval, str);
    g_free(str);

    if (info->part_stat != VCAL_PSTAT_UNKNOWN)
	g_string_append_printf(retval, " (%s)",
			       libbalsa_vcal_part_stat_to_str(info->part_stat));

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

    return info->rsvp;
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


time_t
libbalsa_vevent_timestamp(LibBalsaVEvent *event, LibBalsaVEventTimestamp which)
{
	time_t result;

	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event) &&
		(which >= VEVENT_DATETIME_STAMP) && (VEVENT_DATETIME_END), (time_t) -1);
	switch (which) {
	case VEVENT_DATETIME_STAMP:
		result = event->stamp;
		break;
	case VEVENT_DATETIME_START:
		result = event->start;
		break;
	case VEVENT_DATETIME_END:
		result = event->end;
		break;
	default:
		g_assert_not_reached();
	}
	return result;
}


gboolean
libbalsa_vevent_timestamp_date_only(LibBalsaVEvent *event, LibBalsaVEventTimestamp which)
{
	gboolean result;

	g_return_val_if_fail(LIBBALSA_IS_VEVENT(event) &&
		(which >= VEVENT_DATETIME_STAMP) && (VEVENT_DATETIME_END), FALSE);
	switch (which) {
	case VEVENT_DATETIME_STAMP:
		result = FALSE;
		break;
	case VEVENT_DATETIME_START:
		result = event->start_date_only;
		break;
	case VEVENT_DATETIME_END:
		result = event->end_date_only;
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


/* return a new buffer containing a proper reply to an event for a new
 * participant status */
gchar *
libbalsa_vevent_reply(const LibBalsaVEvent * event, const gchar * sender,
		      LibBalsaVCalPartStat new_stat)
{
    GString *retval;
    gchar *buf;
    gssize p;
    gchar *eol;

    /* check for allowed new state and sender */
    g_return_val_if_fail((new_stat == VCAL_PSTAT_ACCEPTED ||
			  new_stat == VCAL_PSTAT_DECLINED ||
			  new_stat == VCAL_PSTAT_TENTATIVE) &&
			 sender != NULL, NULL);

    /* build the part */
    retval = g_string_new("BEGIN:VCALENDAR\nVERSION:2.0\n"
			  "PRODID:-//GNOME//Balsa " BALSA_VERSION "//EN\n"
			  "METHOD:REPLY\nBEGIN:VEVENT\n");
    buf = time_t_to_date_time_2445(time(NULL));
    g_string_append_printf(retval, "DTSTAMP:%s\n", buf);
    g_free(buf);
    g_string_append_printf(retval, "ATTENDEE;PARTSTAT=%s:mailto:%s\n",
			   pstats[(int) new_stat].str_2445, sender);
    if (event->uid)
	g_string_append_printf(retval, "UID:%s\n", event->uid);
    if (event->organizer != NULL) {
        const gchar *addr = libbalsa_address_get_addr(event->organizer);

        if (addr != NULL)
            g_string_append_printf(retval, "ORGANIZER:mailto:%s\n", addr);
    }
    if (event->summary) {
	buf = text_2445_escape(event->summary);
	g_string_append_printf(retval, "SUMMARY:%s\n", buf);
	g_free(buf);
    }
    if (event->description) {
	buf = text_2445_escape(event->description);
	g_string_append_printf(retval, "DESCRIPTION:%s\n", buf);
	g_free(buf);
    }
    if (event->start != (time_t) - 1) {
	buf = time_t_to_date_time_2445(event->start);
	g_string_append_printf(retval, "DTSTART:%s\n", buf);
	g_free(buf);
    }
    if (event->end != (time_t) - 1) {
	buf = time_t_to_date_time_2445(event->end);
	g_string_append_printf(retval, "DTEND:%s\n", buf);
	g_free(buf);
    }
    retval = g_string_append(retval, "END:VEVENT\nEND:VCALENDAR");

    /* fold */
    p = 0;
    eol = strchr(retval->str, '\n');
    while (eol) {
	if (eol - (retval->str + p) > 74) {
	    retval = g_string_insert(retval, p + 74, "\n ");
	    p += 76;
	} else
	    p = eol - retval->str + 1;
	eol = strchr(retval->str + p, '\n');
    }

    /* done */
    return g_string_free(retval, FALSE);
}


/* -- rfc 2445 parser helper functions -- */

/* convert a rfc 2445 time string into a time_t value */
// FIXME - what about entries containing a TZID?
static time_t
date_time_2445_to_time_t(const gchar *date_time, const gchar *modifier, gboolean *date_only)
{
    gint len;
    time_t the_time = (time_t) (-1);

    g_return_val_if_fail(date_time != NULL, (time_t) (-1));
    len = strlen(date_time);

    /* must be yyyymmddThhmmssZ? */
    if (((len == 15) || ((len == 16) && (date_time[15] == 'Z'))) &&
    	(date_time[8] == 'T')) {
#if GLIB_CHECK_VERSION(2, 56, 0)
        GDateTime *datetime;

        datetime = g_date_time_new_from_iso8601(date_time, NULL);
        if (datetime != NULL) {
            the_time = g_date_time_to_unix(datetime);
            if (date_only != NULL) {
                *date_only = FALSE;
            }
            g_date_time_unref(datetime);
        }
#else /* GLIB_CHECK_VERSION(2, 56, 0) */
        GTimeVal timeval;

        /* the rfc2445 date-time is a special case of an iso8901 date/time value... */
        if (g_time_val_from_iso8601(date_time, &timeval)) {
        	the_time = timeval.tv_sec;
        	if (date_only != NULL) {
        		*date_only = FALSE;
        	}
        }
#endif /* GLIB_CHECK_VERSION(2, 56, 0) */
    } else if ((modifier!= NULL) && (g_ascii_strcasecmp(modifier, "VALUE=DATE") == 0) && (len == 8)) {
    	struct tm tm;

    	/* date only (yyyymmdd) */
    	memset(&tm, 0, sizeof(tm));
    	if (strptime(date_time, "%Y%m%d", &tm) != NULL) {
    		the_time = mktime(&tm);
        	if (date_only != NULL) {
        		*date_only = TRUE;
        	}
    	}
    }

    return the_time;
}


static gchar *
time_t_to_date_time_2445(time_t ttime)
{
	gchar *retval = NULL;
	GDateTime *date_time;

	date_time = g_date_time_new_from_unix_utc(ttime);
	if (date_time != NULL) {
		retval = g_date_time_format(date_time, "%Y%m%dT%H%M%SZ");
		g_date_time_unref(date_time);
	}
	return retval;
}


/* unescape a text from rfc 2445 format to a plain string */
static gchar *
text_2445_unescape(const gchar * text)
{
    gchar *retval;
    gchar *p;

    g_return_val_if_fail(text != NULL, NULL);
    retval = g_strdup(text);
    p = strchr(retval, '\\');
    while (p) {
	if (p[1] == 'n' || p[1] == 'N')
	    p[1] = '\n';
	memmove(p, p + 1, strlen(p + 1) + 1);
	p = strchr(retval, '\\');
    }
    return retval;
}


/* unescape a text from rfc 2445 format to a plain string */
static gchar *
text_2445_escape(const gchar * text)
{
    GString *retval;
    static const gchar *do_escape = "\\;,\n";
    const gchar *esc_p;

    g_return_val_if_fail(text != NULL, NULL);
    retval = g_string_new(text);
    for (esc_p = do_escape; *esc_p; esc_p++) {
	gchar *enc_p = strchr(retval->str, *esc_p);

	while (enc_p) {
	    gssize inspos = enc_p - retval->str;

	    if (*enc_p == '\n')
		*enc_p = 'n';
	    retval = g_string_insert_c(retval, inspos, '\\');
	    enc_p = strchr(retval->str + inspos + 2, *esc_p);
	}
    }
    return g_string_free(retval, FALSE);
}


/* extract a rfc 2445 mailto address and into a LibBalsaAddress and add the
 * extra information (rfc 2445 attributes) as data to the returned object. */
static LibBalsaAddress *
cal_address_2445_to_lbaddress(const gchar * uri, gchar ** attributes,
			      gboolean is_organizer)
{
    LibBalsaAddress *retval;
    LibBalsaVCalInfo *info;

    /* must be a mailto: uri */
    if (g_ascii_strncasecmp("mailto:", uri, 7))
	return NULL;

    retval = libbalsa_address_new();
    libbalsa_address_append_addr(retval, uri + 7);

    info = g_new0(LibBalsaVCalInfo, 1);
    g_object_set_data_full(G_OBJECT(retval), RFC2445_INFO, info, g_free);

    if (!is_organizer)
        info->role = VCAL_ROLE_REQ_PART;

    if (attributes) {
	int n;
	gchar *the_attr;

	/* scan attributes for extra information */
	for (n = 0; (the_attr = attributes[n]); n++) {
	    if (!g_ascii_strncasecmp(the_attr, "CN=", 3))
		libbalsa_address_set_full_name(retval, the_attr + 3);
	    else if (!g_ascii_strncasecmp(the_attr, "ROLE=", 5))
		info->role = vcal_str_to_role(the_attr + 5);
	    else if (!g_ascii_strncasecmp(the_attr, "PARTSTAT=", 9))
		info->part_stat = vcal_str_to_part_stat(the_attr + 9);
	    else if (!g_ascii_strncasecmp(the_attr, "RSVP=", 5))
		info->rsvp = g_ascii_strcasecmp(the_attr + 5, "TRUE") == 0;
	}
    }

    return retval;
}


/* -- conversion helpers string <--> enumeration -- */

/* convert the passed method string into the enumeration */
static LibBalsaVCalMethod
vcal_str_to_method(const gchar * method)
{
    static struct {
	gchar *meth_id;
	LibBalsaVCalMethod meth;
    } meth_list[] = {
	{ "PUBLISH", ITIP_PUBLISH },
        { "REQUEST", ITIP_REQUEST },
        { "REPLY", ITIP_REPLY },
        { "CANCEL", ITIP_CANCEL },
        { NULL, ITIP_UNKNOWN}
    };
    gint n;

    if (!method)
        return ITIP_UNKNOWN;
    for (n = 0;
	 meth_list[n].meth_id
	 && g_ascii_strcasecmp(method, meth_list[n].meth_id); n++);
    return meth_list[n].meth;
}


LibBalsaVCalMethod
libbalsa_vcal_method(LibBalsaVCal *vcal)
{
    g_return_val_if_fail(LIBBALSA_IS_VCAL(vcal) &&
    	(vcal->method >= ITIP_UNKNOWN) && (vcal->method <= ITIP_CANCEL), ITIP_UNKNOWN);
    return vcal->method;
}

/* return a rfc 2445 method as human-readable string */
const gchar *
libbalsa_vcal_method_str(LibBalsaVCal *vcal)
{
	static gchar *methods[] = {
		N_("unknown"),
		N_("Event Notification"),
		N_("Event Request"),
		N_("Reply to Event Request"),
		N_("Event Cancellation"),
	};

    g_return_val_if_fail(LIBBALSA_IS_VCAL(vcal) &&
    	(vcal->method >= ITIP_UNKNOWN) && (vcal->method <= ITIP_CANCEL), NULL);
    return methods[(int) vcal->method];
}


/* return a rfc 2445 role as human-readable string */
static const gchar *
vcal_role_to_str(LibBalsaVCalRole role)
{
    static gchar *roles[] = {
        N_("unknown"),
	N_("chair"),
	N_("required participant"),
	N_("optional participant"),
	N_("non-participant, information only")
    };

    g_return_val_if_fail(role >= VCAL_ROLE_UNKNOWN
			 && role <= VCAL_ROLE_NON_PART, NULL);
    return roles[(int) role];
}


/* convert the passed role string into the enumeration */
static LibBalsaVCalRole
vcal_str_to_role(const gchar * role)
{
    static struct {
	gchar *role_id;
	LibBalsaVCalRole role;
    } role_list[] = {
	{ "CHAIR", VCAL_ROLE_CHAIR },
        { "REQ-PARTICIPANT", VCAL_ROLE_REQ_PART },
        { "OPT-PARTICIPANT", VCAL_ROLE_OPT_PART },
        { "NON-PARTICIPANT", VCAL_ROLE_NON_PART },
        { NULL, VCAL_ROLE_UNKNOWN}
    };
    gint n;

    if (!role)
        return VCAL_ROLE_UNKNOWN;
    for (n = 0;
	 role_list[n].role_id
	 && g_ascii_strcasecmp(role, role_list[n].role_id); n++);
    return role_list[n].role;
}


/* return a rfc 2445 participant status as human-readable string */
const gchar *
libbalsa_vcal_part_stat_to_str(LibBalsaVCalPartStat pstat)
{
    g_return_val_if_fail(pstat >= VCAL_PSTAT_UNKNOWN
			 && pstat <= VCAL_PSTAT_IN_PROCESS, NULL);
    return pstats[(int) pstat].hr_text;

}

guint
libbalsa_vcal_vevents(LibBalsaVCal *vcal)
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

/* convert the passed participant status string into the enumeration */
static LibBalsaVCalPartStat
vcal_str_to_part_stat(const gchar * pstat)
{
    static struct {
	gchar *pstat_id;
	LibBalsaVCalPartStat pstat;
    } pstat_list[] = {
	{ "NEEDS-ACTION", VCAL_PSTAT_NEEDS_ACTION },
        { "ACCEPTED", VCAL_PSTAT_ACCEPTED},
        { "DECLINED", VCAL_PSTAT_DECLINED },
        { "TENTATIVE", VCAL_PSTAT_TENTATIVE },
        { "DELEGATED", VCAL_PSTAT_DELEGATED },
        { "COMPLETED", VCAL_PSTAT_COMPLETED },
        { "IN-PROCESS", VCAL_PSTAT_IN_PROCESS },
        { NULL, VCAL_PSTAT_UNKNOWN }
    };
    gint n;

    if (!pstat)
        return VCAL_PSTAT_UNKNOWN;
    for (n = 0;
	 pstat_list[n].pstat_id
	 && g_ascii_strcasecmp(pstat, pstat_list[n].pstat_id); n++);
    return pstat_list[n].pstat;
}
