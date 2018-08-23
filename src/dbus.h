#pragma once

#include <stdlib.h>
#include <stdbool.h>

typedef struct DBusNode DBusNode;
typedef struct DBusInterface DBusInterface;
typedef struct DBusMethod DBusMethod;
typedef struct DBusArgument DBusArgument;
typedef struct DBusProperty DBusProperty;

struct DBusNode {
        DBusInterface **interfaces;
        size_t n_interfaces;
        size_t n_alloced_interfaces;
};

struct DBusInterface {
        char *name;
        DBusMethod **methods;
        size_t n_methods;
        size_t n_alloced_methods;
        DBusProperty **properties;
        size_t n_properties;
        size_t n_alloced_properties;
};

struct DBusMethod {
        char *name;
        DBusArgument **in_args;
        DBusArgument **out_args;
        size_t n_in_args;
        size_t n_out_args;
        size_t n_alloced_in_args;
        size_t n_alloced_out_args;
};

struct DBusArgument {
        char *name;
        char *type;
};

struct DBusProperty {
        char *name;
        char *type;
        bool writable;
};

int dbus_node_new_from_xml(DBusNode **nodep, const char *xml);
DBusNode * dbus_node_free(DBusNode *node);
void dbus_node_freep(DBusNode **nodep);
DBusMethod * dbus_node_find_method(DBusNode *node, const char *interface_name, const char *method_name);
int signature_element_length(const char *s, size_t *l);
bool bus_type_is_number(char c);
bool bus_type_is_dbus_dict_key(char c);
bool bus_type_is_basic(char c);
