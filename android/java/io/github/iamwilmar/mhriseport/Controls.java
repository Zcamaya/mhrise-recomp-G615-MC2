package io.github.iamwilmar.mhriseport;

import android.content.Context;
import android.content.SharedPreferences;

// Los ajustes de los controles, guardados en las preferencias de la app ("controles"): los táctiles (si se ven, su
// opacidad y tamaño, la vibración, deslizar de un botón a otro, la zona muerta de sus sticks), dónde está cada control,
// los combos (un botón en pantalla, o uno del mando, que pulsa varios de la consola) y el mando físico (zona muerta,
// gatillos, dónde lee el stick derecho). TouchPad dibuja y lee los táctiles; lo del mando lo aplica el loader
// (GameActivity.nativePad).
final class Controls {
    // Bits de nn::hid::NpadButton, como en runtime/src/loader/android_main.cpp.
    static final long A = 1L, B = 1L << 1, X = 1L << 2, Y = 1L << 3, LSTICK = 1L << 4, RSTICK = 1L << 5, L = 1L << 6,
            R = 1L << 7, ZL = 1L << 8, ZR = 1L << 9, PLUS = 1L << 10, MINUS = 1L << 11, LEFT = 1L << 12, UP = 1L << 13,
            RIGHT = 1L << 14, DOWN = 1L << 15;

