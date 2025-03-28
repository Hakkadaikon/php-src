/*
  +----------------------------------------------------------------------+
  | Copyright (c) The PHP Group                                          |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | https://www.php.net/license/3_01.txt                                 |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Rob Richards <rrichards@php.net>                             |
  |         Pierre-A. Joye <pajoye@php.net>                              |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "php.h"
#include "ext/standard/info.h"
#include "php_xmlwriter.h"
#include "php_xmlwriter_arginfo.h"
#include "zend_smart_str.h"

static zend_class_entry *xmlwriter_class_entry_ce;

typedef int (*xmlwriter_read_one_char_t)(xmlTextWriterPtr writer, const xmlChar *content);
typedef int (*xmlwriter_read_int_t)(xmlTextWriterPtr writer);

/* {{{ XMLWRITER_FROM_OBJECT */
#define XMLWRITER_FROM_OBJECT(ptr, object) \
	{ \
		ze_xmlwriter_object *obj = Z_XMLWRITER_P(object); \
		ptr = obj->ptr; \
		if (!ptr) { \
			zend_throw_error(NULL, "Invalid or uninitialized XMLWriter object"); \
			RETURN_THROWS(); \
		} \
	}
/* }}} */

static zend_object_handlers xmlwriter_object_handlers;

static zend_always_inline void xmlwriter_destroy_libxml_objects(ze_xmlwriter_object *intern)
{
	if (intern->ptr) {
		/* Note: this call will also free the output pointer. */
		xmlFreeTextWriter(intern->ptr);
		intern->ptr = NULL;
		intern->output = NULL;
	}
}

/* {{{{ xmlwriter_object_dtor */
static void xmlwriter_object_dtor(zend_object *object)
{
	ze_xmlwriter_object *intern = php_xmlwriter_fetch_object(object);

	/* freeing the resource here may leak, but otherwise we may use it after it has been freed */
	xmlwriter_destroy_libxml_objects(intern);
	zend_objects_destroy_object(object);
}
/* }}} */

/* {{{ xmlwriter_object_new */
static zend_object *xmlwriter_object_new(zend_class_entry *class_type)
{
	ze_xmlwriter_object *intern;

	intern = zend_object_alloc(sizeof(ze_xmlwriter_object), class_type);
	zend_object_std_init(&intern->std, class_type);
	object_properties_init(&intern->std, class_type);

	return &intern->std;
}
/* }}} */

#define XMLW_NAME_CHK(__arg_no, __subject) \
	if (xmlValidateName((xmlChar *) name, 0) != 0) {	\
		zend_argument_value_error(__arg_no, "must be a valid %s, \"%s\" given", __subject, name);	\
		RETURN_THROWS();	\
	}	\

/* {{{ function prototypes */
static PHP_MINIT_FUNCTION(xmlwriter);
static PHP_MINFO_FUNCTION(xmlwriter);
/* }}} */

/* _xmlwriter_get_valid_file_path should be made a
	common function in libxml extension as code is common to a few xml extensions */
