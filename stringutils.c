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
  | Author: Masaki Kagaya                                                |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_stringutils.h"
#include "ext/standard/php_smart_str.h"

#if HAVE_LOCALE_H
#include <locale.h>
#endif
#if PHP_WIN32
#include "config.w32.h"
#else
#include <php_config.h>
#endif

#include "SAPI.h"
#if HAVE_LANGINFO_H
#include <langinfo.h>
#endif

/* https://github.com/php/php-src/blob/master/ext/standard/html.c */

#define MB_FAILURE(pos, advance) do { \
  *cursor = pos + (advance); \
  *status = FAILURE; \
  return 0; \
} while (0)

#define CHECK_LEN(pos, chars_need) ((str_len - (pos)) >= (chars_need))

/* valid as single byte character or leading byte */
#define utf8_lead(c)  ((c) < 0x80 || ((c) >= 0xC2 && (c) <= 0xF4))
/* whether it's actually valid depends on other stuff;
 * this macro cannot check for non-shortest forms, surrogates or
 * code points above 0x10FFFF */
#define utf8_trail(c) ((c) >= 0x80 && (c) <= 0xBF)

#define gb2312_lead(c) ((c) != 0x8E && (c) != 0x8F && (c) != 0xA0 && (c) != 0xFF)
#define gb2312_trail(c) ((c) >= 0xA1 && (c) <= 0xFE)

#define sjis_lead(c) ((c) != 0x80 && (c) != 0xA0 && (c) < 0xFD)
#define sjis_trail(c) ((c) >= 0x40  && (c) != 0x7F && (c) < 0xFD)

/* If you declare any globals in php_stringutils.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(stringutils)
*/

/* True global resources - no need for thread safety here */
static int le_stringutils;

/* {{{ stringutils_functions[]
 *
 * Every user visible function must have an entry in stringutils_functions[].
 */
const zend_function_entry stringutils_functions[] = {
	PHP_FE(str_check_encoding,	NULL)
	PHP_FE(str_scrub,	NULL)
	PHP_FE_END	/* Must be the last line in stringutils_functions[] */
};
/* }}} */

/* {{{ stringutils_module_entry
 */
zend_module_entry stringutils_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"stringutils",
	stringutils_functions,
	PHP_MINIT(stringutils),
	PHP_MSHUTDOWN(stringutils),
	PHP_RINIT(stringutils),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(stringutils),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(stringutils),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_STRINGUTILS_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_STRINGUTILS
