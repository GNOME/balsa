project('balsa', 'c',
        version         : '2.6.4',
        meson_version   : '>= 0.50.0',
        default_options : ['sysconfdir=/etc', 'buildtype=debugoptimized'])

# if meson is used not in a git source tree, use project version:
balsa_version = meson.project_version()

balsa_from_git = run_command('sh', '-c', 'test -d ${MESON_SOURCE_ROOT}/.git',
                             check : false).returncode() == 0

if balsa_from_git
  git_describe = run_command('git', 'describe', '--tags',
                             check : true)
  if git_describe.returncode() == 0
    balsa_version = git_describe.stdout().strip()
  endif
endif

# ###########################################################################
# Versioning
# ###########################################################################

conf = configuration_data()
conf.set_quoted('BALSA_VERSION',   balsa_version)
conf.set_quoted('VERSION',         balsa_version)
conf.set_quoted('PACKAGE_VERSION', balsa_version)
conf.set_quoted('PACKAGE', 'balsa',
                description : 'Name of package')

# #####################################################################
# System extensions
# #####################################################################

conf.set('_GNU_SOURCE', 1)

# #####################################################################
# Options
# #####################################################################

if get_option('flock')
  conf.set('USE_FLOCK', 1,
    description : 'Define if flock locking should be used.')
endif

if get_option('fcntl')
  conf.set('USE_FCNTL', 1,
    description : 'Define if fcntl locking should be used.')
endif

gnome_desktop = get_option('gnome-desktop')
autocrypt     = get_option('autocrypt')
systray       = get_option('systray')
canberra      = get_option('canberra')
compface      = get_option('compface')
gss           = get_option('gss')
html_widget   = get_option('html-widget')
gtksourceview = get_option('gtksourceview')
spell_checker = get_option('spell-checker')
ldap          = get_option('ldap')
igemacint     = get_option('macosx-desktop')
rubrica       = get_option('rubrica')
osmo          = get_option('osmo')
sqlite        = get_option('sqlite')
libsecret     = get_option('libsecret')
gcr           = get_option('gcr')
more_warnings = get_option('more-warnings')
help_files    = get_option('help-files')

# Documentation and Testing requirements for libnetclient
libnetclient_docs = get_option('libnetclient-docs')
libnetclient_test = get_option('libnetclient-test')

# #####################################################################
# Programs: compilers and their options.
# #####################################################################

compiler = meson.get_compiler('c')

uts_prefix = '''
#define _GNU_SOURCE
#include <sys/utsname.h>
'''
if compiler.has_member('struct utsname', 'domainname', prefix : uts_prefix)
  conf.set('HAVE_STRUCT_UTSNAME_DOMAINNAME', 1,
    description : 'Define to 1 if ‘domainname’ is a member of ‘struct utsname’.')
endif

# yelp

if help_files
  yelptool = find_program('yelp-build', required : false)
  if not yelptool.found()
    message('*** The tools to build the documentation are not found.')
    message('    To build it, please install the yelp-tools package.')
    message('    Continuing with build. ****')
    help_files = false
  endif
endif

# _NL_MEASUREMENT_MEASUREMENT is an enum and not a define

code = '''
#include <langinfo.h>
void func()
{
    char c;
    c = *((unsigned char *) nl_langinfo(_NL_MEASUREMENT_MEASUREMENT));
}
'''
result = compiler.compiles(code, name : '_NL_MEASUREMENT_MEASUREMENT test')
if result
  conf.set('HAVE__NL_MEASUREMENT_MEASUREMENT', 1,
    description : 'Define if _NL_MEASUREMENT_MEASUREMENT is available')
endif

# #####################################################################
# GNOME libs
# #####################################################################

if gnome_desktop
  TOOLKIT_CATEGORIES = 'GNOME;GTK;'
else
  TOOLKIT_CATEGORIES = 'GTK;'
endif # gnome_desktop

conf.set('TOOLKIT_CATEGORIES', TOOLKIT_CATEGORIES)

# #####################################################################
# Libraries.
# #####################################################################