/* {{{ _xmlwriter_get_valid_file_path */
static char *_xmlwriter_get_valid_file_path(char *source, char *resolved_path, int resolved_path_len ) {
	xmlURI *uri;
	xmlChar *escsource;
	char *file_dest;
	int isFileUri = 0;

	uri = xmlCreateURI();
	if (uri == NULL) {
		return NULL;
	}
	escsource = xmlURIEscapeStr((xmlChar *)source, (xmlChar *) ":");
	xmlParseURIReference(uri, (char *)escsource);
	xmlFree(escsource);

	if (uri->scheme != NULL) {
		/* absolute file uris - libxml only supports localhost or empty host */
		if (strncasecmp(source, "file:///", 8) == 0) {
			if (source[sizeof("file:///") - 1] == '\0') {
				xmlFreeURI(uri);
				return NULL;
			}
			isFileUri = 1;
#ifdef PHP_WIN32
			source += 8;
#else
			source += 7;
#endif
		} else if (strncasecmp(source, "file://localhost/",17) == 0) {
			if (source[sizeof("file://localhost/") - 1] == '\0') {
				xmlFreeURI(uri);
				return NULL;
			}

			isFileUri = 1;
#ifdef PHP_WIN32
			source += 17;
#else
			source += 16;
#endif
		}
	}

	if ((uri->scheme == NULL || isFileUri)) {
		char file_dirname[MAXPATHLEN];
		size_t dir_len;

		if (!VCWD_REALPATH(source, resolved_path) && !expand_filepath(source, resolved_path)) {
			xmlFreeURI(uri);
			return NULL;
		}

		memcpy(file_dirname, source, strlen(source));
		dir_len = zend_dirname(file_dirname, strlen(source));

		if (dir_len > 0) {
			zend_stat_t buf = {0};
			if (php_sys_stat(file_dirname, &buf) != 0) {
				xmlFreeURI(uri);
				return NULL;
			}
		}

		file_dest = resolved_path;
	} else {
		file_dest = source;
	}

	xmlFreeURI(uri);

	return file_dest;
}
/* }}} */

static void xml_writer_create_static(INTERNAL_FUNCTION_PARAMETERS, xmlTextWriterPtr writer, smart_str *output)
{
	if (object_init_with_constructor(return_value, Z_CE_P(ZEND_THIS), 0, NULL, NULL) == SUCCESS) {
		ze_xmlwriter_object *intern = Z_XMLWRITER_P(return_value);
		intern->ptr = writer;
		intern->output = output;
	} else {
		// output is freed by writer, so we don't need to free it here.
		xmlFreeTextWriter(writer);
	}
}

static const zend_module_dep xmlwriter_deps[] = {
	ZEND_MOD_REQUIRED("libxml")
	ZEND_MOD_END
};

/* {{{ xmlwriter_module_entry */
zend_module_entry xmlwriter_module_entry = {
	STANDARD_MODULE_HEADER_EX, NULL,
	xmlwriter_deps,
	"xmlwriter",
	ext_functions,
	PHP_MINIT(xmlwriter),
	NULL,
	NULL,
	NULL,
	PHP_MINFO(xmlwriter),
	PHP_XMLWRITER_VERSION,
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_XMLWRITER
ZEND_GET_MODULE(xmlwriter)
#endif

/* {{{ xmlwriter_objects_clone
static void xmlwriter_objects_clone(void *object, void **object_clone)
{
	TODO
}
}}} */

static void php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAMETERS, xmlwriter_read_one_char_t internal_function, char *subject_name)
{
	xmlTextWriterPtr ptr;
	char *name;
	size_t name_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Os", &self, xmlwriter_class_entry_ce, &name, &name_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	if (subject_name != NULL) {
		XMLW_NAME_CHK(2, subject_name);
	}

	retval = internal_function(ptr, (xmlChar *) name);

	RETURN_BOOL(retval != -1);
}

static void php_xmlwriter_end(INTERNAL_FUNCTION_PARAMETERS, xmlwriter_read_int_t internal_function)
{
	xmlTextWriterPtr ptr;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &self, xmlwriter_class_entry_ce) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	retval = internal_function(ptr);

	RETURN_BOOL(retval != -1);
}

/* {{{ Toggle indentation on/off - returns FALSE on error */
PHP_FUNCTION(xmlwriter_set_indent)
{
	xmlTextWriterPtr ptr;
	int retval;
	bool indent;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Ob", &self, xmlwriter_class_entry_ce, &indent) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	retval = xmlTextWriterSetIndent(ptr, indent);

	RETURN_BOOL(retval == 0);
}
/* }}} */

/* {{{ Set string used for indenting - returns FALSE on error */
PHP_FUNCTION(xmlwriter_set_indent_string)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterSetIndentString, NULL);
}
/* }}} */

