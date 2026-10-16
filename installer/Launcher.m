#import <Cocoa/Cocoa.h>
int main(void) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"Install CamelCrusher Native 1.0.4";
        alert.informativeText = @"Install the VST2 and Audio Unit with all 20 factory presets.\n\nThe macOS installer will guide you through installation and ask for an administrator password. Existing system plugins are backed up first.\n\nSave your work and quit your DAW before continuing.";
        [alert addButtonWithTitle:@"Continue to Installer"];
        [alert addButtonWithTitle:@"Cancel"];
        if ([alert runModal] != NSAlertFirstButtonReturn) return 0;
        NSURL *pkg = [[NSBundle mainBundle] URLForResource:@"CamelCrusherNative-1.0.4" withExtension:@"pkg"];
        if (!pkg || ![[NSWorkspace sharedWorkspace] openURL:pkg]) {
            NSAlert *error = [NSAlert new];
            error.messageText = @"The installation package could not be opened.";
            error.informativeText = @"Download the installer again, or open the standalone .pkg from the release.";
            [error runModal];
            return 1;
        }
    }
    return 0;
}
