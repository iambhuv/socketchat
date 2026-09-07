#ifndef SOCKET_CHAT_DEPENDENCIES_H
#define SOCKET_CHAT_DEPENDENCIES_H

#include <stdbool.h>

/* Initializes the external libraries used by the application. */
bool dependencies_initialize(void);

/* Releases library-wide resources after the application has finished. */
void dependencies_shutdown(void);

#endif
