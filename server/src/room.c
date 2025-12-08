// ============================================================
//  ROOM MODULE IMPLEMENTATION
//  ------------------------------------------------------------
//  Manages rooms, player assignments, reconnect logic, and
//  synchronization between clients during gameplay.
// ============================================================

#include "room.h"
#include "utils.h"
#include "client.h"
#include "config.h"
#include "log.h"

#include <string.h>
#include <stdio.h>
#include <pthread.h>

// ============================================================
//  GLOBAL DATA
// ============================================================
Room g_rooms[MAX_ROOMS];
int  g_room_count = 0;
static int g_next_room_id = 0;
pthread_mutex_t g_rooms_mtx = PTHREAD_MUTEX_INITIALIZER;

// Internal helper: assumes g_rooms_mtx is locked
static void room_remove_if_empty_locked(Room* r);
static void prune_slot(Room* r, struct Client** slot, bool* flag, time_t* ts);


// ============================================================
//  room_create()
//  ------------------------------------------------------------
//  Creates a new room, assigns the creator as Player 1 (p1),
//  and sets the initial WAITING state.
// ============================================================
Room* room_create(const char* name, struct Client* creator) {
    pthread_mutex_lock(&g_rooms_mtx);
    if (g_room_count >= g_config.max_rooms) {
        pthread_mutex_unlock(&g_rooms_mtx);
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
    logf("Room created: id=%d name=%s by %s", r->id, r->name, creator->name);
    pthread_mutex_unlock(&g_rooms_mtx);
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
    pthread_mutex_lock(&g_rooms_mtx);
    Room* r = room_find_by_id(room_id);
    if (!r) { pthread_mutex_unlock(&g_rooms_mtx); sendp(joiner->fd, "ERROR|No such room"); return NULL; }
    if (r->p1 == joiner) { pthread_mutex_unlock(&g_rooms_mtx); sendp(joiner->fd, "ERROR|Cannot join your own room"); return NULL; }

    // Normalize room so that a lone player always occupies p1.
    // This avoids a "full" room when p1 left voluntarily after a replay decline.
    if (r->p1 == NULL && r->p2 != NULL && !r->p2_disconnected) {
        r->p1 = r->p2;
        r->p2 = NULL;
        snprintf(r->p1_name, sizeof(r->p1_name), "%s", r->p1->name);
        snprintf(r->p1_session, sizeof(r->p1_session), "%s", r->p1->session_id);
        r->p2_name[0] = '\0';
        r->p2_session[0] = '\0';
        r->p2_disconnected = false;
        r->p2_disconnected_at = 0;
    }

    // Room is considered full only if both slots are occupied
    if (r->state != ROOM_WAITING || (r->p1 != NULL && r->p2 != NULL)) { pthread_mutex_unlock(&g_rooms_mtx); sendp(joiner->fd, "ERROR|Room full"); return NULL; }

    // Assign player slot
    if (r->p1 == NULL) {
        r->p1 = joiner;
        snprintf(r->p1_name, sizeof(r->p1_name), "%s", joiner->name);
        snprintf(r->p1_session, sizeof(r->p1_session), "%s", joiner->session_id);
    } else {
        r->p2 = joiner;
        snprintf(r->p2_name, sizeof(r->p2_name), "%s", joiner->name);
        snprintf(r->p2_session, sizeof(r->p2_session), "%s", joiner->session_id);
    }
    r->state = ROOM_PLAYING;

    joiner->current_room = r;
    joiner->state = CLIENT_STATE_PLAYING;

    // Notify both players
    sendp(joiner->fd, "JOINEDROOM|%d|%s", r->id, r->name);
    struct Client* first = r->p1;
    struct Client* second = r->p2;

    // Always start with a clean board for both sides (important if one player stayed after a declined replay)
    if (first)  sendp(first->fd, "CLEAR|");
    if (second) sendp(second->fd, "CLEAR|");

    sendp(first->fd, "START|Opponent:%s", second->name);
    sendp(second->fd, "START|Opponent:%s", first->name);

    // Initialize first game
    r->starting_player = 0;
    game_reset(&r->game, first);
    sendp(first->fd, "SYMBOL|X");
    sendp(second->fd, "SYMBOL|O");

    r->replay_p1 = r->replay_p2 = 0;
    game_start(r);
    logf("Room %s started game: %s (X) vs %s (O)", r->name, first->name, second->name);
    pthread_mutex_unlock(&g_rooms_mtx);
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
    pthread_mutex_lock(&g_rooms_mtx);
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
        r->p1_disconnected_at = 0;
    }
    if (!r->p2) {
        r->p2_name[0] = '\0';
        r->p2_session[0] = '\0';
        r->p2_disconnected = false;
        r->p2_disconnected_at = 0;
    }

    c->current_room = NULL;
    c->state = CLIENT_STATE_LOBBY;
    sendp(c->fd, "EXITED|");
    logf("Player %s left room %s", c->name, r->name);

    struct Client* other = (r->p1) ? r->p1 : r->p2;
    if (other && was_playing) {
        sendp(other->fd, "INFO|Opponent left");
        sendp(other->fd, "WIN|You");
        logf("Room %s: opponent left, awarding win to %s", r->name, other->name);
    }

    r->replay_p1 = r->replay_p2 = 0;

    if (!r->p1 && !r->p2) {
        r->state = ROOM_EMPTY;
        room_remove_if_empty_locked(r);
        logf("Room %s removed (empty)", r->name);
    } else if (!r->p1 || !r->p2) {
        r->state = ROOM_WAITING;
        logf("Room %s set to WAITING (one player remaining)", r->name);
    }
    pthread_mutex_unlock(&g_rooms_mtx);
}


