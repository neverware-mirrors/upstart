# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([upstart], [0.6.3], [upstart-devel@lists.ubuntu.com])
AC_COPYRIGHT([[Copyright © 2009 Canonical Ltd.]])
AC_CONFIG_SRCDIR([init/main.c])
AC_CONFIG_MACRO_DIR([m4])

AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.10 gnu nostdinc check-news dist-bzip2])

LT_PREREQ(2.2.4)
LT_INIT

AM_GNU_GETTEXT_VERSION([0.16.1])
AM_GNU_GETTEXT()

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG

# Checks for libraries.
NIH_INIT([dbus])

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

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

PKG_CHECK_MODULES([UDEV], [libudev >= 146])


AM_CONDITIONAL(CROSS_COMPILING, test "x$cross_compiling" = xyes)
if test "x$cross_compiling" = xyes; then
	AR_FOR_BUILD=${AR_FOR_BUILD-ar}
	CC_FOR_BUILD=${CC_FOR_BUILD-gcc -std=gnu99}
	RANLIB_FOR_BUILD=${RANLIB_FOR_BUILD-ranlib}
	dnl This is unfortunately specific to the way dpkg-buildpackage
	dnl overrides pkg-config's library path, which makes it hard for
	dnl this patch to go upstream. In future we should have a
	dnl ${host}-pkg-config instead.
	m4_pattern_allow([^PKG_CONFIG_LIBDIR$])
	DBUS_CFLAGS_FOR_BUILD=`unset PKG_CONFIG_LIBDIR; pkg-config --cflags 'dbus-1 >= 1.2.16'`
	DBUS_LIBS_FOR_BUILD=`unset PKG_CONFIG_LIBDIR; pkg-config --libs 'dbus-1 >= 1.2.16'`
else
	AR_FOR_BUILD="\$(AR)"
	CC_FOR_BUILD="\$(CC)"
	RANLIB_FOR_BUILD="\$(RANLIB)"
	CFLAGS_FOR_BUILD=${CFLAGS_FOR_BUILD-$CFLAGS}
	LDFLAGS_FOR_BUILD=${LDFLAGS_FOR_BUILD-$LDFLAGS}
fi
AC_SUBST(AR_FOR_BUILD)
AC_SUBST(CC_FOR_BUILD)
AC_SUBST(RANLIB_FOR_BUILD)
AC_SUBST(CFLAGS_FOR_BUILD)
AC_SUBST(LDFLAGS_FOR_BUILD)
AC_SUBST(DBUS_CFLAGS_FOR_BUILD)
AC_SUBST(DBUS_LIBS_FOR_BUILD)


AC_CONFIG_FILES([ Makefile m4/Makefile intl/Makefile
		  nih/Makefile nih-dbus/Makefile nih-dbus-tool/Makefile
		  dbus/Makefile init/Makefile util/Makefile udev/Makefile conf/Makefile
		  doc/Makefile contrib/Makefile po/Makefile.in ])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
