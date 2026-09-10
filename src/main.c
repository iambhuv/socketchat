#include <raylib.h>

#include "client/client_database.h"
#include "client/onboarding_screen.h"
#include "core/app_config.h"
#include "core/dependencies.h"
#include "ui/ui_font.h"

#include <stdio.h>

int main(void)
{
    const int initialWindowWidth = 960;
    const int initialWindowHeight = 600;
    ClientDatabase database = { 0 };
    ClientDatabaseStatus database_status;
    ClientIdentity identity;
    bool identity_was_created;
    OnboardingScreen onboarding_screen;

    if (!dependencies_initialize())
    {
        fprintf(stderr, "Could not initialize libsodium or SQLite.\n");
        return 1;
    }

    database_status = client_database_open(
        &database,
        APP_CLIENT_DATABASE_PATH);
    if (database_status == CLIENT_DATABASE_OK)
    {
        database_status = client_database_load_or_create_identity(
            &database,
            &identity,
            &identity_was_created);
    }

    if (database_status != CLIENT_DATABASE_OK)
    {
        fprintf(
            stderr,
            "Could not initialize client identity: %s.\n",
            client_database_status_string(database_status));
        client_database_close(&database);
        dependencies_shutdown();
        return 1;
    }

    (void)identity_was_created;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(initialWindowWidth, initialWindowHeight, "Socket Chat");

    if (!IsWindowReady())
    {
        fprintf(stderr, "Could not create the Raylib window.\n");
        client_identity_clear(&identity);
        client_database_close(&database);
        dependencies_shutdown();
        return 1;
    }

    SetWindowMinSize(640, 540);
    SetTargetFPS(60);
    ui_font_load();
    onboarding_screen_init(&onboarding_screen);

    while (!WindowShouldClose())
    {
        bool account_requested;

        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        BeginDrawing();
        account_requested = onboarding_screen_draw(&onboarding_screen);
        EndDrawing();

        /* Account registration will use this event in the next project step. */
        (void)account_requested;
    }

    ui_font_unload();
    CloseWindow();
    client_identity_clear(&identity);
    client_database_close(&database);
    dependencies_shutdown();

    return 0;
}
