/*
 * tests.c
 *
 *  Created on: 07.01.2017
 *      Author: albrecht
 */

#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <sput.h>
#include "net-client.h"
#include "net-client-smtp.h"
#include "net-client-utils.h"


static void test_basic(void);
static void test_basic_crypt(void);
static void test_smtp(void);
static void test_utils(void);


int
main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv)
{
	sput_start_testing();

	sput_enter_suite("test basic (plain)");
	sput_run_test(test_basic);

	sput_enter_suite("test basic (encrypted)");
	sput_run_test(test_basic_crypt);

	sput_enter_suite("test SMTP");
	sput_run_test(test_smtp);

	//sput_enter_suite("test POP3");
	//sput_run_test(test_pop3);

	sput_enter_suite("test utility functions");
	sput_run_test(test_utils);

	sput_finish_testing();

	return sput_get_return_value();
}


static void
test_basic(void)
{
	static gchar *nc_args[] = { NCAT, "-l", "65000", "--exec", SED " -u -e s/x/ThisIsLong/g", NULL };
	NetClient *basic;
	GPid child;
	GError *error = NULL;
	gboolean op_res;
	gchar *read_res;

	sput_fail_unless(net_client_new(NULL, 65000, 42) == NULL, "missing host");
	sput_fail_unless(net_client_new("localhost", 65000, 0) == NULL, "zero max line length");

	sput_fail_unless((basic = net_client_new("localhost", 65000, 42)) != NULL, "localhost; port 65000");
	sput_fail_unless(net_client_get_host(NULL) == NULL, "get host w/o client");
	sput_fail_unless(strcmp(net_client_get_host(basic), "localhost") == 0, "read host ok");
	sput_fail_unless(net_client_connect(basic, NULL) == FALSE, "connect failed");
	g_object_unref(basic);

	sput_fail_unless((basic = net_client_new("www.google.com", 80, 1)) != NULL, "www.google.com:80; port 0");
	sput_fail_unless(net_client_configure(NULL, "localhost", 65000, 42, NULL) == FALSE, "configure w/o client");
	sput_fail_unless(net_client_configure(basic, NULL, 65000, 42, NULL) == FALSE, "configure w/o host");
	sput_fail_unless(net_client_configure(basic, "localhost", 65000, 0, NULL) == FALSE, "configure w/ zero max line length");
	sput_fail_unless(net_client_configure(basic, "localhost", 65000, 42, NULL) == TRUE, "configure localhost:65000 ok");

	sput_fail_unless(net_client_set_timeout(NULL, 3) == FALSE, "set timeout w/o client");
	sput_fail_unless(net_client_set_timeout(basic, 10) == TRUE, "set timeout");

	op_res =  net_client_write_line(basic, "Hi There", &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_NOT_CONNECTED), "write w/o connection");
	g_clear_error(&error);
	op_res =  net_client_read_line(basic, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_NOT_CONNECTED), "read w/o connection");
	g_clear_error(&error);

	op_res =
		g_spawn_async(NULL, nc_args, NULL, G_SPAWN_STDOUT_TO_DEV_NULL + G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &child, &error);
	if (!op_res) {
		g_error("launching %s failed: %s", nc_args[0], error->message);
		g_assert_not_reached();
	}
	sleep(1);

	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect succeeded");
	op_res = net_client_connect(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_CONNECTED), "cannot connect already connected");
	g_clear_error(&error);
	op_res = net_client_configure(basic, "localhost", 65000, 42, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_CONNECTED), "cannot configure already connected");
	g_clear_error(&error);

	sput_fail_unless(net_client_write_buffer(NULL, "xxx", 3U, NULL) == FALSE, "write buffer w/o client");
	sput_fail_unless(net_client_write_buffer(basic, NULL, 3U, NULL) == FALSE, "write buffer w/o buffer");
	sput_fail_unless(net_client_write_buffer(basic, "xxx", 0U, NULL) == FALSE, "write buffer w/o count");

	sput_fail_unless(net_client_write_line(NULL, "%100s", NULL, "x") == FALSE, "write line w/o client");
	sput_fail_unless(net_client_write_line(basic, NULL, NULL) == FALSE, "write line w/o format string");
	op_res = net_client_write_line(basic, "%100s", &error, "x");
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_LINE_TOO_LONG), "write w/ line too long");
	g_clear_error(&error);
	sput_fail_unless(net_client_write_line(basic, "%s", NULL, "x") == TRUE, "write ok");

	sput_fail_unless(net_client_read_line(NULL, NULL, NULL) == FALSE, "read w/o client");
	sput_fail_unless(net_client_read_line(basic, NULL, NULL) == TRUE, "read, data discarded");
	op_res = net_client_read_line(basic, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == G_IO_ERROR_TIMED_OUT), "read timeout");
	g_message("%s %d %s", g_quark_to_string(error->domain), error->code, error->message);
	g_clear_error(&error);

	sput_fail_unless(net_client_execute(NULL, NULL, "Hi There", NULL) == FALSE, "execute w/o client");
	sput_fail_unless(net_client_execute(basic, NULL, NULL, NULL) == FALSE, "execute w/o format string");
	sput_fail_unless(net_client_execute(basic, NULL, "%100s", NULL, "x") == FALSE, "execute w/ xmit line too long");
	op_res = net_client_execute(basic, &read_res, "Hi There", NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("Hi There", read_res) == 0), "execute ok");
	g_free(read_res);

	sput_fail_unless(net_client_write_buffer(basic, "H", 1U, NULL) == TRUE, "write buffer part 1");
	sput_fail_unless(net_client_write_buffer(basic, "i Y", 3U, NULL) == TRUE, "write buffer part 2");
	sput_fail_unless(net_client_write_buffer(basic, "ou\r\n", 4U, NULL) == TRUE, "write buffer part 3");
	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("Hi You", read_res) == 0), "read back ok");
	g_free(read_res);

	op_res = net_client_execute(basic, &read_res, "xxxxxxxxxx", &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_LINE_TOO_LONG), "read line too long");
	g_clear_error(&error);

	kill(child, SIGTERM);

	op_res = net_client_read_line(basic, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code = NET_CLIENT_ERROR_CONNECTION_LOST), "read line, client lost");
	g_clear_error(&error);

	g_object_unref(basic);
}


