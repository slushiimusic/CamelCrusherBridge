// Cocoa view factory for the Audio Unit. Named by kAudioUnitProperty_CocoaUI.
#import <Cocoa/Cocoa.h>
#import <AudioUnit/AudioUnit.h>
#import <AudioUnit/AUCocoaUIView.h>
#include "bridge.h"

#define kCCProperty_Bridge 0x43436272 /* 'CCbr' */

@interface CCAUViewFactory : NSObject <AUCocoaUIBase>
@end

@implementation CCAUViewFactory

- (unsigned)interfaceVersion { return 0; }

- (NSString*)description { return @"CamelCrusher"; }

- (NSView*)uiViewForAudioUnit:(AudioUnit)au withSize:(NSSize)size {
    (void)size;
    Bridge* b = NULL;
    UInt32 sz = sizeof(b);
    if (AudioUnitGetProperty(au, kCCProperty_Bridge, kAudioUnitScope_Global,
                             0, &b, &sz) != noErr || !b)
        return nil;

    NSView* v = (__bridge_transfer NSView*)cc_editor_create_standalone(b);
    return v;   /* AU hosts take ownership of the returned view */
}

@end
