# Tic-Tac-Toe

A modern, full-stack implementation of the classic Tic-Tac-Toe game featuring a C# MAUI client and a C# server with TCP networking.

## Project Overview

This project demonstrates a complete client-server architecture for a multiplayer Tic-Tac-Toe game. It showcases modern software development practices including networking, event-driven architecture, and cross-platform mobile development.

## Architecture

```
Tic-Tac-Toe/
├── server/     - C# game server with TCP socket communication
└── client/     - .NET MAUI cross-platform mobile application
```

## Server

The server component handles:
- TCP socket communication with multiple clients
- Game logic and state management
- Player matchmaking and room management
- Text-based protocol for game communication
- Support for multiple concurrent game sessions

**Technology Stack:**
- C#
- .NET Framework
- TCP Sockets
- Custom text protocol for game communication

## Client

The client is a cross-platform mobile application built with .NET MAUI:
- **Platforms:** Windows, Android, iOS, macOS (via Catalyst), Tizen
- **Features:**
  - Connect to game server via TCP
  - Real-time game updates
  - Interactive game board UI
  - Player vs Player gameplay
  - Lobby and room management

**Technology Stack:**
- C# with .NET MAUI (Multi-platform App UI)
- XAML for UI design
- TCP client implementation
- Cross-platform development

## How It Works

1. **Server Start:** Launch the server to listen for incoming client connections
2. **Client Connection:** Connect the MAUI client to the server via TCP
3. **Game Flow:**
   - Players join game rooms
   - Real-time board updates via TCP protocol
   - Turn-based gameplay
   - Winner detection and game completion

## Technologies Used

- **Language:** C#
- **Frameworks:** .NET, .NET MAUI
- **Networking:** TCP Sockets
- **UI:** XAML, MAUI Controls
- **Protocol:** Custom text-based game protocol

## Getting Started

### Server
Navigate to the `server/` directory and run the server application to start listening for client connections.

### Client
The MAUI client can be built and run on any supported platform:
- Windows desktop
- Android mobile
- iOS mobile
- macOS (Catalyst)
- Tizen wearable

## Game Rules

Standard Tic-Tac-Toe rules apply:
- 3x3 game board
- Players alternate turns (X and O)
- First player to get three marks in a row (horizontal, vertical, or diagonal) wins
- Game ends in a draw if the board is full with no winner

## Project Purpose

This project serves as a portfolio demonstration of:
- Full-stack application development
- Client-server architecture design
- Cross-platform mobile development with .NET MAUI
- Network programming and TCP communication
- Real-time game state synchronization
- Object-oriented design principles

## Author

Created as a portfolio project showcasing modern C# and .NET development practices.
