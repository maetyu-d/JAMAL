#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <CoreAudio/AudioHardware.h>

#include "audio_engine.h"
#import "meter_view.h"
#import "memory_map_view.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
    NSWindow *_window;
    NSWindow *_vizWindow;
    NSTextView *_editor;
    NSScrollView *_scrollView;
    NSTextField *_statusLabel;
    MeterView *_meterLeft;
    MeterView *_meterRight;
    MemoryMapView *_memoryView;
    NSTimer *_meterTimer;
    NSTimer *_vizTimer;
    unsigned long long _lastPatternEpoch;
    BOOL _dayMode;
    id _keyMonitor;
    NSRange _errorRange;
    NSURL *_lastScriptURL;
    NSString *_lastScriptName;
    NSSlider *_masterSlider;
    NSTextField *_masterValue;
    NSPopUpButton *_bufferPopup;
    NSPopUpButton *_ratePopup;
    NSPopUpButton *_bitPopup;
    NSPopUpButton *_devicePopup;
    NSTextField *_settingsNote;
    NSTextField *_renderSecondsField;
    NSButton *_renderButton;
    NSBox *_settingsBox;
    NSButton *_playButton;
    NSButton *_stopButton;
    NSButton *_loadButton;
    NSButton *_saveButton;
}

- (void)layoutUI {
    if (!_window) return;
    NSView *content = _window.contentView;
    NSRect bounds = content.bounds;

    CGFloat padding = 16.0;
    CGFloat topBarHeight = 56.0;
    CGFloat buttonHeight = 30.0;
    CGFloat settingsWidth = 240.0;

    CGFloat editorX = padding;
    CGFloat editorY = padding;
    CGFloat editorW = bounds.size.width - settingsWidth - padding * 3;
    CGFloat editorH = bounds.size.height - padding * 2 - topBarHeight;
    if (editorW < 200) editorW = 200;
    if (editorH < 200) editorH = 200;

    _scrollView.frame = NSMakeRect(editorX, editorY, editorW, editorH);
    _editor.frame = _scrollView.bounds;

    CGFloat topY = bounds.size.height - padding - buttonHeight;
    _playButton.frame = NSMakeRect(padding, topY, 84, buttonHeight);
    _stopButton.frame = NSMakeRect(padding + 90, topY, 84, buttonHeight);
    _loadButton.frame = NSMakeRect(padding + 180, topY, 78, buttonHeight);
    _saveButton.frame = NSMakeRect(padding + 266, topY, 78, buttonHeight);

    CGFloat statusX = padding + 350;
    CGFloat statusW = editorW - 360;
    if (statusW < 120) statusW = 120;
    _statusLabel.frame = NSMakeRect(statusX, topY, statusW, buttonHeight);

    CGFloat settingsX = bounds.size.width - padding - settingsWidth;
    CGFloat settingsY = padding;
    CGFloat settingsH = editorH;
    if (settingsH < 260) settingsH = 260;
    _settingsBox.frame = NSMakeRect(settingsX, settingsY, settingsWidth, settingsH);
}


- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    fprintf(stderr, "JAMAL: terminate requested\n");
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"JAMAL";
    alert.informativeText = @"Dedicated to Jamal Ahmad Hamza Khashoggi (13 October 1958 - 2 October 2018).";
    [alert addButtonWithTitle:@"Quit"];
    [alert runModal];
    return NSTerminateNow;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    fprintf(stderr, "JAMAL: did finish launching\n");

    audio_engine_init();
    _errorRange = NSMakeRange(NSNotFound, 0);

    NSArray<NSString *> *args = [[NSProcessInfo processInfo] arguments];
    if (args.count >= 5 && [args[1] isEqualToString:@"--render"]) {
        NSString *scriptPath = args[2];
        NSString *outPath = args[3];
        double seconds = [args[4] doubleValue];
        int sampleRate = 48000;
        int bufferFrames = 256;
        if (args.count >= 6) {
            sampleRate = [args[5] intValue];
        }
        if (args.count >= 7) {
            bufferFrames = [args[6] intValue];
        }
        NSError *readErr = nil;
        NSString *script = [NSString stringWithContentsOfFile:scriptPath encoding:NSUTF8StringEncoding error:&readErr];
        if (!script) {
            fprintf(stderr, "Failed to read script: %s\n", readErr.localizedDescription.UTF8String);
            [NSApp terminate:nil];
            return;
        }
        char error[256] = {0};
        int ok = audio_engine_render_to_wav(script.UTF8String, outPath.UTF8String, seconds, sampleRate, bufferFrames, error, sizeof(error));
        if (!ok) {
            fprintf(stderr, "Render error: %s\n", error);
        } else {
            fprintf(stderr, "Rendered to %s\n", outPath.UTF8String);
        }
        [NSApp terminate:nil];
        return;
    }

    NSRect frame = NSMakeRect(0, 0, 1100, 700);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window center];
    [_window setTitle:@"JAMAL"];

    NSView *content = _window.contentView;

    CGFloat padding = 16;
    CGFloat buttonHeight = 30;
    CGFloat meterWidth = 22;
    CGFloat meterHeight = 150;
    CGFloat topBarHeight = 56;
    CGFloat settingsWidth = 240;

    NSRect editorFrame = NSMakeRect(padding,
                                    padding,
                                    frame.size.width - padding * 3 - settingsWidth,
                                    frame.size.height - padding * 2 - topBarHeight);

    _scrollView = [[NSScrollView alloc] initWithFrame:editorFrame];
    _scrollView.hasVerticalScroller = YES;
    _scrollView.borderType = NSBezelBorder;

    _editor = [[NSTextView alloc] initWithFrame:editorFrame];
    _editor.font = [NSFont fontWithName:@"Menlo" size:13.0];
    _editor.backgroundColor = [NSColor colorWithCalibratedWhite:0.08 alpha:1.0];
    _editor.textColor = [NSColor colorWithCalibratedRed:0.9 green:0.9 blue:0.95 alpha:1.0];
    _editor.insertionPointColor = [NSColor whiteColor];
    _editor.editable = YES;
    _editor.selectable = YES;
    _editor.richText = NO;
    _editor.allowsUndo = YES;
    _editor.automaticQuoteSubstitutionEnabled = NO;
    _editor.automaticDashSubstitutionEnabled = NO;
    _editor.automaticSpellingCorrectionEnabled = NO;
    _editor.automaticTextCompletionEnabled = NO;
    [_editor setMinSize:NSMakeSize(0.0, 0.0)];
    [_editor setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
    [_editor setVerticallyResizable:YES];
    [_editor setHorizontallyResizable:NO];
    [_editor setAutoresizingMask:NSViewWidthSizable];
    [_editor.textContainer setContainerSize:NSMakeSize(editorFrame.size.width, FLT_MAX)];
    [_editor.textContainer setWidthTracksTextView:YES];

    NSString *initialScript = @"tempo_scale 1\n"
    "tempo 64\n"
    "tempo_map (intro=0.5,verse=0.5,chorus=1.0,bridge=0.5,final=1.0)\n"
    "root D3\n"
    "maqam hijaz\n"
    "\n"
    "// Palette\n"
    "synth lead acid\n"
    "set lead cutoff 1400\n"
    "set lead res 0.45\n"
    "set lead atk 0.005\n"
    "set lead dec 0.12\n"
    "set lead sus 0.55\n"
    "set lead rel 0.2\n"
    "synth bass acid\n"
    "set bass cutoff 600\n"
    "set bass res 0.3\n"
    "set bass atk 0.005\n"
    "set bass dec 0.18\n"
    "set bass sus 0.45\n"
    "set bass rel 0.2\n"
    "synth k kick909\n"
    "synth s snare909\n"
    "synth hc hat909\n"
    "synth ho hat808\n"
    "synth c clap909\n"
    "synth rim rim\n"
    "synth metal metal\n"
    "set s dec 0.06\n"
    "set c dec 0.08\n"
    "set hc dec 0.02\n"
    "set ho dec 0.08\n"
    "set rim dec 0.02\n"
    "set metal dec 0.08\n"
    "synth lead2 pulse\n"
    "set lead2 cutoff 1800\n"
    "set lead2 res 0.2\n"
    "set lead2 atk 0.003\n"
    "set lead2 dec 0.1\n"
    "set lead2 sus 0.4\n"
    "set lead2 rel 0.15\n"
    "\n"
    "// Section patterns\n"
    "pattern intro_mel (1 . 2 . 3- . 4 . 5 . 4 . 3- . 2 .)\n"
    "pattern verse_mel (1 2 3-~40 4 5 4 3- 2 1 2 3 4 5 6 7 1')\n"
    "pattern chorus_mel (1'! . 7 6! . 5 6 . 7! 1' . 6 . 5! 4 .)\n"
    "pattern bridge_mel (5 4 3- 2 1 2 3- 4 5 6 5 4 3- 2 1)\n"
    "pattern final_mel (1' . 7 6 . 5 6 . 7 1' . 2' 1' . 7 6)\n"
    "\n"
    "pattern intro_bass (1, . . . 1, . . . 5,, . . . 1, . . .)\n"
    "pattern verse_bass (1, . 1, . 5, . 1, . 4, . 5, . 1, . 7,, .)\n"
    "pattern chorus_bass (1, . 5, 1, . 4, . 5, 1, . 7,, . 1, 5, .)\n"
    "pattern bridge_bass (5,, . 1, . 4,, . 1, . 5,, . 1, . 7,, .)\n"
    "pattern final_bass (1, 1, 5, 1, 4, 5, 1, 7,, 1, 1, 5, 1, 4, 5, 1, 7,,)\n"
    "\n"
    "pattern intro_k (1 . . . . . . . 1 . . . . . . .)\n"
    "pattern verse_k (1 . . . 1 . . . 1 . . . 1 . . .)\n"
    "pattern chorus_k (1 . 1 . 1 . 1 . 1 . 1 . 1 . 1 .)\n"
    "pattern final_k (1 1 . 1 1 . 1 1 1 . 1 1 1 . 1 1)\n"
    "\n"
    "pattern verse_s (. . 1 . . . 1 . . . 1 . . . 1 .)\n"
    "pattern chorus_s (. . 1 . . 1 . . . . 1 . . 1 . .)\n"
    "pattern final_s (. . 1 . 1 . 1 . . . 1 . 1 . 1 .)\n"
    "\n"
    "pattern hats_8 (1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1)\n"
    "pattern hats_16 (1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1)\n"
    "pattern open_hat (. . . . . 1 . . . . . 1 . . . .)\n"
    "pattern clap_pat (. . . . 1 . . . . . . . 1 . . .)\n"
    "pattern rim_pat (. . 1 . . . . . . . 1 . . . . .)\n"
    "pattern metal_pat (. 1 . . . . 1 . . . . 1 . . . .)\n"
    "\n"
    "// Sequence structure\n"
    "sequence melseq (intro_mel, verse_mel, chorus_mel, verse_mel, chorus_mel, bridge_mel, final_mel*2)\n"
    "sequence mel2seq (intro_mel, verse_mel, chorus_mel, verse_mel, chorus_mel, bridge_mel, final_mel*2)\n"
    "sequence bassseq (intro_bass, verse_bass, chorus_bass, verse_bass, chorus_bass, bridge_bass, final_bass*2)\n"
    "sequence kseq (intro_k, verse_k, chorus_k, verse_k, chorus_k, verse_k, final_k*2)\n"
    "sequence sseq (verse_s, verse_s, chorus_s, verse_s, chorus_s, verse_s, final_s*2)\n"
    "\n"
    "// Play\n"
    "playseq melseq lead fast 2 orn 0.3 alt slide 35 acc 0.25\n"
    "playseq mel2seq lead2 fast 4 density 0.6 slide 20 only 7-7\n"
    "playseq bassseq bass fast 1 density 0.95 slide 20\n"
    "playseq kseq k\n"
    "playseq sseq s\n"
    "play hats_8 hc fast 2 density 0.35 only 1-6\n"
    "play hats_16 hc fast 4 density 0.15 only 7-7\n"
    "play open_hat ho density 0.2 only 3-7\n"
    "play clap_pat c only 3-7\n"
    "play rim_pat rim density 0.6 only 7-7\n"
    "play metal_pat metal density 0.5 only 7-7\n";

    _editor.string = initialScript;
    _scrollView.documentView = _editor;
    [content addSubview:_scrollView];

    _playButton = [[NSButton alloc] initWithFrame:NSZeroRect];
    _playButton.title = @"Play";
    _playButton.bezelStyle = NSBezelStyleTexturedRounded;
    _playButton.target = self;
    _playButton.action = @selector(handlePlay:);
    [content addSubview:_playButton];

    _stopButton = [[NSButton alloc] initWithFrame:NSZeroRect];
    _stopButton.title = @"Stop";
    _stopButton.bezelStyle = NSBezelStyleTexturedRounded;
    _stopButton.target = self;
    _stopButton.action = @selector(handleStop:);
    [content addSubview:_stopButton];

    _loadButton = [[NSButton alloc] initWithFrame:NSZeroRect];
    _loadButton.title = @"Load";
    _loadButton.bezelStyle = NSBezelStyleTexturedRounded;
    _loadButton.target = self;
    _loadButton.action = @selector(handleLoad:);
    [content addSubview:_loadButton];

    _saveButton = [[NSButton alloc] initWithFrame:NSZeroRect];
    _saveButton.title = @"Save";
    _saveButton.bezelStyle = NSBezelStyleTexturedRounded;
    _saveButton.target = self;
    _saveButton.action = @selector(handleSave:);
    [content addSubview:_saveButton];

    _statusLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _statusLabel.editable = NO;
    _statusLabel.bezeled = NO;
    _statusLabel.drawsBackground = NO;
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.9 green:0.4 blue:0.4 alpha:1.0];
    _statusLabel.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
    [content addSubview:_statusLabel];

    NSRect settingsFrame = NSMakeRect(frame.size.width - padding - settingsWidth,
                                      padding,
                                      settingsWidth,
                                      editorFrame.size.height);
    _settingsBox = [[NSBox alloc] initWithFrame:settingsFrame];
    _settingsBox.title = @"";
    _settingsBox.boxType = NSBoxPrimary;
    _settingsBox.titlePosition = NSNoTitle;
    _settingsBox.contentViewMargins = NSMakeSize(0.0, 0.0);
    [content addSubview:_settingsBox];

    NSView *settingsContent = _settingsBox.contentView;

    CGFloat settingsPad = 8.0;
    CGFloat labelH = 14.0;
    CGFloat rowH = 20.0;
    CGFloat groupGap = 50.0;
    CGFloat y = settingsFrame.size.height - 50.0;

    NSTextField *masterLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, labelH)];
    masterLabel.editable = NO;
    masterLabel.bezeled = NO;
    masterLabel.drawsBackground = NO;
    masterLabel.textColor = [NSColor secondaryLabelColor];
    masterLabel.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    masterLabel.stringValue = @"Master Volume";
    [settingsContent addSubview:masterLabel];
    y -= rowH;

    _masterSlider = [[NSSlider alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2 - 40, rowH)];
    _masterSlider.minValue = 0.0;
    _masterSlider.maxValue = 2.0;
    _masterSlider.floatValue = 1.0;
    _masterSlider.target = self;
    _masterSlider.action = @selector(handleMasterChange:);
    [settingsContent addSubview:_masterSlider];

    _masterValue = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsWidth - settingsPad - 36, y, 36, rowH)];
    _masterValue.editable = NO;
    _masterValue.bezeled = NO;
    _masterValue.drawsBackground = NO;
    _masterValue.alignment = NSTextAlignmentRight;
    _masterValue.textColor = [NSColor secondaryLabelColor];
    _masterValue.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightMedium];
    _masterValue.stringValue = @"1.00";
    [settingsContent addSubview:_masterValue];
    y -= groupGap;

    NSTextField *bufferLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, labelH)];
    bufferLabel.editable = NO;
    bufferLabel.bezeled = NO;
    bufferLabel.drawsBackground = NO;
    bufferLabel.textColor = [NSColor secondaryLabelColor];
    bufferLabel.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    bufferLabel.stringValue = @"Buffer Size";
    [settingsContent addSubview:bufferLabel];
    y -= rowH;

    _bufferPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, rowH)];
    [_bufferPopup addItemsWithTitles:@[@"128", @"256", @"512"]];
    _bufferPopup.target = self;
    _bufferPopup.action = @selector(handleBufferChange:);
    [_bufferPopup selectItemWithTitle:@"256"];
    [settingsContent addSubview:_bufferPopup];
    y -= groupGap;

    NSTextField *rateLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, labelH)];
    rateLabel.editable = NO;
    rateLabel.bezeled = NO;
    rateLabel.drawsBackground = NO;
    rateLabel.textColor = [NSColor secondaryLabelColor];
    rateLabel.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    rateLabel.stringValue = @"Sample Rate";
    [settingsContent addSubview:rateLabel];
    y -= rowH;

    _ratePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, rowH)];
    [_ratePopup addItemsWithTitles:@[@"44.1 kHz", @"48 kHz"]];
    _ratePopup.target = self;
    _ratePopup.action = @selector(handleRateChange:);
    [_ratePopup selectItemWithTitle:@"48 kHz"];
    [settingsContent addSubview:_ratePopup];
    y -= groupGap;

    NSTextField *bitLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, labelH)];
    bitLabel.editable = NO;
    bitLabel.bezeled = NO;
    bitLabel.drawsBackground = NO;
    bitLabel.textColor = [NSColor secondaryLabelColor];
    bitLabel.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    bitLabel.stringValue = @"Bit Depth";
    [settingsContent addSubview:bitLabel];
    y -= rowH;

    _bitPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, rowH)];
    [_bitPopup addItemsWithTitles:@[@"16-bit", @"24-bit", @"32-bit float"]];
    _bitPopup.target = self;
    _bitPopup.action = @selector(handleBitDepthChange:);
    [_bitPopup selectItemWithTitle:@"32-bit float"];
    [settingsContent addSubview:_bitPopup];
    y -= groupGap;

    NSTextField *deviceLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, labelH)];
    deviceLabel.editable = NO;
    deviceLabel.bezeled = NO;
    deviceLabel.drawsBackground = NO;
    deviceLabel.textColor = [NSColor secondaryLabelColor];
    deviceLabel.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    deviceLabel.stringValue = @"Output Device";
    [settingsContent addSubview:deviceLabel];
    y -= rowH;

    _devicePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, rowH)];
    _devicePopup.target = self;
    _devicePopup.action = @selector(handleDeviceChange:);
    [settingsContent addSubview:_devicePopup];

    y -= groupGap;
    NSTextField *renderLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsPad, y, settingsWidth - settingsPad * 2, labelH)];
    renderLabel.editable = NO;
    renderLabel.bezeled = NO;
    renderLabel.drawsBackground = NO;
    renderLabel.textColor = [NSColor secondaryLabelColor];
    renderLabel.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    renderLabel.stringValue = @"Render (sec)";
    [settingsContent addSubview:renderLabel];
    y -= rowH;

    _renderSecondsField = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsPad, y, 64, rowH)];
    _renderSecondsField.stringValue = @"30";
    _renderSecondsField.alignment = NSTextAlignmentRight;
    [settingsContent addSubview:_renderSecondsField];

    _renderButton = [[NSButton alloc] initWithFrame:NSMakeRect(settingsPad + 72, y, settingsWidth - settingsPad * 2 - 72, rowH)];
    _renderButton.title = @"Render WAV";
    _renderButton.bezelStyle = NSBezelStyleTexturedRounded;
    _renderButton.target = self;
    _renderButton.action = @selector(handleRender:);
    [settingsContent addSubview:_renderButton];

    y -= groupGap;
    _settingsNote = [[NSTextField alloc] initWithFrame:NSMakeRect(settingsPad, y - 18.0, settingsWidth - settingsPad * 2, 14.0)];
    _settingsNote.editable = NO;
    _settingsNote.bezeled = NO;
    _settingsNote.drawsBackground = NO;
    _settingsNote.textColor = [NSColor secondaryLabelColor];
    _settingsNote.font = [NSFont systemFontOfSize:10.0 weight:NSFontWeightRegular];
    _settingsNote.stringValue = @"";
    [settingsContent addSubview:_settingsNote];

    [self populateOutputDevices];

    CGFloat meterW = meterWidth + 6.0f;
    CGFloat meterH = meterHeight + 5.0f;
    CGFloat meterGap = 12.0f;
    CGFloat totalMeters = meterW * 2.0f + meterGap;
    CGFloat meterX = (settingsWidth - totalMeters) * 0.5f - 5.0f;
    CGFloat meterY = 12.0f;
    _meterLeft = [[MeterView alloc] initWithFrame:NSMakeRect(meterX, meterY, meterW, meterH)];
    _meterRight = [[MeterView alloc] initWithFrame:NSMakeRect(meterX + meterW + meterGap, meterY, meterW, meterH)];
    [settingsContent addSubview:_meterLeft];
    [settingsContent addSubview:_meterRight];

    NSRect vizFrame = NSMakeRect(0, 0, 520, 720);
    _vizWindow = [[NSWindow alloc] initWithContentRect:vizFrame
                                             styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
    [_vizWindow setTitle:@"JAMAL Visuals"];
    [_vizWindow setLevel:NSNormalWindowLevel];
    [_vizWindow center];

    _memoryView = [[MemoryMapView alloc] initWithFrame:_vizWindow.contentView.bounds];
    _memoryView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [_vizWindow.contentView addSubview:_memoryView];

    _meterTimer = [NSTimer scheduledTimerWithTimeInterval:0.033
                                                   target:self
                                                 selector:@selector(updateMeters:)
                                                 userInfo:nil
                                                  repeats:YES];

    __weak __typeof__(self) weakSelf = self;
    _keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent * _Nullable(NSEvent * _Nonnull event) {
        if ((event.modifierFlags & NSEventModifierFlagCommand) && event.keyCode == 36) {
            [weakSelf handlePlay:nil];
            return nil;
        }
        if (event.modifierFlags & NSEventModifierFlagCommand) {
            NSString *chars = event.charactersIgnoringModifiers.lowercaseString;
            if ([chars isEqualToString:@"c"]) {
                [NSApp sendAction:@selector(copy:) to:nil from:nil];
                return nil;
            }
            if ([chars isEqualToString:@"z"]) {
                if (event.modifierFlags & NSEventModifierFlagShift) {
                    [NSApp sendAction:@selector(redo:) to:nil from:nil];
                } else {
                    [NSApp sendAction:@selector(undo:) to:nil from:nil];
                }
                return nil;
            }
            if ([chars isEqualToString:@"v"]) {
                [NSApp sendAction:@selector(paste:) to:nil from:nil];
                return nil;
            }
            if ([chars isEqualToString:@"x"]) {
                [NSApp sendAction:@selector(cut:) to:nil from:nil];
                return nil;
            }
            if ([chars isEqualToString:@"a"]) {
                [NSApp sendAction:@selector(selectAll:) to:nil from:nil];
                return nil;
            }
        }
        return event;
    }];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(layoutUI)
                                                 name:NSWindowDidResizeNotification
                                               object:_window];

    [self layoutUI];
    [_window makeKeyAndOrderFront:nil];
    [_window setInitialFirstResponder:_editor];
    [_window makeFirstResponder:_editor];
    [_vizWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    _lastPatternEpoch = 0;
    _dayMode = NO;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    [_meterTimer invalidate];
    [_vizTimer invalidate];
    if (_keyMonitor) {
        [NSEvent removeMonitor:_keyMonitor];
        _keyMonitor = nil;
    }
    audio_engine_shutdown();
}

