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

#define MB_FAILURE(advance) do { \
  buf_len = (advance); \
  *status = FAILURE; \
  return buf_len; \
} while (0)

#define CHECK_LEN(pos, chars_need) ((str_len - (pos)) >= (chars_need))

/* valid as single byte character or leading byte */
#define utf8_lead(c) ((c) < 0x80 || ((c) >= 0xC2 && (c) <= 0xF4))
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
	PHP_FE(str_scrub, NULL)
	PHP_FE(len,	NULL)
	PHP_FE(str_to_array, NULL)
	PHP_FE(str_each_char, NULL)
	PHP_FE(str_take_while, NULL)
	PHP_FE(str_drop_while, NULL)
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

typedef size_t (*next_char_func) (
    const unsigned char *,
    size_t,
    size_t,
    int*,
    unsigned int*
);

size_t utf8_next_char(
    const unsigned char *str,
    size_t str_len,
    size_t pos,
    int *status,
    unsigned int *cp)
{
    assert(pos <= str_len);
 
    *status = SUCCESS;

    size_t buf_len = 0;
    unsigned int this_char = 0;
    unsigned char c = str[pos];
 
    c = str[pos];

    if (c < 0x80) {
        this_char = c;
        buf_len = 1;
	} else if (c < 0xc2) {
        MB_FAILURE(1);
	} else if (c < 0xe0) {

        if (!CHECK_LEN(pos, 2)) {
            MB_FAILURE(1);
        }

        if (!utf8_trail(str[pos + 1])) {
            MB_FAILURE(utf8_lead(str[pos + 1]) ? 1 : 2);
        }

        this_char = ((c & 0x1f) << 6) | (str[pos + 1] & 0x3f);

        if (this_char < 0x80) { /* non-shortest form */
            MB_FAILURE(2);
        }

        buf_len = 2;
    } else if (c < 0xf0) {
        size_t avail = str_len - pos;

        if (avail < 3 ||
            !utf8_trail(str[pos + 1]) || !utf8_trail(str[pos + 2])) {
        if (avail < 2 || utf8_lead(str[pos + 1]))
            MB_FAILURE(1);
        } else if (avail < 3 || utf8_lead(str[pos + 2])) {
            MB_FAILURE(2);
        } else {
            MB_FAILURE(3);
        }

        this_char = ((c & 0x0f) << 12) | ((str[pos + 1] & 0x3f) << 6) | (str[pos + 2] & 0x3f);

        if (this_char < 0x800) { /* non-shortest form */
            MB_FAILURE(3);
        } else if (this_char >= 0xd800 && this_char <= 0xdfff) { /* surrogate */
            MB_FAILURE(3);
        }
        buf_len = 3;
    } else if (c < 0xf5) {
        size_t avail = str_len - pos;

        if (avail < 4 ||
            !utf8_trail(str[pos + 1]) || !utf8_trail(str[pos + 2]) ||
            !utf8_trail(str[pos + 3])
        ) {

		    if (avail < 2 || utf8_lead(str[pos + 1])) {
                MB_FAILURE(1);
            } else if (avail < 3 || utf8_lead(str[pos + 2])) {
                MB_FAILURE(2);
            } else if (avail < 4 || utf8_lead(str[pos + 3])) {
                MB_FAILURE(3);
            } else {
                MB_FAILURE(4);
            }			
        }
				
        this_char = ((c & 0x07) << 18) | ((str[pos + 1] & 0x3f) << 12) | ((str[pos + 2] & 0x3f) << 6) | (str[pos + 3] & 0x3f);

        if (this_char < 0x10000 || this_char > 0x10FFFF) {
            /* non-shortest form or outside range */
            MB_FAILURE(4);
        }

        buf_len = 4;
    } else {
        MB_FAILURE(1);
        buf_len = 1;
    }

    *cp = this_char;

    return buf_len;
}

size_t sjis_next_char(
    const unsigned char *str,
    size_t str_len,
    size_t pos,
    int *status,
    unsigned int *cp)
{

	unsigned int this_char = 0;
    size_t buf_len = 0;

	*status = SUCCESS;

	assert(pos <= str_len);

	if (!CHECK_LEN(pos, 1)) {
        MB_FAILURE(1);
	}

    unsigned char c = str[pos];

    if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
        unsigned char next;

        if (!CHECK_LEN(pos, 2)) {
            MB_FAILURE(1);
        }
					
        next = str[pos + 1];

        if (sjis_trail(next)) {
            this_char = (c << 8) | next;
        } else if (sjis_lead(next)) {
            MB_FAILURE(1);
        } else {
            MB_FAILURE(2);
        }

        buf_len = 2;
    } else if (c < 0x80 || (c >= 0xA1 && c <= 0xDF)) {
        *cp = c;
        buf_len = 1;
    } else {
       MB_FAILURE(1);
    }

  	return buf_len;
}

