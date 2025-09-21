/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * Balsa E-Mail Client
 *
 * X509 certificate (TLS, S/MIME) widgets
 * Copyright (C) 2019 Albrecht Dreß <albrecht.dress@arcor.de>
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if HAVE_GCR
#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>
#else
#include <gnutls/x509.h>
#include <gnutls/crypto.h>
#include "libbalsa.h"
#endif
#include "misc.h"

#include <glib/gi18n.h>

#include "libbalsa-gpgme.h"
#include "libbalsa-gpgme-keys.h"
#include "x509-cert-widget.h"


/* stuff for displaying a certificate chain */
enum {
    CERT_NAME_COLUMN = 0,
	CERT_WIDGET_COLUMN,
	CERT_COLUMNS
};


typedef struct {
	gchar *label;
	GtkWidget *widget;
} cert_data_t;


static GtkWidget *x509_cert_widget_from_buffer(const guchar  *buffer,
											   size_t         buflen,
											   gchar        **subject);
static cert_data_t *cert_data_tls(GTlsCertificate *cert);
static cert_data_t *cert_data_smime(gpgme_ctx_t   ctx,
									gchar       **fingerprint);
static void cert_data_free(cert_data_t *cert_data);
static GtkWidget *create_chain_widget(GList *cert_list);
static void cert_selected_cb(GtkTreeView       *tree_view,
							 GtkTreePath       *path,
							 GtkTreeViewColumn *column,
							 GtkStack          *stack);


/* documentation - see header file */
GtkWidget *
x509_cert_chain_tls(GTlsCertificate *cert)
{
	GList *chain;
	GTlsCertificate *issuer;
	GtkWidget *widget;

	g_return_val_if_fail(G_IS_TLS_CERTIFICATE(cert), NULL);

	chain = g_list_prepend(NULL, cert_data_tls(cert));
	issuer = g_tls_certificate_get_issuer(cert);
	while (issuer != NULL) {
		GTlsCertificate *parent;

		chain = g_list_prepend(chain, cert_data_tls(issuer));

		/* get parent - note: this is part of the source certificate, so *don't* unref it */
		parent = g_tls_certificate_get_issuer(issuer);
		issuer = parent;
	}

	if (chain->next != NULL) {
		widget = create_chain_widget(chain);
	} else {
		widget = ((cert_data_t *) chain->data)->widget;
	}
	g_list_free_full(chain, (GDestroyNotify) cert_data_free);
	return widget;
}


/* documentation - see header file */
GtkWidget *
x509_cert_chain_smime(const gchar *fingerprint)
{
	gpgme_ctx_t ctx;
	GtkWidget *widget = NULL;

	if (fingerprint != NULL) {
		ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_CMS, NULL);
		if (ctx != NULL) {
			GList *chain = NULL;
			gchar *keyid;

			keyid = g_strdup(fingerprint);
			while (keyid != NULL) {
				chain = g_list_prepend(chain, cert_data_smime(ctx, &keyid));
			}
			gpgme_release(ctx);
			if (chain != NULL) {
				if (chain->next != NULL) {
					widget = create_chain_widget(chain);
				} else {
					widget = ((cert_data_t *) chain->data)->widget;
				}
				g_list_free_full(chain, (GDestroyNotify) cert_data_free);
			}
		}
	}

	if (widget == NULL) {
		widget = gtk_label_new(_("Broken key, cannot identify certificate chain."));
	}
	return widget;
}


/** \brief Create the certificate chain widget
 *
 * \param cert_list list of \ref cert_data_t items, starting with the root certificate
 * \return a newly created certificate chain widget
 *
 * The certificate chain widget is a VBox, containing the certificate chain tree view in the upper and a GtkStack in the lower part.
 * The latter displays the certificate selected in the tree view.
 */