# Dependencies for balsa and balsa_ab:
glib_dep    = dependency('glib-2.0',  version : '>= 2.48.0')
gtk_dep     = dependency('gtk+-3.0',  version : '>= 3.24.0')
gmime_dep   = dependency('gmime-3.0', version : '>= 3.2.6' )
gio_dep     = dependency('gio-2.0'                         )
gthread_dep = dependency('gthread-2.0'                     )
gnutls_dep  = dependency('gnutls',    version : '>= 3.0'   )
gpgme_dep   = dependency('gpgme',     version : '>= 1.8.0' )
fribidi_dep = dependency('fribidi'                         )
libical_dep = dependency('libical',   version : '>= 2.0.0' )

# Dependencies for balsa
balsa_deps = [glib_dep,
              gtk_dep,
              gmime_dep,
              gio_dep,
              gthread_dep,
              gnutls_dep,
              gpgme_dep,
              fribidi_dep,
              libical_dep]

# Dependencies for balsa_ab:
balsa_ab_deps = [glib_dep,
                 gtk_dep,
                 gmime_dep]

# Dependencies for libnetclient:
libnetclient_deps = [glib_dep,
                     gio_dep,
                     gnutls_dep]

# ##########################################################################
# Check versions
# ##########################################################################

# HTML widget
#
if html_widget == 'webkit2'
  html_dep = dependency('webkit2gtk-4.1', required : false)
  if not html_dep.found()
    html_dep = dependency('webkit2gtk-4.0', version : '>= 2.28.0')
  endif
  htmlpref_dep = dependency('sqlite3', version : '>= 3.24.0')
  balsa_web_extensions = join_paths(get_option('prefix'), get_option('libdir'), 'balsa')
  add_project_arguments('-DBALSA_WEB_EXTENSIONS="' + balsa_web_extensions + '"', language : 'c')
  add_project_arguments('-DBALSA_WEB_EXT_DEVEL="' + join_paths(meson.current_build_dir(), 'libbalsa') + '"', language : 'c')

  html2text = find_program('python-html2text',
                           'html2markdown',
                           'html2markdown.py2',
                           'html2markdown.py3',
                           'html2text',
                           required : false)
  if html2text.found()
    if meson.version().version_compare('>=0.55')
      html2text_path = html2text.full_path()
    else # meson.version().version_compare('>=0.55')
      html2text_path = html2text.path()
    endif # meson.version().version_compare('>=0.55')
    conf.set_quoted('HTML2TEXT', html2text_path,
      description : 'Path to html2text program.')
    conftest_htm = join_paths(meson.current_build_dir(), 'conftest.htm')
    run_command('sh', '-c', 'echo "<html/>" > ' + conftest_htm, check : true)
    unicode_snob = run_command(html2text, '--unicode-snob', conftest_htm,
                               check : false)
    if unicode_snob.returncode() == 0
      conf.set('HTML2TEXT_UCOPT', 1,
        description : 'Defined when HTML2TEXT accepts the --unicode-snob option.')
    endif
    # With meson version 0.52, the following run_command() makes ninja
    # go into an endless loop, so we'll skip it, and just leave
    # conftest.htm in the current build dir:
    # run_command('rm', '-f', conftest_htm)
  else
    message('You cannot properly quote html-only messags.')
  endif

  conf.set('HAVE_HTML_WIDGET', 1,
    description : 'Defined when an HTML widget can be used.')
  balsa_deps += [html_dep, htmlpref_dep]
endif

# Autocrypt
if autocrypt
  autocrypt_dep = dependency('sqlite3', required : true)
  conf.set('ENABLE_AUTOCRYPT', 1, description : 'If defined, enable Autocrypt support.')
  balsa_deps += autocrypt_dep
endif

# System Tray Icons
if systray
  systray_dep = dependency('xapp', required : true)
  conf.set('ENABLE_SYSTRAY', 1, description : 'If defined, enable System Tray Icon support.')
  balsa_deps += systray_dep
endif

