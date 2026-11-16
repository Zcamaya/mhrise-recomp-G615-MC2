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

import static io.github.iamwilmar.mhriseport.SetupActivity.tr;

// El menú de ajustes, sobre el juego, en el estilo de su menú: paneles de vino oscuro con borde de bronce, títulos
// dorados con la raya roja y verde, y un "?" en cada fila que despliega lo que hace. Se abre con el engranaje de los
// controles táctiles. A la izquierda las secciones; a la derecha las filas de la elegida.
// Los controles se guardan y se aplican al momento (GameActivity.refresh). Pantalla, Gráficos y Rendimiento son los
// interruptores del juego: ficheros vacíos en la carpeta del juego (runtime/src/nnsdk/nvn/flags.h), que el juego mira
// una vez por segundo; los que solo se leen al arrancar lo dicen en su ayuda.
final class ControlsMenu {
    static final int PANEL = 0xF0201010, ROW = 0xFF2C1614, EDGE = 0xFFC49646, GOLD = 0xFFE2B65C, CREAM = 0xFFF0E4C8,
            DIM = 0xFFB09C84, RED = 0xFFB0221E, GREEN = 0xFF508E36;

    private static int section = 0;  // la última abierta (mientras viva el proceso)

    private final GameActivity activity;
    private final Controls cfg;
    private final Dialog dialog;
    private final LinearLayout list, sections;
    private final ScrollView scroll;
    private final TextView sub;
    private final File folder;  // la del juego (files/carpeta.txt), donde van los interruptores

    ControlsMenu(GameActivity activity, Controls cfg) {
        this.activity = activity;
        this.cfg = cfg;
        dialog = new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);

        LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackground(plate(PANEL, 2, 0));

        // arriba: "AJUSTES ◆ Controles" y la X, con la raya roja y verde debajo
        LinearLayout top = new LinearLayout(activity);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setPadding(dp(18), dp(10), dp(10), dp(10));
        TextView title = text(tr("AJUSTES", "SETTINGS"), 22, GOLD, true);
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

    // ---- secciones -------------------------------------------------------------------------------------------------------