// ============================================================
//  room_remove_if_empty()
//  ------------------------------------------------------------
//  Physically removes a room from the global array if both
//  players are gone.
// ============================================================
void room_remove_if_empty(Room* r) {
    if (!r) return;
    pthread_mutex_lock(&g_rooms_mtx);
    room_remove_if_empty_locked(r);
    pthread_mutex_unlock(&g_rooms_mtx);
}


// ============================================================
//  rooms_list_send()
//  ------------------------------------------------------------
//  Sends the current list of rooms to a requesting client.
// ============================================================
void rooms_list_send(struct Client* c) {
    char buf[512];
    pthread_mutex_lock(&g_rooms_mtx);
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
    pthread_mutex_unlock(&g_rooms_mtx);
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

    pthread_mutex_lock(&g_rooms_mtx);

    if (r->replay_p1 && r->replay_p2) {
        r->starting_player = 1 - r->starting_player;

        if (r->starting_player == 0) game_reset(&r->game, r->p1);
        else                         game_reset(&r->game, r->p2);

        r->state = ROOM_PLAYING;
        r->replay_p1 = r->replay_p2 = 0;

        sendp(r->p1->fd, "RESTART|");
        sendp(r->p2->fd, "RESTART|");
        logf("Room %s replay agreed, starting player: %s", r->name,
             r->starting_player == 0 ? r->p1->name : r->p2->name);

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
    pthread_mutex_unlock(&g_rooms_mtx);
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
    time_t now = time(NULL);

    printf("Client %s disconnected\n", c->name);
    logf("Client %s disconnected from room %s", c->name, r->name);
    pthread_mutex_lock(&g_rooms_mtx);

    // Preserve identity for reconnect
    if (r->p1 == c) {
        snprintf(r->p1_name, sizeof(r->p1_name), "%s", c->name);
        snprintf(r->p1_session, sizeof(r->p1_session), "%s", c->session_id);
        r->p1 = NULL;
        r->p1_disconnected = (r->p2 != NULL);
        r->p1_disconnected_at = now;
        r->p1_pending_win = 0;
    } else if (r->p2 == c) {
        snprintf(r->p2_name, sizeof(r->p2_name), "%s", c->name);
        snprintf(r->p2_session, sizeof(r->p2_session), "%s", c->session_id);
        r->p2 = NULL;
        r->p2_disconnected = (r->p1 != NULL);
        r->p2_disconnected_at = now;
        r->p2_pending_win = 0;
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
        pthread_mutex_unlock(&g_rooms_mtx);
        sendp(other->fd, "INFO|Opponent disconnected, waiting %d s to reconnect", g_config.disconnect_grace);
        pthread_mutex_lock(&g_rooms_mtx);
        other->state = CLIENT_STATE_WAITING;
        other->current_room = r;
        r->state = ROOM_WAITING;
        logf("Room %s waiting for reconnect of %s", r->name, c->name);
    } else {
        r->state = ROOM_EMPTY;
        room_remove_if_empty_locked(r);
        logf("Room %s empty after disconnect", r->name);
    }
    pthread_mutex_unlock(&g_rooms_mtx);
}


// ============================================================
//  room_reconnect()
//  ------------------------------------------------------------
//  Restores a disconnected player (by name + session_id)
//  back into their previous room slot.
// ============================================================
Room* room_reconnect(const char* nick, const char* session, struct Client* newcomer) {
    pthread_mutex_lock(&g_rooms_mtx);
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
                r->p1_disconnected_at = 0;
            } else {
                r->p2 = newcomer;
                r->p2_disconnected = false;
                r->p2_disconnected_at = 0;
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
                        sendp(newcomer->fd, "MOVE|%s|%d|%d", mover, x, y);
                    }
                }
            }

            // === SEND CURRENT TURN ===
            if (r->game.current_turn == newcomer) {
                sendp(newcomer->fd, "TURN|");
            }

            // Notify the other player
            if (opponent) {
                sendp(opponent->fd, "INFO|Opponent reconnected");
            }

            logf("Client %s reconnected to room %s as %c", newcomer->name, r->name, symbol);
            pthread_mutex_unlock(&g_rooms_mtx);
            return r;
        }
    }

    sendp(newcomer->fd, "ERROR|No reconnect slot");
    pthread_mutex_unlock(&g_rooms_mtx);
    return NULL;
}


