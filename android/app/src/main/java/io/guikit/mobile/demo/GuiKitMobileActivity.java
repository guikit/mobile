package io.guikit.mobile.demo;

import org.libsdl.app.SDLActivity;

/**
 * Main Android Activity for GuiKit Mobile Demo.
 * Subclasses SDL3's SDLActivity and loads the single self-contained native library (libmain.so)
 * which statically links SDL3, ThorVG, guikit::core, guikit::scenes, and guikit::mobile.
 */
public class GuiKitMobileActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "main"
        };
    }
}
