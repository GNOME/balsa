/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2020 Stuart Parmenter and others,
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

#include "oauth2.h"


#if defined(HAVE_OAUTH2)

#include <glib/gi18n.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include "libbalsa-conf.h"
#include "net-client-utils.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "oauth"


#if defined (HAVE_LIBSECRET)
#include <libsecret/secret.h>
#endif                          /* defined(HAVE_LIBSECRET) */


/** Web page template for displaying a message in the web browser that the authorisation code has been received. */
#define OAUTH_AUTH_DONE_TEMPLATE													\
	"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">\n"			\
	"<html>\n"																		\
	"<head>\n"																		\
	"<meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\"/>\n"	\
	"<title>%s</title>\n"															\
	"</head>\n"																		\
	"<body>%s</p>%s</p></body></html>"


/** @brief Provider data
 *
 * This struct collects all information about the OAuth2-capable providers implemented here.
 */
typedef struct {
	gchar *id;							/**< Provider identifier. */
	gchar *display_name;				/**< Provider display name. */
	gchar *email_re;					/**< Regular Expression for e-mail addresses. */
	gchar *client_id;					/**< Balsa client ID. */
	gchar *client_secret;				/**< Balsa client "secret" (not really secret, as its hard-coded in the sources). */
	gchar *auth_uri;					/**< URI to call for authentication. */
	gchar *token_uri;					/**< URI to call for for receiving an access token. */
	gchar *scope;						/**< OAuth2 scope, not used by some providers. */
	gboolean oob_mode;					/**< Indicates if the provider cannot redirect to an arbitrary URI with port. */
} oauth2_provider_t;


/** @brief Authorisation context
 *
 * This struct collects the data required for performing the initial OAuth2 authorisation of Balsa for a particular account.
 */
typedef struct {
	LibBalsaOauth2 *oauth;				/**< Related OAuth2 object. */
	GtkWidget *auth_dialog;				/**< Authorisation dialogue. */
	GtkWidget *code_entry;				/**< Entry for authorisation code in OOB mode, NULL otherwise. */
	GtkWidget *spinner;					/**< Spinner and... */
	GtkWidget *spinner_label;			/**< ...related label. */
	gchar *auth_request_uri;			/**< URI for the authorisation request, to be opened in a web browser. */
	gchar *listen_uri;					/**< Local listen URI, or 'oob' if oauth2_provider_t::oob_mode is TRUE. */
	SoupServer *server;					/**< Locally listening server object, NULL if oauth2_provider_t::oob_mode is TRUE. */
	GError *auth_err;					/**< Location for propagating any error. */
} oauth2_auth_ctx_t;


struct _LibBalsaOauth2 {
	GObject parent;

	const oauth2_provider_t *provider;	/**< The provider information for this OAuth object. */
	gchar *account;						/**< The account name. */

	GMutex mutex;						/**< Mutex to avoid multiple auth server transactions in parallel. */
	gchar *access_token;				/**< Access token. */
	gint64 valid_until;					/**< Access token validity end, in UTC. */
	gchar *refresh_token;				/**< Refresh token. */
	gchar *scope;						/**< OAuth2 scope, may be NULL for some providers. */
};


G_DEFINE_TYPE(LibBalsaOauth2, libbalsa_oauth2, G_TYPE_OBJECT)


// FIXME:
// - not all OAuth2 providers are listed here
static const oauth2_provider_t providers[] = {
	{ 	// FIXME - limited account, users must be added explicitly for the time being
		// alternative: create your own account, and modify the client credentials below
		.id            = "gmail.com",
		.display_name  = "Gmail",
		.email_re      = "^.*@(gmail|googlemail|google)\\.com$",		// FIXME - are there more?
		.client_id     = "855255990023-n0gsa2lgg5ubudrce69kcvu0bpmi1m2q.apps.googleusercontent.com",
		.client_secret = "0Od0RKXFtomBoOgDidCyRxKs",
		.auth_uri      = "https://accounts.google.com/o/oauth2/v2/auth",
		.token_uri     = "https://oauth2.googleapis.com/token",
		.scope         = "https://mail.google.com/",
		.oob_mode      = FALSE },
	{	// see https://developer.verizonmedia.com/mail/mail-api-access/ and
		// https://developer.verizonmedia.com/mail/imap-smtp-documentation/
		// FIXME - Thunderbird client id & secret
		.id            = "yahoo.com",
		.display_name  = "Yahoo Mail",
		.email_re      = "^.*@yahoo\\.[a-z.]+$",						// FIXME - are there more?
		.client_id     = "dj0yJmk9NUtCTWFMNVpTaVJmJmQ9WVdrOVJ6UjVTa2xJTXpRbWNHbzlNQS0tJnM9Y29uc3VtZXJzZWNyZXQmeD0yYw--",
		.client_secret = "f2de6a30ae123cdbc258c15e0812799010d589cc",
		.auth_uri      = "https://api.login.yahoo.com/oauth2/request_auth",
		.token_uri     = "https://api.login.yahoo.com/oauth2/get_token",
		.scope         = "mail-w",
		.oob_mode      = TRUE },
	{	/*
		 * Microsoft Accounts: see
	     * https://docs.microsoft.com/en-us/Exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth
	     */
		.id            = "outlook.com",
		.display_name  = "Microsoft",
		.email_re      = "^.*@(outlook|hotmail)(\\.com)?\\.[a-z]{2,3}$",	// FIXME - are there more?
		.client_id     = "04a18be9-b37c-43a4-a58e-9c251ba1eb5b",
		.client_secret = NULL,
		.auth_uri      = "https://login.microsoftonline.com/common/oauth2/v2.0/authorize",
		.token_uri     = "https://login.microsoftonline.com/common/oauth2/v2.0/token",
		.scope         = "https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/POP.AccessAsUser.All "
						 "https://outlook.office.com/SMTP.Send offline_access",
		.oob_mode      = FALSE }
};


#if defined(HAVE_LIBSECRET)