    private String[] sectionNames() {
        return new String[] {tr("Pantalla", "Display"), tr("Gráficos", "Graphics"), tr("Rendimiento", "Performance"),
                tr("Controles táctiles", "Touch controls"), tr("Combos", "Combos"), tr("Mando físico", "Controller")};
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
            default: buildController(); break;
        }
    }

    private void buildDisplay() {
        header(tr("Resolución", "Resolution"));
        note(tr("La del 3D del juego. En la consola portátil es 960x540 dentro de una imagen de 720p, que luego se escala a "
                        + "la pantalla. Más alta: más nítido y más trabajo para la GPU.",
                "The game's 3D resolution. On the handheld console it's 960x540 inside a 720p picture, which is then scaled "
                        + "to the screen. Higher: sharper, and more work for the GPU."));
        final String[] files = {null, "resolucion_576", "resolucion_648", "resolucion_720"};
        choice(tr("Resolución base", "Base resolution"),
                tr("540p es la del juego. 720p dibuja la escena entera a 1280x720, sin escalarla dentro del juego. Se aplica "
                                + "al momento.",
                        "540p is the game's own. 720p draws the whole scene at 1280x720, with no scaling inside the game. "
                                + "Applies at once."),
                new String[] {"540p (960x540)", "576p (1024x576)", "648p (1152x648)", "720p (1280x720)"}, () -> {
                    for (int i = files.length - 1; i > 0; i--)
                        if (has(files[i])) return i;
                    return 0;
                }, v -> {
                    for (int i = 1; i < files.length; i++) put(files[i], i == v);
                });
        flag(tr("Resolución dinámica", "Dynamic resolution"),
                tr("Si la GPU no llega a tiempo al frame, baja la resolución por pasos (hasta un 73 % de la base) y la vuelve "
                                + "a subir cuando sobra. Apagada: siempre la base.",
                        "If the GPU can't finish a frame in time, it lowers the resolution in steps (down to 73 % of the base) "
                                + "and raises it again when there's room. Off: always the base."),
                "sin_resolucion_dinamica", true);
        flag(tr("Escalado FSR", "FSR upscaling"),
                tr("FSR 1 de AMD lleva la imagen de 720p a la pantalla con más nitidez que un escalado normal. Apagado: "
                                + "escalado lineal, un poco más barato.",
                        "AMD FSR 1 takes the 720p picture to the screen sharper than a plain scale. Off: linear scaling, a "
                                + "little cheaper."),
                "sin_fsr", true);

        header(tr("Imagen", "Picture"));
        final String[] caps = {null, "fps_20", "fps_25", "fps_40", "fps_45", "60fps"};
        choice(tr("Límite de FPS", "FPS limit"),
                tr("El juego mide cuánto dura cada frame, así que entre 20 y 60 fps va a su velocidad: menos fps ahorran GPU, "
                                + "calor y batería sin cámara lenta. 30 es el del juego (en las cargas siempre pide 60). 40 va "
                                + "parejo en pantallas de 120 Hz; en una de 60, 40 y 45 dan algún tirón. Se aplica al momento.",
                        "The game measures how long each frame takes, so between 20 and 60 fps it runs at its normal speed: "
                                + "fewer fps save GPU, heat and battery with no slow motion. 30 is the game's own (loading screens "
                                + "always ask for 60). 40 is even on 120 Hz screens; on a 60 Hz one, 40 and 45 judder a little. "
                                + "Applies at once."),
                new String[] {tr("30 (el del juego)", "30 (the game's)"), "20", "25", "40", "45", "60"}, () -> {
                    for (int i = caps.length - 1; i > 0; i--)
                        if (has(caps[i])) return i;
                    return 0;
                }, v -> {
                    for (int i = 1; i < caps.length; i++) put(caps[i], i == v);
                });
        flag(tr("Contador de FPS", "FPS counter"), tr("El número en la esquina.", "The number in the corner."), "sin_fps",
                true);
    }

    private void buildGraphics() {
        header(tr("Efectos", "Effects"));
        flag(tr("Sombras", "Shadows"),
                tr("Las de los cazadores, los monstruos y el escenario. Apagadas no se dibujan los mapas de sombra: menos "
                                + "trabajo para una GPU floja. Se aplica al momento.",
                        "Those of the hunters, monsters and scenery. Off, the shadow maps aren't drawn: less work for a weak "
                                + "GPU. Applies at once."),
                "sin_sombras", true);
        header(tr("Texturas", "Textures"));
        flag(tr("Filtrado anisótropo", "Anisotropic filtering"),
                tr("Suelos y paredes en ángulo más nítidos a lo lejos. Apagado: un poco más barato para una GPU floja.",
                        "Sharper floors and walls at an angle in the distance. Off: a little cheaper for a weak GPU."),
                "sin_af", true);
        header(tr("Driver", "Driver"));
        action(tr("Driver de GPU", "GPU driver"),
                tr("El del sistema, o uno propio para una Adreno (Turnip…), agregado desde un .zip. Vale desde el siguiente arranque.",
                        "The system's, or one of your own for an Adreno (Turnip…), added from a .zip. Takes effect on the next start."),
                () -> {
                    dialog.dismiss();
                    activity.startActivity(new Intent(activity, DriverActivity.class).putExtra(DriverActivity.FROM_GAME, true));
                });
    }

    private void buildPerformance() {
        header(tr("CPU", "CPU"));
        flag(tr("Pistas de rendimiento", "Performance hints"),
                tr("Le dice a Android cuánto dura cada frame y cuánto trabajo lleva, para que suba los relojes a tiempo en vez "
                                + "de dejarlos bajar en cada espera (Android 13 o más nuevo). Suele dar un ritmo más regular.",
                        "Tells Android how long each frame should take and how much work it took, so it raises the clocks in "
                                + "time instead of letting them drop on every wait (Android 13 or newer). Usually a steadier pace."),
                "sin_adpf", true);
        flag(tr("Hilos del juego en los núcleos rápidos", "Game threads on the fast cores"),
                tr("Los tres hilos que hacen el frame van a los núcleos rápidos (si el teléfono tiene al menos tres). Apagado: "
                                + "los reparte Android.",
                        "The three threads that build each frame go on the fast cores (if the phone has at least three). Off: "
                                + "Android places them."),
                "sin_afinidad", true);
        header(tr("Memoria y shaders", "Memory and shaders"));
        flag(tr("Memoria baja", "Low memory"),
                tr("Cachés más pequeñas, como en un teléfono de menos de 5 GB (ahí ya va así solo). Vale desde el siguiente "
                                + "arranque.",
                        "Smaller caches, as on a phone with less than 5 GB (it's already like that there). Takes effect on the "
                                + "next start."),
                "memoria_baja", false);
        flag(tr("Precalentar shaders", "Shader warm-up"),
                tr("Al arrancar crea las pipelines que ya se vieron otras veces, para que no tiren el ritmo al aparecer. "
                                + "Vale desde el siguiente arranque.",
                        "At start it builds the pipelines seen in earlier runs, so they don't drop frames when they appear. "
                                + "Takes effect on the next start."),
                "sin_precalentado", true);
        flag(tr("Pipelines en un solo hilo", "Pipelines on one thread"),
                tr("Sin hilos de fondo ni precalentado: más tirones, pero algunos drivers se cuelgan creando varias a la vez "
                                + "(PowerVR va así solo). Vale desde el siguiente arranque.",
                        "No background threads or warm-up: more stutter, but some drivers crash building several at once "
                                + "(PowerVR is like that by itself). Takes effect on the next start."),
                "pipelines_un_hilo", false);
    }

    private void buildTouch() {
        header(tr("Controles táctiles", "Touch controls"));
        toggle(tr("Controles en pantalla", "On-screen controls"),
                tr("Los botones y sticks sobre el juego. Apagados queda solo el engranaje, para volver aquí.",
                        "The buttons and sticks over the game. Off, only the gear stays, to come back here."),
                () -> cfg.touchOn ? 1 : 0, v -> cfg.touchOn = v != 0);
        number(tr("Opacidad", "Opacity"), tr("Lo que se ve el juego a través de los controles.",
                "How much of the game shows through the controls."), 20, 100, 5, " %", () -> cfg.opacity, v -> cfg.opacity = v);
        number(tr("Tamaño", "Size"), tr("El de todos los controles a la vez. Cada uno tiene además el suyo, en Editar.",
                "All controls at once. Each one also has its own, in Edit layout."), 60, 160, 5, " %", () -> cfg.size,
                v -> cfg.size = v);
        choice(tr("Vibración", "Vibration"), tr("Un golpe corto al pulsar un control.", "A short tap when a control is pressed."),
                new String[] {tr("Apagada", "Off"), tr("Suave", "Light"), tr("Media", "Medium"), tr("Fuerte", "Strong")},
                () -> cfg.vibration, v -> {
                    cfg.vibration = v;
                    activity.tick();
                });
        toggle(tr("Deslizar entre botones", "Slide between buttons"),
                tr("Un dedo que pasa de un botón a otro suelta el primero y pulsa el segundo, sin levantarlo.",
                        "A finger moving from one button to another lets go of the first and presses the second, without lifting."),
                () -> cfg.slide ? 1 : 0, v -> cfg.slide = v != 0);
        number(tr("Zona muerta de los sticks", "Stick dead zone"),
                tr("Cuánto hay que mover un stick táctil desde su centro antes de que cuente.",
                        "How far a touch stick moves from its centre before it counts."), 0, 30, 1, " %",
                () -> cfg.touchDeadzone, v -> cfg.touchDeadzone = v);
        toggle(tr("Toque en el stick: L3 / R3", "Tap a stick: L3 / R3"),
                tr("Un toque corto en un stick, sin moverlo, lo pulsa (L3 el izquierdo, R3 el derecho). También están sus botones.",
                        "A short tap on a stick, without moving it, presses it (L3 left, R3 right). Their own buttons are there too."),
                () -> cfg.stickPress ? 1 : 0, v -> cfg.stickPress = v != 0);
        action(tr("Editar la disposición", "Edit layout"),
                tr("Arrastra cada control a su sitio. Arriba: su tamaño, esconderlo o enseñarlo, y volver a la de siempre.",
                        "Drag each control where you want it. At the top: its size, hide or show it, and back to the original."),
                () -> {
                    dialog.dismiss();
                    activity.startEditing();
                });
        action(tr("Restablecer la disposición", "Reset layout"),
                tr("La disposición tradicional, con todos los controles en su sitio y a su tamaño.",
                        "The traditional layout, every control in its place and at its size."),
                () -> {
                    cfg.resetLayout();
                    changed();
                });

    }

    private void buildCombos() {
        header(tr("Combos", "Combos"));
        note(tr("Un combo pulsa varios botones a la vez: con su botón en pantalla, o con uno (o dos) del mando.",
                "A combo presses several buttons at once: with its button on the screen, or with one (or two) of the controller's."));
        for (int i = 0; i < Controls.COMBOS; i++) {
            final int k = i;
            String value = cfg.combo[k] == 0 ? tr("Ninguno", "None")
                                             : Controls.names(cfg.combo[k]) + (cfg.comboPad[k] != 0
                                                       ? "  ·  " + tr("mando: ", "pad: ") + Controls.names(cfg.comboPad[k]) : "");
            row(tr("Combo ", "Combo ") + (k + 1), value, tr("Elige los botones que pulsa y, si quieres, el botón del mando que lo hace.",
                    "Choose the buttons it presses and, if you like, the controller button that does it."), v -> editCombo(k), false);
        }

    }

    private void buildController() {
        header(tr("Mando físico", "Controller"));
        number(tr("Zona muerta", "Dead zone"), tr("Cuánto hay que mover un stick del mando antes de que cuente (los dos sticks).",
                "How far a controller stick moves before it counts (both sticks)."), 0, 40, 1, " %",
                () -> cfg.padDeadzone, v -> cfg.padDeadzone = v);
        number(tr("Gatillos analógicos", "Analog triggers"),
                tr("Desde qué parte del recorrido un gatillo analógico pulsa ZL / ZR.",
                        "How far an analog trigger travels before it presses ZL / ZR."), 10, 90, 5, " %",
                () -> cfg.trigger, v -> cfg.trigger = v);
        choice(tr("Stick derecho", "Right stick"),
                tr("Los ejes de donde se lee el stick derecho. Según el mando: Z/RZ en los de Android y Xbox, RX/RY (con los "
                                + "gatillos en Z/RZ) en muchos genéricos. Cámbialo si la cámara no se mueve o se mueve sola.",
                        "Which axes the right stick is read from. By controller: Z/RZ on Android and Xbox pads, RX/RY (with the "
                                + "triggers on Z/RZ) on many generic ones. Change it if the camera won't move or moves on its own."),
                new String[] {tr("Según el mando", "By controller"), "Z / RZ", "RX / RY"}, () -> cfg.rightAxes,
                v -> cfg.rightAxes = v);
    }

    // ---- el editor de un combo ----------------------------------------------------------------------------------------

    private void editCombo(int k) {
        final Dialog d = new Dialog(activity);
        d.requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout box = new LinearLayout(activity);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(16), dp(12), dp(16), dp(14));
        box.setBackground(plate(PANEL, 2, 0));
        TextView title = text(tr("Combo ", "Combo ") + (k + 1), 20, GOLD, true);
        title.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        box.addView(title);
        box.addView(rule(RED, 2));
        box.addView(rule(GREEN, 1));
        box.addView(text(tr("Los botones que pulsa:", "The buttons it presses:"), 14, DIM, false));

        // los botones, en filas de ocho: tocar uno lo pone o lo quita
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

        // el botón del mando que lo hace: se recorren los de Controls.PAD_TRIGGERS
        final TextView padValue = text("", 16, GOLD, false);
        LinearLayout padRow = rowView(tr("Botón del mando", "Controller button"), padValue,
                tr("Mientras se pulsa, el juego recibe los botones del combo en lugar de él.",
                        "While it is held, the game gets the combo's buttons instead of it."));
        Runnable showPad = () -> padValue.setText(padMask[0] == 0 ? tr("Ninguno", "None") : Controls.names(padMask[0]));
        showPad.run();
        padRow.setOnClickListener(v -> {
            int at = 0;
            for (int i = 0; i < Controls.PAD_TRIGGERS.length; i++) if (Controls.PAD_TRIGGERS[i] == padMask[0]) at = i;
            padMask[0] = Controls.PAD_TRIGGERS[(at + 1) % Controls.PAD_TRIGGERS.length];
            showPad.run();
        });
        box.addView(padRow);

        LinearLayout buttons = new LinearLayout(activity);
        buttons.setPadding(0, dp(10), 0, 0);
        TextView clear = button(tr("Quitar", "Clear"), false);
        clear.setOnClickListener(v -> {
            cfg.combo[k] = 0;
            cfg.comboPad[k] = 0;
            d.dismiss();
            changed();
            build();
        });
        TextView ok = button(tr("Guardar", "Save"), true);
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

    // ---- filas ----------------------------------------------------------------------------------------------------------

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

    // Una fila: el título a la izquierda, su valor a la derecha y su "?"; un toque hace click(+1)
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
        String[] onOff = {tr("Apagado", "Off"), tr("Activo", "On")};
        choice(title, help, onOff, get, set);
    }

    private void choice(String title, String help, String[] options, IntSupplier get, IntConsumer set) {
        final TextView[] value = new TextView[1];
        value[0] = row(title, options[get.getAsInt()], help, step -> {
            set.accept((get.getAsInt() + step) % options.length);
            value[0].setText(options[get.getAsInt()]);
            changed();
        }, false);
    }

    // Un número con − y +: un toque en la fila también sube (y vuelve al mínimo pasado el máximo)
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

    // ---- interruptores del juego -----------------------------------------------------------------------------------------

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
            // (la SD no deja: se queda como estaba, y la fila lo enseña al volver a abrir)
        }
    }

    // Una fila de encendido / apagado sobre un interruptor. inverted: el fichero lo apaga (sin_*).
    private void flag(String title, String help, String file, boolean inverted) {
        toggle(title, help, () -> has(file) != inverted ? 1 : 0, v -> put(file, (v != 0) != inverted));
    }

    private void action(String title, String help, Runnable run) {
        row(title, "›", help, x -> run.run(), false);
    }

    // ---- piezas ----------------------------------------------------------------------------------------------------------

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
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, activity.getResources().getDisplayMetrics()));
    }
}
