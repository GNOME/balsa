/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "libbalsa.h"

#include <glib.h>

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>

#if ENABLE_LDAP
#include <ldap.h>
#endif

#if HAVE_COMPFACE
#include <compface.h>
#endif                          /* HAVE_COMPFACE */

#if HAVE_GTKSOURCEVIEW
#include <gtksourceview/gtksource.h>
#endif

#if HAVE_CANBERRA
#include <canberra-gtk.h>
#endif

#include "misc.h"
#include "missing.h"
#include "x509-cert-widget.h"
#include "html.h"
#include <glib/gi18n.h>

static GThread *main_thread_id;


void
libbalsa_init(void)
{
    main_thread_id = g_thread_self();

    g_mime_init(); /* Registers all GMime types */

    /* Register our types to avoid possible race conditions. See
       output of "valgrind --tool=helgrind --log-file=balsa.log balsa"
       Mailbox type registration is needed also for
       libbalsa_mailbox_new_from_config() to work. */
    LIBBALSA_TYPE_MAILBOX_LOCAL;
    LIBBALSA_TYPE_MAILBOX_POP3;
    LIBBALSA_TYPE_MAILBOX_IMAP;
    LIBBALSA_TYPE_MAILBOX_MBOX;
    LIBBALSA_TYPE_MAILBOX_MH;
    LIBBALSA_TYPE_MAILBOX_MAILDIR;
    LIBBALSA_TYPE_MESSAGE;

    LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ;
    LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
#if ENABLE_LDAP
    LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
#endif
#if HAVE_GPE
    LIBBALSA_TYPE_ADDRESS_BOOK_GPE;
#endif
#if HAVE_OSMO
    LIBBALSA_TYPE_ADDRESS_BOOK_OSMO;
#endif
#if HAVE_WEBDAV
    LIBBALSA_TYPE_ADDRESS_BOOK_CARDDAV;
#endif
#ifdef HAVE_HTML_WIDGET
    libbalsa_html_init();
#endif
}


/* libbalsa_guess_mail_spool

   Returns an allocated gchar * with our best guess of the user's
   mail spool file.
*/
gchar *
libbalsa_guess_mail_spool(void)
{
    gchar *env;
    gchar *spool;
    static const gchar *guesses[] = {
	"/var/mail/",
	"/var/spool/mail/",
	"/usr/spool/mail/",
	"/usr/mail/",
	NULL
    };

    if ((env = getenv("MAIL")) != NULL)
	return g_strdup(env);

    if ((env = getenv("USER")) != NULL) {
        int i;

	for (i = 0; guesses[i] != NULL; i++) {
	    spool = g_strconcat(guesses[i], env, NULL);

	    if (g_file_test(spool, G_FILE_TEST_EXISTS))
		return spool;

	    g_free(spool);
	}
    }

    /* libmutt's configure.in indicates that this 
     * ($HOME/mailbox) exists on
     * some systems, and it's a good enough default if we
     * can't guess it any other way. */
    return g_strconcat(g_get_home_dir(), "/mailbox", NULL);
}


gchar*
libbalsa_date_to_utf8(const time_t date, const gchar *date_string)
{
	gchar *result;

    g_return_val_if_fail(date_string != NULL, NULL);

    if (date == (time_t) 0) {
        /* Missing "Date:" field?  It is required by RFC 2822. */
        result = NULL;
    } else {
    	GDateTime *footime;

    	footime = g_date_time_new_from_unix_local(date);
    	result = g_date_time_format(footime, date_string);
    	g_date_time_unref(footime);
    }
    return result;
}

