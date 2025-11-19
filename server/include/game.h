#ifndef GAME_H
#define GAME_H

// ============================================================
//  GAME MODULE HEADER
//  ------------------------------------------------------------
//  Contains game board logic and validation utilities for the
//  Tic-Tac-Toe server.
//
//  Responsibilities:
//   - Maintaining game board state
//   - Checking for wins / draws
//   - Processing player moves
//   - Resetting and starting new rounds
// ============================================================

#define SIZE 3  // 3x3 Tic-Tac-Toe grid

// Forward declarations
struct Client;
struct Room;


// ------------------------------------------------------------
//  Game structure
// ------------------------------------------------------------
typedef struct Game {
    char board[SIZE][SIZE];     // Current board (X / O / space)
    struct Client* current_turn; // Pointer to the client whose turn it is
    int state;                   // 0 = running, 1 = win, 2 = draw
} Game;


// ------------------------------------------------------------
//  Utility functions
// ------------------------------------------------------------

/**
 * @brief Parses a "MOVE" message and extracts coordinates.
 *        Format: ##MOVE|<x>|<y>
 *
 * @param buf Input buffer containing message
 * @param x   Output X coordinate (0–2)
 * @param y   Output Y coordinate (0–2)
 * @return 1 if valid, 0 otherwise
 */
int parse_move(const char* buf, int* x, int* y);

/**
 * @brief Parses "YES" / "NO" type responses.
 *
 * @param buf Input buffer
 * @return 1 for YES, 0 for NO
 */
int parse_yesno(const char* buf);

/**
 * @brief Checks for a winner or draw condition.
 *
 * @param board Current game board
 * @return 1 if someone won, 2 for draw, 0 for ongoing
 */
int check_win(char board[SIZE][SIZE]);


// ------------------------------------------------------------
//  Game management
// ------------------------------------------------------------
/**
 * @brief Resets the game board and sets the first player.
 *
 * @param g     Pointer to Game struct
 * @param first Client who starts the new round
 */
void game_reset(Game* g, struct Client* first);

/**
 * @brief Sends start message to both players, initializes turn.
 *
 * @param r Pointer to active Room
 */
void game_start(struct Room* r);

/**
 * @brief Processes a player move.
 *
 * @param r   Room containing the game
 * @param who Player making the move
 * @param x   X coordinate
 * @param y   Y coordinate
 * @return 1 if valid move, 0 otherwise
 */
int game_move(struct Room* r, struct Client* who, int x, int y);

#endif // GAME_H
