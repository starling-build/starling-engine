// STARLING: see FlutterEngine+StarlingViews.h.

#import "flutter/shell/platform/darwin/ios/framework/Source/FlutterEngine+StarlingViews.h"

#include "flutter/common/constants.h"
#include "flutter/lib/ui/window/viewport_metrics.h"
#import "flutter/shell/platform/darwin/ios/framework/Source/FlutterEngine_Internal.h"
#include "flutter/shell/platform/darwin/ios/ios_surface_metal_impeller.h"
#include "flutter/shell/platform/darwin/ios/platform_view_ios.h"

FLUTTER_ASSERT_ARC

// (Defined in FlutterEngine.mm, declared nowhere a second file can see.)
@interface FlutterEngine (StarlingPrivate)
- (flutter::PlatformViewIOS*)platformView;
@end

static flutter::ViewportMetrics MetricsFor(CGSize size, double pixelRatio, double insetBottom = 0) {
  flutter::ViewportMetrics m;
  m.device_pixel_ratio = pixelRatio;
  m.physical_width = size.width * pixelRatio;
  m.physical_height = size.height * pixelRatio;
  m.physical_min_width_constraint = m.physical_width;
  m.physical_max_width_constraint = m.physical_width;
  m.physical_min_height_constraint = m.physical_height;
  m.physical_max_height_constraint = m.physical_height;
  m.physical_view_inset_bottom = insetBottom * pixelRatio;
  return m;
}

@implementation FlutterEngine (StarlingViews)

- (void)starlingAddViewWithId:(int64_t)viewId
                        layer:(CAMetalLayer*)layer
                         size:(CGSize)size
                   pixelRatio:(double)pixelRatio
                         done:(void (^)(BOOL))done {
  flutter::PlatformViewIOS* platformView = [self platformView];
  if (!platformView || viewId == flutter::kFlutterImplicitViewId) {
    if (done) done(NO);
    return;
  }
  // The layer first: the first frame of the view may be drawn as soon as it is added.
  flutter::IOSSurfaceMetalImpeller::RegisterLayerForView(viewId, layer);
  platformView->AddView(viewId, MetricsFor(size, pixelRatio), [done, viewId](bool added) {
    if (!added) {
      flutter::IOSSurfaceMetalImpeller::UnregisterLayerForView(viewId);
    }
    if (done) done(added);
  });
}

- (void)starlingResizeViewWithId:(int64_t)viewId size:(CGSize)size pixelRatio:(double)pixelRatio {
  [self starlingResizeViewWithId:viewId size:size pixelRatio:pixelRatio insetBottom:0];
}

- (void)starlingResizeViewWithId:(int64_t)viewId
                            size:(CGSize)size
                      pixelRatio:(double)pixelRatio
                     insetBottom:(double)insetBottom {
  flutter::PlatformViewIOS* platformView = [self platformView];
  if (platformView && viewId != flutter::kFlutterImplicitViewId) {
    platformView->SetViewportMetrics(viewId, MetricsFor(size, pixelRatio, insetBottom));
  }
}

- (void)starlingDispatchTouches:(NSSet<UITouch*>*)touches
                         inView:(UIView*)view
                         viewId:(int64_t)viewId {
  size_t removes = 0;
  for (UITouch* touch in touches) {
    if (touch.phase == UITouchPhaseEnded || touch.phase == UITouchPhaseCancelled) removes++;
  }
  const CGFloat scale = view.window.screen.scale ?: UIScreen.mainScreen.scale;
  auto packet = std::make_unique<flutter::PointerDataPacket>(touches.count + removes);
  size_t index = 0;
  for (UITouch* touch in touches) {
    CGPoint at = [touch locationInView:view];
    flutter::PointerData data;
    data.Clear();
    data.time_stamp = touch.timestamp * 1000 * 1000;
    switch (touch.phase) {
      case UITouchPhaseBegan: data.change = flutter::PointerData::Change::kDown; break;
      case UITouchPhaseMoved:
      case UITouchPhaseStationary: data.change = flutter::PointerData::Change::kMove; break;
      case UITouchPhaseEnded: data.change = flutter::PointerData::Change::kUp; break;
      default: data.change = flutter::PointerData::Change::kCancel; break;
    }
    switch (touch.type) {
      case UITouchTypeStylus: data.kind = flutter::PointerData::DeviceKind::kStylus; break;
      case UITouchTypeIndirectPointer: data.kind = flutter::PointerData::DeviceKind::kMouse; break;
      default: data.kind = flutter::PointerData::DeviceKind::kTouch; break;
    }
    data.device = reinterpret_cast<int64_t>(touch);
    data.view_id = viewId;
    data.pointer_identifier = 0;  // made by the packet converter
    data.physical_x = at.x * scale;
    data.physical_y = at.y * scale;
    data.pressure = touch.force;
    data.pressure_max = touch.maximumPossibleForce;
    data.radius_major = touch.majorRadius;
    data.radius_min = touch.majorRadius - touch.majorRadiusTolerance;
    data.radius_max = touch.majorRadius + touch.majorRadiusTolerance;
    packet->SetPointerData(index++, data);
    if (touch.phase == UITouchPhaseEnded || touch.phase == UITouchPhaseCancelled) {
      flutter::PointerData remove = data;
      remove.change = flutter::PointerData::Change::kRemove;
      packet->SetPointerData(index++, remove);
    }
  }
  [self dispatchPointerDataPacket:std::move(packet)];
}

- (void)starlingRemoveViewWithId:(int64_t)viewId done:(void (^)(BOOL))done {
  flutter::PlatformViewIOS* platformView = [self platformView];
  if (!platformView || viewId == flutter::kFlutterImplicitViewId) {
    if (done) done(NO);
    return;
  }
  platformView->RemoveView(viewId, [done, viewId](bool removed) {
    flutter::IOSSurfaceMetalImpeller::UnregisterLayerForView(viewId);
    if (done) done(removed);
  });
}


@end
