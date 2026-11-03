namespace ScoobyReMasked;

// Moving from Scooby-Doo: Refrightened. The FrightWorks zip unpacks to its own folder, so someone updating from
// Refrightened starts without their saves. Once, before the launcher window opens, offer to copy them from an old
// Refrightened folder next to this one: saves, settings, controls, mods and other texture packs, plus the game files
// so the disc needn't be chosen again. The old folder is only read.
static class RefrightenedImport
{
    // Game exe names of the Refrightened releases.
    static readonly string[] OldGameExes = { "Scooby-Doo Refrightened.exe", "Scooby-Doo Re-Masked.exe" };
    const string Mark = "imported-from-refrightened.txt";  // in UserData: asked once, whatever the answer
    // Rebuilt by the game or shipped with FrightWorks itself.
    static readonly string[] Skip = { "Logs", "Cache", "button_icons", Path.Combine("texture_replacements", "ScoobyHD") };

    public static void Offer(string root, string game, string title)
    {
        if (Path.GetFileName(game) != "data") return;  // release folders only, not the dev tree
        var userData = Path.Combine(game, "UserData");
        if (File.Exists(Path.Combine(userData, Mark)) || File.Exists(Path.Combine(userData, "Config.toml"))) return;
        var saves = Path.Combine(userData, "Saves");
        if (Directory.Exists(saves) && Directory.EnumerateFileSystemEntries(saves).Any()) return;
        var old = FindOld(root);
        if (old == null) return;
        var (oldRoot, oldUserData, oldDisc) = old.Value;

        var disc = Path.Combine(game, "Disc", "DATA");
        bool copyDisc = File.Exists(Path.Combine(oldDisc, "sys", "boot.bin")) && !File.Exists(Path.Combine(disc, "sys", "boot.bin"));
        var answer = MessageBox.Show(
            $"Found Scooby-Doo: Refrightened in\n{oldRoot}\n\nCopy its saves, settings, controls and mods into FrightWorks?" +
            (copyDisc ? " Its game files come too, so you don't have to choose the disc again." : "") +
            "\n\nThe old folder is left as it is.",
            title, MessageBoxButtons.YesNo, MessageBoxIcon.Question);
        Directory.CreateDirectory(userData);
        if (answer != DialogResult.Yes)
        {
            File.WriteAllText(Path.Combine(userData, Mark), $"Not copied from {oldRoot}{Environment.NewLine}");
            return;
        }

        var files = new List<(string from, string to)>();
        Collect(oldUserData, userData, oldUserData, files);
        var staging = disc + ".new";
        if (copyDisc && Directory.Exists(staging)) Directory.Delete(staging, true);
        if (copyDisc) Collect(oldDisc, staging, null, files);
        var error = Copy(files, title);
        if (error == null && copyDisc)
        {
            if (Directory.Exists(disc)) Directory.Delete(disc, true);
            Directory.Move(staging, disc);
            var cfg = new GameConfig(Path.Combine(userData, "Config.toml"));
            cfg.Set("paths", "dvd_root", "'../Disc/DATA'");  // relative to UserData, as the launcher writes it
            cfg.Save();
        }
        if (error != null)
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
            MessageBox.Show($"Copying stopped: {error.Message}\n\nCopy the old data\\UserData folder by hand if something is missing.",
                            title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        File.WriteAllText(Path.Combine(userData, Mark),
                          $"{(error == null ? "Copied" : "Partly copied")} from {oldRoot}{Environment.NewLine}");
    }

    // The most recently used Refrightened folder beside this one: release layout (data\) or the first one (Play\).
    static (string root, string userData, string disc)? FindOld(string root)
    {
        try
        {
            var here = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
            var parent = Directory.GetParent(here);
            if (parent == null) return null;
            (string, string, string)? best = null;
            var bestTime = DateTime.MinValue;
            foreach (var d in parent.EnumerateDirectories())
            {
                if (string.Equals(d.FullName, here, StringComparison.OrdinalIgnoreCase)) continue;
                foreach (var sub in new[] { "data", "Play" })
                {
                    var game = Path.Combine(d.FullName, sub);
                    var userData = Path.Combine(game, "UserData");
                    if (!Directory.Exists(userData) || !OldGameExes.Any(e => File.Exists(Path.Combine(game, e)))) continue;
                    var time = Directory.GetLastWriteTimeUtc(userData);
                    if (time <= bestTime) continue;
                    var disc = sub == "data" ? Path.Combine(game, "Disc", "DATA") : Path.Combine(d.FullName, "Disc", "DATA");
                    best = (d.FullName, userData, disc);
                    bestTime = time;
                }
            }
            return best;
        }
        catch (Exception) { return null; }
    }

    // Every file under dir, mapped to the same place under to. skipBase: leave out the Skip folders under it.
    static void Collect(string dir, string to, string? skipBase, List<(string, string)> files)
    {
        foreach (var f in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories))
        {
            var rel = Path.GetRelativePath(dir, f);
            if (skipBase != null && Skip.Any(s => rel.StartsWith(s + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))) continue;
            files.Add((f, Path.Combine(to, rel)));
        }
    }

    // Copies with a small progress window; never overwrites a file FrightWorks already has. Returns the error, if any.
    static Exception? Copy(List<(string from, string to)> files, string title)
    {
        Exception? error = null;
        using var form = new Form
        {
            Text = title, FormBorderStyle = FormBorderStyle.FixedDialog, ControlBox = false, StartPosition = FormStartPosition.CenterScreen,
            ClientSize = new Size(480, 96), Font = new Font("Segoe UI", 10f),
        };
        var label = new Label { Text = "Copying from Refrightened...", Location = new Point(16, 14), AutoSize = true };
        var bar = new ProgressBar { Location = new Point(16, 48), Size = new Size(448, 20), Maximum = Math.Max(1, files.Count) };
        form.Controls.Add(label);
        form.Controls.Add(bar);
        form.Shown += async (_, _) =>
        {
            var progress = new Progress<int>(i =>
            {
                bar.Value = Math.Min(i, bar.Maximum);
                label.Text = $"Copying from Refrightened: {i}/{files.Count} files";
            });
            try
            {
                await Task.Run(() =>
                {
                    for (int i = 0; i < files.Count; i++)
                    {
                        var (from, to) = files[i];
                        Directory.CreateDirectory(Path.GetDirectoryName(to)!);
                        if (!File.Exists(to)) File.Copy(from, to);
                        if (i % 20 == 0) ((IProgress<int>)progress).Report(i);
                    }
                });
            }
            catch (Exception e) { error = e; }
            form.Close();
        };
        form.ShowDialog();
        return error;
    }
}
