#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#include "game_demo_macos_menu.h"

static NSString *const kMacosViewerWindowAutosaveName = @"macos_viewer.main_window";
static NSString *const kMacosViewerScreenUUIDKey = @"macos_viewer.window.screen_uuid";
static NSString *const kMacosViewerFrameXKey = @"macos_viewer.window.frame_x";
static NSString *const kMacosViewerFrameYKey = @"macos_viewer.window.frame_y";
static id g_macos_viewer_performance_activity = nil;
static bool g_macos_viewer_window_autosave_bound = false;
static bool g_macos_viewer_window_callbacks_registered = false;
static bool g_macos_viewer_screen_restore_applied = false;

static void macos_viewer_set_performance_activity(id token) {
    id old_token = g_macos_viewer_performance_activity;
    if (old_token == token) {
        return;
    }
    [token retain];
    [old_token release];
    g_macos_viewer_performance_activity = token;
}

static NSNumber *macos_viewer_screen_number(NSScreen *screen) {
    if (!screen) {
        return nil;
    }
    NSDictionary<NSDeviceDescriptionKey, id> *desc = [screen deviceDescription];
    id n = [desc objectForKey:@"NSScreenNumber"];
    if ([n isKindOfClass:[NSNumber class]]) {
        return (NSNumber *)n;
    }
    return nil;
}

static NSString *macos_viewer_screen_uuid_string(NSScreen *screen) {
    NSNumber *screen_number = macos_viewer_screen_number(screen);
    if (!screen_number) {
        return nil;
    }
    CGDirectDisplayID display_id = (CGDirectDisplayID)[screen_number unsignedIntValue];
    CFUUIDRef uuid_ref = CGDisplayCreateUUIDFromDisplayID(display_id);
    if (!uuid_ref) {
        return nil;
    }
    CFStringRef uuid_cf = CFUUIDCreateString(kCFAllocatorDefault, uuid_ref);
    CFRelease(uuid_ref);
    if (!uuid_cf) {
        return nil;
    }
    return CFBridgingRelease(uuid_cf);
}

static NSScreen *macos_viewer_find_screen_by_uuid(NSString *target_uuid) {
    if (!target_uuid || [target_uuid length] == 0) {
        return nil;
    }
    NSArray<NSScreen *> *screens = [NSScreen screens];
    for (NSScreen *screen in screens) {
        NSString *uuid = macos_viewer_screen_uuid_string(screen);
        if (uuid && [uuid isEqualToString:target_uuid]) {
            return screen;
        }
    }
    return nil;
}

static void macos_viewer_store_screen_position_for_window(NSWindow *window) {
    if (!window) {
        return;
    }
    NSScreen *screen = [window screen];
    if (!screen) {
        screen = [NSScreen mainScreen];
    }

    NSString *screen_uuid = macos_viewer_screen_uuid_string(screen);
    if (!screen_uuid) {
        return;
    }

    NSRect frame = [window frame];

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:screen_uuid forKey:kMacosViewerScreenUUIDKey];
    [defaults setDouble:frame.origin.x forKey:kMacosViewerFrameXKey];
    [defaults setDouble:frame.origin.y forKey:kMacosViewerFrameYKey];
    (void)[window saveFrameUsingName:kMacosViewerWindowAutosaveName];
    /*
     * Persist eagerly after every move. We keep NSUserDefaults as the storage backend,
     * but use the underlying CFPreferences sync point instead of -synchronize.
     */
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
    /* Width/height intentionally not saved - game demo uses fixed 2x scale. */
}

