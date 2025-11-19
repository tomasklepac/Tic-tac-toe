// ============================================================
//  ROOM MODULE IMPLEMENTATION
//  ------------------------------------------------------------
//  Manages rooms, player assignments, reconnect logic, and
//  synchronization between clients during gameplay.
// ============================================================

#include "room.h"
#include "utils.h"
#include "client.h"

#include <string.h>
#include <stdio.h>

// ============================================================
//  GLOBAL DATA
// ============================================================
Room g_rooms[MAX_ROOMS];
int  g_room_count = 0;
static int g_next_room_id = 0;


// ============================================================
//  room_create()
//  ------------------------------------------------------------
//  Creates a new room, assigns the creator as Player 1 (p1),
//  and sets the initial WAITING state.
// ============================================================
Room* room_create(const char* name, struct Client* creator) {
    if (g_room_count >= MAX_ROOMS) {
        sendp(creator->fd, "ERROR|Lobby full");
        return NULL;
    }

    Room* r = &g_rooms[g_room_count++];
    memset(r, 0, sizeof(Room));

    r->id = g_next_room_id++;
    snprintf(r->name, sizeof(r->name), "%s", name);

    r->state = ROOM_WAITING;
    r->starting_player = 0;

    r->p1 = creator;
    r->p2 = NULL;
    r->p1_disconnected = false;
    r->p2_disconnected = false;

    // Store identity for reconnect
    snprintf(r->p1_name, sizeof(r->p1_name), "%s", creator->name);
    snprintf(r->p1_session, sizeof(r->p1_session), "%s", creator->session_id);

    creator->current_room = r;
    creator->state = CLIENT_STATE_WAITING;

    r->replay_p1 = 0;
    r->replay_p2 = 0;

    sendp(creator->fd, "CREATED|%d|%s", r->id, r->name);
    return r;
}


// ============================================================
//  room_find_by_id()
//  ------------------------------------------------------------
//  Finds a room by its unique ID.
// ============================================================
static Room* room_find_by_id(int id) {
    for (int i = 0; i < g_room_count; i++)
        if (g_rooms[i].id == id) return &g_rooms[i];
    return NULL;
}


// ============================================================
//  room_join()
//  ------------------------------------------------------------
//  Allows a second player to join an existing WAITING room.
// ============================================================
Room* room_join(int room_id, struct Client* joiner) {
    Room* r = room_find_by_id(room_id);
    if (!r) { sendp(joiner->fd, "ERROR|No such room"); return NULL; }
    if (r->p1 == joiner) { sendp(joiner->fd, "ERROR|Cannot join your own room"); return NULL; }
    if (r->state != ROOM_WAITING || r->p2 != NULL) { sendp(joiner->fd, "ERROR|Room full"); return NULL; }

    // Assign player slot
    r->p2 = joiner;
    snprintf(r->p2_name, sizeof(r->p2_name), "%s", joiner->name);
    snprintf(r->p2_session, sizeof(r->p2_session), "%s", joiner->session_id);
    r->state = ROOM_PLAYING;

    joiner->current_room = r;
    joiner->state = CLIENT_STATE_PLAYING;

    // Notify both players
    sendp(joiner->fd, "JOINEDROOM|%d|%s", r->id, r->name);
    sendp(r->p1->fd, "START|Opponent:%s", joiner->name);
    sendp(joiner->fd, "START|Opponent:%s", r->p1->name);

    // Initialize first game
    r->starting_player = 0;
    game_reset(&r->game, r->p1);
    sendp(r->p1->fd, "SYMBOL|X");
    sendp(r->p2->fd, "SYMBOL|O");

    r->replay_p1 = r->replay_p2 = 0;
    game_start(r);
    return r;
}