# OpenLDAP configuration.
#
if ldap != 'false'
  ldap_compile_args = []
  ldap_link_args    = ['-lldap', '-llber', '-lresolv']
  if ldap != 'true'
    ldap_compile_args += '-I' + join_paths(ldap, 'include')
    ldap_link_args    += '-L' + join_paths(ldap, 'lib')
  endif

  result = compiler.has_function('ldap_search', args : ldap_link_args)
  if result
    conf.set('ENABLE_LDAP', 1,
      description : 'Defined when the LDAP support is to be compiled in.')
  else
    error('*** You enabled LDAP but ldap library is not found.')
  endif

  result = compiler.has_function('ldap_start_tls', args : ldap_link_args)
  if result
    conf.set('HAVE_LDAP_TLS', 1,
      description : 'Defined when LDAP lib supports TLS')
  endif

  ldap_dep = declare_dependency(compile_args : ldap_compile_args,
                                link_args    : ldap_link_args)
  balsa_deps += ldap_dep
  balsa_ab_deps += ldap_dep
endif # ldap != 'false'

# SQLite configuration.
#
if sqlite != 'false'
  sqlite_dep = dependency('sqlite3', required : false)
  if sqlite_dep.found()
    conf.set('HAVE_SQLITE3', 1,
      description : 'If defined, use sqlite3 api.')
  else
    sqlite_dep = dependency('sqlite', version : '>= 2.8')
  endif
  conf.set('HAVE_SQLITE', 1,
    description : 'If defined, enable GPE address book support.')

  if sqlite != 'true'
    # If sqlite is installed in a nonstandard location, PKG_CONFIG_PATH
    # would probably need to be set appropriately in the environment.
    # There doesn't seem to be a way to do that within dependency().
    real_dep = declare_dependency(dependencies : sqlite_dep,
                                  compile_args : '-I' + join_paths(sqlite, 'include'),
                                  link_args    : '-L' + join_paths(sqlite, 'lib'))
  else
    real_dep = sqlite_dep
  endif

  balsa_deps += real_dep
  balsa_ab_deps += real_dep
endif # sqlite != 'false'

# Rubrica (libxml2) configuration
#
if rubrica
  xml_dep = dependency('libxml-2.0')
  conf.set('HAVE_RUBRICA', 1,
    description : 'If defined, enable Rubrica2 address book support.')
  balsa_deps += xml_dep
  balsa_ab_deps += xml_dep
endif # rubrica

# Osmo address book support
#
if osmo
  conf.set('HAVE_OSMO', 1,
    description : 'If defined, enable Osmo address book support.')
endif # osmo

# KRB5/GSSAPI configuration.
#
if gss
  krb5_config = find_program('krb5-config',
                             '/usr/kerberos/bin/krb5-config',
                             '/usr/lib/mit/bin/krb5-config',
                             required : false)
  if not krb5_config.found()
    error('GSS requested but krb5-config not found. Install krb5-devel/heimdal-devel')
  endif
  conf.set('HAVE_GSSAPI', 1,
    description : 'Defined when GSSAPI support is to be compiled.')
  krb5_cflags = run_command(krb5_config, ['--cflags', 'gssapi'], check : true).stdout().strip().split()
  krb5_libs   = run_command(krb5_config, ['--libs',   'gssapi'], check : true).stdout().strip().split()
  if compiler.has_header('gssapi.h')
    conf.set('HAVE_HEIMDAL', 1,
      description : 'Defined for Heimdal.')
  elif not compiler.has_header('gssapi/gssapi.h')
    error('gssapi.h not found')
  endif
  krb5_dep = declare_dependency(compile_args : krb5_cflags,
                                link_args    : krb5_libs)
  balsa_deps        += krb5_dep
  libnetclient_deps += krb5_dep
endif # gss

# gcr configuration
#
if gcr
  gcr_dep = dependency('gcr-3')
  if gcr_dep.found()
    conf.set('HAVE_GCR', 1,
      description : 'Defined when gcr-3 can be used.')
  endif
  balsa_deps += gcr_dep
endif # gcr

