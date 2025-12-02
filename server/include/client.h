#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>
#include <pthread.h>

// ============================================================
//  CLIENT MODULE HEADER
//  ------------------------------------------------------------
//  Defines the structure and behavior of connected clients.
//  Handles connection lifecycle, heartbeat monitoring, and
//  room association.
// ============================================================

#define MAX_CLIENTS 128  // Maximum number of concurrent clients

// Forward declaration (to avoid circular includes)
struct Room;


// ------------------------------------------------------------
//  Client state enumeration
// ------------------------------------------------------------
typedef enum {
    CLIENT_STATE_LOBBY   = 0,   // In lobby (no active room)
    CLIENT_STATE_WAITING = 1,   // Waiting for opponent
    CLIENT_STATE_PLAYING = 2    // Actively playing in a room
} ClientState;


// ------------------------------------------------------------
//  Client structure
// ------------------------------------------------------------
typedef struct Client {
    int fd;                     // Socket file descriptor
    char name[32];              // Player nickname
    ClientState state;          // Current state (LOBBY / WAITING / PLAYING)
    struct Room* current_room;  // Pointer to the room (NULL if none)

    bool alive;                 // Thread running flag
    bool connected;             // True if connection is alive
    int  missed_pongs;          // Number of missed PONG responses
    int  invalid_count;         // Number of invalid protocol inputs

    char session_id[32];        // Unique reconnect session token
} Client;


// ------------------------------------------------------------
//  Lifecycle management
// ------------------------------------------------------------
/**
 * @brief Allocates and initializes a new Client instance.
 * @param fd  Socket file descriptor.
 * @return Pointer to the created Client structure.
 */
struct Client* client_create(int fd);

/**
 * @brief Cleans up and frees a Client instance.
 * @param c Pointer to Client to be destroyed.
 */
void client_destroy(struct Client* c);


// ------------------------------------------------------------
//  Property management
// ------------------------------------------------------------
/**
 * @brief Sets the client's nickname.
 * @param c    Target client.
 * @param name New nickname.
 */
void client_set_name(struct Client* c, const char* name);

/**
 * @brief Updates the client's internal state.
 * @param c  Target client.
 * @param st New state (LOBBY / WAITING / PLAYING).
 */
void client_set_state(struct Client* c, ClientState st);


// ------------------------------------------------------------
//  Thread entry point
// ------------------------------------------------------------
/**
 * @brief Main thread loop handling communication with the client.
 * @param arg Pointer to the Client instance.
 */
void* client_thread(void* arg);


// ------------------------------------------------------------
//  Global client registry
// ------------------------------------------------------------
extern struct Client* g_clients[MAX_CLIENTS];
extern pthread_mutex_t g_clients_mtx;

#endif