/** @brief Balsa OAuth2 tokens in Secret Service
 *
 * The OAuth2 tokens stored in the Secret Service are identified by @em provider and @em user (i.e. the email address).  The value
 * is stored as binary data with the signature <c>(s(sx))</c>, containing the refresh token and the latest access token and its UTC
 * validity end time stamp.
 *
 * If Secret Service support is disabled, or if accessing it fails, Balsa falls back to the private configuration file.  The group
 * containing the tokes and validity is identified by the sha256 hash of provider id and user name.
 */
static const SecretSchema oauth2_schema = {
    "org.gnome.Balsa.OAuth2Token",
	SECRET_SCHEMA_NONE,
    { { "provider", SECRET_SCHEMA_ATTRIBUTE_STRING },
      { "user",     SECRET_SCHEMA_ATTRIBUTE_STRING },
	  { NULL,       0 } }
};
#endif  /* defined(HAVE_LIBSECRET) */


static void on_oauth_toggle_notify(gpointer  data,
								   GObject  *object,
								   gboolean  is_last_ref);
static void libbalsa_oauth2_finalise(GObject *object);
static const oauth2_provider_t *libbalsa_oauth2_find_provider(const gchar *mailbox);

#if defined(HAVE_LIBSECRET)
static void load_oauth2_token_libsecret(LibBalsaOauth2 *oauth);
static void save_oauth2_token_libsecret(const LibBalsaOauth2 *oauth);
static void erase_oauth2_token_libsecret(const LibBalsaOauth2 *oauth);
#endif  /* defined(HAVE_LIBSECRET) */
static void load_oauth2_token_config(LibBalsaOauth2 *oauth);
static void save_oauth2_token_config(const LibBalsaOauth2 *oauth);
static void erase_oauth2_token_config(const LibBalsaOauth2 *oauth);

static gboolean oauth2_refresh(LibBalsaOauth2  *oauth,
							   GError         **error);
static gboolean eval_json_auth_reply(LibBalsaOauth2     *oauth,
									 const SoupMessage  *message,
									 gboolean            is_auth,
									 GError            **error);
static void eval_json_error(const gchar        *prefix,
							const SoupMessage  *message,
							GError            **error);

static gboolean oauth2_authorise(LibBalsaOauth2  *oauth,
								 GtkWindow       *parent,
								 GError         **error);
static gboolean oauth2_authorise_idle(gpointer data);
static gboolean oauth2_authorise_real(LibBalsaOauth2  *oauth,
									  GtkWindow       *parent,
									  GError         **error);
static oauth2_auth_ctx_t *oauth2_authorize_init(const oauth2_provider_t  *provider,
												GError                  **error)
	G_GNUC_WARN_UNUSED_RESULT;
static void oauth2_listener_cb(SoupServer        *server,
							   SoupMessage       *msg,
							   const char        *path,
							   GHashTable        *query,
							   SoupClientContext *client,
							   gpointer           user_data);
static void oauth2_authorize_finish(oauth2_auth_ctx_t *auth_ctx,
									const gchar       *auth_code);
static gboolean run_oauth2_dialog(oauth2_auth_ctx_t		  *auth_ctx,
							  	  const oauth2_provider_t *provider,
								  const gchar             *account,
								  GtkWindow 			  *parent);
static SoupSession *oauth_soup_session_new(void);


/** @brief List of "known" @ref LibBalsaOauth2 items. */
static GList *oauth_list = NULL;
G_LOCK_DEFINE_STATIC(oauth_list);


/* == OAuth2 object implementation ============================================================================================== */

gboolean
libbalsa_oauth2_supported(const gchar *mailbox)
{
	return libbalsa_oauth2_find_provider(mailbox) != NULL;
}


LibBalsaOauth2 *
libbalsa_oauth2_new(LibBalsaServer *server, GError **error)
{
	const oauth2_provider_t *provider;
	const gchar *user;
	LibBalsaOauth2 *oauth = NULL;

	/* the user name is a mailbox, for which we can look up the suitable provider data */
	user = libbalsa_server_get_user(server);
	provider = libbalsa_oauth2_find_provider(user);
	if (provider == NULL) {
		g_set_error(error, LIBBALSA_OAUTH2_ERROR_QUARK, -1, _("OAuth2 is not supported for “%s”: provider unknown"),
			libbalsa_server_get_user(server));
	} else {
		GList *p;

		g_debug("%s: user='%s', provider '%s'", __func__, user, provider->display_name);
		G_LOCK(oauth_list);
		for (p = oauth_list;
			 (p != NULL) && (LIBBALSA_OAUTH2(p->data)->provider != provider) &&
			 (g_ascii_strcasecmp(LIBBALSA_OAUTH2(p->data)->account, user) != 0);
			 p = p->next) {
			/* noop */
		}

		if (p != NULL) {
			oauth = g_object_ref(LIBBALSA_OAUTH2(p->data));
			g_debug("%s: ref oauth %p", __func__, oauth);
		} else {
			oauth = g_object_new(LIBBALSA_OAUTH2_TYPE, NULL);
			oauth->provider = provider;
			oauth->account = g_strdup(user);
			g_debug("%s: new oauth %p", __func__, oauth);
			g_object_add_toggle_ref(G_OBJECT(oauth), on_oauth_toggle_notify, NULL);
			oauth_list = g_list_prepend(oauth_list, oauth);
		}
		G_UNLOCK(oauth_list);

		/* try to load stored tokens and validity */
#if defined(HAVE_LIBSECRET)
		load_oauth2_token_libsecret(oauth);
#else
		load_oauth2_token_config(oauth);
#endif	/* defined(HAVE_LIBSECRET) */
	}

	return oauth;
}