# Compface configuration
#
if compface != 'false'
  compface_compile_args = []
  compface_link_args    = ['-lcompface']
  if compface != 'true'
    compface_compile_args += '-I' + join_paths(compface, 'include')
    compface_link_args    += '-L' + join_paths(compface, 'lib')
  endif

  result = compiler.has_function('uncompface', args : compface_link_args)
  if result
    conf.set('HAVE_COMPFACE', 1,
      description : 'Defined when Compface support is requested.')
  else
    error('*** You enabled Compface but the library is not found.')
  endif

  compface_dep = declare_dependency(compile_args : compface_compile_args,
                                    link_args    : compface_link_args)
  balsa_deps += compface_dep
endif # compface != 'false'

# GtkSourceView configuration
#
if gtksourceview
  gsv_dep = dependency('gtksourceview-4')
  balsa_deps += gsv_dep
  conf.set('HAVE_GTKSOURCEVIEW', 1,
    description : 'Defined when GtkSourceView-4 can be used.')
endif # gtksourceview

# libcanberra configuration
#
if canberra
  canberra_dep = dependency('libcanberra-gtk3')
  conf.set('HAVE_CANBERRA', 1,
    description : 'Defined when canberra can be used.')
  balsa_deps += canberra_dep
endif # canberra

# ige-mac-integration (Mac OS X desktop only)
#
if igemacint
  igemacint_dep = dependency('ige-mac-integration', version  : '>= 0.8')
  conf.set('HAVE_MACOSX_DESKTOP', 1,
    description : 'Defined when menus can be integrated into the Mac OS X desktop')
  balsa_deps    += igemacint_dep
  balsa_ab_deps += igemacint_dep
endif # igemacint

# Libsecret configuration
#
if libsecret
  secret_dep = dependency('libsecret-1', required : false)
  if not secret_dep.found()
    error('You asked to link to libsecret, but the development files are not installed')
  endif
  conf.set('HAVE_LIBSECRET', 1,
    description : 'Defined when libsecret can be used.')
  balsa_deps += secret_dep
endif # libsecret

#####################################################################
# Headers.
#####################################################################

# Spell check detection.
#
if spell_checker == 'gspell'
  spell_dep = dependency('gspell-1', version : '>= 1.6.0')
  conf.set('HAVE_GSPELL', 1,
    description : 'Defined when Gspell can be used.')
elif spell_checker == 'gtkspell'
  spell_dep = dependency('gtkspell3-3.0', version : '>= 3.0.9')
  conf.set('HAVE_GTKSPELL', 1,
    description : 'Defined when GtkSpell can be used.')
elif spell_checker == 'internal'
  spell_dep = dependency('enchant-2',
                         required : false)
  if not spell_dep.found()
    spell_dep = dependency('enchant',
                           version : '!= 1.6.1')
  endif
endif

balsa_deps += spell_dep

#####################################################################
# Typedefs, structures and compiler characteristics.
#####################################################################

conf.set('HAVE_DECL_CTIME_R',
  compiler.has_header_symbol('time.h', 'ctime_r') ? 1 : 0,
  description : 'Define to 1 if you have the declaration of ‘ctime_r’, and to 0 if you don’t.')

if compiler.has_function('ctime_r')
  conf.set('HAVE_CTIME_R', 1,
    description : 'Define to 1 if you have the ‘ctime_r’ function.')
endif

if compiler.has_header('locale.h')
  conf.set('HAVE_LOCALE_H', 1,
    description : 'Define to 1 if you have the <locale.h> header')
endif

# set more warnings.
#

if more_warnings
  # meson sets -Wall, so we do not need these:
  # '-Wsign-compare',
  # '-Wchar-subscripts',
  # '-Wuninitialized',
  # '-Wreturn-type',
  args = compiler.get_supported_arguments([
    '-Wpointer-arith',
    '-Wnested-externs',
    '-Wmissing-prototypes',
    '-Wdeclaration-after-statement',
  ])
  add_global_arguments(args, language : 'c')
endif # more_warnings

