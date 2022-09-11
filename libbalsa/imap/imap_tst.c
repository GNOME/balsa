/* libimap library.
 * Copyright (C) 2003-2016 Pawel Salek.
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
/** @file imap_tst.c tests some IMAP capabilities. It is very useful
    for stress-testing the imap part of balsa. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "libimap.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "util.h"

struct {
  char *user;
  char *password;
  NetClientCryptMode tls_mode;
  gboolean anonymous;
  gboolean compress;
} TestContext = { NULL, NULL, NET_CLIENT_CRYPT_STARTTLS_OPT, FALSE, FALSE };


static gchar*
get_user() {

  if(TestContext.user) {
    printf("Login as user: %s\n", TestContext.user);
    return g_strdup(TestContext.user);
  } else {
    char buf[256];
    size_t len;

    fprintf(stderr, "Login as user: ");
    fflush(stderr);
    if(!fgets(buf, sizeof(buf), stdin))
      return NULL;

    len = strlen(buf);
    if(len>0 && buf[len-1] == '\n')
      buf[len-1] = '\0';

    return g_strdup(buf);
  }
}

static gchar*
get_password()
{
  if(TestContext.password) {
    return g_strdup(TestContext.password);
  } else {
    char buf[255];
    int ttyfd = open ("/dev/tty", O_RDWR);
    int n;
    struct termios tty, oldtty;

    if(write (ttyfd, "Password: ", 10) != 10)
      return NULL;
    tcgetattr(ttyfd, &oldtty);
    tty = oldtty;
    tty.c_lflag &= ~ECHO;
    tcsetattr(ttyfd, TCSANOW, &tty);

    n = read(ttyfd, buf, sizeof(buf)-1);
    tcsetattr(ttyfd, TCSANOW, &oldtty);
    close(ttyfd);

    buf[n] = '\0'; /* Truncate trailing '\n' */
    while(n && !isgraph(buf[n]))
      buf[n--] = '\0';

    return g_strdup(buf);
  }
}

static gchar **
auth_cb(NetClient *client, gboolean need_passwd, gpointer user_data)
{
    gchar **result = NULL;

    result = g_new0(gchar *, 3U);
    result[0] = get_user();
    if (need_passwd) {
    	result[1] = get_password();
    }
    return result;
}

static gboolean
cert_cb(NetClient *client, GTlsCertificate *peer_cert, GTlsCertificateFlags errors, gpointer user_data)
{
	printf("cert error 0x%x, accepted...\n", errors);
	return TRUE;
}

static ImapMboxHandle*
get_handle(const char *host)
{
  ImapMboxHandle *h = imap_mbox_handle_new();

  imap_handle_set_tls_mode(h, TestContext.tls_mode);
  if(TestContext.anonymous)
	  imap_handle_set_auth_mode(h, NET_CLIENT_AUTH_NONE_ANON);

  if(TestContext.compress)
    imap_handle_set_option(h, IMAP_OPT_COMPRESS, TRUE);

  imap_handle_set_authcb(h, G_CALLBACK(auth_cb), NULL);
  imap_handle_set_certcb(h, G_CALLBACK(cert_cb));

  if(imap_mbox_handle_connect(h, host) != IMAP_SUCCESS) {
    g_object_unref(h);
    return NULL;
  }
  return h;
}

/* =================================================================== 
   Test routines.
*/

/** a wrapper around imap_address_new allocating its args. */
#define imap_address_newa(a,b) imap_address_new(g_strdup(a),g_strdup(b))

/** Tests whether Envelope object is serialized and restored
    properly. */
