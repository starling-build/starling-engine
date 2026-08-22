// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/embedder/embedder_external_view_embedder.h"

#include <cstdlib>

#include <cassert>
#include <utility>

#include "flutter/common/constants.h"
#include "flutter/shell/platform/embedder/embedder_layers.h"
#include "flutter/shell/platform/embedder/embedder_render_target.h"
#include "third_party/skia/include/gpu/ganesh/GrDirectContext.h"

namespace flutter {

static const auto kRootViewIdentifier = EmbedderExternalView::ViewIdentifier{};

EmbedderExternalViewEmbedder::EmbedderExternalViewEmbedder(
    bool avoid_backing_store_cache,
    const CreateRenderTargetCallback& create_render_target_callback,
    const PresentCallback& present_callback)
    : avoid_backing_store_cache_(avoid_backing_store_cache),
      partial_repaint_enabled_(std::getenv("STARLING_PARTIAL_REPAINT") !=
                               nullptr),
      create_render_target_callback_(create_render_target_callback),
      present_callback_(present_callback) {
  FML_DCHECK(create_render_target_callback_);
  FML_DCHECK(present_callback_);
}

// |ExternalViewEmbedder|
bool EmbedderExternalViewEmbedder::SupportsPartialRepaint() const {
  return partial_repaint_enabled_;
}

// |ExternalViewEmbedder|
std::optional<DlIRect> EmbedderExternalViewEmbedder::ExistingViewDamage(
    int64_t flutter_view_id) {
  // Nullopt = unknown, forces a full repaint. An EMPTY rect = the cached
  // render target holds the previous frame exactly (age-1: one target per
  // view, collected at the end of every submit and returned by the same
  // descriptor next frame), so nothing beyond the layer-tree diff is stale.
  // Valid only while the frame size matches - a resize changes the
  // descriptor and gets a fresh, uninitialized target.
  auto it = view_partial_state_.find(flutter_view_id);
  if (it == view_partial_state_.end() || !it->second.next_existing_valid ||
      it->second.frame_size != pending_frame_size_) {
    return std::nullopt;
  }
  return DlIRect();
}

EmbedderExternalViewEmbedder::~EmbedderExternalViewEmbedder() = default;

void EmbedderExternalViewEmbedder::CollectView(int64_t view_id) {
  render_target_caches_.erase(view_id);
  view_partial_state_.erase(view_id);
}

void EmbedderExternalViewEmbedder::SetSurfaceTransformationCallback(
    SurfaceTransformationCallback surface_transformation_callback) {
  surface_transformation_callback_ = std::move(surface_transformation_callback);
}

DlMatrix EmbedderExternalViewEmbedder::GetSurfaceTransformation() const {
  if (!surface_transformation_callback_) {
    return DlMatrix{};
  }

  return surface_transformation_callback_();
}

void EmbedderExternalViewEmbedder::Reset() {
  pending_views_.clear();
  composition_order_.clear();
}

// |ExternalViewEmbedder|
void EmbedderExternalViewEmbedder::CancelFrame() {
  Reset();
}

// |ExternalViewEmbedder|
void EmbedderExternalViewEmbedder::BeginFrame(
    GrDirectContext* context,
    const fml::RefPtr<fml::RasterThreadMerger>& raster_thread_merger) {}

// |ExternalViewEmbedder|
void EmbedderExternalViewEmbedder::PrepareFlutterView(
    DlISize frame_size,
    double device_pixel_ratio) {
  Reset();

  pending_frame_size_ = frame_size;
  pending_device_pixel_ratio_ = device_pixel_ratio;
  pending_surface_transformation_ = GetSurfaceTransformation();

  pending_views_[kRootViewIdentifier] = std::make_unique<EmbedderExternalView>(
      pending_frame_size_, pending_surface_transformation_);
  composition_order_.push_back(kRootViewIdentifier);
}

// |ExternalViewEmbedder|
void EmbedderExternalViewEmbedder::PrerollCompositeEmbeddedView(
    int64_t view_id,
    std::unique_ptr<EmbeddedViewParams> params) {
  auto vid = EmbedderExternalView::ViewIdentifier(view_id);
  FML_DCHECK(pending_views_.count(vid) == 0);

  pending_views_[vid] = std::make_unique<EmbedderExternalView>(
      pending_frame_size_,              // frame size
      pending_surface_transformation_,  // surface xformation
      vid,                              // view identifier
      std::move(params)                 // embedded view params
  );
  composition_order_.push_back(vid);
}

// |ExternalViewEmbedder|
DlCanvas* EmbedderExternalViewEmbedder::GetRootCanvas() {
  auto found = pending_views_.find(kRootViewIdentifier);
  if (found == pending_views_.end()) {
    FML_DLOG(WARNING)
        << "No root canvas could be found. This is extremely unlikely and "
           "indicates that the external view embedder did not receive the "
           "notification to begin the frame.";
    return nullptr;
  }
  return found->second->GetCanvas();
}

// |ExternalViewEmbedder|
DlCanvas* EmbedderExternalViewEmbedder::CompositeEmbeddedView(int64_t view_id) {
  auto vid = EmbedderExternalView::ViewIdentifier(view_id);
  auto found = pending_views_.find(vid);
  if (found == pending_views_.end()) {
    FML_DCHECK(false) << "Attempted to composite a view that was not "
                         "pre-rolled.";
    return nullptr;
  }
  return found->second->GetCanvas();
}

static FlutterBackingStoreConfig MakeBackingStoreConfig(
    int64_t view_id,
    const DlISize& backing_store_size) {
  FlutterBackingStoreConfig config = {};

  config.struct_size = sizeof(config);

  config.size.width = backing_store_size.width;
  config.size.height = backing_store_size.height;
  config.view_id = view_id;

  return config;
}

namespace {

struct PlatformView {
  EmbedderExternalView::ViewIdentifier view_identifier;
  const EmbeddedViewParams* params;

