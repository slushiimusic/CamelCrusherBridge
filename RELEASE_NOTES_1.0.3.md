# CamelCrusher Native 1.0.3 — Presets and graphical installer

Both the VST2 and Audio Unit now start with all 20 factory presets from
CamelCrusherPresetsA.fxb instead of twenty Init slots. The preset names,
parameter values, and selected program name are restored. Saved session
state is applied after initialization.

Download **CamelCrusherNative-1.0.3-Installer.zip**, extract it, and open
**Install CamelCrusher Native.app**. It opens the macOS Installer wizard,
which installs both formats together. The standalone **.pkg** is also provided.
Save your work and quit your DAW first, then restart and rescan after installing.

- Apple Silicon, macOS 11+, Rosetta 2, and an administrator account required.
- Installs into the standard system VST and Components folders.
- Existing system bundles are backed up under
  `/Library/Application Support/CamelCrusherBridge/Backups`.
- App and plugins are ad-hoc signed; the PKG is unsigned. This release is not
  notarized, so macOS may require Open Anyway in Privacy & Security.
- Separate Tianzii FL Studio Patcher layouts are not redistributed.

Validation on the development Mac: all 20 presets produced identical output
through the original Intel VST, new native VST2, and new AU: 204,800 finite
samples per preset, 4,096,000 per format. All preset names and 17 parameters
matched the original, bank-state restoration passed, and the installed AU
passed auval. The package manifest, choices and embedded signatures were
checked. A full Installer-wizard installation on a clean Mac has not been tested.
