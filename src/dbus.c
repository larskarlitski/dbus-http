
#include "dbus.h"

#include <errno.h>
#include <expat.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <systemd/sd-bus.h>

static void * grow_pointer_array(void *array, size_t *allocedp) {
        if (*allocedp == 0)
                *allocedp = 8;
        else
                *allocedp *= 2;

        return realloc(array, *allocedp * sizeof(void *));
}

static DBusArgument * dbus_argument_free(DBusArgument *arg) {
        free(arg->name);
        free(arg->type);
        free(arg);

        return NULL;
}

static DBusMethod * dbus_method_free(DBusMethod *method) {
        for (size_t i = 0; i < method->n_in_args; i++)
                dbus_argument_free(method->in_args[i]);

        for (size_t i = 0; i < method->n_out_args; i++)
                dbus_argument_free(method->out_args[i]);

        free(method->name);
        free(method->in_args);
        free(method->out_args);
        free(method);

        return NULL;
}

static DBusProperty * dbus_property_free(DBusProperty *property) {
        free(property->name);
        free(property->type);
        free(property);

        return NULL;
}

static DBusMethod * dbus_interface_append_method(DBusInterface *interface, const char *name) {
        DBusMethod *method;

        method = calloc(1, sizeof(DBusMethod));
        method->name = strdup(name);

        if (interface->n_methods == interface->n_alloced_methods)
                interface->methods = grow_pointer_array(interface->methods, &interface->n_alloced_methods);
        interface->methods[interface->n_methods] = method;
        interface->n_methods += 1;

        return method;
}

static char * strdup_printf(const char *fmt, ...) {
        va_list ap;
        size_t len;
        char *string;

        va_start(ap, fmt);
        len = vsnprintf(NULL, 0, fmt, ap);
        va_end(ap);

        string = malloc(len + 1);

        va_start(ap, fmt);
        len = vsnprintf(string, len + 1, fmt, ap);
        va_end(ap);

        return string;
}

static DBusArgument * dbus_method_append_argument(DBusMethod *method, const char *name, const char *type, const char *direction) {
        DBusArgument *argument;
        bool in;

        if (strcmp(direction, "in") == 0)
                in = true;
        else if (strcmp(direction, "out") == 0)
                in = false;
        else
                return NULL;

        argument = calloc(1, sizeof(DBusArgument));

        /* name is optional */
        if (name)
                argument->name = strdup(name);
        else
                argument->name = strdup_printf("arg%u", in ? method->n_in_args : method->n_out_args);

        argument->type = strdup(type);

        if (in) {
                if (method->n_in_args == method->n_alloced_in_args)
                        method->in_args = grow_pointer_array(method->in_args, &method->n_alloced_in_args);
                method->in_args[method->n_in_args] = argument;
                method->n_in_args += 1;
        } else {
                if (method->n_out_args == method->n_alloced_out_args)
                        method->out_args = grow_pointer_array(method->out_args, &method->n_alloced_out_args);
                method->out_args[method->n_out_args] = argument;
                method->n_out_args += 1;
        }

        return argument;
}

static DBusProperty * dbus_interface_append_property(DBusInterface *interface, const char *name, const char *type, bool writable) {
        DBusProperty *property;

        property = calloc(1, sizeof(DBusProperty));
        property->name = strdup(name);
        property->type = strdup(type);
        property->writable = writable;

        if (interface->n_properties == interface->n_alloced_properties)
                interface->properties = grow_pointer_array(interface->properties, &interface->n_alloced_properties);
        interface->properties[interface->n_properties] = property;
        interface->n_properties += 1;

        return property;
}

static DBusInterface * dbus_interface_free(DBusInterface *interface) {
        for (size_t i = 0; i < interface->n_methods; i++)
                dbus_method_free(interface->methods[i]);

        for (size_t i = 0; i < interface->n_properties; i++)
                dbus_property_free(interface->properties[i]);

        free(interface->name);
        free(interface->methods);
        free(interface->properties);
        free(interface);

        return NULL;
}