LibBalsaMessageStatus
libbalsa_get_icon_from_flags(LibBalsaMessageFlag flags)
{
    LibBalsaMessageStatus icon;
    if (flags & LIBBALSA_MESSAGE_FLAG_DELETED)
	icon = LIBBALSA_MESSAGE_STATUS_DELETED;
    else if (flags & LIBBALSA_MESSAGE_FLAG_NEW)
	icon = LIBBALSA_MESSAGE_STATUS_UNREAD;
    else if (flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	icon = LIBBALSA_MESSAGE_STATUS_FLAGGED;
    else if (flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
	icon = LIBBALSA_MESSAGE_STATUS_REPLIED;
    else
	icon = LIBBALSA_MESSAGE_STATUS_ICONS_NUM;
    return icon;
}


typedef struct {
    GMutex lock;
    GCond condvar;
    int (*cb)(void *arg);
    void *arg;
    gboolean done;
    int res;
} AskData;

/* ask_cert_idle:
   called in MT mode by the main thread.
 */
static gboolean
ask_idle(gpointer data)
{
    AskData* ad = (AskData*)data;
    g_debug("ask_idle: ENTER %p", data);
    ad->res = (ad->cb)(ad->arg);
    ad->done = TRUE;
    g_cond_signal(&ad->condvar);
    g_debug("ask_idle: LEAVE %p", data);
    return FALSE;
}

/* libbalsa_ask_mt:
   executed with GDK UNLOCKED. see mailbox_imap_open() and
   imap_dir_cb()/imap_folder_imap_dir().
*/
static int
libbalsa_ask(gboolean (*cb)(void *arg), void *arg)
{
    AskData ad;

    if (!libbalsa_am_i_subthread()) {
        int ret;
        g_debug("main thread asks the following question");
        ret = cb(arg);
        return ret;
    }
    g_debug("side thread asks the following question");
    g_mutex_init(&ad.lock);
    g_cond_init(&ad.condvar);
    ad.cb  = cb;
    ad.arg = arg;
    ad.done = FALSE;

    g_mutex_lock(&ad.lock);
    g_idle_add(ask_idle, &ad);
    while (!ad.done) {
    	g_cond_wait(&ad.condvar, &ad.lock);
    }

    g_cond_clear(&ad.condvar);
    g_mutex_unlock(&ad.lock);
    return ad.res;
}


static int libbalsa_ask_for_cert_acceptance(GTlsCertificate      *cert,
											GTlsCertificateFlags  errors);

static GList *accepted_certs = NULL; /* GTlsCertificate items accepted for this session */
static GMutex certificate_lock;


#define CERT_ACCEPT_NO			0
#define CERT_ACCEPT_SESSION		1
#define CERT_ACCEPT_PERMANENT	2


gboolean
libbalsa_is_cert_known(GTlsCertificate      *cert,
					   GTlsCertificateFlags  errors)
{
	gchar *cert_file;
	GList *cert_db;
	gboolean cert_ok;
	GList *lst;

	/* check the list of accepted certificates for this session */
	g_mutex_lock(&certificate_lock);
	for (lst = accepted_certs; lst; lst = lst->next) {
		if (g_tls_certificate_is_same(cert, G_TLS_CERTIFICATE(lst->data))) {
			g_mutex_unlock(&certificate_lock);
			return TRUE;
		}
	}

	/* check the database of accepted certificates */
	cert_file = g_build_filename(g_get_user_config_dir(), "balsa", "certificates", NULL);
	cert_db = g_tls_certificate_list_new_from_file(cert_file, NULL);
	g_mutex_unlock(&certificate_lock);

	cert_ok = FALSE;
	for (lst = cert_db; !cert_ok && (lst != NULL); lst = g_list_next(lst)) {
		if (g_tls_certificate_is_same(cert, G_TLS_CERTIFICATE(lst->data))) {
			cert_ok = TRUE;
		}
	}
	g_list_free_full(cert_db, g_object_unref);

	/* ask the user if the certificate is not in the data base */
	if (!cert_ok) {
		int accepted;

		accepted = libbalsa_ask_for_cert_acceptance(cert, errors);
		g_mutex_lock(&certificate_lock);
		if (accepted == CERT_ACCEPT_SESSION) {
			accepted_certs = g_list_prepend(accepted_certs, g_object_ref(cert));
			cert_ok = TRUE;
		} else if (accepted == CERT_ACCEPT_PERMANENT) {
			gchar *pem_data;
			FILE *fd;

			g_object_get(cert, "certificate-pem", &pem_data, NULL);
			fd = fopen(cert_file, "a");
			if (fd != NULL) {
				fputs(pem_data, fd);
				fclose(fd);
			}
			g_free(pem_data);
			cert_ok = TRUE;
		} else {
			/* nothing to do */
		}
		g_mutex_unlock(&certificate_lock);
	}
	g_free(cert_file);

	return cert_ok;
}

/* libbalsa_ask_for_cert_acceptance():
   returns:
   OP_EXIT on reject.
   OP_SAVE - on accept and save.
   OP_MAX - on accept once.
   TODO: check treading issues.

*/
struct AskCertData {
    GTlsCertificate *certificate;
    gchar *explanation;
};


static int
ask_cert_real(void *data)
{
    struct AskCertData *acd = (struct AskCertData*)data;
    GtkWidget *dialog;
    GtkWidget *cert_widget;
    GString *str;
    unsigned i;
    GtkWidget *label;
    GtkWidget *content_area;

    /* never accept if the certificate is broken, resulting in a NULL widget */
    cert_widget = x509_cert_chain_tls(acd->certificate);
    if (cert_widget == NULL) {
    	libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("broken TLS certificate"));
    	return CERT_ACCEPT_NO;
    }

    dialog = gtk_dialog_new_with_buttons(_("SSL/TLS certificate"),
                                         NULL, /* FIXME: NULL parent */
                                         GTK_DIALOG_MODAL |
                                         libbalsa_dialog_flags(),
                                         _("_Accept Once"), 0,
                                         _("Accept & _Save"), 1,
                                         _("_Reject"), GTK_RESPONSE_CANCEL,
                                         NULL);
    gtk_window_set_role(GTK_WINDOW(dialog), "tls_cert_dialog");
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    str = g_string_new("");
    g_string_printf(str, _("<big><b>Authenticity of this certificate "
                           "could not be verified.</b></big>\n"
                           "Reason: %s"),
                    acd->explanation);
    label = gtk_label_new(str->str);
    g_string_free(str, TRUE);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    libbalsa_set_vmargins(label, 1);
    gtk_container_add(GTK_CONTAINER(content_area), label);
    gtk_widget_show(label);

    gtk_widget_set_vexpand(cert_widget, TRUE);
    gtk_widget_set_valign(cert_widget, GTK_ALIGN_FILL);
    libbalsa_set_vmargins(cert_widget, 1);
    gtk_container_add(GTK_CONTAINER(content_area), cert_widget);
    gtk_widget_show_all(cert_widget);

    switch(gtk_dialog_run(GTK_DIALOG(dialog))) {
    case 0:
    	i = CERT_ACCEPT_SESSION;
    	break;
    case 1:
    	i = CERT_ACCEPT_PERMANENT;
    	break;
    case GTK_RESPONSE_CANCEL:
    default:
    	i = CERT_ACCEPT_NO;
    	break;
    }
    gtk_widget_destroy(dialog);
    g_free(acd->explanation);
    return i;
}


static int
libbalsa_ask_for_cert_acceptance(GTlsCertificate      *cert,
								 GTlsCertificateFlags  errors)
{
    struct AskCertData acd;
    static const gchar *reason_msg[] = {
		N_("the signing certificate authority is not known"),
		N_("the certificate does not match the expected identity of the site that it was retrieved from"),
		N_("the certificate’s activation time is still in the future"),
		N_("the certificate has expired"),
		N_("the certificate has been revoked"),
		N_("the certificate’s algorithm is considered insecure"),
		N_("an error occurred validating the certificate")
    };
    GString *exp_buf = g_string_new(NULL);
    gsize n;

    acd.certificate = cert;
    for (n = 0U; n < G_N_ELEMENTS(reason_msg); n++) {
    	if ((errors & (1U << n)) != 0U) {
    		g_string_append_printf(exp_buf, "\n\342\200\242 %s", reason_msg[n]);
    	}
    }

    if (exp_buf->len > 0U) {
    	acd.explanation = g_string_free(exp_buf, FALSE);
    } else {
    	g_string_free(exp_buf, TRUE);
    	acd.explanation = g_strdup_printf(_("unknown certificate validation error %u"), (unsigned) errors);
    }
    return libbalsa_ask(ask_cert_real, &acd);
}


gboolean
libbalsa_am_i_subthread(void)
{
    return g_thread_self() != main_thread_id;
}

/* Initialized by the front end. */
void (*libbalsa_progress_set_text) (LibBalsaProgress * progress,
                                    const gchar * text, guint total);
void (*libbalsa_progress_set_fraction) (LibBalsaProgress * progress,
                                        gdouble fraction);
void (*libbalsa_progress_set_activity) (gboolean set, const gchar * text);

/*
 * Face and X-Face header support.
 */
gchar *
libbalsa_get_header_from_path(const gchar * header, const gchar * path,
                              gsize * size, GError ** err)
{
    gchar *buf, *content;
    size_t name_len;
    gchar *p, *q;

    if (!g_file_get_contents(path, &buf, size, err))
        return NULL;

    content = buf;
    name_len = strlen(header);
    if (g_ascii_strncasecmp(content, header, name_len) == 0)
        /* Skip header and trailing colon: */
        content += name_len + 1;

    /* Unfold. */
    for (p = q = content; *p; p++)
        if (*p != '\r' && *p != '\n')
            *q++ = *p;
    *q = '\0';

    content = g_strdup(content);
    g_free(buf);

    return content;
}

GtkWidget *
libbalsa_get_image_from_face_header(const gchar * content, GError ** err)
{
    GMimeStream *stream;
    GMimeStream *stream_filter;
    GMimeFilter *filter;
    GByteArray *array;
    GtkWidget *image = NULL;

    stream = g_mime_stream_mem_new();
    stream_filter = g_mime_stream_filter_new(stream);

    filter = g_mime_filter_basic_new(GMIME_CONTENT_ENCODING_BASE64, FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), filter);
    g_object_unref(filter);

    g_mime_stream_write_string(stream_filter, content);
    g_object_unref(stream_filter);

    array = GMIME_STREAM_MEM(stream)->buffer;
    if (array->len == 0)
        g_set_error(err, LIBBALSA_IMAGE_ERROR,
                    LIBBALSA_IMAGE_ERROR_NO_DATA, _("No image data"));
    else {
        GdkPixbufLoader *loader =
            gdk_pixbuf_loader_new_with_type("png", NULL);

        gdk_pixbuf_loader_write(loader, array->data, array->len, err);
        gdk_pixbuf_loader_close(loader, *err ? NULL : err);

        if (!*err)
            image = gtk_image_new_from_pixbuf(gdk_pixbuf_loader_get_pixbuf
                                              (loader));
        g_object_unref(loader);
    }
    g_object_unref(stream);

    return image;
}

