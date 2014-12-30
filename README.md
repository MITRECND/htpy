htpy
====

Python bindings to libhtp

Description
===========
htpy is a python interface to libhtp, which is an HTTP parser library. libhtp
is used by suricata and is complete, permissive and fast enough. ;)

Using htpy
==========
The best way to describe how to use htpy is to see it in action. Let's setup an
example where we want to see the Host header and the path part of the URI for a
request. In the example below ''req'' is the HTTP request as a string. How you
get this has nothing to do with htpy. We tend to use pynids and feed the
request data in as we get it. You do not have to buffer the entire request up
and pass it in all at once.

<pre>
import htpy

def uri_callback(cp):
    uri = cp.get_uri()
    if 'path' in uri:
        print uri['path']
    return htpy.HTP_OK

def request_headers_callback(cp):
    host = cp.get_request_header('Host')
    if host:
        print host
    return htpy.HTP_OK

cp = htpy.init()
cp.register_request_uri_normalize(uri_callback)
cp.register_request_headers(request_headers_callback)
cp.req_data(req)
</pre>

Details
=======
Initialization
--------------
There are two ways to start using htpy. The first is to call htpy.init(), which
returns a connection parser object.
<pre>
cp = htpy.init()
</pre>

The second way, using htpy.config() and htpy.connp(), provides more flexibility
though such flexibility is not needed in the common case.

<pre>
cfg = htpy.config()
cp = htpy.connp(cfg)
</pre>

htp.config() returns a configuration object which has various properties that
can be altered before generating a connection parser based upon that
configuration. Once a connection parser has been generated with a given config
it's internal configuration can not be altered.

Callbacks
---------
Callbacks are used heavily throughout htpy as a way to act on data as it is
parsed. The basic premise is to pass data into register callbacks for the
states in the parser you are interested in and then pass the data to the
parser. As the data is parsed (and possibly altered) the appropriate callbacks
will be called.

Return values
-------------
Callback functions must return one of the following values.
* htpy.HTP_OK
* htpy.HTP_ERROR
* htpy.HTP_STOP
* htpy.HTP_DECLINE (XXX: What does this do?)

###HTP_STOP vs HTP_ERROR
When libhtp is given the HTP_ERROR return value it sets the parser state to an
error and will refuse to parse any further. This manifests in htpy as an
htpy_error exception. This is considered a fatal error. The difference between
HTP_STOP and HTP_ERROR is that HTP_STOP will raise a htpy_stop exception,
which should be caught in handleStream() and tcp.stop() should be called when
the exception is caught. The reason you can not call tcp.stop() from a callback
is that the tcp object passed into handleStream() is created on the fly each
time handleStream() is called. This means that if you pass that object into a
callback via the cp.set_obj() method you will be passing in an old tcp object
on every subsequent call into handleStream() beyond the first.

Registering callbacks
---------------------
Callbacks are registered with the connection parser object. They can be any
callable python block. There are a number of callbacks, which fall into three
categories:

<b>Regular callbacks:</b>
* request_start: Called as soon as a new transaction is started.
* request_line: Called after the request line has been parsed.
* request_uri_normalize: Called right before the URI is normalized. When a URI
  is normalized default fields are filled in if they are not specified. This
  hook is useful if you are going to normalize a URI but still want to know
  what was provided by the request as opposed to what was filled in by the
  parser.
* request_headers: Called right after all the headers have been parsed and
  basic sanity checks have passed.
* request_trailer: Called right after all the request headers have been parsed.
* request_complete: Called right after an entire request is parsed.
* response_start: Called as soon as the parser is ready for a response.
* response_line: Called right after the response line has been parsed.
* response_headers: Called right after all the headers have been parsed and
  basic sanity checks have passed.
* response_trailer: Called right after all the response headers have been
  parsed.
* response_complete: Called right after an entire response is parsed.

<b>Transaction callbacks:</b>
* request_header_data: Called whenever request header data is available.
* response_header_data: Called whenever response header data is available.
* request_body_data: Called whenever a body data chunk is processed.
* response_body_data: Called whenever response body data is available. Gzip
  compressed data should be properly decompressed.
* request_trailer_data: Called whenever request trailer data is available.
* response_trailer_data: Called whenever response trailer data is available.

<b>Request File Data callback:</b>
* request_file_data: Called whenever file data is found in a request.

