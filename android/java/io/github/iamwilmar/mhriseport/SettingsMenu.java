package io.github.iamwilmar.mhriseport;

import android.app.Dialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.function.IntConsumer;
import java.util.function.IntSupplier;


// Settings menu drawn over the game, styled after the game's own menus: dark wine panels with a bronze border, gold
// titles over the red and green rule, and a "?" on each row that expands its help text. Opened from the gear button
// of the touch controls. Sections on the left, the selected section's rows on the right.
// Control settings are saved and applied right away (GameActivity.refresh). Display, Graphics, Performance and Debug
// are the game's switches: empty files in the game folder (runtime/src/nnsdk/nvn/flags.h) that the game checks once a
// second. The ones only read at start-up say so in their help.
final class SettingsMenu {
    static final int PANEL = 0xF0201010, ROW = 0xFF2C1614, EDGE = 0xFFC49646, GOLD = 0xFFE2B65C, CREAM = 0xFFF0E4C8,
            DIM = 0xFFB09C84, RED = 0xFFB0221E, GREEN = 0xFF508E36;

    private static int section = 0;  // last one opened (for the life of the process)

    private final GameActivity activity;
    private final Controls cfg;
    private final Dialog dialog;
    private final LinearLayout list, sections;
    private final ScrollView scroll;
    private final TextView sub;
    private final File folder;  // the game folder (files/carpeta.txt), where the switches live
    private final float scale;  // menu size (Display > Menu size): text and spacing, 0.8 by default
    static final String PREFS = "menu", SCALE_KEY = "escala";
    static final int SCALE_DEFAULT = 80;

    SettingsMenu(GameActivity activity, Controls cfg) {
        this.activity = activity;
        this.cfg = cfg;
        this.scale = menuScale(activity) / 100f;
        dialog = new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);

        LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackground(plate(PANEL, 2, 0));

