#pragma once

// Provide macros which are not available with systemd ~<230

// Provide this definition for older systemd versions
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
#endif /* _SD_DEFINE_POINTER_CLEANUP_FUNC */