- (void)clearErrorHighlight {
    if (_errorRange.location != NSNotFound && _errorRange.length > 0) {
        [_editor.textStorage removeAttribute:NSBackgroundColorAttributeName range:_errorRange];
        _errorRange = NSMakeRange(NSNotFound, 0);
    }
}

- (void)highlightLine:(NSInteger)lineNumber {
    if (lineNumber <= 0) {
        return;
    }

    NSString *text = _editor.string ?: @"";
    __block NSInteger currentLine = 1;
    __block NSRange lineRange = NSMakeRange(NSNotFound, 0);
    [text enumerateSubstringsInRange:NSMakeRange(0, text.length)
                             options:NSStringEnumerationByLines | NSStringEnumerationSubstringNotRequired
                          usingBlock:^(__unused NSString *substring, NSRange range, __unused NSRange enclosingRange, BOOL *stop) {
        if (currentLine == lineNumber) {
            lineRange = range;
            *stop = YES;
        }
        currentLine++;
    }];

    if (lineRange.location != NSNotFound) {
        [self clearErrorHighlight];
        _errorRange = lineRange;
        [_editor.textStorage addAttribute:NSBackgroundColorAttributeName
                                    value:[NSColor colorWithCalibratedRed:0.3 green:0.1 blue:0.1 alpha:0.6]
                                    range:_errorRange];
    }
}

