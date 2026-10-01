#include <raylib.h>

#include "client/client_connection.h"
#include "client/client_database.h"
#include "client/onboarding_screen.h"
#include "client/chat_screen.h"
#include "core/app_config.h"
#include "core/dependencies.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const int initialWindowWidth = 960;
    const int initialWindowHeight = 600;
    ClientDatabase database = { 0 };
    ClientDatabaseStatus database_status;
    ClientConnection connection;
    ClientIdentity identity;
    bool identity_was_created;
    bool account_was_found = false;
    bool server_host_was_found = false;
    bool dark_mode_was_found = false;
    bool saved_dark_mode = ui_theme_is_dark_mode();
    bool reconnect_pending;
    bool chat_open = false;
    const char *client_database_path = APP_CLIENT_DATABASE_PATH;
    char server_host[CLIENT_DATABASE_SERVER_HOST_CAPACITY] = APP_SERVER_HOST;
    OnboardingScreen onboarding_screen;

    if (argc == 3 && strcmp(argv[1], "--database") == 0 && argv[2][0] != '\0')
    {
        client_database_path = argv[2];
    }
    else if (argc != 1)
    {
        fprintf(stderr, "Usage: socket_chat [--database path-to-profile.db]\n");
        return 1;
    }

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
        client_database_path);
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
    if (database_status == CLIENT_DATABASE_OK)
    {
        database_status = client_database_load_server_host(
            &database,
            server_host,
            sizeof(server_host),
            &server_host_was_found);
    }
    if (database_status == CLIENT_DATABASE_OK)
    {
        database_status = client_database_load_dark_mode(
            &database,
            &saved_dark_mode,
            &dark_mode_was_found);
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
    if (dark_mode_was_found)
    {
        ui_theme_set_dark_mode(saved_dark_mode);
    }
    ui_font_load();
    onboarding_screen_init(&onboarding_screen);
    onboarding_screen_set_server_host(&onboarding_screen,
                                      server_host_was_found ? server_host : APP_SERVER_HOST);
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
            chat_screen_draw(&connection, &database, &identity);
        }
        EndDrawing();

        if (chat_open && ui_theme_is_dark_mode() != saved_dark_mode)
        {
            if (client_database_save_dark_mode(
                    &database, ui_theme_is_dark_mode()) == CLIENT_DATABASE_OK)
            {
                saved_dark_mode = ui_theme_is_dark_mode();
            }
        }

        if (account_requested)
        {
            ClientConnectionStatus connection_status =
                client_connection_connect(
                    &connection,
                    onboarding_screen_server_host(&onboarding_screen),
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
                    onboarding_screen_server_host(&onboarding_screen),
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
                (void)client_database_save_server_host(
                    &database, onboarding_screen_server_host(&onboarding_screen));
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