gchar *
libbalsa_oauth2_token(LibBalsaOauth2 *oauth, GtkWindow *parent, GError **error)
{
	gchar *result = NULL;
	gboolean save = FALSE;

	g_return_val_if_fail(LIBBALSA_IS_OAUTH2(oauth), NULL);

	g_mutex_lock(&oauth->mutex);

	if (oauth->refresh_token == NULL) {
		if (oauth2_authorise(oauth, parent, error)) {
			result = g_strdup(oauth->access_token);
			save = TRUE;
		}
	} else if ((oauth->access_token == NULL) || (oauth->valid_until <= time(NULL))) {
		if (oauth2_refresh(oauth, error)) {
			result = g_strdup(oauth->access_token);
			save = TRUE;
		}
	} else {
		result = g_strdup(oauth->access_token);
	}

	if (save) {
#if defined(HAVE_LIBSECRET)
		save_oauth2_token_libsecret(oauth);
#else
		save_oauth2_token_config(oauth);
#endif	/* defined(HAVE_LIBSECRET) */
	}

	g_mutex_unlock(&oauth->mutex);

	return result;
}


static void
libbalsa_oauth2_class_init(LibBalsaOauth2Class *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	gobject_class->finalize = libbalsa_oauth2_finalise;
}


static void
libbalsa_oauth2_init(LibBalsaOauth2 *self)
{
	g_mutex_init(&self->mutex);
}


static void
libbalsa_oauth2_finalise(GObject *object)
{
	LibBalsaOauth2 *oauth = LIBBALSA_OAUTH2(object);
	const GObjectClass *parent_class = G_OBJECT_CLASS(libbalsa_oauth2_parent_class);

	g_debug("%s: %p", __func__, object);
	g_free(oauth->account);
	g_free(oauth->access_token);
	g_free(oauth->refresh_token);
	g_free(oauth->scope);
	g_mutex_clear(&oauth->mutex);
	(*parent_class->finalize)(object);
}


static void
on_oauth_toggle_notify(gpointer G_GNUC_UNUSED data, GObject *object, gboolean is_last_ref)
{
	g_debug("%s: %p %d", __func__, object, is_last_ref);
	if (is_last_ref) {
		G_LOCK(oauth_list);
		oauth_list = g_list_remove(oauth_list, object);
		G_UNLOCK(oauth_list);
		g_object_remove_toggle_ref(object, on_oauth_toggle_notify, NULL);
	}
}


/** @brief Identify the provider from the mailbox address
 *
 * @param[in] mailbox user's email address
 * @return the provider item from @ref providers belonging to the passed email address, or NULL if it is not supported
 */
static const oauth2_provider_t *
libbalsa_oauth2_find_provider(const gchar *mailbox)
{
	guint n;

	for (n = 0U; n < G_N_ELEMENTS(providers); ++n) {
		if (g_regex_match_simple(providers[n].email_re, mailbox, G_REGEX_CASELESS, 0)) {
			return &providers[n];
		}
	}
	return NULL;
}


/* == OAuth2 token storage ====================================================================================================== */

#if defined(HAVE_LIBSECRET)

/** @brief Load the OAuth2 tokens from the Secret Service
 *
 * @param[in] oauth OAuth2 context
 *
 * Try to load the refresh and access tokens as well as the validity end time of the latter of the passed OAuth2 object from the
 * Secret Service.  If this fails, load_oauth2_token_config() is called to try the local private configuration file.
 */
static void
load_oauth2_token_libsecret(LibBalsaOauth2 *oauth)
{
	static GVariantType *token_gv_type = NULL;
	SecretValue *secret_value;
	GError *error = NULL;
	gboolean try_cfgfile;

	if (g_once_init_enter(&token_gv_type)) {
		g_once_init_leave(&token_gv_type, g_variant_type_new("(s(sx))"));
	}

	secret_value = secret_password_lookup_binary_sync(&oauth2_schema, NULL, &error, "provider", oauth->provider->id,
		"user", oauth->account, NULL);
	if (secret_value != NULL) {
		const gchar *sec_data;
		gsize sec_length;
		GVariant *tok_data;

		sec_data = secret_value_get(secret_value, &sec_length);
		tok_data = g_variant_new_from_data(token_gv_type, sec_data, sec_length, FALSE, NULL, NULL);
		if (tok_data != NULL) {
			g_free(oauth->access_token);
			g_free(oauth->refresh_token);
			g_variant_get(tok_data, "(s(sx))", &oauth->refresh_token, &oauth->access_token, &oauth->valid_until);
			g_variant_unref(tok_data);
			try_cfgfile = FALSE;
			g_debug("%s: loaded token for %s/%s from secret service", __func__, oauth->account, oauth->provider->id);
		} else {
			secret_password_clear_sync(&oauth2_schema, NULL, NULL, "provider", oauth->provider->id,	"user", oauth->account, NULL);
			g_info("invalid OAuth2 data in secret service, trying private config file");
			try_cfgfile = TRUE;
		}
		secret_value_unref(secret_value);
	} else {
		g_info("failed to load OAuth2 data from secret service, trying private config file: %s",
			(error != NULL) ? error->message : "unknown");
		try_cfgfile = TRUE;
		g_clear_error(&error);

	}

	if (try_cfgfile) {
		load_oauth2_token_config(oauth);
	}
}


/** @brief Store the OAuth2 tokens in the Secret Service
 *
 * @param[in] oauth OAuth2 context
 *
 * Try to save the refresh and access tokens as well as the validity end time of the latter of the passed OAuth2 object in the
 * Secret Service.  If this fails, save_oauth2_token_config() is called to try the local private configuration file.
 */
static void
save_oauth2_token_libsecret(const LibBalsaOauth2 *oauth)
{
	GVariant *tok_data;
	SecretValue *secret_value;
	gboolean result;
	GError *error = NULL;

	tok_data = g_variant_new("(s(sx))", oauth->refresh_token, oauth->access_token, oauth->valid_until);
	secret_value = secret_value_new(g_variant_get_data(tok_data), g_variant_get_size(tok_data), "application/octet-stream");
	g_variant_unref(tok_data);
	result = secret_password_store_binary_sync(&oauth2_schema, NULL, _("Balsa OAuth2"), secret_value, NULL, &error,
		"provider", oauth->provider->id, "user", oauth->account, NULL);
	secret_value_unref(secret_value);

	if (!result) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			_("cannot store OAuth2 access data in secret service, fall back to private config file: %s"),
			(error != NULL) ? error->message : _("unknown"));
		save_oauth2_token_config(oauth);
		g_clear_error(&error);
	} else {
		g_debug("%s: saved token for %s/%s in secret service", __func__, oauth->account, oauth->provider->id);
	}
}