#if HAVE_COMPFACE
GtkWidget *
libbalsa_get_image_from_x_face_header(const gchar * content, GError ** err)
{
    gchar buf[2048];
    GdkPixbuf *pixbuf;
    guchar *pixels;
    gint lines;
    const gchar *p;
    GtkWidget *image = NULL;

    strncpy(buf, content, sizeof buf - 1);

    switch (uncompface(buf)) {
    case -1:
        g_set_error(err, LIBBALSA_IMAGE_ERROR, LIBBALSA_IMAGE_ERROR_FORMAT,
                    _("Invalid input format"));
        return image;
    case -2:
        g_set_error(err, LIBBALSA_IMAGE_ERROR, LIBBALSA_IMAGE_ERROR_BUFFER,
                    _("Internal buffer overrun"));
        return image;
    }

    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 48, 48);
    pixels = gdk_pixbuf_get_pixels(pixbuf);

    p = buf;
    for (lines = 48; lines > 0; --lines) {
        guint x[3];
        gint j, k;
        guchar *q;

        if (sscanf(p, "%8x,%8x,%8x,", &x[0], &x[1], &x[2]) != 3) {
            g_set_error(err, LIBBALSA_IMAGE_ERROR,
                        LIBBALSA_IMAGE_ERROR_BAD_DATA,
                        /* Translators: please do not translate Face. */
                        _("Bad X-Face data"));
            g_object_unref(pixbuf);
            return image;
        }
        for (j = 0, q = pixels; j < 3; j++)
            for (k = 15; k >= 0; --k){
                guchar c = x[j] & (1 << k) ? 0x00 : 0xff;
                *q++ = c;       /* red   */
                *q++ = c;       /* green */
                *q++ = c;       /* blue  */
            }
        p = strchr(p, '\n') + 1;
        pixels += gdk_pixbuf_get_rowstride(pixbuf);
    }

    image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);

    return image;
}
#endif                          /* HAVE_COMPFACE */

