// Copyright 2026 DolphiniOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

#import "EmulationLayoutView.h"

@implementation EmulationLayoutView

- (void)didMoveToWindow {
  [super didMoveToWindow];
  [self setNeedsUpdateConstraints];
}

- (void)safeAreaInsetsDidChange {
  [super safeAreaInsetsDidChange];
  [self setNeedsUpdateConstraints];
}

- (void)setBounds:(CGRect)bounds {
  BOOL changed = !CGRectEqualToRect(self.bounds, bounds);
  [super setBounds:bounds];
  if (changed) {
    [self setNeedsUpdateConstraints];
  }
}

- (void)setFrame:(CGRect)frame {
  BOOL changed = !CGRectEqualToRect(self.frame, frame);
  [super setFrame:frame];
  if (changed) {
    [self setNeedsUpdateConstraints];
  }
}

- (NSArray<NSValue*>*)occlusionFrames {
  NSMutableArray<NSValue*>* frames = [NSMutableArray array];
  if (@available(iOS 27.1, *)) {
    // Query the window so a navigation container cannot filter out the camera.
    UIView* source = self.window ?: self;
    for (UIViewReservedRegion* region in [source reservedRegionsOfKind:[UIViewReservedRegionKind occlusionRegionKind]]) {
      CGRect frame = [self convertRect:region.frame fromView:source];
      [frames addObject:[NSValue valueWithCGRect:frame]];
    }
  }
  return frames;
}

- (void)updateConstraints {
  [self updateRendererConstraints];
  [super updateConstraints];
}

- (void)layoutSubviews {
  // A fold can deliver new display geometry after the constraint update pass.
  // Re-read it immediately before laying out, including when the bounds stay the same.
  [self updateRendererConstraints];
  [super layoutSubviews];
}

- (void)updateRendererConstraints {
  CGFloat top = 0;
  CGFloat bottom = 0;
  CGFloat windowBottom = 0;

  if (self.window != nil) {
    // Window insets describe the display, without the in-game navigation bar.
    // A layout guide's frame can still describe the previous display during a fold.
    CGRect windowSafeFrame = UIEdgeInsetsInsetRect(self.window.bounds, self.window.safeAreaInsets);
    windowSafeFrame = [self convertRect:windowSafeFrame fromView:self.window];
    top = MAX(0, CGRectGetMinY(windowSafeFrame) - CGRectGetMinY(self.bounds));
    windowBottom = MAX(0, CGRectGetMaxY(self.bounds) - CGRectGetMaxY(windowSafeFrame));
  }

  for (NSValue* value in [self occlusionFrames]) {
    CGRect regionFrame = value.CGRectValue;
    CGRect frame = CGRectIntersection(self.bounds, regionFrame);
    if (CGRectIsNull(frame) || CGRectIsEmpty(frame)) {
      continue;
    }

    // UIKit also reports a corner-attached region containing the status/navigation
    // controls. It grows when the overlay opens, so it must not size gameplay.
    // The physical camera has its own inset region. Traditional edge cutouts are
    // already covered by the window's vertical safe-area inset above.
    BOOL touchesSide = CGRectGetMinX(regionFrame) <= CGRectGetMinX(self.bounds) ||
                       CGRectGetMaxX(regionFrame) >= CGRectGetMaxX(self.bounds);
    BOOL touchesEnd = CGRectGetMinY(regionFrame) <= CGRectGetMinY(self.bounds) ||
                      CGRectGetMaxY(regionFrame) >= CGRectGetMaxY(self.bounds);
    if (touchesSide && touchesEnd) {
      continue;
    }

    // Preserve the full width. Clear a camera at the top by moving below it;
    // after rotation, clear a camera at the bottom by ending above it instead.
    // Add breathing room around the physical cutout, independently of app bars.
    const CGFloat cameraClearance = 16;
    if (CGRectGetMidY(frame) <= CGRectGetMidY(self.bounds)) {
      top = MAX(top, CGRectGetMaxY(frame) - CGRectGetMinY(self.bounds) + cameraClearance);
    } else {
      bottom = MAX(bottom, CGRectGetMaxY(self.bounds) - CGRectGetMinY(frame) + cameraClearance);
    }
  }

  self.rendererTopConstraint.constant = top;
  self.rendererBottomConstraint.constant = -bottom;
  // Portrait gameplay occupies the upper half of the unobscured display.
  self.rendererHalfConstraint.constant = (top - MAX(bottom, windowBottom)) / 2;
}

@end
