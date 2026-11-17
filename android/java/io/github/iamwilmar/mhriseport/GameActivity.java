package io.github.iamwilmar.mhriseport;

import android.app.NativeActivity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.view.Gravity;
import android.os.Vibrator;
import android.view.View;
import android.view.WindowManager;

import java.io.File;

// El juego: la NativeActivity de siempre (libnier.so, android_main.cpp), con los controles táctiles encima.
// La NativeActivity se queda con la superficie de su ventana y con su entrada, así que una vista suya no se vería: los
// controles van en una ventana propia, un panel transparente pegado a la del juego (TYPE_APPLICATION_PANEL). No toma el
// foco (FLAG_NOT_FOCUSABLE): las teclas y los ejes del mando siguen yendo al loader. Los toques sí son suyos; los que no
// caen en un control se los pasa al loader para el panel del HUD (nativeTouchPoint).
public final class GameActivity extends NativeActivity implements TouchPad.Host {
    static {
        // (la NativeActivity carga libnier.so por su cuenta; esto la registra también para los métodos native de aquí)
        System.loadLibrary("nier");
    }

    static native void nativeTouch(long buttons, int sticksOn, float lx, float ly, float rx, float ry);
    static native void nativeTouchPoint(int action, float x, float y, long timeNs, int viewWidth, int viewHeight);
    static native void nativePad(float deadzone, float trigger, int rightAxes, long[] combos, long[] mapping);
    static native void nativeTextAnswer(int serial, String text);
    static native void nativeTurbo(boolean on);