        // top bar: "SETTINGS ◆ <section>" and the X, with the red and green rule under it
        LinearLayout top = new LinearLayout(activity);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setPadding(dp(18), dp(10), dp(10), dp(10));
        TextView title = text("SETTINGS", 22, GOLD, true);
        title.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        sub = text("", 17, CREAM, false);
        TextView close = text("✕", 22, CREAM, false);
        close.setPadding(dp(14), dp(4), dp(14), dp(4));
        close.setBackground(plate(0xFF1A0C09, 1, 2));
        close.setOnClickListener(v -> dialog.dismiss());
        top.addView(title);
        top.addView(sub, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        top.addView(close);
        root.addView(top);
        root.addView(rule(RED, 2));
        root.addView(rule(GREEN, 1));

        folder = gameFolder(activity);
        LinearLayout body = new LinearLayout(activity);
        sections = new LinearLayout(activity);
        sections.setOrientation(LinearLayout.VERTICAL);
        sections.setPadding(dp(10), dp(8), dp(6), dp(10));
        ScrollView side = new ScrollView(activity);
        side.addView(sections);
        body.addView(side, new LinearLayout.LayoutParams(dp(176), ViewGroup.LayoutParams.MATCH_PARENT));
        View divider = new View(activity);
        divider.setBackgroundColor(EDGE);
        body.addView(divider, new LinearLayout.LayoutParams(dp(1), ViewGroup.LayoutParams.MATCH_PARENT));
        list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(dp(12), dp(6), dp(12), dp(12));
        scroll = new ScrollView(activity);
        scroll.addView(list);
        body.addView(scroll, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        root.addView(body, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        build();

        dialog.setContentView(root);
        Window w = dialog.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            DisplayMetrics m = activity.getResources().getDisplayMetrics();
            w.setLayout(Math.min(dp(940), (int) (m.widthPixels * 0.94f)), (int) (m.heightPixels * 0.92f));
        }
    }

    void show() { dialog.show(); }

    private void changed() { activity.refresh(); }

    // ---- sections -------------------------------------------------------------------------------------------------------

    private String[] sectionNames() {
        return new String[] {"Display", "Graphics", "Performance",
                "Touch controls", "Combos", "Controller",
                "Debug"};
    }

    private void build() {
        String[] names = sectionNames();
        sections.removeAllViews();
        for (int i = 0; i < names.length; i++) {
            final int k = i;
            boolean on = k == section;
            TextView b = text(names[k], 15, on ? GOLD : CREAM, on);
            b.setPadding(dp(12), dp(11), dp(8), dp(11));
            b.setBackground(plate(on ? 0xFF3E2616 : ROW, on ? 2 : 1, 2));
            b.setOnClickListener(v -> {
                section = k;
                build();
                scroll.scrollTo(0, 0);
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.topMargin = dp(4);
            sections.addView(b, lp);
        }
        sub.setText("  ◆  " + names[section]);
        list.removeAllViews();
        switch (section) {
            case 0: buildDisplay(); break;
            case 1: buildGraphics(); break;
            case 2: buildPerformance(); break;
            case 3: buildTouch(); break;
            case 4: buildCombos(); break;
            case 5: buildController(); break;
            default: buildDebug(); break;
        }
    }

    private void buildDisplay() {
        header("Menu");
        number("Menu size", "The size of this menu's text and buttons.", 60, 130, 5, " %", () -> menuScale(activity), v -> {
                    try {
                        activity.getSharedPreferences(PREFS, android.content.Context.MODE_PRIVATE).edit().putInt(SCALE_KEY, v).apply();
                    } catch (RuntimeException ignored) {
                        // (no preferences: keep the current size)
                    }
                    dialog.dismiss();
                    new SettingsMenu(activity, cfg).show();  // (sizes are fixed at build time, so reopen with the new one)
                });
        header("Resolution");
        note("The game's 3D resolution. On the handheld console it's 960x540 inside a 720p picture, which is then scaled "
                        + "to the screen. Higher: sharper, and more work for the GPU.");
        final String[] files = {null, "resolution_576", "resolution_648", "resolution_720"};
        choice("Base resolution",
                "540p is the game's own. 720p draws the whole scene at 1280x720, with no scaling inside the game. "
                        + "Applies at once.",
                new String[] {"540p (960x540)", "576p (1024x576)", "648p (1152x648)", "720p (1280x720)"}, () -> {
                    for (int i = files.length - 1; i > 0; i--)
                        if (has(files[i])) return i;
                    return 0;
                }, v -> {
                    for (int i = 1; i < files.length; i++) put(files[i], i == v);
                });
        flag("Dynamic resolution",
                "If the GPU can't finish a frame in time, it lowers the resolution in steps (down to 73 % of the base) "
                        + "and raises it again when there's room. Off: always the base.",
                "no_dynamic_resolution", true);
        flag("FSR upscaling",
                "AMD FSR 1 takes the 720p picture to the screen sharper than a plain scale. Off: linear scaling, a "
                        + "little cheaper.",
                "no_fsr", true);

        header("Picture");
        final String[] caps = {null, "fps_20", "fps_25", "fps_40", "fps_45", "60fps"};
        choice("FPS limit",
                "The game measures how long each frame takes, so between 20 and 60 fps it runs at its normal speed: "
                        + "fewer fps save GPU, heat and battery with no slow motion. 30 is the game's own (loading screens "
                        + "always ask for 60). 40 is even on 120 Hz screens; on a 60 Hz one, 40 and 45 judder a little. "
                        + "Applies at once.",
                new String[] {"30 (the game's)", "20", "25", "40", "45", "60"}, () -> {
                    for (int i = caps.length - 1; i > 0; i--)
                        if (has(caps[i])) return i;
                    return 0;
                }, v -> {
                    for (int i = 1; i < caps.length; i++) put(caps[i], i == v);
                });
        flag("FPS counter", "The number in the corner.", "no_fps",
                true);
    }

    private void buildGraphics() {
        header("Effects");
        flag("Shadows",
                "Those of the hunters, monsters and scenery. Off, the shadow maps aren't drawn: less work for a weak "
                        + "GPU. Applies at once.",
                "no_shadows", true);
        header("Textures");
        {
            final String[] files = {null, "bindless_arrays", "no_bindless_arrays"};
            choice("Texture arrays (bindless)",
                    "How shaders read many of the game's textures. Auto: off with Qualcomm's driver on an Adreno 7xx "
                            + "or 8xx, which can't compile many of those shaders (things come out black); on elsewhere. "
                            + "Turn them off if your driver draws things black. Takes effect on the next start.",
                    new String[] {"Auto", "On", "Off"}, () -> {
                        for (int i = files.length - 1; i > 0; i--)
                            if (has(files[i])) return i;
                        return 0;
                    }, v -> {
                        for (int i = 1; i < files.length; i++) put(files[i], i == v);
                    });
        }
        flag("Anisotropic filtering",
                "Sharper floors and walls at an angle in the distance. Off: a little cheaper for a weak GPU.",
                "no_af", true);
        header("Driver");
        action("GPU driver",
                "The system's, or one of your own for an Adreno (Turnip…), added from a .zip. Takes effect on the next start.",
                () -> {
                    dialog.dismiss();
                    activity.startActivity(new Intent(activity, DriverActivity.class).putExtra(DriverActivity.FROM_GAME, true));
                });
    }

    private void buildPerformance() {
        header("CPU");
        flag("Performance hints",
                "Tells Android how long each frame should take and how much work it took, so it raises the clocks in "
                        + "time instead of letting them drop on every wait (Android 13 or newer). Usually a steadier pace.",
                "no_adpf", true);
        flag("Game threads on the fast cores",
                "The three threads that build each frame go on the fast cores (if the phone has at least three). Off: "
                        + "Android places them.",
                "no_affinity", true);
        header("Memory and shaders");
        flag("Low memory",
                "Smaller caches, as on a phone with less than 5 GB (it's already like that there). Takes effect on the "
                        + "next start.",
                "low_memory", false);
        flag("Shader warm-up",
                "At start it builds the pipelines seen in earlier runs, so they don't drop frames when they appear. "
                        + "Takes effect on the next start.",
                "no_warmup", true);
        flag("Pipelines on one thread",
                "No background threads or warm-up: more stutter, but some drivers crash building several at once "
                        + "(PowerVR is like that by itself). Takes effect on the next start.",
                "single_thread_pipelines", false);
    }

    // Switches for testing drivers and collecting logs. Same files as always (see the options table in the README), just
    // with a name here. Most of them are only read at start-up.
    private void buildDebug() {
        String next = " Takes effect on the next start.";
        note("For testing drivers and sending logs. If the game looks right, leave all of this as it is.");
        header("Log");
        flag("Detailed log",
                "Writes much more to log.txt (every draw's state, textures…). Slower: only for a report.",
                "verbose_log", false);
        flag("GPU breadcrumbs",
                "Logs which pass the GPU stops at when the driver drops work (black screen). Slower." + next,
                "breadcrumbs", false);
        action("Profile one GPU frame",
                "Writes how long each pass of one frame takes to the log (\"perfil GPU\" lines). Use it where it's slow.",
                () -> {
                    put("profile_gpu", true);
                    dialog.dismiss();
                });
        header("Shaders");
        flag("Remove unused shader functions",
                "Strips shader code that's never called before the driver sees it. Qualcomm's driver (512.863) "
                        + "rejected the character pipelines because of it (they came out black)." + next,
                "keep_dead_functions", true);
        flag("Integer vertex attributes",
                "Integer vertex data (bone indices…) reaches the shader as integers, as on the console. Without "
                        + "it, characters stretch into spikes on Mali GPUs." + next,
                "no_int_attribs", true);
        flag("spirv-opt on switch shaders",
                "Runs shaders that contain a switch (BRX) through the optimizer; some drivers miscompile them." + next,
                "no_spirv_opt_brx", true);
        flag("Retry rejected pipelines",
                "When the driver rejects a pipeline, its shaders are translated again with safer options.",
                "no_safe_translation", true);
        flag("Subgroups under 32",
                "Uses subgroup operations even when the GPU's subgroups are under 32 (Mali: 16). Off by default: on "
                        + "a Mali-G615 they stalled the GPU for hundreds of ms." + next,
                "short_subgroups", false);
        header("Driver workarounds");
        flag("Qualcomm: return after a switch",
                "On Qualcomm's driver, a return on a switch's impossible path instead of \"unreachable\" (without "
                        + "it the hunter's face wasn't drawn)." + next,
                "no_return_qualcomm", true);
        flag("Qualcomm: emulated shuffle",
                "On Qualcomm's driver, shuffle with a dynamic index is emulated, as in Eden." + next,
                "no_shuffle_qualcomm", true);
        flag("Mali: return after a switch",
                "The same on Mali GPUs (ARM's driver and PanVK)." + next,
                "no_return_mali", true);
    }

    private void buildTouch() {
        header("Touch controls");
        toggle("On-screen controls",
                "The buttons and sticks over the game. Off, only the gear stays, to come back here.",
                () -> cfg.touchOn ? 1 : 0, v -> cfg.touchOn = v != 0);
        number("Opacity", "How much of the game shows through the controls.", 20, 100, 5, " %", () -> cfg.opacity, v -> cfg.opacity = v);
        number("Size", "All controls at once. Each one also has its own, in Edit layout.", 60, 160, 5, " %", () -> cfg.size,
                v -> cfg.size = v);
        choice("Vibration", "A short tap when a control is pressed.",
                new String[] {"Off", "Light", "Medium", "Strong"},
                () -> cfg.vibration, v -> {
                    cfg.vibration = v;
                    activity.tick();
                });
        toggle("Slide between buttons",
                "A finger moving from one button to another lets go of the first and presses the second, without lifting.",
                () -> cfg.slide ? 1 : 0, v -> cfg.slide = v != 0);
        number("Stick dead zone",
                "How far a touch stick moves from its centre before it counts.", 0, 30, 1, " %",
                () -> cfg.touchDeadzone, v -> cfg.touchDeadzone = v);
        toggle("Tap a stick: L3 / R3",
                "A short tap on a stick, without moving it, presses it (L3 left, R3 right). Their own buttons are there too.",
                () -> cfg.stickPress ? 1 : 0, v -> cfg.stickPress = v != 0);
        action("Edit layout",
                "Drag each control where you want it. At the top: its size, hide or show it, and back to the original.",
                () -> {
                    dialog.dismiss();
                    activity.startEditing();
                });
        action("Reset layout",
                "The traditional layout, every control in its place and at its size.",
                () -> {
                    cfg.resetLayout();
                    changed();
                });

    }

    private void buildCombos() {
        header("Combos");
        note("A combo presses several buttons at once: with its button on the screen, or with one (or two) of the controller's.");
        for (int i = 0; i < Controls.COMBOS; i++) {
            final int k = i;
            String value = cfg.combo[k] == 0 ? "None"
                                             : Controls.names(cfg.combo[k]) + (cfg.comboPad[k] != 0
                                                       ? "  ·  pad: " + Controls.names(cfg.comboPad[k]) : "");
            row("Combo " + (k + 1), value, "Choose the buttons it presses and, if you like, the controller button that does it.", v -> editCombo(k), false);
        }

    }

    private void buildController() {
        header("Controller");
        number("Dead zone", "How far a controller stick moves before it counts (both sticks).", 0, 40, 1, " %",
                () -> cfg.padDeadzone, v -> cfg.padDeadzone = v);
        number("Analog triggers",
                "How far an analog trigger travels before it presses ZL / ZR.", 10, 90, 5, " %",
                () -> cfg.trigger, v -> cfg.trigger = v);
        choice("Right stick",
                "Which axes the right stick is read from. By controller: Z/RZ on Android and Xbox pads, RX/RY (with the "
                        + "triggers on Z/RZ) on many generic ones. Change it if the camera won't move or moves on its own.",
                new String[] {"By controller", "Z / RZ", "RX / RY"}, () -> cfg.rightAxes,
                v -> cfg.rightAxes = v);
    }

    // ---- combo editor ---------------------------------------------------------------------------------------------------

    private void editCombo(int k) {
        final Dialog d = new Dialog(activity);
        d.requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout box = new LinearLayout(activity);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(16), dp(12), dp(16), dp(14));
        box.setBackground(plate(PANEL, 2, 0));
        TextView title = text("Combo " + (k + 1), 20, GOLD, true);
        title.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        box.addView(title);
        box.addView(rule(RED, 2));
        box.addView(rule(GREEN, 1));
        box.addView(text("The buttons it presses:", 14, DIM, false));

        // buttons in rows of eight: tap one to add or remove it
        final long[] mask = {cfg.combo[k]};
        final long[] padMask = {cfg.comboPad[k]};
        LinearLayout grid = new LinearLayout(activity);
        grid.setOrientation(LinearLayout.VERTICAL);
        LinearLayout line = null;
        final TextView[] chips = new TextView[Controls.BUTTONS.length];
        for (int i = 0; i < Controls.BUTTONS.length; i++) {
            if (i % 8 == 0) {
                line = new LinearLayout(activity);
                grid.addView(line);
            }
            final int b = i;
            TextView chip = text(Controls.NAMES[i], 16, CREAM, true);
            chip.setGravity(Gravity.CENTER);
            chip.setOnClickListener(v -> {
                mask[0] ^= Controls.BUTTONS[b];
                paintChip(chips[b], (mask[0] & Controls.BUTTONS[b]) != 0);
            });
            chips[i] = chip;
            paintChip(chip, (mask[0] & Controls.BUTTONS[i]) != 0);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(44), 1);
            lp.setMargins(dp(3), dp(3), dp(3), dp(3));
            line.addView(chip, lp);
        }
        box.addView(grid);

        // the controller button that fires it: one of Controls.PAD_TRIGGERS, picked from a drop-down
        final TextView padValue = text("", 16, GOLD, false);
        LinearLayout padRow = rowView("Controller button", padValue,
                "While it is held, the game gets the combo's buttons instead of it.");
        Runnable showPad = () -> padValue.setText((padMask[0] == 0 ? "None" : Controls.names(padMask[0])) + "  ▾");
        showPad.run();
        padRow.setOnClickListener(v -> {
            int at = 0;
            final String[] names = new String[Controls.PAD_TRIGGERS.length];
            for (int i = 0; i < names.length; i++) {
                if (Controls.PAD_TRIGGERS[i] == padMask[0]) at = i;
                names[i] = Controls.PAD_TRIGGERS[i] == 0 ? "None" : Controls.names(Controls.PAD_TRIGGERS[i]);
            }
            dropDown("Controller button", names, at, i -> {
                padMask[0] = Controls.PAD_TRIGGERS[i];
                showPad.run();
            });
        });
        box.addView(padRow);

        LinearLayout buttons = new LinearLayout(activity);
        buttons.setPadding(0, dp(10), 0, 0);
        TextView clear = button("Clear", false);
        clear.setOnClickListener(v -> {
            cfg.combo[k] = 0;
            cfg.comboPad[k] = 0;
            d.dismiss();
            changed();
            build();
        });
        TextView ok = button("Save", true);
        ok.setOnClickListener(v -> {
            cfg.combo[k] = mask[0];
            cfg.comboPad[k] = mask[0] == 0 ? 0 : padMask[0];
            d.dismiss();
            changed();
            build();
        });
        LinearLayout.LayoutParams half = new LinearLayout.LayoutParams(0, dp(46), 1);
        half.setMargins(dp(4), 0, dp(4), 0);
        buttons.addView(clear, half);
        buttons.addView(ok, new LinearLayout.LayoutParams(half));
        box.addView(buttons);

        d.setContentView(box);
        Window w = d.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            DisplayMetrics m = activity.getResources().getDisplayMetrics();
            w.setLayout(Math.min(dp(620), (int) (m.widthPixels * 0.85f)), ViewGroup.LayoutParams.WRAP_CONTENT);
        }
        d.show();
    }