/** @brief Remove OAuth2 tokens from the Secret Service
 *
 * @param[in] oauth OAuth2 context
 *
 * Remove all information of the passed OAuth2 object from the Secret Service.
 */
static void
erase_oauth2_token_libsecret(const LibBalsaOauth2 *oauth)
{
	gboolean result;
	GError *error = NULL;

	result =
		secret_password_clear_sync(&oauth2_schema, NULL, &error, "provider", oauth->provider->id, "user", oauth->account, NULL);
	if (!result) {
		g_warning("%s: cannot remove OAuth2 token for %s/%s from secret service: %s", __func__, oauth->account,
			oauth->provider->id, (error != NULL) ? error->message : "unknown");
		g_clear_error(&error);
	} else {
		g_debug("%s: removed token for %s/%s from secret service", __func__, oauth->account, oauth->provider->id);
	}
}

#endif  /* defined(HAVE_LIBSECRET) */


/** @brief Load the OAuth2 tokens from the private config file
 *
 * @param[in] oauth OAuth2 context
 *
 * Try to load the refresh and access tokens as well as the validity end time of the latter of the passed OAuth2 object from the
 * local private configuration file.
 */
static void
load_oauth2_token_config(LibBalsaOauth2 *oauth)
{
	GChecksum *hash;
	const gchar *group_name;

	hash = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(hash, (const guchar *) oauth->provider->id, strlen(oauth->provider->id));
	g_checksum_update(hash, (const guchar *) oauth->account, strlen(oauth->account));
	group_name = g_checksum_get_string(hash);
	libbalsa_conf_push_group(group_name);
	oauth->refresh_token = libbalsa_conf_private_get_string("Refresh", TRUE);
	oauth->access_token = libbalsa_conf_private_get_string("Access", TRUE);
	oauth->valid_until = libbalsa_conf_get_int_with_default_("Valid", NULL, TRUE);
	libbalsa_conf_pop_group();
	g_checksum_free(hash);
}


/** @brief Store the OAuth2 tokens in the private config file
 *
 * @param[in] oauth OAuth2 context
 *
 * Try to save the refresh and access tokens as well as the validity end time of the latter of the passed OAuth2 object in the
 * local private configuration file.
 */
static void
save_oauth2_token_config(const LibBalsaOauth2 *oauth)
{
	GChecksum *hash;
	const gchar *group_name;

	hash = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(hash, (const guchar *) oauth->provider->id, strlen(oauth->provider->id));
	g_checksum_update(hash, (const guchar *) oauth->account, strlen(oauth->account));
	group_name = g_checksum_get_string(hash);
	libbalsa_conf_push_group(group_name);
	libbalsa_conf_private_set_string("Refresh", oauth->refresh_token, TRUE);
	libbalsa_conf_private_set_string("Access", oauth->access_token, TRUE);
	libbalsa_conf_set_int_("Valid", oauth->valid_until, TRUE);
	libbalsa_conf_pop_group();
	g_checksum_free(hash);
}


/** @brief Remove OAuth2 tokens from the private config file
 *
 * @param[in] oauth OAuth2 context
 *
 * Remove all information of the passed OAuth2 object from the local private configuration file.
 */
static void
erase_oauth2_token_config(const LibBalsaOauth2 *oauth)
{
	GChecksum *hash;
	const gchar *group_name;

	hash = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(hash, (const guchar *) oauth->provider->id, strlen(oauth->provider->id));
	g_checksum_update(hash, (const guchar *) oauth->account, strlen(oauth->account));
	group_name = g_checksum_get_string(hash);
	libbalsa_conf_private_remove_group(group_name);
	g_checksum_free(hash);
}


/* == OAuth2 token refresh related functions ==================================================================================== */

/** @brief Refresh a OAuth2 access token
 *
 * @param[in] oauth OAuth2 context
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE on error
 *
 * Send a @c refresh_token request to the provider's oauth2_provider_t::token_uri, and call eval_json_auth_reply() to extract the
 * new access token.
 */
static gboolean
oauth2_refresh(LibBalsaOauth2 *oauth, GError **error)
{
	gboolean result;
	SoupSession *session;
	SoupMessage *message;
	guint status;

	g_return_val_if_fail((oauth->refresh_token != NULL) && (oauth->provider != NULL), FALSE);

	/* send the refresh token */
	g_debug("%s: post refresh request for %s: uri=%s id=%s secret=%s token=%s", __func__, oauth->account,
		oauth->provider->token_uri, oauth->provider->client_id, oauth->provider->client_secret, oauth->refresh_token);
	session = oauth_soup_session_new();
	if (oauth->provider->client_secret != NULL) {
		message = soup_form_request_new("POST", oauth->provider->token_uri,
			"grant_type", "refresh_token",
			"client_id", oauth->provider->client_id,
			"client_secret", oauth->provider->client_secret,
			"refresh_token", oauth->refresh_token,
			NULL);
	} else {
		message = soup_form_request_new("POST", oauth->provider->token_uri,
			"grant_type", "refresh_token",
			"client_id", oauth->provider->client_id,
			"refresh_token", oauth->refresh_token,
			NULL);
	}
	status = soup_session_send_message(session, message);
	g_debug("%s: status=%u, code=%u, reason=%s", __func__, status, message->status_code, message->reason_phrase);

	g_free(oauth->access_token);
	oauth->access_token = NULL;
	if ((status != SOUP_STATUS_OK) || (message->status_code != SOUP_STATUS_OK) || (message->response_body == NULL)) {
		eval_json_error(_("OAuth2 refresh token request failed"), message, error);
		/* a 4xy status probably means that we must re-authorise, so erase the refresh token in this case */
		if ((status / 100U) == 4U) {
			g_free(oauth->access_token);
			oauth->access_token = NULL;
			g_free(oauth->refresh_token);
			oauth->refresh_token = NULL;
#if defined(HAVE_LIBSECRET)
			erase_oauth2_token_libsecret(oauth);
#endif
			erase_oauth2_token_config(oauth);
		}
		result = FALSE;
	} else {
		result = eval_json_auth_reply(oauth, message, FALSE, error);
	}
	g_object_unref(message);
	g_object_unref(session);
	return result;
}


