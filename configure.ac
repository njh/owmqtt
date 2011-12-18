# Process this file with autoconf to produce a configure script.
AC_INIT([owmqtt], [0.1], [njh@aelius.com])
AC_CONFIG_SRCDIR([owmqtt.c])
AC_CONFIG_AUX_DIR([build-scripts])
AC_CONFIG_MACRO_DIR([build-scripts])
AM_INIT_AUTOMAKE

# Check for C compiler
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries
AC_CHECK_LIB([owcapi], [OW_init],, AC_ERROR([Misssing owfs library]))
AC_CHECK_LIB([mosquitto], [mosquitto_lib_init],, AC_ERROR([Misssing mosquitto client library]))

# Check for headers
AC_CHECK_HEADERS([owcapi.h], [], [AC_ERROR([Missing owfs header file])])
AC_CHECK_HEADERS([mosquitto.h], [], [AC_ERROR([Missing mosquitto header file])])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
