package io.github.iamwilmar.mhriseport;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

// The GPU the game last started on, from <data>/gpu.txt (written by the runtime, nvn/vk.cpp), so the menu shows only
// the options that apply to it. Before the first start it's unknown and everything shows.
final class GpuInfo {
    final boolean known;
    final int vendor;
    final boolean bc;
    final String name;

    private GpuInfo(boolean known, int vendor, boolean bc, String name) {
        this.known = known;
        this.vendor = vendor;
        this.bc = bc;
        this.name = name;
    }

    static GpuInfo read(File folder) {
        int vendor = 0;
        boolean bc = true, found = false;
        String name = "";
        if (folder != null) {
            try {
                for (String line : Files.readAllLines(new File(folder, "gpu.txt").toPath())) {
                    String[] kv = line.trim().split(" ", 2);
                    if (kv.length < 2) continue;
                    switch (kv[0]) {
                        case "vendor": vendor = Integer.decode(kv[1]); found = true; break;
                        case "bc": bc = !kv[1].equals("0"); break;
                        case "name": name = kv[1]; break;
                        default: break;
                    }
                }
            } catch (IOException | RuntimeException ignored) {
                // (not started yet)
            }
        }
        return new GpuInfo(found, vendor, bc, name);
    }

    boolean adreno() { return !known || vendor == 0x5143; }

    boolean mali() { return !known || vendor == 0x13B5; }

    // Can't sample BC textures (Mali, PowerVR…): the texture options for that
    boolean noBc() { return !known || !bc; }
}
