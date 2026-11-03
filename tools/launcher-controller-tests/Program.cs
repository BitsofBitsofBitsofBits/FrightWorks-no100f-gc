using ScoobyReMasked;
using B = ScoobyReMasked.LauncherButtons;

static class Tests
{
    static int assertions;
    static string output = "";

    static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    [STAThread]
    static int Main(string[] args)
    {
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        output = Path.GetFullPath(args.Length > 0 ? args[0] : Path.Combine("autotest", "launcher-controller"));
        Directory.CreateDirectory(output);
        string fixture = Path.Combine(output, "fixture-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(fixture, "data", "UserData", "mods", "Test mod"));
        for (int i = 0; i < 15; i++)
            Directory.CreateDirectory(Path.Combine(fixture, "data", "UserData", "mods", "zz-mod-" + i.ToString("D2")));
        File.WriteAllText(Path.Combine(fixture, "data", "portable.txt"), "");
        var paths = new DataPaths(fixture, Path.Combine(fixture, "data"));
        File.WriteAllText(paths.ConfigPath, "[video]\nhdr_peak_nits = 1234.5\n[controller]\nlayout = \"gamecube\"\n[future]\nvalue = \"keep\"\n");
        try
        {
            Input();
            Console.WriteLine("PASS input reducer");
            using var form = new MainForm(dataPaths: paths, enableServices: false);
            form.Show();
            Application.DoEvents();
            Routing(form);
            Console.WriteLine("PASS ownership, foreground and hotplug routing");
            Pages(form, paths);
            Console.WriteLine("PASS mansion, settings, mods and extras");
            Dialogs(form, fixture);
            Console.WriteLine("PASS confirmations, choices, file/folder browsing and text entry");
            Controls(form);
            form.Close();
            Console.WriteLine($"PASS: {assertions} assertions. No SDL polling, game process, player-data writes, or OS changes.");
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
        finally { Directory.Delete(fixture, true); }
    }

    static void Input()
    {
        var input = new ControllerInput();
        Check(input.Read(B.Confirm, 0) == B.None, "Hotplug with held confirm must not activate");
        Check(input.Read(B.None, 1) == B.None, "Neutral arms input");
        Check(input.Read(B.Confirm, 2) == B.Confirm, "Fresh confirm");
        Check(input.Read(B.Confirm, 1000) == B.None, "Confirm never repeats");
        input.Reset();
        Check(input.Read(B.Confirm, 1001) == B.None, "Reset suppresses held confirm");
        input.Read(B.None, 1002);
        Check(input.Read(B.Down, 1003) == B.Down, "Initial movement");
        Check(input.Read(B.Down, 1402) == B.None, "Repeat waits 400 ms");
        Check(input.Read(B.Down, 1403) == B.Down, "Repeat begins at 400 ms");
        Check(input.Read(B.Down, 1502) == B.None, "Repeat waits another 100 ms");
        Check(input.Read(B.Down, 1503) == B.Down, "Repeat every 100 ms");
        input.Read(B.None, 1504);
        Check(input.Read(B.Up | B.Down, 1505) == B.None, "Opposed input cancels");
        Check(ControllerInput.FromSdl(0, 15999, -15999, 0) == B.None, "Stick deadzone");
        Check(ControllerInput.FromSdl(0, 16001, -16001, 16001) == (B.Up | B.Right | B.ScrollDown), "Axes use SDL positions");
        Check(ControllerInput.FromSdl((1u << 0) | (1u << 1) | (1u << 9) | (1u << 10), 0, 0, 0) ==
            (B.Confirm | B.Back | B.Previous | B.Next), "Physical face and bumper mapping independent of game config");
        Check(ControllerInput.FromSdl(15u << 11, 0, 0, 0) == (B.Up | B.Down | B.Left | B.Right), "SDL D-pad mapping");
    }

    static void Routing(Form owner)
    {
        using var pads = new Gamepads(Path.Combine(output, "no-sdl-here"));
        using var nav = new ControllerNavigation(owner, pads);
        ControllerSample[] Sample(B buttons, uint id = 1) => new[] { new ControllerSample(id, buttons) };
        Check(nav.ReadInput(null, Sample(B.None), 0) == B.None, "No foreground window");
        Check(nav.ReadInput(owner, Sample(B.Confirm), 1) == B.None, "Returning focus with held confirm");
        nav.ReadInput(owner, Sample(B.None), 2);
        Check(nav.ReadInput(owner, Sample(B.Confirm), 3) == B.Confirm, "Owner accepts fresh input");
        using var foreign = new Form();
        foreign.Show();
        Check(nav.ReadInput(foreign, Sample(B.None), 4) == B.None, "Unrelated foreground app is gated");
        Check(!ControllerNavigation.Owns(owner, foreign), "Other window has no ownership");
        using var modal = new Form { Owner = owner };
        modal.Show();
        Check(nav.ReadInput(modal, Sample(B.Confirm), 5) == B.None, "Modal transition suppresses held confirm");
        nav.ReadInput(modal, Sample(B.None), 6);
        Check(nav.ReadInput(modal, Sample(B.Back), 7) == B.Back, "Owned dialog receives input");
        Check(nav.ReadInput(owner, Sample(B.Confirm), 8) == B.None, "Closing modal suppresses held input");
        nav.ReadInput(owner, Sample(B.None), 9);
        nav.ReadInput(owner, Array.Empty<ControllerSample>(), 10);
        Check(nav.ReadInput(owner, Sample(B.Confirm), 11) == B.None, "Reconnect requires neutral");
        nav.ReadInput(owner, Sample(B.None), 12);
        Check(nav.ReadInput(owner, Sample(B.Down), 13) == B.Down, "Reconnected controller navigates");
        Check(nav.ReadInput(owner, Sample(B.Confirm, 2), 14) == B.None, "Replacement device requires neutral");
        owner.Enabled = false;
        Check(nav.ReadInput(owner, Sample(B.None, 2), 15) == B.None, "Disabled modal owner cannot navigate");
        owner.Enabled = true;
        owner.WindowState = FormWindowState.Minimized;
        Check(nav.ReadInput(owner, Sample(B.Confirm, 2), 16) == B.None, "Minimized launcher cannot navigate");
        owner.WindowState = FormWindowState.Normal;
        Check(nav.ReadInput(owner, Sample(B.Confirm, 2), 17) == B.None, "Restore requires neutral");
        modal.Close();
        foreign.Close();
        owner.Activate();
        using var missing = new Gamepads(Path.Combine(output, "no-sdl-here"));
        missing.Start();
        missing.Pause();
        Check(!missing.Available && !string.IsNullOrEmpty(missing.Error), "Missing SDL surfaces an explicit error");
        Check(missing.Input.Count == 0 && missing.Connected.Count == 0, "Pause clears device and input state");
    }

    static IEnumerable<Control> All(Control root)
    {
        foreach (Control child in root.Controls)
        {
            yield return child;
            foreach (var nested in All(child)) yield return nested;
        }
    }

    static T Find<T>(Control root, string text) where T : Control => All(root).OfType<T>().Single(c => c.Text == text && c.Visible);

    static void Press(Form form, B action) { ControllerNavigation.Dispatch(form, action); Application.DoEvents(); }
    static void Click(Form form, Control control) { ControllerNavigation.Focus(control); Press(form, B.Confirm); }

    static void Shot(Form form, string name)
    {
        using var bitmap = new Bitmap(form.Width, form.Height);
        form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
        bitmap.Save(Path.Combine(output, name + ".png"));
    }

    static void CycleTargets(Form form)
    {
        var targets = ControllerNavigation.Targets(form).ToArray();
        Check(targets.Length > 0, "Page has focusable targets");
        ControllerNavigation.Focus(targets[0]);
        for (int i = 1; i <= targets.Length; i++)
        {
            Press(form, B.Next);
            Check(targets[i % targets.Length].ContainsFocus, "Bumper traversal reaches every visible/enabled control");
        }
    }

    static void Pages(MainForm form, DataPaths paths)
    {
        var scene = All(form).OfType<MansionScene>().Single();
        scene.Navigate(B.None);
        Check(scene.FocusedSign == "play", "Initial mansion focus is Play");
        Check(scene.AccessibilityObject.GetChildCount() == 5, "Mansion exposes five accessible signs");
        Press(form, B.Down);
        Check(scene.FocusedSign == "settings", "D-pad selects Settings");
        Shot(form, "Main-focus");
        Press(form, B.Confirm);
        Check(Find<PlankButton>(form, "Display").Focused, "Settings opens with tab focus");
        CycleTargets(form);
        var combo = ControllerNavigation.Targets(form).OfType<ComboBox>().First();
        ControllerNavigation.Focus(combo);
        int original = combo.SelectedIndex;
        WithModal(() => Press(form, B.Confirm), dialog =>
        {
            Press(dialog, B.Down);
            Press(dialog, B.Back);
        });
        Check(combo.SelectedIndex == original, "Cancel choice does not change settings");
        WithModal(() => Press(form, B.Confirm), dialog =>
        {
            Press(dialog, B.Down);
            Press(dialog, B.Confirm);
        });
        Check(combo.SelectedIndex == Math.Min(original + 1, combo.Items.Count - 1), "Confirm applies dropdown choice");
        Click(form, Find<PlankButton>(form, "Controls"));
        Check(ControllerNavigation.Targets(form).OfType<ComboBox>().Any(), "Controls mapping fields reachable");
        var targets = ControllerNavigation.Targets(form).ToArray();
        Check(targets.Contains(Find<PlankButton>(form, "Restore defaults")), "Controls includes restore action");
        CycleTargets(form);
        ControllerNavigation.Focus(Find<PlankButton>(form, "Controls"));
        Shot(form, "Controls-focus");
        Press(form, B.Back);
        Check(scene.Focused && scene.FocusedSign == "settings", "Back returns to original sign");
        Press(form, B.Down);
        Press(form, B.Confirm);
        var mod = Find<CheckBox>(form, "Test mod");
        Click(form, mod);
        Check(!mod.Checked && File.Exists(Path.Combine(paths.Mods, "Test mod", "disabled")), "Mods checkbox uses existing semantics");
        CycleTargets(form);
        var lastMod = Find<CheckBox>(form, "zz-mod-14");
        ControllerNavigation.Focus(lastMod);
        Check(lastMod.Parent is ScrollableControl nested && nested.AutoScrollPosition.Y < 0, "Nested mod list scrolls focused item into view");
        Shot(form, "Mods-focus");
        Press(form, B.Back);
        Press(form, B.Down);
        Press(form, B.Confirm);
        CycleTargets(form);
        var project = Find<PlankButton>(form, "Project page");
        ControllerNavigation.Focus(project);
        var board = All(form).OfType<Board>().Single(b => b.Visible);
        Check(board.Body.AutoScrollPosition.Y < 0, "Focusing offscreen Extras controls scrolls the board");
        int scrollBefore = board.Body.AutoScrollPosition.Y;
        Press(form, B.ScrollUp);
        Check(board.Body.AutoScrollPosition.Y > scrollBefore, "Right stick scrolls the page upward");
        Shot(form, "Extras-focus");
        var config = new GameConfig(paths.ConfigPath);
        Check(config.Get("future", "value") == "keep", "Unknown setting preserved");
        Check(config.Get("video", "hdr_peak_nits") == "1234.5", "Custom HDR value preserved");
        Check(config.Get("controller", "layout") == "gamecube", "Game mapping unchanged by launcher navigation");
        Press(form, B.Back);
    }

    static void WithModal(Action open, Action<Form> exercise)
    {
        Exception? error = null;
        using var timer = new System.Windows.Forms.Timer { Interval = 30 };
        var clock = System.Diagnostics.Stopwatch.StartNew();
        timer.Tick += (_, _) =>
        {
            var dialog = Application.OpenForms.Cast<Form>().LastOrDefault(f => f.Modal);
            if (dialog == null) return;
            timer.Stop();
            if (clock.Elapsed.TotalSeconds > 10)
            {
                error = new TimeoutException("Modal test did not complete");
                dialog.Close();
                return;
            }
            try { exercise(dialog); }
            catch (Exception e) { error = e; dialog.Close(); }
            if (!dialog.IsDisposed && dialog.Visible && dialog.DialogResult == DialogResult.None)
            {
                error ??= new InvalidOperationException("Modal exercise did not close " + dialog.Text);
                dialog.Close();
            }
        };
        timer.Start();
        open();
        if (error != null) throw error;
    }

    static void Dialogs(MainForm owner, string fixture)
    {
        DialogResult answer = DialogResult.None;
        WithModal(() => answer = LauncherDialogs.Show(owner, "A disposable confirmation", "Test", MessageBoxButtons.YesNoCancel), dialog =>
        {
            Check(ControllerNavigation.Focused(dialog)?.Text == "Cancel", "Question focuses safe Cancel choice");
            Shot(dialog, "Confirmation");
            Press(dialog, B.Back);
        });
        Check(answer == DialogResult.Cancel, "Back cancels confirmation");
        int selection = -2;
        WithModal(() => selection = LauncherDialogs.Choose(owner, new[] { "First", "Second" }, 0), dialog =>
        { Press(dialog, B.Down); Press(dialog, B.Confirm); });
        Check(selection == 1, "Choice dialog supports list selection");
        string? text = null;
        WithModal(() => text = LauncherDialogs.EditText(owner, ""), dialog =>
        {
            Check(All(dialog).OfType<TextBox>().Single().TabStop, "Text editor remains keyboard-focusable");
            Press(dialog, B.Confirm);
            Check(Find<Button>(dialog, "a").Focused, "Confirm in text editor moves to keys without opening another editor");
            Click(dialog, Find<Button>(dialog, "a"));
            Click(dialog, Find<Button>(dialog, "b"));
            Click(dialog, Find<Button>(dialog, "Backspace"));
            Click(dialog, Find<Button>(dialog, "Done"));
        });
        Check(text == "a", "Controller text entry and erase");
        string image = Path.Combine(fixture, "test.iso");
        File.WriteAllText(image, "test fixture, not game data");
        string? selected = null;
        WithModal(() => selected = LauncherDialogs.PickPath(owner, "Disc picker test", fixture, false), dialog =>
        {
            var list = All(dialog).OfType<ListBox>().Single();
            Check(list.Items.Cast<string>().Contains("test.iso"), "Disc file browser lists image");
            list.SelectedIndex = list.Items.IndexOf("test.iso");
            Shot(dialog, "File-browser");
            Press(dialog, B.Confirm);
        });
        Check(selected == image, "Controller can choose disc path without shell keyboard input");
        WithModal(() => selected = LauncherDialogs.PickPath(owner, "Folder picker test", fixture, true), dialog =>
        { Click(dialog, Find<Button>(dialog, "Select this folder")); });
        Check(selected == fixture, "Controller can choose existing folder");
        WithModal(() => selected = LauncherDialogs.PickPath(owner, "Folder picker test", fixture, true), dialog => Press(dialog, B.Back));
        Check(selected == null, "Folder cancel returns no path");
        WithModal(() => selected = LauncherDialogs.PickPath(owner, "Create folder test", fixture, true), dialog =>
        {
            WithModal(() => Click(dialog, Find<Button>(dialog, "New folder")), keyboard =>
            {
                Click(keyboard, Find<Button>(keyboard, "a"));
                Click(keyboard, Find<Button>(keyboard, "Done"));
            });
            Check(Directory.Exists(Path.Combine(fixture, "a")), "New folder created only in disposable fixture");
            Click(dialog, Find<Button>(dialog, "Select this folder"));
        });
        Check(selected == Path.Combine(fixture, "a"), "Controller can choose its newly created folder");
        bool? xbox = true;
        WithModal(() => xbox = owner.AskXboxLayout("Test controller"), dialog =>
        {
            Check(ControllerNavigation.Targets(dialog).OfType<PlankButton>().Count() == 2, "Both layout choices reachable");
            Press(dialog, B.Back);
        });
        Check(xbox == null, "Back cancels Xbox layout prompt without changing game mappings");
        MainForm.UpdateAnswer update = MainForm.UpdateAnswer.Update;
        WithModal(() => update = owner.AskUpdate(new Updater.Found("test", null, null)), dialog =>
        {
            Check(ControllerNavigation.Targets(dialog).OfType<PlankButton>().Count() == 3, "All update prompt choices reachable");
            Press(dialog, B.Back);
        });
        Check(update == MainForm.UpdateAnswer.Later, "Back defers update without network/download");
    }

    static void Controls(Form owner)
    {
        using var form = new Form { Owner = owner, Size = new Size(500, 300) };
        var slider = new TrackBar { Minimum = 0, Maximum = 3, Value = 1 };
        var number = new NumericUpDown { Minimum = 0, Maximum = 3, Value = 1, Top = 70 };
        var disabled = new Button { Enabled = false, Text = "Disabled", Top = 120 };
        form.Controls.AddRange(new Control[] { slider, number, disabled });
        form.Show();
        ControllerNavigation.Focus(slider);
        Press(form, B.Right);
        Check(slider.Value == 2, "Slider adjusts");
        Press(form, B.Next);
        Check(number.ContainsFocus, "Bumper leaves slider for next control");
        Press(form, B.Left);
        Check(number.Value == 0, "Numeric field adjusts and clamps");
        Press(form, B.Left);
        Check(number.Value == 0, "Numeric minimum respected");
        Check(!ControllerNavigation.Targets(form).Contains(disabled), "Disabled actions skipped");
        form.Close();
    }
}
