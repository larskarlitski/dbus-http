
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include "systemd-compat.h"
#include "dbus-http.h"
#include "dbus.h"
#include "json.h"
#include "log.h"
#include "environment.h"

#define _cleanup_(fn) __attribute__((__cleanup__(fn)))


typedef struct {
        char *destination;
        char *object;
        JsonValue *json;
        DBusNode *node;
        DBusMethod *method;
} MethodCallRequest;


static int bus_message_element_to_json(sd_bus_message *message, JsonValue **jsonp);

static inline void freep(void *p) {
        free(*(void **)p);
}

static void method_call_request_free(MethodCallRequest *request) {
        free(request->destination);
        free(request->object);
        if (request->json)
                json_value_free(request->json);
        if (request->node)
                dbus_node_free(request->node);
        free(request);
}

static void http_response_end_json(HttpResponse *response, int status, JsonValue *reply) {
        FILE *f;

        f = http_response_get_stream(response, "application/json");
        json_print(reply, f);
        http_response_end(response, status);
}

static void http_response_end_error(HttpResponse *response, int status, const char *name, const char *message) {
        _cleanup_(json_value_freep) JsonValue *reply;

        reply = json_object_new();
        json_object_insert_string(reply, "error", name);

        if (message)
                json_object_insert_string(reply, "message", message);

        http_response_end_json(response, status, reply);
}

static void http_response_end_dbus_error(HttpResponse *response, const sd_bus_error *error) {
        int status;

        if (strcmp(error->name, "org.freedesktop.DBus.Error.UnknownMethod") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.UnknownObject") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.UnknownInterface") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.UnknownProperty") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.InvalidSignature") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.InvalidArgs") == 0)
                status = 400;
        else if (strcmp(error->name, "org.freedesktop.DBus.Error.AccessDenied") == 0)
                status = 403;
        else if (strcmp(error->name, "org.freedesktop.DBus.Error.ServiceUnknown") == 0 ||
                 strcmp(error->name, "org.freedesktop.DBus.Error.NameHasNoOwner") == 0)
                status = 404;
        else if (strcmp(error->name, "org.freedesktop.DBus.Error.NoReply") == 0 ||
                 strcmp(error->name, "org.freedesktop.DBus.Error.Timeout") == 0)
                status = 408;
        else
                status = 500;

        log_err("dbus error: %s", error->name);
        http_response_end_error(response, status, error->name, error->message);
}


static int bus_message_dict_entry_to_json(sd_bus_message *message, char **keyp, JsonValue **valuep) {
        char type;
        const char *contents;
        const char *key;
        _cleanup_(json_value_freep) JsonValue *value = NULL;
        int r;

        r = sd_bus_message_peek_type(message, &type, &contents);
        if (r < 0)
                return r;

        r = sd_bus_message_enter_container(message, type, contents);
        if (r < 0)
                return r;

        switch (contents[0]) {
                case SD_BUS_TYPE_STRING:
                case SD_BUS_TYPE_OBJECT_PATH:
                case SD_BUS_TYPE_SIGNATURE: {
                        r = sd_bus_message_read_basic(message, contents[0], &key);
                        if (r < 0)
                                return r;

                        *keyp = strdup(key);
                        break;
                }

                case SD_BUS_TYPE_BYTE: {
                        uint8_t num;
                        r = sd_bus_message_read_basic(message, contents[0], &num);
                        if (r < 0)
                                return r;
                        asprintf(keyp, "%" PRIu8, num);
                        break;
                }

                case SD_BUS_TYPE_INT16: {
                        int16_t num;
                        r = sd_bus_message_read_basic(message, contents[0], &num);
                        if (r < 0)
                                return r;
                        asprintf(keyp, "%" PRId16, num);
                        break;
                }

                case SD_BUS_TYPE_UINT16: {
                        uint16_t num;
                        r = sd_bus_message_read_basic(message, contents[0], &num);
                        if (r < 0)
                                return r;
                        asprintf(keyp, "%" PRIu16, num);
                        break;
                }

                case SD_BUS_TYPE_INT32: {
                        int32_t num;
                        r = sd_bus_message_read_basic(message, contents[0], &num);
                        if (r < 0)
                                return r;
                        asprintf(keyp, "%" PRId32, num);
                        break;
                }

                case SD_BUS_TYPE_UINT32: {
                        uint32_t num;
                        r = sd_bus_message_read_basic(message, contents[0], &num);
                        if (r < 0)
                                return r;
                        asprintf(keyp, "%" PRIu32, num);
                        break;
                }

                case SD_BUS_TYPE_INT64: {
                        int64_t num;
                        r = sd_bus_message_read_basic(message, contents[0], &num);
                        if (r < 0)
                                return r;
                        asprintf(keyp, "%" PRId64, num);
                        break;
                }

                case SD_BUS_TYPE_UINT64: {
                        uint64_t num;
                        r = sd_bus_message_read_basic(message, contents[0], &num);
                        if (r < 0)
                                return r;
                        asprintf(keyp, "%" PRIu64, num);
                        break;
                }

                case SD_BUS_TYPE_DOUBLE: {
                        double num;
                        r = sd_bus_message_read_basic(message, contents[0], &num);
                        if (r < 0)
                                return r;
                        asprintf(keyp, "%f", num);
                        break;
                }

                default:
                        return -ENOTSUP;
        }

        r = bus_message_element_to_json(message, &value);
        if (r < 0)
                return r;

        r = sd_bus_message_exit_container(message);
        if (r < 0)
                return r;

        key = NULL;

        *valuep = value;
        value = NULL;

        return 0;
}