/* {{{ Create start attribute - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_attribute)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterStartAttribute, "attribute name");
}
/* }}} */

/* {{{ End attribute - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_attribute)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndAttribute);
}
/* }}} */

/* {{{ Create start namespaced attribute - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_attribute_ns)
{
	xmlTextWriterPtr ptr;
	char *name, *prefix, *uri;
	size_t name_len, prefix_len, uri_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Os!ss!", &self, xmlwriter_class_entry_ce,
		&prefix, &prefix_len, &name, &name_len, &uri, &uri_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(3, "attribute name");

	retval = xmlTextWriterStartAttributeNS(ptr, (xmlChar *)prefix, (xmlChar *)name, (xmlChar *)uri);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Write full attribute - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_attribute)
{
	xmlTextWriterPtr ptr;
	char *name, *content;
	size_t name_len, content_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Oss", &self, xmlwriter_class_entry_ce,
		&name, &name_len, &content, &content_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(2, "attribute name");

	retval = xmlTextWriterWriteAttribute(ptr, (xmlChar *)name, (xmlChar *)content);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Write full namespaced attribute - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_attribute_ns)
{
	xmlTextWriterPtr ptr;
	char *name, *prefix, *uri, *content;
	size_t name_len, prefix_len, uri_len, content_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Os!ss!s", &self, xmlwriter_class_entry_ce,
		&prefix, &prefix_len, &name, &name_len, &uri, &uri_len, &content, &content_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(3, "attribute name");

	retval = xmlTextWriterWriteAttributeNS(ptr, (xmlChar *)prefix, (xmlChar *)name, (xmlChar *)uri, (xmlChar *)content);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Create start element tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_element)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterStartElement, "element name");
}
/* }}} */

/* {{{ Create start namespaced element tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_element_ns)
{
	xmlTextWriterPtr ptr;
	char *name, *prefix, *uri;
	size_t name_len, prefix_len, uri_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Os!ss!", &self, xmlwriter_class_entry_ce,
		&prefix, &prefix_len, &name, &name_len, &uri, &uri_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(3, "element name");

	retval = xmlTextWriterStartElementNS(ptr, (xmlChar *)prefix, (xmlChar *)name, (xmlChar *)uri);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ End current element - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_element)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndElement);
}
/* }}} */

/* {{{ End current element - returns FALSE on error */
PHP_FUNCTION(xmlwriter_full_end_element)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterFullEndElement);
}
/* }}} */

/* {{{ Write full element tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_element)
{
	xmlTextWriterPtr ptr;
	char *name, *content = NULL;
	size_t name_len, content_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Os|s!", &self, xmlwriter_class_entry_ce,
		&name, &name_len, &content, &content_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(2, "element name");

	if (!content) {
		retval = xmlTextWriterStartElement(ptr, (xmlChar *)name);
		if (retval == -1) {
			RETURN_FALSE;
		}
		retval = xmlTextWriterEndElement(ptr);
	} else {
		retval = xmlTextWriterWriteElement(ptr, (xmlChar *)name, (xmlChar *)content);
	}

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Write full namesapced element tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_element_ns)
{
	xmlTextWriterPtr ptr;
	char *name, *prefix, *uri, *content = NULL;
	size_t name_len, prefix_len, uri_len, content_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Os!ss!|s!", &self, xmlwriter_class_entry_ce,
		&prefix, &prefix_len, &name, &name_len, &uri, &uri_len, &content, &content_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(3, "element name");

	if (!content) {
		retval = xmlTextWriterStartElementNS(ptr,(xmlChar *)prefix, (xmlChar *)name, (xmlChar *)uri);
		if (retval == -1) {
			RETURN_FALSE;
		}
		retval = xmlTextWriterEndElement(ptr);
	} else {
		retval = xmlTextWriterWriteElementNS(ptr, (xmlChar *)prefix, (xmlChar *)name, (xmlChar *)uri, (xmlChar *)content);
	}

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Create start PI tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_pi)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterStartPI, "PI target");
}
/* }}} */

