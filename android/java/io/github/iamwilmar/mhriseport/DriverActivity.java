package io.github.iamwilmar.mhriseport;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import static io.github.iamwilmar.mhriseport.ControlsMenu.CREAM;
import static io.github.iamwilmar.mhriseport.ControlsMenu.DIM;
import static io.github.iamwilmar.mhriseport.ControlsMenu.EDGE;
import static io.github.iamwilmar.mhriseport.ControlsMenu.GOLD;
import static io.github.iamwilmar.mhriseport.ControlsMenu.GREEN;
import static io.github.iamwilmar.mhriseport.ControlsMenu.RED;
import static io.github.iamwilmar.mhriseport.ControlsMenu.ROW;
import static io.github.iamwilmar.mhriseport.SetupActivity.tr;

// "Driver de GPU": un driver Vulkan propio para una Adreno (Turnip, o una versión de Qualcomm más nueva), en vez del
// del sistema. Acepta los .zip de siempre de estos drivers (meta.json y la librería .so, como los de AdrenoTools). Cada uno
// se descomprime en files/drivers/<carpeta>/ (el linker no carga librerías de la SD) y el elegido se apunta en
// files/drivers/elegido, que lee el juego al arrancar (runtime/src/nnsdk/nvn/driver.h). Si con él no llega a dibujar, el
// juego vuelve solo al del sistema y deja files/drivers/fallo, que esta pantalla enseña y borra.
// Se abre desde la pantalla de inicio y desde el menú de ajustes del juego; el cambio vale desde el siguiente arranque.
public final class DriverActivity extends Activity {
    static final String FROM_GAME = "desde_juego";
    private static final int PICK = 1;
    private static final int PANEL = 0xFF201010;

    private LinearLayout list;
    private TextView status;
    private String startedWith;  // el elegido al abrir: si cambia y venimos del juego, se ofrece cerrarlo

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(PANEL);

        LinearLayout top = new LinearLayout(this);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setPadding(dp(18), dp(10), dp(10), dp(10));
        TextView title = text(tr("AJUSTES", "SETTINGS"), 22, GOLD, true);
        title.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        TextView sub = text("  ◆  " + tr("Driver de GPU", "GPU driver"), 17, CREAM, false);
        TextView close = text("✕", 22, CREAM, false);
        close.setPadding(dp(14), dp(4), dp(14), dp(4));
        close.setBackground(plate(0xFF1A0C09, 1, 2));
        close.setOnClickListener(v -> finish());
        top.addView(title);
        top.addView(sub, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        top.addView(close);
        root.addView(top);
        root.addView(rule(RED, 2));
        root.addView(rule(GREEN, 1));

        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(dp(16), dp(6), dp(16), dp(16));
        ScrollView scroll = new ScrollView(this);
        scroll.addView(list);
        root.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        setContentView(root);

        startedWith = selected()[0];
        build();
    }

    private File root() {
        return new File(getFilesDir(), "drivers");
    }

    // ---- pantalla -------------------------------------------------------------------------------------------------------