static gboolean
check_cert(NetClient *client, GTlsCertificate *peer_cert, GTlsCertificateFlags errors, gpointer user_data)
{
	gchar *hash;
	GByteArray *cert_der = NULL;

	g_object_get(G_OBJECT(peer_cert), "certificate", &cert_der, NULL);
	hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, cert_der->data, cert_der->len);
	g_byte_array_unref(cert_der);
	g_message("%s(%p, %p, %x, %p) -> fp(sha256) = %s", __func__, client, peer_cert, errors, user_data, hash);
	g_free(hash);
	return TRUE;
}


static gchar *
get_cert_passwd(NetClient *client, const GByteArray *cert_der, gpointer user_data)
{
	g_message("%s(%p, %p, %p)", __func__, client, cert_der, user_data);
	return g_strdup("test-server");
}


static void
test_basic_crypt(void)
{
	NetClient *basic;
	gboolean op_res;
	GError *error = NULL;

	/* tests without client cert check */
	sput_fail_unless((basic = net_client_new("localhost", 65001, 42)) != NULL, "localhost; port 65001");
	sput_fail_unless(net_client_is_encrypted(NULL) == FALSE, "NULL client is unencrypted");
	sput_fail_unless(net_client_is_encrypted(basic) == FALSE, "unconnected client is unencrypted");
	sput_fail_unless(net_client_start_tls(NULL, NULL) == FALSE, "start tls: no client");
	op_res = net_client_start_tls(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_NOT_CONNECTED), "start tls: not connected");
	g_clear_error(&error);
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_is_encrypted(basic) == FALSE, "still unencrypted");
	op_res = net_client_start_tls(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error != NULL), "start tls: bad server cert");
	g_clear_error(&error);
	g_object_unref(basic);

	sput_fail_unless((basic = net_client_new("localhost", 65001, 42)) != NULL, "localhost; port 65001");
	g_signal_connect(G_OBJECT(basic), "cert-check", G_CALLBACK(check_cert), NULL);
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_start_tls(basic, NULL) == TRUE, "start tls: success");
	sput_fail_unless(net_client_is_encrypted(basic) == TRUE, "is encrypted");
	op_res = net_client_start_tls(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_TLS_ACTIVE), "start tls: already started");
	g_clear_error(&error);
	g_object_unref(basic);

	/* tests with client cert check */
	sput_fail_unless((basic = net_client_new("localhost", 65002, 42)) != NULL, "localhost; port 65002");
	g_signal_connect(G_OBJECT(basic), "cert-check", G_CALLBACK(check_cert), NULL);
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_start_tls(basic, NULL) == FALSE, "start tls: fails");
	sput_fail_unless(net_client_is_encrypted(basic) == FALSE, "not encrypted");
	g_object_unref(basic);

	sput_fail_unless((basic = net_client_new("localhost", 65002, 42)) != NULL, "localhost; port 65002");
	g_signal_connect(G_OBJECT(basic), "cert-check", G_CALLBACK(check_cert), NULL);
	sput_fail_unless(net_client_set_cert_from_file(NULL, "cert_u.pem", NULL) == FALSE, "load cert file w/o client");
	sput_fail_unless(net_client_set_cert_from_file(basic, NULL, NULL) == FALSE, "load cert file w/o file");
	sput_fail_unless(net_client_set_cert_from_file(basic, "no_such_file.crt", NULL) == FALSE, "load cert file, file missing");
	sput_fail_unless(net_client_set_cert_from_pem(NULL, "This is no cert", NULL) == FALSE, "load cert buffer w/o client");
	sput_fail_unless(net_client_set_cert_from_pem(basic, NULL, NULL) == FALSE, "load cert buffer w/o buffer");
	op_res = net_client_set_cert_from_pem(basic, "This is no cert", &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_GNUTLS), "load cert buffer w/ broken pem data");
	g_clear_error(&error);
	sput_fail_unless(net_client_set_cert_from_file(basic, "cert_u.pem", NULL) == TRUE, "load cert file w/ plain key ok");
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_start_tls(basic, NULL) == TRUE, "start tls: ok");
	sput_fail_unless(net_client_is_encrypted(basic) == TRUE, "encrypted");
	g_object_unref(basic);

	sput_fail_unless((basic = net_client_new("localhost", 65002, 42)) != NULL, "localhost; port 65002");
	g_signal_connect(G_OBJECT(basic), "cert-check", G_CALLBACK(check_cert), NULL);
	sput_fail_unless(net_client_set_cert_from_file(basic, "cert_u.pem", NULL) == TRUE, "load cert file w/ plain key ok");
	sput_fail_unless(net_client_set_cert_from_file(basic, "cert.pem", NULL) == FALSE, "load cert file w/ crypt key fails");
	g_signal_connect(G_OBJECT(basic), "cert-pass", G_CALLBACK(get_cert_passwd), NULL);
	sput_fail_unless(net_client_set_cert_from_file(basic, "cert.pem", NULL) == TRUE, "load cert file w/ crypt key ok");
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_start_tls(basic, NULL) == TRUE, "start tls: ok");
	sput_fail_unless(net_client_is_encrypted(basic) == TRUE, "encrypted");
	g_object_unref(basic);
}