/* {{{ End current PI - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_pi)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndPI);
}
/* }}} */

/* {{{ Write full PI tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_pi)
{
	xmlTextWriterPtr ptr;
	char *name, *content;
	size_t name_len, content_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Oss", &self, xmlwriter_class_entry_ce,
		&name, &name_len, &content, &content_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(2, "PI target");

	retval = xmlTextWriterWritePI(ptr, (xmlChar *)name, (xmlChar *)content);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Create start CDATA tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_cdata)
{
	xmlTextWriterPtr ptr;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &self, xmlwriter_class_entry_ce) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	retval = xmlTextWriterStartCDATA(ptr);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ End current CDATA - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_cdata)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndCDATA);
}
/* }}} */

/* {{{ Write full CDATA tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_cdata)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterWriteCDATA, NULL);
}
/* }}} */

/* {{{ Write text - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_raw)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterWriteRaw, NULL);
}
/* }}} */

/* {{{ Write text - returns FALSE on error */
PHP_FUNCTION(xmlwriter_text)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterWriteString, NULL);
}
/* }}} */

/* {{{ Create start comment - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_comment)
{
	xmlTextWriterPtr ptr;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &self, xmlwriter_class_entry_ce) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	retval = xmlTextWriterStartComment(ptr);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Create end comment - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_comment)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndComment);
}
/* }}} */

/* {{{ Write full comment tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_comment)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterWriteComment, NULL);
}
/* }}} */

/* {{{ Create document tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_document)
{
	xmlTextWriterPtr ptr;
	char *version = NULL, *enc = NULL, *alone = NULL;
	size_t version_len, enc_len, alone_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O|s!p!s!", &self, xmlwriter_class_entry_ce, &version, &version_len, &enc, &enc_len, &alone, &alone_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	retval = xmlTextWriterStartDocument(ptr, version, enc, alone);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ End current document - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_document)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndDocument);
}
/* }}} */

/* {{{ Create start DTD tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_dtd)
{
	xmlTextWriterPtr ptr;
	char *name, *pubid = NULL, *sysid = NULL;
	size_t name_len, pubid_len, sysid_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Os|s!s!", &self, xmlwriter_class_entry_ce, &name, &name_len, &pubid, &pubid_len, &sysid, &sysid_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	retval = xmlTextWriterStartDTD(ptr, (xmlChar *)name, (xmlChar *)pubid, (xmlChar *)sysid);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ End current DTD - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_dtd)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndDTD);
}
/* }}} */

/* {{{ Write full DTD tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_dtd)
{
	xmlTextWriterPtr ptr;
	char *name, *pubid = NULL, *sysid = NULL, *subset = NULL;
	size_t name_len, pubid_len, sysid_len, subset_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Os|s!s!s!", &self, xmlwriter_class_entry_ce, &name, &name_len, &pubid, &pubid_len, &sysid, &sysid_len, &subset, &subset_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	retval = xmlTextWriterWriteDTD(ptr, (xmlChar *)name, (xmlChar *)pubid, (xmlChar *)sysid, (xmlChar *)subset);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Create start DTD element - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_dtd_element)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterStartDTDElement, "element name");
}
/* }}} */

/* {{{ End current DTD element - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_dtd_element)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndDTDElement);
}
/* }}} */

/* {{{ Write full DTD element tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_dtd_element)
{
	xmlTextWriterPtr ptr;
	char *name, *content;
	size_t name_len, content_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Oss", &self, xmlwriter_class_entry_ce,
		&name, &name_len, &content, &content_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(2, "element name");

	retval = xmlTextWriterWriteDTDElement(ptr, (xmlChar *)name, (xmlChar *)content);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Create start DTD AttList - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_dtd_attlist)
{
	php_xmlwriter_string_arg(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterStartDTDAttlist, "element name");
}
/* }}} */

