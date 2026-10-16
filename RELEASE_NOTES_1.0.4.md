# CamelCrusher Native 1.0.4 — AU editor crash fix

Fixes the crash when opening the Audio Unit editor in Ableton Live. The editor
was calling a VST dispatcher that does not exist in the AU. Both formats now
use the shared bridge APIs to select and name presets. AU editor timers and
references are detached on replacement and disposal, and the AU view class has
a distinct name from the VST view class.

Download CamelCrusherNative-1.0.4-Installer.zip or the standalone .pkg. Save your
work and quit the DAW before installing. Restart it and rescan after updating;
an already-loaded 1.0.3 binary stays loaded until the host exits.

Includes all 20 presets and the graphical installer introduced in 1.0.3.
Requires Apple Silicon, macOS 11+, Rosetta 2 and administrator access. The app
and plugins are ad-hoc signed; the PKG is unsigned and the release is not
notarized. Existing system bundles are backed up by the package installer.

Validation: reproduced 1.0.3's SIGSEGV in an isolated Cocoa AU editor host;
1.0.4 passed 25 cycles of editor creation, all 20 preset selections,
wraparound, redraw, replacement and AU disposal with the VST co-loaded.
The 20-preset audio comparisons passed for the staged VST and AU.
The package was installed successfully. The installed AU passed auval,
25 editor cycles with the VST co-loaded, and bit-identical audio checks on
presets 0, 7 and 19 (204,800 samples each).
The fix has not yet been manually exercised inside an Ableton session.