- (void)handlePlay:(id)sender {
    (void)sender;
    const char *script = _editor.string.UTF8String;
    char error[256] = {0};
    int ok = audio_engine_play_script(script, error, sizeof(error));
    if (!ok) {
        _statusLabel.stringValue = [NSString stringWithUTF8String:error];
        NSString *err = [NSString stringWithUTF8String:error];
        NSRange lineRange = [err rangeOfString:@"Line "];
        if (lineRange.location != NSNotFound) {
            NSString *tail = [err substringFromIndex:lineRange.location + lineRange.length];
            NSInteger line = [tail integerValue];
            [self highlightLine:line];
        }
    } else {
        audio_engine_set_master(_masterSlider.floatValue);
        _statusLabel.stringValue = @"";
        [self clearErrorHighlight];
        [_vizTimer invalidate];
        float tempo = audio_engine_get_tempo();
        if (tempo < 20.0f) tempo = 120.0f;
        NSTimeInterval tick = (60.0 / tempo) / 4.0; // 16th-note grid
        _vizTimer = [NSTimer scheduledTimerWithTimeInterval:tick
                                                     target:self
                                                   selector:@selector(updateViz:)
                                                   userInfo:nil
                                                    repeats:YES];
    }
}

- (void)handleStop:(id)sender {
    (void)sender;
    audio_engine_stop();
    [_vizTimer invalidate];
    _vizTimer = nil;
}