next_char_func get_next_func(const char* charset)
{
    static const struct {
       const char *name;
       next_char_func func;
    } next_funcs[] = {
      {"UTF-8", utf8_next_char},
      {"Shift_JIS", sjis_next_char},
      { NULL }
    };

    size_t name_length;
    int found = 0;
    next_char_func func;

    for (int i = 0; next_funcs[i].name; ++i) {
        name_length = strlen(next_funcs[i].name);

        if (strncasecmp((const char *) charset, next_funcs[i].name, name_length) == 0) {
        	func = next_funcs[i].func;
        	found = 1;
        	break;
        } 
 
    }
 
    if (!found) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "charset `%s' not supported, assuming utf-8",
        charset);
	}

    return func;
}

PHP_FUNCTION(str_check_encoding)
{
    char *str;
    int str_len;
    char *charset_hint;
    size_t charset_hint_size;

    next_char_func func = utf8_next_char;
    size_t pos = 0;
    size_t buf_len = 0;
    int status = 0;
    unsigned int cp = 0;

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &str, &str_len, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    func = get_next_func(charset_hint);

    while (pos < str_len) {
        buf_len = func((const unsigned char *) str, str_len, pos, &status, &cp);
        pos += buf_len;

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

    next_char_func func = utf8_next_char;
    size_t pos = 0;
    size_t buf_len = 0;
    int status = 0;

    smart_str buf = {0};
    char *substitute; 
    int substitute_size;

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &str, &size, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    func = get_next_func(charset_hint);

    if (strncasecmp(charset_hint, "UTF-8", charset_hint_size) == 0) {
        substitute_size = 3;
        substitute = calloc(substitute_size, sizeof(char)); 
        strncpy(substitute, "\xEF\xBF\xBD", substitute_size);
    } else {
        substitute_size = 1;
        substitute = calloc(substitute_size, sizeof(char));
        strncpy(substitute, "\x3F", substitute_size);
    }

    while (pos < size) {

        buf_len = func((const unsigned char *) str, size, pos, &status, NULL);

        if (status == SUCCESS) {
            smart_str_appendl(&buf, str + pos, buf_len);
        } else {
            smart_str_appendl(&buf, substitute, substitute_size); 
        }

        pos += buf_len;
    }

    smart_str_0(&buf);
    RETURN_STRINGL(buf.c, buf.len, 0);
    smart_str_free(&buf);
}

PHP_FUNCTION(len)
{
    char *str;
    int str_len;
    char *charset_hint = "UTF-8";
    size_t charset_hint_size;

    next_char_func func;
    size_t pos = 0;
    size_t buf_len = 0;
    int status = SUCCESS;
    unsigned int cp = 0;
    int len = 0;

    if (zend_parse_parameters(
            ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &str, &str_len, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    func = get_next_func((const char*) charset_hint);

    while (pos < str_len) {
        buf_len = func((const unsigned char *) str, str_len, pos, &status, &cp);
        pos += buf_len;
        ++len;
    }
 
    RETURN_LONG(len);
}

PHP_FUNCTION(str_to_array)
{
    char *str;
    int size;
    char *charset_hint;
    size_t charset_hint_size;

    next_char_func func;
    size_t pos = 0;
    size_t buf_len = 0;
    int status = 0;
    unsigned int cp = 0;

    if (zend_parse_parameters(
        ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &str, &size, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    func = get_next_func(charset_hint);

    array_init(return_value);

    while (pos < size) {
        buf_len = func((const unsigned char *) str, size, pos, &status, &cp);
        add_next_index_stringl(return_value, str + pos, buf_len, 1);
        pos += buf_len;
    }
}

PHP_FUNCTION(str_each_char)
{
    char *str;
    int size;
    char *charset_hint;
    size_t charset_hint_size;

    next_char_func func;
    size_t pos = 0;
    size_t buf_len = 0;
    int status = 0;
    unsigned int cp = 0;

    smart_str buf = {0};
    int index = 0;

    zend_fcall_info fci;
    zend_fcall_info_cache fci_cache;
    zval **params[2];
    zval *value = NULL;
    zval *key = NULL;
    zval *retval_ptr = NULL;

    if (zend_parse_parameters(
        ZEND_NUM_ARGS() TSRMLS_CC, "sf|s", &str, &size, &fci, &fci_cache, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    func = get_next_func(charset_hint);

    MAKE_STD_ZVAL(value);
    MAKE_STD_ZVAL(key);

    params[0] = &value;
    params[1] = &key;

    fci.param_count = 2;
    fci.params = params;
    fci.no_separation = 0;
    fci.retval_ptr_ptr = &retval_ptr;

    while (pos < size) {
        buf_len = func((const unsigned char *) str, size, pos, &status, &cp);

        ZVAL_STRINGL(value, str + pos, buf_len, 1);
        ZVAL_LONG(key, index);

        if (zend_call_function(&fci, &fci_cache TSRMLS_CC) == SUCCESS && fci.retval_ptr_ptr && *fci.retval_ptr_ptr) {
            convert_to_string_ex(&retval_ptr);
            smart_str_appendl(&buf, Z_STRVAL_P(retval_ptr), Z_STRLEN_P(retval_ptr));
        }

        pos += buf_len;
        ++index;

    }

    smart_str_0(&buf);
    RETVAL_STRINGL(buf.c, buf.len, 1);

    smart_str_free(&buf);

    if (retval_ptr) {
        zval_ptr_dtor(&retval_ptr);
    }

    zval_ptr_dtor(&value);
    zval_ptr_dtor(&key);
}

PHP_FUNCTION(str_take_while)
{
    char *str;
    int size;
    char *charset_hint;
    size_t charset_hint_size;

    next_char_func func;
    size_t pos = 0;
    size_t buf_len = 0;
    int status = 0;
    unsigned int cp = 0;

    smart_str buf = {0};
    int index = 0;

    zend_fcall_info fci;
    zend_fcall_info_cache fci_cache;
    zval **params[2];
    zval *value = NULL;
    zval *key = NULL;
    zval *retval_ptr = NULL;

    if (zend_parse_parameters(
        ZEND_NUM_ARGS() TSRMLS_CC, "sf|s", &str, &size, &fci, &fci_cache, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    func = get_next_func(charset_hint);

    MAKE_STD_ZVAL(value);
    MAKE_STD_ZVAL(key);

    params[0] = &value;
    params[1] = &key;

    fci.param_count = 2;
    fci.params = params;
    fci.no_separation = 0;
    fci.retval_ptr_ptr = &retval_ptr;

    while (pos < size) {
        buf_len = func((const unsigned char *) str, size, pos, &status, &cp);

        ZVAL_STRINGL(value, str + pos, buf_len, 1);
        ZVAL_LONG(key, index);

        if (zend_call_function(&fci, &fci_cache TSRMLS_CC) == SUCCESS
          && fci.retval_ptr_ptr && *fci.retval_ptr_ptr
        ) {

            if (zend_is_true(retval_ptr)) {
                break;
            }

            smart_str_appendl(&buf, str + pos, buf_len);  
        }

        pos += buf_len;
        ++index;
    }

    smart_str_0(&buf);
    RETVAL_STRINGL(buf.c, buf.len, 1);

    smart_str_free(&buf);

    if (retval_ptr) {
        zval_ptr_dtor(&retval_ptr);
    }

    zval_ptr_dtor(&value);
    zval_ptr_dtor(&key);
}

PHP_FUNCTION(str_drop_while)
{
    char *str;
    int size;
    char *charset_hint;
    size_t charset_hint_size;

    next_char_func func = utf8_next_char;
    size_t pos = 0;
    size_t buf_len = 0;
    int status = 0;
    unsigned int cp = 0;

    smart_str buf = {0};
    int index = 0;

    zend_fcall_info fci;
    zend_fcall_info_cache fci_cache;
    zval **params[2];
    zval *value = NULL;
    zval *key = NULL;
    zval *retval_ptr = NULL;

    zend_bool checked = false;

    if (zend_parse_parameters(
        ZEND_NUM_ARGS() TSRMLS_CC, "sf|s", &str, &size, &fci, &fci_cache, &charset_hint, &charset_hint_size) == FAILURE
    ) {
        return;
    }

    func = get_next_func(charset_hint);

    MAKE_STD_ZVAL(value);
    MAKE_STD_ZVAL(key);

    params[0] = &value;
    params[1] = &key;

    fci.param_count = 2;
    fci.params = params;
    fci.no_separation = 0;
    fci.retval_ptr_ptr = &retval_ptr;

    while (pos < size) {

        buf_len = func((const unsigned char *) str, size, pos, &status, &cp);

        ZVAL_STRINGL(value, str + pos, buf_len, 1);
        ZVAL_LONG(key, index);

        if (zend_call_function(&fci, &fci_cache TSRMLS_CC) == SUCCESS
          && fci.retval_ptr_ptr && *fci.retval_ptr_ptr
        ) {

            if (!checked && zend_is_true(retval_ptr)) {
                checked = true;
            }

            if (checked) {
                smart_str_appendl(&buf, str + pos, buf_len); 
            }
 
        }

        pos += buf_len;
        ++index;
    }

    smart_str_0(&buf);
    RETVAL_STRINGL(buf.c, buf.len, 1);

    smart_str_free(&buf);

    if (retval_ptr) {
        zval_ptr_dtor(&retval_ptr);
    }

    zval_ptr_dtor(&value);
    zval_ptr_dtor(&key);
}
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
