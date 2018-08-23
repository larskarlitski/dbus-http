/* A simple dbus service implementing a calculator
 * It's derived from the example provided by Lenard
 * Runs on session bus by default
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <sys/timerfd.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#ifndef _SD_DEFINE_POINTER_CLEANUP_FUNC
#define _SD_DEFINE_POINTER_CLEANUP_FUNC(type, func)             \
        static __inline__ void func##p(type **p) {              \
                if (*p)                                         \
                        func(*p);                               \
        }                                                       \
struct _sd_useless_struct_to_allow_trailing_semicolon_

/* Define helpers so that __attribute__((cleanup(sd_event_unrefp))) and similar may be used. */
_SD_DEFINE_POINTER_CLEANUP_FUNC(sd_event, sd_event_unref);
_SD_DEFINE_POINTER_CLEANUP_FUNC(sd_event_source, sd_event_source_unref);
_SD_DEFINE_POINTER_CLEANUP_FUNC(sd_bus, sd_bus_unref);
_SD_DEFINE_POINTER_CLEANUP_FUNC(sd_bus_message, sd_bus_message_unref);
_SD_DEFINE_POINTER_CLEANUP_FUNC(sd_bus_slot, sd_bus_slot_unref);

#endif /* _SD_DEFINE_POINTER_CLEANUP_FUNC */

#define _cleanup_(fn) __attribute__((__cleanup__(fn)))

typedef struct {
        bool session_bus;
} CmdArgs;

static unsigned int zdiv_counter = 0;


static int method_multiply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        int64_t x, y;
        int r;

        /* Read the parameters */
        r = sd_bus_message_read(m, "xx", &x, &y);
        if (r < 0) {
                fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
                return r;
        }
        printf("%"PRId64" * %"PRId64" = %"PRId64"\n", x, y, x*y);
        fflush(stdout);

        /* Reply with the response */
        return sd_bus_reply_method_return(m, "x", x * y);
}

static int method_divide(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        int64_t x, y;
        int r;

        /* Read the parameters */
        r = sd_bus_message_read(m, "xx", &x, &y);
        if (r < 0) {
                fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
                return r;
        }

        /* Return an error on division by zero */
        if (y == 0) {
                zdiv_counter++;
                fprintf(stderr, "Division by Zero! (%"PRId64" / %"PRId64")\n", x, y);
                sd_bus_error_set_const(ret_error, "dbus.http.DivisionByZero", "Sorry, can't allow division by zero.");
                return -EINVAL;
        }

        printf("%"PRId64" / %"PRId64" = %"PRId64"\n", x, y, x/y);
        fflush(stdout);
        return sd_bus_reply_method_return(m, "x", x / y);
}

static int get_zdiv_counter(sd_bus *bus, const char *path, const char *interface, const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *error) {
        int r = sd_bus_message_append(reply, "u", zdiv_counter);
        if (r >= 0) {
                printf("Returned zdiv counter: %u\n", zdiv_counter);
        } else {
                printf("Error while returning zdiv counter: %u\n", zdiv_counter);
        }

        return 1;
}


static int get_set_array[] = { 0, INT_MAX, INT_MIN };

static int method_get_array(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        _cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
        int r;

        printf("returned array [%d, %d, %d]\n", get_set_array[0], get_set_array[1], get_set_array[2]);
        fflush(stdout);

        r = sd_bus_message_new_method_return(m, &reply);
        if (r < 0) {
                fprintf(stderr, "Failed to append array values: %s\n", strerror(-r));
                return r;
        }

        r = sd_bus_message_append_array(reply, 'i', get_set_array, sizeof(get_set_array));
        if (r < 0) {
                fprintf(stderr, "Failed to append array values: %s\n", strerror(-r));
                return r;
        }
        return sd_bus_send(NULL, reply, NULL);
}


static int method_set_array(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        int r;
// free a????
        const void *a;
        size_t sz;

        // int sd_bus_message_read_array(sd_bus_message *m, char type, const void **ptr, size_t *size);
        r = sd_bus_message_read_array(m, 'i', &a, &sz);
        if (r < 0  || sz != sizeof(get_set_array)) {
                fprintf(stderr, "reading array failed\n");
                sd_bus_error_set_const(ret_error, "dbus.http.SetArray", "Sorry, can handle array with 3 integers only.");
                return -EINVAL;
        }

        memcpy(&get_set_array, a, sz);

        printf("set array to %d, %d, %d\n", get_set_array[0], get_set_array[1], get_set_array[2]);
        fflush(stdout);

        return sd_bus_reply_method_return(m, NULL);
}



static char get_set_dict_key1[20] = "key1";
static int get_set_dict_i1 = 17;
static char get_set_dict_key2[20] = "key2";
static char get_set_dict_s2[20] = "test-string";