- (void)handleMasterChange:(id)sender {
    (void)sender;
    float value = _masterSlider.floatValue;
    audio_engine_set_master(value);
    _masterValue.stringValue = [NSString stringWithFormat:@"%.2f", value];
}

- (void)setSettingsDirty:(BOOL)dirty {
    if (!_settingsNote) return;
    _settingsNote.stringValue = @"";
}

- (void)restartAudioIfRunning {
    if (audio_engine_is_running()) {
        [self handleStop:nil];
        [self handlePlay:nil];
        if (_settingsNote) {
            _settingsNote.stringValue = @"Restarted";
        }
    }
}

- (void)handleBufferChange:(id)sender {
    (void)sender;
    int frames = _bufferPopup.selectedItem.title.intValue;
    audio_engine_set_buffer_frames(frames);
    if (audio_engine_is_running()) {
        [self restartAudioIfRunning];
    } else {
        [self setSettingsDirty:YES];
    }
}

- (void)handleRateChange:(id)sender {
    (void)sender;
    NSString *title = _ratePopup.selectedItem.title;
    double rate = [title containsString:@"44.1"] ? 44100.0 : 48000.0;
    audio_engine_set_sample_rate(rate);
    if (audio_engine_is_running()) {
        [self restartAudioIfRunning];
    } else {
        [self setSettingsDirty:YES];
    }
}

