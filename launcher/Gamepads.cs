using System.Runtime.InteropServices;

namespace ScoobyReMasked;

// Connected gamepads, read with the game's own SDL3.dll so the launcher sees the same controller types as the game
// (button prompts, Xbox layout). SDL runs on its own thread (its device notifications need that thread's message
// loop); HIDAPI is off, so the launcher never sends anything to a controller. Paused while the game runs.
sealed class Gamepads : IDisposable
{
    public enum Kind { Other, Xbox, PlayStation, Switch, GameCube }
    public sealed record Pad(string Name, Kind Kind);

    readonly string sdlPath;
    readonly object gate = new();
    IReadOnlyList<Pad> pads = Array.Empty<Pad>();
    IReadOnlyList<ControllerSample> input = Array.Empty<ControllerSample>();
    Thread? thread;
    volatile bool stop;

    public Gamepads(string gameFolder) => sdlPath = Path.Combine(gameFolder, "SDL3.dll");

    // False when SDL3.dll is missing or failed to start (no detection; the launcher works without it).
    public bool Available { get; private set; } = true;
    public string? Error { get; private set; }

    public IReadOnlyList<Pad> Connected { get { lock (gate) return pads; } }
    public IReadOnlyList<ControllerSample> Input { get { lock (gate) return input; } }

    public void Start()
    {
        if (thread != null || !Available) return;
        stop = false;
        thread = new Thread(Run) { IsBackground = true, Name = "Gamepads" };
        thread.Start();
    }

    // Lets the game have the controllers to itself.
    public void Pause()
    {
        stop = true;
        thread?.Join();
        thread = null;
        lock (gate) { pads = Array.Empty<Pad>(); input = Array.Empty<ControllerSample>(); }
    }

    public void Dispose() => Pause();

    void Run()
    {
        IntPtr lib = IntPtr.Zero;
        try
        {
            if (!File.Exists(sdlPath) || !NativeLibrary.TryLoad(sdlPath, out lib))
            { Available = false; Error = "SDL3.dll is missing or could not be loaded."; return; }
            var api = new Api(lib);
            api.SetHint("SDL_JOYSTICK_HIDAPI", "0");
            api.SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
            if (!api.Init(0x2000 /* SDL_INIT_GAMEPAD */))
            { Available = false; Error = api.GetError(); return; }
            var opened = new Dictionary<uint, IntPtr>();
            try
            {
                while (!stop)
                {
                    api.PumpEvents();
                    api.FlushEvents(0, 0xFFFF);
                    var list = new List<Pad>();
                    var samples = new List<ControllerSample>();
                    var present = new HashSet<uint>();
                    IntPtr ids = api.GetGamepads(out int count);
                    string? inputError = ids == IntPtr.Zero ? api.GetError() : null;
                    if (ids != IntPtr.Zero)
                    {
                        for (int i = 0; i < count; i++)
                        {
                            uint id = (uint)Marshal.ReadInt32(ids, i * 4);
                            present.Add(id);
                            string name = Marshal.PtrToStringUTF8(api.GetGamepadNameForID(id)) ?? "Controller";
                            list.Add(new Pad(name, KindOf(api.GetGamepadTypeForID(id))));
                            if (!opened.TryGetValue(id, out var pad))
                            {
                                pad = api.OpenGamepad(id);
                                if (pad != IntPtr.Zero) opened.Add(id, pad);
                                else inputError = "Couldn't open " + name + ": " + api.GetError();
                            }
                            if (pad != IntPtr.Zero)
                            {
                                uint buttons = 0;
                                foreach (int b in new[] { 0, 1, 9, 10, 11, 12, 13, 14 })
                                    if (api.GetGamepadButton(pad, b)) buttons |= 1u << b;
                                samples.Add(new ControllerSample(id, ControllerInput.FromSdl(buttons,
                                    api.GetGamepadAxis(pad, 0), api.GetGamepadAxis(pad, 1), api.GetGamepadAxis(pad, 3))));
                            }
                        }
                        api.Free(ids);
                    }
                    foreach (var id in opened.Keys.Where(id => !present.Contains(id)).ToArray())
                    { api.CloseGamepad(opened[id]); opened.Remove(id); }
                    lock (gate) { pads = list; input = samples; Error = inputError; }
                    Thread.Sleep(16);
                }
            }
            finally
            {
                foreach (var pad in opened.Values) api.CloseGamepad(pad);
                api.Quit();
            }
        }
        catch (Exception e) { Available = false; Error = e.Message; System.Diagnostics.Trace.TraceError("Controller input: {0}", e); }
        finally
        {
            // SDL stays loaded: unloading it while its threads wind down is not worth the risk.
            lock (gate) { pads = Array.Empty<Pad>(); input = Array.Empty<ControllerSample>(); }
        }
    }