static DBusInterface * dbus_node_append_interface(DBusNode *node, const char *name) {
        DBusInterface *interface;

        interface = calloc(1, sizeof(DBusInterface));
        interface->name = strdup(name);

        if (node->n_interfaces == node->n_alloced_interfaces)
                node->interfaces = grow_pointer_array(node->interfaces, &node->n_alloced_interfaces);
        node->interfaces[node->n_interfaces] = interface;
        node->n_interfaces += 1;

        return interface;
}

DBusNode * dbus_node_free(DBusNode *node) {
        for (size_t i = 0; i < node->n_interfaces; i++)
                dbus_interface_free(node->interfaces[i]);

        free(node->interfaces);
        free(node);

        return NULL;
}

void dbus_node_freep(DBusNode **nodep) {
        if (*nodep)
                dbus_node_free(*nodep);
}

enum {
        STATE_ROOT,
        STATE_NODE,
        STATE_INTERFACE,
        STATE_METHOD,
        STATE_ARGUMENT,
        STATE_PROPERTY
};

typedef struct {
        int level;
        DBusNode *node;
} State;

static const char * find_attribute(const char **attributes, const char *attribute) {
        for (size_t i = 0; attributes[i]; i += 2) {
                if (strcmp(attributes[i], attribute) == 0)
                        return attributes[i + 1];
        }

        return NULL;
}

static void start_element(void *data, const char *element, const char **attributes) {
        State *state = data;

        switch (state->level) {
                case STATE_ROOT:
                        if (strcmp(element, "node") == 0 && !state->node) {
                                state->node = calloc(1, sizeof(DBusNode));
                                state->level = STATE_NODE;
                        }
                        break;

                case STATE_NODE:
                        if (strcmp(element, "interface") == 0) {
                                const char *name = find_attribute(attributes, "name");

                                if (name) {
                                        dbus_node_append_interface(state->node, name);
                                        state->level = STATE_INTERFACE;
                                }
                        }
                        break;

                case STATE_METHOD: {
                        DBusInterface *interface = state->node->interfaces[state->node->n_interfaces - 1];
                        DBusMethod *method = interface->methods[interface->n_methods - 1];

                        if (strcmp(element, "arg") == 0) {
                                const char *name = find_attribute(attributes, "name");
                                const char *type = find_attribute(attributes, "type");
                                const char *direction = find_attribute(attributes, "direction");

                                if (!direction)
                                        direction = "in";

                                if (type) {
                                        dbus_method_append_argument(method, name, type, direction);
                                        state->level = STATE_ARGUMENT;
                                }
                        }
                        break;
                }

                case STATE_INTERFACE: {
                        DBusInterface *interface = state->node->interfaces[state->node->n_interfaces - 1];

                        if (strcmp(element, "method") == 0) {
                                const char *name = find_attribute(attributes, "name");

                                if (name) {
                                        dbus_interface_append_method(interface, name);
                                        state->level = STATE_METHOD;
                                }
                        } else if (strcmp(element, "property") == 0) {
                                const char *name = find_attribute(attributes, "name");
                                const char *type = find_attribute(attributes, "type");
                                const char *access = find_attribute(attributes, "access");

                                if (name && type && access) {
                                        dbus_interface_append_property(interface, name, type, strcmp(access, "readwrite") == 0);
                                        state->level = STATE_PROPERTY;
                                }
                        }
                        break;
                }
        }
}

static void end_element(void *data, const char *element) {
        State *state = data;

        switch (state->level) {
                case STATE_NODE:
                        if (strcmp(element, "node") == 0)
                                state->level = STATE_ROOT;
                        break;

                case STATE_INTERFACE:
                        if (strcmp(element, "interface") == 0)
                                state->level = STATE_NODE;
                        break;

                case STATE_METHOD:
                        if (strcmp(element, "method") == 0)
                                state->level = STATE_INTERFACE;
                        break;

                case STATE_ARGUMENT:
                        if (strcmp(element, "arg") == 0)
                                state->level = STATE_METHOD;
                        break;

                case STATE_PROPERTY:
                        if (strcmp(element, "property") == 0)
                                state->level = STATE_INTERFACE;
                        break;
        }
}

