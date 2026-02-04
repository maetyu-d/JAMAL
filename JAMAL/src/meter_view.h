#import <Cocoa/Cocoa.h>

@interface MeterView : NSView
@property (nonatomic, assign) float level; // RMS 0..1
@property (nonatomic, assign) float peak;  // Peak 0..1+
@property (nonatomic, assign) BOOL clip;
@end