/* {{{ End current DTD AttList - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_dtd_attlist)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndDTDAttlist);
}
/* }}} */

/* {{{ Write full DTD AttList tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_dtd_attlist)
{
	xmlTextWriterPtr ptr;
	char *name, *content;
	size_t name_len, content_len;
	int retval;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Oss", &self, xmlwriter_class_entry_ce,
		&name, &name_len, &content, &content_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(2, "element name");

	retval = xmlTextWriterWriteDTDAttlist(ptr, (xmlChar *)name, (xmlChar *)content);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Create start DTD Entity - returns FALSE on error */
PHP_FUNCTION(xmlwriter_start_dtd_entity)
{
	xmlTextWriterPtr ptr;
	char *name;
	size_t name_len;
	int retval;
	bool isparm;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Osb", &self, xmlwriter_class_entry_ce, &name, &name_len, &isparm) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(2, "attribute name");

	retval = xmlTextWriterStartDTDEntity(ptr, isparm, (xmlChar *)name);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ End current DTD Entity - returns FALSE on error */
PHP_FUNCTION(xmlwriter_end_dtd_entity)
{
	php_xmlwriter_end(INTERNAL_FUNCTION_PARAM_PASSTHRU, xmlTextWriterEndDTDEntity);
}
/* }}} */

/* {{{ Write full DTD Entity tag - returns FALSE on error */
PHP_FUNCTION(xmlwriter_write_dtd_entity)
{
	xmlTextWriterPtr ptr;
	char *name, *content;
	size_t name_len, content_len;
	int retval;
	/* Optional parameters */
	char *pubid = NULL, *sysid = NULL, *ndataid = NULL;
	bool pe = 0;
	size_t pubid_len, sysid_len, ndataid_len;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Oss|bs!s!s!", &self, xmlwriter_class_entry_ce,
		&name, &name_len, &content, &content_len, &pe, &pubid, &pubid_len,
		&sysid, &sysid_len, &ndataid, &ndataid_len) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	XMLW_NAME_CHK(2, "element name");

	retval = xmlTextWriterWriteDTDEntity(ptr, pe, (xmlChar *)name, (xmlChar *)pubid, (xmlChar *)sysid, (xmlChar *)ndataid, (xmlChar *)content);

	RETURN_BOOL(retval != -1);
}
/* }}} */

/* {{{ Create new xmlwriter using source uri for output */
PHP_FUNCTION(xmlwriter_open_uri)
{
	char *valid_file = NULL;
	xmlTextWriterPtr ptr;
	char *source;
	char resolved_path[MAXPATHLEN + 1];
	size_t source_len;
	zval *self = getThis();
	ze_xmlwriter_object *ze_obj = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "p", &source, &source_len) == FAILURE) {
		RETURN_THROWS();
	}

	if (self) {
		/* We do not use XMLWRITER_FROM_OBJECT, xmlwriter init function here */
		ze_obj = Z_XMLWRITER_P(self);
	}

	if (source_len == 0) {
		zend_argument_must_not_be_empty_error(1);
		RETURN_THROWS();
	}

	valid_file = _xmlwriter_get_valid_file_path(source, resolved_path, MAXPATHLEN);
	if (!valid_file) {
		php_error_docref(NULL, E_WARNING, "Unable to resolve file path");
		RETURN_FALSE;
	}

	ptr = xmlNewTextWriterFilename(valid_file, 0);

	if (!ptr) {
		RETURN_FALSE;
	}

	if (self) {
		xmlwriter_destroy_libxml_objects(ze_obj);
		ze_obj->ptr = ptr;
		ze_obj->output = NULL;
		RETURN_TRUE;
	} else {
		ze_obj = php_xmlwriter_fetch_object(xmlwriter_object_new(xmlwriter_class_entry_ce));
		ze_obj->ptr = ptr;
		ze_obj->output = NULL;
		RETURN_OBJ(&ze_obj->std);
	}
}
/* }}} */

