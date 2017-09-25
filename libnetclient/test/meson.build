# libnetclient/test/meson.build

if libnetclient_test

  test_flags    = ['-DNCAT="' + ncat + '"',
                   '-DSED="'  + sed  + '"',
                   '-fprofile-arcs',
                   '-ftest-coverage',
                   '-g',
                   '-Wno-error']
  link_arg      = '-lgcov'
  lcov_flags    = ['--rc',
                   'lcov_branch_coverage=1']
  genhtml_flags = ['--function-coverage',
                   '--branch-coverage',
                   '--num-spaces', '4']
  valgr_flags   = ['--tool=memcheck',
                   '--leak-check=full',
                   '--track-fds=yes',
                   '--child-silent-after-fork=yes']

  test_compile_dep = declare_dependency(compile_args : test_flags,
                                        link_args    : link_arg)
  test_executable  = executable('tests', 'tests.c',
                                include_directories : [top_include, libnetclient_include],
                                link_with           : libnetclient_a,
                                dependencies        : libnetclient_deps + [test_compile_dep],
                                install             : false)
  test('libnetclient-test', test_executable)
  # now run the test with:
  #   meson test --wrap='valgrind --tool=memcheck --leak-check=full --track-fds=yes --child-silent-after-fork=yes --suppressions=valgrind.supp' libnetclient-test
  #
  # To enable coverage measurements:
  # If you enable coverage measurements by giving Meson
  # the command line flag -Db_coverage=true, you can generate coverage reports.
  # Meson will autodetect what coverage generator tools you have installed
  # and will generate the corresponding targets.
  # These targets are coverage-xml and coverage-text which are both provided by Gcovr
  # and coverage-html, which requires Lcov and GenHTML.

  # Or just use this code, munged from Makefile.am:
  source_path = join_paths(meson.source_root(), meson.current_source_dir())
  build_path  = meson.current_build_dir()

  # valgrind
  vg_path    = join_paths(build_path, 'vg')
  supp_path  = join_paths(source_path, 'valgrind.supp')
  tests_path = join_paths(build_path, 'tests')
  run_command(valgrind, [valgr_flags,
                         '--suppressions=' + supp_path,
                         '--log-file=' + vg_path,
                         tests_path])

  # lcov
  # Note: the following hack is needed so lcov recognises the paths of the sources...
  libsrcdir = join_paths(source_path, '..')
  lcov_out_path = join_paths(build_path, 'tests.covi')
  run_command(lcov, ['-c', '-b', libsrcdir, '-d', source_path, '--no-external', '-o', lcov_out_path])
  run_command(lcov, ['-r', lcov_out_path, 'tests.c', '-o', lcov_out_path])

  # genhtml
  html_out_path = join_paths(build_path, 'gcov')
  run_command(genhtml, [genhtml_flags, '-o', html_out_path, lcov_out_path])

endif # libnetclient_test
