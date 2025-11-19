using System;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.ObjectModel;
using Microsoft.Extensions.Logging;

namespace PiskvorkyClientGUI;

/// <summary>
/// Main page for the Tic-Tac-Toe (Piškvorky) MAUI client.
/// Responsibilities:
/// - Manage TCP connection to the game server
/// - Parse and handle server text protocol messages
/// - Keep and update game / lobby UI state
/// </summary>
public partial class MainPage : ContentPage
{
    #region Fields & Properties
    // -------------------------------------------------------------------------
    // Networking fields
    // -------------------------------------------------------------------------
    private TcpClient? _client;
    private StreamReader? _reader;
    private StreamWriter? _writer;
    private CancellationTokenSource? _cts;

    // -------------------------------------------------------------------------
    // Client / game state
    // -------------------------------------------------------------------------
    private string _playerName = string.Empty;
    private string _sessionId = string.Empty;
    private string _opponentName = string.Empty;
    private bool _isMyTurn = false;
    private string _mySymbol = string.Empty; // "X" or "O"

    // Rooms collection bound to the UI (rooms list in lobby)
    public ObservableCollection<RoomInfo> Rooms { get; } = new();
    private RoomInfo? _selectedRoom;

    // -------------------------------------------------------------------------
    // Session persistence constants
    // -------------------------------------------------------------------------
    private const string SessionFile = "session.txt";

    // -------------------------------------------------------------------------
    // Logger instance
    // -------------------------------------------------------------------------
    private readonly ILogger<MainPage> _logger;

    // Tracks whether opponent left/disconnected (used to decide replay behaviour)
    private bool _opponentLeft = false;
    #endregion

    #region Constructor
    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    public MainPage(ILogger<MainPage> logger)
    {
        InitializeComponent();
        BindingContext = this;
        _logger = logger;

        _logger.LogInformation("MainPage initialized.");

        // Try to load existing session on startup (for automatic reconnect)
        var existingSession = LoadSession();
        if (existingSession != null)
        {
            _playerName = existingSession.Value.name;
            _sessionId = existingSession.Value.session;
        }
    }
    #endregion

    #region Session Persistence Helpers
    // -------------------------------------------------------------------------
    // Session helpers - save/load/clear session info in app data directory
    // -------------------------------------------------------------------------

    /// <summary>
    /// Save session information to local app storage (plain text, pipe-separated).
    /// Format: name|session|state
    /// </summary>
    private void SaveSession(string name, string session, string state)
    {
        var path = Path.Combine(FileSystem.Current.AppDataDirectory, SessionFile);
        File.WriteAllText(path, $"{name}|{session}|{state}");
    }

    /// <summary>
    /// Load session info if present. Returns tuple or null when missing/invalid.
    /// </summary>
    private (string name, string session, string state)? LoadSession()
    {
        var path = Path.Combine(FileSystem.Current.AppDataDirectory, SessionFile);
        if (!File.Exists(path)) return null;

        var parts = File.ReadAllText(path).Split('|');
        if (parts.Length < 3) return null;

        return (parts[0], parts[1], parts[2]);
    }

    /// <summary>
    /// Delete the local session file if it exists and clear session id in memory.
    /// </summary>
    private void ClearSession()
    {
        var path = Path.Combine(FileSystem.Current.AppDataDirectory, SessionFile);
        if (File.Exists(path)) File.Delete(path);
        _sessionId = string.Empty;
    }
    #endregion

    #region Networking – Send / Connect / Disconnect / Listener
    // -------------------------------------------------------------------------
    // Networking: generic send helper
    // -------------------------------------------------------------------------

    /// <summary>
    /// Send a single protocol line to the server. No-op when writer is not available.
    /// </summary>
    private async Task SendAsync(string msg)
    {
        if (_writer == null) return;

        try
        {
            _logger.LogInformation("Sending message to server: {Message}", msg);
            await _writer.WriteLineAsync(msg);
            await _writer.FlushAsync();
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to send message: {Message}", msg);
        }
    }

