package io.github.iamwilmar.mhriseport;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;

// Los dibujos de los controles táctiles, hechos con trazos (sin imágenes): en los colores del menú del juego, vino oscuro
// con borde de bronce y letras doradas; un control pulsado se llena de oro con las letras en vino. alpha (0..255) es la
// opacidad de todo el control.
final class Glyphs {
    static final int WINE = 0xFF4A1816, DEEP = 0xFF281010, BRONZE = 0xFFC49646, GOLD = 0xFFE2B65C, CREAM = 0xFFF0E4C8;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint edge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ink = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF box = new RectF();

    Glyphs() {
        fill.setStyle(Paint.Style.FILL);
        edge.setStyle(Paint.Style.STROKE);
        ink.setStyle(Paint.Style.FILL);
        ink.setStrokeCap(Paint.Cap.ROUND);
        ink.setStrokeJoin(Paint.Join.ROUND);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
    }

    private static int withAlpha(int color, int alpha) {
        return (color & 0x00FFFFFF) | ((((color >>> 24) * alpha) / 255) << 24);
    }

    // Plato y borde de un control: vino (o oro pulsado), con un aro de bronce
    private void plate(int alpha, boolean pressed, float stroke) {
        fill.setColor(withAlpha(pressed ? GOLD : WINE, pressed ? alpha : alpha * 9 / 10));
        edge.setColor(withAlpha(BRONZE, alpha));
        edge.setStrokeWidth(stroke);
    }
    private int inkColor(int alpha, boolean pressed) { return withAlpha(pressed ? DEEP : GOLD, alpha); }

    private void label(Canvas c, String s, float cx, float cy, float size, int color) {
        text.setTextSize(size);
        text.setColor(color);
        Paint.FontMetrics m = text.getFontMetrics();
        c.drawText(s, cx, cy - (m.ascent + m.descent) / 2, text);
    }

    // A, B, X, Y: un círculo con su letra y un segundo aro fino por dentro
    void face(Canvas c, float cx, float cy, float r, String letter, boolean pressed, int alpha) {
        plate(alpha, pressed, r * 0.07f);
        c.drawCircle(cx, cy, r, fill);
        c.drawCircle(cx, cy, r, edge);
        edge.setStrokeWidth(r * 0.025f);
        edge.setColor(withAlpha(BRONZE, alpha / 2));
        c.drawCircle(cx, cy, r * 0.82f, edge);
        label(c, letter, cx, cy, r * 1.05f, inkColor(alpha, pressed));
    }

    // L, R: una pastilla; ZL, ZR: un gatillo, con la punta de fuera más alta y redonda
    void shoulder(Canvas c, float cx, float cy, float w, float h, String name, boolean trigger, boolean left, boolean pressed,
                  int alpha) {
        plate(alpha, pressed, h * 0.08f);
        path.reset();
        float l = cx - w / 2, r = cx + w / 2, t = cy - h / 2, b = cy + h / 2;
        if (trigger) {
            float lift = h * 0.35f;  // (el lado de fuera sube: la forma del gatillo de un mando)
            float outer = left ? l : r, inner = left ? r : l, dir = left ? 1 : -1;
            path.moveTo(inner, b);
            path.lineTo(outer + dir * h * 0.45f, b);
            path.quadTo(outer, b, outer, b - h * 0.45f);
            path.lineTo(outer, t + h * 0.1f);
            path.quadTo(outer, t - lift, outer + dir * w * 0.45f, t - lift * 0.4f);
            path.quadTo(inner - dir * w * 0.1f, t, inner, t + h * 0.15f);
            path.close();
        } else {
            box.set(l, t, r, b);
            path.addRoundRect(box, h / 2, h / 2, Path.Direction.CW);
        }
        c.drawPath(path, fill);
        c.drawPath(path, edge);
        label(c, name, cx, cy + (trigger ? h * 0.05f : 0), h * 0.62f, inkColor(alpha, pressed));
    }

    // Botón pequeño redondo: − y + (sus barras), el menú (un engranaje), L3 y R3 (un stick visto desde arriba con su letra)
    enum Small { MINUS, PLUS, MENU, L3, R3 }