#if HAVE_GTKSOURCEVIEW
GtkWidget *
libbalsa_source_view_new(gboolean highlight_phrases)
{
    GtkSourceBuffer *sbuffer;
    GtkWidget *sview;


    static GtkSourceLanguageManager * lm = NULL;
    static GtkSourceStyleScheme * scheme = NULL;
    static GtkSourceLanguage * src_lang = NULL;

    /* initialise the source language manager if necessary */
    if (!lm) {
	const gchar * const * lm_dpaths;

	if ((lm = gtk_source_language_manager_new()) &&
	    (lm_dpaths = gtk_source_language_manager_get_search_path(lm))) {
	    gchar ** lm_rpaths;
	    gint n;

	    /* add the balsa share path to the language manager's paths - we
	     * cannot simply replace it as it still wants to see the
	     * RelaxNG schema... */
	    for (n = 0; lm_dpaths[n]; n++);
	    lm_rpaths = g_new0(gchar *, n + 2);
	    for (n = 0; lm_dpaths[n]; n++)
		lm_rpaths[n] = g_strdup(lm_dpaths[n]);
	    lm_rpaths[n] = g_strdup(BALSA_DATA_PREFIX "/gtksourceview-3.0");
	    gtk_source_language_manager_set_search_path(lm, lm_rpaths);
	    g_strfreev(lm_rpaths);

	    /* try to load the language */
	    if ((src_lang =
		 gtk_source_language_manager_get_language(lm, "balsa"))) {
		GtkSourceStyleSchemeManager *smgr =
		    gtk_source_style_scheme_manager_get_default();
		gchar * sm_paths[] = {
		    BALSA_DATA_PREFIX "/gtksourceview-3.0",
		    NULL };
	    
		/* try to load the colouring scheme */
		gtk_source_style_scheme_manager_set_search_path(smgr, sm_paths);
		scheme = gtk_source_style_scheme_manager_get_scheme(smgr, "balsa-mail");
	    }
	}
    }

    /* create a new buffer and set the language and scheme */
    sbuffer = gtk_source_buffer_new(NULL);
    if (src_lang)
	gtk_source_buffer_set_language(sbuffer, src_lang);
    if (scheme)
	gtk_source_buffer_set_style_scheme(sbuffer, scheme);
    gtk_source_buffer_set_highlight_syntax(sbuffer, TRUE);
    gtk_source_buffer_set_highlight_matching_brackets(sbuffer, FALSE);

    /* create & return the source view */
    sview = gtk_source_view_new_with_buffer(sbuffer);
    g_object_unref(sbuffer);

    return sview;
}
#endif  /* HAVE_GTKSOURCEVIEW */