typedef struct {
	gchar *msg_text;
	gchar *read_ptr;
} msg_data_t;


static gssize
msg_data_cb(gchar *buffer, gsize count, gpointer user_data, GError **error)
{
	msg_data_t *msg_data = (msg_data_t *) user_data;
	size_t msg_len;
	gssize result;

	g_message("%s(%p, %lu, %p, %p)", __func__, buffer, count, user_data, error);
	msg_len = strlen(msg_data->read_ptr);
	if (msg_len > 0) {
		if (msg_len > count) {
			result = count;
		} else {
			result = msg_len;
		}
		memcpy(buffer, msg_data->read_ptr, result);
		msg_data->read_ptr = &msg_data->read_ptr[result];
	} else {
		result = 0;
	}
	g_message("%s: return %ld", __func__, result);
	return result;
}


#define MSG_TEXT										\
	"From: \"Sender Name\" <me@here.com>\r\n" 			\
	"To: \"Recipient Name\" <you@there.com>\r\n" 		\
	"Cc: \"Other Recipient\" <other@there.com>\r\n" 	\
	"Subject: This is just a test message\r\n" 			\
	"Date: Fri, 13 Jan 2017 21:27:11 +0100\r\n" 		\
	"Message-ID: <20170113212711.19833@here.com>\r\n" 	\
	"\r\n" 												\
	"This is the body of the test message.\r\n"


static gchar **
get_auth(NetClient *client, gpointer user_data)
{
	gchar ** result;

	g_message("%s(%p, %p)", __func__, client, user_data);
	result = g_new0(gchar *, 3U);
	result[0] = g_strdup("john.doe");
	result[1] = g_strdup("@ C0mplex P@sswd");
	return result;
}


