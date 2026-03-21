#import <AppKit/AppKit.h>
#include <stdlib.h>
#include <string.h>

#include "game_demo_macos_menu.h"
#include "tiny_fx_macos_app.h"

struct TinyFxMacosWindow {
    id controller;
    NSWindow *window;
    NSView *view;
    uint8_t key_buffer[KB_KEY_LAST + 1];
    uint32_t *present_buffer;
    size_t present_capacity;
    unsigned present_width;
    unsigned present_height;
    bool close_requested;
};

static void tinyfx_macos_window_set_key(TinyFxMacosWindow *window, mfb_key key, bool down) {
    if (!window || key < 0 || key > KB_KEY_LAST) {
        return;
    }
    window->key_buffer[key] = down ? 1u : 0u;
}

mfb_key tinyfx_macos_key_from_virtual_key(unsigned short key_code) {
    switch (key_code) {
        case 0x12: return KB_KEY_1;
        case 0x13: return KB_KEY_2;
        case 0x14: return KB_KEY_3;
        case 0x15: return KB_KEY_4;
        case 0x17: return KB_KEY_5;
        case 0x16: return KB_KEY_6;
        case 0x1A: return KB_KEY_7;
        case 0x1C: return KB_KEY_8;
        case 0x19: return KB_KEY_9;
        case 0x1D: return KB_KEY_0;
        case 0x00: return KB_KEY_A;
        case 0x0B: return KB_KEY_B;
        case 0x08: return KB_KEY_C;
        case 0x02: return KB_KEY_D;
        case 0x0E: return KB_KEY_E;
        case 0x03: return KB_KEY_F;
        case 0x05: return KB_KEY_G;
        case 0x04: return KB_KEY_H;
        case 0x22: return KB_KEY_I;
        case 0x26: return KB_KEY_J;
        case 0x28: return KB_KEY_K;
        case 0x25: return KB_KEY_L;
        case 0x2E: return KB_KEY_M;
        case 0x2D: return KB_KEY_N;
        case 0x1F: return KB_KEY_O;
        case 0x23: return KB_KEY_P;
        case 0x0C: return KB_KEY_Q;
        case 0x0F: return KB_KEY_R;
        case 0x01: return KB_KEY_S;
        case 0x11: return KB_KEY_T;
        case 0x20: return KB_KEY_U;
        case 0x09: return KB_KEY_V;
        case 0x0D: return KB_KEY_W;
        case 0x07: return KB_KEY_X;
        case 0x10: return KB_KEY_Y;
        case 0x06: return KB_KEY_Z;
        case 0x24: return KB_KEY_ENTER;
        case 0x30: return KB_KEY_TAB;
        case 0x31: return KB_KEY_SPACE;
        case 0x33: return KB_KEY_BACKSPACE;
        case 0x35: return KB_KEY_ESCAPE;
        case 0x7B: return KB_KEY_LEFT;
        case 0x7C: return KB_KEY_RIGHT;
        case 0x7D: return KB_KEY_DOWN;
        case 0x7E: return KB_KEY_UP;
        case 0x37: return KB_KEY_LEFT_SUPER;
        case 0x36: return KB_KEY_RIGHT_SUPER;
        case 0x38: return KB_KEY_LEFT_SHIFT;
        case 0x3C: return KB_KEY_RIGHT_SHIFT;
        case 0x3B: return KB_KEY_LEFT_CONTROL;
        case 0x3E: return KB_KEY_RIGHT_CONTROL;
        case 0x3A: return KB_KEY_LEFT_ALT;
        case 0x3D: return KB_KEY_RIGHT_ALT;
        default: return KB_KEY_UNKNOWN;
    }
}

@interface TinyFxView : NSView
@property(nonatomic, assign) TinyFxMacosWindow *hostWindow;
- (void)presentPixels:(const uint32_t *)buffer width:(unsigned)width height:(unsigned)height;
@end