<b>Log callbacks:</b>
* log: Called for every log message that should be handled.

Registering callbacks
---------------------
Registering a callback is done with the appropriate method within the
connection parser object. The method for any callback is the name of the
callback pre-pended with "register_". For example, to register a response
callback:
<pre>
def response_callback(cp):
    print "INSIDE RESPONSE CALLBACK"
    return htpy.HTP_OK

cp = htpy.init()
cp.register_response(response_callback)
</pre>

All registrations take one parameter, the python function to call. The only
exception to this rule is the request_file_data callback. This registration
can take an optional second argument which is used to tell libhtp if it
should write the file data to disk. For example:
<pre>
def file_data_callback(data):
	print "INSIDE FILE DATA CALLBACK"
	return htpy.HTP_OK

cp = htpy.init()
cp.register_request_file_data(file_data_callback, True)
</pre>

The "True" above is the optional argument used to turn on file carving. If
this argument is not provided then file carving will be left off (the
default state).

Callback definitions
--------------------
###Regular callbacks
Regular callbacks are passed one argument:

* cp: The connection parser.

<pre>
def request_uri_normalize_callback(cp):
    uri = cp.get_uri()
    if query in uri:
        print uri['query']
    return htpy.HTP_OK
</pre>

###Transaction callbacks
Transaction callbacks are passed two arguments:

* data: The transaction data.
* length: The length of the data.

<pre>
def response_body_data_callback(data, length):
    print "Got %i bytes: %s" % (length, data)
    return htpy.HTP_OK
</pre>

###Log callback
Log callbacks are passed three arguments:

* cp: The connection parser from which this log message was generated.
* msg: A string containing the log message.
* level: An integer. The level of the log message. See documentation about
  setting the log_level of a configuration object if desired.

<pre>
def log_callback(cp, msg, level):
    # elog['level'] is the same as level
    # elog['msg'] is the same as msg
    # When level == htpy.LOG_ERROR you can get a dictionary of the error
    # by calling get_last_error().
    if level == htpy.HTP_LOG_ERROR:
        elog = cp.get_last_error()
        if elog == None:
            return htpy.HTP_ERROR
        print "%s:%i - %s (%i)" % (elog['file'], elog['line'], elog['msg'], elog['level'])
    else:
        print "%i - %s" % (level, msg)
    return htpy.HTP_OK
</pre>

###Request file data callback
Request file data callbacks are passed one argument:

* data: A dictionary which has the following key/value pairs:
 * data: A blob of the data that has been parsed.
 * filename: The filename parsed out of the data. This is an optional entry
   in the dictionary. If libhtp can not find the filename then it will be
   left out of the dictionary.
 * tmpname: The filename on disk where the data is being written. This is an
   optional entry in the dictionary. If libhtp is not carving the file then
   it will be left out of the dictionary.

<pre>
def file_data_callback(data):
	print "Wrote %i bytes to %s for %s" % (len(data['data']), data['tmpname'], data['filename'])
	return htpy.HTP_OK

cp = htpy.init()
cp.register_request_file_data(file_data_callback, True)
</pre>

Sending an object to callbacks
------------------------------
It is possible to pass an arbitrary object to each callback. This object
will be passed to each callback as the last argument. When using this the
definition of each callback must take this into account:

<pre>
x = "FOO"

def request_uri_normalize_callback(cp, obj):
    uri = cp.get_uri()
    if query in uri:
        print uri['query']
    print obj
    return htpy.HTP_OK

cp = htpy.init()
cp.set_obj(x)
cp.register_request_uri_normalize(request_uri_normalize_callback)
cp.req_data(req)
</pre>

It is also possible to remove this object using the ''del_obj()'' method of the
connection parser.

###EXCEPTION
The only callback which can not be passed an arbitrary object is the
request_file_data callback. This is due to a limitation in libhtp.

Sending data to the parser
--------------------------
Sending data to the parser uses one of two functions:

<pre>
cp.req_data(req)
cp.res_data(res)
</pre>

You use ''req_data()'' to send request data as you get it. You use ''res_data''
to send response data as you get it. There is no requirement to pass the entire
request or response data in one shot, you can send data to the parser as you
get it.

Objects
=======
Config object
-------------
###Methods
Configuration objects have no methods. All their functionality is exposed as
attributes that can be manipulated.

###Attributes
Configuration objects contain the following attributes. In many cases the
value being set is not sanity checked. Using the wrong value can potentially
alter the parser in odd ways.

