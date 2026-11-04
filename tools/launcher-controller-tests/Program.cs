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
        File.WriteAllText(paths.ConfigPath, "[video]\nhdr_peak_nits = 1234.5\nresolution_multiplier = 8.0\nfullscreen_display = \"NOPE0000/gone\"\n[controller]\nlayout = \"gamecube\"\n[future]\nvalue = \"keep\"\n");
        try
        {
            Input();
            Console.WriteLine("PASS input reducer");
            LayoutMatching(paths);
            Console.WriteLine("PASS controller type matching, automatic/manual intent and hotplug");
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
            LayoutControls(form, paths);
            Console.WriteLine("PASS scheme migration, explicit same-value choices and foreground/game gates");
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

    static void LayoutMatching(DataPaths paths)
    {
        var xbox = new Gamepads.Pad("Xbox One Controller", Gamepads.Kind.Xbox);
        var cube = new Gamepads.Pad("GameCube adapter", Gamepads.Kind.GameCube);
        Check(Gamepads.KindOf(2) == Gamepads.Kind.Xbox && Gamepads.KindOf(3) == Gamepads.Kind.Xbox,
            "SDL Xbox 360/One types are reliable Xbox classifications");
        Check(Gamepads.KindOf(11) == Gamepads.Kind.GameCube, "SDL GameCube type");
        Check(ControllerLayout.Match(new[] { xbox }) == "xbox_original", "Xbox One selects existing Original Xbox scheme");
        Check(ControllerLayout.Match(new[] { cube }) == "gamecube", "GameCube selects GameCube scheme");
        Check(ControllerLayout.Match(new[] { xbox, new Gamepads.Pad("Another Xbox", Gamepads.Kind.Xbox) }) == "xbox_original",
            "Controllers agreeing on a scheme can match");
        Check(ControllerLayout.Match(new[] { xbox, cube }) == null, "Conflicting supported types do not guess an active device");
        Check(ControllerLayout.Match(Array.Empty<Gamepads.Pad>()) == null, "Disconnect does not select a default");
        foreach (var kind in new[] { Gamepads.Kind.Other, Gamepads.Kind.PlayStation, Gamepads.Kind.Switch })
        {
            var other = new Gamepads.Pad("Xbox One Controller", kind);
            Check(ControllerLayout.Match(new[] { other }) == null, "Unsupported type is not inferred from a controller name");
            Check(ControllerLayout.Match(new[] { xbox, other }) == null, "Mixed unsupported devices do not silently take an Xbox scheme");
        }
        for (int type = 0; type <= 12; type++)
            Check(Gamepads.KindOf(type) == (type is 2 or 3 ? Gamepads.Kind.Xbox :
                type is 4 or 5 or 6 ? Gamepads.Kind.PlayStation :
                type is 7 or 8 or 9 or 10 ? Gamepads.Kind.Switch :
                type == 11 ? Gamepads.Kind.GameCube : Gamepads.Kind.Other), "SDL type classification remains in step with game");

        string file = Path.Combine(paths.UserData, "SchemeTests.toml");
        var config = new GameConfig(file);
        Check(ControllerLayout.Automatic(config) && !ControllerLayout.NeedsConsent(config), "Unchosen default is automatic");
        Check(!ControllerLayout.ApplyDetected(config, Array.Empty<Gamepads.Pad>()) && !File.Exists(file), "No controller creates no config");
        config.SetString("controller", "a", "north@30,east");
        config.SetString("controller", "xbox_black", "right_paddle1,left_shoulder");
        config.SetString("future", "value", "keep");
        Check(ControllerLayout.ApplyDetected(config, new[] { xbox }), "First supported detection selects a layout");
        config.Save();
        config = new GameConfig(file);
        Check(ControllerLayout.Automatic(config) && config.Get("controller", "layout") == "xbox_original", "Auto mode and scheme persist");
        Check(!ControllerLayout.ApplyDetected(config, new[] { xbox }), "Steady connected device does not rewrite config");
        Check(!ControllerLayout.ApplyDetected(config, Array.Empty<Gamepads.Pad>()) &&
            config.Get("controller", "layout") == "xbox_original", "Disconnect preserves last choice");
        Check(ControllerLayout.ApplyDetected(config, new[] { cube }) && config.Get("controller", "layout") == "gamecube",
            "Supported hotplug follows type while automatic");
        Check(!ControllerLayout.ApplyDetected(config, new[] { xbox, cube }), "Conflicting hotplug keeps current scheme");
        Check(config.Get("controller", "a") == "north@30,east" &&
            config.Get("controller", "xbox_black") == "right_paddle1,left_shoulder" &&
            config.Get("future", "value") == "keep", "Auto choice preserves custom bindings, thresholds, second bindings and unknown settings");

        ControllerLayout.SetManual(config, "gamecube");
        config.Save();
        config = new GameConfig(file);
        Check(!ControllerLayout.Automatic(config) && !ControllerLayout.ApplyDetected(config, new[] { xbox }),
            "Manual choice of the same layout persists across restart and reconnect");
        ControllerLayout.SetAutomatic(config, true);
        Check(ControllerLayout.ApplyDetected(config, new[] { xbox }), "Explicitly re-enabling auto can switch again");
        config.SetString("controller", "layout", "gamecube");
        Check(ControllerLayout.ApplyDetected(config, new[] { xbox }) && !ControllerLayout.Automatic(config) &&
            config.Get("controller", "layout") == "gamecube", "Changed manual preference from an older game ends automatic mode");

        config.Remove("launcher", "auto_controller_layout");
        Check(ControllerLayout.NeedsConsent(config) && !ControllerLayout.Automatic(config), "Legacy saved choice requires opt-in");
        Check(!ControllerLayout.ApplyDetected(config, new[] { xbox }) && config.Get("controller", "layout") == "gamecube",
            "Legacy preference is never overwritten without consent");
        config.SetString("controller", "layout", "future-layout");
        Check(ControllerLayout.Describe(config.Get("controller", "layout")) == "(custom) future-layout" &&
            !ControllerLayout.ApplyDetected(config, new[] { xbox }), "Unknown layout is displayed and preserved");
        config.Set("launcher", "auto_controller_layout", "future-mode");
        Check(!ControllerLayout.Automatic(config) && !ControllerLayout.NeedsConsent(config), "Unknown intent value is not replaced by a default");
        ControllerLayout.SetAutomatic(config, true);
        Check(!ControllerLayout.ApplyDetected(config, new[] { new Gamepads.Pad("PS5", Gamepads.Kind.PlayStation) }) &&
            config.Get("controller", "layout") == "future-layout", "Opt-in does not invent a PlayStation scheme");
    }

    static void OpenControls(MainForm form)
    {
        form.CloseControllerBoard();
        var scene = All(form).OfType<MansionScene>().Single();
        for (int i = 0; i < 5 && scene.FocusedSign != "settings"; i++) scene.Navigate(B.Down);
        Press(form, B.Confirm);
        Click(form, Find<PlankButton>(form, "Controls"));
    }

    static void LayoutControls(MainForm form, DataPaths paths)
    {
        string saved = File.ReadAllText(paths.ConfigPath);
        var xbox = new[] { new Gamepads.Pad("Xbox One Controller", Gamepads.Kind.Xbox) };
        var cube = new[] { new Gamepads.Pad("GameCube adapter", Gamepads.Kind.GameCube) };
        try
        {
            using var foreign = new Form();
            using var modal = new Form { Owner = form };
            foreach (var target in new Form?[] { null, foreign, modal })
                form.RefreshControllerLayout(xbox, target, false);
            form.RefreshControllerLayout(xbox, form, true);
            form.Enabled = false;
            form.RefreshControllerLayout(xbox, form, false);
            form.Enabled = true;
            form.WindowState = FormWindowState.Minimized;
            form.RefreshControllerLayout(xbox, form, false);
            form.WindowState = FormWindowState.Normal;
            Check(File.ReadAllText(paths.ConfigPath) == saved, "Foreign/modal/no foreground, game, disabled and minimized states cannot migrate or auto-write");
            WithModal(() => form.RefreshControllerLayout(xbox, form, false), dialog =>
            {
                Check(Find<PlankButton>(dialog, "Keep my layout").Focused, "Legacy migration defaults to preserving a saved choice");
                Shot(dialog, "Layout-migration");
                Press(dialog, B.Back);
            });
            Check(File.ReadAllText(paths.ConfigPath) == saved, "Back defers migration without writing a preference");
            form.RefreshControllerLayout(xbox, form, false);
            Check(File.ReadAllText(paths.ConfigPath) == saved, "Cancelled migration does not ask again in the same start");

            using (var next = new MainForm(dataPaths: paths, enableServices: false))
            {
                next.Show();
                Application.DoEvents();
                WithModal(() => next.RefreshControllerLayout(xbox, next, false), dialog =>
                    Click(dialog, Find<PlankButton>(dialog, "Use auto-select")));
                Check(ControllerLayout.Automatic(new GameConfig(paths.ConfigPath)), "Legacy opt-in persists automatic mode");
                Check(new GameConfig(paths.ConfigPath).Get("controller", "layout") == "gamecube", "Opt-in rechecks ownership before changing layout");
                next.RefreshControllerLayout(xbox, next, true);
                Check(new GameConfig(paths.ConfigPath).Get("controller", "layout") == "gamecube", "Game ownership defers a pending auto selection");
                next.RefreshControllerLayout(xbox, next, false);
                OpenControls(next);
                var layout = (ThemedCombo)Field(next, "Layout");
                var automatic = Find<CheckBox>(next, "Auto-select controller layout");
                Check(layout.Text == "Original Xbox" && automatic.Checked, "Auto-detected scheme and mode are visible");
                Find<Label>(next, "Looking for controllers...").Text = "Offline sample: Xbox One Controller (Xbox)";
                ControllerNavigation.Focus(automatic);
                Shot(next, "Xbox-auto-layout");
                WithModal(() => Click(next, layout), dialog => Press(dialog, B.Back));
                Check(automatic.Checked, "Cancelling a scheme chooser keeps automatic mode");
                WithModal(() => Click(next, layout), dialog => Press(dialog, B.Confirm));
                Check(!automatic.Checked && !ControllerLayout.Automatic(new GameConfig(paths.ConfigPath)),
                    "Confirming an already-selected scheme is an explicit manual choice");
                next.RefreshControllerLayout(cube, next, false);
                Check(layout.Text == "Original Xbox", "Manual choice survives replacement-device hotplug");
                Click(next, automatic);
                next.RefreshControllerLayout(cube, next, false);
                Check(layout.Text == "GameCube" && automatic.Checked, "Controller-accessible checkbox re-enables matching");
                Shot(next, "Auto-layout");
                layout.SelectedIndex = 1;
                Check(!automatic.Checked && new GameConfig(paths.ConfigPath).Get("controller", "layout") == "xbox_original",
                    "Accessibility/native selection-change path still persists manual choices");
                Click(next, automatic);
                next.RefreshControllerLayout(cube, next, false);
                Check(automatic.Checked && layout.Text == "GameCube", "Programmatic auto refresh does not masquerade as accessibility/manual input");
                layout.CommitSelection(0);
                Check(!automatic.Checked, "Native/keyboard commit path also records same-value manual intent");
                next.Close();
            }
            Check(!ControllerLayout.Automatic(new GameConfig(paths.ConfigPath)), "Manual choice survives closing the launcher");
            var legacy = new GameConfig(paths.ConfigPath);
            legacy.Remove("launcher", "auto_controller_layout");
            legacy.SetString("controller", "layout", "future-layout");
            legacy.Save();
            using (var keep = new MainForm(dataPaths: paths, enableServices: false))
            {
                keep.Show();
                Application.DoEvents();
                WithModal(() => keep.RefreshControllerLayout(xbox, keep, false), dialog =>
                    Click(dialog, Find<PlankButton>(dialog, "Keep my layout")));
                keep.RefreshControllerLayout(cube, keep, false);
                OpenControls(keep);
                Check(Field(keep, "Layout").Text == "(custom) future-layout" &&
                    !ControllerLayout.NeedsConsent(new GameConfig(paths.ConfigPath)), "Keep preserves unknown layout and persists the one-time decision");
                keep.Close();
            }
        }
        finally
        {
            File.WriteAllText(paths.ConfigPath, saved);
            form.Activate();
        }
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

    static ComboBox Field(Control root, string label)
    {
        var name = Find<Label>(root, label);
        var table = (TableLayoutPanel)name.Parent!;
        return (ComboBox)table.GetControlFromPosition(1, table.GetPositionFromControl(name).Row)!;
    }

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
        var resolution = Field(form, "Resolution");
        var interpolation = Field(form, "Frame interpolation");
        var monitor = Field(form, "Fullscreen monitor");
        Check(resolution.SelectedItem?.ToString() == "8x (3840p)", "Numeric resolution matching from current main is preserved");
        Check(monitor.SelectedItem?.ToString() == "Saved monitor (not connected)", "Disconnected monitor remains selected");
        ControllerNavigation.Focus(interpolation);
        WithModal(() => Press(form, B.Confirm), dialog =>
        {
            Press(dialog, B.Down);
            Press(dialog, B.Back);
        });
        Check(new GameConfig(paths.ConfigPath).Get("video", "frame_interpolation_fps") == null, "Cancelling interpolation makes no config change");
        WithModal(() => Press(form, B.Confirm), dialog =>
        {
            Press(dialog, B.Down);
            Press(dialog, B.Confirm);
        });
        Check(new GameConfig(paths.ConfigPath).Get("video", "frame_interpolation_fps") == "120", "Controller applies interpolation through existing setting handler");
        Check(new GameConfig(paths.ConfigPath).Get("video", "resolution_multiplier") == "4", "Current-main interpolation resolution cap is preserved");
        Check(resolution.SelectedItem?.ToString() == "4x (1920p)" &&
            !resolution.Items.Cast<object>().Any(o => o.ToString() == "8x (3840p)"), "Resolution choices refresh after interpolation change");
        Check(new GameConfig(paths.ConfigPath).Get("video", "fullscreen_display") == "NOPE0000/gone", "Changing another field retains saved monitor id");
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
        var savedScroll = board.Body.AutoScrollPosition;
        Press(form, B.Back);
        Press(form, B.Confirm);
        Check(board.Body.AutoScrollPosition == savedScroll, "Reopening Extras preserves scroll position");
        Check(ControllerNavigation.Focused(form) is Control focus && ControllerNavigation.InViewport(focus, fully: true),
            "Reopened board focuses a fully visible control without jumping to top");
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
        bool? automatic = true;
        WithModal(() => automatic = owner.AskAutoLayout("Test controller", "gamecube", "xbox_original"), dialog =>
        {
            Check(ControllerNavigation.Targets(dialog).OfType<PlankButton>().Count() == 2, "Both layout choices reachable");
            Press(dialog, B.Back);
        });
        Check(automatic == null, "Back cancels automatic-layout prompt without changing game mappings");
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
