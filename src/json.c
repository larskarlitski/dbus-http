
#include "json.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#define _cleanup_(func) __attribute__((__cleanup__(func)))


struct JsonValue {
        JsonType type;
        union {
                double number;
                char *string;
                struct {
                        JsonObjectEntry **entries;
                        size_t n_entries;
                        size_t n_alloced;
                        bool sorted;
                } object;
                struct {
                        JsonValue **elements;
                        size_t n_elements;
                        size_t n_alloced;
                } array;
        };
};

struct JsonObjectEntry {
        char *key;
        JsonValue *value;
};

char* json_object_entry_key(JsonObjectEntry* joe){
        return joe->key;
}

JsonValue* json_object_entry_value(JsonObjectEntry* joe) {
        return joe->value;
}


static bool json_read_value(const char **j, JsonValue **valuep);

static void freep(void *p) {
        free(*(void **)p);
}

static void fclosep(FILE **filep) {
        if (*filep)
                fclose(*filep);
}

static void skip_whitespace(const char **j) {
        while (**j == ' ' || **j == '\t' || **j == '\n' || **j == '\r')
                *j += 1;
}

static bool json_read_char(const char **j, char c) {
        skip_whitespace(j);

        if (**j != c)
                return false;

        *j += 1;
        return true;
}

static bool json_read_literal(const char **j, const char *literal) {
        size_t len = strlen(literal);

        skip_whitespace(j);

        if (strncmp(*j, literal, len) != 0)
                return false;

        *j += len;
        return true;
}

static int unhex(char d, uint8_t *valuep) {
        if (d >= '0' && d <= '9')
                *valuep = d - '0';
        else if (d >= 'a' && d <= 'f')\
                *valuep = d - 'a' + 0x0a;
        else if (d >= 'A' && d <= 'F')
                *valuep = d - 'A' + 0x0a;
        else
                return -EINVAL;

        return 0;
}

static bool json_read_unicode_char(const char **j, FILE *stream) {
        const char *p = *j;
        uint8_t digits[4];
        uint16_t cp;

        for (size_t i = 0; i < 4; i++)
                if (p[i] == '\0' || unhex(p[i], &digits[i]) != 0)
                        return false;

        cp = digits[0] << 12 | digits[1] << 8 | digits[2] << 4 | digits[3];

        if (cp <= 0x007f) {
                fputc((char)cp, stream);

        } else if (cp <= 0x07ff) {
                fputc((char)(0xc0 | (cp >> 6)), stream);
                fputc((char)(0x80 | (cp & 0x3f)), stream);
        }

        else {
                fputc((char)(0xe0 | (cp >> 12)), stream);
                fputc((char)(0x80 | ((cp >> 6) & 0x3f)), stream);
                fputc((char)(0x80 | (cp & 0x3f)), stream);
        }

        return true;
}

static bool json_read_string(const char **j, char **stringp) {
        _cleanup_(fclosep) FILE *stream = NULL;
        _cleanup_(freep) char *string = NULL;
        size_t size;
        const char *p = *j;

        skip_whitespace(&p);

        if (*p != '"')
                return false;

        stream = open_memstream(&string, &size);

        for (p = p + 1; *p != '"'; p++) {
                if (*p == '\0')
                        return false;

                if (*p == '\\') {
                        p += 1;
                        switch (*p) {
                                case '"':
                                        fputc('"', stream);
                                        break;
                                case '\\':
                                         fputc('\\', stream);
                                         break;
                                case '/':
                                         fputc('/', stream);
                                         break;
                                case 'b':
                                         fputc('\b', stream);
                                         break;
                                case 'f':
                                         fputc('\f', stream);
                                         break;
                                case 'n':
                                         fputc('\n', stream);
                                         break;
                                case 'r':
                                         fputc('\r', stream);
                                         break;
                                case 't':
                                         fputc('\t', stream);
                                         break;
                                case 'u':
                                         if (!json_read_unicode_char(&p, stream))
                                                 return false;
                                         break;
                                default:
                                           return false;
                        }

                } else
                        fputc(*p, stream);
        }

        fclose(stream);
        stream = NULL;

        *j = p + 1;
        if (stringp) {
                *stringp = string;
                string = NULL;
        }
        return true;
}