- (void)handleBitDepthChange:(id)sender {
    (void)sender;
    NSString *title = _bitPopup.selectedItem.title;
    int bits = [title hasPrefix:@"16"] ? 16 : ([title hasPrefix:@"24"] ? 24 : 32);
    audio_engine_set_bit_depth(bits);
    if (audio_engine_is_running()) {
        [self restartAudioIfRunning];
    } else {
        [self setSettingsDirty:YES];
    }
}

- (void)handleDeviceChange:(id)sender {
    (void)sender;
    NSNumber *deviceId = _devicePopup.selectedItem.representedObject;
    unsigned int dev = deviceId ? deviceId.unsignedIntValue : 0;
    if (dev == 0) {
        AudioDeviceID defDev = 0;
        AudioObjectPropertyAddress defAddr = {
            kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        UInt32 defSize = sizeof(defDev);
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defAddr, 0, NULL, &defSize, &defDev) == noErr) {
            dev = (unsigned int)defDev;
        }
    }
    audio_engine_set_output_device(dev);
    if (audio_engine_is_running()) {
        [self restartAudioIfRunning];
    } else {
        [self setSettingsDirty:YES];
    }
}

- (void)handleRender:(id)sender {
    (void)sender;
    double seconds = _renderSecondsField.doubleValue;
    if (seconds <= 0.0) {
        _statusLabel.stringValue = @"Render requires a positive duration";
        return;
    }
    NSString *title = _ratePopup.selectedItem.title;
    int sampleRate = [title containsString:@"44.1"] ? 44100 : 48000;
    int bufferFrames = _bufferPopup.selectedItem.title.intValue;

    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.allowedContentTypes = @[ UTTypeAudio ];
    panel.canCreateDirectories = YES;
    panel.prompt = @"Render";
    panel.nameFieldStringValue = @"render.wav";
    if ([panel runModal] != NSModalResponseOK) {
        return;
    }
    NSURL *url = panel.URL;
    if (!url) return;

    if (audio_engine_is_running()) {
        [self handleStop:nil];
    }

    NSString *script = _editor.string ?: @"";
    char error[256] = {0};
    int ok = audio_engine_render_to_wav(script.UTF8String, url.path.UTF8String, seconds, sampleRate, bufferFrames, error, sizeof(error));
    if (!ok) {
        _statusLabel.stringValue = [NSString stringWithFormat:@"Render error: %s", error];
    } else {
        _statusLabel.stringValue = @"Render complete";
    }
}