    private void paintChip(TextView chip, boolean on) {
        chip.setTextColor(on ? 0xFF281010 : CREAM);
        chip.setBackground(plate(on ? GOLD : ROW, 1, 4));
    }

    // ---- rows -----------------------------------------------------------------------------------------------------------

    private void header(String title) {
        TextView t = text(title, 18, GOLD, true);
        t.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        t.setPadding(dp(4), dp(16), 0, dp(4));
        list.addView(t);
        View line = rule(EDGE, 1);
        list.addView(line);
    }

    private void note(String s) {
        TextView t = text(s, 13, DIM, false);
        t.setPadding(dp(4), dp(6), dp(4), dp(2));
        list.addView(t);
    }

    // A row: title on the left, value and "?" on the right; a tap calls click(+1)
    private TextView row(String title, String value, String help, IntConsumer click, boolean steppers) {
        TextView v = text(value, 16, GOLD, false);
        LinearLayout r = rowView(title, v, help);
        r.setOnClickListener(x -> click.accept(1));
        list.addView(r);
        return v;
    }

    private LinearLayout rowView(String title, TextView value, String help) {
        LinearLayout outer = new LinearLayout(activity);
        outer.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams olp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        olp.setMargins(0, dp(4), 0, 0);
        outer.setLayoutParams(olp);
        outer.setBackground(plate(ROW, 1, 2));
        LinearLayout r = new LinearLayout(activity);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(dp(14), dp(10), dp(8), dp(10));
        TextView t = text(title, 16, CREAM, false);
        r.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        value.setPadding(dp(8), 0, dp(8), 0);
        r.addView(value);
        final TextView helpText = text(help, 13, DIM, false);
        helpText.setPadding(dp(14), 0, dp(14), dp(10));
        helpText.setVisibility(View.GONE);
        TextView q = text("?", 15, GOLD, true);
        q.setGravity(Gravity.CENTER);
        GradientDrawable ring = new GradientDrawable();
        ring.setShape(GradientDrawable.OVAL);
        ring.setStroke(dp(1), EDGE);
        q.setBackground(ring);
        q.setOnClickListener(x -> helpText.setVisibility(helpText.getVisibility() == View.GONE ? View.VISIBLE : View.GONE));
        r.addView(q, new LinearLayout.LayoutParams(dp(30), dp(30)));
        outer.addView(r);
        outer.addView(helpText);
        return outer;
    }