static bool json_read_number(const char **j, double *nump) {
        char *end;
        double num;

        skip_whitespace(j);

        num = strtod(*j, &end);
        if (end == *j)
                return false;

        *j = end;
        if (nump)
                *nump = num;
        return true;
}

static void * grow_pointer_array(void *array, size_t *allocedp) {
        if (*allocedp == 0)
                *allocedp = 8;
        else
                *allocedp *= 2;

        return realloc(array, *allocedp * sizeof(void *));
}

static JsonObjectEntry * json_object_entry_free(JsonObjectEntry *entry) {
        free(entry->key);
        if (entry->value)
                json_value_free(entry->value);
        free(entry);
        return NULL;
}

static int json_object_entry_compare(const void *p1, const void *p2) {
        const JsonObjectEntry *entry1 = *(JsonObjectEntry **)p1;
        const JsonObjectEntry *entry2 = *(JsonObjectEntry **)p2;

        return strcmp(entry1->key, entry2->key);
}

static int json_object_entry_compare_key(const void *key, const void *p) {
        const JsonObjectEntry *entry = *(JsonObjectEntry **)p;

        return strcmp(key, entry->key);
}

JsonValue * json_value_free(JsonValue *value) {
        switch (value->type) {
                case JSON_TYPE_STRING:
                        free(value->string);
                        break;

                case JSON_TYPE_OBJECT:
                        for (size_t i = 0; i < value->object.n_entries; i++)
                                json_object_entry_free(value->object.entries[i]);
                        free(value->object.entries);
                        break;

                case JSON_TYPE_ARRAY:
                        for (size_t i = 0; i < value->array.n_elements; i++)
                                json_value_free(value->array.elements[i]);
                        free(value->array.elements);
                        break;

                case JSON_TYPE_NUMBER:
                case JSON_TYPE_TRUE:
                case JSON_TYPE_FALSE:
                case JSON_TYPE_NULL:
                        break;
        }

        free(value);
        return NULL;
}

static bool json_read_object_entry(const char **j, JsonObjectEntry **entryp) {
        JsonObjectEntry *entry;

        entry = calloc(1, sizeof(JsonObjectEntry));

        if (!json_read_string(j, &entry->key) ||
            !json_read_char(j, ':') ||
            !json_read_value(j, &entry->value)) {
                json_object_entry_free(entry);
                return false;
        }

        *entryp = entry;

        return true;
}

void json_value_freep(JsonValue **valuep) {
        if (*valuep)
                json_value_free(*valuep);
}

static bool json_read_value(const char **j, JsonValue **valuep) {
        _cleanup_(json_value_freep) JsonValue *value = NULL;

        value = calloc(1, sizeof(JsonValue));

        if (json_read_string(j, &value->string))
                value->type = JSON_TYPE_STRING;
        else if (json_read_number(j, &value->number))
                value->type = JSON_TYPE_NUMBER;
        else if (json_read_literal(j, "null"))
                value->type = JSON_TYPE_NULL;
        else if (json_read_literal(j, "true"))
                value->type = JSON_TYPE_TRUE;
        else if (json_read_literal(j, "false"))
                value->type = JSON_TYPE_FALSE;
        else if (json_read_char(j, '{')) {
                JsonObjectEntry *entry;

                value->type = JSON_TYPE_OBJECT;

                while (json_read_object_entry(j, &entry)) {
                        if (value->object.n_entries == value->object.n_alloced)
                                value->object.entries = grow_pointer_array(value->object.entries, &value->object.n_alloced);

                        value->object.entries[value->object.n_entries] = entry;
                        value->object.n_entries += 1;

                        if (!json_read_char(j, ','))
                                break;
                }

                if (!json_read_char(j, '}'))
                        return false;

        } else if (json_read_char(j, '[')) {
                JsonValue *element;

                value->type = JSON_TYPE_ARRAY;

                while (json_read_value(j, &element)) {
                        if (value->array.n_elements == value->array.n_alloced)
                                value->array.elements = grow_pointer_array(value->array.elements, &value->array.n_alloced);

                        value->array.elements[value->array.n_elements] = element;
                        value->array.n_elements += 1;

                        if (!json_read_char(j, ','))
                                break;
                }

                if (!json_read_char(j, ']'))
                        return false;

        } else
                return false;

        *valuep = value;
        value = NULL;

        return true;
}

