#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

typedef enum JsonType JsonType;
typedef struct JsonValue JsonValue;

enum JsonType {
        JSON_TYPE_OBJECT = 1,
        JSON_TYPE_ARRAY,
        JSON_TYPE_STRING,
        JSON_TYPE_NUMBER,
        JSON_TYPE_TRUE,
        JSON_TYPE_FALSE,
        JSON_TYPE_NULL
};

int json_parse(const char *string, JsonValue **valuep, unsigned expected_type);

JsonValue * json_value_free(JsonValue *value);
void json_value_freep(JsonValue **valuep);
JsonType json_value_get_type(const JsonValue *value);

const char * json_value_get_string(JsonValue *value);
double json_value_get_number(JsonValue *value);

JsonValue * json_number_new(double number);
JsonValue * json_string_new(const char *string);
JsonValue * json_boolean_new(bool b);
JsonValue * json_null_new(void);

JsonValue * json_object_new(void);
bool json_object_lookup(JsonValue *value, const char *key, JsonValue **valuep, unsigned expected_type);
bool json_object_lookup_string(JsonValue *value, const char *key, const char **stringp);
int json_object_insert(JsonValue *value, const char *key, JsonValue *element);
int json_object_insert_string(JsonValue *value, const char *key, const char *string);

JsonValue * json_array_new(void);
size_t json_array_get_length(JsonValue *value);
bool json_array_get(JsonValue *value, size_t index, JsonValue **valuep, unsigned expected_type);
int json_array_append(JsonValue *value, JsonValue *element);

void json_print(JsonValue *value, FILE *f);


typedef struct JsonObjectEntry JsonObjectEntry;
char* json_object_entry_key(JsonObjectEntry* joe);
JsonValue* json_object_entry_value(JsonObjectEntry* joe);

typedef struct JsonObjectIterator JsonObjectIterator;
JsonObjectIterator* json_object_iterator_new(JsonValue* jobject);
void json_object_iterator_freep(JsonObjectIterator **joiter);
JsonObjectEntry* json_object_iterator_next(JsonObjectIterator* joiter);
