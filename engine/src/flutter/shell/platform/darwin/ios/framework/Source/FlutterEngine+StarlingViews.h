// STARLING: views beyond the implicit one on an iOS engine.
//
// Upstream's iOS embedder drives exactly one view (the FlutterViewController's)
// and one surface. This adds the pieces for several: each extra view draws into
// a CAMetalLayer of its own (see IOSSurfaceMetalImpeller::RegisterLayerForView),
// is announced to the framework with AddView and sized with SetViewportMetrics.
// Kept in our own files so upstream merges stay cheap.

#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#import "flutter/shell/platform/darwin/ios/framework/Headers/FlutterEngine.h"

NS_ASSUME_NONNULL_BEGIN

@interface FlutterEngine (StarlingViews)

/// Adds view `viewId` (not 0, which is the implicit view), drawn into `layer`,
/// `size` points big at `pixelRatio`. `done` gets whether the framework took it
/// (on an arbitrary thread).
- (void)starlingAddViewWithId:(int64_t)viewId
                        layer:(CAMetalLayer*)layer
                         size:(CGSize)size
                   pixelRatio:(double)pixelRatio
                         done:(void (^_Nullable)(BOOL added))done;

/// A new size, in points, for view `viewId`.
- (void)starlingResizeViewWithId:(int64_t)viewId size:(CGSize)size pixelRatio:(double)pixelRatio;

/// UIKit touches that landed on the UIView of view `viewId`, sent to it as pointer events (the view
/// controller does this for view 0). `view` is the UIView the touches' locations are taken in. The
/// device of a touch is the UITouch itself, so two windows touched at once cannot collide.
- (void)starlingDispatchTouches:(NSSet<UITouch*>*)touches
                         inView:(UIView*)view
                         viewId:(int64_t)viewId;

/// Removes view `viewId` and forgets its layer; `done` once the shell has finished with it.
- (void)starlingRemoveViewWithId:(int64_t)viewId done:(void (^_Nullable)(BOOL removed))done;

@end

NS_ASSUME_NONNULL_END
