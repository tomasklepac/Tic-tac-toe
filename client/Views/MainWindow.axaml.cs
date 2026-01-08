using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using Avalonia.Media;
using PiskvorkyClientAvalonia.Models;

namespace PiskvorkyClientAvalonia.Views;

public partial class MainWindow : Window
{
    private readonly string _logPath = Path.Combine(AppContext.BaseDirectory, "client.log");
    private TcpClient? _client;
    private StreamReader? _reader;
    private StreamWriter? _writer;
    private CancellationTokenSource? _cts;

    private string _playerName = string.Empty;
    private string _sessionId = string.Empty;
    private string _opponentName = string.Empty;
    private string _serverIp = string.Empty;
    private int _serverPort;
    private bool _isMyTurn;
    private string _mySymbol = string.Empty;
    private bool _allowAutoReconnect = true;
    private bool _opponentLeft = false;

    public ObservableCollection<RoomInfo> Rooms { get; } = new();
    private RoomInfo? _selectedRoom;

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;
        InitLog();
        Log("Application start");
    }

    private async Task SendAsync(string msg)
    {
        if (_writer == null) return;
        try
        {
            await _writer.WriteLineAsync(msg);
            await _writer.FlushAsync();
            Log($"TX: {msg}");
        }
        catch
        {
            // swallow; reconnect loop will handle
        }
    }

    private async void OnConnectClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        string ip = EntryIP.Text ?? string.Empty;
        string name = EntryName.Text ?? string.Empty;
        if (!int.TryParse(EntryPort.Text, out int port))
        {
            await ShowMessage("Error", "Invalid port.");
            return;
        }

        if (string.IsNullOrWhiteSpace(ip) || string.IsNullOrWhiteSpace(name))
        {
            await ShowMessage("Error", "Please enter IP, port and username.");
            return;
        }

        try
        {
            _client = new TcpClient();
            await _client.ConnectAsync(ip, port);
            _serverIp = ip;
            _serverPort = port;
            _allowAutoReconnect = true;
            Log($"Connected to {ip}:{port} as {name}");

            var stream = _client.GetStream();
            _reader = new StreamReader(stream, Encoding.UTF8);
            _writer = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = true };

            _playerName = name;
            await SendAsync($"##JOIN|{name}");
            await SendAsync("##LIST|");

            LobbyView.IsVisible = false;
            RoomsView.IsVisible = true;
            GameRoomView.IsVisible = false;

            _cts = new CancellationTokenSource();
            _ = Task.Run(() => ListenToServerAsync(_cts.Token));
        }
        catch (Exception ex)
        {
            Log($"Connect failed: {ex.Message}");
            await ShowMessage("Connection Failed", ex.Message);
            await SafeDisconnectAsync();
        }
    }

    private async Task ListenToServerAsync(CancellationToken token)
    {
        try
        {
            // Watchdog timeout - slightly more than 2x PING interval (5s)
            // If server works, we get PING every 5s. If silence > 12s, assume dead.
            TimeSpan receiveTimeout = TimeSpan.FromMilliseconds(12000);

            while (!token.IsCancellationRequested)
            {
                string? line;
                try
                {
                    var readTask = _reader!.ReadLineAsync();
                    
                    // Wait for read OR timeout OR cancellation
                    // Note: Task.WaitAsync is .NET 6+, fallback for compat if needed, 
                    // but Avalonia implies modern .NET. simpler approach using WhenAny for robustness:
                    var completed = await Task.WhenAny(readTask, Task.Delay(receiveTimeout, token));

                    if (completed != readTask)
                    {
                        // Timeout happened (or token cancelled Task.Delay)
                        if (token.IsCancellationRequested) break;
                        
                        Log("Watchdog timeout: Server silent for > 12s. Assuming disconnect.");
                        break; // Trigger finally -> SafeDisconnect
                    }

                    // Read completed
                    line = await readTask;
                }
                catch
                {
                    break;
                }

                if (line == null) break;

                var captured = line;
                await Dispatcher.UIThread.InvokeAsync(() => HandleServerMessage(captured));
            }
        }
        catch (OperationCanceledException)
        {
            // Normal cancellation
        }
        finally
        {
            await SafeDisconnectAsync();

            await Dispatcher.UIThread.InvokeAsync(() =>
            {
                // Immediate feedback: switch to Lobby (Login screen)
                GameRoomView.IsVisible = false;
                RoomsView.IsVisible = false;
                LobbyView.IsVisible = true;
                
                LabelLobbyStatus.Text = "Connection lost. Reconnecting...";
                LabelLobbyStatus.IsVisible = true;
                
                // Clear in-game status just in case
                LabelStatus.Text = "Reconnecting..."; 
            });

            bool reconnected = await AttemptReconnectLoopAsync();
            if (!reconnected)
            {
                await Dispatcher.UIThread.InvokeAsync(async () =>
                {
                    LabelStatus.Text = "Connection lost.";
                    LabelLobbyStatus.Text = "Connection lost.";
                    LabelLobbyStatus.IsVisible = true;
                    
                    // Views are already switched above, but ensure:
                    GameRoomView.IsVisible = false;
                    RoomsView.IsVisible = false;
                    LobbyView.IsVisible = true;

                    await ShowMessage("Connection Lost", "Unable to reconnect to the server.");
                });
            }
        }
    }

    private async Task SafeDisconnectAsync()
    {
        try
        {
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
                try { _reader.Dispose(); } catch { }
                _reader = null;
            }

            if (_client != null)
            {
                try { _client.Close(); _client.Dispose(); } catch { }
                _client = null;
            }
        }
        catch
        {
            // ignore
        }
    }

    private async Task<bool> AttemptReconnectLoopAsync()
    {
        if (!_allowAutoReconnect) return false;
        if (string.IsNullOrEmpty(_serverIp) || _serverPort == 0 || string.IsNullOrEmpty(_playerName) || string.IsNullOrEmpty(_sessionId))
            return false;

        await Dispatcher.UIThread.InvokeAsync(() =>
        {
            LabelStatus.Text = "Reconnecting...";
            LabelLobbyStatus.Text = "Reconnecting...";
            LabelLobbyStatus.IsVisible = true;
        });

        int delayMs = 1000;
        const int maxAttempts = 10;
        for (int attempt = 1; attempt <= maxAttempts; attempt++)
        {
            try
            {
                _client = new TcpClient();
                await _client.ConnectAsync(_serverIp, _serverPort);

                var stream = _client.GetStream();
                _reader = new StreamReader(stream, Encoding.UTF8);
                _writer = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = true };

                await SendAsync($"##RECONNECT|{_playerName}|{_sessionId}");

                _cts = new CancellationTokenSource();
                _ = Task.Run(() => ListenToServerAsync(_cts.Token));

                await Dispatcher.UIThread.InvokeAsync(() =>
                {
                    LabelStatus.Text = "Reconnected.";
                    LabelLobbyStatus.Text = "Reconnected.";
                    LabelLobbyStatus.IsVisible = true;
                });
                return true;
            }
            catch
            {
                await Dispatcher.UIThread.InvokeAsync(() =>
                {
                    LabelStatus.Text = $"Reconnecting... (attempt {attempt}/{maxAttempts})";
                    LabelLobbyStatus.Text = LabelStatus.Text;
                    LabelLobbyStatus.IsVisible = true;
                });
                await Task.Delay(delayMs);
                delayMs = Math.Min(delayMs * 2, 8000);
            }
        }
        return false;
    }

    private async Task HandleServerMessage(string line)
    {
        try
        {
            Log($"RX: {line}");
            if (line.StartsWith("##PING|", StringComparison.Ordinal))
            {
                await SendAsync("##PONG|");
                return;
            }

            if (line.StartsWith("##INFO|", StringComparison.Ordinal))
            {
                string msg = line.Length > 7 ? line[7..] : string.Empty;
                LabelStatus.Text = msg;
                LabelLobbyStatus.Text = msg;
                if (msg.Contains("Opponent left", StringComparison.OrdinalIgnoreCase) ||
                    msg.Contains("Opponent disconnected", StringComparison.OrdinalIgnoreCase) ||
                    msg.Contains("did not return", StringComparison.OrdinalIgnoreCase) ||
                    msg.Contains("declined replay", StringComparison.OrdinalIgnoreCase))
                {
                    _opponentLeft = true;
                    ClearBoard();
                    SetButtonsEnabled(false);
                }
                return;
            }

            if (line.StartsWith("##ERROR|", StringComparison.Ordinal))
            {
                string msg = line.Length > 8 ? line[8..] : string.Empty;
                LabelStatus.Text = msg;
                LabelLobbyStatus.Text = msg;
                await ShowMessage("Server error", msg);
                return;
            }

            if (line.StartsWith("##HELLO|", StringComparison.Ordinal))
            {
                LabelLobbyStatus.Text = line.Length > 8 ? line[8..] : string.Empty;
                LabelLobbyStatus.IsVisible = true;
                return;
            }

            if (line.StartsWith("##JOINED|", StringComparison.Ordinal))
            {
                _playerName = line.Length > 9 ? line[9..] : string.Empty;
                return;
            }

            if (line.StartsWith("##SESSION|", StringComparison.Ordinal))
            {
                _sessionId = line.Length > 10 ? line[10..] : string.Empty;
                return;
            }

            if (line.StartsWith("##RECONNECTED|", StringComparison.Ordinal))
            {
                LabelStatus.Text = "Reconnected to game.";
                SwitchToGameRoom();
                return;
            }

            if (line.StartsWith("##ROOMS|", StringComparison.Ordinal))
            {
                var parts = line.Split('|');
                Rooms.Clear();
                for (int i = 2; i + 3 < parts.Length; i += 4)
                {
                    if (!int.TryParse(parts[i], out var rid)) continue;
                    string rname = parts[i + 1];
                    string state = parts[i + 2];
                    string occ = parts[i + 3];
                    string display = state == "PLAYING" ? $"{rname} (playing)" : rname;
                    Rooms.Add(new RoomInfo { Id = rid, Name = display, Capacity = occ });
                }
                if (!GameRoomView.IsVisible)
                {
                    RoomsView.IsVisible = true;
                    LobbyView.IsVisible = false;
                }
                return;
            }

            if (line.StartsWith("##CREATED|", StringComparison.Ordinal))
            {
                SwitchToGameRoom();
                LabelStatus.Text = "Room created, waiting for opponent...";
                return;
            }

            if (line.StartsWith("##JOINEDROOM|", StringComparison.Ordinal))
            {
                SwitchToGameRoom();
                LabelStatus.Text = "Joined room, waiting for game start...";
                return;
            }

            if (line.StartsWith("##EXITED|", StringComparison.Ordinal))
            {
                GameRoomView.IsVisible = false;
                RoomsView.IsVisible = true;
                LobbyView.IsVisible = false;
                ClearBoard();
                await SendAsync("##LIST|");
                return;
            }

            if (line.StartsWith("##START|", StringComparison.Ordinal))
            {
                var payload = line.Length > 8 ? line[8..] : string.Empty;
                if (payload.StartsWith("Opponent:", StringComparison.Ordinal))
                    _opponentName = payload.Substring("Opponent:".Length);
                else
                    _opponentName = payload;
                UpdatePlayersLabel();
                return;
            }

            if (line.StartsWith("##SYMBOL|", StringComparison.Ordinal))
            {
                _mySymbol = line.Length > 9 && line[9..].Trim() == "X" ? "X" : "O";
                LabelStatus.Text = _mySymbol == "X" ? "You are X - you start!" : "You are O - opponent starts";
                UpdatePlayersLabel();
                return;
            }

            if (line.StartsWith("##RESTART|", StringComparison.Ordinal))
            {
                ClearBoard();
                _isMyTurn = false;
                LabelStatus.Text = "New round started.";
                return;
            }

            if (line.StartsWith("##CLEAR|", StringComparison.Ordinal))
            {
                ClearBoard();
                _isMyTurn = false;
                LabelStatus.Text = "Board reset. Waiting for opponent...";
                SetButtonsEnabled(false);
                return;
            }

            if (line.StartsWith("##TURN|", StringComparison.Ordinal))
            {
                _isMyTurn = true;
                LabelStatus.Text = $"Your turn! You are '{_mySymbol}'.";
                SetButtonsEnabled(true);
                return;
            }

            if (line.StartsWith("##MOVE|", StringComparison.Ordinal))
            {
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
                else return;

                string OppSymbol() => _mySymbol == "X" ? "O" : "X";
                string sym;
                if (!string.IsNullOrEmpty(_mySymbol))
                {
                    sym = mover != null ? (mover == _playerName ? _mySymbol : OppSymbol())
                        : (_isMyTurn ? OppSymbol() : _mySymbol);
                }
                else sym = "?";

                var btn = FindButton(x, y);
                if (btn != null)
                {
                    btn.Content = sym;
                    btn.IsEnabled = false;
                }

                _isMyTurn = false;
                SetButtonsEnabled(false);
                LabelStatus.Text = "Waiting for opponent...";
                return;
            }

            if (line.StartsWith("##WIN|You", StringComparison.Ordinal))
            {
                _isMyTurn = false;
                SetButtonsEnabled(false);
                _ = ShowResultThenReplayAsync("You win!");
                return;
            }

            if (line.StartsWith("##LOSE|", StringComparison.Ordinal))
            {
                _isMyTurn = false;
                SetButtonsEnabled(false);
                _ = ShowResultThenReplayAsync("You lose.");
                return;
            }

            if (line.StartsWith("##DRAW|", StringComparison.Ordinal))
            {
                _isMyTurn = false;
                SetButtonsEnabled(false);
                _ = ShowResultThenReplayAsync("Draw.");
                return;
            }
        }
        catch
        {
            // ignore parsing errors
        }

        // Protocol violation
        Log($"Protocol violation: Unknown or unhandled message: {line}");
        
        // Disable auto-reconnect to prevent loop
        _allowAutoReconnect = false;

        // Disconnect IMMEDIATELY
        await SafeDisconnectAsync();

        // Notify user
        await ShowMessage("Protocol Error", $"Received invalid message from server:\n{line}\n\nDisconnected.");
    }

    private void SwitchToGameRoom()
    {
        LobbyView.IsVisible = false;
        RoomsView.IsVisible = false;
        GameRoomView.IsVisible = true;
        ClearBoard();
        SetButtonsEnabled(false);
        _opponentLeft = false;
    }

    private void UpdatePlayersLabel()
    {
        string mySym = string.IsNullOrEmpty(_mySymbol) ? "" : $" ({_mySymbol})";
        string oppSym = string.IsNullOrEmpty(_mySymbol) ? "" : (_mySymbol == "X" ? " (O)" : " (X)");
        LabelPlayers.Text = $"{_playerName}{mySym} vs {_opponentName}{oppSym}";
    }

    private void ClearBoard()
    {
        foreach (var child in GameGrid.Children.OfType<Button>())
        {
            child.Content = " ";
            child.IsEnabled = false;
        }
    }

    private void SetButtonsEnabled(bool enabled)
    {
        foreach (var child in GameGrid.Children.OfType<Button>())
        {
            if ((child.Content as string) == " " || string.IsNullOrEmpty(child.Content as string))
                child.IsEnabled = enabled;
        }
    }

    private Button? FindButton(int x, int y)
    {
        string name = $"B{y}{x}";
        return this.FindControl<Button>(name);
    }

    private async Task ShowResultThenReplayAsync(string message)
    {
        LabelStatus.Text = message;
        await ShowMessage("Game Over", message);
        if (_opponentLeft)
        {
            _opponentLeft = false;
            return;
        }
        bool again = await ShowYesNo("Replay", "Play again?");
        if (again)
        {
            await SendAsync("##REPLAY|YES");
            Log("Replay vote: YES");
            LabelStatus.Text = "Waiting for opponent's replay decision...";
        }
        else
        {
            await SendAsync("##REPLAY|NO");
            Log("Replay vote: NO");
            LabelStatus.Text = "You declined replay.";
        }
    }

    private async Task ShowMessage(string title, string message)
    {
        var tcs = new TaskCompletionSource();
        var ok = new Button
        {
            Content = "OK",
            Width = 120,
            HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
            IsDefault = true
        };

        var dialog = new Window
        {
            Title = title,
            SizeToContent = SizeToContent.WidthAndHeight,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            CanResize = false,
            Background = new SolidColorBrush(Color.Parse("#0B0B0F")),
            Content = new Border
            {
                Padding = new Thickness(18),
                Background = new SolidColorBrush(Color.Parse("#111827")),
                CornerRadius = new CornerRadius(12),
                Child = new StackPanel
                {
                    Spacing = 14,
                    Children =
                    {
                        new TextBlock
                        {
                            Text = message,
                            TextWrapping = Avalonia.Media.TextWrapping.Wrap,
                            Foreground = Brushes.White
                        },
                        ok
                    }
                }
            }
        };

        ok.Click += (_, __) => { dialog.Close(); tcs.TrySetResult(); };
        dialog.Closed += (_, __) => tcs.TrySetResult();
        dialog.Show(this);
        await tcs.Task;
    }

    private async Task<bool> ShowYesNo(string title, string message)
    {
        var tcs = new TaskCompletionSource<bool>();
        var yes = new Button { Content = "Yes", Width = 120, IsDefault = true };
        var no = new Button { Content = "No", Width = 120, IsCancel = true };
        yes.Click += (_, __) => { tcs.TrySetResult(true); };
        no.Click += (_, __) => { tcs.TrySetResult(false); };

        var dialog = new Window
        {
            Title = title,
            SizeToContent = SizeToContent.WidthAndHeight,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            CanResize = false,
            Background = new SolidColorBrush(Color.Parse("#0B0B0F")),
            Content = new Border
            {
                Padding = new Thickness(18),
                Background = new SolidColorBrush(Color.Parse("#111827")),
                CornerRadius = new CornerRadius(12),
                Child = new StackPanel
                {
                    Spacing = 14,
                    Children =
                    {
                        new TextBlock
                        {
                            Text = message,
                            TextWrapping = Avalonia.Media.TextWrapping.Wrap,
                            Foreground = Brushes.White
                        },
                        new StackPanel
                        {
                            Orientation = Avalonia.Layout.Orientation.Horizontal,
                            HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
                            Spacing = 12,
                            Children = { yes, no }
                        }
                    }
                }
            }
        };

        dialog.Closed += (_, __) =>
        {
            if (!tcs.Task.IsCompleted)
                tcs.TrySetResult(false);
        };

        yes.Click += (_, __) => dialog.Close();
        no.Click += (_, __) => dialog.Close();

        dialog.Show(this);
        return await tcs.Task;
    }

    private async void OnCellClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (!_isMyTurn || _writer == null) return;
        if (sender is not Button btn) return;
        var name = btn.Name ?? string.Empty;
        if (name.Length < 3) return;
        int row = int.Parse(name[1].ToString());
        int col = int.Parse(name[2].ToString());
        _isMyTurn = false;
        SetButtonsEnabled(false);
        await SendAsync($"##MOVE|{col}|{row}");
    }

    private void OnRoomSelected(object? sender, SelectionChangedEventArgs e)
    {
        _selectedRoom = RoomsList.SelectedItem as RoomInfo;
    }

    private async void OnRefreshRoomsClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        await SendAsync("##LIST|");
    }

    private async void OnCreateRoomClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        string name = EntryRoomName.Text?.Trim() ?? string.Empty;
        if (!string.IsNullOrWhiteSpace(name))
            await SendAsync($"##CREATE|{name}");
    }

    private async void OnJoinRoomClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (_selectedRoom != null)
            await SendAsync($"##JOINROOM|{_selectedRoom.Id}");
    }

    private async void OnBackToRoomsClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        await SendAsync("##EXIT|");
        GameRoomView.IsVisible = false;
        RoomsView.IsVisible = true;
        LobbyView.IsVisible = false;
        ClearBoard();
        await SendAsync("##LIST|");
    }

    private async void OnBackToLobbyClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        _allowAutoReconnect = false;
        await SendAsync("##QUIT|");
        await SafeDisconnectAsync();

        GameRoomView.IsVisible = false;
        RoomsView.IsVisible = false;
        LobbyView.IsVisible = true;
        LabelLobbyStatus.Text = "Disconnected.";
        LabelLobbyStatus.IsVisible = true;
        LabelPlayers.Text = string.Empty;
        ClearBoard();
    }

    // ------------------------------------------------------------
    // Logging helpers
    // ------------------------------------------------------------
    private void InitLog()
    {
        try
        {
            File.WriteAllText(_logPath, string.Empty);
        }
        catch { }
    }

    private void Log(string message)
    {
        try
        {
            File.AppendAllText(_logPath, $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {message}{Environment.NewLine}");
        }
        catch { }
    }
}
