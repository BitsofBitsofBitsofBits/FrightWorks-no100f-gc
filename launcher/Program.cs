using Avalonia;

namespace ScoobyReMasked;

static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        // Velopack (the installed game): during install, update and uninstall it starts this exe with its own arguments,
        // and Run() handles them and exits. Nothing to do there: the player's files live outside the install folder, so
        // an uninstall leaves them (DataPaths). In the zip and the dev tree this does nothing.
        Velopack.VelopackApp.Build().Run();

        // Headless: "--extract <image> <outdir>" writes <outdir>/extract.log; exit 0 ok, 2 holes, 3 wrong disc, 1 error.
        if (args.Length == 3 && args[0] == "--extract")
        {
            var log = new List<string>();
            try
            {
                using var disc = GcDisc.Open(args[1]);
                log.Add($"game {disc.GameId} rev {disc.Revision} \"{disc.Title}\" problem=\"{disc.Problem()}\"");
                if (disc.Problem().Length > 0) { Write(args[2], log); return 3; }
                int n = disc.Extract(args[2], (done, total, name) => { if (done % 500 == 0) log.Add($"{done}/{total} {name}"); });
                log.Add($"extracted {n} files, holes {disc.Holes}");
                Write(args[2], log);
                return disc.Holes == 0 ? 0 : 2;
            }
            catch (Exception e)
            {
                log.Add("ERROR " + e);
                Write(args[2], log);
                return 1;
            }
        }
        return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    // Also used by the layout tests and Avalonia's designer.
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>().UsePlatformDetect().WithInterFont();

    static void Write(string dir, List<string> log)
    {
        Directory.CreateDirectory(dir);
        File.WriteAllLines(Path.Combine(dir, "extract.log"), log);
    }
}
