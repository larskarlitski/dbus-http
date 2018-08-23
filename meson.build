project('dbus-http', 'c',
        version : '0.2',
        license : 'Apache 2.0',
        default_options: [
                'c_std=gnu99',
                'prefix=/usr',
                'sysconfdir=/etc',
                'localstatedir=/var',
        ]
)


conf = configuration_data()
conf.set('_GNU_SOURCE', true)
conf.set('__SANE_USERSPACE_TYPES__', true)

# Generate a config header file
config_h = configure_file(output : 'dbus-http-config.h', configuration : conf)


cc = meson.get_compiler('c')
foreach arg : ['-Wextra',
               '-Werror=undef',
               '-Wlogical-op',
               '-Wmissing-include-dirs',
               '-Wold-style-definition',
               '-Wpointer-arith',
               '-Winit-self',
               '-Wdeclaration-after-statement',
               '-Wfloat-equal',
               '-Wsuggest-attribute=noreturn',
               '-Werror=missing-prototypes',
               '-Werror=implicit-function-declaration',
               '-Werror=missing-declarations',
               '-Werror=return-type',
               '-Werror=incompatible-pointer-types',
               '-Werror=format=2',
               '-Wstrict-prototypes',
               '-Wredundant-decls',
               '-Wmissing-noreturn',
               '-Wshadow',
               '-Wendif-labels',
               '-Wstrict-aliasing=2',
               '-Wwrite-strings',
               '-Werror=overflow',
               '-Wdate-time',
               '-Wnested-externs',
               '-ffast-math',
               '-fno-common',
               '-fdiagnostics-show-option',
               '-fno-strict-aliasing',
               '-fvisibility=hidden',
               '-fstack-protector',
               '-fstack-protector-strong',
               '-fPIE',
               '--param=ssp-buffer-size=4',
              ]
        if cc.has_argument(arg)
                add_project_arguments(arg, language : 'c')
        endif
endforeach

# "negative" arguments: gcc on purpose does not return an error for "-Wno-"
# arguments, just emits a warnings. So test for the "positive" version instead.
foreach arg : ['unused-parameter',
               'missing-field-initializers',
               'unused-result',
               'format-signedness',
               'error=nonnull', # work-around for gcc 7.1 turning this on on its own
              ]
        if cc.has_argument('-W' + arg)
                add_project_arguments('-Wno-' + arg, language : 'c')
        endif
endforeach


#### unit file ####
prefixdir = get_option('prefix')
conf_inst = configuration_data()
conf_inst.set('bindir', join_paths(prefixdir, get_option('bindir')))
systemunitdir = join_paths(prefixdir, 'lib/systemd/system')
configure_file(input : 'data/dbus-http.service.in',
	       output : 'dbus-http.service',
	       configuration : conf_inst,
	       install_dir : systemunitdir)

#### dbus-http ####
src = [
  'src/main.c',
  'src/dbus-http.h',
  'src/dbus-http.c',
  'src/http-server.h',
  'src/http-server.c',
  'src/json.h',
  'src/json.c',
  'src/dbus.h',
  'src/dbus.c',
  'src/log.c',
  'src/log.h',
  'src/environment.h',
  'src/systemd-compat.h'
]

dep_expat = dependency('expat')
dep_libmicrohttpd = dependency('libmicrohttpd')
dep_libsystemd = dependency('libsystemd')

executable('dbus-http',
  sources : src,
  c_args : ['-include', 'dbus-http-config.h'],
  dependencies : [dep_expat, dep_libmicrohttpd, dep_libsystemd],
  install : true
)


#### dbus-http-testd ####
src_testd = 'test/dbus-http-testd.c'

executable('dbus-http-testd',
  sources : src_testd,
  dependencies : [dep_libsystemd],
  install : false
)