  // The frame of the platform view, after clipping, in screen coordinates.
  SkRect clipped_frame;

  explicit PlatformView(const EmbedderExternalView* view) {
    FML_DCHECK(view->HasPlatformView());
    view_identifier = view->GetViewIdentifier();
    params = view->GetEmbeddedViewParams();

    DlRect clip = view->GetEmbeddedViewParams()->finalBoundingRect();
    DlMatrix matrix;
    for (auto i = params->mutatorsStack().Begin();
         i != params->mutatorsStack().End(); ++i) {
      const auto& m = *i;
      switch (m->GetType()) {
        case MutatorType::kClipRect: {
          auto rect = m->GetRect().TransformAndClipBounds(matrix);
          clip = clip.IntersectionOrEmpty(rect);
          break;
        }
        case MutatorType::kClipRRect: {
          auto rect = m->GetRRect().GetBounds().TransformAndClipBounds(matrix);
          clip = clip.IntersectionOrEmpty(rect);
          break;
        }
        case MutatorType::kClipRSE: {
          auto rect = m->GetRSE().GetBounds().TransformAndClipBounds(matrix);
          clip = clip.IntersectionOrEmpty(rect);
          break;
        }
        case MutatorType::kClipPath: {
          auto rect = m->GetPath().GetBounds().TransformAndClipBounds(matrix);
          clip = clip.IntersectionOrEmpty(rect);
          break;
        }
        case MutatorType::kTransform: {
          matrix = matrix * m->GetMatrix();
          break;
        }
        case MutatorType::kOpacity:
        case MutatorType::kBackdropFilter:
          break;
      }
    }
    clipped_frame = ToSkRect(clip);
  }
};

/// Each layer will result in a single physical surface that contains Flutter
/// contents. It may contain multiple platform views and the slices
/// that would be otherwise rendered between these platform views will be
/// collapsed into this layer, as long as they do not intersect any of the
/// platform views.
/// In Z order the Flutter contents of Layer is above the platform views.
class Layer {
 public:
  /// Returns whether the rectangle intersects any of the platform views of
  /// this layer.
  bool IntersectsPlatformView(const SkRect& rect) {
    for (auto& platform_view : platform_views_) {
      if (platform_view.clipped_frame.intersects(rect)) {
        return true;
      }
    }
    return false;
  }

  /// Returns whether the region intersects any of the platform views of this
  /// layer.
  bool IntersectsPlatformView(const DlRegion& region) {
    for (auto& platform_view : platform_views_) {
      auto clipped_frame = ToDlIRect(platform_view.clipped_frame.roundOut());
      if (region.intersects(clipped_frame)) {
        return true;
      }
    }
    return false;
  }

  /// Returns whether the rectangle intersects any of the Flutter contents of
  /// this layer.
  bool IntersectsFlutterContents(const SkRect& rect) {
    return flutter_contents_region_.intersects(ToDlIRect(rect.roundOut()));
  }

  /// Returns whether the region intersects any of the Flutter contents of this
  /// layer.
  bool IntersectsFlutterContents(const DlRegion& region) {
    return flutter_contents_region_.intersects(region);
  }

  /// Adds a platform view to this layer.
  void AddPlatformView(const PlatformView& platform_view) {
    platform_views_.push_back(platform_view);
  }

