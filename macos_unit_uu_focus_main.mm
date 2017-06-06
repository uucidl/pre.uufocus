#define BUILD(__os,...)
#define DOC(...)
#define TAG(...)
BUILD(osx,"clang++ -std=c++11 cocoa.mm -o cocoa -framework AppKit")

#include "uu_focus_effects.hpp"
#include "uu_focus_main.hpp"

#include "macos_coreaudio.hpp"

#import "AppKit/AppKit.h"
#import "Foundation/NSTimer.h"
#import "Foundation/NSRunLoop.h"

#include <mach/mach_time.h>

#include <cstdint>

@interface UUContentView : NSView {}
@end

@interface UUTimerProxy : NSObject
- (void)onTimer;
@end

struct Platform
{
  NSView* content_view;
};

static UUFocusMainCoroutine global_uu_focus_main;
static mach_timebase_info_data_t global_mach_timebase_info;
static uint64_t global_mach_absolute_time_origin;
static CoreaudioStream coreaudio_stream;

static uint64_t now_micros();
static COREAUDIO_STREAM_RENDER(audio_render);

int main(int argc, char **argv)
{
  if (mach_timebase_info(&global_mach_timebase_info)) {
    return 0x238622c6; // "could not obtain timebase"
  }
  global_mach_absolute_time_origin = mach_absolute_time();

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
  Platform platform = {};
  DOC("create main window") {
    auto window_style_mask = NSTitledWindowMask|
      NSClosableWindowMask|
      NSMiniaturizableWindowMask|
      NSTexturedBackgroundWindowMask|
      NSResizableWindowMask;
    auto contentView = [[[UUContentView alloc]
                          initWithFrame: NSMakeRect(0, 0, 640, 480)] autorelease];
    auto window = [[[NSWindow alloc]
                    initWithContentRect: [contentView frame]
                              styleMask: window_style_mask
                                backing: NSBackingStoreBuffered
                                  defer: NO] autorelease];
    [window cascadeTopLeftFromPoint: NSMakePoint(20, 20)];
    [window setTitle: app_name];
    [window setContentView: contentView];
    // NOTE(nicolas): a key window is the one that receives input
    // events; it also becomes our main window:
    [window makeKeyAndOrderFront: nil];
    platform.content_view = contentView;
  }

  global_uu_focus_main.timer_effect = timer_make(&platform);

  auto timerCallback = [[UUTimerProxy alloc] autorelease];
  [NSApp activateIgnoringOtherApps: YES];

  DOC("schedule time evaluation") {
    auto nstimer = [[NSTimer
                     timerWithTimeInterval: 0.200
                                    target:timerCallback
                                  selector:@selector(onTimer)
                                  userInfo: nil
                                   repeats: YES] autorelease];
    [[NSRunLoop currentRunLoop] addTimer: nstimer forMode: NSRunLoopCommonModes];
  }

  coreaudio_stream.header.input_render = audio_render;
  
  if (macos_coreaudio_open_stereo(&coreaudio_stream, 48000)) {
    return 0x476142b8; // "could not open audio stream"
  }
  [NSApp run];
  macos_coreaudio_close(&coreaudio_stream);
  return 0;
}

static uint64_t now_micros()
{
  auto const timebase = global_mach_timebase_info;
  return (mach_absolute_time() - global_mach_absolute_time_origin) *
    timebase.numer / timebase.denom / 1000;
}

#include "uu_focus_effects_types.hpp"

@implementation UUContentView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
  auto const pTextAttributes =
    [NSDictionary
         dictionaryWithObjectsAndKeys:
             [NSFont systemFontOfSize: 34],
      NSFontAttributeName,
      [NSColor whiteColor],
      NSForegroundColorAttributeName,
      nil];
  if (timer_is_active(global_uu_focus_main.timer_effect)) {
    [[NSColor greenColor] set];
    NSRectFill(dirtyRect);
    auto const &timer = *global_uu_focus_main.timer_effect;
    int const timer_countdown_s =
      1+int((timer.end_micros - now_micros())/1'000'000);
    auto minutes = timer_countdown_s/60;
    auto seconds = timer_countdown_s - minutes*60;

    auto string = [[NSString stringWithFormat:@"%02d:%02d",
                             minutes, seconds] autorelease];
    [string drawInRect:[self frame] withAttributes: pTextAttributes];
  } else {
    [[NSColor blackColor] set];
    NSRectFill(dirtyRect);
    auto string = @"Press LMB to start timer.";
    [string drawInRect:[self frame] withAttributes: pTextAttributes];
  }
}
-(void)mouseDown:(NSEvent *)event
{
  global_uu_focus_main.input.command.type = Command_timer_start;
  global_uu_focus_main.input.time_micros = now_micros();
  uu_focus_main(&global_uu_focus_main);

}
@end

@implementation UUTimerProxy
-(void)onTimer
{
  auto &main = global_uu_focus_main;
  auto &input = main.input;
  main.input = {};

  main.input.time_micros = now_micros();

  main.timer_effect->now_micros = main.input.time_micros;
  uu_focus_main(&global_uu_focus_main);
}
@end

#include "uu_focus_platform.hpp"

void platform_render_async(Platform* platform)
{
  auto view = platform->content_view;
  [view setNeedsDisplay: YES];
}

#import "Foundation/NSUserNotification.h"
#include "uu_focus_platform_types.hpp"

void platform_notify(Platform*, UIText content)
{
  UITextValue content_text;
  memcpy(&content_text, &content, sizeof content_text);

  NSUserNotification *notification = [[NSUserNotification alloc] init];
  notification.title = @"UUFocus";

  notification.informativeText =
    [NSString stringWithFormat:@"%*s",
              content_text.utf8_data_size,
              content_text.utf8_data_first];
  notification.soundName = NSUserNotificationDefaultSoundName;
  [[NSUserNotificationCenter defaultUserNotificationCenter]
    deliverNotification:notification];
}

static COREAUDIO_STREAM_RENDER(audio_render)
{
  audio_thread_render(global_uu_focus_main.audio_effect, frames, frames_n);
  return CoreaudioStreamError_Success;
}

#include "uu_focus_effects.cpp"
#include "uu_focus_main.cpp"
#include "uu_focus_platform.cpp"

#include "macos_coreaudio.cpp"

// TODO(uucidl): mark ui strings as such (will allow localization)
// TODO(uucidl): on quit, we must notify the coroutine and also close the audio stream
// TODO(uucidl): timer cancellation
