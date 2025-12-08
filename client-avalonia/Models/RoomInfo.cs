using System.ComponentModel;

namespace PiskvorkyClientAvalonia.Models;

public class RoomInfo : INotifyPropertyChanged
{
    public int Id { get; set; }
    public string Name { get; set; } = string.Empty;
    public string Capacity { get; set; } = string.Empty;

    public event PropertyChangedEventHandler? PropertyChanged;

    protected void OnPropertyChanged(string propertyName) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    public override string ToString() => $"{Name} ({Capacity})";
}
