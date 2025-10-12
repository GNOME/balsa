project('balsa', 'c',
        version         : '2.6.5',
        meson_version   : '>= 1.0.0',
        default_options : ['sysconfdir=/etc', 'buildtype=debugoptimized'])

# if meson is used not in a git source tree, use project version:
balsa_version = meson.project_version()

balsa_from_git = run_command('sh', '-c', 'test -d ${MESON_SOURCE_ROOT}/.git',
                             check : false).returncode() == 0

if balsa_from_git
  git_reltag = run_command('git', 'tag', '--points-at', check : true)
  reltag_version = git_reltag.stdout().strip()
  if reltag_version != ''
    balsa_version = reltag_version
  else
    git_describe = run_command('git', 'describe', '--tags',
                               check : true)
    if git_describe.returncode() == 0
      balsa_version = git_describe.stdout().strip()
    endif
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
osmo          = get_option('osmo')
gpe           = get_option('gpe')
libsecret     = get_option('libsecret')
webdav        = get_option('webdav')
gcr           = get_option('gcr')
more_warnings = get_option('more-warnings')
help_files    = get_option('help-files')
nls           = get_option('nls')

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
glib_dep    = dependency('glib-2.0',  version : '>= 2.74.0')
gtk_dep     = dependency('gtk+-3.0',  version : '>= 3.24.0')
gmime_dep   = dependency('gmime-3.0', version : '>= 3.2.13')
gio_dep     = dependency('gio-2.0'                         )
gthread_dep = dependency('gthread-2.0'                     )
gnutls_dep  = dependency('gnutls',    version : '>= 3.7'   )
gpgme_dep   = dependency('gpgme',     version : '>= 1.18.0')
fribidi_dep = dependency('fribidi'                         )
libical_dep = dependency('libical',   version : '>= 3.0.0' )

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
    html_dep = dependency('webkit2gtk-4.0', version : '>= 2.48.5')
  endif
  htmlpref_dep = dependency('sqlite3', version : '>= 3.24.0')
  balsa_web_extensions = join_paths(get_option('prefix'), get_option('libdir'), 'balsa')
  add_project_arguments('-DBALSA_WEB_EXTENSIONS="' + balsa_web_extensions + '"', language : 'c')
  add_project_arguments('-DBALSA_WEB_EXT_DEVEL="' + join_paths(meson.current_build_dir(), 'libbalsa') + '"', language : 'c')

  html2text = find_program('python-html2text',
                           'html2markdown',
                           'html2markdown.py3',
                           'html2markdown.py2',
                           'html2text',
                           required : false)
  if html2text.found()
    html2text_path = html2text.full_path()
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
  autocrypt_dep = dependency('sqlite3')
  conf.set('ENABLE_AUTOCRYPT', 1, description : 'If defined, enable Autocrypt support.')
  balsa_deps += autocrypt_dep
endif

# System Tray Icons
if systray
  systray_dep = dependency('xapp')
  conf.set('ENABLE_SYSTRAY', 1, description : 'If defined, enable System Tray Icon support.')
  balsa_deps += systray_dep
endif

# OpenLDAP configuration.
#
if ldap
  ldap_dep = dependency('ldap')
  conf.set('ENABLE_LDAP', 1, description : 'Defined when the LDAP support is to be compiled in.')
  result = compiler.has_function('ldap_start_tls', dependencies : ldap_dep)
  if result
    conf.set('HAVE_LDAP_TLS', 1, description : 'Defined when LDAP lib supports TLS')
  endif
  balsa_deps += ldap_dep
  balsa_ab_deps += ldap_dep
endif # ldap

# GPE address book configuration.
#
if gpe
  gpe_dep = dependency('sqlite3')
  conf.set('HAVE_GPE', 1, description : 'If defined, enable GPE address book support.')
  balsa_deps += gpe_dep
  balsa_ab_deps += gpe_dep
endif # gpe

# Osmo address book support
#
if osmo
  conf.set('HAVE_OSMO', 1, description : 'If defined, enable Osmo address book support.')
endif # osmo

# KRB5/GSSAPI configuration.
#
if gss
  krb5_dep = dependency('krb5-gssapi')
  conf.set('HAVE_GSSAPI', 1, description : 'Defined when GSSAPI support is to be compiled.')
  balsa_deps        += krb5_dep
  libnetclient_deps += krb5_dep
endif # gss

# gcr configuration
#
if gcr
  gcr_dep = dependency('gcr-3')
  conf.set('HAVE_GCR', 1, description : 'Defined when gcr-3 can be used.')
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
  conf.set('HAVE_GTKSOURCEVIEW', 1, description : 'Defined when GtkSourceView-4 can be used.')
endif # gtksourceview