    // SDL_GamepadType, as the game sorts it (hle/input/pad.cpp).
    internal static Kind KindOf(int type) => type switch
    {
        2 or 3 => Kind.Xbox,                 // XBOX360, XBOXONE (also third-party pads in XInput mode)
        4 or 5 or 6 => Kind.PlayStation,     // PS3, PS4, PS5
        7 or 8 or 9 or 10 => Kind.Switch,    // Switch Pro, Joy-Cons
        11 => Kind.GameCube,                 // GameCube adapter
        _ => Kind.Other,
    };

    public static string Describe(Kind kind) => kind switch
    {
        Kind.Xbox => "Xbox",
        Kind.PlayStation => "PlayStation",
        Kind.Switch => "Nintendo Switch",
        Kind.GameCube => "GameCube",
        _ => "other",
    };

    // The few SDL3 functions used, bound from the game's DLL.
    sealed class Api
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] [return: MarshalAs(UnmanagedType.U1)] delegate bool InitFn(uint flags);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] [return: MarshalAs(UnmanagedType.U1)]
        delegate bool SetHintFn([MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.LPUTF8Str)] string value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void VoidFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr GetGamepadsFn(out int count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr NameFn(uint id);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int TypeFn(uint id);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FreeFn(IntPtr p);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr OpenFn(uint id);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate short AxisFn(IntPtr pad, int axis);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] [return: MarshalAs(UnmanagedType.U1)]
        delegate bool ButtonFn(IntPtr pad, int button);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr ErrorFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FlushFn(uint min, uint max);

        public readonly Func<uint, bool> Init;
        public readonly Func<string, string, bool> SetHint;
        public readonly Action PumpEvents, Quit;
        readonly GetGamepadsFn getGamepads;
        public readonly Func<uint, IntPtr> GetGamepadNameForID;
        public readonly Func<uint, int> GetGamepadTypeForID;
        public readonly Action<IntPtr> Free;
        public readonly Func<uint, IntPtr> OpenGamepad;
        public readonly Action<IntPtr> CloseGamepad;
        public readonly Func<IntPtr, int, short> GetGamepadAxis;
        public readonly Func<IntPtr, int, bool> GetGamepadButton;
        public readonly Func<string> GetError;
        public readonly Action<uint, uint> FlushEvents;

        public Api(IntPtr lib)
        {
            T F<T>(string name) where T : Delegate => Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(lib, name));
            var init = F<InitFn>("SDL_Init"); Init = f => init(f);
            var hint = F<SetHintFn>("SDL_SetHint"); SetHint = (n, v) => hint(n, v);
            var pump = F<VoidFn>("SDL_PumpEvents"); PumpEvents = () => pump();
            var quit = F<VoidFn>("SDL_Quit"); Quit = () => quit();
            getGamepads = F<GetGamepadsFn>("SDL_GetGamepads");
            var name = F<NameFn>("SDL_GetGamepadNameForID"); GetGamepadNameForID = id => name(id);
            var type = F<TypeFn>("SDL_GetGamepadTypeForID"); GetGamepadTypeForID = id => type(id);
            var free = F<FreeFn>("SDL_free"); Free = p => free(p);
            var open = F<OpenFn>("SDL_OpenGamepad"); OpenGamepad = id => open(id);
            var close = F<FreeFn>("SDL_CloseGamepad"); CloseGamepad = p => close(p);
            var axis = F<AxisFn>("SDL_GetGamepadAxis"); GetGamepadAxis = (p, a) => axis(p, a);
            var button = F<ButtonFn>("SDL_GetGamepadButton"); GetGamepadButton = (p, b) => button(p, b);
            var error = F<ErrorFn>("SDL_GetError"); GetError = () => Marshal.PtrToStringUTF8(error()) ?? "SDL initialization failed.";
            var flush = F<FlushFn>("SDL_FlushEvents"); FlushEvents = (min, max) => flush(min, max);
        }

        public IntPtr GetGamepads(out int count) => getGamepads(out count);
    }
}