// ============================================================
//  room_leave()
//  ------------------------------------------------------------
//  Handles voluntary leaving of a player. Keeps identity for
//  reconnect, updates room state accordingly.
// ============================================================
void room_leave(struct Client* c) {
    Room* r = c->current_room;
    if (!r) return;
    int was_playing = (r->state == ROOM_PLAYING);

    if (r->p1 == c) r->p1 = NULL;
    if (r->p2 == c) r->p2 = NULL;

    /* Voluntary exit (triggered by ##EXIT|): do NOT preserve the
     * departing player's name/session for reconnect. Only unexpected
     * disconnects (handled in handle_disconnect) should mark a slot
     * as disconnected and keep identity for reconnect attempts. */
    if (!r->p1) {
        r->p1_name[0] = '\0';
        r->p1_session[0] = '\0';
        r->p1_disconnected = false;
    }
    if (!r->p2) {
        r->p2_name[0] = '\0';
        r->p2_session[0] = '\0';
        r->p2_disconnected = false;
    }

    c->current_room = NULL;
    c->state = CLIENT_STATE_LOBBY;
    sendp(c->fd, "EXITED|");

    struct Client* other = (r->p1) ? r->p1 : r->p2;
    if (other && was_playing) {
        sendp(other->fd, "INFO|Opponent left");
        sendp(other->fd, "WIN|You");
    }

    r->replay_p1 = r->replay_p2 = 0;

    if (!r->p1 && !r->p2) {
        r->state = ROOM_EMPTY;
        room_remove_if_empty(r);
    } else if (!r->p1 || !r->p2) {
        r->state = ROOM_WAITING;
    }
}


// ============================================================
//  room_remove_if_empty()
//  ------------------------------------------------------------
//  Physically removes a room from the global array if both
//  players are gone.
// ============================================================
void room_remove_if_empty(Room* r) {
    if (!r) return;

    int idx = -1;
    for (int i = 0; i < g_room_count; i++) {
        if (&g_rooms[i] == r) { idx = i; break; }
    }
    if (idx == -1) return;

    if (!r->p1 && !r->p2) {
        for (int j = idx; j < g_room_count - 1; j++)
            g_rooms[j] = g_rooms[j + 1];
        g_room_count--;
    }
}


// ============================================================
//  rooms_list_send()
//  ------------------------------------------------------------
//  Sends the current list of rooms to a requesting client.
// ============================================================
void rooms_list_send(struct Client* c) {
    char buf[512];
    int off = snprintf(buf, sizeof(buf), "ROOMS|%d", g_room_count);

    for (int i = 0; i < g_room_count; i++) {
        Room* r = &g_rooms[i];
        if (r->state == ROOM_EMPTY) continue;

        int players = 0;
        if (r->p1) players++;
        if (r->p2) players++;

        off += snprintf(buf + off, sizeof(buf) - off,
                        "|%d|%s|%s|%d/2",
                        r->id, r->name,
                        (r->state == ROOM_WAITING ? "WAITING" : "PLAYING"),
                        players);
    }
    sendp(c->fd, "%s", buf);
}


// ============================================================
//  room_try_restart()
//  ------------------------------------------------------------
//  When both players confirm "Play Again", resets the board,
//  swaps the starting player, and starts a new round.
// ============================================================
void room_try_restart(Room* r) {
    if (!r || !r->p1 || !r->p2) return;

    if (r->replay_p1 && r->replay_p2) {
        r->starting_player = 1 - r->starting_player;

        if (r->starting_player == 0) game_reset(&r->game, r->p1);
        else                         game_reset(&r->game, r->p2);

        r->state = ROOM_PLAYING;
        r->replay_p1 = r->replay_p2 = 0;

        sendp(r->p1->fd, "RESTART|");
        sendp(r->p2->fd, "RESTART|");

        if (r->starting_player == 0) {
            sendp(r->p1->fd, "TURN|Your move");
            sendp(r->p1->fd, "SYMBOL|X");
            sendp(r->p2->fd, "SYMBOL|O");
        } else {
            sendp(r->p2->fd, "TURN|Your move");
            sendp(r->p2->fd, "SYMBOL|X");
            sendp(r->p1->fd, "SYMBOL|O");
        }
    }
}