- (void)populateOutputDevices {
    [_devicePopup removeAllItems];
    NSMenuItem *defaultItem = [[NSMenuItem alloc] initWithTitle:@"Default Output" action:nil keyEquivalent:@""];
    defaultItem.representedObject = @(0);
    [_devicePopup.menu addItem:defaultItem];

    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size) != noErr) {
        return;
    }
    int count = (int)(size / sizeof(AudioDeviceID));
    if (count <= 0) {
        return;
    }
    AudioDeviceID *devices = malloc(size);
    if (!devices) {
        return;
    }
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, devices) != noErr) {
        free(devices);
        return;
    }

    for (int i = 0; i < count; i++) {
        AudioDeviceID dev = devices[i];
        AudioObjectPropertyAddress cfgAddr = {
            kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        UInt32 cfgSize = 0;
        if (AudioObjectGetPropertyDataSize(dev, &cfgAddr, 0, NULL, &cfgSize) != noErr || cfgSize == 0) {
            continue;
        }
        AudioBufferList *bufList = (AudioBufferList *)malloc(cfgSize);
        if (!bufList) continue;
        if (AudioObjectGetPropertyData(dev, &cfgAddr, 0, NULL, &cfgSize, bufList) != noErr) {
            free(bufList);
            continue;
        }
        int channels = 0;
        for (UInt32 b = 0; b < bufList->mNumberBuffers; b++) {
            channels += bufList->mBuffers[b].mNumberChannels;
        }
        free(bufList);
        if (channels <= 0) {
            continue;
        }

        CFStringRef name = NULL;
        AudioObjectPropertyAddress nameAddr = {
            kAudioObjectPropertyName,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        UInt32 nameSize = sizeof(name);
        NSString *title = nil;
        if (AudioObjectGetPropertyData(dev, &nameAddr, 0, NULL, &nameSize, &name) == noErr && name) {
            title = [(__bridge NSString *)name copy];
            CFRelease(name);
        } else {
            title = [NSString stringWithFormat:@"Device %u", (unsigned int)dev];
        }

        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        item.representedObject = @((unsigned int)dev);
        [_devicePopup.menu addItem:item];
    }

    free(devices);

    AudioDeviceID defDev = 0;
    AudioObjectPropertyAddress defAddr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 defSize = sizeof(defDev);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defAddr, 0, NULL, &defSize, &defDev) == noErr) {
        for (NSMenuItem *item in _devicePopup.itemArray) {
            NSNumber *devNum = item.representedObject;
            if (devNum && devNum.unsignedIntValue == (unsigned int)defDev) {
                [_devicePopup selectItem:item];
                audio_engine_set_output_device((unsigned int)defDev);
                [self setSettingsDirty:YES];
                break;
            }
        }
    }
}

