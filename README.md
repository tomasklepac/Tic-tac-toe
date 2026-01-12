# Tic-Tac-Toe

A modern, full-stack implementation of the classic Tic-Tac-Toe game featuring a Cross-Platform C# Avalonia client and a C server with TCP networking.

## Project Overview

This project demonstrates a complete client-server architecture for a multiplayer Tic-Tac-Toe game. It showcases modern software development practices including networking, event-driven architecture, and cross-platform desktop/mobile development.

## Architecture

```
Tic-Tac-Toe/
├── server/     - C game server with TCP socket communication
└── client/     - C# Avalonia cross-platform application
```

## Server

The server component handles:
- TCP socket communication with multiple clients
- Game logic and state management
- Player matchmaking and room management
- Text-based protocol for game communication
- Support for multiple concurrent game sessions
- Robust resource management and memory safety

**Technology Stack:**
- C Language (C11 standard)
- POSIX Sockets (Linux/Unix environment recommended)
- Multi-threading (pthread)
- Custom text protocol for game communication

## Client

The client is a cross-platform application built with C# and Avalonia UI:
- **Platforms:** Windows, Linux, macOS
- **Features:**
  - Connect to game server via TCP
  - Real-time game updates
  - Interactive game board UI
  - Player vs Player gameplay
  - Lobby and room management
  - Modern, responsive UI

**Technology Stack:**
- C# with .NET 8
- Avalonia UI Framework
- TCP client implementation (System.Net.Sockets)
- MVVM Architecture (ReactiveUI)

## How It Works

1. **Server Start:** Launch the server to listen for incoming client connections.
2. **Client Connection:** Connect the Avalonia client to the server via TCP.
3. **Game Flow:**
   - Players join game rooms
   - Real-time board updates via TCP protocol
   - Turn-based gameplay
   - Winner detection and game completion

## Technologies Used

- **Client Language:** C# (.NET 8)
- **Server Language:** C
- **UI Framework:** Avalonia UI
- **Networking:** TCP Sockets
- **Protocol:** Custom text-based game protocol

## Getting Started

### Server
Navigate to the `server/` directory and compile the server using text:
```bash
cd server
make
./server
```
(Note: Designed for Linux/Unix environments or WSL on Windows)

### Client
The Avalonia client can be built and run on any supported platform:
```bash
cd client
dotnet run
```

## Game Rules

Standard Tic-Tac-Toe rules apply:
- 3x3 game board
- Players alternate turns (X and O)
- First player to get three marks in a row (horizontal, vertical, or diagonal) wins
- Game ends in a draw if the board is full with no winner or if a player disconnects.

## Project Purpose

This project serves as a portfolio demonstration of:
- Full-stack application development (C & C#)
- Client-server architecture design
- Cross-platform development with Avalonia UI
- Low-level network programming (C Sockets) and high-level client networking
- Real-time game state synchronization
- Text protocol design and implementation

## Author

Created as a term project for KIV/UPS.
