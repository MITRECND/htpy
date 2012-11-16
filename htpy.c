/*
 * Copyright (c) 2012 The MITRE Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <Python.h>
#include <structmember.h>
#include "htp.h"

#define HTPY_VERSION "0.6"

static PyObject *htpy_error;
static PyObject *htpy_stop;

/*
 * We set the python connection parser as user_data in the libhtp connection
 * parser. Most callbacks are given a way to eventually get to the connection
 * parser by doing something like this:
 *
 * PyObject *obj = (PyObject *) htp_connp_get_user_data(txd->tx->connp);
 *
 * We store the python objects for the callbacks in the connection parser
 * object. Once we have the connection parser using the above snippet
 * we can call the appropriate python function by using obj->foo_callback.
 *
 * The only callback that is not able to get to the connection parser is
 * the request_file_data callback. Since we can't get to a connection
 * parser from there we are storing the python object as a global.
 */
PyObject *request_file_data_callback;

typedef struct {
	PyObject_HEAD
	htp_cfg_t *cfg;
} htpy_config;

static PyObject *htpy_config_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	htpy_config *self;

	self = (htpy_config *) type->tp_alloc(type, 0);

	return (PyObject *) self;
}

static int htpy_config_init(htpy_config *self, PyObject *args, PyObject *kwds) {
	self->cfg = htp_config_create();
	if (!self->cfg)
		return -1;

	return 0;
}

static void htpy_config_dealloc(htpy_config *self) {
	htp_config_destroy(self->cfg);
	self->ob_type->tp_free((PyObject *) self);
}

static PyMethodDef htpy_config_methods[] = {
	{ NULL }
};

#define CONFIG_GET(ATTR) \
static PyObject *htpy_config_get_##ATTR(htpy_config *self, void *closure) { \
	PyObject *ret; \
	ret = Py_BuildValue("i", self->cfg->ATTR); \
	if (!ret) { \
		PyErr_SetString(htpy_error, "Unable to get this attribute."); \
		return NULL; \
	} \
	return(ret); \
}

CONFIG_GET(log_level)
CONFIG_GET(spersonality)
CONFIG_GET(path_case_insensitive)
CONFIG_GET(path_compress_separators)
CONFIG_GET(path_backslash_separators)
CONFIG_GET(path_control_char_handling)
CONFIG_GET(path_convert_utf8)
CONFIG_GET(path_decode_separators)
CONFIG_GET(path_invalid_encoding_handling)
CONFIG_GET(path_invalid_utf8_handling)
CONFIG_GET(path_nul_encoded_handling)
CONFIG_GET(path_nul_raw_handling)
CONFIG_GET(generate_request_uri_normalized)
CONFIG_GET(tx_auto_destroy)

#define CONFIG_SET(ATTR) \
static int htpy_config_set_##ATTR(htpy_config *self, PyObject *value, void *closure) { \
	int v; \
	if (!value) { \
		PyErr_SetString(htpy_error, "Value may not be None."); \
		return -1; \
	} \
	if (!PyInt_Check(value)) { \
		PyErr_SetString(htpy_error, "Attribute must be of type int."); \
		return -1; \
	} \
	v = (int) PyInt_AsLong(value); \
	htp_config_set_##ATTR((htp_cfg_t *) self->cfg, v); \
	return 0; \
}

CONFIG_SET(path_case_insensitive)
CONFIG_SET(path_compress_separators)
CONFIG_SET(path_backslash_separators)
CONFIG_SET(path_control_char_handling)
CONFIG_SET(path_convert_utf8)
CONFIG_SET(path_decode_separators)
CONFIG_SET(path_invalid_encoding_handling)
CONFIG_SET(path_invalid_utf8_handling)
CONFIG_SET(path_nul_encoded_handling)
CONFIG_SET(path_nul_raw_handling)
CONFIG_SET(generate_request_uri_normalized)
CONFIG_SET(tx_auto_destroy)

/*
 * Sadly the log level is not exposed like others. Only way to set it
 * is to manually set it in the config structure directly.
 *
 * I'm not checking for values above the maximum log level. If someone
 * wants to do something stupid like set it to htpy.HTP_1_1 (101) then
 * they can have fun dealing with it.
 */
static int htpy_config_set_log_level(htpy_config *self, PyObject *value, void *closure) {
	int v;
	if (!value) {
		PyErr_SetString(htpy_error, "Value may not be None.");
		return -1;
	}

	if (!PyInt_Check(value)) {
		PyErr_SetString(htpy_error, "Attribute must be of type int.");
		return -1;
	}

	v = (int) PyInt_AsLong(value);
	self->cfg->log_level = v;

	return 0;
}

/* This is a special case attribute setter - it checks the return value */
static int htpy_config_set_spersonality(htpy_config *self, PyObject *value, void *closure) {
	int v;

	if (!value) {
		PyErr_SetString(htpy_error, "Value may not be None");
		return -1;
	}

	if (!PyInt_Check(value)) {
		PyErr_SetString(htpy_error, "Attribute must be of type int.");
		return -1;
	}

	v = (int) PyInt_AsLong(value);

	if (htp_config_set_server_personality((htp_cfg_t *) self->cfg, v) != HTP_OK) {
		PyErr_SetString(htpy_error, "Invalid spersonality.");
		return -1;
	}

	return 0;
}