    private Controls controls;
    private TouchPad pad;
    private boolean shown;
    private Vibrator vibrator;
    private CaptureButton capture;  // the floating capture button, while Debug → Capture button is on
    private WindowManager.LayoutParams captureLp;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        controls = new Controls(this);
        vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        applyPad();
        pad = new TouchPad(this, controls, this);
        // La ventana del juego tiene que estar puesta para colgarle el panel: después del primer paso por la cola.
        getWindow().getDecorView().post(this::showPad);
    }

    private void showPad() {
        if (shown || isFinishing()) return;
        View decor = getWindow().getDecorView();
        if (decor.getWindowToken() == null) {  // (aún no: otra vuelta)
            decor.postDelayed(this::showPad, 50);
            return;
        }
        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT);
        lp.token = decor.getWindowToken();
        lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        lp.setTitle("controles");
        try {
            getWindowManager().addView(pad, lp);
            shown = true;
            showCapture();
        } catch (RuntimeException e) {
            // (sin panel el juego sigue con el mando físico y el panel del HUD, como antes)
            System.err.println("[controles] no se pudo poner la ventana de los controles: " + e);
        }
    }

    // GPU boost (Performance): on while the game is in front, if the switch is on and the GPU is an Adreno
    private void applyTurbo(boolean front) {
        File folder = SettingsMenu.gameFolder(this);
        boolean on = front && folder != null && new Ini(folder).on("adreno_turbo") && GpuInfo.read(folder).adreno();
        try {
            nativeTurbo(on);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyTurbo(true);
    }

    @Override
    protected void onPause() {
        applyTurbo(false);
        if (pad != null) pad.releaseAll();  // (un dedo que se queda en un botón al irse la app no lo deja pulsado)
        if (controls != null) controls.saveNow();  // (the app may not come back: swiped away from recents, killed for memory)
        super.onPause();
    }

    // The capture button goes in its own small window above the controls' one, only as big as the button, so touches
    // anywhere else go on to the controls and the game. It follows the switch: on and off from the menu, live.
    private void showCapture() {
        if (!shown) return;
        File folder = SettingsMenu.gameFolder(this);
        boolean want = folder != null && new Ini(folder).on(CaptureButton.SWITCH);
        if (want == (capture != null)) return;
        if (!want) {
            try {
                getWindowManager().removeViewImmediate(capture);
            } catch (RuntimeException ignored) {
            }
            capture = null;
            return;
        }
        CaptureButton b = new CaptureButton(this, new CaptureButton.Host() {
            @Override public File folder() { return SettingsMenu.gameFolder(GameActivity.this); }
            @Override public void moved(int x, int y) { placeCapture(x, y); }
            @Override public void tick() { GameActivity.this.tick(); }
        });
        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(b.size(), b.size(),
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                        | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT);
        lp.token = getWindow().getDecorView().getWindowToken();
        lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        lp.gravity = Gravity.TOP | Gravity.LEFT;
        lp.setTitle("capture");
        try {
            getWindowManager().addView(b, lp);
            capture = b;
            captureLp = lp;
            placeCapture(b.x, b.y);
        } catch (RuntimeException e) {
            System.err.println("[controles] couldn't add the capture button: " + e);
        }
    }

    // Kept on the screen, even if it was left near an edge on a bigger screen or in the other orientation.
    private void placeCapture(int x, int y) {
        if (capture == null) return;
        View decor = getWindow().getDecorView();
        int size = capture.size();
        captureLp.x = capture.x = Math.max(0, Math.min(x, decor.getWidth() - size));
        captureLp.y = capture.y = Math.max(0, Math.min(y, decor.getHeight() - size));
        try {
            getWindowManager().updateViewLayout(capture, captureLp);
        } catch (RuntimeException ignored) {
        }
    }

    @Override
    protected void onDestroy() {
        if (capture != null) {
            try {
                getWindowManager().removeViewImmediate(capture);
            } catch (RuntimeException ignored) {
            }
            capture = null;
        }
        if (shown) {
            try {
                getWindowManager().removeViewImmediate(pad);
            } catch (RuntimeException ignored) {
            }
            shown = false;
        }
        super.onDestroy();
    }

    // Los ajustes del mando físico, al loader
    void applyPad() {
        try {
            nativePad(controls.padDeadzone / 100f, controls.trigger / 100f, controls.rightAxes, controls.padCombos(),
                    controls.padMapping());
        } catch (UnsatisfiedLinkError e) {
            System.err.println("[controles] el loader no tiene nativePad: " + e);
        }
    }

    void refresh() {
        controls.save();
        applyPad();
        pad.invalidate();
        showCapture();
        applyTurbo(true);
    }

    void startEditing() {
        pad.releaseAll();
        pad.editing = true;
        pad.invalidate();
    }

    // ---- TouchPad.Host ----

    @Override
    public void send(long buttons, int sticksOn, float lx, float ly, float rx, float ry) {
        try {
            nativeTouch(buttons, sticksOn, lx, ly, rx, ry);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    @Override
    public void touchPoint(int action, float x, float y, long timeNs, int width, int height) {
        try {
            nativeTouchPoint(action, x, y, timeNs, width, height);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    // Un golpe corto del vibrador al pulsar: suave, medio o fuerte. Con un vibrador sin control de fuerza, solo cambia
    // lo que dura.
    @Override
    public void tick() {
        int level = controls.vibration;
        if (level <= 0 || vibrator == null || !vibrator.hasVibrator()) return;
        long ms = level == 1 ? 8 : level == 2 ? 14 : 22;
        int amplitude = level == 1 ? 60 : level == 2 ? 140 : 255;
        vibrator.vibrate(VibrationEffect.createOneShot(ms, vibrator.hasAmplitudeControl() ? amplitude
                                                                                          : VibrationEffect.DEFAULT_AMPLITUDE));
    }

    // Software keyboard (swkbd.cpp, TextPrompt). The loader calls this from its own thread; the answer goes back with the
    // request's serial (null: cancelled). Touch buttons are released first, since the game waits on the text with
    // whatever was held down.
    void askText(int serial, String header, String guide, String initial, String error, int minLength, int maxLength,
                 boolean password, boolean multiline) {
        runOnUiThread(() -> {
            if (pad != null) pad.releaseAll();
            new TextPrompt(this).show(header, guide, initial, error, minLength, maxLength, password, multiline, text -> {
                try {
                    nativeTextAnswer(serial, text);
                } catch (UnsatisfiedLinkError e) {
                    System.err.println("[keyboard] loader has no nativeTextAnswer: " + e);
                }
            });
        });
    }

    @Override
    public void openMenu() {
        pad.releaseAll();
        new SettingsMenu(this, controls).show();
    }

    @Override
    public void editDone() {
        controls.saveNow();
        pad.invalidate();
    }

    @Override
    public void layoutChanged() {
        controls.save();
    }
}
