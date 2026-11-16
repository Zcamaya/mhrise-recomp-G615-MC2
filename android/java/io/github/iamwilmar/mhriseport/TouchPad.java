package io.github.iamwilmar.mhriseport;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

import io.github.iamwilmar.mhriseport.Controls.Ctl;

// Los controles táctiles, en una ventana transparente encima del juego (GameActivity): la disposición tradicional de un
// mando con todo (los dos sticks, la cruceta, A B X Y, L R ZL ZR, − +, L3 y R3) y los combos que haya. Cada dedo es de
// un control: un botón se pulsa mientras el dedo esté encima, un stick sigue al dedo desde su centro, y un toque corto en
// un stick (sin moverlo) es L3 / R3. Con "deslizar" un dedo que pasa a otro botón suelta el primero y pulsa el segundo.
// Lo que no es de ningún control va al panel del HUD (nativeTouchPoint), como si no hubiera controles.
// En modo edición cada control se arrastra a su sitio; la barra de arriba cambia el tamaño del elegido, lo esconde o lo
// enseña, y lo devuelve todo a su sitio.
@SuppressLint("ViewConstructor")
final class TouchPad extends View {
    interface Host {
        void send(long buttons, int sticksOn, float lx, float ly, float rx, float ry);
        void touchPoint(int action, float x, float y, long timeNs, int width, int height);
        void tick();       // la vibración de un botón pulsado
        void openMenu();
        void editDone();
    }

    private final Controls cfg;
    private final Host host;
    private final Glyphs glyphs = new Glyphs();
    private final Paint barPaint = new Paint(Paint.ANTI_ALIAS_FLAG), barText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();

    private static final int MAX_FINGERS = 10;
    private static final class Finger {
        int id = -1;
        Ctl ctl;          // null: no es de ningún control (va al HUD, si es el primero)
        boolean hud;
        float x0, y0, x, y;
        long down;
        boolean moved;
        long mask;        // los botones que pulsa ahora
        int dirs;         // la cruceta: los brazos
        float dx, dy;     // en edición: la distancia del dedo al centro del control
        boolean camera;   // a swipe on the game: it moves the right stick
        float lastX, lastY;
        long lastT;
    }
    // Camera swipe: a finger that drags on the game, away from the controls, turns the camera through the right stick, by
    // how fast it moves (a phone game's swipe camera; the game only has a stick to turn it with). A tap still goes to the
    // game's touch screen. The stick goes back to the middle when the finger stops.
    private final Runnable cameraStop = () -> {
        rx = ry = 0;
        update();
    };
    private final Finger[] fingers = new Finger[MAX_FINGERS];
    private long pulse;  // L3 / R3 de un toque corto en un stick, unos instantes
    private float lx, ly, rx, ry;
    private int sticksOn;
    private long lastButtons = -1;
    private int lastSticks = -1;

    boolean editing;
    private Ctl selected;

    TouchPad(Context context, Controls cfg, Host host) {
        super(context);
        this.cfg = cfg;
        this.host = host;
        for (int i = 0; i < MAX_FINGERS; i++) fingers[i] = new Finger();
        barText.setTextAlign(Paint.Align.CENTER);
        barText.setColor(Glyphs.CREAM);
    }

    // ---- dónde y cómo de grande es cada control ---------------------------------------------------------------------

    private float unit() { return getHeight() * cfg.size / 100f; }
    private float cx(Ctl c) { return cfg.place[c.ordinal()][0] * getWidth(); }
    private float cy(Ctl c) { return cfg.place[c.ordinal()][1] * getHeight(); }
    private float scale(Ctl c) { return cfg.place[c.ordinal()][2]; }

    // El radio de un control (el de la mitad de su ancho para L, R, ZL y ZR)
    private float radius(Ctl c) {
        float u = unit() * scale(c);
        switch (c) {
            case LSTICK: case RSTICK: return 0.15f * u;
            case DPAD: return 0.13f * u;
            case L: case R: return 0.075f * u;
            case ZL: case ZR: return 0.06f * u;
            case MINUS: case PLUS: case MENU: case L3: case R3: return 0.042f * u;
            case A: case B: case X: case Y: return 0.062f * u;
            default: return 0.06f * u;  // combos
        }
    }

