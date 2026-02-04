#import "memory_map_view.h"
#include <string.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} Pixel;

static Pixel hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f) { r = c; g = x; b = 0.0f; }
    else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
    else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
    else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
    else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
    else { r = c; g = 0.0f; b = x; }
    Pixel p;
    p.r = (uint8_t)fminf((r + m) * 255.0f, 255.0f);
    p.g = (uint8_t)fminf((g + m) * 255.0f, 255.0f);
    p.b = (uint8_t)fminf((b + m) * 255.0f, 255.0f);
    p.a = 255;
    return p;
}

@interface MemoryMapView ()
@end

@implementation MemoryMapView {
    Pixel *_pixels;
    Pixel *_scratch;
    int _w;
    int _h;
    uint32_t _rng;
    float _time;
    float _scroll;
    float _skyline[256];
    BOOL _day;
    float _portal;
    float _portalDecay;
    float _beatPulse;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    if ((self = [super initWithFrame:frameRect])) {
        _w = 128;
        _h = 128;
        _pixels = calloc((size_t)_w * (size_t)_h, sizeof(Pixel));
        _scratch = calloc((size_t)_w * (size_t)_h, sizeof(Pixel));
        _rng = 0x12345678u;
        _time = 0.0f;
        _scroll = 0.0f;
        _day = NO;
        _portal = 0.0f;
        _portalDecay = 0.9f;
        _beatPulse = 0.0f;
        [self setWantsLayer:YES];
        [self seedPixels];
    }
    return self;
}

- (void)setDayMode:(BOOL)isDay {
    _day = isDay;
    [self setNeedsDisplay:YES];
}

- (void)triggerPortal {
    _portal = 1.0f;
    _portalDecay = 0.92f;
}

- (void)setBeatPulse:(float)pulse {
    _beatPulse = pulse;
}

- (void)dealloc {
    free(_pixels);
    free(_scratch);
}

- (BOOL)isFlipped {
    return YES;
}

- (uint32_t)nextRand {
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return _rng;
}

- (Pixel)palette:(uint32_t)r {
    float h = fmodf((r % 360) + _time * 3.0f, 360.0f);
    return hsv_to_rgb(h, 0.65f, 0.85f);
}

- (Pixel)bgPixel {
    return hsv_to_rgb(fmodf(_time * 2.0f, 360.0f), 0.6f, 0.12f);
}

- (float)hash:(int)x y:(int)y {
    uint32_t h = (uint32_t)(x * 374761393u + y * 668265263u) ^ (uint32_t)_rng;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h & 0xFFFFFF) / 16777215.0f;
}

- (float)fractal:(float)x y:(float)y {
    float amp = 0.6f;
    float freq = 0.05f;
    float sum = 0.0f;
    for (int i = 0; i < 5; i++) {
        int xi = (int)floorf(x * freq);
        int yi = (int)floorf(y * freq);
        sum += [self hash:xi y:yi] * amp;
        amp *= 0.5f;
        freq *= 2.05f;
    }
    return sum;
}

- (void)seedPixels {
    for (int i = 0; i < _w; i++) {
        _skyline[i] = 0.35f + 0.35f * [self fractal:(float)i y:1.0f];
    }
    for (int y = 0; y < _h; y++) {
        for (int x = 0; x < _w; x++) {
            _pixels[y * _w + x] = [self bgPixel];
        }
    }
}

