/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2018 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "identity.h"

#include "rfc3156.h"
#include "libbalsa.h"
#include "information.h"
#include "libbalsa-conf.h"
#include <glib/gi18n.h>
#include "misc.h"

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

#ifdef HAVE_GPGME
#  include "libbalsa-gpgme.h"
#endif

#include <string.h>
#include "smtp-server.h"

/*
 * The class.
 */

/* Forward references. */
static void libbalsa_identity_dispose(GObject* object);
static void libbalsa_identity_finalize(GObject* object);

G_DEFINE_TYPE(LibBalsaIdentity, libbalsa_identity, G_TYPE_OBJECT)

static void
libbalsa_identity_class_init(LibBalsaIdentityClass* klass)
{
    GObjectClass* object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->dispose  = libbalsa_identity_dispose;
    object_class->finalize = libbalsa_identity_finalize;
}

/*
 * Instance inititialization function: set defaults for new objects.
 */
static void
libbalsa_identity_init(LibBalsaIdentity* ident)
{
    ident->identity_name = NULL;
    ident->ia = NULL;
    ident->replyto = NULL;
    ident->domain = NULL;
    ident->bcc = NULL;
    ident->reply_string = g_strdup(_("Re:"));
    ident->forward_string = g_strdup(_("Fwd:"));
    ident->send_mp_alternative = FALSE;
    ident->signature_path = NULL;
    ident->sig_executable = FALSE;
    ident->sig_sending = TRUE;
    ident->sig_whenforward = TRUE;
    ident->sig_whenreply = TRUE;
    ident->sig_separator = TRUE;
    ident->sig_prepend = FALSE;
    ident->gpg_sign = FALSE;
    ident->gpg_encrypt = FALSE;
    ident->always_trust = FALSE;
    ident->warn_send_plain = TRUE;
    ident->crypt_protocol = LIBBALSA_PROTECT_OPENPGP;
    ident->force_gpg_key_id = NULL;
    ident->force_smime_key_id = NULL;
    ident->request_mdn = FALSE;
    ident->request_dsn = FALSE;
    /*
    ident->face = NULL;
    ident->x_face = NULL;
    */
}

/*
 * Destroy the object, freeing all the values in the process.
 */
static void
libbalsa_identity_dispose(GObject * object)
{
    LibBalsaIdentity *ident = LIBBALSA_IDENTITY(object);

    g_clear_object(&ident->ia);
    g_clear_object(&ident->smtp_server);

    G_OBJECT_CLASS(libbalsa_identity_parent_class)->dispose(object);
}

static void
libbalsa_identity_finalize(GObject * object)
{
    LibBalsaIdentity *ident = LIBBALSA_IDENTITY(object);

    g_free(ident->identity_name);
    g_free(ident->replyto);
    g_free(ident->domain);
    g_free(ident->bcc);
    g_free(ident->reply_string);
    g_free(ident->forward_string);
    g_free(ident->signature_path);
    g_free(ident->face);
    g_free(ident->x_face);
    g_free(ident->force_gpg_key_id);
    g_free(ident->force_smime_key_id);

    G_OBJECT_CLASS(libbalsa_identity_parent_class)->finalize(object);
}

/*
 * Public methods.
 */

/*
 * Create a new object with the default identity name.  Does not add
 * it to the list of identities for the application.
 */
GObject*
libbalsa_identity_new(void)
{
    return libbalsa_identity_new_with_name(_("New Identity"));
}


/*
 * Create a new object with the specified identity name.  Does not add
 * it to the list of identities for the application.
 */
GObject*
libbalsa_identity_new_with_name(const gchar* ident_name)
{
    LibBalsaIdentity* ident;

    ident = g_object_new(LIBBALSA_TYPE_IDENTITY, NULL);
    libbalsa_identity_set_identity_name(ident, ident_name);

    return G_OBJECT(ident);
}