    private boolean active(Ctl c) {
        if (c == Ctl.MENU) return true;  // (siempre: es la forma de volver a encender los demás)
        if (!cfg.touchOn) return false;
        if (c.combo() && cfg.combo[c.comboIndex()] == 0) return false;
        return cfg.shown[c.ordinal()] || editing;
    }

    // El control bajo un punto: el más cercano a su centro entre los que lo tienen dentro (un poco más grandes de lo
    // que se ven, que un pulgar no es fino); null si ninguno
    private Ctl hit(float x, float y) {
        Ctl best = null;
        float bestD = Float.MAX_VALUE;
        for (Ctl c : Ctl.values()) {
            if (!active(c) || (!editing && !cfg.shown[c.ordinal()])) continue;
            float r = radius(c), dx = x - cx(c), dy = y - cy(c);
            float reach = c == Ctl.LSTICK || c == Ctl.RSTICK ? r * 1.35f : c == Ctl.L || c == Ctl.R ? r * 1.15f : r * 1.25f;
            if (c == Ctl.L || c == Ctl.R) dy *= 2.0f;  // (una pastilla: más ancha que alta)
            float d = (float) Math.hypot(dx, dy);
            if (d <= reach && d / reach < bestD) {
                best = c;
                bestD = d / reach;
            }
        }
        return best;
    }

    private static long buttonOf(Ctl c, Controls cfg) {
        switch (c) {
            case A: return Controls.A;
            case B: return Controls.B;
            case X: return Controls.X;
            case Y: return Controls.Y;
            case L: return Controls.L;
            case R: return Controls.R;
            case ZL: return Controls.ZL;
            case ZR: return Controls.ZR;
            case MINUS: return Controls.MINUS;
            case PLUS: return Controls.PLUS;
            case L3: return Controls.LSTICK;
            case R3: return Controls.RSTICK;
            case COMBO1: case COMBO2: case COMBO3: case COMBO4: return cfg.combo[c.comboIndex()];
            default: return 0;
        }
    }
    private static boolean isButton(Ctl c) {
        return c != null && c != Ctl.LSTICK && c != Ctl.RSTICK && c != Ctl.MENU;
    }

    // ---- los dedos ----------------------------------------------------------------------------------------------------

