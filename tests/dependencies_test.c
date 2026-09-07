#include "core/dependencies.h"

#include <sodium.h>
#include <sqlite3.h>

#include <stddef.h>

int main(void)
{
    sqlite3 *database = NULL;
    unsigned char random_bytes[16];

    if (!dependencies_initialize())
    {
        return 1;
    }

    randombytes_buf(random_bytes, sizeof(random_bytes));

    if (sqlite3_open(":memory:", &database) != SQLITE_OK)
    {
        sqlite3_close(database);
        dependencies_shutdown();
        return 1;
    }

    if (sqlite3_exec(
            database,
            "CREATE TABLE messages (id INTEGER PRIMARY KEY, body BLOB);",
            NULL,
            NULL,
            NULL) != SQLITE_OK)
    {
        sqlite3_close(database);
        dependencies_shutdown();
        return 1;
    }

    if (sqlite3_close(database) != SQLITE_OK)
    {
        dependencies_shutdown();
        return 1;
    }

    dependencies_shutdown();

    return 0;
}