static int method_get_dict(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        _cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
        int r;

        printf("returned dict [%s, %d, %s, %s]\n", get_set_dict_key1, get_set_dict_i1, get_set_dict_key2, get_set_dict_s2);
        fflush(stdout);

        r = sd_bus_message_new_method_return(m, &reply);
        if (r < 0) {
                fprintf(stderr, "Failed to append dict values: %s\n", strerror(-r));
                return r;
        }

        r = sd_bus_message_append(reply, "a{sv}", 2, get_set_dict_key1, "i", get_set_dict_i1, get_set_dict_key2, "s", get_set_dict_s2);
        if (r < 0) {
                fprintf(stderr, "Failed to append dict values: %s\n", strerror(-r));
                return r;
        }
        return sd_bus_send(NULL, reply, NULL);
}


static int method_set_dict(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        int r;
        const char *key1, *key2, *s2;
        int i1;

        r = sd_bus_message_read(m, "a{sv}", 2, &key1, "i", &i1, &key2, "s", &s2);
        if (r < 0) {
                fprintf(stderr, "reading dict failed\n");
                sd_bus_error_set_const(ret_error, "dbus.http.SetDict", "Sorry, can handle this dict.");
                return -EINVAL;
        }

        printf("set dict to %s: %d, %s: %s\n", key1, i1, key2, s2);
        get_set_dict_i1 = i1;
        strncpy(get_set_dict_s2, s2, sizeof(get_set_dict_s2));
        fflush(stdout);

        return sd_bus_reply_method_return(m, NULL);
}



typedef struct {
        int an_int;
        char str[20];
} TestStruct;

static TestStruct test_struct_1 = { .an_int = 123, .str = "foo bar" };

static int method_get_struct(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        _cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
        int r;

        printf("returned struct [%i, %s]\n", test_struct_1.an_int, test_struct_1.str);
        fflush(stdout);

        r = sd_bus_message_new_method_return(m, &reply);
        if (r < 0) {
                fprintf(stderr, "Failed to append struct values: %s\n", strerror(-r));
                return r;
        }

        r = sd_bus_message_append(reply, "(is)", test_struct_1.an_int, test_struct_1.str);
        if (r < 0) {
                fprintf(stderr, "Failed to append struct values: %s\n", strerror(-r));
                return r;
        }
        return sd_bus_send(NULL, reply, NULL);
}


static int method_set_struct(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        int r;
        int an_int;
        const char *str;

        r = sd_bus_message_read(m, "(is)", &an_int, &str);
        if (r < 0) {
                fprintf(stderr, "reading dict failed\n");
                sd_bus_error_set_const(ret_error, "dbus.http.SetDict", "Sorry, can handle this dict.");
                return -EINVAL;
        }

        test_struct_1.an_int = an_int;
        strncpy(test_struct_1.str, str, sizeof(test_struct_1.str));
        printf("set struct to [%i, %s]\n", test_struct_1.an_int, test_struct_1.str);
        fflush(stdout);

        return sd_bus_reply_method_return(m, NULL);
}


typedef enum {Unsigned, String} VariantType1;

typedef struct {
        TestStruct test_struct[2];
        VariantType1 variant1_type;
        union{
                char str1[20];
                unsigned unsigned1;
        } variant1;
        int array1[3];
} TestStruct2;

static TestStruct2 test_struct_2 = {
        .test_struct = {
                { .an_int = 1212, .str = "bar1" },
                { .an_int = 1313, .str = "bar2" }
        },
        .variant1_type = Unsigned,
        .variant1.unsigned1 = 123,
        .array1 = { 1, 2, 3 }
};

static void TestStruct2_print(TestStruct2 *ts){
        printf("nested structure (%i, %s)(%i, %s), ",
                        ts->test_struct[0].an_int, ts->test_struct[0].str,
                        ts->test_struct[1].an_int, ts->test_struct[1].str);
        if(ts->variant1_type == Unsigned){
                printf("%u ", ts->variant1.unsigned1);
        } else {
                printf("%s ", ts->variant1.str1);
        }
        printf("[%i, %i, %i]\n", ts->array1[0], ts->array1[1], ts->array1[2]);
        fflush(stdout);
}

static int method_get_nested1(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        _cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
        int r;

        TestStruct2_print(&test_struct_2);

        r = sd_bus_message_new_method_return(m, &reply);
        if (r < 0) {
                fprintf(stderr, "Failed to append struct values: %s\n", strerror(-r));
                return r;
        }

        r = sd_bus_message_append(reply, "a(is)", 2,
                        test_struct_2.test_struct[0].an_int, test_struct_2.test_struct[0].str,
                        test_struct_2.test_struct[1].an_int, test_struct_2.test_struct[1].str);
        if (r < 0) {
                fprintf(stderr, "Failed to append array of structs (%s)\n", strerror(-r));
                return r;
        }

        if(test_struct_2.variant1_type == Unsigned){
                r = sd_bus_message_append(reply, "v", "u", test_struct_2.variant1.unsigned1);
                if (r < 0) {
                        fprintf(stderr, "Failed to append variant as unsigned (%s)\n", strerror(-r));
                        return r;
                }
        } else {
                r = sd_bus_message_append(reply, "v", "s", test_struct_2.variant1.str1);
                if (r < 0) {
                        fprintf(stderr, "Failed to append variant as string (%s)\n", strerror(-r));
                        return r;
                }
        }
        r = sd_bus_message_append(reply, "ai", 3, test_struct_2.array1[0], test_struct_2.array1[1], test_struct_2.array1[2]);

        return sd_bus_send(NULL, reply, NULL);
}