    private void build() {
        list.removeAllViews();
        note(tr("Solo para GPU Adreno (Snapdragon), a tu riesgo. Agrega un .zip de driver (meta.json y la librería .so, como "
                        + "los de Turnip para AdrenoTools) y elígelo. Vale desde el siguiente arranque del juego; si con él no "
                        + "llega a dibujar, vuelve solo al del sistema.",
                "Adreno GPUs (Snapdragon) only, at your own risk. Add a driver .zip (meta.json and the .so library, like the "
                        + "Turnip ones for AdrenoTools) and pick it. It takes effect the next time the game starts; if the game "
                        + "can't draw with it, it goes back to the system's by itself."));
        status = text("", 14, CREAM, false);
        status.setPadding(dp(4), dp(6), dp(4), 0);
        status.setVisibility(View.GONE);
        list.addView(status);
        File failed = new File(root(), "fallo");
        if (failed.isFile()) {
            say(tr("El último no funcionó: ", "The last one didn't work: ") + firstLine(failed)
                    + tr(". Se usa el del sistema.", ". The system's is in use."), true);
            failed.delete();
        }

        String chosen = selected()[0];
        header(tr("Drivers", "Drivers"));
        entry(tr("Del sistema", "System driver"), tr("El que trae el teléfono", "The one that came with the phone"),
                chosen == null, () -> choose(null), null);
        File[] folders = root().listFiles(File::isDirectory);
        if (folders != null) {
            Arrays.sort(folders, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
            for (File dir : folders) {
                String[] info = describe(dir);
                if (info == null) continue;
                entry(info[0], info[1], dir.getName().equals(chosen), () -> choose(dir), () -> remove(dir));
            }
        }
        TextView add = button(tr("Agregar un driver (.zip)", "Add a driver (.zip)"), true);
        add.setOnClickListener(v -> pick());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(50));
        lp.topMargin = dp(14);
        list.addView(add, lp);

        String now = selected()[0];
        boolean changed = now == null ? startedWith != null : !now.equals(startedWith);
        if (getIntent().getBooleanExtra(FROM_GAME, false) && changed) {
            note(tr("El juego sigue con el driver con el que arrancó. Ciérralo y vuelve a abrir la app para usar el nuevo.",
                    "The game keeps the driver it started with. Close it and open the app again to use the new one."));
            TextView quit = button(tr("Cerrar el juego", "Close the game"), false);
            quit.setOnClickListener(v -> {
                finishAffinity();
                Process.killProcess(Process.myPid());
            });
            LinearLayout.LayoutParams q = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46));
            q.topMargin = dp(8);
            list.addView(quit, q);
        }
    }

    // Una fila: nombre y detalle, ✓ si es el elegido, y "Quitar" si se puede borrar
    private void entry(String name, String detail, boolean on, Runnable choose, Runnable remove) {
        LinearLayout r = new LinearLayout(this);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(dp(14), dp(10), dp(8), dp(10));
        r.setBackground(plate(on ? 0xFF3E2616 : ROW, on ? 2 : 1, 2));
        TextView mark = text(on ? "✓" : "", 18, GOLD, true);
        mark.setGravity(Gravity.CENTER);
        r.addView(mark, new LinearLayout.LayoutParams(dp(26), ViewGroup.LayoutParams.WRAP_CONTENT));
        LinearLayout words = new LinearLayout(this);
        words.setOrientation(LinearLayout.VERTICAL);
        words.addView(text(name, 16, on ? GOLD : CREAM, on));
        if (detail != null && !detail.isEmpty()) words.addView(text(detail, 13, DIM, false));
        r.addView(words, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        if (remove != null) {
            TextView x = button(tr("Quitar", "Remove"), false);
            x.setOnClickListener(v -> remove.run());
            r.addView(x, new LinearLayout.LayoutParams(dp(92), dp(40)));
        }
        r.setOnClickListener(v -> choose.run());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(4);
        list.addView(r, lp);
    }

    private void say(String s, boolean bad) {
        status.setText(s);
        status.setTextColor(bad ? 0xFFE07060 : CREAM);
        status.setVisibility(View.VISIBLE);
    }

    // ---- elegir, quitar -------------------------------------------------------------------------------------------------

    // [carpeta, librería] del elegido, o [null, null]: el del sistema
    private String[] selected() {
        List<String> lines = lines(new File(root(), "elegido"));
        if (lines.size() < 2 || !new File(new File(root(), lines.get(0)), lines.get(1)).isFile()) return new String[2];
        return new String[] {lines.get(0), lines.get(1)};
    }

    private void choose(File dir) {
        File chosen = new File(root(), "elegido");
        new File(root(), "probando").delete();  // (lo que quedara de otro driver)
        if (dir == null) {
            chosen.delete();
        } else {
            List<String> info = lines(new File(dir, "driver.txt"));
            if (info.size() < 2) return;
            try (FileWriter w = new FileWriter(chosen)) {
                w.write(dir.getName() + "\n" + info.get(1) + "\n");
            } catch (IOException e) {
                say(tr("No se pudo guardar: ", "Could not save it: ") + e.getMessage(), true);
                return;
            }
        }
        build();
    }

    private void remove(File dir) {
        if (dir.getName().equals(selected()[0])) choose(null);
        deleteTree(dir);
        build();
    }

    // ---- agregar --------------------------------------------------------------------------------------------------------

    private void pick() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed",
                "application/octet-stream"});
        try {
            startActivityForResult(i, PICK);
        } catch (android.content.ActivityNotFoundException e) {
            say(tr("No hay un selector de archivos en este teléfono.", "There's no file picker on this phone."), true);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri zip = data.getData();
        say(tr("Agregando…", "Adding…"), false);
        new Thread(() -> {
            String shown, error = null;
            try {
                shown = unpack(zip);
            } catch (IOException e) {
                shown = null;
                error = e.getMessage();
            }
            final String ok = shown, bad = error;
            runOnUiThread(() -> {
                build();
                if (ok != null) say(tr("Agregado: ", "Added: ") + ok + tr(". Tócalo para usarlo.", ". Tap it to use it."), false);
                else say(tr("No se pudo agregar: ", "Couldn't add it: ") + bad, true);
            });
        }, "AgregarDriver").start();
    }

    // El meta.json y las librerías .so del zip (sin sus carpetas) en una carpeta nueva; driver.txt guarda lo que se enseña
    // y el nombre de la librería. Devuelve el nombre que se enseña.
    private String unpack(Uri zip) throws IOException {
        File dir = new File(root(), "nuevo");
        deleteTree(dir);
        if (!dir.mkdirs()) throw new IOException(tr("no se pudo crear la carpeta", "couldn't create the folder"));
        JSONObject meta = null;
        List<String> libraries = new ArrayList<>();
        try (InputStream in = getContentResolver().openInputStream(zip); ZipInputStream z = new ZipInputStream(in)) {
            for (ZipEntry e; (e = z.getNextEntry()) != null; ) {
                if (e.isDirectory()) continue;
                String name = e.getName().substring(e.getName().lastIndexOf('/') + 1);
                if (name.equalsIgnoreCase("meta.json")) {
                    try {
                        meta = new JSONObject(new String(readAll(z), "UTF-8"));
                    } catch (JSONException bad) {
                        throw new IOException(tr("meta.json no se entiende", "meta.json can't be read"));
                    }
                } else if (name.endsWith(".so") && !name.contains("..")) {
                    try (OutputStream out = new FileOutputStream(new File(dir, name))) {
                        copy(z, out);
                    }
                    libraries.add(name);
                }
            }
        } catch (IOException e) {
            deleteTree(dir);
            throw e;
        }
        if (libraries.isEmpty()) {
            deleteTree(dir);
            throw new IOException(tr("el zip no trae ninguna librería .so", "the zip has no .so library"));
        }
        String library = meta != null ? meta.optString("libraryName", "") : "";
        if (library.isEmpty() || !libraries.contains(library)) library = libraries.get(0);
        library = notTheSystems(dir, library);
        String name = meta != null ? meta.optString("name", "").trim() : "";
        String version = meta != null ? meta.optString("driverVersion", "").trim() : "";
        String vendor = meta != null ? meta.optString("vendor", "").trim() : "";
        if (name.isEmpty()) name = library;
        String detail = (vendor + " " + version).trim();

        String folder = name.replaceAll("[^A-Za-z0-9._-]+", "_");
        if (folder.isEmpty() || folder.equals("nuevo")) folder = "driver";
        File target = new File(root(), folder);
        for (int n = 2; target.exists(); n++) target = new File(root(), folder + "_" + n);
        try (FileWriter w = new FileWriter(new File(dir, "driver.txt"))) {
            w.write(name + "\n" + library + "\n" + detail + "\n");
        }
        if (!dir.renameTo(target)) {
            deleteTree(dir);
            throw new IOException(tr("no se pudo guardar", "couldn't save it"));
        }
        return name;
    }

    // El sistema ya tiene abierta su vulkan.adreno.so (y las de /vendor/lib64/hw): una librería con ese mismo nombre no se
    // llegaría a cargar y correría la suya sin avisar. Se le da un nombre propio.
    private static String notTheSystems(File dir, String library) {
        boolean clash = library.equals("vulkan.adreno.so") || library.equals("libvulkan.so")
                || new File("/vendor/lib64/hw/" + library).exists() || new File("/system/lib64/hw/" + library).exists();
        if (!clash) return library;
        String own = "vulkan.propio.so";
        return new File(dir, library).renameTo(new File(dir, own)) ? own : library;
    }

    // [nombre, detalle] de un driver agregado, o null si su carpeta no lo es
    private static String[] describe(File dir) {
        List<String> info = lines(new File(dir, "driver.txt"));
        if (info.size() < 2 || !new File(dir, info.get(1)).isFile()) return null;
        return new String[] {info.get(0), info.size() > 2 ? info.get(2) : ""};
    }

    // ---- ficheros -------------------------------------------------------------------------------------------------------

    private static List<String> lines(File f) {
        try {
            return Files.readAllLines(f.toPath());
        } catch (IOException e) {
            return new ArrayList<>();
        }
    }

    private static String firstLine(File f) {
        List<String> l = lines(f);
        return l.isEmpty() ? "" : l.get(0);
    }

    private static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        copy(in, b);
        return b.toByteArray();
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[1 << 16];
        for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
    }

    private static void deleteTree(File f) {
        File[] subs = f.listFiles();
        if (subs != null) for (File s : subs) deleteTree(s);
        f.delete();
    }

    // ---- piezas (las del menú de ajustes) -------------------------------------------------------------------------------

    private void header(String title) {
        TextView t = text(title, 18, GOLD, true);
        t.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        t.setPadding(dp(4), dp(16), 0, dp(4));
        list.addView(t);
        list.addView(rule(EDGE, 1));
    }

    private void note(String s) {
        TextView t = text(s, 13, DIM, false);
        t.setPadding(dp(4), dp(8), dp(4), dp(2));
        list.addView(t);
    }

    private TextView button(String s, boolean main) {
        TextView b = text(s, 16, main ? 0xFF281010 : CREAM, true);
        b.setGravity(Gravity.CENTER);
        b.setBackground(plate(main ? GOLD : 0xFF3E2616, 1, 4));
        return b;
    }

    private View rule(int color, int heightDp) {
        View v = new View(this);
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
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
    }
}