/** @brief Evaluate the JSON success reply from an OAuth2 server
 *
 * @param[in] oauth OAuth2 context
 * @param[in] message message received from the remote OAuth2 server which @em must have a non-NULL @c response_body
 * @param[in] is_auth TRUE if the JSON string @em must contain a refresh token (i.e. the reply to a authorisation request)
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE on error
 *
 * Parse the passed JSON string, extract the <c>access_token</c>, <c>scope</c> (if present, see below), <c>expires_in</c> and, for
 * an authorisation reply, the <c>refresh_token</c> elements, and set the respective values in the passed OAuth2 context.  Any
 * missing or malformed JSON element will result in an error.
 *
 * Apparently, some providers (e.g. Yahoo) do not include the scope in the response.  If this field is missing in the response,
 * just copy the scope from @ref oauth2_provider_t::scope.
 */
static gboolean
eval_json_auth_reply(LibBalsaOauth2 *oauth, const SoupMessage *message, gboolean is_auth, GError **error)
{
	gboolean result = FALSE;
	JsonParser *parser;

	parser = json_parser_new();
	if (json_parser_load_from_data(parser, message->response_body->data, message->response_body->length, error)) {
		JsonNode *root;

		root = json_parser_get_root(parser);
		if (JSON_NODE_HOLDS_OBJECT(root)) {
			JsonObject *reply;
			gint64 valid_secs;

			reply = json_node_get_object(root);
			oauth->access_token = g_strdup(json_object_get_string_member(reply, "access_token"));
			if (is_auth) {
				oauth->refresh_token = g_strdup(json_object_get_string_member(reply, "refresh_token"));
			}
			/* some providers don't include the scope in the reply */
			if (json_object_has_member(reply, "scope")) {
				oauth->scope = g_strdup(json_object_get_string_member(reply, "scope"));
			} else {
				oauth->scope = g_strdup(oauth->provider->scope);
			}
			valid_secs = json_object_get_int_member(reply, "expires_in");
			if ((oauth->access_token == NULL) || (oauth->refresh_token == NULL) || (oauth->scope == NULL) ||
				(valid_secs <= 0LL)) {
				g_set_error(error, LIBBALSA_OAUTH2_ERROR_QUARK, -1, _("OAuth2 authorization request failed: incomplete reply"));
				g_debug("%s: malformed json reply for %s/%s", __func__, oauth->account, oauth->provider->id);
			} else {
				oauth->valid_until = time(NULL) + valid_secs - 5;
				g_debug("%s: got token for %s/%s", __func__, oauth->account, oauth->provider->id);
				result = TRUE;
			}
		} else {
			g_set_error(error, LIBBALSA_OAUTH2_ERROR_QUARK, -1, _("OAuth2 authorization request failed: malformed reply"));
		}
	}
	g_object_unref(parser);

	return result;
}


/** @brief Evaluate the JSON error reply from an OAuth2 server
 *
 * @param[in] prefix error message prefix string
 * @param[in] message message received from the remote OAuth2 server
 * @param[out] error filled with the error information
 *
 * Fill the passed error location with the prefix and the @c error_description item received in the passed JSON message, or with
 * the status code and reason phrase if parsing the JSON body fails.
 */
static void
eval_json_error(const gchar *prefix, const SoupMessage *message, GError **error)
{
	gboolean assigned = FALSE;

	if (message->response_body != NULL) {
		JsonParser *parser;

		parser = json_parser_new();
		if (json_parser_load_from_data(parser, message->response_body->data, message->response_body->length, NULL)) {
			JsonNode *root;

			root = json_parser_get_root(parser);
			if (JSON_NODE_HOLDS_OBJECT(root)) {
				JsonObject *reply;
				const gchar *err_desc;

				reply = json_node_get_object(root);
				err_desc = json_object_get_string_member(reply, "error_description");
				if (err_desc != NULL) {
					g_set_error(error, LIBBALSA_OAUTH2_ERROR_QUARK, -1, "%s: %s", prefix, err_desc);
					assigned = TRUE;
				}
			}
			g_object_unref(parser);
		}
	}
	if (!assigned) {
		g_set_error(error, LIBBALSA_OAUTH2_ERROR_QUARK, message->status_code, "%s: %u: %s", prefix, message->status_code,
			message->reason_phrase);
	}
}


/* == OAuth2 authorisation related functions ==================================================================================== */

/** @brief OAuth2 authorisation idle callback data */
typedef struct {
    GCond cond;						/**< Condition for signalling that the authorisation has been finished. */
    gboolean done;					/**< Done flag (see oauth2_auth_idle_t::cond) */
    LibBalsaOauth2 *oauth;			/**< OAuth2 context. */
    GtkWindow *parent;				/**< Transient parent window for the authorisation dialogue. */
    gboolean result;				/**< TRUE if the authorisation process has been successful, FALSE if not. */
    GError **error;					/**< Error location for any error if the authorisation process failed. */
} oauth2_auth_idle_t;


/** @brief Authorise Balsa for OAuth2
 *
 * @param[in] oauth OAuth2 context
 * @param[in] parent transient parent of the authorisation dialogue
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE if any error occurred
 *
 * Authorise Balsa for the passed OAuth2 context.  If this function is not called from the main thread, oauth2_authorise_idle() is
 * scheduled as idle callback to perform the user interaction and authorication process.
 */