static void
test_envelope_strings()
{
  ImapEnvelope *e1, *e2;
  gchar *e1str;

  e1 = imap_envelope_new();
  e1->date = 1234567;
  e1->subject = g_strdup("Test \"Subject");
  e1->from = imap_address_newa("FROM", "from@example.com");
  e1->sender = imap_address_newa("\"sender bender\"","sender@example.com");
  e1->replyto = imap_address_newa("", "replyto@kth.se");
  e1->to = imap_address_newa(NULL, "to@example.com");
  e1->cc = imap_address_newa("cc", "cc@example.com");
  e1->bcc = NULL;
  e1->in_reply_to = g_strdup("to-some-string");
  e1->message_id = g_strdup("Message\"id");

  e1str = imap_envelope_to_string(e1);
  e2 = imap_envelope_from_string(e1str);
  g_free(e1str);

  printf("DATE %10u %10u\n", (unsigned)e1->date, (unsigned)e2->date);
  printf("SUBJ %s | %s\n", e1->subject, e2->subject);
  printf("FROM %s %s | %s %s\n", e1->from->name, e1->from->addr_spec,
	 e2->from->name, e2->from->addr_spec);
  printf("SEND %s %s | %s %s\n", e1->sender->name, e1->sender->addr_spec,
	 e2->sender->name, e2->sender->addr_spec);
  printf("REPL %s %s | %s %s\n", e1->replyto->name, e1->replyto->addr_spec,
	 e2->replyto->name, e2->replyto->addr_spec);
  printf("TO %s %s | %s %s\n", e1->to->name, e1->to->addr_spec,
	 e2->to->name, e2->to->addr_spec);
  printf("CC %s %s | %s %s\n", e1->cc->name, e1->cc->addr_spec,
	 e2->cc->name, e2->cc->addr_spec);
  printf("BCC %p | %p\n", e1->bcc, e2->bcc);

  printf("INRT %s %s\n", e1->in_reply_to, e2->in_reply_to);
  printf("MSGI %s %s\n", e1->message_id, e2->message_id);
  imap_envelope_free(e1);
  imap_envelope_free(e2);
}

/** Tests whether the ImapBody structure can be serialized and
    restored. */
static gboolean
test_body_strings()
{
  ImapBody *b1, *b2;
  gchar *s;
  b1 = imap_body_new();

  b1->encoding = IMBENC_QUOTED;
  b1->media_basic = IMBMEDIA_TEXT;
  b1->media_basic_name = g_strdup("text");
  b1->media_subtype = g_strdup("plain");

  /* Test b1->params */
  b1->octets = 0xdeed;
  b1->lines = 0x1ea;
  b1->content_id = g_strdup("content_id");
  b1->desc = NULL;
  /* ImapEnvelope *envelope; used only if media/basic == MESSAGE */
  b1->content_dsp = IMBDISP_INLINE;
  b1->dsp_params =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_insert(b1->dsp_params, g_strdup("display"),
                      g_strdup("immediately"));
  b1->content_dsp_other = g_strdup("content_dsp_other");
  b1->content_uri = g_strdup("URI");

  b1->ext.onepart.md5 = g_strdup("MD5 checksum");
  b1->child = NULL;
  b1->next = NULL;

  s = imap_body_to_string(b1);
  imap_body_free(b1);
  printf("BODY1: %s\n", s);
  b2 = imap_body_from_string(s);
  g_free(s);
  s = imap_body_to_string(b2);
  printf("BODY2: %s\n", s);
  imap_body_free(b2);
  g_free(s);

  return TRUE;
}

/** Tests whether all messages from a test mailbox can be extracted
    from a mailbox. */
static int
dump_mbox(const char *host, const char *mailbox,
	  ImapFetchBodyCb cb, void *cb_data)
{
  gboolean read_only;
  ImapMboxHandle *h;

  h = get_handle(host);
  if(!h) {
    fprintf(stderr, "Connection to %s failed.\n", host);
    return 1;
  }

  if(imap_mbox_select(h, mailbox, &read_only) == IMR_OK) {
    unsigned cnt =  imap_mbox_handle_get_exists(h);
    unsigned i;
#define FETCH_AT_ONCE 300
    for(i=0; i<cnt; i+= FETCH_AT_ONCE) {
      unsigned arr[FETCH_AT_ONCE], j;
      unsigned batch_length = i+FETCH_AT_ONCE > cnt ? cnt-i : FETCH_AT_ONCE;
      printf("Fetching %u:%u\n", i+1, i+batch_length);
      for(j=0; j<batch_length; j++) arr[j] = i+1+j;
      if( imap_mbox_handle_fetch_rfc822(h, batch_length, arr, TRUE,
					cb, cb_data) != IMR_OK) {
	fprintf(stderr, "Fetching %u:%u failed: %s\n", i+1,
		i+batch_length,
		imap_mbox_handle_get_last_msg(h));
	break;
      }
    }
  }
  g_object_unref(h);
  return 0;
}

