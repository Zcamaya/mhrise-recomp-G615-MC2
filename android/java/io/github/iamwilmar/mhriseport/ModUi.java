package io.github.iamwilmar.mhriseport;

import android.app.Activity;
import android.app.Dialog;
import android.opengl.GLSurfaceView;
import android.view.MotionEvent;
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
    private static native boolean nativeFrame(int width,int height);
    private static native void nativeTouch(float x,float y,boolean down);
    private static native void nativeChar(int codepoint);
    private static native void nativeClose();
    private int width,height;
    private boolean keyboard;
    private ModUi(Activity a) {
        super(a);setEGLContextClientVersion(3);setFocusableInTouchMode(true);setRenderer(this);
    }
    static void show(Activity a) {
        Dialog dialog=new Dialog(a);
        ModUi view=new ModUi(a);
        dialog.setTitle("MOD UI");dialog.setContentView(view);
        dialog.show();dialog.getWindow().setLayout((int)(a.getResources().getDisplayMetrics().widthPixels*.82),
            (int)(a.getResources().getDisplayMetrics().heightPixels*.8));
        dialog.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM);
        view.requestFocus();
    }
    public void onSurfaceCreated(GL10 gl,EGLConfig config){nativeCreate(getResources().getDisplayMetrics().density);}
    @Override public void surfaceDestroyed(SurfaceHolder holder){
        queueEvent(ModUi::nativeClose);
        super.surfaceDestroyed(holder);
    }
    public void onSurfaceChanged(GL10 gl,int w,int h){width=w;height=h;}
    public void onDrawFrame(GL10 gl){
        boolean wants=nativeFrame(width,height);
        if(wants!=keyboard){keyboard=wants;post(()->{
            InputMethodManager im=getContext().getSystemService(InputMethodManager.class);
            if(wants)im.showSoftInput(this,InputMethodManager.SHOW_IMPLICIT);
            else im.hideSoftInputFromWindow(getWindowToken(),0);
        });}
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