static int method_set_nested1(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
        int r;
        const char *str0;
        const char *str1;

        r = sd_bus_message_read(m, "a(is)vai",
                        2,
                        &(test_struct_2.test_struct[0].an_int), &str0,
                        &(test_struct_2.test_struct[1].an_int), &str1,
                        "u", &(test_struct_2.variant1.unsigned1),
                        3,
                        &(test_struct_2.array1[0]), &(test_struct_2.array1[1]), &(test_struct_2.array1[2])
                        );
        if (r < 0) {
                fprintf(stderr, "reading dict failed\n");
                sd_bus_error_set_const(ret_error, "dbus.http.SetDict", "Sorry, can handle this dict.");
                return -EINVAL;
        }
        strncpy(test_struct_2.test_struct[0].str, str0, sizeof(test_struct_2.test_struct[0].str));
        strncpy(test_struct_2.test_struct[1].str, str1, sizeof(test_struct_2.test_struct[1].str));

        TestStruct2_print(&test_struct_2);

        return sd_bus_reply_method_return(m, NULL);
}


/* The vtable of our little object, implements the dbus.http.Calculator interface */
static const sd_bus_vtable calculator_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Multiply", "xx", "x", method_multiply, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("Divide", "xx", "x", method_divide,   SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("GetArray", NULL, "ai", method_get_array, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("SetArray", "ai", NULL, method_set_array, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("GetDict", NULL, "a{sv}", method_get_dict, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("SetDict", "a{sv}", NULL, method_set_dict, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("GetStruct", NULL, "(is)", method_get_struct, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("SetStruct", "(is)", NULL, method_set_struct, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("GetNested1", NULL, "a(is)vai", method_get_nested1, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("SetNested1", "a(is)vai", NULL, method_set_nested1, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_PROPERTY("ZeroDivisionCounter", "u", get_zdiv_counter, 0, 0),
        SD_BUS_VTABLE_END
};


static int parse_args (int argc, char **argv, CmdArgs *cmd_args) {
        int short_arg;

        opterr = 0;

        // Default settings
        cmd_args->session_bus = false;

        while ((short_arg = getopt (argc, argv, "sh")) != -1) {
                switch (short_arg)
                {
                case 's':
                        cmd_args->session_bus = true;
                        break;
                case '?':
                case 'h':
                        puts("-s run on session DBUS");
                        break;
                // Invalid arguments
                default:
                        return -1;
                }
        }
        return 0;
}


int main(int argc, char *argv[]) {
        _cleanup_(sd_event_unrefp) sd_event *loop = NULL;
        _cleanup_(sd_bus_slot_unrefp) sd_bus_slot *slot = NULL;
        _cleanup_(sd_bus_unrefp) sd_bus *bus = NULL;
        _cleanup_(sd_event_source_unrefp) sd_event_source *periodic_timer= NULL;
        int r;
        sigset_t ss;
        CmdArgs cmd_args;

        r = parse_args(argc, argv, &cmd_args);
        if (r < 0)
                goto finish;

        r = sd_event_default(&loop);

        if (sigemptyset(&ss) < 0 || sigaddset(&ss, SIGTERM) < 0 || sigaddset(&ss, SIGINT) < 0) {
                r = -errno;
                goto finish;
        }

        /* Block SIGTERM first, so that the event loop can handle it */
        if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0) {
                r = -errno;
                goto finish;
        }

        /* Let's make use of the default handler and "floating" reference features of sd_event_add_signal() */
        r = sd_event_add_signal(loop, NULL, SIGTERM, NULL, NULL);
        if (r < 0)
                goto finish;
        r = sd_event_add_signal(loop, NULL, SIGINT, NULL, NULL);
        if (r < 0)
                goto finish;

        printf("dbus.http.Calculator (running on ");
        if(cmd_args.session_bus) {
                puts("session bus)\n");
                r = sd_bus_open_user(&bus);
        }
        else {
                puts("system bus)\n");
                r = sd_bus_open_system(&bus);
        }
        if (r < 0)
                goto finish;

        /* Install the object */
        r = sd_bus_add_object_vtable(bus,
                                     &slot,
                                     "/dbus/http/Calculator",  /* object path */
                                     "dbus.http.Calculator",   /* interface name */
                                     calculator_vtable,
                                     NULL);
        if (r < 0)
                goto finish;

        /* Take a well-known service name so that clients can find us */
        r = sd_bus_request_name(bus, "dbus.http.Calculator", 0);
        if (r < 0)
                goto finish;

        r = sd_bus_attach_event(bus, loop, 0);
        if (r < 0)
                goto finish;

        r = sd_event_loop(loop);
        if (r < 0)
                goto finish;

finish:
        if (r < 0)
                fprintf(stderr, "Failure: %s\n", strerror(-r));

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
