#import <AppKit/AppKit.h>

void tinyclj_host_viewer_install_macos_menu(void) {
    @autoreleasepool {
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

void tinyclj_host_viewer_set_macos_window_title(const char *title) {
    @autoreleasepool {
        if (!title) {
            return;
        }
        NSString *ns_title = [NSString stringWithUTF8String:title];
        if (!ns_title) {
            return;
        }
        NSWindow *key = [NSApp keyWindow];
        if (key) {
            [key setTitle:ns_title];
            return;
        }
        NSArray<NSWindow *> *windows = [NSApp windows];
        if ([windows count] > 0) {
            [[windows objectAtIndex:0] setTitle:ns_title];
        }
    }
}
