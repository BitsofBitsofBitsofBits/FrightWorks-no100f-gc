using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Controls.Documents;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;

namespace ScoobyReMasked;

// The Avalonia application: Fluent's dark theme recoloured in the launcher's purples, lime and cream, and the start
// (an offer to copy an earlier version's files first, then the launcher window).
sealed class App : Application
{
    public static WindowIcon? Icon
    {
        get
        {
            using var s = Skin.Resource("frightworks.ico");
            return s == null ? null : new WindowIcon(s);
        }
    }

    public override void Initialize()
    {
        RequestedThemeVariant = ThemeVariant.Dark;
        Styles.Add(new FluentTheme());
        var r = Resources;
        IBrush field = Skin.Brush(Skin.Field), hover = Skin.Brush(Skin.FieldHover), purple = Skin.Brush(Skin.Purple),
               purpleLight = Skin.Brush(Skin.Light(Skin.Purple, 0.25f)), deep = Skin.Brush(Skin.PurpleDeep),
               cream = Skin.Brush(Skin.Cream), dim = Skin.Brush(Skin.CreamDim);
        void Set(IBrush brush, params string[] keys) { foreach (var key in keys) r[key] = brush; }
        // The accent (focus rings, ticked boxes) in the signs' purple.
        r["SystemAccentColor"] = Skin.Purple;
        r["SystemAccentColorLight1"] = Skin.Light(Skin.Purple, 0.15f);
        r["SystemAccentColorLight2"] = Skin.Light(Skin.Purple, 0.3f);
        r["SystemAccentColorLight3"] = Skin.Light(Skin.Purple, 0.45f);
        r["SystemAccentColorDark1"] = Skin.PurpleDeep;
        r["SystemAccentColorDark2"] = Skin.PurpleDeep;
        r["SystemAccentColorDark3"] = Skin.PurpleDeep;
        // Drop-down lists: the dark field, cream text, purple for the highlighted choice.
        Set(field, "ComboBoxBackground", "ComboBoxBackgroundUnfocused", "ComboBoxBackgroundDisabled", "ComboBoxDropDownBackground", "ComboBoxItemBackground");
        Set(hover, "ComboBoxBackgroundPointerOver", "ComboBoxBackgroundPressed");
        Set(deep, "ComboBoxBorderBrush", "ComboBoxBorderBrushDisabled", "ComboBoxDropDownBorderBrush", "ComboBoxBackgroundBorderBrushUnfocused");
        Set(purple, "ComboBoxBorderBrushPointerOver", "ComboBoxBorderBrushPressed", "ComboBoxBackgroundBorderBrushFocused");
        Set(cream, "ComboBoxForeground", "ComboBoxForegroundFocused", "ComboBoxForegroundFocusedPressed", "ComboBoxDropDownGlyphForeground",
            "ComboBoxDropDownGlyphForegroundFocused", "ComboBoxDropDownGlyphForegroundFocusedPressed", "ComboBoxItemForeground",
            "ComboBoxItemForegroundPointerOver", "ComboBoxItemForegroundPressed", "ComboBoxItemForegroundSelected",
            "ComboBoxItemForegroundSelectedPointerOver", "ComboBoxItemForegroundSelectedPressed");
        Set(dim, "ComboBoxForegroundDisabled", "ComboBoxDropDownGlyphForegroundDisabled", "ComboBoxItemForegroundDisabled");
        Set(hover, "ComboBoxItemBackgroundPointerOver", "ComboBoxItemBackgroundPressed");
        Set(purple, "ComboBoxItemBackgroundSelected", "ComboBoxItemBackgroundSelectedPointerOver", "ComboBoxItemBackgroundSelectedPressed");
        // Check boxes (the mods list): cream text, a cream outline, purple when ticked.
        Set(cream, "CheckBoxForegroundUnchecked", "CheckBoxForegroundChecked", "CheckBoxForegroundUncheckedPointerOver",
            "CheckBoxForegroundCheckedPointerOver", "CheckBoxForegroundUncheckedPressed", "CheckBoxForegroundCheckedPressed",
            "CheckBoxCheckGlyphForegroundChecked", "CheckBoxCheckGlyphForegroundCheckedPointerOver", "CheckBoxCheckGlyphForegroundCheckedPressed");
        Set(dim, "CheckBoxCheckBackgroundStrokeUnchecked", "CheckBoxCheckBackgroundStrokeUncheckedPointerOver", "CheckBoxCheckBackgroundStrokeUncheckedPressed");
        Set(purple, "CheckBoxCheckBackgroundFillChecked", "CheckBoxCheckBackgroundStrokeCheckedPointerOver", "CheckBoxCheckBackgroundStrokeCheckedPressed");
        Set(purpleLight, "CheckBoxCheckBackgroundFillCheckedPointerOver", "CheckBoxCheckBackgroundFillCheckedPressed");
        // Scroll bars: a purple thumb on a dark track.
        r["ScrollBarThumbBackgroundColor"] = Skin.Purple;
        r["ScrollBarPanningThumbBackgroundColor"] = Skin.Purple;
        Set(purpleLight, "ScrollBarThumbFillPointerOver", "ScrollBarThumbFillPressed");
        Set(Skin.Brush(Skin.Argb(70, 0, 0, 0)), "ScrollBarBackground", "ScrollBarBackgroundPointerOver", "ScrollBarTrackFill", "ScrollBarTrackFillPointerOver");
        Set(Brushes.Transparent, "ScrollBarTrackStroke", "ScrollBarTrackStrokePointerOver", "ScrollBarButtonBackground");
        Set(dim, "ScrollBarButtonArrowForeground");
        Set(cream, "ScrollBarButtonArrowForegroundPointerOver", "ScrollBarButtonArrowForegroundPressed");

        // The body face everywhere, drop-down lists' popups included.
        Styles.Add(new Style(x => x.Is<TopLevel>())
        {
            Setters =
            {
                new Setter(TextElement.FontFamilyProperty, Skin.BodyFamily),
                new Setter(TextElement.FontSizeProperty, Skin.BodySize),
                new Setter(TextElement.ForegroundProperty, cream),
            },
        });
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.ShutdownMode = ShutdownMode.OnExplicitShutdown;
            var args = desktop.Args ?? Array.Empty<string>();
            Dispatcher.UIThread.Post(() => _ = Start(desktop, args));
        }
        base.OnFrameworkInitializationCompleted();
    }

    // "--shots <dir>": draw every page to <dir>/<page>.png and exit. "--selftest": build every page, then exit 0.
    static async Task Start(IClassicDesktopStyleApplicationLifetime desktop, string[] args)
    {
        try
        {
            int shots = Array.IndexOf(args, "--shots");
            bool selftest = args.Contains("--selftest");
            if (shots < 0 && !selftest)
            {
                var paths = DataPaths.Find();
                if (paths.Portable) paths.RemoveOldShippedCopies();
                await DataImport.OfferAtStart(paths, MainWindow.AppTitle);
            }
            var window = new MainWindow(shots >= 0 && shots + 1 < args.Length ? args[shots + 1] : null);
            desktop.MainWindow = window;
            desktop.ShutdownMode = ShutdownMode.OnMainWindowClose;
            if (selftest) window.Opened += (_, _) => window.Close();
            window.Show();
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(e);
            desktop.Shutdown(1);
        }
    }
}