int json_parse(const char *string, JsonValue **valuep, unsigned expected_type) {
        _cleanup_(json_value_freep) JsonValue *value = NULL;

        if (!json_read_value(&string, &value))
                return -EINVAL;

        if (expected_type > 0 && value->type != expected_type)
                return -EINVAL;

        skip_whitespace(&string);
        if (*string != '\0')
                return -EINVAL;

        *valuep = value;
        value = NULL;

        return 0;
}

JsonType json_value_get_type(const JsonValue *value) {
        return value->type;
}

const char * json_value_get_string(JsonValue *value) {
        if (value->type != JSON_TYPE_STRING)
                return NULL;

        return value->string;
}

double json_value_get_number(JsonValue *value) {
        if (value->type != JSON_TYPE_NUMBER)
                return 0.0;

        return value->number;
}

JsonValue * json_number_new(double number) {
        JsonValue *value;

        value = calloc(1, sizeof(JsonValue));
        value->type = JSON_TYPE_NUMBER;
        value->number = number;

        return value;
}

JsonValue * json_string_new(const char *string) {
        JsonValue *value;

        value = calloc(1, sizeof(JsonValue));
        value->type = JSON_TYPE_STRING;
        value->string = strdup(string);

        return value;
}

JsonValue * json_boolean_new(bool b) {
        JsonValue *value;

        value = calloc(1, sizeof(JsonValue));
        value->type = b ? JSON_TYPE_TRUE : JSON_TYPE_FALSE;

        return value;
}

JsonValue * json_null_new(void) {
        JsonValue *value;

        value = calloc(1, sizeof(JsonValue));
        value->type = JSON_TYPE_NULL;

        return value;
}

JsonValue * json_object_new(void) {
        JsonValue *value;

        value = calloc(1, sizeof(JsonValue));
        value->type = JSON_TYPE_OBJECT;

        return value;
}

bool json_object_lookup(JsonValue *value, const char *key, JsonValue **valuep, unsigned expected_type) {
        JsonObjectEntry **entryp;

        if (value->type != JSON_TYPE_OBJECT)
                return false;

        if (!value->object.sorted) {
                qsort(value->object.entries, value->object.n_entries, sizeof(JsonObjectEntry *),
                                json_object_entry_compare);
                value->object.sorted = true;
        }

        entryp = bsearch(key, value->object.entries, value->object.n_entries, sizeof(JsonObjectEntry *),
                         json_object_entry_compare_key);
        if (!entryp)
                return false;

        if (expected_type > 0 && (*entryp)->value->type != expected_type)
                return false;

        if (valuep)
                *valuep = (*entryp)->value;

        return true;
}

bool json_object_lookup_string(JsonValue *value, const char *key, const char **stringp) {
        JsonValue *entry;

        if (!json_object_lookup(value, key, &entry, JSON_TYPE_STRING))
                return false;

        *stringp = entry->string;

        return true;
}

int json_object_insert(JsonValue *value, const char *key, JsonValue *element) {
        JsonObjectEntry *entry;

        assert(value);
        assert(value->type == JSON_TYPE_OBJECT);
        assert(key);
        assert(value);

        entry = calloc(1, sizeof(JsonObjectEntry));

        entry->key = strdup(key);
        entry->value = element;

        if (value->object.n_entries == value->object.n_alloced)
                value->object.entries = grow_pointer_array(value->object.entries, &value->object.n_alloced);

        value->object.entries[value->object.n_entries] = entry;
        value->object.n_entries += 1;

        value->object.sorted = false;

        return 0;
}

int json_object_insert_string(JsonValue *value, const char *key, const char *string) {
        _cleanup_(json_value_freep) JsonValue *element = NULL;
        int r;

        element = calloc(1, sizeof(JsonValue));
        element->type = JSON_TYPE_STRING;
        element->string = strdup(string);

        r = json_object_insert(value, key, element);
        if (r < 0)
                return r;

        element = NULL;

        return 0;
}

