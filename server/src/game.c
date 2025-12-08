// ============================================================
//  GAME MODULE IMPLEMENTATION
//  ------------------------------------------------------------
//  Handles all game logic for the Tic-Tac-Toe server.
//  Responsible for board management, move validation,
//  and determining win/draw states.
// ============================================================

#include "game.h"
#include "room.h"
#include "utils.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================
//  game_reset()
//  ------------------------------------------------------------
//  Clears the board and assigns the first player to move.
// ============================================================
void game_reset(Game* g, struct Client* first) {
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < SIZE; ++x)
            g->board[y][x] = ' ';

    g->current_turn = first;
    g->state = 0;  // running
}


// ============================================================
//  game_start()
//  ------------------------------------------------------------
//  Starts a new game round and signals the first player to move.
// ============================================================
void game_start(struct Room* r) {
    game_reset(&r->game, r->p1);
    if (r->p1)
        sendp(r->p1->fd, "TURN|Your move");
}


// ============================================================
//  game_move()
//  ------------------------------------------------------------
//  Processes a player move and checks for game outcome.
//
//  Returns 1 on success, 0 if invalid move or error.
// ============================================================
int game_move(struct Room* r, struct Client* who, int x, int y) {
    if (!r || !who) return 0;
    Game* g = &r->game;

    // --- validation ---
    if (g->state != 0) { sendp(who->fd, "ERROR|Game finished"); return 0; }
    if (who != g->current_turn) { sendp(who->fd, "ERROR|Not your turn"); return 0; }
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE) { sendp(who->fd, "ERROR|Invalid position"); return 0; }
    if (g->board[y][x] != ' ') { sendp(who->fd, "ERROR|Occupied"); return 0; }

    // --- place symbol ---
    char sym = (who == r->p1) ? 'X' : 'O';
    g->board[y][x] = sym;
    logf("Move: room %s %s (%c) -> %d,%d", r->name, who->name, sym, x, y);

    // Broadcast move to both players
    if (r->p1) sendp(r->p1->fd, "MOVE|%s|%d|%d", who->name, x, y);
    if (r->p2) sendp(r->p2->fd, "MOVE|%s|%d|%d", who->name, x, y);

    // --- check for result ---
    int res = check_win(g->board);
    if (res == 1) { // Win
        g->state = 1;
        r->replay_p1 = r->replay_p2 = 0;

        if (who == r->p1) {
            if (r->p1) sendp(r->p1->fd, "WIN|You");
            if (r->p2) sendp(r->p2->fd, "LOSE|%s", r->p1->name);
            logf("Game result room %s: %s wins vs %s", r->name, r->p1_name, r->p2_name);
        } else {
            if (r->p2) sendp(r->p2->fd, "WIN|You");
            if (r->p1) sendp(r->p1->fd, "LOSE|%s", r->p2->name);
            logf("Game result room %s: %s wins vs %s", r->name, r->p2_name, r->p1_name);
        }
        
        /* If opponent is missing, end game without replay option */
        if (!r->p1 || !r->p2) {
            if (r->p1) sendp(r->p1->fd, "INFO|Game ended");
            if (r->p2) sendp(r->p2->fd, "INFO|Game ended");
            r->state = ROOM_WAITING;
        }
        return 1;
    }
    else if (res == 2) { // Draw
        g->state = 2;
        r->replay_p1 = r->replay_p2 = 0;
        if (r->p1) sendp(r->p1->fd, "DRAW|");
        if (r->p2) sendp(r->p2->fd, "DRAW|");
        logf("Game result room %s: draw", r->name);
        return 1;
    }

    // --- next turn ---
    g->current_turn = (g->current_turn == r->p1) ? r->p2 : r->p1;
    if (g->current_turn)
        sendp(g->current_turn->fd, "TURN|Your move");
    return 1;
}


// ============================================================
//  parse_move()
//  ------------------------------------------------------------
//  Parses "##MOVE|x|y" and extracts coordinates.
//  Returns 1 if valid, 0 otherwise.
// ============================================================
int parse_move(const char* buf, int* x, int* y) {
    if (strncmp(buf, "##MOVE|", 7) != 0)
        return 0;

    const char* p = buf + 7;
    char* end;
    long xv = strtol(p, &end, 10);
    if (*end != '|')
        return 0;

    long yv = strtol(end + 1, &end, 10);
    if (xv < 0 || xv >= SIZE || yv < 0 || yv >= SIZE)
        return 0;

    *x = (int)xv;
    *y = (int)yv;
    return 1;
}


// ============================================================
//  parse_yesno()
//  ------------------------------------------------------------
//  Parses "##YES|" or "##NO|" style protocol messages.
//  Returns 1 for YES, 0 for NO, -1 for invalid.
// ============================================================
int parse_yesno(const char* buf) {
    if (strncmp(buf, "##YES|", 6) == 0) return 1;
    if (strncmp(buf, "##NO|", 5) == 0)  return 0;
    return -1;
}


// ============================================================
//  check_win()
//  ------------------------------------------------------------
//  Evaluates the game board and checks for:
//    - Win (1)
//    - Draw (2)
//    - Ongoing game (0)
// ============================================================
int check_win(char board[SIZE][SIZE]) {
    // Rows
    for (int i = 0; i < SIZE; i++)
        if (board[i][0] != ' ' && board[i][0] == board[i][1] && board[i][1] == board[i][2])
            return 1;

    // Columns
    for (int i = 0; i < SIZE; i++)
        if (board[0][i] != ' ' && board[0][i] == board[1][i] && board[1][i] == board[2][i])
            return 1;

    // Diagonals
    if (board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2])
        return 1;
    if (board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0])
        return 1;

    // Check draw
    int full = 1;
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (board[i][j] == ' ')
                full = 0;

    return full ? 2 : 0;
}