    private void toggle(String title, String help, IntSupplier get, IntConsumer set) {
        String[] onOff = {"Off", "On"};
        choice(title, help, onOff, get, set);
    }

    // Two options (on / off): a tap flips it. More than two: a drop-down with the current one highlighted.
    private void choice(String title, String help, String[] options, IntSupplier get, IntConsumer set) {
        final TextView[] value = new TextView[1];
        final boolean list = options.length > 2;
        value[0] = row(title, options[get.getAsInt()] + (list ? "  ▾" : ""), help, step -> {
            if (!list) {
                set.accept((get.getAsInt() + step) % options.length);
                value[0].setText(options[get.getAsInt()]);
                changed();
                return;
            }
            dropDown(title, options, get.getAsInt(), i -> {
                set.accept(i);
                value[0].setText(options[get.getAsInt()] + "  ▾");
                changed();
            });
        }, false);
    }

    // The drop-down: a panel like the menu itself, with a title, the X and one row per option (current one in gold)
    private void dropDown(String title, String[] options, int current, IntConsumer pick) {
        final Dialog d = new Dialog(activity);
        d.requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackground(plate(PANEL, 2, 0));
        LinearLayout top = new LinearLayout(activity);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setPadding(dp(16), dp(8), dp(8), dp(8));
        TextView t = text("◆  " + title, 18, GOLD, true);
        t.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        TextView close = text("✕", 20, CREAM, false);
        close.setPadding(dp(12), dp(2), dp(12), dp(2));
        close.setBackground(plate(0xFF1A0C09, 1, 2));
        close.setOnClickListener(v -> d.dismiss());
        top.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        top.addView(close);
        root.addView(top);
        root.addView(rule(RED, 2));
        root.addView(rule(GREEN, 1));
        LinearLayout items = new LinearLayout(activity);
        items.setOrientation(LinearLayout.VERTICAL);
        items.setPadding(dp(10), dp(6), dp(10), dp(10));
        for (int i = 0; i < options.length; i++) {
            final int k = i;
            final boolean on = i == current;
            LinearLayout r = new LinearLayout(activity);
            r.setGravity(Gravity.CENTER_VERTICAL);
            r.setPadding(dp(14), dp(12), dp(14), dp(12));
            r.setBackground(on ? gold() : plate(ROW, 1, 2));
            TextView name = text((on ? "◆  " : "") + options[i], 16, on ? 0xFF281010 : CREAM, on);
            r.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            if (on) r.addView(text("◆", 14, 0xFF281010, true));
            r.setOnClickListener(v -> {
                d.dismiss();
                if (k != current) pick.accept(k);
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.topMargin = dp(4);
            items.addView(r, lp);
        }
        ScrollView sv = new ScrollView(activity);
        sv.addView(items);
        root.addView(sv);
        d.setContentView(root);
        Window w = d.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            DisplayMetrics m = activity.getResources().getDisplayMetrics();
            w.setLayout(Math.min(dp(460), (int) (m.widthPixels * 0.6f)), ViewGroup.LayoutParams.WRAP_CONTENT);
        }
        d.show();
    }

    // Selected drop-down row: gold with a glow, like the game's menus
    private GradientDrawable gold() {
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[] {0xFFD4A43C, 0xFF8E5A22});
        g.setStroke(dp(2), 0xFFF0D080);
        g.setCornerRadius(dp(2));
        return g;
    }