struct DumpfileState {
  FILE *fl;
  int error;
};

static void
dumpfile_cb(unsigned seqno, const char *buf, size_t buflen, void *arg)
{
  struct DumpfileState *dfs = (struct DumpfileState*)arg;
  static const char header[] =
    "From addr@example.com Thu Oct 18 00:50:45 2007\r\n";
  
  if(fwrite(header, 1, sizeof(header)-1, dfs->fl) != sizeof(header)-1 ||
     fwrite(buf, 1, buflen, dfs->fl) != buflen) {
    if(!dfs->error) {
      fprintf(stderr, "Cannot write\n");
      dfs->error = 1;
    }
  }
}

static int
test_mbox_dumpfile(int argc, char *argv[])
{
  struct DumpfileState state = { NULL, 0 };
  int res = 1;

  if(argc<2) {
    fprintf(stderr, "dump HOST MAILBOX\n");
    return 1;
  }
  state.fl = fopen("dump.mbox", "at");
  if(!state.fl) {
    fprintf(stderr, "Cannot open dump.mbox for writing\n");
  } else {
    res= dump_mbox(argv[0], argv[1], dumpfile_cb, &state);
    fclose(state.fl);
  }
  return res;
}

struct DumpdirState {
  const char *dst_directory;
  int error;
  unsigned last_seqno;
};

static void
dumpdir_cb(unsigned seqno, const char *buf, size_t buflen, void *arg)
{
  struct DumpdirState *dds = (struct DumpdirState*)arg;
  FILE *f;
  char num[16];
  char *fname;

  g_snprintf(num, sizeof(num), "%u", seqno);
  fname = g_build_filename(dds->dst_directory, num, NULL);
  f = fopen(fname, seqno == dds->last_seqno ? "at" : "wt");
  dds->last_seqno = seqno;
  if(!f) {
    fprintf(stderr, "Cannot open %s for writing.\n", fname);
    dds->error = 1;
  } else {
    if( fwrite(buf, 1, buflen, f) != buflen) {
      if(!dds->error) {
	fprintf(stderr, "Cannot write %lu bytes to %s.\n",
		(unsigned long)buflen, fname);
	dds->error = 1;
      }
    }
    fclose(f);
  }
  g_free(fname);
}

/** Tests whether specified mailbox can be dumped to a directory. */
static int
test_mbox_dumpdir(int argc, char *argv[])
{
  struct DumpdirState state;
  struct stat buf;
  if(argc<3) {
    fprintf(stderr, "dumpdir HOST MAILBOX DST_DIRECTORY\n");
    return 1;
  }
  state.dst_directory = argv[2];
  if(stat(state.dst_directory, &buf) != 0) {
    fprintf(stderr, "Cannot stat %s\n", state.dst_directory);
    return 1;
  }
  if(!S_ISDIR(buf.st_mode)) {
    fprintf(stderr, "%s is not a directory\n", state.dst_directory);
    return 1;
  }
  state.last_seqno = 0;
  return dump_mbox(argv[0], argv[1], dumpdir_cb, &state);
}

struct MsgIterator {
  const char *src_dir;
  DIR *dir;
  FILE *fh;
};

