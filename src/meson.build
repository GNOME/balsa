# src/meson.build

balsa_base_srclist = [
  'address-book-config.c',
  'address-book-config.h',
  'ab-window.c',
  'ab-window.h',
  'balsa-app.c',
  'balsa-app.h',
  'balsa-icons.c',
  'balsa-icons.h',
  'balsa-index.c',
  'balsa-index.h',
  'balsa-mblist.c',
  'balsa-mblist.h',
  'balsa-message.c',
  'balsa-message.h',
  'balsa-mime-widget.c',
  'balsa-mime-widget.h',
  'balsa-mime-widget-callbacks.c',
  'balsa-mime-widget-callbacks.h',
  'balsa-mime-widget-crypto.c',
  'balsa-mime-widget-crypto.h',
  'balsa-mime-widget-image.c',
  'balsa-mime-widget-image.h',
  'balsa-mime-widget-message.c',
  'balsa-mime-widget-message.h',
  'balsa-mime-widget-multipart.c',
  'balsa-mime-widget-multipart.h',
  'balsa-mime-widget-text.c',
  'balsa-mime-widget-text.h',
  'balsa-mime-widget-vcalendar.c',
  'balsa-mime-widget-vcalendar.h',
  'balsa-cite-bar.c',
  'balsa-cite-bar.h',
  'filter-edit-callbacks.c',
  'filter-edit-dialog.c',
  'filter-edit.h',
  'filter-export-callbacks.c',
  'filter-export-dialog.c',
  'filter-export.h',
  'filter-run-callbacks.c',
  'filter-run-dialog.c',
  'filter-run.h',
  'folder-conf.c',
  'folder-conf.h',
  'information-dialog.c',
  'information-dialog.h',
  'mailbox-conf.c',
  'mailbox-conf.h',
  'mailbox-node.c',
  'mailbox-node.h',
  'main-window.c',
  'main-window.h',
  'main.c',
  'message-window.c',
  'message-window.h',
  'pref-manager.c',
  'pref-manager.h',
  'print.h',
  'save-restore.c',
  'save-restore.h',
  'sendmsg-window.c',
  'sendmsg-window.h',
  'store-address.c',
  'store-address.h',
  'toolbar-factory.c',
  'toolbar-factory.h',
  'toolbar-prefs.c',
  'toolbar-prefs.h',
  'xdg-folders.c',
  'xdg-folders.h'
  ]

balsa_print_source = [
  'print-gtk.c',
  'balsa-print-object.c',
  'balsa-print-object.h',
  'balsa-print-object-decor.c',
  'balsa-print-object-decor.h',
  'balsa-print-object-default.c',
  'balsa-print-object-default.h',
  'balsa-print-object-header.c',
  'balsa-print-object-header.h',
  'balsa-print-object-html.c',
  'balsa-print-object-html.h',
  'balsa-print-object-image.c',
  'balsa-print-object-image.h',
  'balsa-print-object-text.c',
  'balsa-print-object-text.h'
  ]

balsa_sources = balsa_base_srclist + balsa_print_source
if spell_checker == 'internal'
  balsa_spell_extra = [
    'quote-color.c',
    'quote-color.h',
    'spell-check.c',
    'spell-check.h'
    ]
  balsa_sources += balsa_spell_extra
endif

locale_dir = join_paths(get_option('prefix'), get_option('datadir'), 'locale')
balsa_compile_arg = '-DGNOMELOCALEDIR="' + locale_dir + '"'

balsa_compile_dep = declare_dependency(compile_args : balsa_compile_arg)
balsa_libs = [libinit_balsa_a, libbalsa_a, libimap_a, libnetclient_a]

executable('balsa', balsa_sources, balsa_resources,
           dependencies        : balsa_deps + [balsa_compile_dep],
           include_directories : [top_include, libbalsa_include, libnetclient_include],
           link_with           : balsa_libs,
           install             : true)

balsa_ab_sources = [
  'ab-main.c',
  'address-book-config.c'
  ]

balsa_ab_compile_arg = '-DGNOMELOCALEDIR="' + locale_dir + '"'
balsa_ab_compile_dep = declare_dependency(compile_args : balsa_ab_compile_arg)
executable('balsa-ab', balsa_ab_sources, balsa_ab_resources,
           dependencies        : balsa_ab_deps + [balsa_ab_compile_dep],
           include_directories : [top_include, libbalsa_include],
           link_with           : libbalsa_a,
           install             : true)