    // Saved menu size, in %
    static int menuScale(android.content.Context c) {
        try {
            return c.getSharedPreferences(PREFS, android.content.Context.MODE_PRIVATE).getInt(SCALE_KEY, SCALE_DEFAULT);
        } catch (RuntimeException e) {
            return SCALE_DEFAULT;
        }
    }

    // A number with − and +: tapping the row also steps up (and wraps to the minimum past the maximum)
    private void number(String title, String help, int min, int max, int step, String unit, IntSupplier get, IntConsumer set) {
        TextView value = text(get.getAsInt() + unit, 16, GOLD, false);
        LinearLayout r = rowView(title, value, help);
        LinearLayout line = (LinearLayout) r.getChildAt(0);
        TextView minus = smallButton("−"), plus = smallButton("+");
        line.addView(minus, 1, new LinearLayout.LayoutParams(dp(40), dp(36)));
        line.addView(plus, 3, new LinearLayout.LayoutParams(dp(40), dp(36)));
        IntConsumer by = d -> {
            set.accept(Math.max(min, Math.min(max, get.getAsInt() + d)));
            value.setText(get.getAsInt() + unit);
            changed();
        };
        minus.setOnClickListener(x -> by.accept(-step));
        plus.setOnClickListener(x -> by.accept(step));
        r.setOnClickListener(x -> {
            int next = get.getAsInt() + step;
            set.accept(next > max ? min : next);
            value.setText(get.getAsInt() + unit);
            changed();
        });
        list.addView(r);
    }

