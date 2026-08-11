// Native arm64 Cocoa GUI for the CamelCrusher bridge.
// Renders the original Camel Audio skin bitmaps using the coordinates from
// the shipped SkinParameters.txt, so the UI matches the 2011 plugin.
#import <Cocoa/Cocoa.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include "bridge.h"

#define SKIN_DIR "/Library/Application Support/Camel Audio/CamelCrusherData/Skins/default"

enum { T_KNOB, T_ONBTN, T_PHAT, T_KICK, T_PRESET };

typedef struct { int type; int param; int x, y, w, h; } Ctl;

/* Coordinates verbatim from SkinParameters.txt (top-left origin). */
static const Ctl kCtls[] = {
    { T_ONBTN, 0,   39, 175, 32, 24 },   /* DistOn         */
    { T_KNOB,  2,   63, 214, 48, 48 },   /* DistTube       */
    { T_KNOB,  1,  123, 214, 48, 48 },   /* DistMech       */
    { T_ONBTN, 3,  199, 175, 32, 24 },   /* MmFilterOn     */
    { T_KNOB,  4,  222, 214, 48, 48 },   /* MmFilterCutoff */
    { T_KNOB,  5,  282, 214, 48, 48 },   /* MmFilterRes    */
    { T_ONBTN, 6,   39, 280, 32, 24 },   /* CompressOn     */
    { T_KNOB,  7,   63, 319, 48, 48 },   /* CompressAmount */
    { T_PHAT,  8,  123, 319, 22, 20 },   /* CompressMode   */
    { T_ONBTN, 9,  199, 280, 32, 24 },   /* MasterOn       */
    { T_KNOB, 11,  222, 319, 48, 48 },   /* MasterVolume   */
    { T_KNOB, 10,  282, 319, 48, 48 },   /* MasterMix      */
    { T_KICK, -1,  275, 127, 93, 28 },   /* Randomize      */
    { T_PRESET,-1,  23, 113, 193, 27 },  /* PresetSelector */
};
static const int kNumCtls = (int)(sizeof(kCtls) / sizeof(kCtls[0]));

/* Entries with only x,y in SkinParameters.txt give the control's CENTRE;
   PresetSelector is the one entry specifying a full x1,y1,x2,y2 rect. */
static NSRect ctlRect(const Ctl* c) {
    if (c->type == T_PRESET) return NSMakeRect(c->x, c->y, c->w, c->h);
    return NSMakeRect(c->x - c->w / 2.0, c->y - c->h / 2.0, c->w, c->h);
}

/* ---- bitmap font (FontDisplay.png + widths from SkinParameters.txt) ---- */
static const char* kFontChars =
  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz%'()*+,-./:?";
static const int kFontW[] = {
  6,5,6,6,6,6,6,6,6,6, 6,6,6,6,6,6,6,6,5,6,6,5,7,7,6,5,6,6,6,5,6,7,7,7,5,5,
  6,6,6,6,6,4,6,6,3,4,6,3,7,6,6,6,6,4,6,4,6,7,7,7,6,6, 7,3,4,4,7,5,3,5,3,5,3,5 };

@interface CCView : NSView {
@public
    Bridge*   bridge;
    NSImage*  imgBg;
    NSImage*  imgKnob;
    NSImage*  imgOn;
    NSImage*  imgPhat;
    NSImage*  imgRand;
    NSImage*  imgSel;
    NSImage*  imgFont;
    int       knobFrames;
    int       dragIdx;
    float     dragStartVal;
    CGFloat   dragStartY;
    int       curProgram;
    char      progName[64];
    float     cache[BRIDGE_MAX_PARAM];
    NSTimer*  timer;
}
@end

/* Prefer skin art bundled inside the plugin; fall back to the system install. */
static const char* skinBase(void) {
    static char base[1024];
    static int resolved = 0;
    if (resolved) return base;
    resolved = 1;
    Dl_info info;
    if (dladdr((void*)skinBase, &info) && info.dli_fname) {
        char dir[900];
        snprintf(dir, sizeof(dir), "%s", info.dli_fname);
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            snprintf(base, sizeof(base), "%s/../Resources/Skins/default", dir);
            char probe[1100];
            snprintf(probe, sizeof(probe), "%s/Background.png", base);
            if (access(probe, R_OK) == 0) return base;
        }
    }
    snprintf(base, sizeof(base), "%s", SKIN_DIR);
    return base;
}

static NSImage* loadSkin(const char* name) {
    char p[1200];
    snprintf(p, sizeof(p), "%s/%s.png", skinBase(), name);
    NSString* s = [NSString stringWithUTF8String:p];
    return [[NSImage alloc] initWithContentsOfFile:s];
}

@implementation CCView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { (void)e; return YES; }

/* Draw one frame of a vertical filmstrip. NSImage source rects use a
   bottom-left origin, so convert the top-down frame index. */