int dbus_node_new_from_xml(DBusNode **nodep, const char *xml) {
        XML_Parser parser;
        State state = { 0 };
        int r = 0;

        parser = XML_ParserCreate(NULL);
        XML_SetElementHandler(parser, start_element, end_element);
        XML_SetUserData(parser, &state);

        if (XML_Parse(parser, xml, strlen(xml), XML_TRUE) == 0)
                r = -EINVAL;

        if (r == 0)
                *nodep = state.node;
        else if (state.node)
                dbus_node_free(state.node);

        XML_ParserFree(parser);
        return r;
}

DBusMethod * dbus_node_find_method(DBusNode *node, const char *interface_name, const char *method_name) {
        DBusInterface *interface = NULL;

        for (size_t i = 0; i < node->n_interfaces; i++) {
                if (strcmp(node->interfaces[i]->name, interface_name) == 0) {
                        interface = node->interfaces[i];
                        break;
                }
        }

        if (!interface)
                return NULL;

        for (size_t i = 0; i < interface->n_methods; i++) {
                if (strcmp(interface->methods[i]->name, method_name) == 0)
                        return interface->methods[i];
        }

        return NULL;
}

static const char valid_dbus_basic_types[] = {
        SD_BUS_TYPE_BYTE,
        SD_BUS_TYPE_INT16,
        SD_BUS_TYPE_UINT16,
        SD_BUS_TYPE_INT32,
        SD_BUS_TYPE_UINT32,
        SD_BUS_TYPE_INT64,
        SD_BUS_TYPE_UINT64,
        SD_BUS_TYPE_DOUBLE,  // this is the last number type
        SD_BUS_TYPE_STRING,
        SD_BUS_TYPE_BOOLEAN,
        SD_BUS_TYPE_OBJECT_PATH,
        SD_BUS_TYPE_SIGNATURE,
        SD_BUS_TYPE_UNIX_FD
};

bool bus_type_is_number(char c) {
        return !!memchr(valid_dbus_basic_types, c, 8);
}

bool bus_type_is_dbus_dict_key(char c) {
        return !!memchr(valid_dbus_basic_types, c, 9);
}

bool bus_type_is_basic(char c) {
        return !!memchr(valid_dbus_basic_types, c, sizeof(valid_dbus_basic_types));
}

static int signature_element_length_internal(
                const char *s,
                bool allow_dict_entry,
                unsigned array_depth,
                unsigned struct_depth,
                size_t *l) {

        int r;

        if (!s)
                return -EINVAL;

        assert(l);

        if (bus_type_is_basic(*s) || *s == SD_BUS_TYPE_VARIANT) {
                *l = 1;
                return 0;
        }

        if (*s == SD_BUS_TYPE_ARRAY) {
                size_t t;

                if (array_depth >= 32)
                        return -EINVAL;

                r = signature_element_length_internal(s + 1, true, array_depth+1, struct_depth, &t);
                if (r < 0)
                        return r;

                *l = t + 1;
                return 0;
        }

        if (*s == SD_BUS_TYPE_STRUCT_BEGIN) {
                const char *p = s + 1;

                if (struct_depth >= 32)
                        return -EINVAL;

                while (*p != SD_BUS_TYPE_STRUCT_END) {
                        size_t t;

                        r = signature_element_length_internal(p, false, array_depth, struct_depth+1, &t);
                        if (r < 0)
                                return r;

                        p += t;
                }

                *l = p - s + 1;
                return 0;
        }

        if (*s == SD_BUS_TYPE_DICT_ENTRY_BEGIN && allow_dict_entry) {
                const char *p = s + 1;
                unsigned n = 0;

                if (struct_depth >= 32)
                        return -EINVAL;

                while (*p != SD_BUS_TYPE_DICT_ENTRY_END) {
                        size_t t;

                        if (n == 0 && !bus_type_is_basic(*p))
                                return -EINVAL;

                        r = signature_element_length_internal(p, false, array_depth, struct_depth+1, &t);
                        if (r < 0)
                                return r;

                        p += t;
                        n++;
                }

                if (n != 2)
                        return -EINVAL;

                *l = p - s + 1;
                return 0;
        }

        return -EINVAL;
}

int signature_element_length(const char *s, size_t *l) {
        return signature_element_length_internal(s, true, 0, 0, l);
}