PHP_METHOD(XMLWriter, toUri)
{
	char *source;
	size_t source_len;
	char resolved_path[MAXPATHLEN + 1];

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_PATH(source, source_len)
	ZEND_PARSE_PARAMETERS_END();

	if (source_len == 0) {
		zend_argument_must_not_be_empty_error(1);
		RETURN_THROWS();
	}

	const char *valid_file = _xmlwriter_get_valid_file_path(source, resolved_path, MAXPATHLEN);
	if (!valid_file) {
		zend_argument_value_error(1, "must resolve to a valid file path");
		RETURN_THROWS();
	}

	xmlTextWriterPtr writer = xmlNewTextWriterFilename(valid_file, 0);
	if (!writer) {
		zend_throw_error(NULL, "Could not construct libxml writer");
		RETURN_THROWS();
	}

	xml_writer_create_static(INTERNAL_FUNCTION_PARAM_PASSTHRU, writer, NULL);
}

static int xml_writer_stream_write_memory(void *context, const char *buffer, int len)
{
	smart_str *output = context;
	smart_str_appendl(output, buffer, len);
	return len;
}

static int xml_writer_stream_close_memory(void *context)
{
	smart_str *output = context;
	smart_str_free_ex(output, false);
	efree(output);
	return 0;
}

static xmlTextWriterPtr xml_writer_create_in_memory(smart_str **output_ptr)
{
	smart_str *output = emalloc(sizeof(*output));
	memset(output, 0, sizeof(*output));

	xmlOutputBufferPtr output_buffer = xmlOutputBufferCreateIO(xml_writer_stream_write_memory, xml_writer_stream_close_memory, output, NULL);
	if (output_buffer == NULL) {
		efree(output);
		return NULL;
	}

	xmlTextWriterPtr writer = xmlNewTextWriter(output_buffer);
	if (!writer) {
		/* This call will free output too. */
		xmlOutputBufferClose(output_buffer);
		return NULL;
	}
	*output_ptr = output;
	return writer;
}

/* {{{ Create new xmlwriter using memory for string output */
PHP_FUNCTION(xmlwriter_open_memory)
{
	zval *self = getThis();
	ze_xmlwriter_object *ze_obj = NULL;

	if (zend_parse_parameters_none() == FAILURE) {
		RETURN_THROWS();
	}

	if (self) {
		/* We do not use XMLWRITER_FROM_OBJECT, xmlwriter init function here */
		ze_obj = Z_XMLWRITER_P(self);
	}

	smart_str *output;
	xmlTextWriterPtr ptr = xml_writer_create_in_memory(&output);
	if (! ptr) {
		RETURN_FALSE;
	}

	if (self) {
		xmlwriter_destroy_libxml_objects(ze_obj);
		ze_obj->ptr = ptr;
		ze_obj->output = output;
		RETURN_TRUE;
	} else {
		ze_obj = php_xmlwriter_fetch_object(xmlwriter_object_new(xmlwriter_class_entry_ce));
		ze_obj->ptr = ptr;
		ze_obj->output = output;
		RETURN_OBJ(&ze_obj->std);
	}

}
/* }}} */

PHP_METHOD(XMLWriter, toMemory)
{
	ZEND_PARSE_PARAMETERS_NONE();

	smart_str *output;
	xmlTextWriterPtr writer = xml_writer_create_in_memory(&output);

	/* No need for an explicit buffer check as this will fail on a NULL buffer. */
	if (!writer) {
		zend_throw_error(NULL, "Could not construct libxml writer");
		RETURN_THROWS();
	}

	xml_writer_create_static(INTERNAL_FUNCTION_PARAM_PASSTHRU, writer, output);
}

