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

import java.util.function.IntConsumer;
import java.util.function.IntSupplier;

import static io.github.iamwilmar.mhriseport.SetupActivity.tr;

// El menú de los controles, sobre el juego, en el estilo de su menú: paneles de vino oscuro con borde de bronce, títulos
// dorados con la raya roja y verde, y un "?" en cada fila que despliega lo que hace. Se abre con el engranaje de los
// controles táctiles. Cada cambio se guarda y se aplica al momento (GameActivity.refresh).
final class ControlsMenu {
    static final int PANEL = 0xF0201010, ROW = 0xFF2C1614, EDGE = 0xFFC49646, GOLD = 0xFFE2B65C, CREAM = 0xFFF0E4C8,
            DIM = 0xFFB09C84, RED = 0xFFB0221E, GREEN = 0xFF508E36;

    private final GameActivity activity;
    private final Controls cfg;
    private final Dialog dialog;
    private final LinearLayout list;

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
        TextView sub = text("  ◆  " + tr("Controles", "Controls"), 17, CREAM, false);
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

        list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(dp(12), dp(6), dp(12), dp(12));
        ScrollView scroll = new ScrollView(activity);
        scroll.addView(list);
        root.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        build();

        dialog.setContentView(root);
        Window w = dialog.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            DisplayMetrics m = activity.getResources().getDisplayMetrics();
            w.setLayout(Math.min(dp(760), (int) (m.widthPixels * 0.9f)), (int) (m.heightPixels * 0.9f));
        }
    }

    void show() { dialog.show(); }

    private void changed() { activity.refresh(); }

    private void build() {
        list.removeAllViews();
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

        header(tr("Gráficos", "Graphics"));
        action(tr("Driver de GPU", "GPU driver"),
                tr("El del sistema, o uno propio para una Adreno (Turnip…), agregado desde un .zip. Vale desde el siguiente arranque.",
                        "The system's, or one of your own for an Adreno (Turnip…), added from a .zip. Takes effect on the next start."),
                () -> {
                    dialog.dismiss();
                    activity.startActivity(new Intent(activity, DriverActivity.class).putExtra(DriverActivity.FROM_GAME, true));
                });
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
