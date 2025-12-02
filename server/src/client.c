// ============================================================
//  CLIENT MODULE IMPLEMENTATION
//  ------------------------------------------------------------
//  Handles all client-side logic for communication with
//  the Tic-Tac-Toe server.
//  Responsibilities:
//   - Connection lifecycle (JOIN, QUIT, PING/PONG, RECONNECT)
//   - Dispatching protocol messages
//   - Handling room creation, join, and gameplay
// ============================================================

#include "client.h"
#include "room.h"
#include "utils.h"
#include "game.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>
#include <sys/socket.h>

// ============================================================
//  Global variables
// ============================================================

struct Client* g_clients[MAX_CLIENTS];
pthread_mutex_t g_clients_mtx = PTHREAD_MUTEX_INITIALIZER;

#define MAX_INVALID_MSG 3  // Disconnect after 3 invalid inputs


// ============================================================
//  Client lifecycle management
// ============================================================

struct Client* client_create(int fd) {
    // Enforce max_clients limit
    pthread_mutex_lock(&g_clients_mtx);
    int active = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i]) active++;
    }
    if (active >= g_config.max_clients) {
        pthread_mutex_unlock(&g_clients_mtx);
        sendp(fd, "ERROR|Server full");
        return NULL;
    }
    pthread_mutex_unlock(&g_clients_mtx);

    struct Client* c = calloc(1, sizeof(struct Client));
    if (!c) return NULL;

    c->fd = fd;
    c->state = CLIENT_STATE_LOBBY;
    c->alive = true;
    c->connected = true;
    c->missed_pongs = 0;
    c->invalid_count = 0;

    // Generate random session token for reconnect
    snprintf(c->session_id, sizeof(c->session_id),
             "%08x%08x", rand(), rand());

    // Register into global list
    pthread_mutex_lock(&g_clients_mtx);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i]) {
            g_clients[i] = c;
            break;
        }
    }
    pthread_mutex_unlock(&g_clients_mtx);
    return c;
}

void client_destroy(struct Client* c) {
    if (!c) return;

    pthread_mutex_lock(&g_clients_mtx);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] == c) {
            g_clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_clients_mtx);

    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    free(c);
}


// ============================================================
//  Client property setters
// ============================================================

void client_set_name(struct Client* c, const char* name) {
    if (!c) return;
    if (!name) name = "";
    snprintf(c->name, sizeof(c->name), "%.*s", (int)sizeof(c->name) - 1, name);
}

void client_set_state(struct Client* c, ClientState st) {
    if (c) c->state = st;
}


// ============================================================
//  Command handling (JOIN, CREATE, MOVE, etc.)
// ============================================================

static void handle_join(struct Client* c, const char* payload);
static void handle_quit(struct Client* c);
static void bump_invalid(struct Client* c, const char* reason);