    // Los botones que se pueden poner en un combo, en el orden del menú, y su nombre.
    static final long[] BUTTONS = {A, B, X, Y, L, R, ZL, ZR, PLUS, MINUS, LSTICK, RSTICK, UP, DOWN, LEFT, RIGHT};
    static final String[] NAMES = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "+", "−", "L3", "R3", "↑", "↓", "←", "→"};

    static String names(long mask) {
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < BUTTONS.length; i++) {
            if ((mask & BUTTONS[i]) == 0) continue;
            if (s.length() > 0) s.append('+');
            s.append(NAMES[i]);
        }
        return s.toString();
    }

    // Los controles en pantalla. Cada uno: dónde está su centro (fracción del ancho y del alto de la pantalla), su tamaño
    // (1: el suyo) y si se ve.
    enum Ctl {
        LSTICK, RSTICK, DPAD, A, B, X, Y, L, R, ZL, ZR, MINUS, PLUS, L3, R3, MENU, COMBO1, COMBO2, COMBO3, COMBO4;

        boolean combo() { return ordinal() >= COMBO1.ordinal(); }
        int comboIndex() { return ordinal() - COMBO1.ordinal(); }
    }

    // La disposición tradicional: la de un mando, con todo a la vista. Sticks arriba a la izquierda y abajo a la derecha
    // (como la consola), la cruceta debajo del izquierdo, los cuatro botones arriba a la derecha, los gatillos en las
    // esquinas de arriba, − y + arriba en el centro con el menú entre ellos, y L3 / R3 al lado de su stick.
    static final float[][] HOME = {
        {0.12f, 0.46f, 1}, // LSTICK
        {0.73f, 0.78f, 1}, // RSTICK
        {0.26f, 0.79f, 1}, // DPAD
        {0.935f, 0.46f, 1}, // A
        {0.875f, 0.60f, 1}, // B
        {0.875f, 0.32f, 1}, // X
        {0.815f, 0.46f, 1}, // Y
        {0.16f, 0.10f, 1}, // L
        {0.84f, 0.10f, 1}, // R
        {0.05f, 0.21f, 1}, // ZL
        {0.95f, 0.21f, 1}, // ZR
        {0.42f, 0.07f, 1}, // MINUS
        {0.58f, 0.07f, 1}, // PLUS
        {0.045f, 0.70f, 1}, // L3
        {0.585f, 0.90f, 1}, // R3
        {0.50f, 0.07f, 1}, // MENU
        {0.69f, 0.36f, 1}, // COMBO1
        {0.69f, 0.56f, 1}, // COMBO2
        {0.62f, 0.36f, 1}, // COMBO3
        {0.62f, 0.56f, 1}, // COMBO4
    };

    static final int COMBOS = 4;
    // Los botones del mando que pueden disparar un combo, en el orden en que el menú los recorre (0: ninguno).
    static final long[] PAD_TRIGGERS = {0, LSTICK, RSTICK, LSTICK | RSTICK, MINUS, PLUS, MINUS | PLUS, L | R, ZL | ZR, L | ZL,
                                        R | ZR, MINUS | L, MINUS | R, PLUS | L, PLUS | R};

    // Los toques
    boolean touchOn = true;     // los controles en pantalla (apagados: solo el botón del menú)
    int opacity = 70;           // %
    int size = 100;             // %
    int vibration = 2;          // 0 apagada, 1 suave, 2 media, 3 fuerte
    boolean slide = true;       // un dedo que pasa de un botón a otro suelta el primero y pulsa el segundo
    int touchDeadzone = 8;      // % del radio del stick táctil
    boolean stickPress = true;  // un toque corto en un stick es L3 / R3
    boolean cameraSwipe = true; // dragging on the game (away from the controls) turns the camera
    int swipeSpeed = 100;       // %: how far the camera turns for a drag
    final float[][] place = new float[Ctl.values().length][3];
    final boolean[] shown = new boolean[Ctl.values().length];
    // Los combos
    final long[] combo = new long[COMBOS];     // los botones que pulsa (0: ninguno, y no sale en pantalla)
    final long[] comboPad = new long[COMBOS];  // el botón (o los dos) del mando que lo dispara (0: ninguno)
    // El mando físico
    int padDeadzone = 12;   // % del recorrido del stick
    int trigger = 35;       // % del recorrido de un gatillo analógico desde el que cuenta como ZL / ZR
    int rightAxes = 0;      // 0 según el mando, 1 Z/RZ, 2 RX/RY (android_main.cpp: kAxes*)

    private final SharedPreferences prefs;

    Controls(Context context) {
        prefs = context.getSharedPreferences("controles", Context.MODE_PRIVATE);
        resetLayout();
        load();
    }

    void resetLayout() {
        for (Ctl c : Ctl.values()) {
            place[c.ordinal()] = HOME[c.ordinal()].clone();
            shown[c.ordinal()] = true;
        }
    }

    private void load() {
        touchOn = prefs.getBoolean("tactil", touchOn);
        opacity = prefs.getInt("opacidad", opacity);
        size = prefs.getInt("tamano", size);
        vibration = prefs.getInt("vibracion", vibration);
        slide = prefs.getBoolean("deslizar", slide);
        touchDeadzone = prefs.getInt("zona_tactil", touchDeadzone);
        stickPress = prefs.getBoolean("toque_stick", stickPress);
        cameraSwipe = prefs.getBoolean("camera_swipe", cameraSwipe);
        swipeSpeed = prefs.getInt("swipe_speed", swipeSpeed);
        padDeadzone = prefs.getInt("zona_mando", padDeadzone);
        trigger = prefs.getInt("gatillo", trigger);
        rightAxes = prefs.getInt("stick_derecho", rightAxes);
        for (int i = 0; i < COMBOS; i++) {
            combo[i] = prefs.getLong("combo" + i, 0);
            comboPad[i] = prefs.getLong("combo_mando" + i, 0);
        }
        // "LSTICK:0.12,0.46,1,1;..." (x, y, tamaño, se ve): lo que no esté, como viene
        for (String item : prefs.getString("disposicion", "").split(";")) {
            String[] kv = item.split(":");
            if (kv.length != 2) continue;
            try {
                Ctl c = Ctl.valueOf(kv[0]);
                String[] v = kv[1].split(",");
                place[c.ordinal()][0] = Float.parseFloat(v[0]);
                place[c.ordinal()][1] = Float.parseFloat(v[1]);
                place[c.ordinal()][2] = Float.parseFloat(v[2]);
                shown[c.ordinal()] = v.length < 4 || !v[3].equals("0");
            } catch (RuntimeException ignored) {
                // (un control que ya no existe, o un número roto: queda el de siempre)
            }
        }
    }

    void save() {
        StringBuilder layout = new StringBuilder();
        for (Ctl c : Ctl.values()) {
            float[] p = place[c.ordinal()];
            layout.append(c.name()).append(':').append(p[0]).append(',').append(p[1]).append(',').append(p[2]).append(',')
                    .append(shown[c.ordinal()] ? '1' : '0').append(';');
        }
        SharedPreferences.Editor e = prefs.edit();
        e.putBoolean("tactil", touchOn).putInt("opacidad", opacity).putInt("tamano", size).putInt("vibracion", vibration)
                .putBoolean("deslizar", slide).putInt("zona_tactil", touchDeadzone).putBoolean("toque_stick", stickPress)
                .putInt("zona_mando", padDeadzone).putInt("gatillo", trigger).putInt("stick_derecho", rightAxes)
                .putBoolean("camera_swipe", cameraSwipe).putInt("swipe_speed", swipeSpeed)
                .putString("disposicion", layout.toString());
        for (int i = 0; i < COMBOS; i++) e.putLong("combo" + i, combo[i]).putLong("combo_mando" + i, comboPad[i]);
        e.apply();
    }

    // Los combos del mando para el loader: pares (botones del mando, botones que pulsa), los de dos botones primero
    long[] padCombos() {
        long[] out = new long[COMBOS * 2];
        int n = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < COMBOS; i++) {
                if (comboPad[i] == 0 || combo[i] == 0) continue;
                boolean two = Long.bitCount(comboPad[i]) > 1;
                if ((pass == 0) != two) continue;
                out[n++] = comboPad[i];
                out[n++] = combo[i];
            }
        return java.util.Arrays.copyOf(out, n);
    }
}