static GtkWidget *
create_chain_widget(GList *cert_list)
{
	GtkWidget *vbox;
	GtkWidget *scrolledwin;
	GtkTreeStore *store;
	GtkTreeIter iter;
	GtkTreeIter parent;
	GtkWidget *tree_view;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreePath *path;
	GtkWidget *stack;
	GList *p;
	gboolean is_root;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, HIG_PADDING);
	scrolledwin = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_vexpand(scrolledwin, TRUE);
        gtk_widget_set_valign(scrolledwin, GTK_ALIGN_FILL);
        libbalsa_set_vmargins(scrolledwin, HIG_PADDING);
        gtk_container_add(GTK_CONTAINER(vbox), scrolledwin);

	store = gtk_tree_store_new(CERT_COLUMNS, G_TYPE_STRING, GTK_TYPE_WIDGET);
	tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_container_add(GTK_CONTAINER(scrolledwin), tree_view);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(NULL, renderer, "text", CERT_NAME_COLUMN, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(tree_view), FALSE);
	gtk_tree_view_set_level_indentation(GTK_TREE_VIEW(tree_view), 2 * HIG_PADDING);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);
	gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(tree_view), TRUE);

	stack = gtk_stack_new();
        gtk_widget_set_vexpand(stack, TRUE);
        gtk_widget_set_valign(stack, GTK_ALIGN_FILL);
        libbalsa_set_vmargins(stack, HIG_PADDING);
	gtk_container_add(GTK_CONTAINER(vbox), stack);
	g_signal_connect(tree_view, "row-activated", G_CALLBACK(cert_selected_cb), stack);

	is_root = TRUE;
	for (p = cert_list; p != NULL; p = p->next) {
		cert_data_t *cert = (cert_data_t *) p->data;

		gtk_widget_show(cert->widget);
		gtk_tree_store_append(store, &iter, is_root ? NULL : &parent);
		parent = iter;
		gtk_tree_store_set(store, &iter,
			CERT_NAME_COLUMN, cert->label,
			CERT_WIDGET_COLUMN, cert->widget,
			-1);
		gtk_stack_add_named(GTK_STACK(stack), cert->widget, cert->label);
		gtk_stack_set_visible_child(GTK_STACK(stack), cert->widget);
		is_root = FALSE;
	}

	gtk_tree_view_expand_all(GTK_TREE_VIEW(tree_view));
	gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view)), &iter);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree_view), path, NULL, FALSE, 0.0, 0.0);
	gtk_tree_path_free(path);
  	g_object_unref(store);

	return vbox;
}


/** \brief Certificate selection callback
 *
 * \param tree_view tree view
 * \param path selected tree view path
 * \param column selected tree view column, unused
 * \param stack target stack
 *
 * Switch the passed GtkStack to display the widget indicated by the \ref CERT_WIDGET_COLUMN of the selected tree view path.
 */
static void
cert_selected_cb(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn G_GNUC_UNUSED *column, GtkStack *stack)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter(model, &iter, path)) {
		GtkWidget *widget;

		gtk_tree_model_get(model, &iter, CERT_WIDGET_COLUMN, &widget, -1);
		gtk_stack_set_visible_child(stack, widget);
	}
}


/** \brief Get subject and widget for a S/MIME certificate
 *
 * \param ctx properly initialised GpgME context
 * \param fingerprint fingerprint of the S/MIME certificate to be loaded, replaced by the parent certificate fingerprint
 * \return a newly allocated certificate data item, containing subject string and widget
 *
 * If the certificate can be loaded using the passed fingerprint, the widget in the returned struct is a display widget as created
 * by calling x509_cert_widget_from_buffer(), and the subject is the sanitised primary uid.  The passed fingerprint is replaced by
 * the fingerprint of the issuer certificate.  The caller shall free this value if necessary.
 *
 * Otherwise, the widget is just a GtkLabel saying that the certificate is not available, the label contains the fingerprint string,
 * and the passed fingerprint is freed and returned as NULL.
 */
static cert_data_t *
cert_data_smime(gpgme_ctx_t ctx, gchar **fingerprint)
{
	gpgme_key_t key;
	GError *error = NULL;
	cert_data_t *result;

	result = g_new0(cert_data_t, 1U);
	key = libbalsa_gpgme_load_key(ctx, *fingerprint, &error);
	if (key != NULL) {
		gpgme_data_t keybuf;
		gchar *key_data;
		size_t key_bytes;

		result->label = libbalsa_cert_subject_readable(key->uids->uid);
		gpgme_data_new(&keybuf);
		(void) gpgme_op_export(ctx, *fingerprint, 0, keybuf);
		key_data = gpgme_data_release_and_get_mem(keybuf, &key_bytes);
		result->widget = x509_cert_widget_from_buffer((const guchar *) key_data, key_bytes, NULL);
		gpgme_free(key_data);

		/* check if we reached the root */
		if ((key->chain_id != NULL) && (g_ascii_strcasecmp(*fingerprint, key->chain_id) != 0)) {
			g_free(*fingerprint);
			*fingerprint = g_strdup(key->chain_id);
		} else {
			g_free(*fingerprint);
			*fingerprint = NULL;
		}
		gpgme_key_unref(key);
	} else {
		gchar *errbuf;

		result->label = g_strdup_printf(_("fingerprint %s"), *fingerprint);
		errbuf = g_strdup_printf(_("cannot load key with fingerprint %s: %s"), *fingerprint, error->message);
		g_error_free(error);
		result->widget = gtk_label_new(errbuf);
		g_free(errbuf);
		g_free(*fingerprint);
		*fingerprint = NULL;
	}

	return result;
}


