#ifndef ROOM_H
#define ROOM_H

#include <stdbool.h>
#include "client.h"
#include "game.h"

// ============================================================
//  ROOM MODULE HEADER
//  ------------------------------------------------------------
//  Manages active game rooms and player interactions.
//  Each room can contain up to two players (p1, p2).
//
//  Responsibilities:
//   - Creating and joining rooms
//   - Handling player disconnects / reconnects
//   - Managing replay state and next-round logic
//   - Tracking room lifecycle (WAITING, PLAYING, EMPTY)
// ============================================================


// ------------------------------------------------------------
//  Constants and enums
// ------------------------------------------------------------
#define MAX_ROOMS 16

/**
 * @enum RoomState
 * @brief Represents the current status of a game room.
 */
typedef enum {
    ROOM_EMPTY   = 0,   ///< Room is removed or inactive
    ROOM_WAITING = 1,   ///< One player waiting for opponent
    ROOM_PLAYING = 2    ///< Active game in progress
} RoomState;


// ------------------------------------------------------------
//  Room structure
// ------------------------------------------------------------
/**
 * @struct Room
 * @brief Holds data for a single active room, including players,
 *        current game state, and reconnect identifiers.
 */
typedef struct Room {
    int id;                     ///< Unique room ID
    char name[32];              ///< Room name (provided by creator)
    RoomState state;            ///< Current state (WAITING, PLAYING, etc.)

    Game game;                  ///< Embedded game data

    // Replay state for "Play Again" logic
    int replay_p1;
    int replay_p2;

    // Player slots
    struct Client* p1;
    struct Client* p2;

    // Disconnection tracking
    bool p1_disconnected;
    bool p2_disconnected;

    // Identification for reconnect
    char p1_name[32];
    char p2_name[32];
    char p1_session[32];
    char p2_session[32];

    // Determines who starts the next round (0 = p1, 1 = p2)
    int starting_player;

} Room;


// ------------------------------------------------------------
//  Global data
// ------------------------------------------------------------
extern Room g_rooms[MAX_ROOMS];
extern int  g_room_count;


// ------------------------------------------------------------
//  Core management functions
// ------------------------------------------------------------

/**
 * @brief Creates a new room and assigns the creator as Player 1.
 * @param name     Name of the room.
 * @param creator  Pointer to the client who creates the room.
 * @return Pointer to the newly created Room structure.
 */
Room* room_create(const char* name, struct Client* creator);

/**
 * @brief Adds a player to an existing room (as Player 2).
 * @param room_id  The unique ID of the room to join.
 * @param joiner   Pointer to the joining client.
 * @return Pointer to the joined room, or NULL on error.
 */
Room* room_join(int room_id, struct Client* joiner);

/**
 * @brief Removes a client from their current room.
 * @param c  The client leaving the room.
 */
void room_leave(struct Client* c);

/**
 * @brief Deletes the room from the list if it becomes empty.
 * @param r  Pointer to the room.
 */
void room_remove_if_empty(Room* r);

/**
 * @brief Attempts to restart the game if both players confirmed replay.
 * @param r  Pointer to the room.
 */
void room_try_restart(Room* r);


// ------------------------------------------------------------
//  Reconnection logic
// ------------------------------------------------------------

/**
 * @brief Attempts to reconnect a previously disconnected client to their room.
 * @param nick      The client's nickname.
 * @param session   The client's unique reconnect session token.
 * @param newcomer  The new Client instance trying to reconnect.
 * @return Pointer to the reconnected room, or NULL if not found.
 */
Room* room_reconnect(const char* nick, const char* session, struct Client* newcomer);


// ------------------------------------------------------------
//  Utility
// ------------------------------------------------------------

/**
 * @brief Sends a formatted list of all rooms to the client.
 * @param c  Target client.
 */
void rooms_list_send(struct Client* c);


// ------------------------------------------------------------
//  Internal helper used by main/client
// ------------------------------------------------------------

/**
 * @brief Handles cleanup and notifications when a client disconnects unexpectedly.
 * @param c  Pointer to the disconnected client.
 */
void handle_disconnect(struct Client* c);


#endif // ROOM_H