    private Finger finger(int id) {
        for (Finger f : fingers) if (f.id == id) return f;
        return null;
    }
    private Finger free() {
        for (Finger f : fingers) if (f.id < 0) return f;
        return null;
    }
    private boolean hudTaken() {
        for (Finger f : fingers) if (f.id >= 0 && f.hud) return true;
        return false;
    }

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        long timeNs = e.getEventTime() * 1_000_000L;
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                Finger f = free();
                if (f == null) break;
                f.id = e.getPointerId(index);
                f.x = f.x0 = e.getX(index);
                f.y = f.y0 = e.getY(index);
                f.down = SystemClock.uptimeMillis();
                f.moved = false;
                f.mask = 0;
                f.dirs = 0;
                f.hud = false;
                if (editing && toolbar(f.x, f.y)) {
                    f.ctl = null;
                    break;
                }
                f.ctl = hit(f.x, f.y);
                if (editing) {
                    selected = f.ctl;
                    if (f.ctl != null) {
                        f.dx = cx(f.ctl) - f.x;
                        f.dy = cy(f.ctl) - f.y;
                    }
                } else if (f.ctl == null) {
                    if (!hudTaken()) {  // (el panel del HUD sigue un dedo: el primero que no es de un control)
                        f.hud = true;
                        host.touchPoint(MotionEvent.ACTION_DOWN, f.x, f.y, timeNs, getWidth(), getHeight());
                    }
                } else {
                    press(f, true);
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    Finger f = finger(e.getPointerId(i));
                    if (f == null) continue;
                    f.x = e.getX(i);
                    f.y = e.getY(i);
                    if (Math.hypot(f.x - f.x0, f.y - f.y0) > getHeight() * 0.025f) f.moved = true;
                    if (editing) {
                        if (f.ctl != null) {
                            float[] p = cfg.place[f.ctl.ordinal()];
                            p[0] = clamp((f.x + f.dx) / getWidth(), 0.02f, 0.98f);
                            p[1] = clamp((f.y + f.dy) / getHeight(), 0.03f, 0.97f);
                        }
                    } else if (f.camera) {
                        swipe(f, e.getEventTime());
                    } else if (f.ctl == null && cfg.cameraSwipe && f.moved) {
                        // (from touch to camera: the game gets its touch cancelled)
                        if (f.hud) host.touchPoint(MotionEvent.ACTION_CANCEL, f.x, f.y, timeNs, getWidth(), getHeight());
                        f.hud = false;
                        f.camera = true;
                        f.lastX = f.x;
                        f.lastY = f.y;
                        f.lastT = e.getEventTime();
                    } else if (f.hud) {
                        host.touchPoint(MotionEvent.ACTION_MOVE, f.x, f.y, timeNs, getWidth(), getHeight());
                    } else if (f.ctl != null) {
                        move(f);
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                boolean all = action == MotionEvent.ACTION_CANCEL;
                for (int i = 0; i < e.getPointerCount(); i++) {
                    if (!all && i != index) continue;
                    Finger f = finger(e.getPointerId(i));
                    if (f == null) continue;
                    if (f.hud) {
                        host.touchPoint(all ? MotionEvent.ACTION_CANCEL : MotionEvent.ACTION_UP, e.getX(i), e.getY(i), timeNs,
                                getWidth(), getHeight());
                    } else if (!editing && f.ctl != null) {
                        release(f, !all);
                    }
                    if (f.camera) {
                        removeCallbacks(cameraStop);
                        rx = ry = 0;
                        f.camera = false;
                    }
                    f.id = -1;
                    f.ctl = null;
                    f.hud = false;
                }
                break;
            }
            default:
                break;
        }
        update();
        return true;
    }

    private static float clamp(float v, float lo, float hi) { return Math.max(lo, Math.min(hi, v)); }

    // Full stick for a drag of about half the screen's height a second (at 100 %), smoothed over a few events
    private void swipe(Finger f, long timeMs) {
        float dt = Math.max(timeMs - f.lastT, 4) / 1000f;
        float gain = 2.0f * cfg.swipeSpeed / 100f / getHeight();
        float vx = clamp((f.x - f.lastX) / dt * gain, -1, 1), vy = clamp((f.y - f.lastY) / dt * gain, -1, 1);
        rx = rx * 0.4f + vx * 0.6f;
        ry = ry * 0.4f + vy * 0.6f;
        f.lastX = f.x;
        f.lastY = f.y;
        f.lastT = timeMs;
        removeCallbacks(cameraStop);
        postDelayed(cameraStop, 70);
    }

    private void press(Finger f, boolean first) {
        Ctl c = f.ctl;
        if (c == Ctl.LSTICK || c == Ctl.RSTICK) {
            stickTo(f);
            return;
        }
        if (c == Ctl.DPAD) {
            int was = f.dirs;
            f.dirs = dpadDirs(f);
            f.mask = dirMask(f.dirs);
            if (f.dirs != 0 && f.dirs != was) host.tick();
            return;
        }
        if (c == Ctl.MENU) return;  // (al soltarlo)
        f.mask = buttonOf(c, cfg);
        if (first || f.mask != 0) host.tick();
    }

    private void move(Finger f) {
        Ctl c = f.ctl;
        if (c == Ctl.LSTICK || c == Ctl.RSTICK) {
            stickTo(f);
            return;
        }
        if (c == Ctl.DPAD) {
            Ctl under = cfg.slide ? hit(f.x, f.y) : Ctl.DPAD;
            if (under == Ctl.DPAD || under == null || !isButton(under)) {
                press(f, false);
                return;
            }
        }
        if (!cfg.slide) return;
        Ctl under = hit(f.x, f.y);
        if (under != null && under != c && isButton(under)) {  // (al pasar a otro botón: suelta este y pulsa aquel)
            f.ctl = under;
            f.dirs = 0;
            press(f, true);
        }
    }

    private void release(Finger f, boolean lifted) {
        Ctl c = f.ctl;
        if (c == Ctl.MENU && lifted && !f.moved) host.openMenu();
        if ((c == Ctl.LSTICK || c == Ctl.RSTICK) && lifted && cfg.stickPress) {
            // un toque corto sin mover el stick: L3 / R3
            long held = SystemClock.uptimeMillis() - f.down;
            if (held < 250 && Math.hypot(f.x - f.x0, f.y - f.y0) < radius(c) * 0.18f) {
                final long bit = c == Ctl.LSTICK ? Controls.LSTICK : Controls.RSTICK;
                pulse |= bit;
                host.tick();
                postDelayed(() -> {
                    pulse &= ~bit;
                    update();
                }, 90);
            }
        }
        if (c == Ctl.LSTICK) lx = ly = 0;
        if (c == Ctl.RSTICK) rx = ry = 0;
        f.mask = 0;
        f.dirs = 0;
    }

    // El stick sigue al dedo desde su centro; fuera del aro, al tope en esa dirección. La zona muerta del menú, radial.
    private void stickTo(Finger f) {
        Ctl c = f.ctl;
        float r = radius(c) * 0.8f;
        float x = (f.x - cx(c)) / r, y = (f.y - cy(c)) / r;
        float m = (float) Math.hypot(x, y);
        if (m > 1) {
            x /= m;
            y /= m;
            m = 1;
        }
        float zone = cfg.touchDeadzone / 100f;
        if (m <= zone) {
            x = y = 0;
        } else {
            float k = (m - zone) / (1 - zone) / m;
            x *= k;
            y *= k;
        }
        if (c == Ctl.LSTICK) {
            lx = x;
            ly = y;
        } else {
            rx = x;
            ry = y;
        }
    }

    // La cruceta en ocho sectores: arriba, abajo, izquierda, derecha y las diagonales (dos brazos a la vez)
    private int dpadDirs(Finger f) {
        float dx = f.x - cx(Ctl.DPAD), dy = f.y - cy(Ctl.DPAD);
        if (Math.hypot(dx, dy) < radius(Ctl.DPAD) * 0.22f) return 0;
        double a = Math.toDegrees(Math.atan2(dy, dx));  // 0 a la derecha, 90 abajo
        int sector = (int) Math.floor(((a + 360 + 22.5) % 360) / 45);
        switch (sector) {
            case 0: return 8;       // derecha
            case 1: return 8 | 2;   // abajo a la derecha
            case 2: return 2;       // abajo
            case 3: return 4 | 2;
            case 4: return 4;       // izquierda
            case 5: return 4 | 1;
            case 6: return 1;       // arriba
            default: return 8 | 1;
        }
    }
    private static long dirMask(int dirs) {
        long m = 0;
        if ((dirs & 1) != 0) m |= Controls.UP;
        if ((dirs & 2) != 0) m |= Controls.DOWN;
        if ((dirs & 4) != 0) m |= Controls.LEFT;
        if ((dirs & 8) != 0) m |= Controls.RIGHT;
        return m;
    }

    // Lo que tocan los dedos, al loader (solo si cambió)
    private void update() {
        long buttons = pulse;
        int on = 0;
        for (Finger f : fingers) {
            if (f.id < 0 || f.ctl == null || editing) continue;
            buttons |= f.mask;
            if (f.ctl == Ctl.LSTICK) on |= 1;
            if (f.ctl == Ctl.RSTICK) on |= 2;
        }
        for (Finger f : fingers)
            if (f.id >= 0 && f.camera && !editing) on |= 2;
        if (on == 0 && (rx != 0 || ry != 0)) on |= 2;  // (the last swipe's stick going back to the middle)
        sticksOn = on;
        if (buttons != lastButtons || on != 0 || lastSticks != on) {
            host.send(buttons, on, lx, -ly, rx, -ry);  // (al juego, hacia arriba positivo)
            lastButtons = buttons;
            lastSticks = on;
        }
        invalidate();
    }

    // Suelta todo (al abrir el menú o al irse la app a segundo plano)
    void releaseAll() {
        for (Finger f : fingers) {
            if (f.hud) host.touchPoint(MotionEvent.ACTION_CANCEL, f.x, f.y, SystemClock.uptimeMillis() * 1_000_000L, getWidth(),
                    getHeight());
            f.id = -1;
            f.ctl = null;
            f.hud = false;
            f.camera = false;
            f.mask = 0;
        }
        removeCallbacks(cameraStop);
        pulse = 0;
        lx = ly = rx = ry = 0;
        update();
    }

    // ---- la barra del modo edición ------------------------------------------------------------------------------------

    private static final int BAR_SMALLER = 0, BAR_BIGGER = 1, BAR_HIDE = 2, BAR_RESET = 3, BAR_DONE = 4, BAR_ITEMS = 5;

    private RectF barItem(int i) {
        float h = getHeight() * 0.085f, w = getHeight() * 0.22f, gap = h * 0.2f;
        float total = BAR_ITEMS * w + (BAR_ITEMS - 1) * gap;
        float x = (getWidth() - total) / 2 + i * (w + gap), y = getHeight() * 0.18f;
        return new RectF(x, y, x + w, y + h);
    }
    private String barLabel(int i) {
        switch (i) {
            case BAR_SMALLER: return "−";
            case BAR_BIGGER: return "+";
            case BAR_HIDE: return selected != null && !cfg.shown[selected.ordinal()] ? "Show" : "Hide";
            case BAR_RESET: return "Reset";
            default: return "Done";
        }
    }

    // Un toque en la barra: true si era suyo
    private boolean toolbar(float x, float y) {
        for (int i = 0; i < BAR_ITEMS; i++) {
            if (!barItem(i).contains(x, y)) continue;
            switch (i) {
                case BAR_SMALLER:
                case BAR_BIGGER:
                    if (selected != null) {
                        float[] p = cfg.place[selected.ordinal()];
                        p[2] = clamp(p[2] + (i == BAR_BIGGER ? 0.1f : -0.1f), 0.5f, 2.0f);
                    }
                    break;
                case BAR_HIDE:
                    if (selected != null && selected != Ctl.MENU) cfg.shown[selected.ordinal()] = !cfg.shown[selected.ordinal()];
                    break;
                case BAR_RESET:
                    cfg.resetLayout();
                    break;
                default:
                    editing = false;
                    selected = null;
                    host.editDone();
                    break;
            }
            host.tick();
            return true;
        }
        return false;
    }

    // ---- dibujo -------------------------------------------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas c) {
        int alpha = Math.round(255 * cfg.opacity / 100f);
        for (Ctl ctl : Ctl.values()) {
            if (!active(ctl)) continue;
            int a = alpha;
            if (editing && !cfg.shown[ctl.ordinal()]) a = alpha / 3;  // (escondido: se ve tenue, para poder enseñarlo)
            if (editing && ctl == selected) a = 255;
            draw(c, ctl, a);
            if (editing && ctl == selected) {  // (el elegido: un marco dorado de puntos)
                barPaint.setStyle(Paint.Style.STROKE);
                barPaint.setStrokeWidth(getHeight() * 0.004f);
                barPaint.setColor(Glyphs.GOLD);
                barPaint.setPathEffect(new android.graphics.DashPathEffect(new float[] {12, 8}, 0));
                float r = radius(ctl) * 1.25f;
                c.drawCircle(cx(ctl), cy(ctl), r, barPaint);
                barPaint.setPathEffect(null);
            }
        }
        if (editing) drawBar(c);
    }

    private void draw(Canvas c, Ctl ctl, int a) {
        float x = cx(ctl), y = cy(ctl), r = radius(ctl);
        boolean down = pressed(ctl);
        switch (ctl) {
            case LSTICK:
                glyphs.stick(c, x, y, r, lx, ly, (sticksOn & 1) != 0, (pulse & Controls.LSTICK) != 0, a);
                break;
            case RSTICK:
                glyphs.stick(c, x, y, r, rx, ry, (sticksOn & 2) != 0, (pulse & Controls.RSTICK) != 0, a);
                break;
            case DPAD:
                glyphs.dpad(c, x, y, r, dpadHeld(), a);
                break;
            case A: case B: case X: case Y:
                glyphs.face(c, x, y, r, ctl.name(), down, a);
                break;
            case L: case R:
                glyphs.shoulder(c, x, y, r * 2, r * 0.9f, ctl.name(), false, ctl == Ctl.L, down, a);
                break;
            case ZL: case ZR:
                glyphs.shoulder(c, x, y, r * 2, r * 1.3f, ctl.name(), true, ctl == Ctl.ZL, down, a);
                break;
            case MINUS:
                glyphs.small(c, x, y, r, Glyphs.Small.MINUS, down, a);
                break;
            case PLUS:
                glyphs.small(c, x, y, r, Glyphs.Small.PLUS, down, a);
                break;
            case MENU:
                glyphs.small(c, x, y, r, Glyphs.Small.MENU, down, a);
                break;
            case L3:
                glyphs.small(c, x, y, r, Glyphs.Small.L3, down || (pulse & Controls.LSTICK) != 0, a);
                break;
            case R3:
                glyphs.small(c, x, y, r, Glyphs.Small.R3, down || (pulse & Controls.RSTICK) != 0, a);
                break;
            default:
                glyphs.combo(c, x, y, r, Controls.names(cfg.combo[ctl.comboIndex()]), down, a);
                break;
        }
    }

    private boolean pressed(Ctl ctl) {
        for (Finger f : fingers) if (f.id >= 0 && f.ctl == ctl && !editing) return ctl != Ctl.MENU || !f.moved;
        return false;
    }
    private int dpadHeld() {
        int d = 0;
        for (Finger f : fingers) if (f.id >= 0 && f.ctl == Ctl.DPAD) d |= f.dirs;
        return d;
    }

    private void drawBar(Canvas c) {
        barText.setTextSize(getHeight() * 0.04f);
        barText.setTypeface(android.graphics.Typeface.create(android.graphics.Typeface.SERIF, android.graphics.Typeface.BOLD));
        for (int i = 0; i < BAR_ITEMS; i++) {
            RectF b = barItem(i);
            barPaint.setStyle(Paint.Style.FILL);
            barPaint.setColor(i == BAR_DONE ? 0xF0E2B65C : 0xF0281010);
            c.drawRect(b, barPaint);
            barPaint.setStyle(Paint.Style.STROKE);
            barPaint.setStrokeWidth(getHeight() * 0.003f);
            barPaint.setColor(Glyphs.BRONZE);
            c.drawRect(b, barPaint);
            barText.setColor(i == BAR_DONE ? Glyphs.DEEP : Glyphs.CREAM);
            Paint.FontMetrics m = barText.getFontMetrics();
            c.drawText(barLabel(i), b.centerX(), b.centerY() - (m.ascent + m.descent) / 2, barText);
        }
        // debajo, qué hacer
        barText.setTextSize(getHeight() * 0.03f);
        barText.setColor(Glyphs.CREAM);
        String hint = selected == null ? "Drag a control to move it; tap it to choose it."
                : "Chosen: " + label(selected);
        rect.set(barItem(0).left, barItem(0).bottom, barItem(BAR_ITEMS - 1).right, barItem(0).bottom + getHeight() * 0.06f);
        c.drawText(hint, rect.centerX(), rect.centerY() + getHeight() * 0.012f, barText);
    }

    private String label(Ctl c) {
        switch (c) {
            case LSTICK: return "left stick";
            case RSTICK: return "right stick";
            case DPAD: return "D-pad";
            case MENU: return "menu";
            case MINUS: return "−";
            case PLUS: return "+";
            default:
                if (c.combo()) return "combo " + (c.comboIndex() + 1);
                return c.name();
        }
    }
}