static void macos_viewer_apply_saved_screen_placement(NSWindow *window) {
    if (!window || g_macos_viewer_screen_restore_applied) {
        return;
    }

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSString *saved_screen_uuid = [defaults stringForKey:kMacosViewerScreenUUIDKey];
    if (!saved_screen_uuid || [saved_screen_uuid length] == 0) {
        g_macos_viewer_screen_restore_applied = true;
        return;
    }
    NSScreen *target_screen = macos_viewer_find_screen_by_uuid(saved_screen_uuid);
    if (!target_screen) {
        target_screen = [NSScreen mainScreen];
        if (!target_screen) {
            NSArray<NSScreen *> *screens = [NSScreen screens];
            if ([screens count] > 0) {
                target_screen = [screens objectAtIndex:0];
            }
        }
        if (!target_screen) {
            g_macos_viewer_screen_restore_applied = true;
            return;
        }
    }

    NSRect frame = [window frame];
    if ([defaults objectForKey:kMacosViewerFrameXKey]) {
        frame.origin.x = [defaults doubleForKey:kMacosViewerFrameXKey];
    }
    if ([defaults objectForKey:kMacosViewerFrameYKey]) {
        frame.origin.y = [defaults doubleForKey:kMacosViewerFrameYKey];
    }
    /* Size is intentionally NOT restored - the game demo uses a fixed 2x scale. */

    [window setFrame:frame display:NO];
    g_macos_viewer_screen_restore_applied = true;
}

static void macos_viewer_bind_window_autosave(NSWindow *window) {
    if (!window || g_macos_viewer_window_autosave_bound) {
        return;
    }
    macos_viewer_apply_saved_screen_placement(window);
    [window setFrameAutosaveName:kMacosViewerWindowAutosaveName];
    g_macos_viewer_window_autosave_bound = true;
}

static NSWindow *macos_viewer_primary_window(void) {
    NSWindow *key = [NSApp keyWindow];
    if (key) {
        return key;
    }
    NSArray<NSWindow *> *windows = [NSApp windows];
    if ([windows count] > 0) {
        return [windows objectAtIndex:0];
    }
    return nil;
}

@interface MacosViewerWindowObserver : NSObject
@end

@implementation MacosViewerWindowObserver
- (void)windowDidBecomeMainOrKey:(NSNotification *)note {
    id obj = [note object];
    if (![obj isKindOfClass:[NSWindow class]]) {
        return;
    }
    NSWindow *window = (NSWindow *)obj;
    macos_viewer_bind_window_autosave(window);
}

- (void)windowDidMoveOrChangeScreen:(NSNotification *)note {
    id obj = [note object];
    if (![obj isKindOfClass:[NSWindow class]]) {
        return;
    }
    // Avoid clobbering persisted frame/screen during startup before restore applies.
    if (!g_macos_viewer_screen_restore_applied) {
        return;
    }
    NSWindow *window = (NSWindow *)obj;
    macos_viewer_store_screen_position_for_window(window);
}
@end

static MacosViewerWindowObserver *g_macos_viewer_window_observer = nil;
static bool g_macos_viewer_foreground_promoted = false;

static void macos_viewer_promote_current_process_to_foreground(void) {
    if (g_macos_viewer_foreground_promoted) {
        return;
    }
    ProcessSerialNumber psn = {0, kCurrentProcess};
    (void)TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    (void)SetFrontProcess(&psn);
    g_macos_viewer_foreground_promoted = true;
}