ZEND_GET_MODULE(stringutils)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("stringutils.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_stringutils_globals, stringutils_globals)
    STD_PHP_INI_ENTRY("stringutils.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_stringutils_globals, stringutils_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_stringutils_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_stringutils_init_globals(zend_stringutils_globals *stringutils_globals)
{
	stringutils_globals->global_value = 0;
	stringutils_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(stringutils)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(stringutils)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(stringutils)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(stringutils)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(stringutils)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "stringutils support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

/* {{{ get_next_char
 */
static inline unsigned int get_next_char(
		enum entity_charset charset,
		const unsigned char *str,
		size_t str_len,
		size_t *cursor,
		int *status)
{
	size_t pos = *cursor;
	unsigned int this_char = 0;

	*status = SUCCESS;
	assert(pos <= str_len);

	if (!CHECK_LEN(pos, 1))
		MB_FAILURE(pos, 1);

	switch (charset) {
	case cs_utf_8:
		{
			/* We'll follow strategy 2. from section 3.6.1 of UTR #36:
			 * "In a reported illegal byte sequence, do not include any
			 *  non-initial byte that encodes a valid character or is a leading
			 *  byte for a valid sequence." */
			unsigned char c;
			c = str[pos];
			if (c < 0x80) {
				this_char = c;
				pos++;
			} else if (c < 0xc2) {
				MB_FAILURE(pos, 1);
			} else if (c < 0xe0) {
				if (!CHECK_LEN(pos, 2))
					MB_FAILURE(pos, 1);

				if (!utf8_trail(str[pos + 1])) {
					MB_FAILURE(pos, utf8_lead(str[pos + 1]) ? 1 : 2);
				}
				this_char = ((c & 0x1f) << 6) | (str[pos + 1] & 0x3f);
				if (this_char < 0x80) { /* non-shortest form */
					MB_FAILURE(pos, 2);
				}
				pos += 2;
			} else if (c < 0xf0) {
				size_t avail = str_len - pos;

				if (avail < 3 ||
						!utf8_trail(str[pos + 1]) || !utf8_trail(str[pos + 2])) {
					if (avail < 2 || utf8_lead(str[pos + 1]))
						MB_FAILURE(pos, 1);
					else if (avail < 3 || utf8_lead(str[pos + 2]))
						MB_FAILURE(pos, 2);
					else
						MB_FAILURE(pos, 3);
				}

				this_char = ((c & 0x0f) << 12) | ((str[pos + 1] & 0x3f) << 6) | (str[pos + 2] & 0x3f);
				if (this_char < 0x800) { /* non-shortest form */
					MB_FAILURE(pos, 3);
				} else if (this_char >= 0xd800 && this_char <= 0xdfff) { /* surrogate */
					MB_FAILURE(pos, 3);
				}
				pos += 3;
			} else if (c < 0xf5) {
				size_t avail = str_len - pos;

				if (avail < 4 ||
						!utf8_trail(str[pos + 1]) || !utf8_trail(str[pos + 2]) ||
						!utf8_trail(str[pos + 3])) {
					if (avail < 2 || utf8_lead(str[pos + 1]))
						MB_FAILURE(pos, 1);
					else if (avail < 3 || utf8_lead(str[pos + 2]))
						MB_FAILURE(pos, 2);
					else if (avail < 4 || utf8_lead(str[pos + 3]))
						MB_FAILURE(pos, 3);
					else
						MB_FAILURE(pos, 4);
				}
				
				this_char = ((c & 0x07) << 18) | ((str[pos + 1] & 0x3f) << 12) | ((str[pos + 2] & 0x3f) << 6) | (str[pos + 3] & 0x3f);
				if (this_char < 0x10000 || this_char > 0x10FFFF) { /* non-shortest form or outside range */
					MB_FAILURE(pos, 4);
				}
				pos += 4;
			} else {
				MB_FAILURE(pos, 1);
			}
		}
		break;

	case cs_big5:
		/* reference http://demo.icu-project.org/icu-bin/convexp?conv=big5 */
		{
			unsigned char c = str[pos];
			if (c >= 0x81 && c <= 0xFE) {
				unsigned char next;
				if (!CHECK_LEN(pos, 2))
					MB_FAILURE(pos, 1);

				next = str[pos + 1];

				if ((next >= 0x40 && next <= 0x7E) ||
						(next >= 0xA1 && next <= 0xFE)) {
					this_char = (c << 8) | next;
				} else {
					MB_FAILURE(pos, 1);
				}
				pos += 2;
			} else {
				this_char = c;
				pos += 1;
			}
		}
		break;

	case cs_big5hkscs:
		{
			unsigned char c = str[pos];
			if (c >= 0x81 && c <= 0xFE) {
				unsigned char next;
				if (!CHECK_LEN(pos, 2))
					MB_FAILURE(pos, 1);

				next = str[pos + 1];

				if ((next >= 0x40 && next <= 0x7E) ||
						(next >= 0xA1 && next <= 0xFE)) {
					this_char = (c << 8) | next;
				} else if (next != 0x80 && next != 0xFF) {
					MB_FAILURE(pos, 1);
				} else {
					MB_FAILURE(pos, 2);
				}
				pos += 2;
			} else {
				this_char = c;
				pos += 1;
			}
		}
		break;

	case cs_gb2312: /* EUC-CN */
		{
			unsigned char c = str[pos];
			if (c >= 0xA1 && c <= 0xFE) {
				unsigned char next;
				if (!CHECK_LEN(pos, 2))
					MB_FAILURE(pos, 1);

				next = str[pos + 1];

				if (gb2312_trail(next)) {
					this_char = (c << 8) | next;
				} else if (gb2312_lead(next)) {
					MB_FAILURE(pos, 1);
				} else {
					MB_FAILURE(pos, 2);
				}
				pos += 2;
			} else if (gb2312_lead(c)) {
				this_char = c;
				pos += 1;
			} else {
				MB_FAILURE(pos, 1);
			}
		}
		break;

	case cs_sjis:
		{
			unsigned char c = str[pos];
			if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
				unsigned char next;
				if (!CHECK_LEN(pos, 2))
					MB_FAILURE(pos, 1);

				next = str[pos + 1];

				if (sjis_trail(next)) {
					this_char = (c << 8) | next;
				} else if (sjis_lead(next)) {
					MB_FAILURE(pos, 1);
				} else {
					MB_FAILURE(pos, 2);
				}
				pos += 2;
			} else if (c < 0x80 || (c >= 0xA1 && c <= 0xDF)) {
				this_char = c;
				pos += 1;
			} else {
				MB_FAILURE(pos, 1);
			}
		}
		break;

	case cs_eucjp:
		{
			unsigned char c = str[pos];

			if (c >= 0xA1 && c <= 0xFE) {
				unsigned next;
				if (!CHECK_LEN(pos, 2))
					MB_FAILURE(pos, 1);
				next = str[pos + 1];

				if (next >= 0xA1 && next <= 0xFE) {
					/* this a jis kanji char */
					this_char = (c << 8) | next;
				} else {
					MB_FAILURE(pos, (next != 0xA0 && next != 0xFF) ? 1 : 2);
				}
				pos += 2;
			} else if (c == 0x8E) {
				unsigned next;
				if (!CHECK_LEN(pos, 2))
					MB_FAILURE(pos, 1);

				next = str[pos + 1];
				if (next >= 0xA1 && next <= 0xDF) {
					/* JIS X 0201 kana */
					this_char = (c << 8) | next;
				} else {
					MB_FAILURE(pos, (next != 0xA0 && next != 0xFF) ? 1 : 2);
				}
				pos += 2;
			} else if (c == 0x8F) {
				size_t avail = str_len - pos;

				if (avail < 3 || !(str[pos + 1] >= 0xA1 && str[pos + 1] <= 0xFE) ||
						!(str[pos + 2] >= 0xA1 && str[pos + 2] <= 0xFE)) {
					if (avail < 2 || (str[pos + 1] != 0xA0 && str[pos + 1] != 0xFF))
						MB_FAILURE(pos, 1);
					else if (avail < 3 || (str[pos + 2] != 0xA0 && str[pos + 2] != 0xFF))
						MB_FAILURE(pos, 2);
					else
						MB_FAILURE(pos, 3);
				} else {
					/* JIS X 0212 hojo-kanji */
					this_char = (c << 16) | (str[pos + 1] << 8) | str[pos + 2];
				}
				pos += 3;
			} else if (c != 0xA0 && c != 0xFF) {
				/* character encoded in 1 code unit */
				this_char = c;
				pos += 1;
			} else {
				MB_FAILURE(pos, 1);
			}
		}
		break;
	default:
		/* single-byte charsets */
		this_char = str[pos++];
		break;
	}

	*cursor = pos;
  	return this_char;
}
/* }}} */

/* {{{ entity_charset determine_charset
 * returns the charset identifier based on current locale or a hint.
 * defaults to UTF-8 */
static enum entity_charset determine_charset(char *charset_hint TSRMLS_DC)
{
	int i;
	enum entity_charset charset = cs_utf_8;
	int len = 0;
	const zend_encoding *zenc;

	/* Default is now UTF-8 */
	if (charset_hint == NULL)
		return cs_utf_8;

	if ((len = strlen(charset_hint)) != 0) {
		goto det_charset;
	}

	zenc = zend_multibyte_get_internal_encoding(TSRMLS_C);
	if (zenc != NULL) {
		charset_hint = (char *)zend_multibyte_get_encoding_name(zenc);
		if (charset_hint != NULL && (len=strlen(charset_hint)) != 0) {
			if ((len == 4) /* sizeof (none|auto|pass) */ &&
					(!memcmp("pass", charset_hint, 4) ||
					 !memcmp("auto", charset_hint, 4) ||
					 !memcmp("auto", charset_hint, 4))) {
				charset_hint = NULL;
				len = 0;
			} else {
				goto det_charset;
			}
		}
	}

	charset_hint = SG(default_charset);
	if (charset_hint != NULL && (len=strlen(charset_hint)) != 0) {
		goto det_charset;
	}

	/* try to detect the charset for the locale */
#if HAVE_NL_LANGINFO && HAVE_LOCALE_H && defined(CODESET)
	charset_hint = nl_langinfo(CODESET);
	if (charset_hint != NULL && (len=strlen(charset_hint)) != 0) {
		goto det_charset;
	}
#endif

#if HAVE_LOCALE_H
	/* try to figure out the charset from the locale */
	{
		char *localename;
		char *dot, *at;

		/* lang[_territory][.codeset][@modifier] */
		localename = setlocale(LC_CTYPE, NULL);

		dot = strchr(localename, '.');
		if (dot) {
			dot++;
			/* locale specifies a codeset */
			at = strchr(dot, '@');
			if (at)
				len = at - dot;
			else
				len = strlen(dot);
			charset_hint = dot;
		} else {
			/* no explicit name; see if the name itself
			 * is the charset */
			charset_hint = localename;
			len = strlen(charset_hint);
		}
	}
#endif

det_charset:

	if (charset_hint) {
		int found = 0;
		
		/* now walk the charset map and look for the codeset */
		for (i = 0; charset_map[i].codeset; i++) {
			if (len == strlen(charset_map[i].codeset) && strncasecmp(charset_hint, charset_map[i].codeset, len) == 0) {
				charset = charset_map[i].charset;
				found = 1;
				break;
			}
		}
		if (!found) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "charset `%s' not supported, assuming utf-8",
					charset_hint);
		}
	}
	return charset;
}
/* }}} */


