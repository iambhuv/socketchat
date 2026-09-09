#include "server/server.h"

#include "core/app_config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int read_port(const char *text, uint16_t *port)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);

    if (errno != 0 || text[0] == '\0' || *end != '\0' ||
        value == 0 || value > UINT16_MAX)
    {
        return 0;
    }

    *port = (uint16_t)value;
    return 1;
}

int main(int argument_count, char **arguments)
{
    uint16_t port = APP_SERVER_PORT;

    if (argument_count > 2)
    {
        fprintf(stderr, "Usage: %s [port]\n", arguments[0]);
        return 1;
    }

    if (argument_count == 2 && !read_port(arguments[1], &port))
    {
        fprintf(stderr, "Port must be a number from 1 to 65535.\n");
        return 1;
    }

    return server_run(port);
}