  /// Adds Flutter contents to this layer.
  void AddFlutterContents(EmbedderExternalView* contents,
                          const DlRegion& contents_region) {
    flutter_contents_.push_back(contents);
    flutter_contents_region_ =
        DlRegion::MakeUnion(flutter_contents_region_, contents_region);
  }

  bool has_flutter_contents() const { return !flutter_contents_.empty(); }

  void SetRenderTarget(std::unique_ptr<EmbedderRenderTarget> target) {
    FML_DCHECK(render_target_ == nullptr);
    FML_DCHECK(has_flutter_contents());
    render_target_ = std::move(target);
  }

  /// Renders this layer Flutter contents to the render target previously
  /// assigned with SetRenderTarget.
  void RenderFlutterContents(
      const std::optional<DlIRect>& partial_clip = std::nullopt) {
    FML_DCHECK(has_flutter_contents());
    if (render_target_) {
      bool clear_surface = true;
      for (auto c : flutter_contents_) {
        c->Render(*render_target_, clear_surface, partial_clip);
        clear_surface = false;
      }
    }
  }

  /// Returns platform views for this layer. In Z-order the platform views are
  /// positioned *below* this layer's Flutter contents.
  const std::vector<PlatformView>& platform_views() const {
    return platform_views_;
  }

  EmbedderRenderTarget* render_target() { return render_target_.get(); }

  std::vector<DlIRect> coverage() {
    return flutter_contents_region_.getRects();
  }

 private:
  std::vector<PlatformView> platform_views_;
  std::vector<EmbedderExternalView*> flutter_contents_;
  DlRegion flutter_contents_region_;
  std::unique_ptr<EmbedderRenderTarget> render_target_;
  friend class LayerBuilder;
};

/// A layout builder is responsible for building an optimized list of Layers
/// from a list of `EmbedderExternalView`s. Single EmbedderExternalView contains
/// at most one platform view and at most one layer of Flutter contents
/// ('slice'). LayerBuilder is responsible for producing as few Layers from the
/// list of EmbedderExternalViews as possible while maintaining identical visual
/// result.
///
/// Implements https://flutter.dev/go/optimized-platform-view-layers
class LayerBuilder {
 public:
  using RenderTargetProvider =
      std::function<std::unique_ptr<EmbedderRenderTarget>(
          const DlISize& frame_size)>;

  explicit LayerBuilder(DlISize frame_size) : frame_size_(frame_size) {
    layers_.push_back(Layer());
  }

  /// Adds the platform view and/or flutter contents from the
  /// EmbedderExternalView instance.
  ///
  /// This will try to add the content and platform view to an existing layer
  /// if possible. If not, a new layer will be created.
  void AddExternalView(EmbedderExternalView* view) {
    if (view->HasPlatformView()) {
      PlatformView platform_view(view);
      AddPlatformView(platform_view);
    }
    if (view->HasEngineRenderedContents()) {
      AddFlutterContents(view);
    }
  }

  /// Prepares the render targets for all layers that have Flutter contents.
  void PrepareBackingStore(const RenderTargetProvider& target_provider) {
    for (auto& layer : layers_) {
      if (layer.has_flutter_contents()) {
        layer.SetRenderTarget(target_provider(frame_size_));
      }
    }
  }

  /// Renders all layers with Flutter contents to their respective render
  /// targets. `partial_clip` (STARLING) restricts the clear-and-replay to
  /// the frame's buffer damage - only ever set for the steady
  /// single-flutter-layer case, where the cached render target still holds
  /// the previous frame outside that region.
  void Render(const std::optional<DlIRect>& partial_clip = std::nullopt) {
    for (auto& layer : layers_) {
      if (layer.has_flutter_contents()) {
        layer.RenderFlutterContents(partial_clip);
      }
    }
  }

  /// STARLING: whether this frame's composition is exactly one Flutter
  /// layer with no platform views - the only shape partial repaint vouches
  /// for (one cached target, no slicing to change under the diff).
  bool SinglePlainFlutterLayer() const {
    return layers_.size() == 1 && layers_[0].has_flutter_contents() &&
           layers_[0].platform_views().empty();
  }

  /// Populates EmbedderLayers from layer builder's layers. `frame_damage`
  /// (STARLING) rides along to the present info so an embedder blitting to
  /// a preserved window target can copy only what changed this frame.
  void PushLayers(EmbedderLayers& layers,
                  const std::optional<DlIRect>& frame_damage = std::nullopt) {
    for (auto& layer : layers_) {
      for (auto& view : layer.platform_views()) {
        auto platform_view_id = view.view_identifier.platform_view_id;
        if (platform_view_id.has_value()) {
          layers.PushPlatformViewLayer(platform_view_id.value(), *view.params);
        }
      }
      if (layer.render_target() != nullptr) {
        layers.PushBackingStoreLayer(layer.render_target()->GetBackingStore(),
                                     layer.coverage(), frame_damage);
      }
    }
  }