/** \brief Free a certificate data item
 *
 * \param cert_data certificate data
 *
 * This function is used as callback for g_list_free_full() from x509_cert_chain_tls() and x509_cert_chain_smime() when cleaning up
 * the certificate chain data.  It frees cert_data_t::label and the passed data item itself, but \em not cert_data::widget, as it
 * is consumed in the returned widget of the aforementioned functions.
 */
static void
cert_data_free(cert_data_t *cert_data)
{
	g_free(cert_data->label);
	g_free(cert_data);
}


#if HAVE_GCR

/** \brief Get subject and widget for a TLS certificate
 *
 * \param cert TLS certificate
 * \return a newly allocated certificate data item, containing subject string and widget
 */
static cert_data_t *
cert_data_tls(GTlsCertificate *cert)
{
	GByteArray *der_data;
    GcrCertificate *gcr_cert;
	cert_data_t *result;

	result = g_new0(cert_data_t, 1U);
	g_object_get(cert, "certificate", &der_data, NULL);
	gcr_cert = gcr_simple_certificate_new(der_data->data, der_data->len);
	g_byte_array_unref(der_data);
	result->label = gcr_certificate_get_subject_name(gcr_cert);
	result->widget = GTK_WIDGET(gcr_certificate_widget_new(gcr_cert));
    g_object_unref(gcr_cert);
	return result;
}


/** \brief Create a certificate widget from a DER buffer
 *
 * \param buffer DER data buffer
 * \param buflen number of bytes in the DER data buffer
 * \param subject unused, required for a unified API when GCR is not available
 * \return a new (GCR) certificate widget
 */
static GtkWidget *
x509_cert_widget_from_buffer(const guchar *buffer, size_t buflen, gchar G_GNUC_UNUSED **subject)
{
    GcrCertificate *gcr_cert;
    GtkWidget *widget;

	g_return_val_if_fail((buffer != NULL) && (buflen > 0U), NULL);

	gcr_cert = gcr_simple_certificate_new(buffer, buflen);
    widget = GTK_WIDGET(gcr_certificate_widget_new(gcr_cert));
    g_object_unref(gcr_cert);
    return widget;
}

#else

/** \brief Create a GnuTLS certificate item from DER data
 *
 * \param buffer DER data buffer
 * \param buflen number of bytes in the DER data buffer
 * \return a new GnuTLS certificate item
 */
static gnutls_x509_crt_t G_GNUC_WARN_UNUSED_RESULT
get_gnutls_cert(const guchar *buffer, guint buflen)
{
	gnutls_x509_crt_t res_crt;
    int gnutls_res;

    gnutls_res = gnutls_x509_crt_init(&res_crt);
    if (gnutls_res == GNUTLS_E_SUCCESS) {
    	gnutls_datum_t data;

    	data.data = (unsigned char *) buffer;
    	data.size = buflen;
    	gnutls_res = gnutls_x509_crt_import(res_crt, &data, GNUTLS_X509_FMT_DER);
    	if (gnutls_res != GNUTLS_E_SUCCESS) {
    		gnutls_x509_crt_deinit(res_crt);
    		res_crt = NULL;
    	}
    } else {
    	res_crt = NULL;
    }

    if (res_crt == NULL) {
    	g_warning("GnuTLS: %d: %s", gnutls_res, gnutls_strerror(gnutls_res));
    }

    return res_crt;
}


/** \brief Get a DN from a GnuTLS certificate
 *
 * \param cert GnuTLS certificate
 * \param load_fn function pointer for loading the DN
 * \return a newly allocated, utf8-clean string containing the loaded DN
 */
static gchar * G_GNUC_WARN_UNUSED_RESULT
gnutls_get_dn(gnutls_x509_crt_t cert, int (*load_fn)(gnutls_x509_crt_t cert, char *buf, size_t *buf_size))
{
    size_t buf_size;
    gchar *str_buf;

    buf_size = 0U;
    (void) load_fn(cert, NULL, &buf_size);
    str_buf = g_malloc0(buf_size + 1U);
    if (load_fn(cert, str_buf, &buf_size) != GNUTLS_E_SUCCESS) {
    	g_free(str_buf);
    	str_buf = NULL;
    } else {
    	libbalsa_utf8_sanitize(&str_buf, TRUE, NULL);
    }
    return str_buf;
}


