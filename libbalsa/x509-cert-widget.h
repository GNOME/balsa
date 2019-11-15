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

#ifndef LIBBALSA_X509_CERT_WIDGET_H_
#define LIBBALSA_X509_CERT_WIDGET_H_


#include <gtk/gtk.h>


G_BEGIN_DECLS


/** \brief Create a certificate chain widget for a TLS certificate
 *
 * \param cert TLS certificate
 * \return a new widget on success or NULL on error
 *
 * If the passed certificate is self-signed or if the issuer cannot be determined, the function returns a widget containing the
 * certificate information.  Otherwise, the returned widget is a vertical GtkBox, containing the certificate chain tree view in the
 * upper and a GtkStack in the lower part.  The latter displays the certificate selected in the tree view.
 */
GtkWidget *x509_cert_chain_tls(GTlsCertificate *cert);

/** \brief Create a certificate chain widget for a S/MIME certificate
 *
 * \param fingerprint fingerprint of a S/MIME certificate
 * \return a new widget on success or NULL on error
 *
 * If S/MIME certificate identified by the fingerprint is self-signed or if the issuer cannot be determined, the function returns a
 * widget containing the certificate information.  Otherwise, the returned widget is a vertical GtkBox, containing the certificate
 * chain tree view in the upper and a GtkStack in the lower part.  The latter displays the certificate selected in the tree view.
 */
GtkWidget *x509_cert_chain_smime(const gchar *fingerprint);


G_END_DECLS


#endif		/* LIBBALSA_X509_CERT_WIDGET_H_ */