JsonValue * json_array_new(void) {
        JsonValue *value;

        value = calloc(1, sizeof(JsonValue));
        value->type = JSON_TYPE_ARRAY;

        return value;
}

size_t json_array_get_length(JsonValue *value) {
        return value->array.n_elements;
}

bool json_array_get(JsonValue *value, size_t index, JsonValue **valuep, unsigned expected_type) {
        JsonValue *element;

        if (index >= value->array.n_elements)
                return false;

        element = value->array.elements[index];
        if (expected_type > 0 && element->type != expected_type)
                return false;

        *valuep = element;

        return true;
}

int json_array_append(JsonValue *value, JsonValue *element) {
        assert(value);
        assert(value->type == JSON_TYPE_ARRAY);
        assert(element);

        if (value->array.n_elements == value->array.n_alloced)
                value->array.elements = grow_pointer_array(value->array.elements, &value->array.n_alloced);

        value->array.elements[value->array.n_elements] = element;
        value->array.n_elements += 1;

        return 0;
}

static void json_print_string(const char *string, FILE *f) {
        const char *p;

        fputc('"', f);

        for (p = string; *p; p++) {
                if (*p == '"')
                        fputs("\\\"", f);

                else if (*p == '\\')
                        fputs("\\\\", f);

                else if (*p == '/')
                        fputs("\\/", f);

                else if (*p == '\b')
                        fputs("\\b", f);

                else if (*p == '\f')
                        fputs("\\f", f);

                else if (*p == '\n')
                        fputs("\\n", f);

                else if (*p == '\r')
                        fputs("\\r", f);

                else if (*p == '\t')
                        fputs("\\t", f);

                else
                        fprintf(f, "%c", *p);
        }

        fputc('"', f);
}

void json_print(JsonValue *value, FILE *f) {
        switch (value->type) {
                case JSON_TYPE_STRING:
                        json_print_string(value->string, f);
                        break;

                case JSON_TYPE_OBJECT:
                        /* force sorting */
                        json_object_lookup(value, "", NULL, 0);

                        fputs("{ ", f);
                        for (size_t i = 0; i < value->object.n_entries; i++) {
                                json_print_string(value->object.entries[i]->key, f);
                                fputs(": ", f);
                                json_print(value->object.entries[i]->value, f);

                                if (i < value->object.n_entries - 1)
                                        fputs(", ", f);
                        }
                        fputs(" }", f);
                        break;

                case JSON_TYPE_ARRAY:
                        fputs("[ ", f);
                        for (size_t i = 0; i < value->array.n_elements; i++) {
                                json_print(value->array.elements[i], f);

                                if (i < value->array.n_elements - 1)
                                        fputs(", ", f);
                        }
                        fputs(" ]", f);
                        break;

                case JSON_TYPE_NUMBER:
                        fprintf(f, "%.30g", value->number);
                        break;

                case JSON_TYPE_TRUE:
                        fputs("true", f);
                        break;

                case JSON_TYPE_FALSE:
                        fputs("false", f);
                        break;

                case JSON_TYPE_NULL:
                        fputs("null", f);
                        break;
        }
}


struct JsonObjectIterator {
        const JsonValue* jobject;
        unsigned index;
};

JsonObjectIterator* json_object_iterator_new(JsonValue* jobject){
        JsonObjectIterator* joiter = (JsonObjectIterator*)calloc(1, sizeof(JsonObjectIterator));

        if(jobject->type != JSON_TYPE_OBJECT){
                free(joiter);
                return NULL;
        }

        joiter->jobject = jobject;
        joiter->index = 0;
        return joiter;
}

void json_object_iterator_freep(JsonObjectIterator **joiter) {
        if (*joiter)
                free(*joiter);
}

JsonObjectEntry* json_object_iterator_next(JsonObjectIterator* joiter){
        if(joiter->jobject->object.n_entries > joiter->index){
                JsonObjectEntry* joe = joiter->jobject->object.entries[joiter->index];
                joiter->index++;
                return joe;
        }
        return NULL;
}
