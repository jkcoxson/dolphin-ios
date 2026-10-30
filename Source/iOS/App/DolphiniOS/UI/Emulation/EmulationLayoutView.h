// Copyright 2026 DolphiniOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

// Keeps the game viewport independent of the navigation bar's safe-area insets.
@interface EmulationLayoutView : UIView

@property (weak, nonatomic) IBOutlet NSLayoutConstraint* rendererTopConstraint;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint* rendererBottomConstraint;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint* rendererHalfConstraint;

@end

NS_ASSUME_NONNULL_END