static void
test_smtp(void)
{
	msg_data_t msg_buf;
	NetClientSmtp *smtp;
	NetClientSmtpMessage *msg;
	GError *error = NULL;
	gboolean op_res;
	gchar *read_res;

	// message creation
	msg_buf.msg_text = msg_buf.read_ptr = MSG_TEXT;
	sput_fail_unless(net_client_smtp_msg_new(NULL, NULL) == NULL, "create msg: no callback");
	sput_fail_unless((msg = net_client_smtp_msg_new(msg_data_cb, &msg_buf)) != NULL, "create msg: ok");

	sput_fail_unless(net_client_smtp_msg_set_sender(NULL, "some@sender.com") == FALSE, "set sender, no message");
	sput_fail_unless(net_client_smtp_msg_set_sender(msg, NULL) == FALSE, "set sender, no address");

	sput_fail_unless(net_client_smtp_msg_add_recipient(NULL, "you@there.com", NET_CLIENT_SMTP_DSN_NEVER)  == FALSE,
		"add recipient, no message");
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, NULL, NET_CLIENT_SMTP_DSN_NEVER)  == FALSE,
		"add recipient, no address");

	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(NULL, NULL, FALSE) == FALSE, "set dsn opts, no message");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, NULL, FALSE) == TRUE, "set dsn opts ok");


	// smtp stuff - test various failures
	sput_fail_unless(net_client_smtp_new(NULL, 0, NET_CLIENT_CRYPT_NONE) == NULL, "missing host");

	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65000, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost; port 65000");
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == FALSE, "no server");
	g_object_unref(smtp);

	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost:65025");
	op_res = net_client_smtp_connect(smtp, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp(read_res, "mail.inetsim.org INetSim Mail Service ready.") == 0),
		"connect: success");
	g_free(read_res);
	sput_fail_unless(net_client_is_encrypted(NET_CLIENT(smtp)) == FALSE, "not encrypted");
	op_res = net_client_smtp_connect(smtp, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_CONNECTED), "cannot reconnect");
	g_clear_error(&error);
	sput_fail_unless(net_client_smtp_can_dsn(NULL) == FALSE, "NULL client: no dsn");
	sput_fail_unless(net_client_smtp_can_dsn(smtp) == TRUE, "inetsim: can dsn");

	sput_fail_unless(net_client_smtp_send_msg(NULL, msg, NULL) == FALSE, "send msg, NULL client");
	sput_fail_unless(net_client_smtp_send_msg(smtp, NULL, NULL) == FALSE, "send msg, NULL message");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == FALSE, "send msg: error, no sender");
	sput_fail_unless(net_client_smtp_msg_set_sender(msg, "some@sender.com") == TRUE, "set sender ok");
	sput_fail_unless(net_client_smtp_msg_set_sender(msg, "me@here.com") == TRUE, "replace sender ok");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == FALSE, "send msg: error, no recipient");
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "you@there.com", NET_CLIENT_SMTP_DSN_NEVER)  == TRUE,
		"add recipient ok (no dsn)");
	op_res = net_client_smtp_send_msg(smtp, msg, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_SMTP_PERMANENT), "send failed: not authenticated");
	g_clear_error(&error);
	g_object_unref(smtp);

	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_STARTTLS)) != NULL,
		"localhost:65025, starttls");
	op_res = net_client_smtp_connect(smtp, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error != NULL), "connect: fails (untrusted cert)");
	g_clear_error(&error);
	g_object_unref(smtp);

	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost:65025");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, FALSE, 0U) == TRUE, "force no auth mech available");
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	op_res = net_client_smtp_connect(smtp, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_SMTP_NO_AUTH), "connect: fails");
	g_clear_error(&error);
	g_object_unref(smtp);

	// unencrypted, PLAIN auth
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost:65025");
	sput_fail_unless(net_client_smtp_allow_auth(NULL, FALSE, NET_CLIENT_SMTP_AUTH_PLAIN) == FALSE, "set auth meths, no client");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, FALSE, NET_CLIENT_SMTP_AUTH_PLAIN) == TRUE, "force auth meth PLAIN");
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// STARTTLS required, LOGIN auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other1@there.com", NET_CLIENT_SMTP_DSN_SUCCESS) == TRUE,
		"add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_STARTTLS)) != NULL,
		"localhost:65025, starttls");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, TRUE, NET_CLIENT_SMTP_AUTH_LOGIN) == TRUE, "force auth meth LOGIN");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// STARTTLS optional, CRAM-MD5 auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other2@there.com", NET_CLIENT_SMTP_DSN_FAILURE) == TRUE,
		"add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_STARTTLS_OPT)) != NULL,
		"localhost:65025, starttls");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, TRUE, NET_CLIENT_SMTP_AUTH_CRAM_MD5) == TRUE, "force auth meth CRAM-MD5");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, "20170113212711.19833@here.com", FALSE) == TRUE, "dsn: envid, headers");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// SSL, CRAM-SHA1 auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other3@there.com", NET_CLIENT_SMTP_DSN_DELAY) == TRUE,
		"add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65465, NET_CLIENT_CRYPT_ENCRYPTED)) != NULL, "localhost:65025, ssl");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, TRUE, NET_CLIENT_SMTP_AUTH_CRAM_SHA1) == TRUE, "force auth meth CRAM-SHA1");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, NULL, TRUE) == TRUE, "dsn: no envid, message");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// SSL, auto select auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other4@there.com",
		NET_CLIENT_SMTP_DSN_SUCCESS + NET_CLIENT_SMTP_DSN_FAILURE + NET_CLIENT_SMTP_DSN_DELAY) == TRUE, "add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65465, NET_CLIENT_CRYPT_ENCRYPTED)) != NULL, "localhost:65025, ssl");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, "20170113212711.19833@here.com", TRUE) == TRUE,
		"dsn: envid, message");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	net_client_smtp_msg_free(NULL);
	net_client_smtp_msg_free(msg);
}


