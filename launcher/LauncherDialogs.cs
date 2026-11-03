namespace ScoobyReMasked;

// Standard accessible WinForms controls keep keyboard/mouse working alongside the controller.
// The file browser does not automate the Windows shell or require a desktop virtual keyboard.
static class LauncherDialogs
{
    static Form Dialog(string title) => new()
    {
        Text = title, BackColor = Theme.BoardBottom, ForeColor = Theme.Cream, Font = Theme.Body,
        StartPosition = FormStartPosition.CenterParent, ShowInTaskbar = false,
        MinimizeBox = false, MaximizeBox = false, FormBorderStyle = FormBorderStyle.FixedDialog,
        AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = Theme.S(16, 12, 16, 12),
    };

    static FlowLayoutPanel Body(Form form)
    {
        var body = new FlowLayoutPanel
        {
            AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false,
            BackColor = Theme.BoardBottom, Margin = Padding.Empty,
        };
        form.Controls.Add(body);
        return body;
    }

    static Button Button(string text, DialogResult result = DialogResult.None) => new()
    {
        Text = text, AutoSize = true, MinimumSize = Theme.S(90, 34), DialogResult = result,
        FlatStyle = FlatStyle.Flat, BackColor = Theme.PurpleDeep, ForeColor = Theme.Cream,
        Margin = Theme.S(3, 5, 6, 3),
    };