# extra warnings for GIT.
#
if balsa_from_git
  args = [
    '-Werror',
    '-Wdeprecated-declarations',
    '-Wredundant-decls',
    '-Wshadow',
    '-Wformat',
    '-Wformat-security',
    '-Wignored-qualifiers',
    '-Wenum-conversion',
    '-DGDK_DISABLE_DEPRECATED',
    '-DGMIME_DISABLE_DEPRECATED',
    '-DGTK_DISABLE_DEPRECATED',
    '-DNM_DISABLE_DEPRECATED',
    '-DPANGO_DISABLE_DEPRECATED',
    ]
  opt_level = get_option('optimization')
  if opt_level == '2' or opt_level == '3'
    message('Add FORTIFY_SOURCE macro')
    args += [
      '-U_FORTIFY_SOURCE',
      '-D_FORTIFY_SOURCE=2',
      ]
  endif # get_option('optimization') >= 2
  add_global_arguments(compiler.get_supported_arguments(args), language : 'c')
endif # balsa_from_git

###########################################################################
# Remember our prefixes
###########################################################################

balsa_data_prefix = join_paths(get_option('prefix'), get_option('datadir'), 'balsa')
add_global_arguments('-DBALSA_DATA_PREFIX="' + balsa_data_prefix + '"', language : 'c')

###########################################################################
# Documentation and Testing requirements for libnetclient
###########################################################################

if libnetclient_docs
  doxygen_program = find_program('doxygen', required : false)
  if not doxygen_program.found()
    error('*** Cannot build libnetclient API documentation, see libnetclient/README for fixing this issue ***')
  endif
endif # libnetclient_docs

if libnetclient_test
  have_sput           = compiler.has_header('sput.h')
  valgrind_program    = find_program('valgrind',    required : false)
  lcov_program        = find_program('lcov',        required : false)
  genhtml_program     = find_program('genhtml',     required : false)
  screen_program      = find_program('screen',      required : false)
  gnutls_serv_program = find_program('gnutls-serv', required : false)
  sudo_program        = find_program('sudo',        required : false)
  inetsim_program     = find_program('inetsim',     required : false)

  if not (have_sput
          and valgrind_program.found()
          and lcov_program.found()
          and genhtml_program.found()
          and screen_program.found()
          and gnutls_serv_program.found()
          and sudo_program.found()
          and inetsim_program.found())
    error('*** Cannot run libnetclient tests, see libnetclient/README for fixing this issue ***')
  endif

endif # libnetclient_test

#####################################################################
# Output
#####################################################################

file = 'balsa.1'
configure_file(input         : file + '.in',
               output        : file,
               configuration : conf,
               install       : true,
               install_dir   : join_paths(get_option('datadir'),
                                          'man/man1'))

file = 'balsa.spec'
configure_file(input : file + '.in', output : file, configuration : conf)

# config.h
configure_file(output : 'config.h', configuration : conf)
add_project_arguments('-DHAVE_CONFIG_H=1', language : 'c')

#####################################################################
# gettext
#####################################################################

i18n = import('i18n')

gettext_package = 'balsa'
add_project_arguments('-DGETTEXT_PACKAGE="' + gettext_package + '"', language : 'c')
subdir('po')

desktop_files = ['org.desktop.Balsa.desktop', 'balsa-mailto-handler.desktop']
foreach file : desktop_files
  file_in = configure_file(input         : file + '.meson',
                           output        : file + '.in',
                           configuration : conf)
  i18n.merge_file(
    input       : file_in,
    output      : file,
    type        : 'desktop',
    po_dir      : 'po',
    install     : true,
    install_dir : join_paths(get_option('datadir'), 'applications')
  )
endforeach

i18n.merge_file(
  input       : 'balsa.appdata.xml.in',
  output      : 'balsa.appdata.xml',
  po_dir      : 'po',
  install     : true,
  install_dir : join_paths(get_option('datadir'), 'metainfo')
)

##########################################################################
# Summary
##########################################################################