static int bus_message_element_to_json(sd_bus_message *message, JsonValue **jsonp) {
        _cleanup_(json_value_freep) JsonValue *json = NULL;
        const char *contents = NULL;
        char type;
        int r;

        r = sd_bus_message_peek_type(message, &type, &contents);
        if (r < 0) {
                log_err("sd_bus_message_peek_type internal error");
                return r;
        }
        if (r == 0){
                log_err("sd_bus_message_peek_type unknown dbus type");
                return 0;
        }

        switch (type) {
                case SD_BUS_TYPE_BOOLEAN: {
                        uint8_t b;
                        r = sd_bus_message_read_basic(message, type, &b);
                        if (r < 0)
                                return r;
                        json = json_boolean_new(b);
                        break;
                }

                case SD_BUS_TYPE_BYTE: {
                        uint8_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_INT16: {
                        int16_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_UINT16: {
                        uint16_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_INT32: {
                        int32_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_UINT32: {
                        uint32_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_INT64: {
                        int64_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_UINT64: {
                        uint64_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_DOUBLE: {
                        double num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_STRING:
                case SD_BUS_TYPE_OBJECT_PATH:
                case SD_BUS_TYPE_SIGNATURE: {
                        const char *string;
                        r = sd_bus_message_read_basic(message, type, &string);
                        if (r < 0)
                                return r;
                        json = json_string_new(string);
                        break;
                }

                case SD_BUS_TYPE_ARRAY:
                case SD_BUS_TYPE_STRUCT:
                        r = sd_bus_message_enter_container(message, type, contents);
                        if (r < 0)
                                return r;

                        if (contents[0] == SD_BUS_TYPE_DICT_ENTRY_BEGIN) {
                                json = json_object_new();
                                while (!sd_bus_message_at_end(message, false)) {
                                        _cleanup_(freep) char *key = NULL;
                                        _cleanup_(json_value_freep) JsonValue *element = NULL;

                                        r = bus_message_dict_entry_to_json(message, &key, &element);
                                        if (r < 0)
                                                return r;

                                        r = json_object_insert(json, key, element);
                                        if (r < 0)
                                                return r;

                                        element = NULL;
                                }
                        } else {
                                json = json_array_new();
                                while (!sd_bus_message_at_end(message, false)) {
                                        _cleanup_(json_value_freep) JsonValue *element = NULL;

                                        r = bus_message_element_to_json(message, &element);
                                        if (r < 0)
                                                return r;

                                        r = json_array_append(json, element);
                                        if (r < 0)
                                                return r;

                                        element = NULL;
                                }
                        }

                        r = sd_bus_message_exit_container(message);
                        if (r < 0)
                                return r;
                        break;

                case SD_BUS_TYPE_VARIANT:
                        r = sd_bus_message_enter_container(message, type, contents);
                        if (r < 0)
                                return r;

                        r = bus_message_element_to_json(message, &json);
                        if (r < 0)
                                return r;

                        r = sd_bus_message_exit_container(message);
                        if (r < 0)
                                return r;
                        break;

                case SD_BUS_TYPE_UNIX_FD:
                        log_err("UNIX FD is not supported");
                        return -ENOTSUP;
                default:
                        log_err("Data type %d is not supported.", type);
        }

        *jsonp = json;
        json = NULL;

        return 0;
}

static int bus_message_to_json(sd_bus_message *message, JsonValue **jsonp, DBusMethod *method) {
        _cleanup_(json_value_freep) JsonValue *json = NULL;
        int r;

        json = json_object_new();

        for (size_t i = 0; i < method->n_out_args; i++) {
                _cleanup_(json_value_freep) JsonValue *element = NULL;

                if (sd_bus_message_at_end(message, false)){
                        log_err("sd_bus_message_at_end failed.");
                        return -EINVAL;
                }

                r = bus_message_element_to_json(message, &element);
                if (r < 0){
                        log_err("bus_message_element_to_json failed.");
                        return r;
                }

                r = json_object_insert(json, method->out_args[i]->name, element);
                if (r < 0){
                        log_err("json_object_insert failed.");
                        return r;
                }
                element = NULL;
        }

        if (!sd_bus_message_at_end(message, false)){
                log_err("!sd_bus_message_at_end failed.");
                return -EINVAL;
        }

        *jsonp = json;
        json = NULL;

        return 0;
}

static int bus_message_append_number(sd_bus_message *message, char type, double number) {
        switch (type) {
                case SD_BUS_TYPE_BOOLEAN:
                case SD_BUS_TYPE_BYTE: {
                        uint8_t num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }
                case SD_BUS_TYPE_INT16: {
                        int16_t num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }
                case SD_BUS_TYPE_UINT16: {
                        uint16_t num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }
                case SD_BUS_TYPE_INT32: {
                        int32_t num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }
                case SD_BUS_TYPE_UINT32: {
                        uint32_t num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }

                case SD_BUS_TYPE_INT64: {
                        int64_t num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }
                case SD_BUS_TYPE_UINT64: {
                        uint64_t num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }
                case SD_BUS_TYPE_DOUBLE: {
                        double num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }
                default:
                        return -EINVAL;
        }

        return 0;
}

static int bus_message_append_from_json(sd_bus_message *message, JsonValue *json, const char *type, unsigned int* type_inc) {
        if (bus_type_is_number(*type)) {
                if (json_value_get_type(json) != JSON_TYPE_NUMBER)
                        return -EINVAL;
                *type_inc = 1;
                return bus_message_append_number(message, *type, json_value_get_number(json));
        }

        switch (*type) {
                case SD_BUS_TYPE_ARRAY: {
                        int r;
                        JsonValue *j_a_element;
                        size_t signature_len;
                        unsigned int sub_type_inc = 0;

                        // array
                        if(json_value_get_type(json) != JSON_TYPE_ARRAY && json_value_get_type(json) != JSON_TYPE_OBJECT){
                                log_err("DBUS interface expected array");
                                return -EINVAL;
                        }

                        r = signature_element_length(type+1, &signature_len);
                        if (r < 0) {
                                log_err("Invalid array signature.");
                                return r;
                        }
                        {
                                size_t ja_len;
                                char sub_signature[signature_len + 1];
                                memcpy(sub_signature, type+1, signature_len);
                                sub_signature[signature_len] = 0;


                                r = sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, sub_signature);
                                if (r < 0){
                                        log_err("Cannot create dbus array container");
                                        return -EINVAL;
                                }

                                if(json_value_get_type(json) == JSON_TYPE_ARRAY){
                                        ja_len = json_array_get_length(json);
                                        log_debug("Parsing array of length %d, %s", ja_len, type);
                                        for(size_t i=0; i<ja_len; i++) {
                                                if(json_array_get(json, i, &j_a_element, 0) == false) {  // TODO: validate expected type: use type expected by dbus instead of 0
                                                        log_err("Fetching value from array failed");
                                                        return -EINVAL;
                                                }

                                                r = bus_message_append_from_json(message, j_a_element, sub_signature, &sub_type_inc);
                                                if ( r < 0) {
                                                        return r;
                                                }
                                        }
                                }else{  // it's json object to dbus dict a{
                                        log_debug("Parsing array as dict %s", type);
                                        r = bus_message_append_from_json(message, json, sub_signature, &sub_type_inc);
                                        if ( r < 0) {
                                                return r;
                                        }
                                }
                        }
                        r = sd_bus_message_close_container(message);
                        if (r < 0){
                                log_err("Closing dbus container failed");
                                return r;
                        }
                        *type_inc = 1 + signature_len;  // +a
                        return 0;
                }

                case SD_BUS_TYPE_STRUCT_BEGIN: {
                        size_t ja_len;
                        int r;
                        JsonValue *a_json;
                        size_t signature_len;

                        if(json_value_get_type(json) != JSON_TYPE_ARRAY){
                                log_err("DBUS interface expected json array or object");
                                return -EINVAL;
                        }
                        ja_len = json_array_get_length(json);

                        // get length of e.g. (i(vy)i) --> 8
                        r = signature_element_length(type, &signature_len);
                        if (r < 0) {
                                log_err("Invalid struct entry signature.");
                                return r;
                        }
                        {
                                // get the signature inside the brackets e.g. (i(vy)i) --> i(vy)i
                                char sub_signature[signature_len-1];
                                char* sub_signature_p = sub_signature;
                                memcpy(sub_signature, type + 1, signature_len - 2);
                                sub_signature[signature_len - 2] = 0;

                                log_debug("Parsing struct of lenght %d, %s", ja_len, sub_signature);

                                // TODO: Check if container must be opened and closed in the for loop
                                r = sd_bus_message_open_container(message, SD_BUS_TYPE_STRUCT, sub_signature);
                                if (r < 0){
                                        log_err("Creating struct container failed.");
                                        return -EINVAL;
                                }

                                for(size_t i=0; i<ja_len; i++) {
                                        unsigned int sub_type_inc = 0;

                                        if(json_array_get(json, i, &a_json, 0) == false) {
                                                log_err("Fetching value from array failed");
                                                return -EINVAL;
                                        }

                                        r = bus_message_append_from_json(message, a_json, sub_signature_p, &sub_type_inc);
                                        if ( r < 0) {
                                                log_err("Appending value to dbus struct failed");
                                                return r;
                                        }
                                        sub_signature_p += sub_type_inc;
                                }
                        }
                        *type_inc = signature_len;
                        r = sd_bus_message_close_container(message);
                        if (r < 0){
                                log_err("Closing struct container failed.");
                        }
                        return r;
                }

                case SD_BUS_TYPE_DICT_ENTRY_BEGIN: {
                        int r;
                        size_t signature_len;

                        if(json_value_get_type(json) != JSON_TYPE_OBJECT){
                                log_err("DBUS interface expected json object -> dict");
                                return -EINVAL;
                        }

                        // get length of e.g. (i(vy)i) --> 8
                        r = signature_element_length(type, &signature_len);
                        if (r < 0) {
                                log_err("Invalid struct entry signature.");
                                return r;
                        }
                        {
                                // Json dict iterator
                                JsonObjectEntry* joe;
                                _cleanup_(json_object_iterator_freep) JsonObjectIterator* joiter = json_object_iterator_new(json);

                                // get the signature inside the brackets e.g. {i(vy)} --> i(vy)
                                char sub_signature[signature_len-1];
                                memcpy(sub_signature, type + 1, signature_len - 2);
                                sub_signature[signature_len - 2] = 0;

                                if(joiter == NULL){
                                        log_err("Invalid json object");
                                        return -EINVAL;
                                }

                                log_debug("Parsing object %s", sub_signature);

                                joe = json_object_iterator_next(joiter);
                                while(joe){
                                        unsigned int sub_type_inc = 0;
                                        char* key = json_object_entry_key(joe);
                                        JsonValue* value = json_object_entry_value(joe);

                                        r = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, sub_signature);
                                        if (r < 0){
                                                log_err("Creating dict container failed.");
                                                return -EINVAL;
                                        }

                                        log_debug("Adding key: %s", key);
                                        r = sd_bus_message_append_basic(message, SD_BUS_TYPE_STRING, key);  // TODO: convert to any basic type, not just string
                                        if ( r < 0) {
                                                log_err("DBUS appending key of dict failed");
                                                return r;
                                        }

                                        r = bus_message_append_from_json(message, value, sub_signature + 1, &sub_type_inc);
                                        if ( r < 0) {
                                                log_err("DBUS appending value of dict failed");
                                                return r;
                                        }
                                        r = sd_bus_message_close_container(message);
                                        if ( r < 0) {
                                                log_err("CLosing dict entry container failed");
                                                return r;
                                        }

                                        joe = json_object_iterator_next(joiter);
                                }
                        }
                        *type_inc = signature_len;
                        return 0;
                }

                case SD_BUS_TYPE_VARIANT: {
                        int r;
                        JsonType j_type;
                        JsonValue *json_copy = json;
                        char dbus_variant_sign[2] = { _SD_BUS_TYPE_INVALID, '\0' };
                        const char *p_dbus_variant_sign = dbus_variant_sign;
                        unsigned int sub_type_inc = 0;

                        j_type = json_value_get_type(json);
                        switch(j_type) {
                                case JSON_TYPE_OBJECT: {
                                        JsonValue *valuep;

                                        if(json_object_lookup(json, "dbus_variant_sign", &valuep, JSON_TYPE_STRING)) {
                                                p_dbus_variant_sign = json_value_get_string(valuep);
                                                if(json_object_lookup(json, "data", &valuep, 0)) {
                                                        json_copy = valuep;
                                                }
                                        }
                                        break;
                                }
                                case JSON_TYPE_ARRAY:
                                        log_err("Variant is expected: Array needs to be passed as an object containing the dbus signature and the data.");
                                        return -EINVAL;
                                case JSON_TYPE_STRING:
                                        dbus_variant_sign[0] = SD_BUS_TYPE_STRING;
                                        break;
                                case JSON_TYPE_NUMBER:
                                        log_err("Variant is expected: Numbers need to be passed as an object containing the dbus signature and the data.");
                                        return -EINVAL;
                                case JSON_TYPE_TRUE:
                                case JSON_TYPE_FALSE:
                                        dbus_variant_sign[0] = SD_BUS_TYPE_BOOLEAN;
                                        break;
                                // case JSON_TYPE_NULL:
                                //        d_type = SD_BUS_TYPE_BYTE;
                                default:
                                        dbus_variant_sign[0] = _SD_BUS_TYPE_INVALID;
                        };

                        r = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, p_dbus_variant_sign);
                        if (r < 0){
                                log_err("Opening variant container failed");
                                return -EINVAL;
                        }

                        r = bus_message_append_from_json(message, json_copy, p_dbus_variant_sign, &sub_type_inc);
                        if ( r < 0) {
                                log_err("Appending variant failed");
                                return r;
                        }
                        *type_inc = 1;
                        r = sd_bus_message_close_container(message);
                        if (r < 0){
                                log_err("Closing variant container failed.");
                        }
                        return r;
                }

                case SD_BUS_TYPE_UNIX_FD:
                        return -ENOTSUP;

                case SD_BUS_TYPE_BOOLEAN: {
                        bool b;

                        if (json_value_get_type(json) == JSON_TYPE_TRUE)
                                b = true;
                        else if (json_value_get_type(json) == JSON_TYPE_FALSE)
                                b = false;
                        else
                                return -EINVAL;

                        *type_inc = 1;
                        return sd_bus_message_append_basic(message, 'b', &b);
                }

                case SD_BUS_TYPE_STRING:
                case SD_BUS_TYPE_OBJECT_PATH:
                case SD_BUS_TYPE_SIGNATURE: {
                        if (json_value_get_type(json) != JSON_TYPE_STRING)
                                return -EINVAL;

                        *type_inc = 1;
                        return sd_bus_message_append_basic(message, *type, json_value_get_string(json));
                }
                default:
                        return -EINVAL;
        }

        return -EINVAL;
}

static int bus_message_append_args_from_json(sd_bus_message *message, DBusMethod *method, JsonValue *args) {
        size_t n_args;
        int r;
        unsigned int sub_type_inc = 0;

        n_args = json_array_get_length(args);

        if (method->n_in_args != n_args)
                return -EINVAL;

        for (size_t i = 0; i < n_args; i++) {
                JsonValue *arg;

                json_array_get(args, i, &arg, 0);

                r = bus_message_append_from_json(message, arg, method->in_args[i]->type, &sub_type_inc);
                if (r < 0)
                        return r;
        }

        return 0;
}

static int get_properties_finished(sd_bus_message *message, void *userdata, sd_bus_error *ret_error) {
        HttpResponse *response = userdata;
        const sd_bus_error *error;
        _cleanup_(json_value_freep) JsonValue *reply = NULL;
        int r;

        error = sd_bus_message_get_error(message);
        if (error) {
                http_response_end_dbus_error(response, error);
                return 0;
        }

        r = bus_message_element_to_json(message, &reply);
        if (r < 0) {
                http_response_end(response, 500);
                return 0;
        }

        http_response_end_json(response, 200, reply);
        return 0;
}

static int method_call_finished(sd_bus_message *message, void *userdata, sd_bus_error *ret_error) {
        HttpResponse *response = userdata;
        MethodCallRequest *request = http_response_get_user_data(response);
        const sd_bus_error *error;
        _cleanup_(json_value_freep) JsonValue *reply = NULL;
        int r;

        log_info("get properties from dbus");
        error = sd_bus_message_get_error(message);
        if (error) {
                log_err("sd_bus_message_get_error failed");
                http_response_end_dbus_error(response, error);
                return 0;
        }

        r = bus_message_to_json(message, &reply, request->method);
        if (r < 0) {
                log_err("bus_message_to_json failed");
                http_response_end(response, 500);
                return 0;
        }

        http_response_end_json(response, 200, reply);
        return 0;
}

static int introspect_finished(sd_bus_message *message, void *userdata, sd_bus_error *ret_error) {
        HttpResponse *response = userdata;
        MethodCallRequest *request = http_response_get_user_data(response);
        const sd_bus_error *error;
        const char *xml;
        _cleanup_(sd_bus_message_unrefp) sd_bus_message *method_message = NULL;
        const char *interface;
        const char *method_name;
        JsonValue *args;
        int r;

        log_debug("dbus introspection");

        error = sd_bus_message_get_error(message);
        if (error) {
                http_response_end_dbus_error(response, error);
                return 0;
        }

        r = sd_bus_message_read(message, "s", &xml);
        if (r < 0) {
                log_err("dbus read failed");
                http_response_end(response, 500);
                return 0;
        }

        r = dbus_node_new_from_xml(&request->node, xml);
        if (r < 0) {
                log_err("dbus_node_new_from_xml failed");
                http_response_end(response, 500);
                return 0;
        }

        if (!json_object_lookup_string(request->json, "interface", &interface) ||
            !json_object_lookup_string(request->json, "method", &method_name) ||
            !json_object_lookup(request->json, "arguments", &args, JSON_TYPE_ARRAY)) {
                log_err("Request requires parameter: interface, method, arguments[]!");
                http_response_end_error(response, 400, "Invalid request", NULL);
                return 0;
        }

        request->method = dbus_node_find_method(request->node, interface, method_name);
        if (!request->method) {
                log_err("Invalid dbus method: %s", method_name);
                http_response_end_error(response, 400, "No such method", NULL);
                return 0;
        }

        r = sd_bus_message_new_method_call(sd_bus_message_get_bus(message),
                                           &method_message, request->destination, request->object, interface, method_name);
        if (r < 0) {
                log_err("sd_bus_message_new_method_call failed.");
                http_response_end(response, 500);
                return 0;
        }

        r = bus_message_append_args_from_json(method_message, request->method, args);
        if (r == -EINVAL) {
                log_err("dbus request with invalid parameters");
                http_response_end_error(response, 400, "Invalid request", NULL);
                return 0;
        } else if (r < 0) {
                log_err("dbus request unknown error");
                http_response_end(response, 500);
                return 0;
        }

        log_debug("dbus call to %s %s %s", request->destination, request->object, request->method->name);
        r = sd_bus_call_async(sd_bus_message_get_bus(message), NULL, method_message, method_call_finished, response, 0);

        return 0;
}

static int parse_url(const char *url, char **namep, char **objectp) {
        const char *p;
        char *name;
        char *object;

        if (url[0] != '/')
                return -EINVAL;

        p = strchr(url + 1, '/');
        if (p) {
                name = strndup(url + 1, p - url - 1);
                object = strdup(p);
        } else {
                name = strdup(url + 1);
                object = strdup("/");
        }

        if (namep)
                *namep = name;

        if (objectp)
                *objectp = object;

        return 0;
}

HttpServerHandlerStatus handle_get_dbus(const char *path, HttpResponse *response, void *userdata) {
        Environment *env = userdata;

        if (strncmp(env->dbus_prefix, path, strlen(env->dbus_prefix)) == 0) {  // starts with dbus_prefix
                const char *dbus_path = &path[strlen(env->dbus_prefix) - 1]; // dbus_path must start with a slash!
                sd_bus *bus = env->bus;
                _cleanup_(freep) char *name = NULL;
                _cleanup_(freep) char *object = NULL;
                int r;
                const char prop_interface[] = "org.freedesktop.DBus.Properties";
                const char prop_func[] = "GetAll";

                r = parse_url(dbus_path, &name, &object);
                if (r < 0) {
                        http_response_end(response, 400);
                        return HTTP_SERVER_HANDLED_ERROR;
                }

                http_suspend_connection(response);

                r = sd_bus_call_method_async(bus, NULL, name, object, prop_interface, prop_func,
                                get_properties_finished, response, "s", "");
                if (r == -EINVAL) {
                        log_err("handle_get_dbus got EINVAL from dbus call %s %s %s %s", name, object, prop_interface, prop_func);
                        http_response_end(response, 400);
                        return HTTP_SERVER_HANDLED_ERROR;
                }
                else if (r < 0) {
                        log_err("handle_get_dbus error in call %s %s %s %s", name, object, prop_interface, prop_func);
                        http_response_end(response, 500);
                        return HTTP_SERVER_HANDLED_ERROR;
                }
                log_info("handle_get_dbus handled URL %s", path);
                return HTTP_SERVER_HANDLED_SUCCESS;
        }
        log_debug("handle_get_dbus ignored URL %s", path);
        return HTTP_SERVER_HANDLED_IGNORED;
}

HttpServerHandlerStatus handle_post_dbus(const char *path, void *body, size_t len, HttpResponse *response, void *userdata) {
        Environment *env = userdata;

        if (strncmp(env->dbus_prefix, path, strlen(env->dbus_prefix)) == 0) {  // starts with dbus_prefix
                const char *dbus_path = &path[strlen(env->dbus_prefix) - 1]; // dbus_path must start with a slash!
                sd_bus *bus = env->bus;
                MethodCallRequest *request;
                int r;

                if (!body) {
                        log_err("POST to URL %s without body", path);
                        http_response_end(response, 400);
                        return HTTP_SERVER_HANDLED_ERROR;
                }

                request = calloc(1, sizeof(MethodCallRequest));
                http_response_set_user_data(response, request, (void (*)(void *))method_call_request_free);

                r = parse_url(dbus_path, &request->destination, &request->object);
                if (r < 0) {
                        log_err("POST to with invalid dbus URL %s", path);
                        http_response_end(response, 400);
                        return HTTP_SERVER_HANDLED_ERROR;
                }

                r = json_parse(body, &request->json, JSON_TYPE_OBJECT);
                if (r < 0) {
                        log_err("POST to %s with invalid JSON", path);
                        http_response_end(response, 400);
                        return HTTP_SERVER_HANDLED_ERROR;
                }

                http_suspend_connection(response);

                r = sd_bus_call_method_async(bus, NULL, request->destination, request->object,
                                "org.freedesktop.DBus.Introspectable", "Introspect", introspect_finished, response, NULL);
                if (r < 0) {
                        log_err("handle_post_dbus introspection error for %s %s", request->destination, request->object);
                        http_response_end(response, 400);
                        return HTTP_SERVER_HANDLED_ERROR;
                }
                log_info("handle_post_dbus handled URL %s", path);
                return HTTP_SERVER_HANDLED_SUCCESS;
        }
        log_debug("handle_post_dbus ignored URL %s", path);
        return HTTP_SERVER_HANDLED_IGNORED;
}