void macos_viewer_install_menu(void) {
    @autoreleasepool {
        macos_viewer_promote_current_process_to_foreground();
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSMenu *main_menu = [[NSMenu alloc] initWithTitle:@""];
        NSMenuItem *app_menu_item = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
        [main_menu addItem:app_menu_item];
        [app setMainMenu:main_menu];

        NSMenu *app_menu = [[NSMenu alloc] initWithTitle:@"Application"];
        NSString *app_name = [[NSProcessInfo processInfo] processName];
        NSString *quit_title = [NSString stringWithFormat:@"Quit %@", app_name];
        NSMenuItem *quit_item = [[NSMenuItem alloc] initWithTitle:quit_title
                                                           action:@selector(terminate:)
                                                    keyEquivalent:@"q"];
        [quit_item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        [app_menu addItem:quit_item];
        [app_menu_item setSubmenu:app_menu];
    }
}

void macos_viewer_set_window_title(const char *title) {
    @autoreleasepool {
        if (!title) {
            return;
        }
        NSString *ns_title = [NSString stringWithUTF8String:title];
        if (!ns_title) {
            return;
        }
        NSWindow *window = macos_viewer_primary_window();
        if (window) {
            [window setTitle:ns_title];
        }
    }
}

void macos_viewer_restore_window_position(void) {
    @autoreleasepool {
        NSWindow *window = macos_viewer_primary_window();
        macos_viewer_bind_window_autosave(window);
    }
}

void macos_viewer_activate_app_window(void) {
    @autoreleasepool {
        macos_viewer_promote_current_process_to_foreground();
        NSApplication *app = [NSApplication sharedApplication];
        if ([app respondsToSelector:@selector(finishLaunching)]) {
            [app finishLaunching];
        }
        if ([NSRunningApplication respondsToSelector:@selector(currentApplication)]) {
            NSRunningApplication *current = [NSRunningApplication currentApplication];
            if (current) {
                [current activateWithOptions:(NSApplicationActivateIgnoringOtherApps |
                                             NSApplicationActivateAllWindows)];
            }
        }
        [app activateIgnoringOtherApps:YES];
        [app unhide:nil];

        NSWindow *window = macos_viewer_primary_window();
        if (!window) {
            return;
        }
        [window makeKeyAndOrderFront:nil];
        [window orderFrontRegardless];
    }
}

void macos_viewer_register_window_callbacks(void) {
    @autoreleasepool {
        if (g_macos_viewer_window_callbacks_registered) {
            return;
        }
        if (!g_macos_viewer_window_observer) {
            g_macos_viewer_window_observer = [MacosViewerWindowObserver new];
        }
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        [nc addObserver:g_macos_viewer_window_observer
               selector:@selector(windowDidBecomeMainOrKey:)
                   name:NSWindowDidBecomeMainNotification
                 object:nil];
        [nc addObserver:g_macos_viewer_window_observer
               selector:@selector(windowDidBecomeMainOrKey:)
                   name:NSWindowDidBecomeKeyNotification
                 object:nil];
        [nc addObserver:g_macos_viewer_window_observer
               selector:@selector(windowDidMoveOrChangeScreen:)
                   name:NSWindowDidMoveNotification
                 object:nil];
        [nc addObserver:g_macos_viewer_window_observer
               selector:@selector(windowDidMoveOrChangeScreen:)
                   name:NSWindowDidChangeScreenNotification
                 object:nil];
        g_macos_viewer_window_callbacks_registered = true;
    }
}

void macos_viewer_save_window_position(void) {
    @autoreleasepool {
        NSWindow *window = macos_viewer_primary_window();
        if (!window) {
            return;
        }
        macos_viewer_store_screen_position_for_window(window);
    }
}

bool macos_viewer_get_content_size(unsigned *out_w, unsigned *out_h) {
    if (!out_w || !out_h) return false;
    @autoreleasepool {
        NSWindow *window = macos_viewer_primary_window();
        if (!window) return false;
        NSRect frame = [window frame];
        NSRect content = [NSWindow contentRectForFrameRect:frame
                                                 styleMask:[window styleMask]];
        unsigned w = (unsigned)content.size.width;
        unsigned h = (unsigned)content.size.height;
        if (w == 0 || h == 0) return false;
        *out_w = w;
        *out_h = h;
        return true;
    }
}

void macos_viewer_begin_performance_activity(void) {
    @autoreleasepool {
        if (g_macos_viewer_performance_activity != nil) {
            return;
        }
        NSProcessInfo *proc = [NSProcessInfo processInfo];
        if (![proc respondsToSelector:@selector(beginActivityWithOptions:reason:)]) {
            return;
        }
        NSActivityOptions opts = NSActivityUserInitiatedAllowingIdleSystemSleep |
                                 NSActivityAutomaticTerminationDisabled;
        id token =
            [proc beginActivityWithOptions:opts
                                    reason:@"tiny-clj game demo render loop"];
        macos_viewer_set_performance_activity(token);
        [token release];
    }
}

void macos_viewer_end_performance_activity(void) {
    @autoreleasepool {
        if (g_macos_viewer_performance_activity == nil) {
            return;
        }
        id token = g_macos_viewer_performance_activity;
        [token retain];
        macos_viewer_set_performance_activity(nil);
        NSProcessInfo *proc = [NSProcessInfo processInfo];
        if ([proc respondsToSelector:@selector(endActivity:)]) {
            [proc endActivity:token];
        }
        [token release];
    }
}