    // -------------------------------------------------------------------------
    // Connect button handler
    // Establish TCP connection and start background listener
    // -------------------------------------------------------------------------

    /// <summary>
    /// Handles the Connect button:
    /// - Validates IP, port and username
    /// - Opens TCP connection
    /// - Creates reader/writer
    /// - Tries RECONNECT (if session exists) or does JOIN
    /// - Requests room list and switches UI to rooms view
    /// </summary>
    private async void OnConnectClicked(object sender, EventArgs e)
    {
        string ip = EntryIP.Text?.Trim() ?? string.Empty;
        string name = EntryName.Text?.Trim() ?? string.Empty;

        if (string.IsNullOrWhiteSpace(ip) || string.IsNullOrWhiteSpace(name))
        {
            _logger.LogWarning("Connection attempt failed: IP or username is empty.");
            await DisplayAlert("Error", "Please enter IP, port and username.", "OK");
            return;
        }

        if (!int.TryParse(EntryPort.Text?.Trim(), out int port))
        {
            await DisplayAlert("Error", "Invalid port number.", "OK");
            return;
        }

        try
        {
            _logger.LogInformation("Attempting to connect to server at {IP}:{Port}.", ip, port);
            _client = new TcpClient();
            await _client.ConnectAsync(ip, port);

            _logger.LogInformation("Connected to server at {IP}:{Port}.", ip, port);

            // Create reader/writer over network stream (UTF-8)
            var networkStream = _client.GetStream();
            _reader = new StreamReader(networkStream, Encoding.UTF8);
            _writer = new StreamWriter(networkStream, new UTF8Encoding(false))
            {
                AutoFlush = true
            };

            _playerName = name;

            // Attempt to reconnect using saved session for the same username
            var existing = LoadSession();
            if (existing != null && existing.Value.name == name)
            {
                _sessionId = existing.Value.session;
                await SendAsync($"##RECONNECT|{name}|{_sessionId}");
                _logger.LogInformation("Reconnection attempt with session ID {SessionId}.", _sessionId);
            }
            else
            {
                await SendAsync($"##JOIN|{name}");
                _logger.LogInformation("Joining server as new player: {PlayerName}.", name);
            }

            // Request list of rooms after connecting
            await SendAsync("##LIST|");
            _logger.LogInformation("Requested room list from server.");

            // Update UI to rooms view
            LabelLobbyStatus.IsVisible = false;
            LobbyView.IsVisible = false;
            RoomsView.IsVisible = true;
            GameRoomView.IsVisible = false;

            // Start listener with cancellation support
            _cts = new CancellationTokenSource();
            _ = Task.Run(() => ListenToServerAsync(_cts.Token));
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Connection to server failed.");
            await DisplayAlert("Connection Failed", ex.Message, "OK");
            LabelLobbyStatus.Text = "Connection failed.";
            LabelLobbyStatus.IsVisible = true;
            await SafeDisconnectAsync();
        }
    }

    // -------------------------------------------------------------------------
    // Listener - reads lines from server and dispatches them to handler
    // -------------------------------------------------------------------------