PHP_FUNCTION(str_check_encoding)
{
    char *str;
    int size;
    char *charset_hint;
    size_t charset_hint_size;
    enum entity_charset charset;
    size_t cursor = 0;
    int status = 0;
    unsigned int this_char;

    charset_hint = "UTF-8";

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &str, &size, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    charset = determine_charset(charset_hint TSRMLS_CC);

    while (cursor < size) {
        this_char = get_next_char(charset, (const unsigned char *) str, size, &cursor, &status);

        if (status == FAILURE) {
            RETURN_FALSE;
        }

    }

    RETURN_TRUE;
}


PHP_FUNCTION(str_scrub)
{
    char *str;
    int size;
    char *charset_hint;
    size_t charset_hint_size;
    enum entity_charset charset;
    size_t pos = 0;
    size_t next_pos = 0;
    int status = 0;
    unsigned int this_char;
    smart_str buf = {0};
    char *substitute; 
    int substitute_size;

    charset_hint = "UTF-8";

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &str, &size, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    charset = determine_charset(charset_hint TSRMLS_CC);

    if (strncasecmp(charset_hint, "UTF-8", charset_hint_size) == 0) {
        substitute_size = 3;
        substitute = calloc(substitute_size, sizeof(char)); 
        strncpy(substitute, "\xEF\xBF\xBD", substitute_size);
    } else {
        substitute_size = 1;
        substitute = calloc(substitute_size, sizeof(char));
        strncpy(substitute, "\x3F", substitute_size);
    }

    while (next_pos < size) {
        pos = next_pos;
        this_char = get_next_char(charset, (const unsigned char *) str, size, &next_pos, &status);

        if (status == SUCCESS) {
            smart_str_appendl(&buf, str + pos, next_pos - pos);
        } else {
            smart_str_appendl(&buf, substitute, substitute_size); 
        }
    }

    smart_str_0(&buf);
    RETURN_STRINGL(buf.c, buf.len, 0);
    smart_str_free(&buf);
}
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
