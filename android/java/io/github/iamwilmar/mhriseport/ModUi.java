package io.github.iamwilmar.mhriseport;

import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.view.MotionEvent;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

final class ModUi extends GLSurfaceView implements GLSurfaceView.Renderer {
    private static native void nativeCreate(float density);
    private static native int nativeFrame(int width,int height);
    private static native void nativeSize(int width,int height);
    private static native boolean nativePending();
    private static native void nativeMenu(boolean open);
    private static native void nativeTouch(float x,float y,boolean down);
    private static native void nativeChar(int codepoint);
    private static native void nativeClose();
    private int width,height;
    private volatile boolean menu, keyboard;
    private boolean created;
    private final GameActivity activity;
    private final WindowManager.LayoutParams panel;
    private final Runnable poll = new Runnable() {
        public void run() {
            if (menu && !nativeMenuOpen()) setMenu(false);
            if (nativePending()) requestRender();
            postOnAnimation(this);
        }
    };
    ModUi(GameActivity a) {
        super(a);
        activity = a;
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(true);
        setFocusableInTouchMode(true);
        setRenderer(this);
        setRenderMode(RENDERMODE_WHEN_DIRTY);
        panel = new WindowManager.LayoutParams(-1, -1, WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE,
                PixelFormat.TRANSLUCENT);
        panel.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        panel.token = a.getWindow().getDecorView().getWindowToken();
        panel.setTitle("Lua HUD");
        a.getWindowManager().addView(this, panel);
    }
    void setMenu(boolean open) {
        if (menu == open) return;
        menu = open;
        nativeMenu(open);
        int pass = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        if (open) panel.flags &= ~pass;
        else panel.flags |= pass;
        activity.getWindowManager().updateViewLayout(this, panel);
        if (open) requestFocus();
        else setKeyboard(false);
    }
    void dispose() {
        nativeMenu(false);
        activity.getWindowManager().removeViewImmediate(this);
    }
    @Override public boolean onKeyUp(int key, KeyEvent event) {
        if (menu && key == KeyEvent.KEYCODE_BACK) {
            setMenu(false);
            return true;
        }
        return super.onKeyUp(key, event);
    }
    @Override public void onResume() {
        super.onResume();
        removeCallbacks(poll);
        postOnAnimation(poll);
    }
    @Override public void onPause() {
        removeCallbacks(poll);
        queueEvent(this::closeRenderer);
        super.onPause();
    }
    @Override protected void onDetachedFromWindow() {
        removeCallbacks(poll);
        super.onDetachedFromWindow();
    }
    private void closeRenderer() {
        if (created) nativeClose();
        created = false;
    }
    public void onSurfaceCreated(GL10 gl,EGLConfig config) {
        closeRenderer();
        nativeCreate(getResources().getDisplayMetrics().density);
        created = true;
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder){
        queueEvent(this::closeRenderer);
        super.surfaceDestroyed(holder);
    }
    public void onSurfaceChanged(GL10 gl,int w,int h){
        if (!created) onSurfaceCreated(gl,null);
        width=w;height=h;nativeSize(w,h);
    }
    public void onDrawFrame(GL10 gl){
        int state=nativeFrame(width,height);
        boolean wants = (state & 2) != 0;
        if (menu && wants != keyboard)
            post(() -> { if (isAttachedToWindow() && menu) setKeyboard(wants); });
    }
    private static native boolean nativeMenuOpen();
    private void setKeyboard(boolean wants) {
        if (keyboard == wants) return;
        keyboard = wants;
        InputMethodManager im=getContext().getSystemService(InputMethodManager.class);
        if(wants)im.showSoftInput(this,InputMethodManager.SHOW_IMPLICIT);
        else im.hideSoftInputFromWindow(getWindowToken(),0);
    }
    @Override public boolean onTouchEvent(MotionEvent ev){
        int action=ev.getActionMasked();float x=ev.getX(),y=ev.getY();
        boolean down=action!=MotionEvent.ACTION_UP && action!=MotionEvent.ACTION_CANCEL;
        queueEvent(()->nativeTouch(x,y,down));return true;
    }
    @Override public boolean onCheckIsTextEditor(){return true;}
    @Override public InputConnection onCreateInputConnection(EditorInfo info){
        info.inputType=android.text.InputType.TYPE_CLASS_TEXT;
        return new BaseInputConnection(this,false){
            @Override public boolean commitText(CharSequence s,int pos){
                int[] chars=s.toString().codePoints().toArray();queueEvent(()->{for(int c:chars)nativeChar(c);});return true;
            }
            @Override public boolean deleteSurroundingText(int before,int after){
                queueEvent(()->{for(int i=0;i<before;i++)nativeChar(8);});return true;
            }
        };
    }
}