    /// <summary>
    /// Background listener loop that reads text lines from the server
    /// and forwards them to <see cref="HandleServerMessage"/> on the UI thread.
    /// </summary>
    private async Task ListenToServerAsync(CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested)
            {
                string? line;
                try
                {
                    line = await _reader!.ReadLineAsync();
                }
                catch (Exception ex)
                {
                    _logger.LogError(ex, "Error reading from server.");
                    break;
                }

                if (line == null) break;

                _logger.LogInformation("Received message from server: {Message}", line);

                // Dispatch to UI thread to update visuals safely
                Dispatcher.Dispatch(() => HandleServerMessage(line));
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Listener encountered an error.");
            Dispatcher.Dispatch(async () =>
            {
                await DisplayAlert("Connection Lost", ex.Message, "OK");
            });
        }
        finally
        {
            _logger.LogWarning("Listener stopped.");
            await SafeDisconnectAsync();
        }
    }

    // -------------------------------------------------------------------------
    // Centralized disconnect / cleanup
    // -------------------------------------------------------------------------

    /// <summary>
    /// Gracefully closes network resources and resets networking fields.
    /// Does NOT modify UI visibility, only network-related objects.
    /// </summary>
    private async Task SafeDisconnectAsync()
    {
        try
        {
            _logger.LogInformation("Disconnecting from server...");
            _cts?.Cancel();
            _cts?.Dispose();
            _cts = null;

            if (_writer != null)
            {
                try { await _writer.DisposeAsync(); } catch { }
                _writer = null;
            }

            if (_reader != null)
            {
                // Changed from DisposeAsync to Dispose (as per earlier fix)
                try { _reader.Dispose(); } catch { }
                _reader = null;
            }

            if (_client != null)
            {
                try { _client.Close(); _client.Dispose(); } catch { }
                _client = null;
            }

            _logger.LogInformation("Disconnected from server.");
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error during disconnection.");
        }
    }
    #endregion

    #region Protocol Handling – HandleServerMessage
    // -------------------------------------------------------------------------
    // Server message processing
    // -------------------------------------------------------------------------

    /// <summary>
    /// Central handler for all server messages (text protocol).
    /// Routes messages by prefix (##PING, ##INFO, ##ROOMS, ##MOVE, ...),
    /// updates UI and internal state accordingly.
    /// </summary>
    private async void HandleServerMessage(string line)
    {
        try
        {
            // -----------------------------------------------------------------
            // HEARTBEAT
            // -----------------------------------------------------------------
            if (line.StartsWith("##PING|", StringComparison.Ordinal))
            {
                await SendAsync("##PONG|");
                return;
            }

            // -----------------------------------------------------------------
            // INFO / ERROR messages
            // -----------------------------------------------------------------
            if (line.StartsWith("##INFO|", StringComparison.Ordinal))
            {
                string msg = line.Length > 7 ? line.Substring(7) : string.Empty;
                LabelStatus.Text = msg;
                LabelLobbyStatus.Text = msg;

                // Mark opponent as left when info message indicates it
                if (msg.Contains("Opponent left", StringComparison.OrdinalIgnoreCase) ||
                    msg.Contains("Opponent disconnected", StringComparison.OrdinalIgnoreCase))
                {
                    _opponentLeft = true;
                }

                return;
            }

            if (line.StartsWith("##ERROR|", StringComparison.Ordinal))
            {
                string msg = line.Length > 8 ? line.Substring(8) : string.Empty;
                LabelStatus.Text = msg;
                LabelLobbyStatus.Text = msg;

                // Fallback for failed RECONNECT: clear session and try JOIN
                if (msg.Contains("No reconnect slot", StringComparison.OrdinalIgnoreCase) ||
                    msg.Contains("Invalid session", StringComparison.OrdinalIgnoreCase))
                {
                    ClearSession();
                    if (!string.IsNullOrEmpty(_playerName))
                    {
                        await SendAsync($"##JOIN|{_playerName}");
                        await SendAsync("##LIST|");
                    }
                    return;
                }

                await DisplayAlert("Server error", msg, "OK");
                return;
            }

            // -----------------------------------------------------------------
            // INITIAL HANDSHAKE MESSAGES
            // -----------------------------------------------------------------
            if (line.StartsWith("##HELLO|", StringComparison.Ordinal))
            {
                LabelLobbyStatus.Text = line.Length > 8 ? line.Substring(8) : string.Empty;
                LabelLobbyStatus.IsVisible = true;
                return;
            }

            if (line.StartsWith("##JOINED|", StringComparison.Ordinal))
            {
                _playerName = line.Length > 9 ? line.Substring(9) : string.Empty;
                return;
            }

            if (line.StartsWith("##SESSION|", StringComparison.Ordinal))
            {
                _sessionId = line.Length > 10 ? line.Substring(10) : string.Empty;
                SaveSession(_playerName, _sessionId, "CONNECTED");
                return;
            }

            if (line.StartsWith("##RECONNECTED|", StringComparison.Ordinal))
            {
                LabelStatus.Text = "Reconnected to game.";
                SwitchToGameRoom();
                return;
            }

            // -----------------------------------------------------------------
            // ROOMS LIST
            // -----------------------------------------------------------------
            if (line.StartsWith("##ROOMS|", StringComparison.Ordinal))
            {
                var parts = line.Split('|', StringSplitOptions.None);
                Rooms.Clear();

                for (int i = 2; i + 3 < parts.Length; i += 4)
                {
                    if (!int.TryParse(parts[i], out var rid)) continue;
                    string rname = parts[i + 1];
                    string state = parts[i + 2];
                    string occ = parts[i + 3];

                    string display = state == "PLAYING" ? $"{rname} (playing)"
                                   : state == "WAITING" ? rname
                                   : rname;

                    Rooms.Add(new RoomInfo
                    {
                        Id = rid,
                        Name = display,
                        Capacity = occ
                    });
                }

                // Only switch to rooms view if not currently in a game
                if (!GameRoomView.IsVisible)
                {
                    RoomsView.IsVisible = true;
                    LobbyView.IsVisible = false;
                }
                return;
            }

            // -----------------------------------------------------------------
            // ROOM CREATED / JOINED responses
            // -----------------------------------------------------------------
            if (line.StartsWith("##CREATED|", StringComparison.Ordinal))
            {
                var parts = line.Split('|');
                if (parts.Length >= 3)
                {
                    if (int.TryParse(parts[1], out var rid))
                    {
                        string rname = parts[2];
                        LabelStatus.Text = $"Room '{rname}' created, waiting for opponent...";
                    }
                }

                SwitchToGameRoom();
                return;
            }

            if (line.StartsWith("##JOINEDROOM|", StringComparison.Ordinal))
            {
                var parts = line.Split('|');
                if (parts.Length >= 3)
                {
                    string rname = parts[2];
                    LabelStatus.Text = $"Joined room '{rname}', waiting for game start...";
                }

                SwitchToGameRoom();
                return;
            }

            if (line.StartsWith("##EXITED|", StringComparison.Ordinal))
            {
                // Server confirmed EXIT — clear session and return to rooms list
                ClearSession();

                GameRoomView.IsVisible = false;
                RoomsView.IsVisible = true;
                LobbyView.IsVisible = false;
                ClearBoard();
                await SendAsync("##LIST|");
                return;
            }

            // -----------------------------------------------------------------
            // GAME START / SYMBOLS
            // -----------------------------------------------------------------
            if (line.StartsWith("##START|", StringComparison.Ordinal))
            {
                var payload = line.Length > 8 ? line.Substring(8) : string.Empty;
                if (payload.StartsWith("Opponent:", StringComparison.Ordinal))
                    _opponentName = payload.Substring("Opponent:".Length);
                else
                    _opponentName = payload;

                LabelPlayers.Text = $"{_playerName} vs {_opponentName}";
                return;
            }

            if (line.StartsWith("##SYMBOL|", StringComparison.Ordinal))
            {
                _mySymbol = line.Length > 9 && line.Substring(9).Trim() == "X" ? "X" : "O";

                if (_mySymbol == "X")
                    LabelStatus.Text = "You are X — you start!";
                else
                    LabelStatus.Text = "You are O — opponent starts";

                return;
            }

            if (line.StartsWith("##RESTART|", StringComparison.Ordinal))
            {
                ClearBoard();
                _isMyTurn = false;
                LabelStatus.Text = "New round started.";
                return;
            }

            // -----------------------------------------------------------------
            // GAMEPLAY (TURN / MOVE / RESULT)
            // -----------------------------------------------------------------
            if (line.StartsWith("##TURN|", StringComparison.Ordinal))
            {
                _isMyTurn = true;
                LabelStatus.Text = $"Your turn! You are '{_mySymbol}'.";
                SetButtonsEnabled(true);
                return;
            }

            if (line.StartsWith("##MOVE|", StringComparison.Ordinal))
            {
                // Support both ##MOVE|player|x|y and ##MOVE|x|y
                var p = line.Split('|');
                int x, y;
                string? mover = null;

                if (p.Length >= 4 && int.TryParse(p[2], out x) && int.TryParse(p[3], out y))
                {
                    mover = p[1];
                }
                else if (p.Length >= 3 && int.TryParse(p[1], out x) && int.TryParse(p[2], out y))
                {
                    mover = null;
                }
                else
                {
                    return;
                }

                string OppSymbol() => _mySymbol == "X" ? "O" : "X";
                string sym;

                if (!string.IsNullOrEmpty(_mySymbol))
                {
                    sym = mover != null
                        ? (mover == _playerName ? _mySymbol : OppSymbol())
                        : (_isMyTurn ? OppSymbol() : _mySymbol);
                }
                else
                {
                    sym = "?";
                }

                var btn = FindButton(x, y);
                if (btn != null)
                {
                    btn.Text = sym;
                    btn.IsEnabled = false;
                }

                _isMyTurn = false;
                SetButtonsEnabled(false);
                return;
            }

            if (line.StartsWith("##WIN|You", StringComparison.Ordinal))
            {
                _isMyTurn = false;
                SetButtonsEnabled(false);
                LabelStatus.Text = "You win!";

                if (!_opponentLeft)
                {
                    _ = AskReplayAsync();
                }
                else
                {
                    _opponentLeft = false; // Reset local flag when opponent already left
                }

                return;
            }

            if (line.StartsWith("##LOSE|", StringComparison.Ordinal))
            {
                _isMyTurn = false;
                SetButtonsEnabled(false);
                LabelStatus.Text = "You lose.";

                if (!_opponentLeft)
                {
                    _ = AskReplayAsync();
                }
                else
                {
                    _opponentLeft = false; // Reset flag
                }

                return;
            }

            if (line.StartsWith("##DRAW|", StringComparison.Ordinal))
            {
                _isMyTurn = false;
                SetButtonsEnabled(false);
                LabelStatus.Text = "Draw.";

                if (!_opponentLeft)
                {
                    _ = AskReplayAsync();
                }
                else
                {
                    _opponentLeft = false; // Reset flag
                }

                return;
            }
        }
        catch (Exception ex)
        {
            // Swallow UI exceptions, but log them for debugging
            _logger.LogError(ex, "Error processing server message: {Message}", line);
        }
    }
    #endregion

    #region UI Helpers – Views & Board
    // -------------------------------------------------------------------------
    // UI helpers: view switching and board utilities
    // -------------------------------------------------------------------------

    /// <summary>
    /// Switch UI to game room view and reset basic game state.
    /// </summary>
    private void SwitchToGameRoom()
    {
        LobbyView.IsVisible = false;
        RoomsView.IsVisible = false;
        GameRoomView.IsVisible = true;

        ClearBoard();
        SetButtonsEnabled(false);

        // Reset reconnect-related state when entering a new room
        _opponentLeft = false;
    }

    /// <summary>
    /// Reset all game buttons to default (disabled and blank).
    /// </summary>
    private void ClearBoard()
    {
        foreach (var child in GameGrid.Children)
        {
            if (child is Button b)
            {
                b.Text = " ";
                b.IsEnabled = false;
            }
        }
    }

    /// <summary>
    /// Enable or disable all empty buttons (those with text " ").
    /// </summary>
    private void SetButtonsEnabled(bool enabled)
    {
        foreach (var child in GameGrid.Children)
        {
            if (child is Button b && b.Text == " ")
                b.IsEnabled = enabled;
        }
    }

    /// <summary>
    /// Find a board button by coordinates (x = column, y = row).
    /// Buttons are named like "B{row}{col}" in XAML.
    /// </summary>
    private Button? FindButton(int x, int y)
    {
        string name = $"B{y}{x}"; // B[row][col]
        return this.FindByName<Button>(name);
    }

    /// <summary>
    /// Ask the player if they want to play another round and send REPLAY decision.
    /// </summary>
    private async Task AskReplayAsync()
    {
        bool again = await DisplayAlert("Replay", "Play again?", "Yes", "No");
        if (again)
        {
            await SendAsync("##REPLAY|YES");
            LabelStatus.Text = "Waiting for opponent's replay decision...";
        }
        else
        {
            await SendAsync("##REPLAY|NO");
            LabelStatus.Text = "You declined replay.";
        }
    }
    #endregion

    #region Event Handlers – UI Actions
    // -------------------------------------------------------------------------
    // Event handlers: UI actions that send protocol messages
    // -------------------------------------------------------------------------

    /// <summary>
    /// Handles board cell click:
    /// - Only allowed when it's player's turn
    /// - Computes board coordinates from Grid
    /// - Sends MOVE to the server
    /// </summary>
    private async void OnCellClicked(object sender, EventArgs e)
    {
        if (!_isMyTurn || _writer == null) return;
        if (sender is not Button btn) return;

        int row = Grid.GetRow(btn);
        int col = Grid.GetColumn(btn);

        // Optimistically disable local input and notify server
        _isMyTurn = false;
        SetButtonsEnabled(false);

        await SendAsync($"##MOVE|{col}|{row}");
    }

    /// <summary>
    /// Handles selection change in the rooms list.
    /// Remembers currently selected room for Join button.
    /// </summary>
    private void OnRoomSelected(object sender, SelectionChangedEventArgs e)
    {
        _selectedRoom = e.CurrentSelection.FirstOrDefault() as RoomInfo;
    }

    /// <summary>
    /// Refreshes rooms list by sending LIST request to server.
    /// </summary>
    private async void OnRefreshRoomsClicked(object sender, EventArgs e)
    {
        await SendAsync("##LIST|");
    }

    /// <summary>
    /// Creates a new room with the name typed in EntryRoomName.
    /// </summary>
    private async void OnCreateRoomClicked(object sender, EventArgs e)
    {
        string name = EntryRoomName?.Text?.Trim() ?? string.Empty;
        if (!string.IsNullOrWhiteSpace(name))
            await SendAsync($"##CREATE|{name}");
    }

    /// <summary>
    /// Joins currently selected room (if any).
    /// </summary>
    private async void OnJoinRoomClicked(object sender, EventArgs e)
    {
        if (_selectedRoom != null)
            await SendAsync($"##JOINROOM|{_selectedRoom.Id}");
    }

    /// <summary>
    /// Leaves current game room and returns to rooms list.
    /// Sends EXIT to server, clears board and reloads rooms.
    /// </summary>
    private async void OnBackToRoomsClicked(object sender, EventArgs e)
    {
        await SendAsync("##EXIT|");

        GameRoomView.IsVisible = false;
        RoomsView.IsVisible = true;
        LobbyView.IsVisible = false;

        ClearBoard();
        await SendAsync("##LIST|");
    }

    /// <summary>
    /// Back to lobby:
    /// - Sends QUIT to server
    /// - Disconnects from server
    /// - Clears session and in-memory game state
    /// - Shows lobby view again
    /// </summary>
    private async void OnBackToLobbyClicked(object sender, EventArgs e)
    {
        if (_writer != null)
            await SendAsync("##QUIT|");

        await SafeDisconnectAsync();

        // Clear session file when explicitly going back to lobby
        ClearSession();
        _opponentName = string.Empty;
        _mySymbol = string.Empty;
        _isMyTurn = false;

        GameRoomView.IsVisible = false;
        RoomsView.IsVisible = false;
        LobbyView.IsVisible = true;

        LabelLobbyStatus.Text = "Disconnected.";
        LabelLobbyStatus.IsVisible = true;
        LabelPlayers.Text = string.Empty;
        ClearBoard();
    }
    #endregion
}
