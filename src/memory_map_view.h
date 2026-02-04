#import <Cocoa/Cocoa.h>

@interface MemoryMapView : NSView
- (void)tickWithLevel:(float)level;
- (void)setDayMode:(BOOL)isDay;
- (void)triggerPortal;
- (void)setBeatPulse:(float)pulse;
@end