static gboolean
oauth2_authorise(LibBalsaOauth2 *oauth, GtkWindow *parent, GError **error)
{
	gboolean result;

    if (libbalsa_am_i_subthread()) {
    	static GMutex oauth2_auth_lock;
    	oauth2_auth_idle_t idle_data;

    	g_mutex_lock(&oauth2_auth_lock);
    	g_cond_init(&idle_data.cond);
    	idle_data.oauth = g_object_ref(oauth);
    	idle_data.parent = parent;
    	idle_data.error = error;
    	idle_data.done = FALSE;
    	g_idle_add(oauth2_authorise_idle, &idle_data);
    	while (!idle_data.done) {
    		g_cond_wait(&idle_data.cond, &oauth2_auth_lock);
    	}
    	g_cond_clear(&idle_data.cond);
    	g_mutex_unlock(&oauth2_auth_lock);
    	g_object_unref(oauth);
    	result = idle_data.result;
    } else {
    	result = oauth2_authorise_real(oauth, parent, error);
    }
    return result;
}


/** @brief OAuth2 authorisation idle callback
 *
 * @param data OAuth2 authorisation idle callback data, cast'ed to @ref oauth2_auth_idle_t *
 * @return always FALSE
 *
 * Simply run oauth2_authorise_real(), set oauth2_auth_idle_t::done and signal the condition oauth2_auth_idle_t::cond.
 */
static gboolean
oauth2_authorise_idle(gpointer data)
{
	oauth2_auth_idle_t *idle_data = (oauth2_auth_idle_t *) data;

	idle_data->result = oauth2_authorise_real(idle_data->oauth, idle_data->parent, idle_data->error);
	idle_data->done = TRUE;
    g_cond_signal(&idle_data->cond);
	return FALSE;
}


/** \brief Perform the OAuth2 authorisation process for Balsa
 *
 * @param[in] oauth OAuth2 context
 * @param[in] parent transient parent of the authorisation dialogue
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE if any error occurred
 *
 * Create an authorisation context, and run the OAuth2 dialogue, guiding the user through the actual authorisation process.
 */
static gboolean
oauth2_authorise_real(LibBalsaOauth2 *oauth, GtkWindow *parent, GError **error)
{
	oauth2_auth_ctx_t *auth_ctx;
	gboolean result = FALSE;

	auth_ctx = oauth2_authorize_init(oauth->provider, error);
	if (auth_ctx != NULL) {
		auth_ctx->oauth = oauth;
		result = run_oauth2_dialog(auth_ctx, oauth->provider, oauth->account, parent);
		if (auth_ctx->server != NULL) {
			g_object_unref(auth_ctx->server);
		}
		g_free(auth_ctx->listen_uri);
		g_free(auth_ctx->auth_request_uri);
		if (auth_ctx->auth_err != NULL) {
			g_propagate_error(error, auth_ctx->auth_err);
		}
		g_free(auth_ctx);
	}

	return result;
}


/** @brief Initialise a OAuth2 authorisation context
 *
 * @param[in] provider provider data
 * @param[out] error location for error information may be NULL
 * @return an initialised OAuth2 authorisation context on success, NULL on error
 *
 * Create a new OAuth2 authorisation context, and fill in:
 * - oauth2_auth_ctx_t::server with a newly created listening soup server, unless oauth2_provider_t::oob_mode is set;
 * - oauth2_auth_ctx_t::listen_uri
 * - oauth2_auth_ctx_t::auth_request_uri
 */
static oauth2_auth_ctx_t *
oauth2_authorize_init(const oauth2_provider_t *provider, GError **error)
{
	oauth2_auth_ctx_t *auth_ctx;

	g_return_val_if_fail(provider != NULL, NULL);

	auth_ctx = g_new0(oauth2_auth_ctx_t, 1U);

	/* create the local listener unless the provider supports OOB mode only */
	if (!provider->oob_mode) {
		auth_ctx->server = soup_server_new(SOUP_SERVER_SERVER_HEADER, PACKAGE "/" BALSA_VERSION " ", NULL);
		soup_server_add_handler(auth_ctx->server, NULL, oauth2_listener_cb, auth_ctx, NULL);
		if (!soup_server_listen_local(auth_ctx->server, 0U, SOUP_SERVER_LISTEN_IPV4_ONLY, error)) {
			g_object_unref(auth_ctx->server);
			g_free(auth_ctx);
			auth_ctx = NULL;
		} else {
			GSList *uris;
			SoupURI *uri;

			/* get the local listener uri */
			uris = soup_server_get_uris(auth_ctx->server);
			uri = (SoupURI *) uris->data;
			auth_ctx->listen_uri = g_strdup_printf("http://%s:%u", uri->host, uri->port);
			g_slist_free_full(uris, (GDestroyNotify) soup_uri_free);
			g_debug("%s: listening on %s for auth reply", __func__, auth_ctx->listen_uri);
		}
	} else {
		auth_ctx->listen_uri = g_strdup("oob");
	}

	if (auth_ctx != NULL) {
		SoupURI *uri;

		/* calculate the authentication uri */
		uri = soup_uri_new(provider->auth_uri);
		if (provider->scope == NULL) {
			soup_uri_set_query_from_fields(uri,
				"client_id", provider->client_id,
				"redirect_uri", auth_ctx->listen_uri,
				"response_type", "code",
				NULL);
		} else {
			soup_uri_set_query_from_fields(uri,
				"client_id", provider->client_id,
				"redirect_uri", auth_ctx->listen_uri,
				"scope", provider->scope,
				"response_type", "code",
				NULL);
		}
		auth_ctx->auth_request_uri = soup_uri_to_string(uri, FALSE);
		g_debug("%s: auth request uri %s", __func__, auth_ctx->auth_request_uri);
		soup_uri_free(uri);
	}

	return auth_ctx;
}


/** @brief Local listening server callback
 *
 * @param[in] server server object, unused
 * @param[in] msg received message
 * @param[in] path path component of the received URI
 * @param[in] query query component of the received URI
 * @param[in] client additional context information, unused
 * @param[in] user_data authorisation context, cast'ed to @ref oauth2_auth_ctx_t *
 *
 * Evaluate the authorisation message received from the remote server.  The message @em must be directed to path "/" and contain
 * query data, including a "code" component containing the actual authorisation code.  oauth2_authorize_finish() is called with this
 * code to get the refresh and access tokens.  The status of the passed message is set to 200 (ok) in this case, or to a 4xy error
 * code otherwise.
 */
