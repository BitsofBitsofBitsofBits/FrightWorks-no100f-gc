using System.Diagnostics;
using static ScoobyReMasked.Theme;

namespace ScoobyReMasked;

// The launcher window: the haunted mansion (MansionScene) whose signs are the menu. Play starts the game; Settings,
// Mods and Extras open a board over the left of the house. Everything it changes goes to data\UserData\Config.toml,
// the keys the game's F10 menu uses. No game data or character art is shipped.
sealed class MainForm : Form
{
    internal const string GameExe = "FrightWorks Game.exe", AppTitle = "FrightWorks: Night of 100 Frights";
    const string ProjectUrl = "https://github.com/Loneranger419/FrightWorks-no100f-gc";

    readonly string root, play, userData, configPath, modsDir, discDir;
    readonly string? shotDir;
    readonly MansionScene scene;
    readonly Dictionary<string, Board> boards = new();
    Label discStatus = null!, dumpHelp = null!;
    PlankButton chooseDisc = null!;
    ThemedBar progress = null!, ffmpegBar = null!;
    Label ffmpegStatus = null!;
    PlankButton ffmpegButton = null!;
    CancellationTokenSource? ffmpegCancel;  // an ffmpeg download is running
    Action<bool>? showControlsTab;
    readonly Gamepads gamepads;
    Label padStatus = null!;
    ThemedCombo layoutBox = null!;
    readonly List<(PadMapping.Row Row, Label Label, ThemedCombo Box)> padRows = new();
    bool askedLayout;

