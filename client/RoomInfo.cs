using Microsoft.Maui.Controls;

namespace PiskvorkyClientGUI;

/// <summary>
/// Represents a game room displayed in the rooms list.
/// Includes properties for room details and selection state.
/// </summary>
public class RoomInfo : BindableObject
{
    /// <summary>Unique room identifier assigned by the server.</summary>
    public int Id { get; set; }

    /// <summary>Display name of the room (may include state suffix like "(playing)").</summary>
    public string Name { get; set; } = string.Empty;

    /// <summary>Occupancy string, e.g., "1/2".</summary>
    public string Capacity { get; set; } = string.Empty;

    private bool _isSelected;

    /// <summary>
    /// Indicates whether the room is selected in the UI.
    /// OnPropertyChanged is invoked so bound UI elements update automatically.
    /// </summary>
    public bool IsSelected
    {
        get => _isSelected;
        set
        {
            _isSelected = value;
            OnPropertyChanged();
        }
    }

    /// <summary>
    /// Returns a string representation of the room.
    /// </summary>
    public override string ToString()
    {
        return $"{Name} ({Capacity})";
    }
}
