using System.Runtime.InteropServices;

namespace ScoobyReMasked;

// The connected monitors for Settings > Display > Fullscreen monitor, with the same ids, labels, order and matching as
// the game (aurora-main/lib/window.cpp query_monitors, find_monitor, get_monitors): both read Windows' display config.
static class Monitors
{
    public sealed record Monitor(string Id, string Label);

    public static List<Monitor> List()
    {
        var targets = Targets();
        var screens = Screen.AllScreens.OrderByDescending(s => s.Primary).ToList();
        var main = screens.FirstOrDefault(s => s.Primary)?.Bounds ?? default;
        var monitors = new List<Monitor>();
        foreach (var screen in screens)
        {
            if (!targets.TryGetValue(screen.DeviceName, out var target)) continue;
            var r = screen.Bounds;
            string place = "main";
            if (!screen.Primary)
            {
                string vertical = r.Bottom <= main.Top ? "above" : r.Top >= main.Bottom ? "below" : "";
                string horizontal = r.Right <= main.Left ? "left" : r.Left >= main.Right ? "right" : "";
                place = (vertical + " " + horizontal).Trim();
            }
            monitors.Add(new Monitor(target.id, $"{target.name} ({r.Width}x{r.Height}{(place.Length > 0 ? ", " + place : "")})"));
        }
        return monitors;
    }

    // What a saved id means now: that monitor, else the only connected one of its model (plugged into another port).
    public static Monitor? Find(List<Monitor> monitors, string id)
    {
        var exact = monitors.FirstOrDefault(m => m.Id == id);
        if (exact != null) return exact;
        int modelEnd = id.IndexOf('/');
        if (modelEnd < 0) return null;
        var sameModel = monitors.Where(m => m.Id.StartsWith(id[..(modelEnd + 1)], StringComparison.Ordinal)).ToList();
        return sameModel.Count == 1 ? sameModel[0] : null;
    }

    // Desktop device name (\\.\DISPLAY1) -> monitor id and name. Mirrored monitors share one desktop: the first stands for them.
    static Dictionary<string, (string id, string name)> Targets()
    {
        var targets = new Dictionary<string, (string id, string name)>();
        DisplayPath[] paths;
        uint pathCount, modeCount;
        int result;
        do
        {
            result = GetDisplayConfigBufferSizes(QdcOnlyActivePaths, out pathCount, out modeCount);
            if (result != 0) return targets;
            paths = new DisplayPath[pathCount];
            result = QueryDisplayConfig(QdcOnlyActivePaths, ref pathCount, paths, ref modeCount, new DisplayMode[modeCount], IntPtr.Zero);
        } while (result == ErrorInsufficientBuffer);
        if (result != 0) return targets;

        for (uint p = 0; p < pathCount; p++)
        {
            var source = new SourceName { Header = Header(1, Marshal.SizeOf<SourceName>(), paths[p].Source.AdapterId, paths[p].Source.Id) };
            if (GetSourceName(ref source) != 0 || targets.ContainsKey(source.GdiName)) continue;
            var target = new TargetName { Header = Header(2, Marshal.SizeOf<TargetName>(), paths[p].Target.AdapterId, paths[p].Target.Id) };
            if (GetTargetName(ref target) != 0) continue;
            string id = IdFromPath(target.DevicePath);
            if (id.Length > 0) targets[source.GdiName] = (id, target.FriendlyName.Length > 0 ? target.FriendlyName : "Monitor");
        }
        return targets;
    }

    // "\\?\DISPLAY#ACI2498#5&2d9ad897&0&UID4353#{guid}" -> "ACI2498/5&2d9ad897&0&UID4353": the model, then the instance
    // (tells identical monitors apart). No '#': GameConfig reads one as a comment.
    static string IdFromPath(string path)
    {
        var parts = path.Split('#');
        return parts.Length >= 4 ? parts[1] + "/" + parts[2] : "";
    }

    static DeviceInfoHeader Header(uint type, int size, Luid adapter, uint id) =>
        new() { Type = type, Size = (uint)size, AdapterId = adapter, Id = id };

    const uint QdcOnlyActivePaths = 2;
    const int ErrorInsufficientBuffer = 122;

    [StructLayout(LayoutKind.Sequential)] struct Luid { public uint Low; public int High; }
    [StructLayout(LayoutKind.Sequential)] struct PathSource { public Luid AdapterId; public uint Id; public uint ModeIndex; public uint Status; }
    [StructLayout(LayoutKind.Sequential)]
    struct PathTarget
    {
        public Luid AdapterId;
        public uint Id, ModeIndex, Technology, Rotation, Scaling, RefreshNumerator, RefreshDenominator, ScanLine;
        public int Available;
        public uint Status;
    }
    [StructLayout(LayoutKind.Sequential)] struct DisplayPath { public PathSource Source; public PathTarget Target; public uint Flags; }
    [StructLayout(LayoutKind.Sequential, Size = 64)] struct DisplayMode { public uint Unused; }
    [StructLayout(LayoutKind.Sequential)] struct DeviceInfoHeader { public uint Type; public uint Size; public Luid AdapterId; public uint Id; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct SourceName
    {
        public DeviceInfoHeader Header;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string GdiName;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct TargetName
    {
        public DeviceInfoHeader Header;
        public uint Flags, Technology;
        public ushort ManufacturerId, ProductId;
        public uint ConnectorInstance;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string FriendlyName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DevicePath;
    }

    [DllImport("user32.dll")] static extern int GetDisplayConfigBufferSizes(uint flags, out uint pathCount, out uint modeCount);
    [DllImport("user32.dll")] static extern int QueryDisplayConfig(uint flags, ref uint pathCount, [Out] DisplayPath[] paths, ref uint modeCount, [Out] DisplayMode[] modes, IntPtr topology);
    [DllImport("user32.dll", EntryPoint = "DisplayConfigGetDeviceInfo")] static extern int GetSourceName(ref SourceName info);
    [DllImport("user32.dll", EntryPoint = "DisplayConfigGetDeviceInfo")] static extern int GetTargetName(ref TargetName info);
}