static PyGetSetDef htpy_config_getseters[] = {
	{ "log_level", (getter) htpy_config_get_log_level,
	  (setter) htpy_config_set_log_level,
	  "Logs with a level less than this will be ignored.", NULL },
	{ "spersonality", (getter) htpy_config_get_spersonality,
	  (setter) htpy_config_set_spersonality,
	  "Server personality. This affects multiple other attributes.", NULL },
	{ "path_case_insensitive", (getter) htpy_config_get_path_case_insensitive,
	  (setter) htpy_config_set_path_case_insensitive,
	  "Convert paths to lowercase.", NULL },
	{ "path_compress_separators",
	  (getter) htpy_config_get_path_compress_separators,
	  (setter) htpy_config_set_path_compress_separators,
	  "Compress sequential path separators into one.", NULL },
	{ "path_backslash_separators",
	  (getter) htpy_config_get_path_backslash_separators,
	  (setter) htpy_config_set_path_backslash_separators,
	  "Treat backslash as a path separator", NULL },
	{ "path_control_char_handling",
	  (getter) htpy_config_get_path_control_char_handling,
	  (setter) htpy_config_set_path_control_char_handling,
	  "How the parser expects a path with a control char to be handled", NULL },
	{ "path_convert_utf8", (getter) htpy_config_get_path_convert_utf8,
	  (setter) htpy_config_set_path_convert_utf8,
	  "Convert UTF-8 to single byte stream using default best-fit mapping.",
	  NULL },
	{ "path_decode_separators",
	  (getter) htpy_config_get_path_decode_separators,
	  (setter) htpy_config_set_path_decode_separators,
	  "decode path separators", NULL },
	{ "path_invalid_encoding_handling",
	  (getter) htpy_config_get_path_invalid_encoding_handling,
	  (setter) htpy_config_set_path_invalid_encoding_handling,
	  "How server reacts to invalid encoding in path.", NULL },
	{ "path_invalid_utf8_handling",
	  (getter) htpy_config_get_path_invalid_utf8_handling,
	  (setter) htpy_config_set_path_invalid_utf8_handling,
	  "How server reacts to invalid UTF-8 characters in path.", NULL },
	{ "path_nul_encoded_handling",
	  (getter) htpy_config_get_path_nul_encoded_handling,
	  (setter) htpy_config_set_path_nul_encoded_handling,
	  "How server reacts to encoded NUL bytes.", NULL },
	{ "path_nul_raw_handling",
	  (getter) htpy_config_get_path_nul_raw_handling,
	  (setter) htpy_config_set_path_nul_raw_handling,
	  "How server reacts to raw NUL bytes.", NULL },
	{ "generate_request_uri_normalized",
	  (getter) htpy_config_get_generate_request_uri_normalized,
	  (setter) htpy_config_set_generate_request_uri_normalized,
	  "Generate a normalized URI", NULL },
	{ "tx_auto_destroy",
	  (getter) htpy_config_get_tx_auto_destroy,
	  (setter) htpy_config_set_tx_auto_destroy,
	  "Automatically destroy transactions", NULL },
	{ NULL }
};

static PyMemberDef htpy_config_members[] = {
	{ NULL }
};

static PyTypeObject htpy_config_type = {
	PyObject_HEAD_INIT(NULL)
	0,                                /* ob_size */
	"htpy.config",                    /* tp_name */
	sizeof(htpy_config),              /* tp_basicsize */
	0,                                /* tp_itemsize */
	(destructor) htpy_config_dealloc, /* tp_dealloc */
	0,                                /* tp_print */
	0,                                /* tp_getattr */
	0,                                /* tp_setattr */
	0,                                /* tp_compare */
	0,                                /* tp_repr */
	0,                                /* tp_as_number */
	0,                                /* tp_as_sequence */
	0,                                /* tp_as_mapping */
	0,                                /* tp_hash */
	0,                                /* tp_call */
	0,                                /* tp_str */
	0,                                /* tp_getattro */
	0,                                /* tp_setattro */
	0,                                /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,               /* tp_flags */
	"config object",                  /* tp_doc */
	0,                                /* tp_traverse */
	0,                                /* tp_clear */
	0,                                /* tp_richcompare */
	0,                                /* tp_weaklistoffset */
	0,                                /* tp_iter */
	0,                                /* tp_iternext */
	htpy_config_methods,              /* tp_methods */
	htpy_config_members,              /* tp_members */
	htpy_config_getseters,            /* tp_getset */
	0,                                /* tp_base */
	0,                                /* tp_dict */
	0,                                /* tp_descr_get */
	0,                                /* tp_descr_set */
	0,                                /* tp_dictoffset */
	(initproc) htpy_config_init,      /* tp_init */
	0,                                /* tp_alloc */
	htpy_config_new,                  /* tp_new */
};

typedef struct {
	PyObject_HEAD
	htp_connp_t *connp;
	PyObject *obj_store;
	/* Callbacks */
	PyObject *transaction_start_callback;
	PyObject *request_line_callback;
	PyObject *request_uri_normalize_callback;
	PyObject *request_headers_callback;
	PyObject *request_body_data_callback;
	PyObject *request_trailer_callback;
	PyObject *request_callback;
	PyObject *response_start_callback;
	PyObject *response_line_callback;
	PyObject *response_headers_callback;
	PyObject *response_body_data_callback;
	PyObject *response_trailer_callback;
	PyObject *response_callback;
	PyObject *log_callback;
} htpy_connp;

