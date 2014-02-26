dnl $Id$
dnl config.m4 for extension stringutils

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

PHP_ARG_WITH(stringutils, for stringutils support,
Make sure that the comment is aligned:
[  --with-stringutils             Include stringutils support])

dnl Otherwise use enable:

dnl PHP_ARG_ENABLE(stringutils, whether to enable stringutils support,
dnl Make sure that the comment is aligned:
dnl [  --enable-stringutils           Enable stringutils support])

if test "$PHP_STRINGUTILS" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-stringutils -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/stringutils.h"  # you most likely want to change this
  dnl if test -r $PHP_STRINGUTILS/$SEARCH_FOR; then # path given as parameter
  dnl   STRINGUTILS_DIR=$PHP_STRINGUTILS
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for stringutils files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       STRINGUTILS_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$STRINGUTILS_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the stringutils distribution])
  dnl fi

  dnl # --with-stringutils -> add include path
  dnl PHP_ADD_INCLUDE($STRINGUTILS_DIR/include)

  dnl # --with-stringutils -> check for lib and symbol presence
  dnl LIBNAME=stringutils # you may want to change this
  dnl LIBSYMBOL=stringutils # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $STRINGUTILS_DIR/lib, STRINGUTILS_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_STRINGUTILSLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong stringutils lib version or lib not found])
  dnl ],[
  dnl   -L$STRINGUTILS_DIR/lib -lm
  dnl ])
  dnl
  dnl PHP_SUBST(STRINGUTILS_SHARED_LIBADD)

  PHP_NEW_EXTENSION(stringutils, stringutils.c, $ext_shared)
fi