@implementation TinyFxView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)presentPixels:(const uint32_t *)buffer width:(unsigned)width height:(unsigned)height {
    TinyFxMacosWindow *host = self.hostWindow;
    if (!host || !buffer || width == 0u || height == 0u) {
        return;
    }
    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > host->present_capacity) {
        uint32_t *resized = realloc(host->present_buffer, pixel_count * sizeof(uint32_t));
        if (!resized) {
            host->close_requested = true;
            return;
        }
        host->present_buffer = resized;
        host->present_capacity = pixel_count;
    }
    memcpy(host->present_buffer, buffer, pixel_count * sizeof(uint32_t));
    host->present_width = width;
    host->present_height = height;
    [self setNeedsDisplay:YES];
    [self displayIfNeeded];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    TinyFxMacosWindow *host = self.hostWindow;
    if (!host || !host->present_buffer || host->present_width == 0u || host->present_height == 0u) {
        [[NSColor blackColor] setFill];
        NSRectFill(self.bounds);
        return;
    }

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    if (!ctx) {
        return;
    }

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    if (!color_space) {
        return;
    }
    size_t bytes_per_row = (size_t)host->present_width * sizeof(uint32_t);
    size_t buffer_size = (size_t)host->present_height * bytes_per_row;
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(NULL, host->present_buffer, buffer_size, NULL);
    if (!provider) {
        CGColorSpaceRelease(color_space);
        return;
    }
    CGImageRef image = CGImageCreate(host->present_width,
                                     host->present_height,
                                     8u,
                                     32u,
                                     bytes_per_row,
                                     color_space,
                                     kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
                                     provider,
                                     NULL,
                                     false,
                                     kCGRenderingIntentDefault);
    if (!image) {
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(color_space);
        return;
    }

    CGContextSaveGState(ctx);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextTranslateCTM(ctx, 0.0, NSHeight(self.bounds));
    CGContextScaleCTM(ctx, 1.0, -1.0);
    CGContextDrawImage(ctx, NSRectToCGRect(self.bounds), image);
    CGContextRestoreGState(ctx);

    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(color_space);
}

- (void)keyDown:(NSEvent *)event {
    mfb_key key = tinyfx_macos_key_from_virtual_key([event keyCode]);
    tinyfx_macos_window_set_key(self.hostWindow, key, true);
}

- (void)keyUp:(NSEvent *)event {
    mfb_key key = tinyfx_macos_key_from_virtual_key([event keyCode]);
    tinyfx_macos_window_set_key(self.hostWindow, key, false);
}

- (void)flagsChanged:(NSEvent *)event {
    TinyFxMacosWindow *host = self.hostWindow;
    if (!host) {
        return;
    }
    NSEventModifierFlags flags = [event modifierFlags];
    tinyfx_macos_window_set_key(host, KB_KEY_LEFT_SHIFT, (flags & NSEventModifierFlagShift) != 0);
    tinyfx_macos_window_set_key(host, KB_KEY_RIGHT_SHIFT, (flags & NSEventModifierFlagShift) != 0);
    tinyfx_macos_window_set_key(host, KB_KEY_LEFT_CONTROL, (flags & NSEventModifierFlagControl) != 0);
    tinyfx_macos_window_set_key(host, KB_KEY_RIGHT_CONTROL, (flags & NSEventModifierFlagControl) != 0);
    tinyfx_macos_window_set_key(host, KB_KEY_LEFT_ALT, (flags & NSEventModifierFlagOption) != 0);
    tinyfx_macos_window_set_key(host, KB_KEY_RIGHT_ALT, (flags & NSEventModifierFlagOption) != 0);
    tinyfx_macos_window_set_key(host, KB_KEY_LEFT_SUPER, (flags & NSEventModifierFlagCommand) != 0);
    tinyfx_macos_window_set_key(host, KB_KEY_RIGHT_SUPER, (flags & NSEventModifierFlagCommand) != 0);
}

@end

@interface TinyFxApplicationController : NSObject<NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, assign) TinyFxMacosWindow *hostWindow;
@property(nonatomic, retain) NSWindow *window;
@property(nonatomic, retain) TinyFxView *view;
- (instancetype)initWithHostWindow:(TinyFxMacosWindow *)host
                             title:(NSString *)title
                             width:(unsigned)width
                            height:(unsigned)height;
- (BOOL)showWindow;
@end

@implementation TinyFxApplicationController