    void small(Canvas c, float cx, float cy, float r, Small kind, boolean pressed, int alpha) {
        plate(alpha, pressed, r * 0.09f);
        c.drawCircle(cx, cy, r, fill);
        c.drawCircle(cx, cy, r, edge);
        int color = inkColor(alpha, pressed);
        ink.setColor(color);
        float bar = r * 0.5f, thick = r * 0.16f;
        switch (kind) {
            case MINUS:
                box.set(cx - bar, cy - thick / 2, cx + bar, cy + thick / 2);
                c.drawRoundRect(box, thick / 2, thick / 2, ink);
                break;
            case PLUS:
                box.set(cx - bar, cy - thick / 2, cx + bar, cy + thick / 2);
                c.drawRoundRect(box, thick / 2, thick / 2, ink);
                box.set(cx - thick / 2, cy - bar, cx + thick / 2, cy + bar);
                c.drawRoundRect(box, thick / 2, thick / 2, ink);
                break;
            case MENU:
                gear(c, cx, cy, r * 0.62f, color, pressed ? GOLD : WINE, alpha);
                break;
            case L3:
            case R3:
                // (la seta de un stick vista desde arriba, arriba, y su nombre debajo)
                edge.setColor(color);
                edge.setStrokeWidth(r * 0.08f);
                c.drawCircle(cx, cy - r * 0.22f, r * 0.36f, edge);
                c.drawCircle(cx, cy - r * 0.22f, r * 0.14f, ink);
                label(c, kind == Small.L3 ? "L3" : "R3", cx, cy + r * 0.45f, r * 0.46f, color);
                break;
        }
    }

    // Un engranaje de ocho dientes con su agujero (el botón del menú)
    void gear(Canvas c, float cx, float cy, float r, int color, int hole, int alpha) {
        path.reset();
        int teeth = 8;
        for (int i = 0; i < teeth * 2; i++) {
            double a0 = Math.PI * 2 * i / (teeth * 2) - Math.PI / (teeth * 2);
            double a1 = a0 + Math.PI / teeth;
            float rr = i % 2 == 0 ? r : r * 0.76f;
            float x0 = cx + (float) Math.cos(a0) * rr, y0 = cy + (float) Math.sin(a0) * rr;
            if (i == 0) path.moveTo(x0, y0);
            else path.lineTo(x0, y0);
            path.lineTo(cx + (float) Math.cos(a1) * rr, cy + (float) Math.sin(a1) * rr);
        }
        path.close();
        ink.setColor(color);
        c.drawPath(path, ink);
        ink.setColor(withAlpha(hole, alpha));
        c.drawCircle(cx, cy, r * 0.34f, ink);
    }

    // La cruceta: una cruz con una flecha en cada brazo; dirs, los brazos pulsados (bit 0 arriba, 1 abajo, 2 izquierda,
    // 3 derecha)
    void dpad(Canvas c, float cx, float cy, float half, int dirs, int alpha) {
        float arm = half * 0.36f;
        path.reset();
        path.moveTo(cx - arm, cy - half);
        path.lineTo(cx + arm, cy - half);
        path.lineTo(cx + arm, cy - arm);
        path.lineTo(cx + half, cy - arm);
        path.lineTo(cx + half, cy + arm);
        path.lineTo(cx + arm, cy + arm);
        path.lineTo(cx + arm, cy + half);
        path.lineTo(cx - arm, cy + half);
        path.lineTo(cx - arm, cy + arm);
        path.lineTo(cx - half, cy + arm);
        path.lineTo(cx - half, cy - arm);
        path.lineTo(cx - arm, cy - arm);
        path.close();
        plate(alpha, false, half * 0.05f);
        c.drawPath(path, fill);
        // los brazos pulsados, en oro
        fill.setColor(withAlpha(GOLD, alpha));
        if ((dirs & 1) != 0) { box.set(cx - arm, cy - half, cx + arm, cy - arm); c.drawRect(box, fill); }
        if ((dirs & 2) != 0) { box.set(cx - arm, cy + arm, cx + arm, cy + half); c.drawRect(box, fill); }
        if ((dirs & 4) != 0) { box.set(cx - half, cy - arm, cx - arm, cy + arm); c.drawRect(box, fill); }
        if ((dirs & 8) != 0) { box.set(cx + arm, cy - arm, cx + half, cy + arm); c.drawRect(box, fill); }
        c.drawPath(path, edge);
        float tip = half * 0.82f, base = half * 0.52f, w = arm * 0.62f;
        for (int d = 0; d < 4; d++) {
            ink.setColor(inkColor(alpha, (dirs & (1 << d)) != 0));
            float sx = d == 2 ? -1 : d == 3 ? 1 : 0, sy = d == 0 ? -1 : d == 1 ? 1 : 0;
            path.reset();
            path.moveTo(cx + sx * tip, cy + sy * tip);
            path.lineTo(cx + sx * base + sy * w, cy + sy * base + sx * w);
            path.lineTo(cx + sx * base - sy * w, cy + sy * base - sx * w);
            path.close();
            c.drawPath(path, ink);
        }
        ink.setColor(withAlpha(BRONZE, alpha));
        c.drawCircle(cx, cy, arm * 0.35f, ink);
    }