void
libbalsa_identity_set_identity_name(LibBalsaIdentity* ident, const gchar* name)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->identity_name);
    ident->identity_name = g_strdup(name);
}


void
libbalsa_identity_set_address(LibBalsaIdentity * ident,
                              InternetAddress * ia)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_set_object(&ident->ia, ia);
}


void
libbalsa_identity_set_replyto(LibBalsaIdentity* ident, const gchar* address)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->replyto);
    ident->replyto = g_strdup(address);
}


void
libbalsa_identity_set_domain(LibBalsaIdentity* ident, const gchar* dom)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->domain);
    ident->domain = g_strdup(dom);
}


void
libbalsa_identity_set_bcc(LibBalsaIdentity* ident, const gchar* bcc)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->bcc);
    ident->bcc = g_strdup(bcc);
}


void
libbalsa_identity_set_reply_string(LibBalsaIdentity* ident, const gchar* reply)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->reply_string);
    ident->reply_string = g_strdup(reply);
}


void
libbalsa_identity_set_forward_string(LibBalsaIdentity* ident, const gchar* forward)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->forward_string);
    ident->forward_string = g_strdup(forward);
}


void
libbalsa_identity_set_send_mp_alternative(LibBalsaIdentity* ident, gboolean send_mp_alternative)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->send_mp_alternative = send_mp_alternative;
}


void
libbalsa_identity_set_signature_path(LibBalsaIdentity* ident, const gchar* path)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->signature_path);
    ident->signature_path = g_strdup(path);
}


void
libbalsa_identity_set_sig_executable(LibBalsaIdentity* ident, gboolean sig_executable)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->sig_executable = sig_executable;
}


void
libbalsa_identity_set_sig_sending(LibBalsaIdentity* ident, gboolean sig_sending)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->sig_sending = sig_sending;
}


void
libbalsa_identity_set_sig_whenforward(LibBalsaIdentity* ident, gboolean forward)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->sig_whenforward = forward;
}


void
libbalsa_identity_set_sig_whenreply(LibBalsaIdentity* ident, gboolean reply)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->sig_whenreply = reply;
}


void
libbalsa_identity_set_sig_separator(LibBalsaIdentity* ident, gboolean separator)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->sig_separator = separator;
}


void
libbalsa_identity_set_sig_prepend(LibBalsaIdentity* ident, gboolean prepend)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->sig_prepend = prepend;
}


void
libbalsa_identity_set_face_path(LibBalsaIdentity *ident, const gchar *path)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->face);
    ident->face = g_strdup(path);
}


void
libbalsa_identity_set_x_face_path(LibBalsaIdentity *ident, const gchar *path)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->x_face);
    ident->x_face = g_strdup(path);
}


void
libbalsa_identity_set_request_mdn(LibBalsaIdentity *ident, gboolean request)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->request_mdn = request;
}


void
libbalsa_identity_set_request_dsn(LibBalsaIdentity *ident, gboolean request)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->request_dsn = request;
}


void
libbalsa_identity_set_always_trust(LibBalsaIdentity *ident, gboolean trust)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->always_trust = trust;
}


void
libbalsa_identity_set_warn_send_plain(LibBalsaIdentity *ident, gboolean warn)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->warn_send_plain = warn;
}


void
libbalsa_identity_set_force_gpg_key_id(LibBalsaIdentity *ident, const gchar *key_id)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->force_gpg_key_id);
    ident->force_gpg_key_id = g_strdup(key_id);
}


void
libbalsa_identity_set_force_smime_key_id(LibBalsaIdentity *ident, const gchar *key_id)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->force_smime_key_id);
    ident->force_smime_key_id = g_strdup(key_id);
}


/** Returns a signature for given identity, adding a signature prefix
    if needed. parent can be NULL. */