static int xml_writer_stream_write(void *context, const char *buffer, int len)
{
	zend_resource *resource = context;
	if (EXPECTED(resource->ptr)) {
		php_stream *stream = resource->ptr;
		return php_stream_write(stream, buffer, len);
	}
	return -1;
}

static int xml_writer_stream_close(void *context)
{
	zend_resource *resource = context;
	/* Don't close it as others may still use it! We don't own the resource!
	 * Just delete our reference (and clean up if we're the last one). */
	zend_list_delete(resource);
	return 0;
}

PHP_METHOD(XMLWriter, toStream)
{
	zval *stream_zv;
	php_stream *stream;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_RESOURCE(stream_zv)
	ZEND_PARSE_PARAMETERS_END();

	php_stream_from_res(stream, Z_RES_P(stream_zv));

	xmlOutputBufferPtr output_buffer = xmlOutputBufferCreateIO(xml_writer_stream_write, xml_writer_stream_close, stream->res, NULL);
	if (UNEXPECTED(output_buffer == NULL)) {
		zend_throw_error(NULL, "Could not construct libxml output buffer");
		RETURN_THROWS();
	}

	/* When the buffer is closed (even in error paths) the reference is destroyed. */
	Z_ADDREF_P(stream_zv);

	xmlTextWriterPtr writer = xmlNewTextWriter(output_buffer);
	if (UNEXPECTED(writer == NULL)) {
		xmlOutputBufferClose(output_buffer);
		zend_throw_error(NULL, "Could not construct libxml writer");
		RETURN_THROWS();
	}

	/* output_buffer is owned by writer, and so writer will clean that up for us. */
	xml_writer_create_static(INTERNAL_FUNCTION_PARAM_PASSTHRU, writer, NULL);
}

/* {{{ php_xmlwriter_flush */
static void php_xmlwriter_flush(INTERNAL_FUNCTION_PARAMETERS, int force_string) {
	xmlTextWriterPtr ptr;
	bool empty = 1;
	int output_bytes;
	zval *self;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O|b", &self, xmlwriter_class_entry_ce, &empty) == FAILURE) {
		RETURN_THROWS();
	}
	XMLWRITER_FROM_OBJECT(ptr, self);

	smart_str *output = Z_XMLWRITER_P(self)->output;
	if (force_string == 1 && output == NULL) {
		RETURN_EMPTY_STRING();
	}
	output_bytes = xmlTextWriterFlush(ptr);
	if (output) {
		if (empty) {
			RETURN_STR(smart_str_extract(output));
		} else if (smart_str_get_len(output) > 0) {
			RETURN_NEW_STR(zend_string_dup(output->s, false));
		} else {
			RETURN_EMPTY_STRING();
		}
	} else {
		RETVAL_LONG(output_bytes);
	}
}
/* }}} */

/* {{{ Output current buffer as string */
PHP_FUNCTION(xmlwriter_output_memory)
{
	php_xmlwriter_flush(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}
/* }}} */

/* {{{ Output current buffer */
PHP_FUNCTION(xmlwriter_flush)
{
	php_xmlwriter_flush(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
static PHP_MINIT_FUNCTION(xmlwriter)
{
	memcpy(&xmlwriter_object_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	xmlwriter_object_handlers.offset = XtOffsetOf(ze_xmlwriter_object, std);
	xmlwriter_object_handlers.dtor_obj = xmlwriter_object_dtor;
	xmlwriter_object_handlers.clone_obj = NULL;
	xmlwriter_class_entry_ce = register_class_XMLWriter();
	xmlwriter_class_entry_ce->create_object = xmlwriter_object_new;
	xmlwriter_class_entry_ce->default_object_handlers = &xmlwriter_object_handlers;

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
static PHP_MINFO_FUNCTION(xmlwriter)
{
	php_info_print_table_start();
	{
		php_info_print_table_row(2, "XMLWriter", "enabled");
	}
	php_info_print_table_end();
}
/* }}} */
