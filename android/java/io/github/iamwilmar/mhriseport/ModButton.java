package io.github.iamwilmar.mhriseport;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.view.MotionEvent;
import android.view.View;

// The floating MOD UI button (Mods → MOD UI button): tap it to open or close the scripts' windows, drag it out of the
// way. Like the capture button, it sits in a window of its own that only covers it, and remembers where it was left.
final class ModButton extends View {
    static final String SWITCH = "mod_ui_button";

    interface Host {
        void toggle();
        void moved(int x, int y);
        void tick();
    }

    private final Host host;
    private final SharedPreferences prefs;
    private final float density;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private float downX, downY;
    private int startX, startY;
    private boolean dragging;
    int x, y;

    ModButton(Context context, Host host) {
        super(context);
        this.host = host;
        density = context.getResources().getDisplayMetrics().density;
        prefs = context.getSharedPreferences("mod_button", Context.MODE_PRIVATE);
        x = prefs.getInt("x", Math.round(16 * density));
        y = prefs.getInt("y", Math.round(160 * density));
        paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        paint.setTextAlign(Paint.Align.CENTER);
    }

    int size() {
        return Math.round(48 * density);
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downX = e.getRawX();
                downY = e.getRawY();
                startX = x;
                startY = y;
                dragging = false;
                return true;
            case MotionEvent.ACTION_MOVE: {
                float dx = e.getRawX() - downX, dy = e.getRawY() - downY;
                if (!dragging && dx * dx + dy * dy > 100 * density * density) dragging = true;
                if (dragging) {
                    x = startX + Math.round(dx);
                    y = startY + Math.round(dy);
                    host.moved(x, y);
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
                if (dragging) {
                    prefs.edit().putInt("x", x).putInt("y", y).apply();
                } else {
                    host.tick();
                    host.toggle();
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                return true;
        }
        return false;
    }

    @Override
    protected void onDraw(Canvas c) {
        float w = getWidth(), cx = w / 2, cy = getHeight() / 2f, r = w / 2 - 2 * density;
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xB0201410);
        c.drawCircle(cx, cy, r, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2 * density);
        paint.setColor(0xFFE8D9B0);
        c.drawCircle(cx, cy, r, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setTextSize(13 * density);
        c.drawText("MOD", cx, cy - (paint.descent() + paint.ascent()) / 2, paint);
    }
}