static PyObject *htpy_connp_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	htpy_connp *self;

	self = (htpy_connp *) type->tp_alloc(type, 0);

	return (PyObject *) self;
}

static void htpy_connp_dealloc(htpy_connp *self) {
	/*
	 * Decrement reference counters and free the underlying
	 * libhtp backed storage.
	 */
	Py_XDECREF(self->obj_store);
	Py_XDECREF(self->transaction_start_callback);
	Py_XDECREF(self->request_line_callback);
	Py_XDECREF(self->request_uri_normalize_callback);
	Py_XDECREF(self->request_headers_callback);
	Py_XDECREF(self->request_body_data_callback);
	Py_XDECREF(self->request_trailer_callback);
	Py_XDECREF(self->request_callback);
	Py_XDECREF(self->response_start_callback);
	Py_XDECREF(self->response_line_callback);
	Py_XDECREF(self->response_headers_callback);
	Py_XDECREF(self->response_body_data_callback);
	Py_XDECREF(self->response_trailer_callback);
	Py_XDECREF(self->response_callback);
	Py_XDECREF(self->log_callback);
	htp_connp_destroy_all(self->connp);
	self->ob_type->tp_free((PyObject *) self);
}

static int htpy_connp_init(htpy_connp *self, PyObject *args, PyObject *kwds) {
	PyObject *cfg_obj = NULL;

	if (!PyArg_ParseTuple(args, "O:htpy_connp_init", &cfg_obj))
		return -1;

	if (cfg_obj) {
		self->connp = htp_connp_create_copycfg(((htpy_config *) cfg_obj)->cfg);
		if (!self->connp)
			return -1;

		htp_connp_set_user_data(((htpy_connp *) self)->connp, (void *) self);
	}
	else
		return -1;

	return 0;
}

/*
 * Callback handlers.
 *
 * Libhtp will call one of these callbacks. This callback will then convert
 * the htp_connp_t pointer into a PyObject and pass that to the real callback.
 * It will then convert the returned PyObject to an int and pass that back to
 * libhtp.
 *
 * The callbacks that take a htp_tx_data_t are defined with CALLBACK_TX. The
 * log callback is not defined in a macro because there is only one of it's
 * type.
 *
 * XXX: Add support for removing callbacks?
 */
#define CALLBACK(CB) \
int htpy_##CB##_callback(htp_connp_t *connp) { \
	PyObject *obj = (PyObject *) htp_connp_get_user_data(connp); \
	PyObject *arglist; \
	PyObject *res; \
	long i; \
	if (((htpy_connp *) obj)->obj_store) \
		arglist = Py_BuildValue("(OO)", obj, ((htpy_connp *) obj)->obj_store); \
	else \
		arglist = Py_BuildValue("(O)", obj); \
	if (!arglist) \
		return HOOK_ERROR; \
	res = PyObject_CallObject(((htpy_connp *) obj)->CB##_callback, arglist); \
	Py_DECREF(arglist); \
	if (!res) \
		return HOOK_ERROR; \
	i = PyInt_AsLong(res); \
	Py_DECREF(res); \
	return((int) i); \
}

CALLBACK(transaction_start)
CALLBACK(request_line)
CALLBACK(request_uri_normalize)
CALLBACK(request_headers)
CALLBACK(request_trailer)
CALLBACK(request)
CALLBACK(response_start)
CALLBACK(response_line)
CALLBACK(response_headers)
CALLBACK(response_trailer)
CALLBACK(response)

/* These callbacks take a htp_tx_data_t pointer. */
#define CALLBACK_TX(CB) \
int htpy_##CB##_callback(htp_tx_data_t *txd) { \
	PyObject *obj = (PyObject *) htp_connp_get_user_data(txd->tx->connp); \
	PyObject *arglist; \
	PyObject *res; \
	long i; \
	if (((htpy_connp *) obj)->obj_store) \
		arglist = Py_BuildValue("(s#IO)", txd->data, txd->len, txd->len, ((htpy_connp *) obj)->obj_store); \
	else \
		arglist = Py_BuildValue("(s#I)", txd->data, txd->len, txd->len); \
	if (!arglist) \
		return HOOK_ERROR; \
	res = PyObject_CallObject(((htpy_connp *) obj)->CB##_callback, arglist); \
	Py_DECREF(arglist); \
	if (!res) \
		return HOOK_ERROR; \
	i = PyInt_AsLong(res); \
	Py_DECREF(res); \
	return((int) i); \
}

CALLBACK_TX(request_body_data)
CALLBACK_TX(response_body_data)

