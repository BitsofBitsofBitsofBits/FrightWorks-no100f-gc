using System.Runtime.InteropServices;

namespace ScoobyReMasked;

// Dispatches to real WinForms controls, never to the desktop input queue.
sealed class ControllerNavigation : IDisposable
{
    readonly Form owner;
    readonly Gamepads gamepads;
    readonly System.Windows.Forms.Timer timer = new() { Interval = 16 };
    readonly Dictionary<uint, ControllerInput> devices = new();
    IntPtr context;
    bool dispatching;

    public ControllerNavigation(Form owner, Gamepads gamepads)
    {
        this.owner = owner;
        this.gamepads = gamepads;
        timer.Tick += (_, _) => Poll();
    }

    public void Start() => timer.Start();
    public void Dispose() => timer.Dispose();

    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window, int message, IntPtr wParam, IntPtr lParam);

    internal static Form? ForegroundForm => Control.FromHandle(GetForegroundWindow()) as Form;

    internal static bool Owns(Form owner, Form? target)
    {
        for (var form = target; form != null; form = form.Owner)
            if (form == owner) return true;
        return false;
    }

    void Poll()
    {
        // A synchronous modal loop can tick this timer again while Dispatch is still on the stack.
        // It must be allowed to navigate the dialog, but never repeat its opening action.
        var target = ForegroundForm;
        var action = ReadInput(target, gamepads.Input, Environment.TickCount64);
        if (action == LauncherButtons.None || target == null) return;
        if (dispatching && target == owner) return;
        bool wasDispatching = dispatching;
        dispatching = true;
        try { Dispatch(target, action); }
        finally
        {
            dispatching = wasDispatching;
            // Even a very short dialog (or a board transition) requires a fresh neutral sample.
            if (action is LauncherButtons.Confirm or LauncherButtons.Back) Reset();
        }
    }

    internal LauncherButtons ReadInput(Form? target, IReadOnlyList<ControllerSample> samples, long now)
    {
        if (!Owns(owner, target) || target == null || !target.Enabled || !target.Visible ||
            target.WindowState == FormWindowState.Minimized)
        {
            Reset();
            return LauncherButtons.None;
        }
        if (context != target.Handle) { Reset(); context = target.Handle; }
        foreach (var id in devices.Keys.Where(id => !samples.Any(s => s.Id == id)).ToArray()) devices.Remove(id);
        LauncherButtons action = LauncherButtons.None;
        foreach (var sample in samples)
        {
            if (!devices.TryGetValue(sample.Id, out var input)) devices[sample.Id] = input = new ControllerInput();
            var next = input.Read(sample.Buttons, now);
            if (action == LauncherButtons.None) action = next;
        }
        return action;
    }

    internal void Reset()
    {
        foreach (var input in devices.Values) input.Reset();
        context = IntPtr.Zero;
    }

    internal static IEnumerable<Control> Targets(Control root)
    {
        foreach (var c in root.Controls.Cast<Control>().OrderBy(c => c.TabIndex))
        {
            if (!c.Visible || !c.Enabled) continue;
            if (c.TabStop && c.CanSelect && c is PlankButton or ButtonBase or ComboBox or ListBox or TextBoxBase or TrackBar or NumericUpDown)
                yield return c;
            else
                foreach (var child in Targets(c)) yield return child;
        }
    }

    internal static Control? Focused(Control root)
    {
        if (root.Focused) return root;
        foreach (Control child in root.Controls)
            if (child.ContainsFocus) return Focused(child);
        return null;
    }

    internal static void Focus(Control control)
    {
        control.Select();
        // Reveal nested lists as well as the board itself.
        for (var parent = control.Parent; parent != null; parent = parent.Parent)
            if (parent is ScrollableControl { AutoScroll: true } scroll) scroll.ScrollControlIntoView(control);
        control.Invalidate();
    }

    internal static bool InViewport(Control control, bool fully = false)
    {
        var bounds = control.RectangleToScreen(control.ClientRectangle);
        for (var parent = control.Parent; parent != null; parent = parent.Parent)
            if (parent is ScrollableControl { AutoScroll: true })
            {
                var viewport = parent.RectangleToScreen(parent.ClientRectangle);
                if (fully ? !viewport.Contains(bounds) : !viewport.IntersectsWith(bounds)) return false;
            }
        return true;
    }

    internal static void Dispatch(Form form, LauncherButtons action)
    {
        // Clear Windows' "hide focus" UI state for controller navigation, without synthetic keyboard input.
        SendMessage(form.Handle, 0x0127 /* WM_CHANGEUISTATE */, (IntPtr)(2 | (1 << 16)), IntPtr.Zero);
        if (form is MainForm main && main.NavigateScene(action)) return;
        var focused = Focused(form);
        if (focused is ComboBox { DroppedDown: true } open)
        {
            if (action is LauncherButtons.Back or LauncherButtons.Confirm) open.DroppedDown = false;
            else if (action is LauncherButtons.Up or LauncherButtons.Down)
                open.SelectedIndex = Math.Clamp(open.SelectedIndex + (action == LauncherButtons.Up ? -1 : 1), 0, open.Items.Count - 1);
            return;
        }
        if (action == LauncherButtons.Back)
        {
            if (form is MainForm launcher) launcher.CloseControllerBoard();
            else if (form.CancelButton is IButtonControl cancel) cancel.PerformClick();
            else if (form.ControlBox) form.Close();
            return;
        }
        if (action == LauncherButtons.Confirm)
        {
            if (focused != null && !InViewport(focused)) { Focus(focused); return; }
            switch (focused)
            {
                case PlankButton plank: plank.PerformClick(); break;
                case Button button: button.PerformClick(); break;
                case CheckBox check: check.Checked = !check.Checked; break;
                case RadioButton radio: radio.Checked = true; break;
                case ComboBox combo:
                    int choice = LauncherDialogs.Choose(form, combo.Items.Cast<object>().Select(o => o.ToString() ?? "").ToArray(), combo.SelectedIndex);
                    if (choice >= 0)
                    {
                        if (combo is ThemedCombo themed) themed.CommitSelection(choice);
                        else combo.SelectedIndex = choice;
                    }
                    break;
                case TextBox { Tag: Action edit }:
                    edit();
                    break;
                case TextBox text:
                    var value = LauncherDialogs.EditText(form, text.Text);
                    if (value != null) text.Text = value;
                    break;
                case ListBox list:
                    if (list.Tag is Action activate) activate();
                    else form.AcceptButton?.PerformClick();
                    break;
            }
            return;
        }
        bool backwards = action is LauncherButtons.Up or LauncherButtons.Left or LauncherButtons.Previous;
        int step = backwards ? -1 : 1;
        if (action is LauncherButtons.ScrollUp or LauncherButtons.ScrollDown)
        {
            var scroll = focused?.Parent;
            while (scroll != null && scroll is not ScrollableControl { AutoScroll: true }) scroll = scroll.Parent;
            if (scroll is ScrollableControl page)
            {
                page.AutoScrollPosition = new Point(-page.AutoScrollPosition.X,
                    Math.Max(0, -page.AutoScrollPosition.Y + (action == LauncherButtons.ScrollUp ? -1 : 1) * Theme.S(70)));
                var visible = Targets(page).FirstOrDefault(c => InViewport(c));
                if (visible != null) Focus(visible);
            }
            return;
        }
        if (action is not (LauncherButtons.Previous or LauncherButtons.Next))
        {
            if (focused is ListBox list && action is LauncherButtons.Up or LauncherButtons.Down)
            {
                if (list.Items.Count > 0) list.SelectedIndex = Math.Clamp(list.SelectedIndex + step, 0, list.Items.Count - 1);
                return;
            }
            if (focused is TrackBar slider && action is LauncherButtons.Left or LauncherButtons.Right)
            { slider.Value = Math.Clamp(slider.Value + step * slider.SmallChange, slider.Minimum, slider.Maximum); return; }
            if (focused is NumericUpDown number && action is LauncherButtons.Left or LauncherButtons.Right)
            { number.Value = Math.Clamp(number.Value + step * number.Increment, number.Minimum, number.Maximum); return; }
        }
        var targets = Targets(form).ToArray();
        if (targets.Length == 0) return;
        int index = Array.IndexOf(targets, focused);
        Focus(targets[index < 0 ? 0 : (index + step + targets.Length) % targets.Length]);
    }
}