    public MainForm(string? shotDir = null)
    {
        this.shotDir = shotDir;
        K = DeviceDpi / 96f;
        (root, play) = FindGame();
        userData = Path.Combine(play, "UserData");
        configPath = Path.Combine(userData, "Config.toml");
        modsDir = Path.Combine(userData, "mods");
        gamepads = new Gamepads(play);
        // Release: the disc lives in data\Disc next to the game. Dev tree (Play\): the shared Disc\ at the root.
        discDir = IsRelease ? Path.Combine(play, "Disc", "DATA") : Path.Combine(root, "Disc", "DATA");

        Text = AppTitle;
        try { Icon = Icon.ExtractAssociatedIcon(Environment.ProcessPath!); } catch { }
        var area = Screen.PrimaryScreen?.WorkingArea.Size ?? new Size(S(1180), S(830));
        ClientSize = new Size(Math.Min(S(1180), area.Width - 40), Math.Min(S(830), area.Height - 60));
        MinimumSize = new Size(Math.Min(S(960), area.Width), Math.Min(S(680), area.Height));
        BackColor = BoardBottom;
        Font = Body;
        StartPosition = FormStartPosition.CenterScreen;
        KeyPreview = true;

        scene = new MansionScene(shotDir == null && SystemInformation.UIEffectsEnabled, shotDir == null ? Environment.TickCount : 1)
        {
            Dock = DockStyle.Fill,
        };
        scene.SignClicked += OnSign;
        scene.Resize += (_, _) => PlaceBoards();
        Controls.Add(scene);

        AddBoard("settings", "Settings", BuildSettings);
        AddBoard("mods", "Mods", BuildMods);
        AddBoard("extras", "Extras", BuildExtras);
        PlaceBoards();
        RefreshDisc();

        KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Escape && boards.Values.Any(b => b.Visible)) { CloseBoards(); e.Handled = true; }
        };
        // Connected controllers: shown on Settings > Controls; an Xbox pad asks for the layout once.
        var padTimer = new System.Windows.Forms.Timer { Interval = 1000 };
        padTimer.Tick += (_, _) => RefreshGamepads();
        FormClosed += (_, _) => { padTimer.Stop(); gamepads.Dispose(); ffmpegCancel?.Cancel(); };
        Shown += (_, _) =>
        {
            if (shotDir != null) { TakeShots(shotDir); return; }
            gamepads.Start();
            padTimer.Start();
        };
    }

    // The game folder: data\ beside the launcher (release), or Play\ above launcher\publish (dev tree).
    internal static (string root, string game) FindGame()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
            foreach (var sub in new[] { "data", "Play" })
                if (File.Exists(Path.Combine(d.FullName, sub, GameExe))) return (d.FullName, Path.Combine(d.FullName, sub));
        return (AppContext.BaseDirectory, Path.Combine(AppContext.BaseDirectory, "data"));
    }

    bool IsRelease => Path.GetFileName(play) == "data";

    // Where F2 in the game saves (beside the launcher in a release, Play\Screenshots in the dev tree).
    string ScreenshotsDir => IsRelease ? Path.Combine(root, "Screenshots") : Path.Combine(play, "Screenshots");
    string VideosDir => IsRelease ? Path.Combine(root, "Videos") : Path.Combine(play, "Videos");

    // Headless visual check (--shots <dir>): the main screen, a lightning flash and every board, then exit.
    void TakeShots(string dir)
    {
        Directory.CreateDirectory(dir);
        void Shot(string name)
        {
            Application.DoEvents();
            using var bmp = new Bitmap(Width, Height);
            DrawToBitmap(bmp, new Rectangle(Point.Empty, Size));
            bmp.Save(Path.Combine(dir, name + ".png"));
        }
        scene.Showcase(false);
        Shot("Main");
        scene.Showcase(true);
        Shot("Lightning");
        scene.Showcase(false);
        foreach (var id in new[] { "settings", "mods", "extras" })
        {
            ShowBoard(id);
            Shot(char.ToUpperInvariant(id[0]) + id[1..]);
        }
        ShowBoard("settings");
        showControlsTab?.Invoke(true);
        Shot("Settings-Controls");
        Close();
    }

    // ----- Signs and boards -----
    void OnSign(string id)
    {
        switch (id)
        {
            case "play":
                if (CanPlay()) Launch();
                else ShowBoard("extras");
                break;
            case "exit":
                Close();
                break;
            default:
                if (boards.TryGetValue(id, out var b) && b.Visible) CloseBoards();
                else ShowBoard(id);
                break;
        }
    }

    void AddBoard(string id, string title, Action<FlowLayoutPanel> build)
    {
        var board = new Board(title, CloseBoards);
        var flow = new FlowLayoutPanel
        {
            AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, FlowDirection = FlowDirection.TopDown,
            WrapContents = false, BackColor = Color.Transparent, Margin = Padding.Empty,
        };
        build(flow);
        board.Body.Controls.Add(flow);
        boards[id] = board;
        scene.Controls.Add(board);
    }

    void PlaceBoards()
    {
        int left = S(14), top = S(14);
        int right = (int)scene.SignsLeft - S(18), bottom = scene.Height - S(44);
        var r = new Rectangle(left, top, Math.Max(S(300), right - left), Math.Max(S(300), bottom - top));
        foreach (var b in boards.Values) b.Bounds = r;
    }

    void ShowBoard(string id)
    {
        foreach (var (key, b) in boards) b.Visible = key == id;
        boards[id].BringToFront();
        scene.ActiveSign = id;
        RefreshDisc();
    }

    void CloseBoards()
    {
        foreach (var b in boards.Values) b.Visible = false;
        scene.ActiveSign = null;
        scene.Focus();
    }

    static FlowLayoutPanel Row() => new() { AutoSize = true, Margin = Padding.Empty, BackColor = Color.Transparent, WrapContents = true };

    static TableLayoutPanel Table() => new()
    {
        AutoSize = true, ColumnCount = 2, BackColor = Color.Transparent, Margin = S(0, 2, 0, 6),
    };

    void OpenFolder(string path)
    {
        try { Process.Start("explorer.exe", Directory.CreateDirectory(path).FullName); }
        catch (Exception e) { MessageBox.Show(this, "Couldn't open " + path + ": " + e.Message, AppTitle); }
    }

    // ----- Settings: Display and Controls -----
    void BuildSettings(FlowLayoutPanel body)
    {
        var tabs = Row();
        var display = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Color.Transparent, Margin = Padding.Empty };
        var controls = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Color.Transparent, Margin = Padding.Empty, Visible = false };
        PlankButton displayTab = null!, controlsTab = null!;
        void Select(bool showControls)
        {
            display.Visible = !showControls;
            controls.Visible = showControls;
            displayTab.Selected = !showControls;
            controlsTab.Selected = showControls;
        }
        displayTab = new PlankButton("Display", (_, _) => Select(false)) { Selected = true };
        controlsTab = new PlankButton("Controls", (_, _) => Select(true));
        showControlsTab = Select;
        tabs.Controls.Add(displayTab);
        tabs.Controls.Add(controlsTab);
        body.Controls.Add(tabs);
        BuildDisplay(display);
        BuildControls(controls);
        body.Controls.Add(display);
        body.Controls.Add(controls);
    }

    void BuildDisplay(FlowLayoutPanel body)
    {
        var cfg = new GameConfig(configPath);
        var table = Table();
        void Row(string label, string key, (string text, string raw)[] options, string fallback)
        {
            var box = new ThemedCombo();
            foreach (var o in options) box.Items.Add(o.text);
            var current = cfg.Get("video", key) ?? fallback;
            int index = Array.FindIndex(options, o => o.raw.Trim('"') == current.Trim('"'));
            box.SelectedIndex = index >= 0 ? index : Array.FindIndex(options, o => o.raw.Trim('"') == fallback);
            box.SelectedIndexChanged += (_, _) =>
            {
                var c = new GameConfig(configPath);
                c.Set("video", key, options[box.SelectedIndex].raw);
                c.Save();
            };
            table.Controls.Add(FieldLabel(label));
            table.Controls.Add(box);
        }
        Row("Window", "display_mode", new[] { ("Borderless fullscreen", "\"borderless\""), ("Window", "\"windowed\""), ("Exclusive fullscreen", "\"exclusive\"") }, "borderless");
        Row("Resolution", "resolution_multiplier", new[] { ("720p", "1.5"), ("1080p", "2.25"), ("4K", "4.5"), ("Your screen's size", "0") }, "2.25");
        Row("Picture shape", "aspect_mode", new[] { ("Original 4:3", "0"), ("Widescreen 16:9", "1"), ("Fill the screen", "2"), ("Cinematic 2.39:1", "3") }, "3");
        Row("Frame rate", "fps60", new[] { ("30 fps (original)", "false"), ("60 fps", "true") }, "false");
        Row("HD textures", "texture_replacements", new[] { ("On", "true"), ("Off (original textures)", "false") }, "true");
        Row("Anti-aliasing", "msaa", new[] { ("Off", "1"), ("2x MSAA", "2"), ("4x MSAA", "4"), ("8x MSAA", "8") }, "1");
        Row("Intro movies", "skip_intro", new[] { ("Play them", "false"), ("Skip them", "true") }, "false");
        Row("HDR", "hdr", new[] { ("Off", "false"), ("On (needs HDR on in Windows)", "true") }, "false");
        Row("HDR brightness", "hdr_paper_white_nits", new[] { ("Dim (120 nits)", "120.0"), ("Normal (200 nits)", "200.0"), ("Bright (300 nits)", "300.0"), ("Brightest (400 nits)", "400.0") }, "200.0");
        Row("HDR highlights", "hdr_peak_nits", new[] { ("Subtle (400 nits)", "400.0"), ("Standard (1000 nits)", "1000.0"), ("Strong (1500 nits)", "1500.0") }, "1000.0");
        body.Controls.Add(table);
        body.Controls.Add(Note("Changes are saved at once and apply the next time the game starts. In the game, F10 opens the same " +
                               "settings, F11 switches between fullscreen and a window, and F2 saves a screenshot."));
    }

    void BuildControls(FlowLayoutPanel body)
    {
        body.Controls.Add(Note("Gamepads (Xbox, PlayStation, Switch Pro, GameCube adapter...) work right away as a GameCube controller. " +
                               "Change the buttons below, or in the game with F10 > Controls."));
        body.Controls.Add(Title("Gamepad"));
        padStatus = FieldLabel("Looking for controllers...");
        padStatus.Margin = S(0, 4, 0, 6);
        body.Controls.Add(padStatus);
        // [controller] layout: "gamecube" or "xbox_original" (the game's original Xbox controls, hle/no100f_xbox_layout.h).
        var table = Table();
        layoutBox = new ThemedCombo();
        layoutBox.Items.Add("GameCube");
        layoutBox.Items.Add("Original Xbox");
        layoutBox.SelectedIndex = new GameConfig(configPath).Get("controller", "layout") == "xbox_original" ? 1 : 0;
        layoutBox.SelectedIndexChanged += (_, _) => { SaveLayout(layoutBox.SelectedIndex == 1); ShowXboxRows(); };
        table.Controls.Add(FieldLabel("Layout"));
        table.Controls.Add(layoutBox);
        body.Controls.Add(table);
        body.Controls.Add(Note("Original Xbox: A jump, X head smash, B dig / enter / talk and back in menus, Y on-screen display, " +
                               "LT gum, RT run, LB soap bubble (Xbox White), RB sneak (Xbox Black); the bumpers work together with the " +
                               "triggers, and the hints in the game name these buttons. Black and White can go on other buttons below. " +
                               "GameCube: B head smash and back in menus, X dig / enter / talk, Z + L soap bubble, Z + R sneak (Z = RB). " +
                               "Also in F10 > Controls."));
        BuildPadMapping(body);
        body.Controls.Add(Title("Keyboard"));
        table = Table();
        foreach (var (what, key) in new[] { ("Move", "W A S D"), ("Camera (C-stick)", "Arrow keys"), ("A (jump)", "Space"), ("B", "Left Shift"),
                     ("X / Y", "R / F"), ("Z", "C"), ("L / R", "Q / E"), ("Start (pause)", "Enter"), ("D-pad", "I J K L") })
        {
            var name = FieldLabel(what);
            name.Font = Body;
            name.ForeColor = CreamDim;
            table.Controls.Add(name);
            table.Controls.Add(FieldLabel(key));
        }
        body.Controls.Add(table);
        body.Controls.Add(Title("In the game"));
        body.Controls.Add(Note("F10 settings   ·   F11 fullscreen / window   ·   F2 screenshot   ·   F1 project page"));
    }

    // ----- Mods -----
    void BuildMods(FlowLayoutPanel body)
    {
        body.Controls.Add(Note("A mod is a folder in data\\UserData\\mods. Its files\\ folder mirrors the disc's files\\ (replacing game files), " +
                               "and its textures\\ folder holds texture replacements. Untick a mod to switch it off; when two mods change the same file, the later name wins."));
        var list = new FlowLayoutPanel
        {
            Size = S(480, 240), FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true,
            BackColor = Field, Padding = S(10, 8, 10, 8), Margin = S(0, 8, 0, 8),
        };
        var empty = Note("No mods yet.");
        void Fill()
        {
            list.Controls.Clear();
            var dirs = Directory.Exists(modsDir) ? Directory.GetDirectories(modsDir).OrderBy(d => d, StringComparer.OrdinalIgnoreCase).ToArray() : Array.Empty<string>();
            if (dirs.Length == 0) { list.Controls.Add(empty); return; }
            foreach (var d in dirs)
            {
                var box = new CheckBox
                {
                    Text = Path.GetFileName(d), Checked = !File.Exists(Path.Combine(d, "disabled")), AutoSize = true,
                    ForeColor = Cream, BackColor = Field, Font = Body, FlatStyle = FlatStyle.Flat, Cursor = Cursors.Hand, Margin = S(0, 3, 0, 3),
                };
                box.FlatAppearance.BorderColor = Lime;
                box.FlatAppearance.CheckedBackColor = Purple;
                var dir = d;
                box.CheckedChanged += (_, _) =>
                {
                    var flag = Path.Combine(dir, "disabled");
                    if (box.Checked) File.Delete(flag); else File.WriteAllText(flag, "");
                };
                list.Controls.Add(box);
            }
        }
        body.Controls.Add(list);
        var row = Row();
        row.Controls.Add(new PlankButton("Open the mods folder", (_, _) => OpenFolder(modsDir)));
        row.Controls.Add(new PlankButton("Refresh", (_, _) => Fill()));
        body.Controls.Add(row);
        Fill();
    }

    // ----- Extras: the disc, holidays, folders, about -----
    void BuildExtras(FlowLayoutPanel body)
    {
        body.Controls.Add(Title("Game disc"));
        discStatus = FieldLabel("");
        discStatus.MaximumSize = new Size(S(520), 0);
        body.Controls.Add(discStatus);
        chooseDisc = new PlankButton("Choose disc image", async (_, _) => await ChooseDisc(), primary: true);
        body.Controls.Add(chooseDisc);
        progress = new ThemedBar { Visible = false };
        body.Controls.Add(progress);
        body.Controls.Add(dumpHelp = Note(
            "FrightWorks runs your own copy of Scooby-Doo! Night of 100 Frights for the GameCube (USA, GIHE78, version 1.01). " +
            "Dump the disc you own (for example with CleanRip on a Wii) as a plain .iso or .ciso, then choose it here. " +
            "The launcher copies the game files once; the image file isn't needed afterwards."));

        body.Controls.Add(Title("Holidays"));
        body.Controls.Add(HolidayBox());
        body.Controls.Add(Note("Outside Mystic Manor and in the playground, the game celebrates holidays on their real dates. " +
                               "Pick one to see it any day, or turn them off."));

        body.Controls.Add(Title("Developer tools"));
        body.Controls.Add(DevToolsBox());
        body.Controls.Add(Note("For testing and bug hunting: F8 in the game opens the dev menu (go to any level, saved spots, " +
                               "free camera, god mode, texture finder, recording options), F3 records a video of the game into " +
                               "the Videos folder with a .txt for bug reports, and the game shows DEV and its build in the " +
                               "corner. Applies the next time the game starts."));
        ffmpegStatus = Note("");
        body.Controls.Add(ffmpegStatus);
        ffmpegButton = new PlankButton("Download ffmpeg", async (_, _) => await DownloadFfmpeg());
        body.Controls.Add(ffmpegButton);
        ffmpegBar = new ThemedBar { Visible = false };
        body.Controls.Add(ffmpegBar);
        RefreshFfmpeg(offerDownload: false);

        body.Controls.Add(Title("Folders"));
        var folders = Row();
        folders.MaximumSize = new Size(S(520), 0);  // wrap instead of widening the page
        folders.Controls.Add(new PlankButton("Game", (_, _) => OpenFolder(play)));
        folders.Controls.Add(new PlankButton("Saves and settings", (_, _) => OpenFolder(userData)));
        folders.Controls.Add(new PlankButton("Screenshots", (_, _) => OpenFolder(ScreenshotsDir)));
        folders.Controls.Add(new PlankButton("Videos", (_, _) => OpenFolder(VideosDir)));
        body.Controls.Add(folders);

        body.Controls.Add(Title("About"));
        var version = typeof(MainForm).Assembly.GetName().Version;
        body.Controls.Add(Note("FrightWorks " + (version == null ? "" : "v" + version.ToString(3) + " ") + "by Loneranger419, based on Scooby-Doo: Refrightened by JosiahG. Built on WiiCompiled (GPLv3) " +
                               "and Aurora (MIT). HD texture pack by dogerki. Button prompts by Kenney (CC0). Lettering in " +
                               "Luckiest Guy by Astigmatic and in-game menu text in Roboto by Christian Robertson (both Apache 2.0). " +
                               "Scooby-Doo is a trademark of Hanna-Barbera / Warner Bros.; this is an unofficial fan project."));
        body.Controls.Add(new PlankButton("Project page", (_, _) => Process.Start(new ProcessStartInfo(ProjectUrl) { UseShellExecute = true })));
    }

    // The hubs' holiday events ([game] holiday): the game checks the date when a hub loads. "auto" uses today's date,
    // "off" none, "MM-DD" pretends it is that day.
    static readonly (string text, string value)[] Holidays =
    {
        ("By today's date", "auto"), ("Off", "off"), ("New Year's Day", "01-01"), ("St. Patrick's Day", "03-17"),
        ("Fourth of July", "07-04"), ("Halloween", "10-31"), ("Christmas", "12-25"),
    };

    Control HolidayBox()
    {
        var box = new ThemedCombo();
        foreach (var h in Holidays) box.Items.Add(h.text);
        var current = new GameConfig(configPath).Get("game", "holiday") ?? "auto";
        int index = Array.FindIndex(Holidays, h => h.value == current);
        if (index < 0) { box.Items.Add("Custom date (" + current + ")"); index = box.Items.Count - 1; }
        box.SelectedIndex = index;
        box.SelectedIndexChanged += (_, _) =>
        {
            if (box.SelectedIndex >= Holidays.Length) return;
            var c = new GameConfig(configPath);
            c.SetString("game", "holiday", Holidays[box.SelectedIndex].value);
            c.Save();
        };
        return box;
    }

    // [game] dev_tools: the F8 dev menu and the DEV build mark (the game reads it at startup).
    Control DevToolsBox()
    {
        var box = new ThemedCombo();
        box.Items.Add("Off");
        box.Items.Add("On (F8 dev menu, DEV mark)");
        box.SelectedIndex = new GameConfig(configPath).Get("game", "dev_tools") == "true" ? 1 : 0;
        box.SelectedIndexChanged += (_, _) =>
        {
            var c = new GameConfig(configPath);
            c.Set("game", "dev_tools", box.SelectedIndex == 1 ? "true" : "false");
            c.Save();
            RefreshFfmpeg(offerDownload: box.SelectedIndex == 1);
        };
        return box;
    }

    // F3 videos need ffmpeg. Looked for only while the developer tools are on; turning them on offers the download.
    void RefreshFfmpeg(bool offerDownload)
    {
        bool dev = new GameConfig(configPath).Get("game", "dev_tools") == "true";
        bool busy = ffmpegCancel != null;
        var found = dev && !busy ? FfmpegSetup.Find(root, play) : null;
        ffmpegStatus.Visible = dev || busy;
        ffmpegButton.Visible = dev && !busy && found == null;
        if (busy || !dev) return;
        ffmpegStatus.Text = found != null
            ? "F3 videos use ffmpeg from " + found + "."
            : "F3 videos need ffmpeg, which isn't installed. The launcher can download it (about 200 MB, the free " +
              "win64 build from github.com/BtbN/FFmpeg-Builds) into " + FfmpegSetup.InstallDir(root, IsRelease) + ".";
        ffmpegStatus.ForeColor = found != null ? Good : Warn;
        if (found == null && offerDownload &&
            MessageBox.Show(this, "F3 video recording needs ffmpeg, which isn't installed.\n\nDownload it now? (about 200 MB, " +
                                  "the free win64 build from github.com/BtbN/FFmpeg-Builds, checked against its published SHA-256)",
                            AppTitle, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
            _ = DownloadFfmpeg();
    }

    async Task DownloadFfmpeg()
    {
        if (ffmpegCancel != null) return;
        ffmpegCancel = new CancellationTokenSource();
        ffmpegButton.Visible = false;
        ffmpegBar.Visible = true;
        ffmpegBar.Maximum = 1000;
        ffmpegBar.Value = 0;
        ffmpegStatus.ForeColor = CreamDim;
        ffmpegStatus.Text = "Downloading ffmpeg...";
        var report = new Progress<(long done, long total)>(p =>
        {
            if (IsDisposed || ffmpegCancel == null) return;
            ffmpegBar.Value = p.total > 0 ? (int)(p.done * 1000 / p.total) : 0;
            ffmpegStatus.Text = p.total > 0 ? $"Downloading ffmpeg: {p.done >> 20} of {p.total >> 20} MB" : $"Downloading ffmpeg: {p.done >> 20} MB";
        });
        string? error = null;
        try { await FfmpegSetup.Download(root, IsRelease, report, ffmpegCancel.Token); }
        catch (OperationCanceledException) when (ffmpegCancel.IsCancellationRequested) { return; }  // the launcher closed
        catch (Exception e) { error = e.Message; }
        finally
        {
            ffmpegCancel.Dispose();
            ffmpegCancel = null;
            if (!IsDisposed) ffmpegBar.Visible = false;
        }
        RefreshFfmpeg(offerDownload: false);
        if (error != null) MessageBox.Show(this, "Downloading ffmpeg failed: " + error, AppTitle, MessageBoxButtons.OK, MessageBoxIcon.Warning);
    }

    // ----- The disc and starting the game -----
    (bool ok, string text) DiscState()
    {
        var boot = Path.Combine(discDir, "sys", "boot.bin");
        if (!File.Exists(boot)) return (false, "No game disc set up yet. Choose your Scooby-Doo! Night of 100 Frights disc image.");
        var id = System.Text.Encoding.ASCII.GetString(File.ReadAllBytes(boot), 0, 6);
        return id == GcDisc.WantedId ? (true, "Scooby-Doo! Night of 100 Frights (GIHE78) is set up.") : (false, $"The Disc folder holds {id}, not Scooby-Doo! Night of 100 Frights USA (GIHE78).");
    }

    bool CanPlay() => DiscState().ok && File.Exists(Path.Combine(play, GameExe));

    void RefreshDisc()
    {
        var (ok, text) = DiscState();
        bool exe = File.Exists(Path.Combine(play, GameExe));
        scene.SetStatus(!exe ? "Game files missing: re-extract the download" : ok ? "Ready to play" : "No game disc yet: press Play to set it up", ok && exe);
        if (discStatus == null) return;
        discStatus.Text = text;
        discStatus.ForeColor = ok ? Good : Warn;
        chooseDisc.Text = ok ? "Change disc image" : "Choose disc image";
        dumpHelp.Visible = !ok;
    }

    async Task ChooseDisc()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Choose your Scooby-Doo! Night of 100 Frights disc image",
            Filter = "GameCube disc images (*.iso;*.gcm;*.ciso)|*.iso;*.gcm;*.ciso|All files (*.*)|*.*",
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        GcDisc disc;
        try { disc = GcDisc.Open(dialog.FileName); }
        catch (Exception e) { MessageBox.Show(this, "That file can't be read: " + e.Message, AppTitle); return; }
        using (disc)
        {
            var problem = disc.Problem();
            if (problem.Length > 0) { MessageBox.Show(this, problem, AppTitle, MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
            var staging = discDir + ".new";
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
            chooseDisc.Enabled = false;
            progress.Visible = true;
            progress.Value = 0;
            var report = new Progress<(int done, int total, string name)>(p =>
            {
                progress.Maximum = Math.Max(1, p.total);
                progress.Value = Math.Min(p.done, progress.Maximum);
                discStatus.Text = $"Extracting {p.done}/{p.total}: {p.name}";
            });
            try
            {
                await Task.Run(() => disc.Extract(staging, (d, t, n) => ((IProgress<(int, int, string)>)report).Report((d, t, n))));
                if (Directory.Exists(discDir)) Directory.Delete(discDir, true);
                Directory.Move(staging, discDir);
                var cfg = new GameConfig(configPath);
                cfg.Set("paths", "dvd_root", IsRelease ? "'../Disc/DATA'" : "'../../Disc/DATA'");  // relative to UserData, so the folder can move
                cfg.Save();
            }
            catch (Exception e) { MessageBox.Show(this, "Extraction failed: " + e.Message, AppTitle, MessageBoxButtons.OK, MessageBoxIcon.Error); }
            finally { progress.Visible = false; chooseDisc.Enabled = true; RefreshDisc(); }
        }
    }

    void Launch()
    {
        var exe = Path.Combine(play, GameExe);
        if (!File.Exists(exe)) { MessageBox.Show(this, "The game files (the data folder) are missing. Re-extract the download next to this launcher.", AppTitle); return; }
        gamepads.Pause();  // the game has the controllers to itself
        var game = Process.Start(new ProcessStartInfo(exe) { WorkingDirectory = play, UseShellExecute = true });
        if (game != null)
        {
            game.EnableRaisingEvents = true;
            game.Exited += (_, _) => { try { BeginInvoke(() => gamepads.Start()); } catch (InvalidOperationException) { } };
        }
        WindowState = FormWindowState.Minimized;
    }

    // ----- Controllers -----
    // Gamepad buttons: the [controller] keys F10 > Controls edits too (PadMapping). The game reads them at start.
    void BuildPadMapping(FlowLayoutPanel body)
    {
        body.Controls.Add(Title("Gamepad buttons"));
        body.Controls.Add(Note("The controller button that presses each GameCube button. Buttons go by position: South is A " +
                               "on Xbox, Cross on PlayStation, B on Nintendo. Used from the next game start; F10 > Controls " +
                               "in the game edits the same mapping."));
        var table = Table();
        var config = new GameConfig(configPath);
        foreach (var row in PadMapping.Rows)
        {
            var label = FieldLabel(row.Label);
            label.Font = Body;
            label.ForeColor = CreamDim;
            var box = new ThemedCombo();
            foreach (var choice in PadMapping.ChoicesFor(row)) box.Items.Add(choice);
            table.Controls.Add(label);
            table.Controls.Add(box);
            padRows.Add((row, label, box));
            ShowPadValue(row, box, config.Get("controller", row.Key));
            box.SelectedIndexChanged += (_, _) =>
            {
                if (box.SelectedItem is not PadMapping.Choice choice) return;  // the "(custom)" entry
                var c = new GameConfig(configPath);
                string? old = c.Get("controller", row.Key);
                if (old != null && PadMapping.Split(old).Name == choice.Name) return;
                if (old == null && choice.Name == row.Default) return;  // shown as the default already
                c.SetString("controller", row.Key, PadMapping.Compose(choice.Name, old));
                c.Save();
            };
        }
        body.Controls.Add(table);
        ShowXboxRows();
        var restore = new PlankButton("Restore defaults", (_, _) => RestorePadDefaults());
        restore.Margin = S(0, 4, 0, 8);
        body.Controls.Add(restore);
    }

    // Selects the value's pad button; one the list doesn't know stays as it is, shown as "(custom)".
    static void ShowPadValue(PadMapping.Row row, ThemedCombo box, string? value)
    {
        string name = value == null ? row.Default : PadMapping.Split(value).Name;
        for (int i = box.Items.Count - 1; i >= 0; i--)
            if (box.Items[i] is string) box.Items.RemoveAt(i);
        int index = -1;
        for (int i = 0; i < box.Items.Count; i++)
            if (box.Items[i] is PadMapping.Choice c && c.Name == name) { index = i; break; }
        if (index < 0) index = box.Items.Add("(custom) " + value);
        box.SelectedIndex = index;
    }

    // Black / White exist only with the original Xbox layout.
    void ShowXboxRows()
    {
        bool xbox = layoutBox.SelectedIndex == 1;
        foreach (var (row, label, box) in padRows)
            if (row.Xbox) { label.Visible = xbox; box.Visible = xbox; }
    }

    // Writes every GameCube button's default (so a mapping saved for one controller in F10 doesn't stay in force) and
    // drops the Black / White choices.
    void RestorePadDefaults()
    {
        var c = new GameConfig(configPath);
        foreach (var row in PadMapping.Rows)
        {
            if (row.Xbox) c.Remove("controller", row.Key);
            else c.SetString("controller", row.Key, row.Default);
        }
        c.Save();
        foreach (var (row, _, box) in padRows) ShowPadValue(row, box, c.Get("controller", row.Key));
    }

    void SaveLayout(bool original)
    {
        var c = new GameConfig(configPath);
        c.SetString("controller", "layout", original ? "xbox_original" : "gamecube");
        c.Save();
    }

    void RefreshGamepads()
    {
        var pads = gamepads.Connected;
        padStatus.Text = !gamepads.Available ? "Controller detection is not available (SDL3.dll missing from the game folder)."
            : pads.Count == 0 ? "No controller connected."
            : "Connected: " + string.Join(", ", pads.Select(p => $"{p.Name} ({Gamepads.Describe(p.Kind)})"));
        // An Xbox pad and no layout chosen yet (key absent): ask once per launcher start.
        var xbox = pads.FirstOrDefault(p => p.Kind == Gamepads.Kind.Xbox);
        if (xbox == null || askedLayout || new GameConfig(configPath).Get("controller", "layout") != null) return;
        askedLayout = true;
        bool? original = AskXboxLayout(xbox.Name);
        if (original == null) return;
        SaveLayout(original.Value);
        layoutBox.SelectedIndex = original.Value ? 1 : 0;
    }

    // true = original Xbox layout, false = GameCube layout, null = closed without choosing (asked again next start).
    bool? AskXboxLayout(string padName)
    {
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        using var dialog = new Form
        {
            Text = "Xbox controller detected", FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false,
            StartPosition = FormStartPosition.CenterParent, ShowInTaskbar = false, BackColor = BoardBottom, Font = Body,
            AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = S(20, 14, 20, 14), Icon = Icon,
        };
        var body = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = BoardBottom };
        var title = Title("Play with the original Xbox controls?");
        title.Margin = S(0, 0, 0, 6);
        body.Controls.Add(title);
        body.Controls.Add(Note($"{padName} is connected. Night of 100 Frights came out on the Xbox too, with its own button layout."));
        body.Controls.Add(Note("Original Xbox: A jump, X head smash, B dig / enter / talk, LT gum, RT run, LB soap bubble, RB sneak. " +
                               "The bumpers work together with the triggers, like on the Xbox."));
        body.Controls.Add(Note("GameCube: B head smash, X dig / enter / talk, RB (Z) + a trigger for soap bubble and sneak."));
        body.Controls.Add(Note("You can change this any time in Settings > Controls or in the game (F10 > Controls)."));
        var row = Row();
        row.Margin = S(0, 8, 0, 0);
        bool? answer = null;
        row.Controls.Add(new PlankButton("Original Xbox layout", (_, _) => { answer = true; dialog.Close(); }, primary: true));
        row.Controls.Add(new PlankButton("GameCube layout", (_, _) => { answer = false; dialog.Close(); }));
        body.Controls.Add(row);
        dialog.Controls.Add(body);
        dialog.ShowDialog(this);
        return answer;
    }
}