int htpy_request_file_data_callback(htp_file_data_t *file_data) {
	long i;
	PyObject *res;
	PyObject *arglist;
	PyObject *data_key, *data_val;
	PyObject *filename_key, *filename_val;
	PyObject *tmpname_key, *tmpname_val;
	PyObject *dict = PyDict_New();

	if (!dict) {
		PyErr_SetString(htpy_error, "Unable to create dictionary.");
		return HOOK_ERROR;
	}

	data_key = Py_BuildValue("s", "data");
	data_val = Py_BuildValue("s#", file_data->data, file_data->len);
	if (!data_key || !data_val) {
		Py_DECREF(dict);
		return HOOK_ERROR;
	}
	if (PyDict_SetItem(dict, data_key, data_val) == -1) {
		Py_DECREF(dict);
		return HOOK_ERROR;
	}

	if (file_data->file->filename) {
		filename_key = Py_BuildValue("s", "filename");
		filename_val = Py_BuildValue("s#", bstr_ptr(file_data->file->filename), bstr_len(file_data->file->filename));
		if (PyDict_SetItem(dict, filename_key, filename_val) == -1) {
			Py_DECREF(dict);
			return HOOK_ERROR;
		}
	}

    if (file_data->file->tmpname) {
		tmpname_key = Py_BuildValue("s", "tmpname");
		tmpname_val = Py_BuildValue("s", file_data->file->tmpname);
		if (PyDict_SetItem(dict, tmpname_key, tmpname_val) == -1) {
			Py_DECREF(dict);
			return HOOK_ERROR;
		}
	}

	arglist = Py_BuildValue("(O)", dict);
	if (!arglist)
		return HOOK_ERROR;

	res = PyObject_CallObject(request_file_data_callback, arglist);
	Py_DECREF(arglist);
	if (!res)
		return HOOK_ERROR;
	i = PyInt_AsLong(res);
	Py_DECREF(res);
	return((int) i);
}

int htpy_log_callback(htp_log_t *log) {
	PyObject *obj = (PyObject *) htp_connp_get_user_data(log->connp);
	PyObject *arglist = NULL;
	PyObject *res;
	long i;

	if (((htpy_connp *) obj)->obj_store)
		arglist = Py_BuildValue("(OsiO)", (htpy_connp *) obj, log->msg, log->level, ((htpy_connp *) obj)->obj_store);
	else
		arglist = Py_BuildValue("(Osi)", (htpy_connp *) obj, log->msg, log->level);
	if (!arglist)
		return HOOK_ERROR;

	res = PyObject_CallObject(((htpy_connp *) obj)->log_callback, arglist);
	Py_DECREF(arglist);
	if (!res)
		return HOOK_ERROR;
	i = PyInt_AsLong(res);
	Py_DECREF(res);
	return((int) i);
}

/* Registering callbacks... */
#define REGISTER_CALLBACK(CB) \
static PyObject *htpy_connp_register_##CB(PyObject *self, PyObject *args) { \
	PyObject *res = NULL; \
	PyObject *temp; \
	if (PyArg_ParseTuple(args, "O:htpy_connp_register_##CB", &temp)) { \
		if (!PyCallable_Check(temp)) { \
			PyErr_SetString(PyExc_TypeError, "parameter must be callable"); \
			return NULL; \
		} \
		Py_XINCREF(temp); \
		Py_XDECREF(((htpy_connp *) self)->CB##_callback); \
		((htpy_connp *) self)->CB##_callback = temp; \
		htp_config_register_##CB(((htpy_connp *) self)->connp->cfg, htpy_##CB##_callback); \
		Py_INCREF(Py_None); \
		res = Py_None; \
	} \
	return res; \
}

REGISTER_CALLBACK(transaction_start)
REGISTER_CALLBACK(request_line)
REGISTER_CALLBACK(request_uri_normalize)
REGISTER_CALLBACK(request_headers)
REGISTER_CALLBACK(request_body_data)
REGISTER_CALLBACK(request_trailer)
REGISTER_CALLBACK(request)
REGISTER_CALLBACK(response_start)
REGISTER_CALLBACK(response_line)
REGISTER_CALLBACK(response_headers)
REGISTER_CALLBACK(response_body_data)
REGISTER_CALLBACK(response_trailer)
REGISTER_CALLBACK(response)
REGISTER_CALLBACK(log)

static PyObject *htpy_connp_register_request_file_data(PyObject *self, PyObject *args) {
	PyObject *res = NULL;
	PyObject *temp;
	int extract = 0;
	if (PyArg_ParseTuple(args, "O|i:htpy_connp_register_request_file_data", &temp, &extract)) {
		if (!PyCallable_Check(temp)) {
			PyErr_SetString(PyExc_TypeError, "parameter must be callable");
			return NULL;
		}

		Py_XINCREF(temp);
		Py_XDECREF(request_file_data_callback);

		request_file_data_callback = temp;

		if (extract)
			((htpy_connp *) self)->connp->cfg->extract_request_files = 1;

		htp_config_register_multipart_parser(((htpy_connp *) self)->connp->cfg);

		htp_config_register_request_file_data(((htpy_connp *) self)->connp->cfg, htpy_request_file_data_callback);

		Py_INCREF(Py_None);
		res = Py_None;
	}
	return res;
}

