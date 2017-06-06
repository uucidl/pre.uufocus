#define DOC(...)
#define TAG(...)

#include "uu_focus_main.hpp"
#include "uu_focus_effects.hpp"
#include "uu_focus_effects_types.hpp"
#include "uu_focus_platform.hpp"

#import "AppKit/AppKit.h"
@interface UUContentView : NSView
{
}
@end
@implementation UUContentView
- (void)drawRect:(NSRect)dirtyRect
{
  if (true) {
    [[NSColor blackColor] set];
  } else {
    [[NSColor redColor] set];
  }
  NSRectFill(dirtyRect);
}
- (BOOL)acceptsFirstResponder { return YES; }
-(void)mouseDown:(NSEvent *)event
{
  printf("mouseDown\n");
}
@end

static COREAUDIO_STREAM_RENDER(coreaudio_render);

int main(int argc, char **argv)
{
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
  auto const app_name = [[NSProcessInfo processInfo] processName];
  DOC("create menubar") {
    auto menubar = [[NSMenu new] autorelease];
    auto menubar_app_item = [[NSMenuItem new] autorelease];
    {
      auto app_menu = [[NSMenu new] autorelease];
      auto quit_title TAG(userstring) = [@"Quit " stringByAppendingString:app_name];
      auto quit_item =
        [[[NSMenuItem alloc]
           initWithTitle: quit_title
           action:@selector(terminate:)
           keyEquivalent:@"q"]
          autorelease];
      [app_menu addItem: quit_item];
      [menubar_app_item setSubmenu: app_menu];
    }
    [menubar addItem: menubar_app_item];
    [NSApp setMainMenu: menubar];
  }
  DOC("create main window") {
    auto window_style_mask = NSTitledWindowMask|
      NSClosableWindowMask|
      NSMiniaturizableWindowMask|
      NSTexturedBackgroundWindowMask;
    auto contentView = [[[UUContentView alloc] initWithFrame: NSMakeRect(0, 0, 640, 480)] autorelease];
    auto window = [[[NSWindow alloc]
                    initWithContentRect: NSMakeRect(0, 0, 640, 480)
                              styleMask: window_style_mask
                                backing: NSBackingStoreBuffered
                                  defer: NO] autorelease];
    [window cascadeTopLeftFromPoint: NSMakePoint(20, 20)];
    [window setTitle: app_name];
    [window setContentView: contentView];
    // NOTE(nicolas): a key window is the one that receives input
    // events; it also becomes our main window:
    [window makeKeyAndOrderFront: nil];
  }
  [NSApp activateIgnoringOtherApps: YES];
  [NSApp run];
  return 0;
}

COREAUDIO_STREAM_RENDER(coreaudio_render)
{
  audio_thread_render(global_uu_focus_main.audio_effect, frames, frames_n);
}