#if HAVE_CANBERRA
gboolean
libbalsa_play_sound_event(const gchar *event_id, GError **error)
{
	GdkScreen *screen;
	gint rc;

	g_return_val_if_fail(event_id != NULL, FALSE);

	screen = gdk_screen_get_default();
	rc = ca_context_play(ca_gtk_context_get_for_screen(screen), 0, CA_PROP_EVENT_ID, event_id, NULL);
	if (rc != 0) {
		g_set_error(error, LIBBALSA_ERROR_QUARK, rc, _("Cannot play sound event “%s”: %s"), event_id, ca_strerror(rc));
	}
	return rc == 0;
}

gboolean
libbalsa_play_sound_file(const gchar *filename, GError **error)
{
	GdkScreen *screen;
	gint rc;

	g_return_val_if_fail(filename != NULL, FALSE);

	screen = gdk_screen_get_default();
	rc = ca_context_play(ca_gtk_context_get_for_screen(screen), 0, CA_PROP_MEDIA_FILENAME, filename, NULL);
	if (rc != 0) {
		g_set_error(error, LIBBALSA_ERROR_QUARK, rc, _("Cannot play sound file “%s”: %s"), filename, ca_strerror(rc));
	}
	return rc == 0;
}
#endif /* HAVE_CANBERRA */

/*
 * Error domains for GError:
 */

GQuark
libbalsa_scanner_error_quark(void)
{
    static GQuark quark = 0;
    if (quark == 0)
        quark = g_quark_from_static_string("libbalsa-scanner-error-quark");
    return quark;
}

GQuark
libbalsa_mailbox_error_quark(void)
{
    static GQuark quark = 0;
    if (quark == 0)
        quark = g_quark_from_static_string("libbalsa-mailbox-error-quark");
    return quark;
}

GQuark
libbalsa_image_error_quark(void)
{
    static GQuark quark = 0;
    if (quark == 0)
        quark = g_quark_from_static_string("libbalsa-image-error-quark");
    return quark;
}

gboolean
libbalsa_use_headerbar(void)
{
	static gboolean use_headerbar = TRUE;
	static gint check_done = 0;

	if (g_atomic_int_get(&check_done) == 0) {
		const gchar *dialog_env;

		dialog_env = g_getenv("BALSA_DIALOG_HEADERBAR");
		if ((dialog_env != NULL) && (atoi(dialog_env) == 0)) {
			use_headerbar = FALSE;
		}
		g_atomic_int_set(&check_done, 1);
	}
	return use_headerbar;
}

GtkDialogFlags
libbalsa_dialog_flags(void)
{
	return libbalsa_use_headerbar() ? GTK_DIALOG_USE_HEADER_BAR : (GtkDialogFlags) 0;
}