- (void)drawStrip:(NSImage*)img at:(NSRect)dst frame:(int)f of:(int)total {
    if (!img) return;
    NSSize sz = [img size];
    CGFloat fh = sz.height / (CGFloat)total;
    if (f < 0) f = 0;
    if (f >= total) f = total - 1;
    NSRect src = NSMakeRect(0, sz.height - (CGFloat)(f + 1) * fh, sz.width, fh);
    [img drawInRect:dst fromRect:src operation:NSCompositingOperationSourceOver
           fraction:1.0 respectFlipped:YES hints:nil];
}

- (void)drawText:(const char*)txt at:(NSPoint)p {
    if (!imgFont || !txt) return;
    NSSize sz = [imgFont size];
    CGFloat gh = sz.height;
    int nchars = (int)strlen(kFontChars);
    CGFloat pen = p.x;
    for (const char* c = txt; *c; c++) {
        const char* hit = strchr(kFontChars, *c);
        if (!hit) { pen += 5; continue; }          /* space / unknown */
        int gi = (int)(hit - kFontChars);
        if (gi >= nchars) continue;
        CGFloat off = 0;
        for (int i = 0; i < gi; i++) off += kFontW[i];
        CGFloat w = kFontW[gi];
        NSRect src = NSMakeRect(off, 0, w, gh);
        NSRect dst = NSMakeRect(pen, p.y, w, gh);
        [imgFont drawInRect:dst fromRect:src operation:NSCompositingOperationSourceOver
                   fraction:1.0 respectFlipped:YES hints:nil];
        pen += w;
    }
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSColor blackColor] setFill];
    NSRectFill([self bounds]);
    if (imgBg)
        [imgBg drawInRect:NSMakeRect(0, 0, CC_UI_W, CC_UI_H)
                 fromRect:NSZeroRect operation:NSCompositingOperationCopy
                 fraction:1.0 respectFlipped:YES hints:nil];

    for (int i = 0; i < kNumCtls; i++) {
        const Ctl* c = &kCtls[i];
        NSRect r = ctlRect(c);
        float v = (c->param >= 0) ? bridge_get_param(bridge, c->param) : 0.f;
        switch (c->type) {
        case T_KNOB:
            [self drawStrip:imgKnob at:r frame:(int)lroundf(v * (knobFrames - 1))
                         of:knobFrames];
            break;
        case T_ONBTN:
            [self drawStrip:imgOn at:r frame:(v >= 0.5f ? 1 : 0) of:2];
            break;
        case T_PHAT:
            [self drawStrip:imgPhat at:r frame:(v >= 0.5f ? 1 : 0) of:2];
            break;
        case T_KICK:
            [self drawStrip:imgRand at:r frame:0 of:2];
            break;
        case T_PRESET: {
            if (imgSel) {
                NSSize s = [imgSel size];
                NSRect src = NSMakeRect(0, s.height - s.height / 3.0, s.width, s.height / 3.0);
                [imgSel drawInRect:r fromRect:src
                         operation:NSCompositingOperationSourceOver
                          fraction:1.0 respectFlipped:YES hints:nil];
            }
            [self drawText:progName at:NSMakePoint(c->x + 26, c->y + 8)];
            break;
        }
        }
    }
}

- (int)hitTestCtl:(NSPoint)p {
    for (int i = 0; i < kNumCtls; i++) {
        if (NSPointInRect(p, ctlRect(&kCtls[i]))) return i;
    }
    return -1;
}

- (void)pushParam:(int)idx value:(float)v {
    bridge_set_param(bridge, idx, v);
    if (bridge->master) {
        bridge->master(&bridge->eff, audioMasterAutomate, idx, 0, NULL, v);
    }
    cache[idx] = v;
    [self setNeedsDisplay:YES];
}

- (void)loadProgram:(int)n {
    if (n < 0) n = bridge->eff.numPrograms - 1;
    if (n >= bridge->eff.numPrograms) n = 0;
    curProgram = n;
    bridge->eff.dispatcher(&bridge->eff, effSetProgram, 0, n, NULL, 0.f);
    [self syncProgramName];
    [self setNeedsDisplay:YES];
}

- (void)syncProgramName {
    char buf[64];
    memset(buf, 0, sizeof(buf));
    if (bridge->eff.dispatcher(&bridge->eff, effGetProgramNameIndexed,
                               curProgram, 0, buf, 0.f)) {
        strncpy(progName, buf, sizeof(progName) - 1);
        progName[sizeof(progName) - 1] = 0;
    }
}