// ============================================================
//  Remove long-disconnected players after grace period
// ============================================================
void rooms_prune_disconnected(int grace_seconds) {
    if (grace_seconds <= 0) return;
    time_t now = time(NULL);

    pthread_mutex_lock(&g_rooms_mtx);
    for (int i = 0; i < g_room_count; /* increment inside */) {
        Room* r = &g_rooms[i];
        bool removed = false;

        // Check player 1 slot
        if (r->p1_disconnected && r->p1_disconnected_at &&
            difftime(now, r->p1_disconnected_at) >= grace_seconds) {
            struct Client* other = r->p2;
            prune_slot(r, &r->p1, &r->p1_disconnected, &r->p1_disconnected_at);
            if (other) {
                sendp(other->fd, "INFO|Opponent did not return in time");
                sendp(other->fd, "WIN|You");
                logf("Room %s: %s timed out, win to %s", r->name, r->p1_name, other->name);
                other->current_room = NULL;
                other->state = CLIENT_STATE_LOBBY;
                r->p2 = NULL;
            }
            r->state = ROOM_EMPTY;
            room_remove_if_empty_locked(r);
            removed = true;
        }

        // Check player 2 slot (only if room still exists at index)
        if (!removed && r->p2_disconnected && r->p2_disconnected_at &&
            difftime(now, r->p2_disconnected_at) >= grace_seconds) {
            struct Client* other = r->p1;
            prune_slot(r, &r->p2, &r->p2_disconnected, &r->p2_disconnected_at);
            if (other) {
                sendp(other->fd, "INFO|Opponent did not return in time");
                sendp(other->fd, "WIN|You");
                logf("Room %s: %s timed out, win to %s", r->name, r->p2_name, other->name);
                other->current_room = NULL;
                other->state = CLIENT_STATE_LOBBY;
                r->p1 = NULL;
            }
            r->state = ROOM_EMPTY;
            room_remove_if_empty_locked(r);
            removed = true;
        }

        if (!removed) {
            i++;
        } else {
            // After removal, rooms array may be shifted left
            if (i >= g_room_count) break;
        }
    }
    pthread_mutex_unlock(&g_rooms_mtx);
}


// ============================================================
//  Internal helper: remove room if empty (expects lock held)
// ============================================================
static void room_remove_if_empty_locked(Room* r) {
    if (!r) return;

    int idx = -1;
    for (int i = 0; i < g_room_count; i++) {
        if (&g_rooms[i] == r) { idx = i; break; }
    }
    if (idx != -1 && !r->p1 && !r->p2) {
        for (int j = idx; j < g_room_count - 1; j++)
            g_rooms[j] = g_rooms[j + 1];
        g_room_count--;
    }
}


// ============================================================
//  Helper to clear a disconnected slot (expects lock held)
// ============================================================
static void prune_slot(Room* r, struct Client** slot, bool* flag, time_t* ts) {
    if (!r || !slot || !flag || !ts) return;
    *slot = NULL;
    *flag = false;
    *ts = 0;
    r->replay_p1 = r->replay_p2 = 0;
}
