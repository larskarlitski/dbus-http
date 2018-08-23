# ------------------------------------------------------------------------------
# autotools

SHELL = bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

CLEANFILES = $(BUILT_SOURCES)
bin_PROGRAMS =
check_PROGRAMS =
noinst_LIBRARIES =
EXTRA_DIST =

INSTALL_EXEC_HOOKS =
UNINSTALL_EXEC_HOOKS =
.PHONY: $(INSTALL_EXEC_HOOKS) $(UNINSTALL_EXEC_HOOKS)
install-exec-hook: $(INSTALL_EXEC_HOOKS)
uninstall-hook: $(UNINSTALL_DATA_HOOKS) $(UNINSTALL_EXEC_HOOKS)

AM_CPPFLAGS = \
	-include $(top_builddir)/build/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)


data_edit = sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g'



# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	dbus-http

dbus_http_SOURCES = \
	src/main.c \
	src/dbus-http.h \
	src/dbus-http.c \
	src/http-server.h \
	src/http-server.c \
	src/json.h \
	src/json.c \
	src/dbus.h \
	src/dbus.c\
	src/log.c \
	src/log.h \
	environment.h \
	systemd-compat.h

dbus_http_CFLAGS = \
	$(AM_CFLAGS) \
	$(EXPAT_CFLAGS) \
	$(MICROHTTPD_CFLAGS) \
	$(SYSTEMD_CFLAGS)

dbus_http_LDADD = \
	$(EXPAT_LIBS) \
	$(MICROHTTPD_LIBS) \
	$(SYSTEMD_LIBS)

if HAVE_SYSTEMD
systemdsystemunit_DATA = data/dbus-http.service

EXTRA_DIST += \
	data/dbus-http.service.in

CLEANFILES += \
	data/dbus-http.service

data/dbus-http.service: $(srcdir)/data/dbus-http.service.in
	$(AM_V_GEN) mkdir -p data
	$(AM_V_GEN) $(data_edit) $< >$@
endif

install-tree: all
	rm -rf $(abs_builddir)/install-tree
	$(MAKE) install DESTDIR=$(abs_builddir)/install-tree
	tree $(abs_builddir)/install-tree
.PHONY: install-tree



# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	dbus-http-testd

dbus_http_testd_SOURCES = \
	test/dbus-http-testd.c

dbus_http_testd_CFLAGS = \
	$(AM_CFLAGS) \
	$(SYSTEMD_CFLAGS)

dbus_http_testd_LDADD = \
	$(SYSTEMD_LIBS)
