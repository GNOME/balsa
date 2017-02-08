# $Id$

# Note: the following hack is needed so lcov recognises the paths of the sources...
libsrcdir = $(shell echo $(abs_srcdir) | sed -e 's;/test$$;;')
test_src = $(libsrcdir)/net-client.c $(libsrcdir)/net-client-smtp.c $(libsrcdir)/net-client-utils.c

EXTRA_DIST = 		\
	tests.c			\
	inetsim.conf	\
	ca_cert.pem		\
	cert.pem 		\
	cert_u.pem 		\
	valgrind.supp

TESTFLAGS = -DNCAT="\"@NCAT@\"" -DSED="\"@SED@\"" -fprofile-arcs -ftest-coverage -g -Wno-error

LCOVFLGS	= --rc lcov_branch_coverage=1
GENHTMLFLGS	= --function-coverage --branch-coverage --num-spaces 4
VALGRFLAGS  = --tool=memcheck --log-file=$@.vg --suppressions=valgrind.supp --leak-check=full --child-silent-after-fork=yes

CLEANFILES = *.gcda *.gcno *.covi *.vg tests

clean-local:
	-rm -rf gcov

tests:	tests.c
	$(CC) $(LIBNETCLIENT_CFLAGS) $(CPPFLAGS) $(TESTFLAGS) -I. -I.. $< $(test_src) -o $@ $(LIBNETCLIENT_LIBS)
	$(VALGRIND) $(VALGRFLAGS) ./$@
	$(LCOV) $(LCOVFLGS) -c -b $(libsrcdir) -d $(abs_srcdir) --no-external -o $@.covi
	$(LCOV) $(LCOVFLGS) -r $@.covi $< -o $@.covi
	$(GENHTML) $(GENHTMLFLGS) -o gcov $@.covi