// ============================================================
//  handle_disconnect()
//  ------------------------------------------------------------
//  Handles forced disconnect (lost connection / heartbeat timeout)
//  and marks the player slot as temporarily disconnected.
// ============================================================
void handle_disconnect(struct Client* c) {
    if (!c || !c->current_room) return;
    Room* r = c->current_room;

    printf("Client %s disconnected\n", c->name);

    // Preserve identity for reconnect
    if (r->p1 == c) {
        snprintf(r->p1_name, sizeof(r->p1_name), "%s", c->name);
        snprintf(r->p1_session, sizeof(r->p1_session), "%s", c->session_id);
        r->p1 = NULL;
        r->p1_disconnected = (r->p2 != NULL);
    } else if (r->p2 == c) {
        snprintf(r->p2_name, sizeof(r->p2_name), "%s", c->name);
        snprintf(r->p2_session, sizeof(r->p2_session), "%s", c->session_id);
        r->p2 = NULL;
        r->p2_disconnected = (r->p1 != NULL);
    }

    // Reset current turn if he was on move
    if (r->game.current_turn == c) {
        r->game.current_turn = NULL;
    }

    c->connected = false;
    c->current_room = NULL;
    c->state = CLIENT_STATE_LOBBY;

    struct Client* other = (r->p1) ? r->p1 : r->p2;
    if (other) {
        sendp(other->fd, "INFO|Opponent disconnected");
        sendp(other->fd, "WIN|You");
        other->state = CLIENT_STATE_WAITING;
        other->current_room = r;
        r->state = ROOM_WAITING;
    } else {
        r->state = ROOM_EMPTY;
        room_remove_if_empty(r);
    }
}


// ============================================================
//  room_reconnect()
//  ------------------------------------------------------------
//  Restores a disconnected player (by name + session_id)
//  back into their previous room slot.
// ============================================================
Room* room_reconnect(const char* nick, const char* session, struct Client* newcomer) {
    for (int i = 0; i < g_room_count; ++i) {
        Room* r = &g_rooms[i];

        bool match_p1 = (!r->p1 && r->p1_disconnected &&
                         strncmp(r->p1_name, nick, sizeof(r->p1_name)) == 0 &&
                         strncmp(r->p1_session, session, sizeof(r->p1_session)) == 0);

        bool match_p2 = (!r->p2 && r->p2_disconnected &&
                         strncmp(r->p2_name, nick, sizeof(r->p2_name)) == 0 &&
                         strncmp(r->p2_session, session, sizeof(r->p2_session)) == 0);

        if (match_p1 || match_p2) {
            if (match_p1) {
                r->p1 = newcomer;
                r->p1_disconnected = false;
            } else {
                r->p2 = newcomer;
                r->p2_disconnected = false;
            }

            newcomer->current_room = r;
            newcomer->state = (r->p1 && r->p2) ? CLIENT_STATE_PLAYING : CLIENT_STATE_WAITING;

            // === SEND RECONNECT HANDSHAKE ===
            sendp(newcomer->fd, "RECONNECTED|");
            Client* opponent = match_p1 ? r->p2 : r->p1;
            char symbol = match_p1 ? 'X' : 'O';
            sendp(newcomer->fd, "START|Opponent:%s", opponent ? opponent->name : "Unknown");
            sendp(newcomer->fd, "SYMBOL|%c", symbol);

            // === SEND PREVIOUS MOVES ===
            for (int y = 0; y < SIZE; ++y) {
                for (int x = 0; x < SIZE; ++x) {
                    char ch = r->game.board[y][x];
                    if (ch == 'X' || ch == 'O') {
                        const char* mover = (ch == 'X') ? r->p1_name : r->p2_name;
                        sendp(newcomer->fd, "##MOVE|%s|%d|%d", mover, x, y);
                    }
                }
            }

            // === SEND CURRENT TURN ===
            if (r->game.current_turn == newcomer) {
                sendp(newcomer->fd, "##TURN|");
            }

            // Notify the other player
            if (opponent) {
                sendp(opponent->fd, "INFO|Opponent reconnected");
            }

            return r;
        }
    }

    sendp(newcomer->fd, "##ERROR|No reconnect slot");
    return NULL;
}