  /// Removes the render targets from layers and returns them for collection.
  std::vector<std::unique_ptr<EmbedderRenderTarget>>
  ClearAndCollectRenderTargets() {
    std::vector<std::unique_ptr<EmbedderRenderTarget>> result;
    for (auto& layer : layers_) {
      if (layer.render_target() != nullptr) {
        result.push_back(std::move(layer.render_target_));
      }
    }
    layers_.clear();
    return result;
  }

 private:
  void AddPlatformView(PlatformView view) {
    GetLayerForPlatformView(view).AddPlatformView(view);
  }

  void AddFlutterContents(EmbedderExternalView* contents) {
    FML_DCHECK(contents->HasEngineRenderedContents());

    DlRegion region = contents->GetDlRegion();
    GetLayerForFlutterContentsRegion(region).AddFlutterContents(contents,
                                                                region);
  }

  /// Returns the deepest layer to which the platform view can be added. That
  /// would be (whichever comes first):
  /// - First layer from back that has platform view that intersects with this
  ///   view
  /// - Very last layer from back that has surface that doesn't intersect with
  ///   this. That is because layer content renders on top of the platform view.
  Layer& GetLayerForPlatformView(PlatformView view) {
    for (auto iter = layers_.rbegin(); iter != layers_.rend(); ++iter) {
      // This layer has surface that intersects with this view. That means we
      // went one too far and need the layer before this.
      if (iter->IntersectsFlutterContents(view.clipped_frame)) {
        if (iter == layers_.rbegin()) {
          layers_.emplace_back();
          return layers_.back();
        } else {
          --iter;
          return *iter;
        }
      }
      if (iter->IntersectsPlatformView(view.clipped_frame)) {
        return *iter;
      }
    }
    return layers_.front();
  }

  /// Finds layer to which the Flutter content can be added. That would
  /// be first layer from back that has any intersection with this region.
  Layer& GetLayerForFlutterContentsRegion(const DlRegion& region) {
    for (auto iter = layers_.rbegin(); iter != layers_.rend(); ++iter) {
      if (iter->IntersectsPlatformView(region) ||
          iter->IntersectsFlutterContents(region)) {
        return *iter;
      }
    }
    return layers_.front();
  }