    internal static void Show(Form dialog, IWin32Window? owner = null, Control? initialFocus = null)
    {
        dialog.KeyPreview = true;
        dialog.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Escape && dialog.CancelButton == null && dialog.ControlBox)
            { dialog.Close(); e.Handled = e.SuppressKeyPress = true; }
        };
        dialog.Shown += (_, _) =>
        {
            var first = initialFocus ?? ControllerNavigation.Targets(dialog).FirstOrDefault();
            if (first != null) ControllerNavigation.Focus(first);
        };
        dialog.ShowDialog(owner ?? Form.ActiveForm);
    }

    public static DialogResult Show(IWin32Window? owner, string text, string title,
        MessageBoxButtons buttons = MessageBoxButtons.OK, MessageBoxIcon icon = MessageBoxIcon.None)
    {
        using var form = Dialog(title);
        form.AccessibleDescription = icon == MessageBoxIcon.None ? text : icon + ": " + text;
        var body = Body(form);
        body.Controls.Add(Theme.Note(text, 600));
        var row = new FlowLayoutPanel { AutoSize = true, WrapContents = false };
        var choices = buttons switch
        {
            MessageBoxButtons.YesNo => new[] { DialogResult.Yes, DialogResult.No },
            MessageBoxButtons.YesNoCancel => new[] { DialogResult.Yes, DialogResult.No, DialogResult.Cancel },
            MessageBoxButtons.OKCancel => new[] { DialogResult.OK, DialogResult.Cancel },
            _ => new[] { DialogResult.OK },
        };
        foreach (var choice in choices)
        {
            var button = Button(choice.ToString(), choice);
            row.Controls.Add(button);
            // Questions start on the non-destructive answer; accepting requires an explicit press.
            if (choice is DialogResult.Cancel or DialogResult.No or DialogResult.OK) form.CancelButton = button;
        }
        body.Controls.Add(row);
        form.AcceptButton = (IButtonControl)row.Controls[0];
        Show(form, owner, form.CancelButton as Control);
        return form.DialogResult;
    }

    public static DialogResult Show(string text, string title,
        MessageBoxButtons buttons = MessageBoxButtons.OK, MessageBoxIcon icon = MessageBoxIcon.None) =>
        Show(Form.ActiveForm, text, title, buttons, icon);

    public static int Choose(IWin32Window owner, string[] items, int selected)
    {
        using var form = Dialog("Choose a setting");
        var body = Body(form);
        body.Controls.Add(Theme.Note("Up/down: choose   South: apply   East: cancel   Bumpers: next/previous control"));
        var list = new ListBox
        {
            Size = Theme.S(480, 280), BackColor = Theme.Field, ForeColor = Theme.Cream,
            Font = Theme.Body, IntegralHeight = false, AccessibleName = "Setting choices",
        };
        list.Items.AddRange(items);
        if (items.Length > 0) list.SelectedIndex = Math.Clamp(selected, 0, items.Length - 1);
        body.Controls.Add(list);
        var row = new FlowLayoutPanel { AutoSize = true };
        var apply = Button("Apply", DialogResult.OK);
        row.Controls.Add(apply);
        var cancel = Button("Cancel", DialogResult.Cancel);
        row.Controls.Add(cancel);
        body.Controls.Add(row);
        form.AcceptButton = apply;
        form.CancelButton = cancel;
        list.DoubleClick += (_, _) => apply.PerformClick();
        Show(form, owner);
        return form.DialogResult == DialogResult.OK ? list.SelectedIndex : -1;
    }

    public static string? PickPath(IWin32Window owner, string title, string initial, bool folder)
    {
        using var form = Dialog(title);
        var body = Body(form);
        body.Controls.Add(Theme.Note("Up/down: browse   South: open/select   East: cancel   Bumpers: next/previous control"));
        var path = new TextBox { Width = Theme.S(560), Text = initial, AccessibleName = "Folder path" };
        body.Controls.Add(path);
        var list = new ListBox
        {
            Size = Theme.S(560, 300), BackColor = Theme.Field, ForeColor = Theme.Cream,
            IntegralHeight = false, Font = Theme.Body, AccessibleName = folder ? "Folders" : "Folders and disc images",
        };
        body.Controls.Add(list);
        var status = Theme.Note("", 560);
        body.Controls.Add(status);
        var entries = new List<(string Label, string Path, bool Folder)>();
        string current = "", result = "";
        void Load(string directory)
        {
            try
            {
                if (!folder && File.Exists(directory))
                { result = Path.GetFullPath(directory); form.DialogResult = DialogResult.OK; return; }
                var next = new List<(string Label, string Path, bool Folder)>();
                if (directory.Length == 0)
                    next.AddRange(DriveInfo.GetDrives().Select(d => (d.Name, d.Name, true)));
                else
                {
                    directory = Path.GetFullPath(directory);
                    var parent = Directory.GetParent(directory);
                    next.Add(("[..] Parent folder", parent?.FullName ?? "", true));
                    next.AddRange(Directory.GetDirectories(directory).OrderBy(d => d, StringComparer.OrdinalIgnoreCase)
                        .Select(d => ("[Folder] " + Path.GetFileName(d), d, true)));
                    if (!folder)
                        next.AddRange(Directory.GetFiles(directory).Where(f => new[] { ".iso", ".gcm", ".ciso" }
                            .Contains(Path.GetExtension(f), StringComparer.OrdinalIgnoreCase))
                            .OrderBy(f => f, StringComparer.OrdinalIgnoreCase).Select(f => (Path.GetFileName(f), f, false)));
                }
                current = directory;
                path.Text = current;
                entries = next;
                list.Items.Clear();
                list.Items.AddRange(entries.Select(e => e.Label).ToArray());
                if (entries.Count > 0) list.SelectedIndex = 0;
                status.Text = folder ? "Select this folder chooses the folder shown above." : "Showing .iso, .gcm and .ciso images.";
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
            { status.Text = "Couldn't browse that folder: " + e.Message; }
        }
        void Activate()
        {
            if (list.SelectedIndex < 0) return;
            var entry = entries[list.SelectedIndex];
            if (entry.Folder) Load(entry.Path);
            else { result = entry.Path; form.DialogResult = DialogResult.OK; }
        }
        list.Tag = (Action)Activate;
        list.DoubleClick += (_, _) => Activate();
        list.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Enter) { Activate(); e.Handled = e.SuppressKeyPress = true; }
        };
        var row = new FlowLayoutPanel { AutoSize = true, MaximumSize = Theme.S(570, 0) };
        var go = Button("Go to path");
        go.Click += (_, _) => Load(path.Text);
        row.Controls.Add(go);
        var drives = Button("Drives");
        drives.Click += (_, _) => Load("");
        row.Controls.Add(drives);
        var select = Button(folder ? "Select this folder" : "Open image");
        select.Click += (_, _) =>
        {
            if (folder && current.Length > 0) { result = current; form.DialogResult = DialogResult.OK; }
            else Activate();
        };
        path.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Enter) { Load(path.Text); e.Handled = e.SuppressKeyPress = true; }
        };
        row.Controls.Add(select);
        if (folder)
        {
            var create = Button("New folder");
            create.Click += (_, _) =>
            {
                var name = EditText(form, "");
                if (string.IsNullOrWhiteSpace(name)) return;
                if (current.Length == 0 || name is "." or ".." || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
                { status.Text = "Choose a parent folder and enter a single valid folder name."; return; }
                try { var directory = Directory.CreateDirectory(Path.Combine(current, name)); Load(directory.FullName); }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
                { status.Text = "Couldn't create that folder: " + e.Message; }
            };
            row.Controls.Add(create);
        }
        var cancel = Button("Cancel", DialogResult.Cancel);
        row.Controls.Add(cancel);
        body.Controls.Add(row);
        form.AcceptButton = select;
        form.CancelButton = cancel;
        Load(Directory.Exists(initial) ? initial : "");
        Show(form, owner, list);
        return form.DialogResult == DialogResult.OK ? result : null;
    }

    public static string? EditText(IWin32Window owner, string initial)
    {
        using var form = Dialog("Enter text");
        var body = Body(form);
        body.Controls.Add(Theme.Note("Directions: move between keys   South: press key   East: cancel"));
        var text = new TextBox { Text = initial, Width = Theme.S(540), AccessibleName = "Text" };
        body.Controls.Add(text);
        // A combo of characters keeps this usable at high DPI without a very wide keyboard.
        var keys = new FlowLayoutPanel { AutoSize = true, MaximumSize = Theme.S(550, 0), WrapContents = true };
        const string characters = "abcdefghijklmnopqrstuvwxyz0123456789 ._-:\\/ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        foreach (char character in characters)
        {
            char key = character;
            var button = Button(key == ' ' ? "Space" : key.ToString());
            button.MinimumSize = Theme.S(42, 34);
            button.Click += (_, _) => { text.SelectedText = key.ToString(); };
            keys.Controls.Add(button);
        }
        text.Tag = (Action)(() => ControllerNavigation.Focus(keys.Controls[0]));
        body.Controls.Add(keys);
        var row = new FlowLayoutPanel { AutoSize = true };
        var erase = Button("Backspace");
        erase.Click += (_, _) =>
        {
            if (text.SelectionLength > 0) text.SelectedText = "";
            else if (text.SelectionStart > 0) { int i = text.SelectionStart; text.Text = text.Text.Remove(i - 1, 1); text.SelectionStart = i - 1; }
        };
        row.Controls.Add(erase);
        var done = Button("Done", DialogResult.OK);
        row.Controls.Add(done);
        var cancel = Button("Cancel", DialogResult.Cancel);
        row.Controls.Add(cancel);
        body.Controls.Add(row);
        form.AcceptButton = done;
        form.CancelButton = cancel;
        text.SelectionStart = text.TextLength;
        Show(form, owner);
        return form.DialogResult == DialogResult.OK ? text.Text : null;
    }
}
