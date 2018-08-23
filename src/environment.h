#pragma once

#include <systemd/sd-bus.h>

typedef struct {
        sd_bus *bus;
        const char *dbus_prefix;
} Environment;