  std::vector<Layer> layers_;
  DlISize frame_size_;
};

};  // namespace

void EmbedderExternalViewEmbedder::SubmitFlutterView(
    int64_t flutter_view_id,
    GrDirectContext* context,
    const std::shared_ptr<impeller::AiksContext>& aiks_context,
    std::unique_ptr<SurfaceFrame> frame) {
  // The unordered_map render_target_cache creates a new entry if the view ID is
  // unrecognized.
  EmbedderRenderTargetCache& render_target_cache =
      render_target_caches_[flutter_view_id];
  DlRect _rect = DlRect::MakeSize(pending_frame_size_)
                     .TransformAndClipBounds(pending_surface_transformation_);

  LayerBuilder builder(DlIRect::RoundOut(_rect).GetSize());

  for (auto view_id : composition_order_) {
    auto& view = pending_views_[view_id];
    builder.AddExternalView(view.get());
  }

  bool created_fresh_target = false;
  builder.PrepareBackingStore([&](const DlISize& frame_size) {
    if (!avoid_backing_store_cache_) {
      std::unique_ptr<EmbedderRenderTarget> target =
          render_target_cache.GetRenderTarget(
              EmbedderExternalView::RenderTargetDescriptor(frame_size));
      if (target != nullptr) {
        return target;
      }
    }
    created_fresh_target = true;
    auto config = MakeBackingStoreConfig(flutter_view_id, frame_size);
    return create_render_target_callback_(context, aiks_context, config);
  });

  // This is where unused render targets will be collected. Control may flow
  // to the embedder. Here, the embedder has the opportunity to trample on the
  // OpenGL context.
  //
  // For optimum performance, we should tell the render target cache to clear
  // its unused entries before allocating new ones. This collection step
  // before allocating new render targets ameliorates peak memory usage within
  // the frame. But, this causes an issue in a known internal embedder. To
  // work around this issue while that embedder migrates, collection of render
  // targets is deferred after the presentation.
  //
  // @warning: Embedder may trample on our OpenGL context here.
  auto deferred_cleanup_render_targets =
      render_target_cache.ClearAllRenderTargetsInCache();

#if !SLIMPELLER
  // The OpenGL context could have been trampled by the embedder at this point
  // as it attempted to collect old render targets and create new ones. Tell
  // Skia to not rely on existing bindings.
  if (context) {
    context->resetContext(kAll_GrBackendState);
  }
#endif  //  !SLIMPELLER

  // STARLING partial repaint: when the rasterizer produced a
  // damage-clipped recording (buffer_damage set - only possible after
  // ExistingViewDamage vouched for this view), replay it clipped onto the
  // preserved cached target. A fresh target with a clipped recording is the
  // one unexpected case (the size guard should make it impossible): render
  // it unclipped - the culled recording leaves the undamaged area
  // transparent for one frame - and say so loudly.
  std::optional<DlIRect> partial_clip;
  bool partial_requested = partial_repaint_enabled_ &&
                           frame->submit_info().buffer_damage.has_value();
  if (partial_requested) {
    if (created_fresh_target) {
      FML_LOG(ERROR) << "STARLING partial repaint: damage-clipped frame met a "
                        "fresh render target; rendering unclipped.";
    } else if (builder.SinglePlainFlutterLayer()) {
      partial_clip = frame->submit_info().buffer_damage;
    }
    if (std::getenv("STARLING_DAMAGE_LOG") != nullptr) {
      auto& d = frame->submit_info().buffer_damage;
      FML_LOG(ERROR) << "[damage] view=" << flutter_view_id
                     << " clip=" << (partial_clip.has_value() ? 1 : 0)
                     << " rect=" << (d.has_value() ? d->GetLeft() : -1) << ","
                     << (d.has_value() ? d->GetTop() : -1) << ","
                     << (d.has_value() ? d->GetRight() : -1) << ","
                     << (d.has_value() ? d->GetBottom() : -1);
    }
  }
  builder.Render(partial_clip);

#if !SLIMPELLER
  // We are going to be transferring control back over to the embedder there
  // the context may be trampled upon again. Flush all operations to the
  // underlying rendering API.
  //
  // @warning: Embedder may trample on our OpenGL context here.
  if (context) {
    context->flushAndSubmit();
  }
#endif  //  !SLIMPELLER

  {
    auto presentation_time_optional = frame->submit_info().presentation_time;
    uint64_t presentation_time =
        presentation_time_optional.has_value()
            ? presentation_time_optional->ToEpochDelta().ToNanoseconds()
            : 0;

    // Submit the scribbled layer to the embedder for presentation.
    //
    // @warning: Embedder may trample on our OpenGL context here.
    EmbedderLayers presented_layers(
        pending_frame_size_, pending_device_pixel_ratio_,
        pending_surface_transformation_, presentation_time);

    builder.PushLayers(presented_layers,
                       partial_clip.has_value()
                           ? frame->submit_info().frame_damage
                           : std::nullopt);

    presented_layers.InvokePresentCallback(flutter_view_id, present_callback_);
  }

  // See why this is necessary in the comment where this collection in
  // realized.
  //
  // @warning: Embedder may trample on our OpenGL context here.
  deferred_cleanup_render_targets.clear();

  // STARLING partial repaint bookkeeping: after this frame the cached
  // target holds it COMPLETELY when the composition was a single plain
  // Flutter layer and the render was either full or a vouched-for partial.
  // Then the target's staleness next frame is age-1: existing damage EMPTY.
  if (partial_repaint_enabled_) {
    auto& state = view_partial_state_[flutter_view_id];
    bool complete = builder.SinglePlainFlutterLayer() &&
                    !avoid_backing_store_cache_ &&
                    !(partial_requested && created_fresh_target);
    // Vouch only after TWO consecutive complete frames at one size: the
    // second frame guarantees the first's target really round-tripped the
    // cache, which closes the startup window where a vouched frame met a
    // fresh target and fell back (rendering one frame unclipped).
    if (complete && state.frame_size == pending_frame_size_) {
      state.complete_streak = state.complete_streak >= 2 ? 2
                                                         : state.complete_streak + 1;
    } else {
      state.complete_streak = complete ? 1 : 0;
    }
    state.next_existing_valid = complete && state.complete_streak >= 2;
    state.frame_size = pending_frame_size_;
  }

  auto render_targets = builder.ClearAndCollectRenderTargets();
  for (auto& render_target : render_targets) {
    if (!avoid_backing_store_cache_) {
      render_target_cache.CacheRenderTarget(std::move(render_target));
    }
  }

  frame->Submit();
}

}  // namespace flutter