* log_level: Used when deciding whether to store or ignore the messages issued
  by the parser. If the level of a log message is less than the configuration
  log level, the message will be ignored. Htpy provides macros that map
  directly to the levels used internally by libhtp.
 * htpy.HTP_LOG_ERROR (1)
 * htpy.HTP_LOG_WARNING (2)
 * htpy.HTP_LOG_NOTICE (3, DEFAULT)
 * htpy.HTP_LOG_INFO (4)
 * htpy.HTP_LOG_DEBUG (5)
 * htpy.HTP_LOG_DEBUG2 (6)
* tx_auto_destroy: Automatically destroy transactions when done.

Connection parser object
------------------------
###Methods
* get_request_header(string): Given a header (string) return a string that is
  the value of that header.
* get_response_header(string): Given a header (string) return a string that is
  the value of that header.
* get_all_request_headers(): Return a dictionary of all request headers. The
  key will be the header name and the value will be the header value.
* get_all_response_headers(): Return a dictionary of all response headers. The
  key will be the header name and the value will be the header value.
* get_response_status(): Return the status code of the response as an integer.
* get_response_status_string(): Return the status code of the response as a
  string.
* get_request_line(): Return the request line with method, path and host in one line.
* get_response_line(): Return the response line with protocol, status code and status message in one line.
* get_request_message_len(): Return the request message length before decompressed and dechunked.
* get_response_message_len(): Return the response message length before decompressed and dechunked.
* get_request_entity_len(): Return the request message length after decompressed and dechunked.
* get_response_entity_len(): Return the request message length after decompressed and dechunked.
* set_obj(object): Pass ''object'' to each callback as the last argument. Using
  this without altering the callback definition to account for the new object
  will cause htpy to raise an error when calling your callback.
* del_obj(object): Stop passing ''object'' to each callback as the last
  argument. XXX: Does it make sense to have this? Removing an object but
  still using the callback definition that expects it will cause problems
* req_data(data): Send ''data'' into the parser. The data will be treated as a
  request. You do not have to send the entire request at once, you can send it
  into the parser as you get it. XXX: Document return value
* req_data_consumed(): Return the number of request bytes consumed by the
  parser.
* res_data(data): Send ''data'' into the parser. The data will be treated as a
  response. You do not have to send the entire response at once, you can send
  it into the parser as you get it. XXX: Document return value
* res_data_consumed(): Return the number of response bytes consumed by the
  parser.
* get_last_error(): Return a dictionary of the last log message with level
  htpy.HTP_LOG_ERROR. In the case of no error it will return None. The
  dictionary is:
<pre>
{ 'level': int,
  'msg': string,
  'file': string,
  'line': int }
</pre>
* clear_error(): Clear the last saved error. XXX: Is this worth keeping? A
  new error message will overwrite the old one.
* get_request_protocol(): Return the protocol as a string without version.
* get_request_protocol_number(): Return the protocol number. Prefer using htpy.HTP_PROTOCOL_* constants with output values.
* get_response_protocol(): Return the protocol as a string without version.
* get_response_protocol_number(): Return the protocol number. Prefer using htpy.HTP_PROTOCOL_* constants with output values.
* get_uri(): Return a dictionary of the parsed URI. Depending upon the
  normalization setting this dictionary can be the URI as it was parsed or as
  it was normalized. In the case of a normalized URI the missing parts wil be
  given default values. The dictionary is:
<pre>
{ 'scheme': string,
  'username': string,
  'password': string,
  'hostname': string,
  'port': string,
  'port_num': int,
  'path': string,
  'query': string,
  'fragment': string, }
</pre>
* get_method(): Return the request method as a string (GET, POST, HEAD, etc).

Each of the callback methods take a callable python function as the argument.
When the callbacks are called are documented elsewhere.
* register_transaction_start(callback)
* register_request_line(callback)
* register_request_uri_normalize(callback)
* register_request_headers(callback)
* register_request_body_data(callback)
* register_request_trailer(callback)
* register_request_complete(callback)
* register_response_start(callback)
* register_response_line(callback)
* register_response_headers(callback)
* register_response_body_data(callback)
* register_response_trailer(callback)
* register_response_complete(callback)
* register_log(callback)

###Attributes
The connection parser object contains the config object as a member, but
you should not touch it, ever.
