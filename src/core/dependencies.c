#include "core/dependencies.h"

#include <sodium.h>
#include <sqlite3.h>

bool dependencies_initialize(void)
{
    if (sodium_init() < 0)
    {
        return false;
    }

    return sqlite3_initialize() == SQLITE_OK;
}

void dependencies_shutdown(void)
{
    sqlite3_shutdown();
}