- (void)mouseDown:(NSEvent*)ev {
    NSPoint p = [self convertPoint:[ev locationInWindow] fromView:nil];
    int i = [self hitTestCtl:p];
    dragIdx = -1;
    if (i < 0) return;
    const Ctl* c = &kCtls[i];

    switch (c->type) {
    case T_KNOB:
        dragIdx = i;
        dragStartVal = bridge_get_param(bridge, c->param);
        dragStartY = p.y;
        if (bridge->master)
            bridge->master(&bridge->eff, audioMasterBeginEdit, c->param, 0, NULL, 0.f);
        break;
    case T_ONBTN:
    case T_PHAT: {
        float v = bridge_get_param(bridge, c->param) >= 0.5f ? 0.f : 1.f;
        if (bridge->master)
            bridge->master(&bridge->eff, audioMasterBeginEdit, c->param, 0, NULL, 0.f);
        [self pushParam:c->param value:v];
        if (bridge->master)
            bridge->master(&bridge->eff, audioMasterEndEdit, c->param, 0, NULL, 0.f);
        break;
    }
    case T_KICK: {
        static const int rnd[] = { 1, 2, 4, 5, 7, 8, 10, 11 };
        for (unsigned k = 0; k < sizeof(rnd) / sizeof(rnd[0]); k++) {
            float v = (float)arc4random_uniform(10001) / 10000.0f;
            if (rnd[k] == 8) v = (v >= 0.5f) ? 1.f : 0.f;
            [self pushParam:rnd[k] value:v];
        }
        break;
    }
    case T_PRESET: {
        NSRect r = ctlRect(c);
        if (p.x < NSMinX(r) + 22)      [self loadProgram:curProgram - 1];
        else if (p.x > NSMaxX(r) - 22) [self loadProgram:curProgram + 1];
        break;
    }
    }
}

- (void)mouseDragged:(NSEvent*)ev {
    if (dragIdx < 0) return;
    const Ctl* c = &kCtls[dragIdx];
    NSPoint p = [self convertPoint:[ev locationInWindow] fromView:nil];
    CGFloat range = ([ev modifierFlags] & NSEventModifierFlagShift) ? 800.0 : 180.0;
    float v = dragStartVal + (float)((dragStartY - p.y) / range);
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    [self pushParam:c->param value:v];
}

- (void)mouseUp:(NSEvent*)ev {
    (void)ev;
    if (dragIdx >= 0 && bridge->master)
        bridge->master(&bridge->eff, audioMasterEndEdit, kCtls[dragIdx].param, 0, NULL, 0.f);
    dragIdx = -1;
}

/* Poll for host-side automation so the UI tracks the DAW. */
- (void)tick:(NSTimer*)t {
    (void)t;
    int dirty = 0;
    if (bridge->curProgram != curProgram) {   /* host switched preset */
        curProgram = bridge->curProgram;
        [self syncProgramName];
        dirty = 1;
    }
    for (int i = 0; i < 12; i++) {
        float v = bridge_get_param(bridge, i);
        if (fabsf(v - cache[i]) > 1e-6f) { cache[i] = v; dirty = 1; }
    }
    if (dirty) [self setNeedsDisplay:YES];
}
@end

/* Builds a fully wired editor view. Shared by the VST2 (parented into the
   host window) and the AU (returned standalone to the host's view factory). */
static CCView* cc_make_view(Bridge* b) {
    CCView* v = [[CCView alloc] initWithFrame:NSMakeRect(0, 0, CC_UI_W, CC_UI_H)];
    v->bridge  = b;
    v->imgBg   = loadSkin("Background");
    v->imgKnob = loadSkin("Knob");
    v->imgOn   = loadSkin("OnButton");
    v->imgPhat = loadSkin("PhatButton");
    v->imgRand = loadSkin("ButtonRandom");
    v->imgSel  = loadSkin("SelectorPreset");
    v->imgFont = loadSkin("FontDisplay");
    v->dragIdx = -1;
    v->curProgram = b->curProgram;
    v->knobFrames = 55;
    if (v->imgKnob) {
        NSSize s = [v->imgKnob size];
        if (s.width > 0) {
            int n = (int)lround(s.height / s.width);   /* square frames */
            if (n > 1) v->knobFrames = n;
        }
    }
    for (int i = 0; i < BRIDGE_MAX_PARAM; i++) v->cache[i] = bridge_get_param(b, i);
    [v syncProgramName];
    v->timer = [NSTimer scheduledTimerWithTimeInterval:0.05
                target:v selector:@selector(tick:) userInfo:nil repeats:YES];
    return v;
}

void* cc_editor_create(Bridge* b, void* parent) {
    if (!parent) return NULL;
    @autoreleasepool {
        CCView* v = cc_make_view(b);
        [(__bridge NSView*)parent addSubview:v];
        return (void*)CFBridgingRetain(v);
    }
}

/* AU hosts take ownership of a detached view. */
void* cc_editor_create_standalone(Bridge* b) {
    @autoreleasepool { return (void*)CFBridgingRetain(cc_make_view(b)); }
}

void cc_editor_destroy(void* editor) {
    if (!editor) return;
    @autoreleasepool {
        CCView* v = (CCView*)CFBridgingRelease(editor);
        [v->timer invalidate];
        v->timer = nil;
        [v removeFromSuperview];
    }
}

void cc_editor_refresh(void* editor) {
    if (!editor) return;
    /* Called from arbitrary threads; the timer does the actual redraw. */
    (void)editor;
}