- (void)handleLoad:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    UTType *jamalType = [UTType typeWithFilenameExtension:@"jamal"];
    if (jamalType) {
        panel.allowedContentTypes = @[ jamalType, UTTypePlainText ];
    } else {
        panel.allowedContentTypes = @[ UTTypePlainText ];
    }
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;
    panel.canChooseFiles = YES;
    panel.prompt = @"Load";
    if (_lastScriptURL) {
        panel.directoryURL = [_lastScriptURL URLByDeletingLastPathComponent];
    }
    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = panel.URL;
        if (!url) return;
        NSError *err = nil;
        NSString *text = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:&err];
        if (text) {
            _editor.string = text;
            _lastScriptURL = url;
            _lastScriptName = url.lastPathComponent ?: @"jamal_script.jamal";
        }
    }
}

- (void)handleSave:(id)sender {
    (void)sender;
    NSSavePanel *panel = [NSSavePanel savePanel];
    UTType *jamalType = [UTType typeWithFilenameExtension:@"jamal"];
    if (jamalType) {
        panel.allowedContentTypes = @[ jamalType, UTTypePlainText ];
    } else {
        panel.allowedContentTypes = @[ UTTypePlainText ];
    }
    panel.canCreateDirectories = YES;
    panel.prompt = @"Save";
    if (_lastScriptURL) {
        panel.directoryURL = [_lastScriptURL URLByDeletingLastPathComponent];
    }
    if (_lastScriptName.length == 0) {
        _lastScriptName = @"jamal_script.jamal";
    }
    panel.nameFieldStringValue = _lastScriptName;
    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = panel.URL;
        if (!url) return;
        if (url.pathExtension.length == 0) {
            url = [url URLByAppendingPathExtension:@"jamal"];
        }
        NSError *err = nil;
        [_editor.string writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:&err];
        _lastScriptURL = url;
        _lastScriptName = url.lastPathComponent ?: @"jamal_script.jamal";
    }
}

- (void)updateMeters:(NSTimer *)timer {
    (void)timer;
    float rmsL = 0.0f;
    float rmsR = 0.0f;
    float peakL = 0.0f;
    float peakR = 0.0f;
    int clip = 0;
    audio_engine_get_meter_ex(&rmsL, &rmsR, &peakL, &peakR, &clip);
    _meterLeft.level = fminf(rmsL * 2.5f, 1.0f);
    _meterRight.level = fminf(rmsR * 2.5f, 1.0f);
    _meterLeft.peak = peakL * 2.5f;
    _meterRight.peak = peakR * 2.5f;
    _meterLeft.clip = clip ? YES : NO;
    _meterRight.clip = clip ? YES : NO;
}

- (void)updateViz:(NSTimer *)timer {
    (void)timer;
    if (!audio_engine_is_running()) {
        return;
    }
    unsigned long long epoch = audio_engine_get_pattern_epoch();
    if (epoch != _lastPatternEpoch) {
        _lastPatternEpoch = epoch;
        _dayMode = !_dayMode;
        [_memoryView setDayMode:_dayMode];
        [_memoryView triggerPortal];
    }
    float left = 0.0f;
    float right = 0.0f;
    audio_engine_get_meter(&left, &right);
    float level = fminf((left + right) * 1.25f, 1.0f);
    // Beat pulse synced to current tempo (16th note)
    float tempo = audio_engine_get_tempo();
    double now = CACurrentMediaTime();
    double beat = 60.0 / (tempo > 1.0f ? tempo : 120.0f) / 4.0;
    float phase = (float)fmod(now, beat) / (float)beat;
    float pulse = expf(-phase * 6.0f);
    [_memoryView setBeatPulse:pulse];
    [_memoryView tickWithLevel:level];
}

@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
