/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifndef PHP_STRINGUTILS_H
#define PHP_STRINGUTILS_H

extern zend_module_entry stringutils_module_entry;
#define phpext_stringutils_ptr &stringutils_module_entry

#define PHP_STRINGUTILS_VERSION "0.1.0" /* Replace with version number for your extension */

#ifdef PHP_WIN32
#	define PHP_STRINGUTILS_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHP_STRINGUTILS_API __attribute__ ((visibility("default")))
#else
#	define PHP_STRINGUTILS_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(stringutils);
PHP_MSHUTDOWN_FUNCTION(stringutils);
PHP_RINIT_FUNCTION(stringutils);
PHP_RSHUTDOWN_FUNCTION(stringutils);
PHP_MINFO_FUNCTION(stringutils);

PHP_FUNCTION(str_check_encoding);
PHP_FUNCTION(str_scrub);
PHP_FUNCTION(len);
PHP_FUNCTION(str_to_array);
PHP_FUNCTION(str_each_char);
PHP_FUNCTION(str_take_while);

/* 
  	Declare any global variables you may need between the BEGIN
	and END macros here:     

ZEND_BEGIN_MODULE_GLOBALS(stringutils)
	long  global_value;
	char *global_string;
ZEND_END_MODULE_GLOBALS(stringutils)
*/

/* In every utility function you add that needs to use variables 
   in php_stringutils_globals, call TSRMLS_FETCH(); after declaring other 
   variables used by that function, or better yet, pass in TSRMLS_CC
   after the last function argument and declare your utility function
   with TSRMLS_DC after the last declared argument.  Always refer to
   the globals in your function as STRINGUTILS_G(variable).  You are 
   encouraged to rename these macros something shorter, see
   examples in any other php module directory.
*/

#ifdef ZTS
#define STRINGUTILS_G(v) TSRMG(stringutils_globals_id, zend_stringutils_globals *, v)
#else
#define STRINGUTILS_G(v) (stringutils_globals.v)
#endif

#endif	/* PHP_STRINGUTILS_H */

/* https://github.com/php/php-src/blob/master/ext/standard/html_tables.h */

enum entity_charset { cs_utf_8, cs_8859_1, cs_cp1252, cs_8859_15, cs_cp1251,
            cs_8859_5, cs_cp866, cs_macroman, cs_koi8r, cs_big5,
            cs_gb2312, cs_big5hkscs, cs_sjis, cs_eucjp,
            cs_numelems /* used to count the number of charsets */
          };

static const struct {
  const char *codeset;
  enum entity_charset charset;
} charset_map[] = {
  { "ISO-8859-1",   cs_8859_1 },
  { "ISO8859-1",    cs_8859_1 },
  { "ISO-8859-15",  cs_8859_15 },
  { "ISO8859-15",   cs_8859_15 },
  { "utf-8",      cs_utf_8 },
  { "cp1252",     cs_cp1252 },
  { "Windows-1252", cs_cp1252 },
  { "1252",     cs_cp1252 }, 
  { "BIG5",     cs_big5 },
  { "950",      cs_big5 },
  { "GB2312",     cs_gb2312 },
  { "936",      cs_gb2312 },
  { "BIG5-HKSCS",   cs_big5hkscs },
  { "Shift_JIS",    cs_sjis },
  { "SJIS",     cs_sjis },
  { "932",      cs_sjis },
  { "SJIS-win",   cs_sjis },
  { "CP932",      cs_sjis },
  { "EUCJP",      cs_eucjp },
  { "EUC-JP",     cs_eucjp },
  { "eucJP-win",    cs_eucjp },
  { "KOI8-R",     cs_koi8r },
  { "koi8-ru",    cs_koi8r },
  { "koi8r",      cs_koi8r },
  { "cp1251",     cs_cp1251 },
  { "Windows-1251", cs_cp1251 },
  { "win-1251",   cs_cp1251 },
  { "iso8859-5",    cs_8859_5 },
  { "iso-8859-5",   cs_8859_5 },
  { "cp866",      cs_cp866 },
  { "866",      cs_cp866 },    
  { "ibm866",     cs_cp866 },
  { "MacRoman",   cs_macroman },
  { NULL }
};


static unsigned int get_next_char(
    enum entity_charset,
    const unsigned char *,
    size_t,
    size_t*,
    int*
);

static enum entity_charset determine_charset(char *charset_hint TSRMLS_DC);

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