static void
oauth2_listener_cb(SoupServer G_GNUC_UNUSED *server, SoupMessage *msg, const char *path, GHashTable *query,
	SoupClientContext G_GNUC_UNUSED *client, gpointer user_data)
{
	oauth2_auth_ctx_t *auth_ctx = (oauth2_auth_ctx_t *) user_data;

	if ((strcmp(path, "/") == 0) && (query != NULL) && (auth_ctx->oauth->access_token == NULL) && (auth_ctx->auth_err == NULL)) {
		gconstpointer code;

		code = g_hash_table_lookup(query, "code");
		if (code != NULL) {
			gchar *reply;

			g_debug("%s: got authentication code '%s'", __func__, (const gchar *) code);
			reply = g_strdup_printf(OAUTH_AUTH_DONE_TEMPLATE, _("Received Code"),
				_("The OAuth2 Authorization code for Balsa has been received."), _("You may close this window."));
			soup_message_set_status(msg, 200);
			soup_message_set_response(msg, "text/html", SOUP_MEMORY_TAKE, reply, strlen(reply));
			oauth2_authorize_finish(auth_ctx, (const gchar *) code);
		} else {
			g_debug("%s: got request for '%s', but no 'code' parameter", __func__, path);
			soup_message_set_status(msg, 403);
		}
	} else {
		g_debug("%s: ignore request for '%s'", __func__, path);
		soup_message_set_status(msg, 404);
	}
}


/** @brief Finish authorisation and receive refresh and access tokens
 *
 * @param[in] auth_ctx authorisation context
 * @param[in] auth_code authorisation code
 *
 * Post the authorisation code to the provider's authorisation uri, and receive the JSON reply which shall contain the refresh and
 * access tokens.  The reply is parsed by calling eval_json_auth_reply().  Any error is remembered in oauth2_auth_ctx_t::auth_err.
 * Additionally, the function finally stops the spinner oauth2_auth_ctx_t::spinner in the authorisation dialogue, and updates the
 * related label oauth2_auth_ctx_t::spinner_label accordingly.
 */