/** \brief Get the certificate fingerprint
 *
 * \param cert GnuTLS certificate
 * \param algo fingerprint hash algorithm
 * \return a newly allocated string containing the fingerprint as a series of colon-separated hex bytes
 */
static gchar * G_GNUC_WARN_UNUSED_RESULT
x509_fingerprint(gnutls_x509_crt_t cert, gnutls_digest_algorithm_t algo)
{
    size_t buf_size;
    guint8 *sha_buf;
    gchar *str_buf;
    size_t n;

    buf_size = gnutls_hash_get_len(algo);
    sha_buf = g_malloc(buf_size);
    gnutls_x509_crt_get_fingerprint(cert, algo, sha_buf, &buf_size);
    str_buf = g_malloc0((3 * buf_size) + 1U);
    for (n = 0U; n < buf_size; n++) {
    	sprintf(&str_buf[3U * n], "%02x:", sha_buf[n]);
    }
    g_free(sha_buf);
    str_buf[(3 * buf_size) - 1U] = '\0';
    return str_buf;
}


/** \brief Create a certificate widget from a DER buffer
 *
 * \param buffer DER data buffer
 * \param buflen number of bytes in the DER data buffer
 * \param subject if not NULL, filled with a newly allocated string containing the certificate's subject
 * \return a new (GCR) certificate widget
 *
 * The Widget is basically a label, containing the certificate's dn, the issuer dn, the SHA1 and SHA256 fingerprints, and the
 * validity period.
 */
static GtkWidget *
x509_cert_widget_from_buffer(const guchar *buffer, size_t buflen, gchar **subject)
{
    gnutls_x509_crt_t cert;
    GtkWidget *widget;
    GString *str;
    gchar *name;
    gchar *valid_from;
    gchar *valid_until;
    gchar *sha256_fp;
    gchar *c;

	cert = get_gnutls_cert(buffer, buflen);
    if (cert == NULL) {
    	g_warning("%s: unable to create gnutls cert", __func__);
    	return NULL;
    }

    str = g_string_new("<b>This certificate belongs to:</b>\n");

    name = gnutls_get_dn(cert, gnutls_x509_crt_get_dn);
    g_string_append(str, name);
    if (subject != NULL) {
    	*subject = name;
    } else {
    	g_free(name);
    }

    g_string_append(str, _("\n<b>This certificate was issued by:</b>\n"));
    name = gnutls_get_dn(cert, gnutls_x509_crt_get_issuer_dn);
    g_string_append_printf(str, "%s\n", name);
    g_free(name);

    name = x509_fingerprint(cert, GNUTLS_DIG_SHA1);
    sha256_fp = x509_fingerprint(cert, GNUTLS_DIG_SHA256);
    valid_from  = libbalsa_date_to_utf8(gnutls_x509_crt_get_activation_time(cert), "%x %X");
    valid_until = libbalsa_date_to_utf8(gnutls_x509_crt_get_expiration_time(cert), "%x %X");
    g_string_append_printf(str, _("<b>This certificate is valid</b>\n"
    							  "from %s\n"
    							  "to %s\n"
                         		  "<b>Fingerprint:</b>\nSHA1: %s\nSHA256: %s"),
                        	valid_from, valid_until, name, sha256_fp);
    g_free(sha256_fp);
    g_free(name);
    g_free(valid_from);
    g_free(valid_until);
    gnutls_x509_crt_deinit(cert);

    /* This string uses markup, so we must replace "&" with "&amp;" */
    c = str->str;
    while ((c = strchr(c, '&'))) {
        gssize pos;

        pos = (c - str->str) + 1;
        g_string_insert(str, pos, "amp;");
        c = str->str + pos;
    }
    widget = gtk_label_new(str->str);
    gtk_label_set_use_markup(GTK_LABEL(widget), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(widget), PANGO_ELLIPSIZE_END);
    g_string_free(str, TRUE);
    return widget;
}


/** \brief Get subject and widget for a TLS certificate
 *
 * \param cert TLS certificate
 * \return a newly allocated certificate data item, containing subject string and widget
 */
static cert_data_t *
cert_data_tls(GTlsCertificate *cert)
{
	GByteArray *der_data;
	cert_data_t *result;

	result = g_new0(cert_data_t, 1U);
	g_object_get(cert, "certificate", &der_data, NULL);
	result->widget = x509_cert_widget_from_buffer(der_data->data, der_data->len, &result->label);
	g_byte_array_unref(der_data);
	return result;
}

#endif
