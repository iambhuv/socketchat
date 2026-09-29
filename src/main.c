#include <raylib.h>

#include "client/client_connection.h"
#include "client/client_database.h"
#include "client/onboarding_screen.h"
#include "client/chat_screen.h"
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
    ClientConnection connection;
    ClientIdentity identity;
    bool identity_was_created;
    bool account_was_found = false;
    bool reconnect_pending;
    bool chat_open = false;
    OnboardingScreen onboarding_screen;

    if (!dependencies_initialize())
    {
        fprintf(stderr, "Could not initialize libsodium or SQLite.\n");
        return 1;
    }

    if (net_init() != NET_STATUS_OK)
    {
        fprintf(stderr, "Could not initialize networking.\n");
        dependencies_shutdown();
        return 1;
    }

    client_connection_init(&connection);

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
    if (database_status == CLIENT_DATABASE_OK)
    {
        database_status = client_database_ensure_self_contact(&database, &identity);
    }
    if (database_status == CLIENT_DATABASE_OK)
    {
        char saved_username[ONBOARDING_USERNAME_CAPACITY];
        database_status = client_database_load_account_username(
            &database, saved_username, sizeof(saved_username), &account_was_found);
        if (database_status == CLIENT_DATABASE_OK && account_was_found)
        {
            /* Applied after the Raylib UI state is initialized below. */
        }
    }

    if (database_status != CLIENT_DATABASE_OK)
    {
        fprintf(
            stderr,
            "Could not initialize client identity: %s.\n",
            client_database_status_string(database_status));
        client_database_close(&database);
        net_shutdown();
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
        net_shutdown();
        dependencies_shutdown();
        return 1;
    }

    SetWindowMinSize(640, 540);
    SetTargetFPS(60);
    ui_font_load();
    onboarding_screen_init(&onboarding_screen);
    reconnect_pending = account_was_found;
    if (account_was_found)
    {
        char saved_username[ONBOARDING_USERNAME_CAPACITY];
        bool found_again = false;
        if (client_database_load_account_username(&database, saved_username,
                                                  sizeof(saved_username), &found_again) == CLIENT_DATABASE_OK &&
            found_again)
        {
            onboarding_screen_set_username(&onboarding_screen, saved_username);
            onboarding_screen_set_status(&onboarding_screen, UI_STATUS_CONNECTING, "Reconnecting");
        }
    }

    while (!WindowShouldClose())
    {
        bool account_requested;

        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        BeginDrawing();
        account_requested = !chat_open && onboarding_screen_draw(&onboarding_screen);
        if (!chat_open && reconnect_pending)
        {
            account_requested = true;
            reconnect_pending = false;
        }

        if (chat_open)
        {
            chat_screen_draw(&connection, &database);
        }
        EndDrawing();

        if (account_requested)
        {
            ClientConnectionStatus connection_status =
                client_connection_connect(
                    &connection,
                    APP_SERVER_HOST,
                    APP_SERVER_PORT,
                    &identity);

            if (connection_status == CLIENT_CONNECTION_OK)
            {
                onboarding_screen_set_status(
                    &onboarding_screen,
                    UI_STATUS_CONNECTING,
                    "Verifying server");
                connection_status = client_connection_finish_handshake(
                    &connection,
                    &database,
                    APP_SERVER_HOST,
                    APP_SERVER_PORT,
                    &identity);
            }

            if (connection_status == CLIENT_CONNECTION_OK)
            {
                onboarding_screen_set_status(
                    &onboarding_screen,
                    UI_STATUS_CONNECTED,
                    "Connected");
                (void)client_database_save_account_username(
                    &database, onboarding_screen_username(&onboarding_screen));
                chat_open = true;
                printf("Connected with a verified server and authenticated identity.\n");
            }
            else
            {
                onboarding_screen_set_status(
                    &onboarding_screen,
                    UI_STATUS_ERROR,
                    "Server offline");
                fprintf(
                    stderr,
                    "Could not connect: %s.\n",
                    client_connection_status_string(connection_status));
            }
        }
    }

    ui_font_unload();
    CloseWindow();
    client_connection_close(&connection);
    client_identity_clear(&identity);
    client_database_close(&database);
    net_shutdown();
    dependencies_shutdown();

    return 0;
}
