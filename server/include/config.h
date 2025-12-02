#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
//  CONFIG MODULE HEADER
//  ------------------------------------------------------------
//  Provides configuration file loading for server parameters.
//  Supports simple KEY=VALUE format with fallback to defaults.
// ============================================================

/**
 * @struct ServerConfig
 * @brief Holds runtime configuration parameters loaded from file.
 */
typedef struct {
    int port;               ///< TCP port to bind (default: 10000)
    int max_rooms;          ///< Maximum number of concurrent game rooms (default: 16)
    int max_clients;        ///< Maximum number of concurrent clients (default: 128)
    char bind_address[32];  ///< IP address to bind (default: "0.0.0.0")
} ServerConfig;

// Global configuration instance loaded at startup.
extern ServerConfig g_config;

/**
 * @brief Loads server configuration from a file.
 * 
 * Parses a simple KEY=VALUE format configuration file. If the file
 * does not exist or any key is missing, default values are used:
 *   - port: 10000
 *   - max_rooms: 16
 *   - max_clients: 128
 *   - bind_address: "0.0.0.0"
 * 
 * @param filename Path to the configuration file (e.g., "server.config").
 * @param cfg      Pointer to ServerConfig struct to populate.
 */
void config_load(const char* filename, ServerConfig* cfg);

#endif // CONFIG_H