gchar*
libbalsa_identity_get_signature(LibBalsaIdentity * ident, GError ** error)
{
    gchar *ret = NULL, *path;
    gchar *retval;

    if (ident->signature_path == NULL || *ident->signature_path == '\0')
	return NULL;

    path = libbalsa_expand_path(ident->signature_path);
    if (ident->sig_executable) {
        gchar *argv[] = {"/bin/sh", "-c", path, NULL};
        gchar *standard_error = NULL;

        if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                          &ret, &standard_error, NULL, error)) {
            g_prefix_error(error, _("Error executing signature generator “%s”: "),
                           ident->signature_path);
        } else if (standard_error != NULL) {
            g_set_error(error, LIBBALSA_ERROR_QUARK, -1,
                        _("Error executing signature generator “%s”: %s"),
                        ident->signature_path, standard_error);
        }
        g_free(standard_error);
    } else {
    	if (!g_file_get_contents(path, &ret, NULL, error)) {
            g_prefix_error(error, _("Cannot read signature file “%s”: "),
                           ident->signature_path);
    	}
    }
    g_free(path);

    if ((error != NULL && *error != NULL) || ret == NULL)
        return NULL;

    if (!libbalsa_utf8_sanitize(&ret, FALSE, NULL)) {
        g_set_error(error, LIBBALSA_ERROR_QUARK, -1,
                    _("Signature in “%s” is not a UTF-8 text."),
                    ident->signature_path);
    }

    /* Prepend the separator if needed... */

    if (ident->sig_separator
        && !(g_str_has_prefix(ret, "--\n") || g_str_has_prefix(ret, "-- \n"))) {
        retval = g_strconcat("\n-- \n", ret, NULL);
    } else {
        retval = g_strconcat("\n", ret, NULL);
    }
    g_free(ret);

    return retval;
}

void
libbalsa_identity_set_smtp_server(LibBalsaIdentity * ident,
                                  LibBalsaSmtpServer * smtp_server)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_set_object(&ident->smtp_server, smtp_server);
}


/* libbalsa_identity_new_config:
   factory-type method creating new Identity object from given
   configuration data.
*/
LibBalsaIdentity*
libbalsa_identity_new_config(const gchar* name)
{
    LibBalsaIdentity* ident;
    gchar *fname, *email;
    gchar* tmpstr;

    fname = libbalsa_conf_get_string("FullName");
    email = libbalsa_conf_get_string("Address");

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new_with_name(name));
    ident->ia = internet_address_mailbox_new (fname, email);
    g_free(fname);
    g_free(email);

    ident->replyto = libbalsa_conf_get_string("ReplyTo");
    ident->domain = libbalsa_conf_get_string("Domain");
    ident->bcc = libbalsa_conf_get_string("Bcc");

    /*
     * these two have defaults, so we need to use the appropriate
     * functions to manage the memory.
     */
    if ((tmpstr = libbalsa_conf_get_string("ReplyString"))) {
        g_free(ident->reply_string);
        ident->reply_string = tmpstr;
    }

    if ((tmpstr = libbalsa_conf_get_string("ForwardString"))) {
        g_free(ident->forward_string);
        ident->forward_string = tmpstr;
    }
    ident->send_mp_alternative =
        libbalsa_conf_get_bool("SendMultipartAlternative");

    ident->signature_path = libbalsa_conf_get_string("SignaturePath");
    ident->sig_executable = libbalsa_conf_get_bool("SigExecutable");
    ident->sig_sending = libbalsa_conf_get_bool("SigSending");
    ident->sig_whenforward = libbalsa_conf_get_bool("SigForward");
    ident->sig_whenreply = libbalsa_conf_get_bool("SigReply");
    ident->sig_separator = libbalsa_conf_get_bool("SigSeparator");
    ident->sig_prepend = libbalsa_conf_get_bool("SigPrepend");
    ident->face = libbalsa_conf_get_string("FacePath");
    ident->x_face = libbalsa_conf_get_string("XFacePath");
    ident->request_mdn = libbalsa_conf_get_bool("RequestMDN");
    ident->request_dsn = libbalsa_conf_get_bool("RequestDSN");

    ident->gpg_sign = libbalsa_conf_get_bool("GpgSign");
    ident->gpg_encrypt = libbalsa_conf_get_bool("GpgEncrypt");
    ident->always_trust = libbalsa_conf_get_bool("GpgTrustAlways");
    ident->warn_send_plain = libbalsa_conf_get_bool("GpgWarnSendPlain=true");
    ident->crypt_protocol = libbalsa_conf_get_int("CryptProtocol=16");
    ident->force_gpg_key_id = libbalsa_conf_get_string("ForceKeyID");
    ident->force_smime_key_id = libbalsa_conf_get_string("ForceKeyIDSMime");

    return ident;
}