static size_t
msg_iterator(char* buffer, size_t buffer_size, ImapAppendMultiStage stage,
	     ImapMsgFlags *flags, void *arg)
{
  struct MsgIterator *mi = (struct MsgIterator*)arg;
  struct stat buf;
  size_t msg_size;
  struct dirent *file;

  switch(stage) {
  case IMA_STAGE_NEW_MSG:
    *flags = 0;
    if(mi->fh) {
      fclose(mi->fh);
      mi->fh = NULL;
    }
    
    for(msg_size = 0;
	msg_size == 0 &&
	(file = readdir(mi->dir)) != NULL;) {
      gchar *file_name = g_build_filename(mi->src_dir, file->d_name, NULL);

      if(stat(file_name, &buf) == 0) {
	if(S_ISREG(buf.st_mode)) {
	  mi->fh = fopen(file_name, "rb");
	  
	  if(mi->fh) /* Ready to read! */
	    msg_size = buf.st_size;
	}
      }
      g_free(file_name);
    }
    return msg_size;
    break;

  case IMA_STAGE_PASS_DATA:
    return fread(buffer, 1, buffer_size, mi->fh);
  }

  g_assert_not_reached();
  return 0;
}
    

static size_t
pass_file(char* buffer, size_t buffer_size, void* arg)
{
  FILE *f = (FILE*)arg;
  return fread(buffer, 1, buffer_size, f);
}

/** Tests whether specified set of messages can be appended to given
    mailbox.
 */
static int
test_mbox_append_common(gboolean multi, int argc, char *argv[])
{
  const char *host, *mailbox, *src_dir;
  ImapMboxHandle *h;
  DIR *dir;
  struct dirent *file;
  ImapResponse res;

  if(argc<3) {
    fprintf(stderr, "dumpdir HOST MAILBOX SRC_DIRECTORY\n");
    return 1;
  }
  host = argv[0];
  mailbox = argv[1];
  src_dir = argv[2];

  dir = opendir(src_dir);
  if(!dir) {
    fprintf(stderr, "Cannot open directory %s for reading: %s\n", src_dir,
	    strerror(errno));
    return 1;
  }

  h = get_handle(host);
  if(!h) {
    fprintf(stderr, "Connection to %s failed.\n", host);
    return 1;
  }

  imap_mbox_create(h, mailbox); /* Ignore the result. */
  
  /* Now, keep appending... */
  if(multi) {
    struct MsgIterator mi;

    mi.src_dir = src_dir;
    mi.dir = dir;
    mi.fh = NULL;
    res = imap_mbox_append_multi(h, mailbox, msg_iterator, &mi, NULL);
  } else {
    for(res = IMR_OK; res == IMR_OK && (file = readdir(dir)) != NULL;) {
      struct stat buf;
      char *file_name = g_build_filename(src_dir, file->d_name, NULL);

      if(stat(file_name, &buf) == 0) {
	if(S_ISREG(buf.st_mode)) {
	  FILE *fh = fopen(file_name, "rb");

	  if(fh) {
	    res = imap_mbox_append(h, mailbox, 0, buf.st_size, pass_file, fh);
	    fclose(fh);
	  } else 
	    printf("Cannot open %s for reading: %s\n", file_name,
		   strerror(errno));
	}
      } else
	printf("Cannot stat %s: %s\n", file_name, strerror(errno));

      g_free(file_name);
    }
    closedir(dir);
  }

  g_object_unref(h);
  return res == IMR_OK ? 1 : 0;
}

/** Tests appending message by message. */
static int
test_mbox_append(int argc, char *argv[])
{
  return test_mbox_append_common(FALSE, argc, argv);
}

/** Tests appending using the MULTIAPPEND interface. */
static int
test_mbox_append_multi(int argc, char *argv[])
{
  return test_mbox_append_common(TRUE, argc, argv);
}

/** Tests appending using the MULTIAPPEND interface. */
static int
test_mbox_delete(int argc, char *argv[])
{
  ImapResponse rc;
  ImapMboxHandle *h;

  if(argc<1) {
    fprintf(stderr, "delete HOST MAILBOX\n");
    return 1;
  }

  h = get_handle(argv[0]);
  if(!h) {
    fprintf(stderr, "Connection to %s failed.\n", argv[0]);
    return 1;
  }

  rc = imap_mbox_delete(h, argv[1]);
  g_object_unref(h);

  return rc == IMR_OK ? 0 : 1;
}