- (void)tickWithLevel:(float)level {
    _time += 0.4f + level * 1.8f;
    if (_portal > 0.001f) {
        _portal *= _portalDecay;
    }
    _scroll += 0.1f + level * 0.6f;
    if (_scroll > _h) _scroll -= _h;

    float hueBase = fmodf(_time * 4.0f, 360.0f);
    Pixel skyTop = hsv_to_rgb(hueBase + (_day ? 35.0f : 210.0f), 0.6f, _day ? 0.85f : 0.22f);
    Pixel skyBot = hsv_to_rgb(hueBase + (_day ? 10.0f : 240.0f), 0.7f, _day ? 0.6f : 0.15f);
    Pixel sea = hsv_to_rgb(hueBase + (_day ? 210.0f : 260.0f), 0.6f, _day ? 0.45f : 0.18f);
    Pixel glow = hsv_to_rgb(hueBase + 40.0f, 0.85f, 0.98f);

    for (int y = 0; y < _h; y++) {
        float t = (float)y / (float)_h;
        Pixel sky = (Pixel){
            (uint8_t)fminf(skyTop.r * (1.0f - t) + skyBot.r * t, 255.0f),
            (uint8_t)fminf(skyTop.g * (1.0f - t) + skyBot.g * t, 255.0f),
            (uint8_t)fminf(skyTop.b * (1.0f - t) + skyBot.b * t, 255.0f),
            255
        };
        for (int x = 0; x < _w; x++) {
            Pixel base = (y < _h * 0.6f) ? sky : sea;
            if (y < _h * 0.6f) {
                float cloud = [self fractal:(float)x * 0.6f + _time * 0.2f y:(float)y * 0.5f];
                if (cloud > 0.65f) {
                    base.r = (uint8_t)fminf(base.r + 20, 255);
                    base.g = (uint8_t)fminf(base.g + 18, 255);
                    base.b = (uint8_t)fminf(base.b + 25, 255);
                }
                if (!_day && cloud < 0.18f && (x % 5 == 0) && (y % 7 == 0)) {
                    base = (Pixel){245, 240, 255, 255};
                }
            }
            _pixels[y * _w + x] = base;
        }
    }

    float zoom = 1.0f + 0.18f * sinf(_time * 0.02f) + _portal * 0.6f + _beatPulse * 0.2f;
    for (int x = 0; x < _w; x++) {
        float fx = ((float)x - _w * 0.5f) / (_w * zoom);
        float n = [self fractal:fx * 140.0f + _time * 0.35f y:0.0f];
        float n2 = [self fractal:fx * 220.0f + _time * 0.65f y:4.0f];
        float height = _h * (0.22f + n * 0.5f + n2 * 0.18f);
        float dome = (x % 19 == 0) ? 1.0f : 0.0f;
        for (int y = (int)(_h - height); y < _h * 0.6f; y++) {
            float hue = hueBase + (float)(y * 2 + x) * 1.15f + n2 * 80.0f;
            Pixel base = hsv_to_rgb(fmodf(hue, 360.0f), 0.8f, _day ? 0.96f : 0.68f);
            if (dome > 0.5f && y < _h * 0.3f) {
                base = glow;
            }
            if (!_day && (y % 3 == 0) && ((x + y) % 7 == 0)) {
                base = hsv_to_rgb(hueBase + 50.0f, 0.3f, 0.98f);
            }
            _pixels[y * _w + x] = base;
        }
    }

    // Midground layer for extra depth.
    for (int x = 0; x < _w; x++) {
        float fx = ((float)x - _w * 0.5f) / (_w * (zoom * 1.2f));
        float n = [self fractal:fx * 180.0f + _time * 0.2f y:10.0f];
        float height = _h * (0.12f + n * 0.22f);
        for (int y = (int)(_h * 0.42f - height); y < _h * 0.42f; y++) {
            if (y >= 0 && y < _h) {
                Pixel base = hsv_to_rgb(hueBase + 160.0f, 0.65f, _day ? 0.5f : 0.32f);
                _pixels[y * _w + x] = base;
            }
        }
    }

    // Galata-like tower (left third).
    int towerX = _w / 5;
    for (int y = (int)(_h * 0.18f); y < (int)(_h * 0.6f); y++) {
        for (int x = towerX - 2; x <= towerX + 2; x++) {
            if (x >= 0 && x < _w) {
                _pixels[y * _w + x] = (Pixel){140, 130, 110, 255};
            }
        }
    }
    for (int x = towerX - 4; x <= towerX + 4; x++) {
        int y = (int)(_h * 0.16f);
        if (x >= 0 && x < _w && y >= 0) {
            _pixels[y * _w + x] = glow;
        }
    }

    // Bridge silhouette across the water.
    int bridgeY = (int)(_h * 0.68f);
    for (int x = 0; x < _w; x++) {
        int arch = (int)(4 * sinf((float)x / _w * (float)M_PI));
        int y = bridgeY - arch;
        if (y >= 0 && y < _h) {
            _pixels[y * _w + x] = (Pixel){40, 48, 60, 255};
        }
    }

    // Minarets and domes.
    for (int i = 0; i < 5; i++) {
        int mx = (int)([self nextRand] % (uint32_t)_w);
        int baseY = (int)(_h * 0.32f + ([self nextRand] % 8));
        int topY = baseY - 18 - (int)([self nextRand] % 10);
        for (int y = topY; y < baseY; y++) {
            if (y >= 0 && y < _h) {
                _pixels[y * _w + mx] = (Pixel){210, 200, 180, 255};
                if (mx + 1 < _w) _pixels[y * _w + mx + 1] = (Pixel){150, 140, 120, 255};
            }
        }
        if (topY >= 1 && topY < _h) {
            _pixels[(topY - 1) * _w + mx] = glow;
        }
    }

    for (int i = 0; i < 6; i++) {
        int cx = (int)([self nextRand] % (uint32_t)_w);
        int cy = (int)(_h * 0.22f + ([self nextRand] % 10));
        int radius = 3 + (int)([self nextRand] % 5);
        for (int y = -radius; y <= radius; y++) {
            for (int x = -radius; x <= radius; x++) {
                int px = cx + x;
                int py = cy + y;
                if (px >= 0 && px < _w && py >= 0 && py < _h) {
                    if (x * x + y * y <= radius * radius) {
                        _pixels[py * _w + px] = glow;
                    }
                }
            }
        }
    }

    for (int y = (int)(_h * 0.62f); y < _h; y++) {
        int shift = (int)_scroll % _w;
        for (int x = 0; x < _w; x++) {
            int sx = (x + shift) % _w;
            Pixel p = _pixels[y * _w + sx];
            p.r = (uint8_t)fminf(p.r + 6, 255);
            p.g = (uint8_t)fminf(p.g + 12, 255);
            p.b = (uint8_t)fminf(p.b + 22, 255);
            if ((x + y) % 7 == 0) {
                p.r = (uint8_t)fminf(p.r + 10, 255);
                p.g = (uint8_t)fminf(p.g + 6, 255);
            }
            _pixels[y * _w + x] = p;
        }
    }

    // Kaleidoscopic mirroring.
    for (int y = 0; y < _h; y++) {
        for (int x = 0; x < _w / 2; x++) {
            Pixel p = _pixels[y * _w + x];
            _pixels[y * _w + (_w - 1 - x)] = p;
        }
    }
    for (int y = 0; y < _h / 2; y++) {
        for (int x = 0; x < _w; x++) {
            Pixel p = _pixels[y * _w + x];
            _pixels[(_h - 1 - y) * _w + x] = p;
        }
    }

    // Portal bloom in center
    float bloom = _portal + _beatPulse * 0.8f;
    if (bloom > 0.01f) {
        int cx = _w / 2;
        int cy = _h / 2;
        int radius = (int)(6 + bloom * 18);
        for (int y = -radius; y <= radius; y++) {
            for (int x = -radius; x <= radius; x++) {
                int px = cx + x;
                int py = cy + y;
                if (px >= 0 && px < _w && py >= 0 && py < _h) {
                    float dist = sqrtf((float)(x * x + y * y));
                    if (dist <= radius) {
                        float h = fmodf(_time * 6.0f + dist * 8.0f + _beatPulse * 120.0f, 360.0f);
                        Pixel p = hsv_to_rgb(h, 0.9f, 0.9f);
                        _pixels[py * _w + px] = p;
                    }
                }
            }
        }
    }

    // Flowing pixel-art warp (fluid drift).
    if (_scratch) {
        for (int y = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++) {
                float fx = (float)x * 0.9f + _time * 0.15f;
                float fy = (float)y * 0.9f - _time * 0.12f;
                float n = [self fractal:fx y:fy];
                float a = n * 6.28318f;
                float flow = 1.2f + _beatPulse * 1.4f;
                int sx = x + (int)roundf(cosf(a) * flow);
                int sy = y + (int)roundf(sinf(a) * flow);
                if (sx < 0) sx = 0;
                if (sx >= _w) sx = _w - 1;
                if (sy < 0) sy = 0;
                if (sy >= _h) sy = _h - 1;
                _scratch[y * _w + x] = _pixels[sy * _w + sx];
            }
        }
        Pixel *tmp = _pixels;
        _pixels = _scratch;
        _scratch = tmp;
    }

    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!_pixels) {
        return;
    }

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(ctx);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, _pixels, (size_t)_w * (size_t)_h * sizeof(Pixel), NULL);
    CGImageRef image = CGImageCreate(_w,
                                     _h,
                                     8,
                                     32,
                                     (size_t)_w * sizeof(Pixel),
                                     colorSpace,
                                     kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedLast,
                                     provider,
                                     NULL,
                                     false,
                                     kCGRenderingIntentDefault);

    // Preserve pixel aspect with integer scaling and center it.
    float scale = fminf(self.bounds.size.width / _w, self.bounds.size.height / _h);
    int intScale = (int)floorf(scale);
    if (intScale < 1) intScale = 1;
    CGSize drawSize = CGSizeMake(_w * intScale, _h * intScale);
    float dx = (self.bounds.size.width - drawSize.width) * 0.5f;
    float dy = (self.bounds.size.height - drawSize.height) * 0.5f;
    CGRect dest = CGRectMake(dx, dy, drawSize.width, drawSize.height);
    CGContextDrawImage(ctx, dest, image);

    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);

    CGContextRestoreGState(ctx);
}

@end