static void
test_utils(void)
{
	gchar *authstr;

	sput_fail_unless(strcmp(net_client_chksum_to_str(G_CHECKSUM_MD5), "MD5") == 0, "checksum string for MD5");
	sput_fail_unless(strcmp(net_client_chksum_to_str(G_CHECKSUM_SHA1), "SHA1") == 0, "checksum string for SHA1");
	sput_fail_unless(strcmp(net_client_chksum_to_str(G_CHECKSUM_SHA256), "SHA256") == 0, "checksum string for SHA256");
	sput_fail_unless(strcmp(net_client_chksum_to_str(G_CHECKSUM_SHA512), "SHA512") == 0, "checksum string for SHA512");
	sput_fail_unless(strcmp(net_client_chksum_to_str((GChecksumType) -1), "_UNKNOWN_") == 0, "checksum string for unknown");

	/* note: test the md5 example from rfc2195, sect. 2; other hashes calculated from
	 *       http://www.freeformatter.com/hmac-generator.html */
#define CRAM_MD5_CHAL	"PDE4OTYuNjk3MTcwOTUyQHBvc3RvZmZpY2UucmVzdG9uLm1jaS5uZXQ+"
#define CRAM_MD5_USER	"tim"
#define CRAM_MD5_PASS	"tanstaaftanstaaf"
#define CRAM_MD5_RES	"dGltIGI5MTNhNjAyYzdlZGE3YTQ5NWI0ZTZlNzMzNGQzODkw"
#define CRAM_SHA1_RES	"dGltIDhjYjEwZWQwYThiNzY0YWIwZTA1MTI1ZGQ2ZWFhMjk1ZjE5YjU5NDM="
#define CRAM_SHA256_RES "dGltIGYwMDExYmJmN2MxNjE4ZWY5MGUzZjE4MTQ4ZTVlZGE0ZTE1NjMxYzljMzhlYmVmMDYyYmY3MTQzZmY3MmU5NDQ="
	sput_fail_unless(net_client_cram_calc(NULL, G_CHECKSUM_MD5, CRAM_MD5_USER, CRAM_MD5_PASS) == NULL, "cram-md5: no challenge");
	sput_fail_unless(net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_MD5, NULL, CRAM_MD5_PASS) == NULL, "cram-md5: no user");
	sput_fail_unless(net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_MD5, CRAM_MD5_USER, NULL) == NULL, "cram-md5: no password");
	authstr = net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_MD5, CRAM_MD5_USER, CRAM_MD5_PASS);
	sput_fail_unless(strcmp(authstr, CRAM_MD5_RES) == 0, "cram-md5: auth string ok");
	g_free(authstr);

	authstr = net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_SHA1, CRAM_MD5_USER, CRAM_MD5_PASS);
	sput_fail_unless(strcmp(authstr, CRAM_SHA1_RES) == 0, "cram-sha1: auth string ok");
	g_free(authstr);

	authstr = net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_SHA256, CRAM_MD5_USER, CRAM_MD5_PASS);
	sput_fail_unless(strcmp(authstr, CRAM_SHA256_RES) == 0, "cram-sha256: auth string ok");
	g_free(authstr);

#define AUTH_PLAIN_RES	"dGltAHRpbQB0YW5zdGFhZnRhbnN0YWFm"
	sput_fail_unless(net_client_auth_plain_calc(NULL, CRAM_MD5_PASS) == NULL, "auth plain: no user");
	sput_fail_unless(net_client_auth_plain_calc(CRAM_MD5_USER, NULL) == NULL, "auth plain: no password");
	authstr = net_client_auth_plain_calc(CRAM_MD5_USER, CRAM_MD5_PASS);
	sput_fail_unless(strcmp(authstr, AUTH_PLAIN_RES) == 0, "auth plain: auth string ok");
	g_free(authstr);
}