- (instancetype)initWithHostWindow:(TinyFxMacosWindow *)host
                             title:(NSString *)title
                             width:(unsigned)width
                            height:(unsigned)height {
    self = [super init];
    if (!self) {
        return nil;
    }
    _hostWindow = host;

    NSRect frame = NSMakeRect(0, 0, width, height);
    NSWindowStyleMask style_mask = NSWindowStyleMaskClosable | NSWindowStyleMaskTitled;
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:style_mask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    if (!_window) {
        [self release];
        return nil;
    }
    [_window setReleasedWhenClosed:NO];
    [_window setDelegate:self];
    [_window setTitle:(title ? title : @"tiny-fx")];

    _view = [[TinyFxView alloc] initWithFrame:frame];
    if (!_view) {
        [self release];
        return nil;
    }
    _view.hostWindow = host;
    [_window setContentView:_view];
    [_window center];
    return self;
}

- (void)dealloc {
    [_view release];
    [_window release];
    [super dealloc];
}

- (BOOL)showWindow {
    NSApplication *app = [NSApplication sharedApplication];
    if (!app) {
        return NO;
    }
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    if ([app delegate] != self) {
        [app setDelegate:self];
    }
    macos_viewer_install_menu();
    macos_viewer_register_window_callbacks();
    macos_viewer_restore_window_position();
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.view];
    macos_viewer_activate_app_window();
    return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    if (self.hostWindow) {
        self.hostWindow->close_requested = true;
    }
}

@end

static void tinyfx_macos_window_pump_once(TinyFxMacosWindow *window) {
    if (!window) {
        return;
    }
    @autoreleasepool {
        NSEvent *event = nil;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
    }
}

TinyFxMacosWindow *tinyfx_macos_window_open(const char *title, unsigned width, unsigned height) {
    @autoreleasepool {
        TinyFxMacosWindow *window = calloc(1u, sizeof(*window));
        if (!window) {
            return NULL;
        }
        NSString *ns_title = title ? [NSString stringWithUTF8String:title] : @"tiny-fx";
        TinyFxApplicationController *controller =
            [[TinyFxApplicationController alloc] initWithHostWindow:window
                                                              title:ns_title
                                                              width:width
                                                             height:height];
        if (!controller) {
            free(window);
            return NULL;
        }
        window->controller = controller;
        window->window = controller.window;
        window->view = controller.view;
        if (![controller showWindow]) {
            [controller release];
            free(window);
            return NULL;
        }
        tinyfx_macos_window_pump_once(window);
        return window;
    }
}

void tinyfx_macos_window_close(TinyFxMacosWindow *window) {
    if (!window) {
        return;
    }
    @autoreleasepool {
        window->close_requested = true;
        if (window->window) {
            [window->window orderOut:nil];
            [window->window close];
        }
        [window->controller release];
        free(window->present_buffer);
        free(window);
    }
}

mfb_update_state tinyfx_macos_window_update(TinyFxMacosWindow *window,
                                            const uint32_t *buffer,
                                            unsigned width,
                                            unsigned height) {
    if (!window) {
        return STATE_INVALID_WINDOW;
    }
    if (window->close_requested) {
        return STATE_EXIT;
    }
    if (!buffer || width == 0u || height == 0u) {
        return STATE_INVALID_BUFFER;
    }
    @autoreleasepool {
        [(TinyFxView *)window->view presentPixels:buffer width:width height:height];
        tinyfx_macos_window_pump_once(window);
    }
    return window->close_requested ? STATE_EXIT : STATE_OK;
}

bool tinyfx_macos_window_pump_events(TinyFxMacosWindow *window) {
    if (!window) {
        return false;
    }
    tinyfx_macos_window_pump_once(window);
    return !window->close_requested;
}

bool tinyfx_macos_window_wait_sync(TinyFxMacosWindow *window) {
    return tinyfx_macos_window_pump_events(window);
}

const uint8_t *tinyfx_macos_window_get_key_buffer(TinyFxMacosWindow *window) {
    if (!window) {
        return NULL;
    }
    return window->key_buffer;
}

void tinyfx_macos_window_show_cursor(TinyFxMacosWindow *window, bool show) {
    (void)window;
    @autoreleasepool {
        if (show) {
            [NSCursor unhide];
        } else {
            [NSCursor hide];
        }
    }
}

bool tinyfx_macos_window_set_viewport(TinyFxMacosWindow *window,
                                      unsigned offset_x,
                                      unsigned offset_y,
                                      unsigned width,
                                      unsigned height) {
    (void)window;
    return offset_x == 0u && offset_y == 0u && width > 0u && height > 0u;
}
