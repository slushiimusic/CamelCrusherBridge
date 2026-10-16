/* Exercise the Cocoa editor path that audio-only auval does not cover. */
#import <Cocoa/Cocoa.h>
#import <AudioUnit/AudioUnit.h>
#import <AudioUnit/AUCocoaUIView.h>
#include <assert.h>
#include <dlfcn.h>
#include "bridge.h"
@interface NSView (EditorTest)
- (void)loadProgram:(int)n;
- (void)tick:(NSTimer*)t;
@end
static intptr_t master(AEffect*f,int32_t o,int32_t i,intptr_t v,void*p,float t) {
    switch(o) {case 1:return 2400;case 23:return 44100;case 24:return 512;default:return 0;}
}
int main(int argc,char**argv) {
    @autoreleasepool {
        assert(argc>=2);
        [NSApplication sharedApplication];
        /* Load VST first as Live did, to catch duplicate Objective-C classes. */
        AEffect* vst=NULL;
        if(argc>2) {
            void*h=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);assert(h);
            VstMainProc entry=(VstMainProc)dlsym(h,"VSTPluginMain");assert(entry);
            vst=entry(master);assert(vst);
            vst->dispatcher(vst,effOpen,0,0,NULL,0);
        }
        CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8*)argv[1],strlen(argv[1]),true);
        CFBundleRef bundle=CFBundleCreate(NULL,url);CFRelease(url);
        assert(bundle && CFBundleLoadExecutable(bundle));
        AudioComponentFactoryFunction factory=(AudioComponentFactoryFunction)
            CFBundleGetFunctionPointerForName(bundle,CFSTR("CamelCrusherAUFactory"));assert(factory);
        AudioComponentDescription desc={'aumf','CgUI','CamA',0,0};
        AudioComponent comp=AudioComponentRegister(&desc,CFSTR("CamelCrusher editor test"),65540,factory);assert(comp);
        for(int cycle=0;cycle<25;cycle++) {
            @autoreleasepool {
                AudioUnit au=NULL;assert(AudioComponentInstanceNew(comp,&au)==noErr);
                UInt32 size=sizeof(AudioUnitCocoaViewInfo);
                AudioUnitCocoaViewInfo info={0};
                assert(AudioUnitGetProperty(au,kAudioUnitProperty_CocoaUI,kAudioUnitScope_Global,0,&info,&size)==noErr);
                Class cls=NSClassFromString((__bridge NSString*)info.mCocoaAUViewClass[0]);assert(cls);
                id<AUCocoaUIBase> viewFactory=[[cls alloc] init];
                NSView* view=[viewFactory uiViewForAudioUnit:au withSize:NSMakeSize(345,373)];assert(view);
                Bridge*b=NULL;size=sizeof(b);
                assert(AudioUnitGetProperty(au,0x43436272,kAudioUnitScope_Global,0,&b,&size)==noErr);
                assert(b && b->ok);
                for(int p=0;p<20;p++) { [view loadProgram:p];assert(b->curProgram==p);[view tick:nil]; }
                [view loadProgram:20];assert(b->curProgram==0);
                [view loadProgram:-1];assert(b->curProgram==19);
                NSBitmapImageRep*bitmap=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
                [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
                if(cycle==0 && argc>3) [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
                    writeToFile:[NSString stringWithUTF8String:argv[3]] atomically:YES];
                NSView*replacement=[viewFactory uiViewForAudioUnit:au withSize:NSMakeSize(345,373)];assert(replacement);
                [view tick:nil];[view loadProgram:1]; /* old host-owned view is detached */
                assert(AudioComponentInstanceDispose(au)==noErr);
                [replacement tick:nil];[replacement loadProgram:2]; /* host releases view after AU */
                [replacement cacheDisplayInRect:replacement.bounds toBitmapImageRep:bitmap];
                CFRelease(info.mCocoaAUViewBundleLocation);CFRelease(info.mCocoaAUViewClass[0]);
            }
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.06]];
        }
        if(vst) vst->dispatcher(vst,effClose,0,0,NULL,0);
        puts("PASS: 25 AU editor cycles, all presets and wraparound, redraw, replacement and disposal; VST co-loaded.");
    }
    return 0;
}