static void
oauth2_authorize_finish(oauth2_auth_ctx_t *auth_ctx, const gchar *auth_code)
{
	const oauth2_provider_t *provider = auth_ctx->oauth->provider;
	SoupSession *session;
	SoupMessage *message;
	guint status;
	gboolean success = FALSE;
	GtkWidget *icon;

	gtk_label_set_label(GTK_LABEL(auth_ctx->spinner_label), _("sending authorization code…"));
	g_debug("%s: uri=%s, client_id=%s, client_secret=%s code=%s listen_uri=%s", __func__, provider->token_uri, provider->client_id,
		provider->client_secret, auth_code, auth_ctx->listen_uri);

	/* send the authorisation code */
	session = oauth_soup_session_new();
	if (provider->client_secret != NULL) {
		message = soup_form_request_new("POST", provider->token_uri,
			"grant_type", "authorization_code",
			"client_id", provider->client_id,
			"client_secret", provider->client_secret,
			"code", auth_code,
			"redirect_uri", auth_ctx->listen_uri,
			NULL);
	} else {
		message = soup_form_request_new("POST", provider->token_uri,
			"grant_type", "authorization_code",
			"client_id", provider->client_id,
			"code", auth_code,
			"redirect_uri", auth_ctx->listen_uri,
			NULL);
	}
	status = soup_session_send_message(session, message);
	g_debug("%s: status=%u, code=%u, reason=%s", __func__, status, message->status_code, message->reason_phrase);

	gtk_spinner_stop(GTK_SPINNER(auth_ctx->spinner));
	if ((status != SOUP_STATUS_OK) || (message->status_code != SOUP_STATUS_OK) || (message->response_body == NULL)) {
		eval_json_error(_("OAuth2 authorization request failed"), message, &auth_ctx->auth_err);
		gtk_label_set_label(GTK_LABEL(auth_ctx->spinner_label), _("send error"));
	} else {
		if (eval_json_auth_reply(auth_ctx->oauth, message, TRUE, &auth_ctx->auth_err)) {
			gtk_label_set_label(GTK_LABEL(auth_ctx->spinner_label), _("authorization successful"));
			gtk_dialog_set_response_sensitive(GTK_DIALOG(auth_ctx->auth_dialog), GTK_RESPONSE_ACCEPT, TRUE);
			success = TRUE;
		} else {
			gtk_label_set_label(GTK_LABEL(auth_ctx->spinner_label), _("authorization failed"));
		}
	}
	icon = gtk_image_new_from_icon_name(success ? "gtk-yes" : "gtk-no", GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start(GTK_BOX(gtk_widget_get_parent(auth_ctx->spinner)), icon, FALSE, FALSE, 0U);
	gtk_widget_hide(auth_ctx->spinner);
	gtk_widget_destroy(auth_ctx->spinner);
	gtk_widget_show(icon);
	g_object_unref(message);
	g_object_unref(session);
}


/** @brief Authorisation code entry change callback
 *
 * @param entry authorisation code entry
 * @param button related button, enabled iff the entry is not empty
 */
static void
on_code_entry_changed(GtkEntry *entry, GtkWidget *button)
{
	gtk_widget_set_sensitive(button, strlen(gtk_entry_get_text(entry)) > 0UL);
}


/** @brief Authorisation code entry accept button callback
 *
 * @param button source button, unused
 * @param auth_ctx authorisation context
 *
 * Call oauth2_authorize_finish() with the code entered by the user to finish the authorisation process.
 */
static void
on_code_button_clicked(GtkButton G_GNUC_UNUSED *button, oauth2_auth_ctx_t *auth_ctx)
{
	oauth2_authorize_finish(auth_ctx, gtk_entry_get_text(GTK_ENTRY(auth_ctx->code_entry)));
}


/** @brief Run the OAuth2 authorisation dialogue
 *
 * @param auth_ctx authorisation context
 * @param provider provider data
 * @param account user account (email address)
 * @param parent transient parent window
 * @return TRUE on success, FALSE if the user cancelled the process or if any error occurred
 *
 * Create and run the OAuth2 authorisation dialogue, guiding the user through the process.
 */
static gboolean
run_oauth2_dialog(oauth2_auth_ctx_t *auth_ctx, const oauth2_provider_t *provider, const gchar *account, GtkWindow *parent)
{
	GtkWidget *content;
	GtkWidget *widget;
	GtkWidget *hbox;
	gchar *message;
	gint result;

	auth_ctx->auth_dialog = gtk_dialog_new_with_buttons(_("Authorize Balsa"), parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
		_("Cancel"), GTK_RESPONSE_REJECT, _("OK"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(auth_ctx->auth_dialog), GTK_RESPONSE_ACCEPT, FALSE);

	content = gtk_dialog_get_content_area(GTK_DIALOG(auth_ctx->auth_dialog));
	gtk_container_set_border_width(GTK_CONTAINER(content), 12U);
	gtk_box_set_spacing(GTK_BOX(content), 6U);
	if (provider->oob_mode) {
		message = g_strdup_printf(_("Balsa must be authorized to access %s:\n"
			 	 	 	 	 	 	"\342\200\242 click the link to open the authorization page\n"
			 	 	 	 	 	 	"\342\200\242 log in as user %s\n"
			 	 	 	 	 	 	"\342\200\242 follow the instructions on the web page and\n"
			 	 	 	 	 	 	"\342\200\242 enter the authorization code."),
			provider->display_name,	account);
	} else {
		message = g_strdup_printf(_("Balsa must be authorized to access %s:\n"
			 	 	 	 	 	    "\342\200\242 click the link to open the authorization page\n"
			 	 	 	 	 	 	"\342\200\242 log in as user %s and\n"
			 	 	 	 	 	 	"\342\200\242 follow the instructions on the web page."),
			provider->display_name, account);
	}
	widget = gtk_label_new(message);
	g_free(message);
	gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
	gtk_label_set_xalign(GTK_LABEL(widget), 0.0);
	gtk_box_pack_start(GTK_BOX(content), widget, FALSE, FALSE, 0U);

	message = g_strdup_printf(_("Authorize Balsa for %s"), provider->display_name);
	widget = gtk_link_button_new_with_label(auth_ctx->auth_request_uri, message);
	g_free(message);
	gtk_widget_grab_focus(widget);
	gtk_box_pack_start(GTK_BOX(content), widget, FALSE, FALSE, 0U);

	if (provider->oob_mode) {
		GtkWidget *button;

		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 0U);
		gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("Code:")), FALSE, FALSE, 0U);
		auth_ctx->code_entry = gtk_entry_new();
		gtk_box_pack_start(GTK_BOX(hbox), auth_ctx->code_entry, TRUE, TRUE, 0U);
		button = gtk_button_new_from_icon_name("gtk-ok", GTK_ICON_SIZE_BUTTON);
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0U);
		g_signal_connect(button, "clicked", G_CALLBACK(on_code_button_clicked), auth_ctx);
		gtk_widget_set_sensitive(button, FALSE);
		g_signal_connect(auth_ctx->code_entry, "changed", G_CALLBACK(on_code_entry_changed), button);
	} else {
		auth_ctx->code_entry = NULL;
	}

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 0U);
	auth_ctx->spinner = gtk_spinner_new();
	gtk_spinner_start(GTK_SPINNER(auth_ctx->spinner));
	gtk_box_pack_start(GTK_BOX(hbox), auth_ctx->spinner, FALSE, FALSE, 0U);
	auth_ctx->spinner_label = gtk_label_new(_("…waiting for authorization code"));
	gtk_box_pack_end(GTK_BOX(hbox), auth_ctx->spinner_label, TRUE, TRUE, 0U);

	gtk_widget_show_all(auth_ctx->auth_dialog);
	result = gtk_dialog_run(GTK_DIALOG(auth_ctx->auth_dialog));
	gtk_widget_destroy(auth_ctx->auth_dialog);

	return (result == GTK_RESPONSE_ACCEPT);
}


/** @brief Create a Soup session with optional debugging
 *
 * @return a new SoupSession
 *
 * The soup session includes a Soup logger, dumping the full body, iff the environment variable @c G_MESSAGES_DEBUG contains either
 * @c all or <c>oauth</c>.  Note that this logger object is never freed, i.e. running Balsa with the aforementioned configuration
 * will produce a (harmless) leak.
 */
static SoupSession *
oauth_soup_session_new(void)
{
	static gsize env_checked = 0UL;
	static SoupLogger *logger = NULL;
	SoupSession *session;

	/* check the environment once if we should add the soup logger (note: GLib 2.68 adds g_log_writer_default_would_drop() which
	 * might be used instead of the following code...) */
	if (g_once_init_enter(&env_checked)) {
		const gchar *log_domains;

		log_domains = g_getenv("G_MESSAGES_DEBUG");
		if (log_domains != NULL) {
			gchar **domlist;
			guint n;

			domlist = g_strsplit(log_domains, " ", -1);
			for (n = 0U; (logger == NULL) && (domlist[n] != NULL); n++) {
				if ((strcmp(domlist[n], "all") == 0) || (strcmp(domlist[n], G_LOG_DOMAIN) == 0)) {
					logger = soup_logger_new(SOUP_LOGGER_LOG_BODY, -1);
					g_debug("%s: create Soup logger", __func__);
				}
			}
			g_strfreev(domlist);
		}
		g_once_init_leave(&env_checked, 1UL);
	}

	/* create the new session */
	session = soup_session_new();
	g_object_set(session, "user-agent", PACKAGE "/" BALSA_VERSION " ", NULL);
	if (logger != NULL) {
		soup_session_add_feature(session, SOUP_SESSION_FEATURE(logger));
	}

	return session;
}

#endif	/* defined(HAVE_OAUTH2) */