void
libbalsa_identity_save(LibBalsaIdentity* ident, const gchar* group)
{
    g_return_if_fail(ident);

    libbalsa_conf_push_group(group);
    libbalsa_conf_set_string("FullName", ident->ia ? ident->ia->name : NULL);

    if (ident->ia && INTERNET_ADDRESS_IS_MAILBOX (ident->ia))
        libbalsa_conf_set_string("Address", INTERNET_ADDRESS_MAILBOX(ident->ia)->addr);

    libbalsa_conf_set_string("ReplyTo", ident->replyto);
    libbalsa_conf_set_string("Domain", ident->domain);
    libbalsa_conf_set_string("Bcc", ident->bcc);
    libbalsa_conf_set_string("ReplyString", ident->reply_string);
    libbalsa_conf_set_string("ForwardString", ident->forward_string);
    libbalsa_conf_set_bool("SendMultipartAlternative", ident->send_mp_alternative);
    libbalsa_conf_set_string("SmtpServer",
                             libbalsa_smtp_server_get_name(ident->
                                                           smtp_server));

    libbalsa_conf_set_string("SignaturePath", ident->signature_path);
    libbalsa_conf_set_bool("SigExecutable", ident->sig_executable);
    libbalsa_conf_set_bool("SigSending", ident->sig_sending);
    libbalsa_conf_set_bool("SigForward", ident->sig_whenforward);
    libbalsa_conf_set_bool("SigReply", ident->sig_whenreply);
    libbalsa_conf_set_bool("SigSeparator", ident->sig_separator);
    libbalsa_conf_set_bool("SigPrepend", ident->sig_prepend);
    if (ident->face)
        libbalsa_conf_set_string("FacePath", ident->face);
    if (ident->x_face)
        libbalsa_conf_set_string("XFacePath", ident->x_face);
    libbalsa_conf_set_bool("RequestMDN", ident->request_mdn);
    libbalsa_conf_set_bool("RequestDSN", ident->request_dsn);

    libbalsa_conf_set_bool("GpgSign", ident->gpg_sign);
    libbalsa_conf_set_bool("GpgEncrypt", ident->gpg_encrypt);
    libbalsa_conf_set_bool("GpgTrustAlways", ident->always_trust);
    libbalsa_conf_set_bool("GpgWarnSendPlain", ident->warn_send_plain);
    libbalsa_conf_set_int("CryptProtocol", ident->crypt_protocol);
    libbalsa_conf_set_string("ForceKeyID", ident->force_gpg_key_id);
    libbalsa_conf_set_string("ForceKeyIDSMime", ident->force_smime_key_id);

    libbalsa_conf_pop_group();
}


/* collected helper stuff for GPGME support */

void
libbalsa_identity_set_gpg_sign(LibBalsaIdentity* ident, gboolean sign)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->gpg_sign = sign;
}


void
libbalsa_identity_set_gpg_encrypt(LibBalsaIdentity* ident, gboolean encrypt)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->gpg_encrypt = encrypt;
}


void
libbalsa_identity_set_crypt_protocol(LibBalsaIdentity* ident, gint protocol)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));
    ident->crypt_protocol = protocol;
}