summary = [
  '',
  '============= Final configuration =============',
  ' Installing into prefix: @0@'.format(get_option('prefix')),
  'Enable compile warnings: @0@'.format(more_warnings),
  '            HTML widget: @0@'.format(html_widget),
  '              Use GNOME: @0@'.format(gnome_desktop),
  '           Use Canberra: @0@'.format(canberra),
  '          Use Autocrypt: @0@'.format(autocrypt),
  '               Use LDAP: @0@'.format(ldap),
  '                Use GSS: @0@'.format(gss),
  '             Use SQLite: @0@'.format(sqlite),
  '          Spell checker: @0@'.format(spell_checker),
  '      Use GtkSourceView: @0@'.format(gtksourceview),
  '           Use Compface: @0@'.format(compface),
  '          Use libsecret: @0@'.format(libsecret),
  '                Use gcr: @0@'.format(gcr),
  '===============================================',
  ''
  ]

message('\n'.join(summary))

if balsa_from_git
  message('''
============================== NOTICE ================================
                You are using Balsa from GIT source.
  The program is likely to be unstable, contain incomplete features,
or just plain not work. Use it at your own risk. You have been warned.
======================================================================
''')

endif

top_include           = include_directories('.')
libnetclient_include  = include_directories('libnetclient')
libbalsa_include      = include_directories('libbalsa')
libinit_balsa_include = include_directories('libinit_balsa')
src_include           = include_directories('src')

gnome = import('gnome')

balsa_resources = gnome.compile_resources('balsa-resources',
                                          'ui/balsa.gresource.xml',
                                          source_dir : 'ui',
                                          c_name     : 'balsa')

balsa_ab_resources = gnome.compile_resources('balsa-ab-resources',
                                             'ui/balsa-ab.gresource.xml',
                                             source_dir : 'ui',
                                             c_name     : 'balsa_ab')

subdir('sounds')
subdir('images')
subdir('libnetclient')
subdir('libbalsa')
subdir('libinit_balsa')
subdir('src')
if help_files
  subdir('doc')
endif

if gtksourceview
  gtksourceview_files = ['balsa-mail.lang', 'balsa-mail-style.xml']
  gtksourceview_dir = join_paths(get_option('datadir'), 'balsa', 'gtksourceview-3.0')
  install_data(gtksourceview_files, install_dir : gtksourceview_dir)
endif

# desktop icon:
install_data('gnome-balsa2.png',
             install_dir : join_paths(get_option('datadir'), 'pixmaps'))

####################### Autotools stuff below this line #######################
#
#dist-hook: balsa.spec
#	cp balsa.spec $(distdir)
#
#@MAINT@RPM: balsa.spec
#@MAINT@	rm -f *.rpm
#@MAINT@	$(MAKE) distdir="$(PACKAGE)-$(PACKAGE_VERSION)" dist
#@MAINT@	cp $(top_srcdir)/rpm-po.patch $(top_builddir)/rpm-po.patch
#@MAINT@	rpm -ta "./$(PACKAGE)-$(PACKAGE_VERSION).tar.gz"
#@MAINT@	rm $(top_builddir)/rpm-po.patch
#@MAINT@	-test -f "/usr/src/redhat/SRPMS/$(PACKAGE)-$(PACKAGE_VERSION)-@BALSA_RELEASE@.src.rpm" \
#@MAINT@		&& cp -f "/usr/src/redhat/SRPMS/$(PACKAGE)-$(PACKAGE_VERSION)-@BALSA_RELEASE@.src.rpm" .
#@MAINT@	-for ping in /usr/src/redhat/RPMS/* ; do \
#@MAINT@		if test -d $$ping ; then \
#@MAINT@			arch=`echo $$ping |sed -e 's,/.*/\([^/][^/]*\),\1,'` ; \
#@MAINT@			f="$$ping/$(PACKAGE)-$(PACKAGE_VERSION)-@BALSA_RELEASE@.$$arch.rpm" ; \
#@MAINT@			test -f $$f && cp -f $$f . ; \
#@MAINT@		fi ; \
#@MAINT@	done
#
#@MAINT@snapshot:
#@MAINT@	$(MAKE) distdir=$(PACKAGE)-`date +"%y%m%d"` dist
#
#@MAINT@balsa-dcheck:
#@MAINT@	$(MAKE) BALSA_DISTCHECK_HACK=yes distcheck
