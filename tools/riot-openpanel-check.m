/* Standalone AppKit regression for issue #85: the panel's own Open/Cancel
 * buttons must end runModal. Drives only its own NSOpenPanel; no Riot/League
 * client is launched or contacted and no external input is generated.
 *
 *   riot-openpanel-check --auto cancel|open-selected|open-none
 *   riot-openpanel-check --interactive
 */
#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static NSOpenPanel *panel;
static NSString *tempDir, *subDir;
static const char *mode;
static BOOL interactive;
static BOOL panelVisibleAtTrigger;
static NSString *triggerNote = @"";
static const double kTriggerDelay = 1.5, kWatchdog = 15.0;

static NSButton *findButton(NSView *view, SEL action) {
    if ([view isKindOfClass:[NSButton class]] && [(NSButton *)view action] == action)
        return (NSButton *)view;
    for (NSView *sub in [view subviews]) {
        NSButton *found = findButton(sub, action);
        if (found) return found;
    }
    return nil;
}

static NSOutlineView *findOutline(NSView *view) {
    if ([view isKindOfClass:[NSOutlineView class]]) return (NSOutlineView *)view;
    for (NSView *sub in [view subviews]) {
        NSOutlineView *found = findOutline(sub);
        if (found) return found;
    }
    return nil;
}

static void press(SEL action, NSString *name) {
    NSButton *button = findButton([panel contentView], action);
    if (button) {
        triggerNote = [NSString stringWithFormat:@"%@ via button", name];
        [button performClick:nil];
    } else {
        triggerNote = [NSString stringWithFormat:@"%@ via direct action (button not found)", name];
        [panel performSelector:action withObject:nil];
    }
}

static void selectSubfolder(void) {
    NSOutlineView *outline = findOutline([panel contentView]);
    if (!outline) { triggerNote = @"no outline view, subfolder row not found"; return; }
    [outline reloadData];
    for (NSInteger row = 0; row < [outline numberOfRows]; row++) {
        id item = [outline itemAtRow:row];
        NSString *path = [item isKindOfClass:[NSURL class]] ? [item path] : [item description];
        if ([path isEqualToString:subDir]) {
            [outline selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
            return;
        }
    }
    triggerNote = @"subfolder row not found";
}

@interface Trigger : NSObject
- (void)fire:(NSTimer *)timer;
@end
@implementation Trigger
- (void)fire:(NSTimer *)timer {
    static BOOL done;
    if (done) return;
    done = YES;
    panelVisibleAtTrigger = [panel isVisible];
    if (!strcmp(mode, "cancel")) {
        press(@selector(_cancel:), @"cancel");
    } else {
        if (!strcmp(mode, "open-selected")) selectSubfolder();
        press(@selector(_selectFile:), @"open");
    }
}
@end

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (argc == 2 && !strcmp(argv[1], "--interactive")) {
        interactive = YES;
        mode = "interactive";
    } else if (argc == 3 && !strcmp(argv[1], "--auto") &&
               (!strcmp(argv[2], "cancel") || !strcmp(argv[2], "open-selected") ||
                !strcmp(argv[2], "open-none"))) {
        mode = argv[2];
    } else {
        fprintf(stderr, "usage: %s --auto cancel|open-selected|open-none | --interactive\n", argv[0]);
        return 2;
    }
    @autoreleasepool {
        [NSApplication sharedApplication];
        char templ[] = "/tmp/openpanel-check.XXXXXX";
        tempDir = [[NSString stringWithUTF8String:mkdtemp(templ)] retain];
        subDir = [[tempDir stringByAppendingPathComponent:@"subfolder"] retain];
        [[NSFileManager defaultManager] createDirectoryAtPath:subDir
            withIntermediateDirectories:YES attributes:nil error:NULL];

        panel = [NSOpenPanel openPanel];
        [panel setCanChooseDirectories:YES];
        [panel setCanChooseFiles:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setDirectory:tempDir];

        if (!interactive) {
            /* Independent of the run loop: a hung runModal still reports. */
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kWatchdog * NSEC_PER_SEC)),
                           dispatch_get_global_queue(0, 0), ^{
                printf("OPENPANEL_CHECK mode=%s result=FAIL problems=HANG "
                       "trigger=\"%s\" visible_at_trigger=%d\n",
                       mode, [triggerNote UTF8String], panelVisibleAtTrigger);
                fflush(stdout);
                _exit(3);
            });
            Trigger *trigger = [[Trigger alloc] init];
            NSTimer *timer = [NSTimer timerWithTimeInterval:kTriggerDelay target:trigger
                selector:@selector(fire:) userInfo:nil repeats:NO];
            [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
            [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
        } else {
            printf("Interactive: pick the folder, or press Open/Cancel on the panel.\n");
        }

        NSInteger code = [panel runModal];
        NSString *path = [[panel URL] path];
        NSArray *urls = [panel URLs];
        BOOL visible = [panel isVisible];
        NSString *shown = path ? path : @"(none)";

        if (interactive) {
            printf("OPENPANEL_CHECK mode=interactive result=RETURNED code=%ld url=%s urls=%lu visible=%d\n",
                   (long)code, [shown UTF8String], (unsigned long)[urls count], visible);
            return 0;
        }

        NSMutableArray *problems = [NSMutableArray array];
        BOOL wantOpen = strcmp(mode, "cancel") != 0;
        NSString *wantPath = !strcmp(mode, "open-selected") ? subDir : tempDir;
        if (!panelVisibleAtTrigger) [problems addObject:@"panel_not_visible_at_trigger"];
        if (wantOpen) {
            if (code != NSOKButton) [problems addObject:@"code_not_ok"];
            if (![path isEqualToString:wantPath]) [problems addObject:@"wrong_url"];
            if ([urls count] != 1) [problems addObject:@"wrong_url_count"];
        } else if (code != NSCancelButton) {
            [problems addObject:@"code_not_cancel"];
        }
        if (visible) [problems addObject:@"still_visible"];
        if ([triggerNote hasSuffix:@"not found"]) [problems addObject:triggerNote];
        BOOL pass = [problems count] == 0;
        printf("OPENPANEL_CHECK mode=%s result=%s code=%ld url=%s expected_url=%s visible=%d trigger=\"%s\"%s%s\n",
               mode, pass ? "PASS" : "FAIL", (long)code, [shown UTF8String],
               wantOpen ? [wantPath UTF8String] : "-", visible, [triggerNote UTF8String],
               pass ? "" : " problems=",
               pass ? "" : [[problems componentsJoinedByString:@","] UTF8String]);
        fflush(stdout);
        return pass ? 0 : 1;
    }
}
