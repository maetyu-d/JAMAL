#import "meter_view.h"

@implementation MeterView

- (instancetype)initWithFrame:(NSRect)frameRect {
    if ((self = [super initWithFrame:frameRect])) {
        _level = 0.0f;
        _peak = 0.0f;
        _clip = NO;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)setLevel:(float)level {
    _level = fminf(fmaxf(level, 0.0f), 1.0f);
    [self setNeedsDisplay:YES];
}

- (void)setPeak:(float)peak {
    _peak = fmaxf(peak, 0.0f);
    [self setNeedsDisplay:YES];
}

- (void)setClip:(BOOL)clip {
    _clip = clip;
    [self setNeedsDisplay:YES];
}

- (float)dbFromLevel:(float)level {
    float clamped = fmaxf(level, 0.00001f);
    float db = 20.0f * log10f(clamped);
    if (db < -60.0f) db = -60.0f;
    if (db > 6.0f) db = 6.0f;
    return db;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor colorWithCalibratedWhite:0.1 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    float height = self.bounds.size.height * _level;
    NSRect bar = NSMakeRect(0, self.bounds.size.height - height, self.bounds.size.width, height);

    NSColor *color = _level > 0.7f ? [NSColor colorWithCalibratedRed:0.9 green:0.3 blue:0.3 alpha:1.0]
                                 : [NSColor colorWithCalibratedRed:0.25 green:0.7 blue:0.95 alpha:1.0];
    [color setFill];
    NSRectFill(bar);

    float peakY = self.bounds.size.height - (self.bounds.size.height * fminf(_peak, 1.0f));
    NSRect peakBar = NSMakeRect(0, peakY, self.bounds.size.width, 2.0f);
    [[NSColor colorWithCalibratedRed:0.98 green:0.85 blue:0.3 alpha:1.0] setFill];
    NSRectFill(peakBar);

    if (_clip || _peak > 1.0f) {
        [[NSColor colorWithCalibratedRed:0.95 green:0.1 blue:0.1 alpha:1.0] setFill];
        NSRect clipRect = NSMakeRect(0, 0, self.bounds.size.width, 3.0f);
        NSRectFill(clipRect);
    }

    [[NSColor colorWithCalibratedWhite:0.2 alpha:1.0] setStroke];
    NSFrameRect(self.bounds);
}

@end