/** test mailbox name quoting. */
static int
test_mailbox_name_quoting()
{
  static const struct {
    const char *test, *reference;
  } test_mailbox_names[] = {
    { "INBOX", "INBOX" },
    { "ehlo", "ehlo" },
    { "ångström", "&AOU-ngstr&APY-m" },
    { "quot\"ed\"", "quot\\\"ed\\\"" },
    { "dirty & ugly", "dirty &- ugly" },
    { "dir\\mbox", "dir\\\\mbox" }
  };
  int failure_count = 0;
  unsigned i;
  for(i=0;
      i<sizeof(test_mailbox_names)/sizeof(test_mailbox_names[0]);
      ++i) {
    char *mbx7 = imap_utf8_to_mailbox(test_mailbox_names[i].test);
    if (!mbx7)
      continue;
    if (strcmp(mbx7, test_mailbox_names[i].reference) != 0) {
      printf("Encoded name for '%s' expected '%s' found '%s'\n",
             test_mailbox_names[i].test,
             test_mailbox_names[i].reference,
             mbx7);
      ++failure_count;
    }
    free(mbx7);
  }
  return failure_count;
}

static unsigned
process_options(int argc, char *argv[])
{
  int first_arg;
  for(first_arg = 1; first_arg<argc; first_arg++) {
    if( strcmp(argv[first_arg], "-u") == 0 &&
	first_arg+1 < argc) {
      TestContext.user = argv[++first_arg];
    } else if( strcmp(argv[first_arg], "-p") == 0 &&
	       first_arg+1 < argc) {
      TestContext.password = argv[++first_arg];
    } else if( strcmp(argv[first_arg], "-a") == 0) {
      TestContext.anonymous = TRUE;
    } else if( strcmp(argv[first_arg], "-c") == 0) {
      TestContext.compress = TRUE;
    } else if( strcmp(argv[first_arg], "-T") == 0) {
      TestContext.tls_mode = NET_CLIENT_CRYPT_STARTTLS;
    } else if( strcmp(argv[first_arg], "-t") == 0) {
      TestContext.tls_mode = NET_CLIENT_CRYPT_NONE;
    } else {
      break; /* break the loop - non-option encountered. */
    }
  }
  return first_arg;
}

int
main(int argc, char *argv[]) {
  if(argc<=1) {
    test_envelope_strings();
    test_body_strings();
    test_mailbox_name_quoting();
  } else {
    static const struct {
      int (*func)(int argc, char *argv[]);
      const char *cmd;
      const char *help;
    } cmds[] = {
      { test_mbox_dumpfile, "dump", "HOST MAILBOX (dumps to dump.mbox)" },
      { test_mbox_dumpdir, "dumpdir", "HOST MAILBOX DST_DIRECTORY" },
      { test_mbox_append, "append", "HOST MAILBOX SRC_DIRECTORY" },
      { test_mbox_append_multi, "multi", "HOST MAILBOX SRC_DIRECTORY" },
      { test_mbox_delete, "delete", "HOST MAILBOX" }
    };
    unsigned i;
    int first_arg = process_options(argc, argv);
    for(i=0; i<sizeof(cmds)/sizeof(cmds[0]); i++) {
      if(strcmp(argv[first_arg], cmds[i].cmd) == 0) {
	return cmds[i].func(argc-first_arg-1, argv+first_arg+1);
      }
    }
    fprintf(stderr, "Unknown command '%s'. Known commands:\n",
	    argv[first_arg]);
    for(i=0; i<sizeof(cmds)/sizeof(cmds[0]); i++)
      fprintf(stderr, "%s %s\n", cmds[i].cmd, cmds[i].help);
    fprintf(stderr, "Known options:\n"
            "-u USER specify user\n"
            "-p PASSWORD specify password\n"
            "-a anonymous\n"
            "-c compress\n"
            "-m enable monitor\n"
            "-s over ssl\n"
            "-T tls required\n"
            "-t tls disabled\n");

    return 1;
  }

  return 0;
}