/* Return a header who'se key is the given string. */
#define GET_HEADER(TYPE) \
static PyObject *htpy_connp_get_##TYPE##_header(PyObject *self, PyObject *args) { \
	PyObject *ret; \
	htp_header_t *hdr; \
	PyObject *py_str = NULL; \
	htp_tx_t *tx = NULL; \
	char *p = NULL; \
	if (!PyArg_ParseTuple(args, "S:htpy_connp_get_##TYPE##_header", &py_str)) \
		return NULL; \
	tx = list_get(((htpy_connp *) self)->connp->conn->transactions, list_size(((htpy_connp *) self)->connp->conn->transactions) - 1); \
	if (!tx || !tx->TYPE##_headers) { \
		PyErr_SetString(htpy_error, "Missing transaction or headers."); \
		return NULL; \
	} \
	p = PyString_AsString(py_str); \
	if (!p) \
		return NULL; \
	hdr = table_get_c(tx->TYPE##_headers, p); \
	if (!hdr) \
		Py_RETURN_NONE; \
	ret = Py_BuildValue("s#", bstr_ptr(hdr->value), bstr_len(hdr->value)); \
	if (!ret) \
		return NULL; \
	return ret; \
}

GET_HEADER(request)
GET_HEADER(response)

/* Return a dictionary of all request or response headers. */
#define GET_ALL_HEADERS(TYPE) \
static PyObject *htpy_connp_get_all_##TYPE##_headers(PyObject *self, PyObject *args) { \
	htp_header_t *hdr; \
	PyObject *key, *val; \
	htp_tx_t *tx = NULL; \
	PyObject *ret = PyDict_New(); \
	if (!ret) { \
		PyErr_SetString(htpy_error, "Unable to create return dictionary."); \
		return NULL; \
	} \
	tx = list_get(((htpy_connp *) self)->connp->conn->transactions, list_size(((htpy_connp *) self)->connp->conn->transactions) - 1); \
	if (!tx || !tx->TYPE##_headers) { \
		PyErr_SetString(htpy_error, "Missing transaction or headers."); \
		Py_DECREF(ret); \
		return NULL; \
	} \
	table_iterator_reset(tx->TYPE##_headers); \
	while ((table_iterator_next(tx->TYPE##_headers, (void **) &hdr)) != NULL) {\
		key = Py_BuildValue("s#", bstr_ptr(hdr->name), bstr_len(hdr->name)); \
		val = Py_BuildValue("s#", bstr_ptr(hdr->value), bstr_len(hdr->value)); \
		if (!key || !val) { \
			Py_DECREF(ret); \
			return NULL; \
		} \
		if (PyDict_SetItem(ret, key, val) == -1) { \
			Py_DECREF(ret); \
			return NULL; \
		} \
	} \
	return ret; \
}

GET_ALL_HEADERS(request)
GET_ALL_HEADERS(response)

/*
 * XXX: Not sure I like mucking around in the transaction to get the method,
 * but I'm not sure of a better way.
 */
static PyObject *htpy_connp_get_method(PyObject *self, PyObject *args) {
	PyObject *ret;
	htp_tx_t *tx = NULL;

	tx = list_get(((htpy_connp *) self)->connp->conn->transactions, list_size(((htpy_connp *) self)->connp->conn->transactions) - 1);
	if (!tx || !tx->request_method) {
		PyErr_SetString(htpy_error, "Missing transaction or request method.");
		return NULL;
	}

	ret = Py_BuildValue("s#", bstr_ptr(tx->request_method), bstr_len(tx->request_method));

	return ret;
}

static PyObject *htpy_connp_set_obj(PyObject *self, PyObject *args) {
	PyObject *obj;

	if (!PyArg_ParseTuple(args, "O:htpy_connp_set_obj", &obj))
		return NULL;

	Py_XINCREF(obj);
	((htpy_connp *) self)->obj_store = obj;

	Py_RETURN_NONE;
}

static PyObject *htpy_connp_del_obj(PyObject *self, PyObject *args) {
	Py_XDECREF(((htpy_connp *) self)->obj_store);
	((htpy_connp *) self)->obj_store = NULL;

	Py_RETURN_NONE;
}

/*
 * XXX: Not sure I like mucking around in the transaction to get the status,
 * but I'm not sure of a better way.
 */
static PyObject *htpy_connp_get_response_status(PyObject *self, PyObject *args) {
	PyObject *ret;
	htp_tx_t *tx = NULL;

	tx = list_get(((htpy_connp *) self)->connp->conn->transactions, list_size(((htpy_connp *) self)->connp->conn->transactions) - 1);
	if (!tx) {
		PyErr_SetString(htpy_error, "Missing transaction.");
		return NULL;
	}

	ret = Py_BuildValue("i", tx->response_status_number);

	return ret;
}

/* These do the actual parsing. */
#define DATA(TYPE) \
static PyObject *htpy_connp_##TYPE##_data(PyObject *self, PyObject *args) { \
	const char *data; \
	PyObject *ret; \
	int x; \
	int len; \
	if (!PyArg_ParseTuple(args, "s#:htpy_connp_##TYPE##_data", &data, &len)) \
		return NULL; \
	x = htp_connp_##TYPE##_data(((htpy_connp *) self)->connp, NULL, (unsigned char *) data, len); \
	if (x == STREAM_STATE_ERROR) { \
		PyErr_SetString(htpy_error, "Stream error."); \
		return NULL; \
	} \
	if (x == STREAM_STATE_STOP) { \
		PyErr_SetString(htpy_stop, "Stream stop."); \
		return NULL; \
	} \
	ret = PyInt_FromLong((long) x); \
	return(ret); \
}

DATA(req)
DATA(res)

#define DATA_CONSUMED(TYPE) \
static PyObject *htpy_connp_##TYPE##_data_consumed(PyObject *self, PyObject *args) { \
	PyObject *ret; \
	ret = Py_BuildValue("I", htp_connp_##TYPE##_data_consumed(((htpy_connp *) self)->connp)); \
	return(ret); \
}

DATA_CONSUMED(req)
DATA_CONSUMED(res)

static PyObject *htpy_connp_get_last_error(PyObject *self, PyObject *args) {
	htp_log_t *err = NULL;
	PyObject *ret;

	err = htp_connp_get_last_error(((htpy_connp *) self)->connp);
	if (!err)
		Py_RETURN_NONE;

	ret = Py_BuildValue("{sisssssi}", "level", err->level, "msg", err->msg, "file", err->file, "line", err->line);

	return(ret);
}

static PyObject *htpy_connp_clear_error(PyObject *self, PyObject *args) {
	htp_connp_clear_error(((htpy_connp *) self)->connp);
	Py_RETURN_NONE;
}

static PyObject *htpy_connp_get_uri(PyObject *self, PyObject *args) {
	htp_uri_t *uri;
	int fail = 0;
	PyObject *key, *val;
	PyObject *ret = PyDict_New();

	if (!ret) {
		PyErr_SetString(htpy_error, "Unable to create new dictionary.");
		return NULL;
	}

	/* Empty tx? That's odd. */
	if (!((htpy_connp *) self)->connp->in_tx)
		Py_RETURN_NONE;

	if (!((htpy_connp *) self)->connp->in_tx->parsed_uri)
		Py_RETURN_NONE;

	uri = ((htpy_connp *) self)->connp->in_tx->parsed_uri;

	if (uri->scheme) {
		key = Py_BuildValue("s", "scheme");
		val = Py_BuildValue("s", bstr_util_strdup_to_c(uri->scheme));
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	if (uri->username) {
		key = Py_BuildValue("s", "username");
		val = Py_BuildValue("s", bstr_util_strdup_to_c(uri->username));
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	if (uri->password) {
		key = Py_BuildValue("s", "password");
		val = Py_BuildValue("s", bstr_util_strdup_to_c(uri->password));
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	if (uri->hostname) {
		key = Py_BuildValue("s", "hostname");
		val = Py_BuildValue("s", bstr_util_strdup_to_c(uri->hostname));
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	if (uri->port) {
		key = Py_BuildValue("s", "port");
		val = Py_BuildValue("s", bstr_util_strdup_to_c(uri->port));
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	if (uri->port_number) {
		key = Py_BuildValue("s", "port_number");
		val = Py_BuildValue("i", uri->port_number);
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	if (uri->path) {
		key = Py_BuildValue("s", "path");
		val = Py_BuildValue("s", bstr_util_strdup_to_c(uri->path));
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	if (uri->query) {
		key = Py_BuildValue("s", "query");
		val = Py_BuildValue("s", bstr_util_strdup_to_c(uri->query));
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	if (uri->fragment) {
		key = Py_BuildValue("s", "fragment");
		val = Py_BuildValue("s", bstr_util_strdup_to_c(uri->fragment));
		if (!key || !val)
			fail = 1;
		if (PyDict_SetItem(ret, key, val) == -1)
			fail = 1;
	}

	// Exception should be set by Py_BuildValue or PyDict_SetItem failing.
	if (fail) {
		Py_DECREF(ret);
		return NULL;
	}

	return ret;
}

static PyMethodDef htpy_connp_methods[] = {
	{ "get_request_header", htpy_connp_get_request_header, METH_VARARGS,
	  "Return a string for the requested header." },
	{ "get_response_header", htpy_connp_get_response_header, METH_VARARGS,
	  "Return a string for the requested header." },
	{ "get_all_request_headers", htpy_connp_get_all_request_headers,
	  METH_NOARGS, "Return a dictionary of all request headers." },
	{ "get_all_response_headers", htpy_connp_get_all_response_headers,
	  METH_NOARGS, "Return a dictionary of all response headers." },
	{ "get_response_status", htpy_connp_get_response_status, METH_VARARGS,
	  "Return the response status as an integer." },
	{ "register_transaction_start", htpy_connp_register_transaction_start,
	  METH_VARARGS, "Register a hook for start of a transaction." },
	{ "register_request_line", htpy_connp_register_request_line, METH_VARARGS,
	  "Register a hook for right after request line has been parsed." },
	{ "register_request_uri_normalize",
	  htpy_connp_register_request_uri_normalize, METH_VARARGS,
	  "Register a hook for right before the URI is normalized." },
	{ "register_request_headers", htpy_connp_register_request_headers,
	  METH_VARARGS,
	  "Register a hook for right after headers have been parsed and sanity checked." },
	{ "register_request_body_data", htpy_connp_register_request_body_data,
	  METH_VARARGS,
	  "Register a hook for when a piece of request body data is processed." },
	{ "register_request_file_data", htpy_connp_register_request_file_data,
	  METH_VARARGS,
	  "Register a hook for when a full request body data is processed." },
	{ "register_request_trailer", htpy_connp_register_request_trailer,
	  METH_VARARGS,
	  "Register a hook for right after headers have been parsed." },
	{ "register_request", htpy_connp_register_request, METH_VARARGS,
	  "Register a callback for when the entire request is parsed." },
	{ "register_response_start", htpy_connp_register_response_start,
	  METH_VARARGS,
	  "Register a hook for as soon as a response is about to start." },
	{ "register_response_line", htpy_connp_register_response_line,
	  METH_VARARGS,
	  "Register a hook for right after response line has been parsed." },
	{ "register_response_headers", htpy_connp_register_response_headers,
	  METH_VARARGS, "Register a hook for right after headers have been parsed and sanity checked." },
	{ "register_response_body_data", htpy_connp_register_response_body_data,
	  METH_VARARGS,
	  "Register a hook for when a piece of response body data is processed. Chunked and gzip'ed data are handled." },
	{ "register_response_trailer", htpy_connp_register_response_trailer,
	  METH_VARARGS,
	  "Register a hook for right after headers have been parsed." },
	{ "register_response", htpy_connp_register_response, METH_VARARGS,
	  "Register a hook for right after an entire response has been parsed." },
	{ "register_log", htpy_connp_register_log, METH_VARARGS,
	  "Register a callback for when a log message is generated." },
	{ "set_obj", htpy_connp_set_obj, METH_VARARGS,
	  "Set arbitrary python object to be passed to callbacks." },
	{ "del_obj", htpy_connp_del_obj, METH_VARARGS,
	  "Remove arbitrary python object being passed to callbacks." },
	{ "req_data", htpy_connp_req_data, METH_VARARGS, "Parse a request." },
	{ "req_data_consumed", htpy_connp_req_data_consumed, METH_NOARGS,
	  "Return amount of data consumed." },
	{ "res_data", htpy_connp_res_data, METH_VARARGS, "Parse a response." },
	{ "res_data_consumed", htpy_connp_res_data_consumed, METH_NOARGS,
	  "Return amount of data consumed." },
	{ "get_last_error", htpy_connp_get_last_error, METH_NOARGS,
	  "Return a dictionary of the last error for the parser." },
	{ "clear_error", htpy_connp_clear_error, METH_NOARGS,
	  "Clear last error for the parser." },
	{ "get_uri", htpy_connp_get_uri, METH_NOARGS,
	  "Return a dictionary of the URI." },
	{ "get_method", htpy_connp_get_method, METH_NOARGS,
	  "Return the request method as a string." },
	{ NULL }
};

static PyTypeObject htpy_connp_type = {
	PyObject_HEAD_INIT(NULL)
	0,                               /* ob_size */
	"htpy.connp",                    /* tp_name */
	sizeof(htpy_connp),              /* tp_basicsize */
	0,                               /* tp_itemsize */
	(destructor) htpy_connp_dealloc, /* tp_dealloc */
	0,                               /* tp_print */
	0,                               /* tp_getattr */
	0,                               /* tp_setattr */
	0,                               /* tp_compare */
	0,                               /* tp_repr */
	0,                               /* tp_as_number */
	0,                               /* tp_as_sequence */
	0,                               /* tp_as_mapping */
	0,                               /* tp_hash */
	0,                               /* tp_call */
	0,                               /* tp_str */
	0,                               /* tp_getattro */
	0,                               /* tp_setattro */
	0,                               /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,              /* tp_flags */
	"connp object",                  /* tp_doc */
	0,                               /* tp_traverse */
	0,                               /* tp_clear */
	0,                               /* tp_richcompare */
	0,                               /* tp_weaklistoffset */
	0,                               /* tp_iter */
	0,                               /* tp_iternext */
	htpy_connp_methods,              /* tp_methods */
	0,                               /* tp_members */
	0,                               /* tp_getset */
	0,                               /* tp_base */
	0,                               /* tp_dict */
	0,                               /* tp_descr_get */
	0,                               /* tp_descr_set */
	0,                               /* tp_dictoffset */
	(initproc) htpy_connp_init,      /* tp_init */
	0,                               /* tp_alloc */
	htpy_connp_new,                  /* tp_new */
};

static PyObject *htpy_init(PyObject *self, PyObject *args) {
	PyObject *cfg;
	PyObject *connp;
	PyObject *tuple;

	cfg = htpy_config_new(&htpy_config_type, NULL, NULL);
	if (!cfg) {
		PyErr_SetString(htpy_error, "Unable to make new config.");
		return NULL;
	}

	if (htpy_config_init((htpy_config *) cfg, NULL, NULL) == -1) {
		PyErr_SetString(htpy_error, "Unable to init new config.");
		Py_DECREF(cfg);
		return NULL;
	}

	htp_config_set_tx_auto_destroy(((htpy_config *) cfg)->cfg, 1);

	connp = htpy_connp_new(&htpy_connp_type, NULL, NULL);
	if (!connp) {
		PyErr_SetString(htpy_error, "Unable to make new connection parser.");
		Py_DECREF(cfg);
		return NULL;
	}

	/* Pass the config in as a tuple. New style arguments! */
	tuple = Py_BuildValue("(O)", cfg);
	if (htpy_connp_init((htpy_connp *) connp, tuple, NULL) == -1) {
		PyErr_SetString(htpy_error, "Unable to init new connection parser.");
		Py_DECREF(cfg);
		return NULL;
	}
	Py_DECREF(tuple);
	Py_DECREF(cfg);

	return(connp);
}

static PyMethodDef htpy_methods[] = {
	{ "init", htpy_init, METH_VARARGS,
	  "Return a parser object with default config." },
	{ NULL }
};

PyMODINIT_FUNC inithtpy(void) {
	PyObject *m;

	if (PyType_Ready(&htpy_config_type) < 0 || PyType_Ready(&htpy_connp_type) < 0)
		return;

	m = Py_InitModule3("htpy", htpy_methods, "Python interface to libhtp.");
	if (!m)
		return;

	htpy_error = PyErr_NewException("htpy.error", NULL, NULL);
	Py_INCREF(htpy_error);
	PyModule_AddObject(m, "error", htpy_error);

	htpy_stop = PyErr_NewException("htpy.stop", NULL, NULL);
	Py_INCREF(htpy_stop);
	PyModule_AddObject(m, "stop", htpy_stop);

	Py_INCREF(&htpy_config_type);
	PyModule_AddObject(m, "config", (PyObject *) &htpy_config_type);
	Py_INCREF(&htpy_connp_type);
	PyModule_AddObject(m, "connp", (PyObject *) &htpy_connp_type);

	PyModule_AddStringMacro(m, HTPY_VERSION);
	PyModule_AddStringMacro(m, HTP_BASE_VERSION_TEXT);

	PyModule_AddIntMacro(m, HTP_ERROR);
	PyModule_AddIntMacro(m, HTP_OK);
	PyModule_AddIntMacro(m, HTP_DATA);
	PyModule_AddIntMacro(m, HTP_DATA_OTHER);
	PyModule_AddIntMacro(m, HTP_DECLINED);

	PyModule_AddIntMacro(m, PROTOCOL_UNKNOWN);
	PyModule_AddIntMacro(m, HTTP_0_9);
	PyModule_AddIntMacro(m, HTTP_1_0);
	PyModule_AddIntMacro(m, HTTP_1_1);

	PyModule_AddIntMacro(m, COMPRESSION_NONE);
	PyModule_AddIntMacro(m, COMPRESSION_GZIP);
	PyModule_AddIntMacro(m, COMPRESSION_DEFLATE);

	PyModule_AddIntMacro(m, HTP_LOG_ERROR);
	PyModule_AddIntMacro(m, HTP_LOG_WARNING);
	PyModule_AddIntMacro(m, HTP_LOG_NOTICE);
	PyModule_AddIntMacro(m, HTP_LOG_INFO);
	PyModule_AddIntMacro(m, HTP_LOG_DEBUG);
	PyModule_AddIntMacro(m, HTP_LOG_DEBUG2);

	PyModule_AddIntMacro(m, HOOK_ERROR);
	PyModule_AddIntMacro(m, HOOK_OK);
	PyModule_AddIntMacro(m, HOOK_DECLINED);
	PyModule_AddIntMacro(m, HOOK_STOP);

	PyModule_AddIntMacro(m, STREAM_STATE_NEW);
	PyModule_AddIntMacro(m, STREAM_STATE_OPEN);
	PyModule_AddIntMacro(m, STREAM_STATE_CLOSED);
	PyModule_AddIntMacro(m, STREAM_STATE_ERROR);
	PyModule_AddIntMacro(m, STREAM_STATE_TUNNEL);
	PyModule_AddIntMacro(m, STREAM_STATE_DATA_OTHER);
	PyModule_AddIntMacro(m, STREAM_STATE_DATA);
	PyModule_AddIntMacro(m, STREAM_STATE_STOP);

	PyModule_AddIntMacro(m, HTP_SERVER_MINIMAL);
	PyModule_AddIntMacro(m, HTP_SERVER_GENERIC);
	PyModule_AddIntMacro(m, HTP_SERVER_IDS);
	PyModule_AddIntMacro(m, HTP_SERVER_IIS_4_0);
	PyModule_AddIntMacro(m, HTP_SERVER_IIS_5_0);
	PyModule_AddIntMacro(m, HTP_SERVER_IIS_5_1);
	PyModule_AddIntMacro(m, HTP_SERVER_IIS_6_0);
	PyModule_AddIntMacro(m, HTP_SERVER_IIS_7_0);
	PyModule_AddIntMacro(m, HTP_SERVER_IIS_7_5);
	PyModule_AddIntMacro(m, HTP_SERVER_TOMCAT_6_0);
	PyModule_AddIntMacro(m, HTP_SERVER_APACHE);
	PyModule_AddIntMacro(m, HTP_SERVER_APACHE_2_2);

	PyModule_AddIntMacro(m, STATUS_400);
	PyModule_AddIntMacro(m, STATUS_404);
	PyModule_AddIntMacro(m, TERMINATE);
	PyModule_AddIntMacro(m, NONE);

	PyModule_AddIntMacro(m, URL_DECODER_PRESERVE_PERCENT);
	PyModule_AddIntMacro(m, URL_DECODER_REMOVE_PERCENT);
	PyModule_AddIntMacro(m, URL_DECODER_DECODE_INVALID);
	PyModule_AddIntMacro(m, URL_DECODER_STATUS_400);
}
