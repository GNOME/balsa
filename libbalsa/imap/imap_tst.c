/** @file imap_tst.c tests some IMAP capabilities. It is very useful
    for stress-testing the imap part of balsa. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "libimap.h"
#include "imap-handle.h"
#include "imap-commands.h"

struct {
  char *user;
  char *password;
  gboolean monitor;
} TestContext = { NULL, NULL, FALSE };

static void
monitor_cb(const char *buffer, int length, int direction, void *arg)
{
  int i;
  printf("IMAP %c: ", direction ? 'C' : 'S');
  for (i = 0; i < length; i++)
    putchar(buffer[i]);

  fflush(NULL);
}


static gchar*
get_user(const char* method) {
  char buf[256];
  size_t len;

  if(TestContext.user) {
    printf("Login with method %s as user: %s\n", method, TestContext.user);
    return g_strdup(TestContext.user);
  } else {
    printf("Login with method %s as user: ", method);
    fflush(stdout);
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

static void
user_cb(ImapUserEventType ue, void *arg, ...)
{
    va_list alist;
    int *ok;
    va_start(alist, arg);
    switch(ue) {
    case IME_GET_USER_PASS: {
        gchar *method = va_arg(alist, gchar*);
        gchar **user = va_arg(alist, gchar**);
        gchar **pass = va_arg(alist, gchar**);
        ok = va_arg(alist, int*);
	g_free(*user); *user = get_user(method);
	g_free(*pass); *pass = get_password();
        *ok = 1; printf("Logging in with %s\n", method);
        break;
    }
    case IME_GET_USER:  { /* for eg kerberos */
        gchar **user;
        gchar *method;
        method = va_arg(alist, gchar*);
        user = va_arg(alist, gchar**);
        ok = va_arg(alist, int*);
        *ok = 1; /* consider popping up a dialog window here */
        g_free(*user); *user = get_user(method);
        break;
    }
    case IME_TLS_VERIFY_ERROR:
        ok = va_arg(alist, int*);
	*ok = 1;
        break;
    case IME_TLS_NO_PEER_CERT: {
        ok = va_arg(alist, int*); *ok = 0;
        printf("IMAP:TLS: Server presented no cert!\n");
        break;
    }
    case IME_TLS_WEAK_CIPHER: {
        ok = va_arg(alist, int*); *ok = 1;
        printf("IMAP:TLS: Weak cipher accepted.\n");
        break;
    }
    case IME_TIMEOUT: {
        ok = va_arg(alist, int*); *ok = 1;
        break;
    }
    default: g_warning("unhandled imap event type! Fix the code."); break;
    }
    va_end(alist);
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

  h = imap_mbox_handle_new();

  imap_handle_set_tls_mode(h, IMAP_TLS_DISABLED);

  if(TestContext.monitor)
    imap_handle_set_monitorcb(h, monitor_cb, NULL);

  imap_handle_set_usercb(h,    user_cb, NULL);
  if(imap_mbox_handle_connect(h, host, FALSE) != IMAP_SUCCESS) {
    fprintf(stderr, "Connection to %s failed.\n", host);
    return 1;
  }

  if(imap_mbox_select(h, mailbox, &read_only) == IMR_OK) {
    unsigned cnt = imap_mbox_handle_get_exists(h);
    unsigned i;
#define FETCH_AT_ONCE 300
    for(i=0; i<cnt; i+= FETCH_AT_ONCE) {
      unsigned arr[FETCH_AT_ONCE], j;
      unsigned batch_length = i+FETCH_AT_ONCE > cnt ? cnt-i : FETCH_AT_ONCE;
      printf("Fetching %u:%u\n", i+1, i+batch_length);
      for(j=0; j<batch_length; j++) arr[j] = i+1+j;
      if( imap_mbox_handle_fetch_rfc822(h, batch_length, arr,
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
  f = fopen(fname, "wt");
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
  h = imap_mbox_handle_new();
  imap_handle_set_tls_mode(h, IMAP_TLS_DISABLED);

  if(TestContext.monitor)
    imap_handle_set_monitorcb(h, monitor_cb, NULL);

  imap_handle_set_usercb(h, user_cb, NULL);
  if(imap_mbox_handle_connect(h, host, FALSE) != IMAP_SUCCESS) {
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
    res = imap_mbox_append_multi(h, mailbox, msg_iterator, &mi);

  } else {
    for(res = IMR_OK; res == IMR_OK && (file = readdir(dir)) != NULL;) {
      struct stat buf;
      char *file_name = g_build_filename(src_dir, file->d_name, NULL);

      if(stat(file_name, &buf) == 0) {
	if(S_ISREG(buf.st_mode)) {
	  FILE *fh = fopen(file_name, "rb");

	  if(fh) {
	    printf("Processing file %s of size %lu\n", file_name,
		   (unsigned long)buf.st_size);
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
  return 0;
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
    } else if( strcmp(argv[first_arg], "-m") == 0) {
      TestContext.monitor = TRUE;
    } else {
      break; /* break the loop - non-option encountered. */
    }
  }
  return first_arg;
}

int
main(int argc, char *argv[]) {
  g_type_init();

  if(argc<=1) {
    test_envelope_strings();
    test_body_strings();
  } else {
    static const struct {
      int (*func)(int argc, char *argv[]);
      const char *cmd;
      const char *help;
    } cmds[] = {
      { test_mbox_dumpfile, "dump", "HOST MAILBOX (dumps to dump.mbox)" },
      { test_mbox_dumpdir, "dumpdir", "HOST MAILBOX DST_DIRECTORY" },
      { test_mbox_append, "append", "HOST MAILBOX SRC_DIRECTORY" },
      { test_mbox_append_multi, "multi", "HOST MAILBOX SRC_DIRECTORY" }
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
    return 1;
  }

  return 0;
}
