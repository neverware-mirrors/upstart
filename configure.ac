# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([upstart], [1.2], [upstart-devel@lists.ubuntu.com])
NIH_COPYRIGHT([[Copyright © 2011 Scott James Remnant, Google Inc., Canonical Ltd.]])
AC_CONFIG_SRCDIR([init/main.c])
AC_CONFIG_MACRO_DIR([m4])

AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.10 gnu nostdinc check-news color-tests silent-rules])
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE([enable])

LT_PREREQ(2.2.4)
LT_INIT

AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT()

# Checks for programs.
AC_PROG_CC
PKG_PROG_PKG_CONFIG([0.22])

AC_PATH_PROG([NIH_DBUS_TOOL], [nih-dbus-tool])

# Checks for libraries.
NIH_WITH_LOCAL_LIBNIH

PKG_CHECK_MODULES([NIH], [libnih >= 1.0.2])
PKG_CHECK_MODULES([NIH_DBUS], [libnih-dbus >= 1.0.0])
PKG_CHECK_MODULES([DBUS], [dbus-1 >= 1.2.16])
PKG_CHECK_MODULES([UDEV], [libudev >= 146], [have_udev=yes], [have_udev=no])

AM_CONDITIONAL([HAVE_UDEV], [test "$have_udev" = yes])

AC_MSG_CHECKING([wether to add a keyring for ext4 crypto])
AC_ARG_WITH(
	[dircrypto-keyring],
	[AS_HELP_STRING(
		[--with-dircrypto-keyring],
		[add crypto keyring for directory encryption])],
	[],
	[with_dircrypto_keyring=auto]
)

KEYUTILS_LIBS=""

AS_IF(
	[test "$with_dircrypto_keyring" != "no"],
	[
		AC_CHECK_HEADER([keyutils.h],
			[AC_CHECK_LIB([keyutils], [keyctl_read], [have_keyutils="yes"])]
		)
		if test "$have_keyutils" != "yes"; then
			if test "$with_dircrypto_keyring" = "yes"; then
				AC_MSG_ERROR([The keyutils library (or developer files) was not found])
			fi
		else
			KEYUTILS_LIBS="-lkeyutils"
			AC_DEFINE([ADD_DIRCRYPTO_RING], 1, [Use keyutils and kernel keyring])
		fi
	]
)

AC_SUBST(KEYUTILS_LIBS)

# Checks for header files.
AC_CHECK_HEADERS([valgrind/valgrind.h])

AC_ARG_ENABLE(selinux,
	AS_HELP_STRING([--enable-selinux], [enable SELinux support]),
	[], [enable_selinux=no])

if test "x$enable_selinux" = "xyes" ; then
	PKG_CHECK_MODULES(SELINUX, [libselinux])
	AC_DEFINE(HAVE_SELINUX, 1, [Define if we have SELinux])
	AC_DEFINE(RESTORE_PATHS, [{"/dev", "/run"}], [Define restorecon paths])
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_PROG_CC_C99
AM_PROG_CC_C_O
NIH_C_THREAD

# Checks for library functions.

# Other checks
AC_MSG_CHECKING([whether to include sbindir in PATH])
eval upstart_sbindir=\"$sbindir\"
case $upstart_sbindir in
    /sbin|//sbin|/usr/sbin|/usr/local/sbin)
    	AC_MSG_RESULT([no])
	;;
    *)
    	AC_MSG_RESULT([yes])
	AC_DEFINE_UNQUOTED([EXTRA_PATH], ["$upstart_sbindir"],
			   [Directory to append to path.])
	;;
esac

NIH_COMPILER_WARNINGS
NIH_COMPILER_OPTIMISATIONS
NIH_COMPILER_COVERAGE

NIH_LINKER_OPTIMISATIONS


AC_CONFIG_FILES([ Makefile intl/Makefile
		  dbus/Makefile init/Makefile util/Makefile conf/Makefile
		  extra/Makefile doc/Makefile contrib/Makefile po/Makefile.in ])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