static void dispatch_line(struct Client* c, const char* line) {
    if (strncmp(line, "##JOIN|", 7) == 0) {
        handle_join(c, line + 7);

    } else if (strncmp(line, "##RECONNECT|", 12) == 0) {
        char *name = strtok((char*)line + 12, "|");
        char *session = strtok(NULL, "|");

        if (!name || !session) {
            sendp(c->fd, "ERROR|Invalid reconnect format");
            bump_invalid(c, "invalid reconnect");
            return;
        }

        snprintf(c->name, sizeof(c->name), "%.*s", (int)sizeof(c->name) - 1, name);
        snprintf(c->session_id, sizeof(c->session_id), "%.*s", (int)sizeof(c->session_id) - 1, session);

        room_reconnect(c->name, c->session_id, c);

    } else if (strncmp(line, "##CREATE|", 9) == 0) {
        room_create(line + 9, c);

    } else if (strncmp(line, "##JOINROOM|", 11) == 0) {
        int id = atoi(line + 11);
        room_join(id, c);

    } else if (strncmp(line, "##EXIT|", 7) == 0) {
        room_leave(c);

    } else if (strncmp(line, "##LIST|", 7) == 0) {
        rooms_list_send(c);

    } else if (strncmp(line, "##QUIT|", 7) == 0) {
        handle_quit(c);

    } else if (strncmp(line, "##PING|", 7) == 0) {
        sendp(c->fd, "PONG|");

    } else if (strncmp(line, "##PONG|", 7) == 0) {
        c->missed_pongs = 0;

    } else if (strncmp(line, "##MOVE|", 7) == 0) {
        int x, y;
        if (!c->current_room) {
            sendp(c->fd, "ERROR|Not in game room");
            bump_invalid(c, "move outside room");
            return;
        }
        if (parse_move(line, &x, &y))
            game_move(c->current_room, c, x, y);
        else {
            sendp(c->fd, "ERROR|Invalid MOVE format");
            bump_invalid(c, "invalid move format");
        }

    } else if (strncmp(line, "##REPLAY|", 9) == 0) {
        if (!c->current_room) {
            sendp(c->fd, "ERROR|Not in room");
            bump_invalid(c, "replay outside room");
            return;
        }

        Room* r = c->current_room;
        int yes = (strcasecmp(line + 9, "YES") == 0);

        // Player declined replay (voluntary exit - no reconnect allowed)
        if (!yes) {
            sendp(c->fd, "INFO|You declined replay");

            struct Client* other = (r->p1 == c) ? r->p2 : r->p1;
            if (other) {
                sendp(other->fd, "INFO|Opponent declined replay");
                other->state = CLIENT_STATE_WAITING;
                other->current_room = r;
            }

            // Clear slot and DO NOT preserve reconnect info (voluntary exit)
            if (r->p1 == c) {
                r->p1 = NULL;
                r->p1_name[0] = '\0';
                r->p1_session[0] = '\0';
                r->p1_disconnected = false;
            }
            if (r->p2 == c) {
                r->p2 = NULL;
                r->p2_name[0] = '\0';
                r->p2_session[0] = '\0';
                r->p2_disconnected = false;
            }

            c->current_room = NULL;
            c->state = CLIENT_STATE_LOBBY;
            r->state = ROOM_WAITING;
            sendp(c->fd, "EXITED|");
            
            // If room is now empty, remove it
            if (!r->p1 && !r->p2) {
                r->state = ROOM_EMPTY;
                room_remove_if_empty(r);
            }
            return;
        }

        // Both confirmed replay
        if (r->p1 == c) r->replay_p1 = 1;
        if (r->p2 == c) r->replay_p2 = 1;
        sendp(c->fd, "INFO|Replay confirmed");
        room_try_restart(r);

    } else {
        sendp(c->fd, "ERROR|UNKNOWN_CMD");
        bump_invalid(c, "unknown command");
    }
}


// ============================================================
//  Main client thread
// ============================================================

void* client_thread(void* arg) {
    struct Client* c = (struct Client*)arg;
    char buf[512];

    sendp(c->fd, "HELLO|");

    while (c->alive) {
        int n = recv_line(c->fd, buf, sizeof(buf));
        if (n <= 0) {
            c->connected = false;
            handle_disconnect(c);
            break;
        }

        trim_newline(buf);
        dispatch_line(c, buf);
        if (!c->alive) break;
    }

    client_destroy(c);
    return NULL;
}


// ============================================================
//  Internal helpers (JOIN, QUIT)
// ============================================================

static void handle_join(struct Client* c, const char* payload) {
    char tmp[64];
    strncpy(tmp, payload, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char* bar = strchr(tmp, '|');
    if (bar) *bar = '\0';

    client_set_name(c, tmp);
    client_set_state(c, CLIENT_STATE_LOBBY);

    sendp(c->fd, "JOINED|%s", c->name);
    sendp(c->fd, "SESSION|%s", c->session_id);
}

static void handle_quit(struct Client* c) {
    sendp(c->fd, "BYE|");
    c->alive = false;
}


// ============================================================
//  Invalid message tracking
// ============================================================

static void bump_invalid(struct Client* c, const char* reason) {
    if (!c) return;
    c->invalid_count++;
    if (c->invalid_count >= MAX_INVALID_MSG) {
        sendp(c->fd, "ERROR|Too many invalid messages");
        c->alive = false;
        c->connected = false;
        shutdown(c->fd, SHUT_RDWR);
        handle_disconnect(c);
    }
}
