#include "config.h"

#include <stdio.h>
#include <string.h>

ServerConfig g_config;

void config_load(const char* filename, ServerConfig* cfg)
{
    if (!cfg) return;

    // Defaults
    cfg->port = 10000;
    cfg->max_rooms = 16;
    cfg->max_clients = 128;
    strcpy(cfg->bind_address, "0.0.0.0");
    cfg->disconnect_grace = 15;

    FILE* f = fopen(filename, "r");
    if (!f) return; // fallback to defaults

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        (void)sscanf(line, "PORT=%d", &cfg->port);
        (void)sscanf(line, "MAX_ROOMS=%d", &cfg->max_rooms);
        (void)sscanf(line, "MAX_CLIENTS=%d", &cfg->max_clients);
        (void)sscanf(line, "BIND_ADDRESS=%31s", cfg->bind_address);
        (void)sscanf(line, "DISCONNECT_GRACE=%d", &cfg->disconnect_grace);
    }

    fclose(f);

    // Mirror to global config for modules that read it directly.
    g_config = *cfg;
}