    // Un stick: la base con un aro de marcas cada 45 grados, y la seta donde va el dedo (kx, ky de -1 a 1, y hacia abajo
    // positivo); con un dedo encima la seta se aclara, y en oro mientras es L3 / R3
    void stick(Canvas c, float cx, float cy, float r, float kx, float ky, boolean held, boolean pressed, int alpha) {
        fill.setColor(withAlpha(DEEP, alpha * 7 / 10));
        c.drawCircle(cx, cy, r, fill);
        edge.setColor(withAlpha(BRONZE, alpha));
        edge.setStrokeWidth(r * 0.035f);
        c.drawCircle(cx, cy, r, edge);
        edge.setStrokeWidth(r * 0.015f);
        edge.setColor(withAlpha(BRONZE, alpha / 2));
        c.drawCircle(cx, cy, r * 0.70f, edge);
        ink.setColor(withAlpha(BRONZE, alpha * 3 / 4));
        for (int i = 0; i < 8; i++) {  // (las ocho direcciones: rombos pequeños en el borde)
            double a = Math.PI / 4 * i;
            float mx = cx + (float) Math.cos(a) * r * 0.86f, my = cy + (float) Math.sin(a) * r * 0.86f, d = r * (i % 2 == 0 ? 0.06f : 0.04f);
            path.reset();
            path.moveTo(mx, my - d);
            path.lineTo(mx + d, my);
            path.lineTo(mx, my + d);
            path.lineTo(mx - d, my);
            path.close();
            c.drawPath(path, ink);
        }
        float knob = r * 0.42f, x = cx + kx * (r - knob * 0.6f), y = cy + ky * (r - knob * 0.6f);
        plate(alpha, pressed, knob * 0.1f);
        if (held && !pressed) fill.setColor(withAlpha(0xFF6A2A22, alpha));
        c.drawCircle(x, y, knob, fill);
        c.drawCircle(x, y, knob, edge);
        edge.setColor(inkColor(alpha, pressed));
        edge.setStrokeWidth(knob * 0.06f);
        c.drawCircle(x, y, knob * 0.62f, edge);  // (el agarre de goma de la seta)
        ink.setColor(inkColor(alpha, pressed));
        c.drawCircle(x, y, knob * 0.12f, ink);
    }

    // Un combo: dos aros que se cruzan, y debajo los botones que pulsa ("A+X")
    void combo(Canvas c, float cx, float cy, float r, String buttons, boolean pressed, int alpha) {
        plate(alpha, pressed, r * 0.07f);
        box.set(cx - r, cy - r * 0.8f, cx + r, cy + r * 0.8f);
        c.drawRoundRect(box, r * 0.45f, r * 0.45f, fill);
        c.drawRoundRect(box, r * 0.45f, r * 0.45f, edge);
        int color = inkColor(alpha, pressed);
        edge.setColor(color);
        edge.setStrokeWidth(r * 0.07f);
        c.drawCircle(cx - r * 0.2f, cy - r * 0.28f, r * 0.26f, edge);
        c.drawCircle(cx + r * 0.2f, cy - r * 0.28f, r * 0.26f, edge);
        float size = r * (buttons.length() <= 3 ? 0.55f : buttons.length() <= 6 ? 0.42f : 0.32f);
        label(c, buttons, cx, cy + r * 0.38f, size, color);
    }
}
