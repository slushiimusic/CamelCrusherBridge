#!/usr/bin/env python3
"""Package prebuilt, ad-hoc-signed plugins into a macOS installer app and PKG."""
from pathlib import Path
import os, plistlib, shutil, subprocess
ROOT = Path(__file__).resolve().parent
VERSION = '1.0.4'
WORK = ROOT / 'build' / 'installer'
DIST = ROOT / 'dist'

def run(*args):
    subprocess.run([str(a) for a in args], check=True)

def main():
    # Only this dedicated, generated directory is replaced.
    if WORK.exists(): shutil.rmtree(WORK)
    WORK.mkdir(parents=True)
    DIST.mkdir(exist_ok=True)
    payload = WORK / 'root'
    for folder, name in [('VST', 'CamelCrusher.vst'), ('Components', 'CamelCrusher.component')]:
        src = ROOT / 'build' / 'Plugins' / name
        if not (src / 'Contents/Resources/CamelCrusherPresetsA.fxb').is_file():
            raise SystemExit('Build both plugins with CC_BANK before packaging.')
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', src)
        dst = payload / 'Library/Audio/Plug-Ins' / folder / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        run('/usr/bin/ditto', src, dst)
    components = WORK / 'components.plist'
    run('/usr/bin/pkgbuild', '--analyze', '--root', payload, components)
    items = plistlib.loads(components.read_bytes())
    for item in items:
        item['BundleIsRelocatable'] = False
        item['BundleHasStrictIdentifier'] = True
        item['BundleOverwriteAction'] = 'upgrade'
    components.write_bytes(plistlib.dumps(items))
    scripts = WORK / 'scripts'
    shutil.copytree(ROOT / 'installer/scripts', scripts)
    for script in scripts.iterdir():
        script.chmod(0o755)
    run('/usr/bin/pkgbuild', '--root', payload, '--component-plist', components,
        '--identifier', 'com.slushiimusic.camelcrusherbridge', '--version', VERSION,
        '--install-location', '/', '--ownership', 'recommended',
        '--scripts', scripts, WORK / 'plugins.pkg')
    resources = WORK / 'resources'
    resources.mkdir()
    (resources / 'welcome.html').write_text('''<html><body style="font:14px -apple-system;padding:12px">
<h1>CamelCrusher Native 1.0.4</h1><p>VST2 and Audio Unit for Apple Silicon, with all 20 factory presets.</p>
<p>Requires macOS 11 or later and Rosetta 2. Save your work and quit your DAW before installing.</p>
<p>Installs both formats into /Library/Audio/Plug-Ins. Existing system bundles are backed up in
/Library/Application Support/CamelCrusherBridge/Backups.</p>
<p>Restart your DAW and rescan plugins after installation.</p></body></html>''')
    (resources / 'conclusion.html').write_text('''<html><body style="font:14px -apple-system;padding:12px">
<h1>Ready to make music.</h1><p>Restart your DAW and rescan plugins. Select CamelCrusher from Camel Audio,
then use the preset menu or arrows to browse the 20 factory presets.</p>
<p>VST2: /Library/Audio/Plug-Ins/VST/CamelCrusher.vst<br>
Audio Unit: /Library/Audio/Plug-Ins/Components/CamelCrusher.component</p></body></html>''')
    distribution = WORK / 'Distribution.xml'
    distribution.write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
<title>CamelCrusher Native {VERSION}</title>
<welcome file="welcome.html" mime-type="text/html"/>
<conclusion file="conclusion.html" mime-type="text/html"/>
<options customize="never" require-scripts="true" hostArchitectures="arm64"/>
<domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
<allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
<choices-outline><line choice="plugins"/></choices-outline>
<choice id="plugins" title="VST2, Audio Unit and factory presets"><pkg-ref id="com.slushiimusic.camelcrusherbridge"/></choice>
<pkg-ref id="com.slushiimusic.camelcrusherbridge" version="{VERSION}" onConclusion="none">plugins.pkg</pkg-ref>
</installer-gui-script>''')
    pkg = DIST / f'CamelCrusherNative-{VERSION}.pkg'
    run('/usr/bin/productbuild', '--distribution', distribution, '--resources', resources,
        '--package-path', WORK, pkg)
    app = WORK / 'Install CamelCrusher Native.app'
    contents = app / 'Contents'
    (contents / 'MacOS').mkdir(parents=True)
    (contents / 'Resources').mkdir()
    plist = {'CFBundleExecutable':'Installer', 'CFBundleName':'Install CamelCrusher Native',
        'CFBundleIdentifier':'com.slushiimusic.camelcrusherbridge.installer',
        'CFBundlePackageType':'APPL', 'CFBundleShortVersionString':VERSION,
        'CFBundleVersion':VERSION, 'LSMinimumSystemVersion':'11.0', 'NSHighResolutionCapable':True}
    (contents / 'Info.plist').write_bytes(plistlib.dumps(plist))
    shutil.copy2(pkg, contents / 'Resources' / pkg.name)
    run('/usr/bin/clang', '-arch', 'arm64', '-mmacosx-version-min=11.0', '-O2', '-fobjc-arc',
        '-framework', 'Cocoa', ROOT / 'installer/Launcher.m', '-o', contents / 'MacOS/Installer')
    run('/usr/bin/codesign', '--force', '--sign', '-', app)
    run('/usr/bin/codesign', '--verify', '--deep', '--strict', app)
    archive = DIST / f'CamelCrusherNative-{VERSION}-Installer.zip'
    if archive.exists(): archive.unlink()
    run('/usr/bin/ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, archive)
    print(f'Created {pkg}\nCreated {archive}')

if __name__ == '__main__': main()