# libcanberra configuration
#
if canberra
  canberra_dep = dependency('libcanberra-gtk3')
  conf.set('HAVE_CANBERRA', 1, description : 'Defined when canberra can be used.')
  balsa_deps += canberra_dep
endif # canberra

# Libsecret configuration
#
if libsecret
  secret_dep = dependency('libsecret-1')
  conf.set('HAVE_LIBSECRET', 1, description : 'Defined when libsecret can be used.')
  balsa_deps += secret_dep
  balsa_ab_deps += secret_dep
endif # libsecret

# WebDAV configuration
#
if webdav
  webdav_dep = dependency('libsoup-3.0', required : false)
  if not webdav_dep.found()
    webdav_dep = dependency('libsoup-2.4')
  endif
  webdav_xml_dep = dependency('libxml-2.0')
  conf.set('HAVE_WEBDAV', 1, description : 'Defined when WebDAV (CardDAV) support is enabled.')
  balsa_deps += [webdav_dep, webdav_xml_dep]
  balsa_ab_deps += [webdav_dep, webdav_xml_dep]
endif # webdav

#####################################################################
# Headers.
#####################################################################

# Spell check detection.
#
if spell_checker == 'gspell'
  spell_dep = dependency('gspell-1', version : '>= 1.6.0')
  conf.set('HAVE_GSPELL', 1, description : 'Defined when Gspell can be used.')
elif spell_checker == 'gtkspell'
  spell_dep = dependency('gtkspell3-3.0', version : '>= 3.0.9')
  conf.set('HAVE_GTKSPELL', 1, description : 'Defined when GtkSpell can be used.')
elif spell_checker == 'internal'
  spell_dep = dependency('enchant-2', required : false)
  if not spell_dep.found()
    spell_dep = dependency('enchant', version : '!= 1.6.1')
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

#####################################################################
# Native Language Support
#####################################################################

if nls
  if not compiler.has_header('libintl.h')
    error('libintl.h not found')
  endif
  if compiler.has_header('locale.h')
    conf.set('HAVE_LOCALE_H', 1,
      description : 'Define to 1 if you have the <locale.h> header')
  endif
  conf.set_quoted('GETTEXT_PACKAGE', meson.project_name(),
    description : 'The `gettext` package name.')
  conf.set('ENABLE_NLS', 1,
    description : 'Define to 1 if Native Language Support is enabled.')

  # _NL_MEASUREMENT_MEASUREMENT is an enum and not a define
  code = '''
  #include <langinfo.h>
  char func(void)
  {
      char c;
      c = *((unsigned char *) nl_langinfo(_NL_MEASUREMENT_MEASUREMENT));
      return c;
  }
  '''
  result = compiler.compiles(code, name : '_NL_MEASUREMENT_MEASUREMENT test')
  if result
    conf.set('HAVE__NL_MEASUREMENT_MEASUREMENT', 1,
      description : 'Define if _NL_MEASUREMENT_MEASUREMENT is available')
  endif
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
  file_in = configure_file(input         : file + '.in.in',
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
  input       : 'org.desktop.Balsa.metainfo.xml.in',
  output      : 'org.desktop.Balsa.metainfo.xml',
  po_dir      : 'po',
  install     : true,
  install_dir : join_paths(get_option('datadir'), 'metainfo')
)

service_conf = configuration_data()
service_conf.set('BINDIR', join_paths(get_option('prefix'), get_option('bindir')))
configure_file(
  input         : 'org.desktop.Balsa.service.in',
  output        : 'org.desktop.Balsa.service',
  configuration : service_conf,
  install       : true,
  install_dir   : join_paths(get_option('datadir'), 'dbus-1', 'services')
)

##########################################################################
# Summary
##########################################################################

summary = [
  '',
  '============= Final configuration =============',
  ' Installing into prefix: @0@'.format(get_option('prefix')),
  'Enable compile warnings: @0@'.format(more_warnings),
  'Native Language Support: @0@'.format(nls),
  '            HTML widget: @0@'.format(html_widget),
  '              Use GNOME: @0@'.format(gnome_desktop),
  '           Use Canberra: @0@'.format(canberra),
  '          Use Autocrypt: @0@'.format(autocrypt),
  '       System Tray Icon: @0@'.format(systray),
  '      LDAP address book: @0@'.format(ldap),
  ' Use GSS authentication: @0@'.format(gss),
  '       GPE address book: @0@'.format(gpe),
  '   CardDAV address book: @0@'.format(webdav),
  '          Spell checker: @0@'.format(spell_checker),
  '      Use GtkSourceView: @0@'.format(gtksourceview),
  '           Use Compface: @0@'.format(compface),
  '          Use libsecret: @0@'.format(libsecret),
  '                Use gcr: @0@'.format(gcr),
  '     Install help files: @0@'.format(help_files),
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