    // ---- game switches --------------------------------------------------------------------------------------------------

    private static File gameFolder(GameActivity a) {
        try {
            String path = new String(Files.readAllBytes(new File(a.getFilesDir(), "carpeta.txt").toPath())).trim();
            return path.isEmpty() ? null : new File(path);
        } catch (IOException e) {
            return null;
        }
    }

    private boolean has(String name) {
        return folder != null && new File(folder, name).exists();
    }

    private void put(String name, boolean on) {
        if (folder == null) return;
        File f = new File(folder, name);
        try {
            if (on) f.createNewFile();
            else f.delete();
        } catch (IOException ignored) {
            // (storage refused: it stays as it was, and the row shows that next time)
        }
    }

    // An on / off row backed by a switch file. inverted: the file turns it off (the sin_* ones).
    private void flag(String title, String help, String file, boolean inverted) {
        toggle(title, help, () -> has(file) != inverted ? 1 : 0, v -> put(file, (v != 0) != inverted));
    }

    private void action(String title, String help, Runnable run) {
        row(title, "›", help, x -> run.run(), false);
    }

    // ---- pieces ---------------------------------------------------------------------------------------------------------

    private TextView smallButton(String s) {
        TextView b = text(s, 18, CREAM, true);
        b.setGravity(Gravity.CENTER);
        b.setBackground(plate(0xFF1A0C09, 1, 2));
        return b;
    }

    private TextView button(String s, boolean main) {
        TextView b = text(s, 16, main ? 0xFF281010 : CREAM, true);
        b.setGravity(Gravity.CENTER);
        b.setBackground(plate(main ? GOLD : 0xFF3E2616, 1, 4));
        return b;
    }

    private View rule(int color, int heightDp) {
        View v = new View(activity);
        v.setBackgroundColor(color);
        v.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(heightDp)));
        return v;
    }

    private GradientDrawable plate(int fill, int strokeDp, int radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setStroke(dp(strokeDp), EDGE);
        d.setCornerRadius(dp(radiusDp));
        return d;
    }

    private TextView text(String s, int sp, int color, boolean bold) {
        TextView t = new TextView(activity);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp * scale);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private int dp(int v) {  // (scaled by the menu size; 1 dp rules never vanish)
        float px = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v * scale, activity.getResources().getDisplayMetrics());
        return v > 0 ? Math.max(1, Math.round(px)) : Math.round(px);
    }
}
