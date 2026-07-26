// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/codec_bridge.h"
#include "include/image_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/image/dl_image.h"
#include "flutter/lib/ui/painting/image_generator.h"  // nogncheck

// Skia headers for image decoding
#include "third_party/skia/include/codec/SkCodec.h"
#include "third_party/skia/include/codec/SkCodecAnimation.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPixmap.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

namespace flutter::swift_bridge {

// The codec bridge supports three creation paths:
// 1. From an ImageGenerator (passed in from outside via void* - existing path)
// 2. From encoded data via SkCodec directly (new factory method)
// 3. From raw pixel data (new factory method - single-frame only)
enum class CodecSourceType {
  kImageGenerator,
  kSkCodec,
  kRawPixels,
};

// Pimpl implementation holding actual Flutter types.
//
// This mirrors MultiFrameCodec::State for multi-frame images and
// SingleFrameCodec for single-frame images. We use an ImageGenerator
// directly to avoid depending on Dart VM types (UIDartState, task runners).
//
// Architecture:
// - For single-frame codecs: decodes on first DecodeNextFrame, caches result
// - For multi-frame codecs: tracks frame index, handles frame dependencies
struct CodecBridgeImpl {
  CodecSourceType source_type;

  // Path 1: ImageGenerator (passed from outside, already linked)
  std::shared_ptr<flutter::ImageGenerator> generator;

  // Path 2: Direct SkCodec (created by factory methods)
  std::unique_ptr<SkCodec> sk_codec;

  // Path 3: Raw pixel data
  sk_sp<SkData> raw_data;
  SkImageInfo raw_info;
  size_t raw_row_bytes;

  int frame_count;
  int repetition_count;
  int next_frame_index;
  bool disposed;

  // Multi-frame state (mirrors MultiFrameCodec::State)
  std::optional<SkBitmap> last_required_frame;
  int last_required_frame_index;
  std::optional<SkIRect> restore_bg_color_rect;

  // Last decoded frame results
  ImageBridge* last_frame_image;
  int last_frame_duration_ms;
  std::string last_error;

  // Constructor for ImageGenerator path
  CodecBridgeImpl(std::shared_ptr<flutter::ImageGenerator> gen)
      : source_type(CodecSourceType::kImageGenerator),
        generator(std::move(gen)),
        raw_row_bytes(0),
        frame_count(0),
        repetition_count(0),
        next_frame_index(0),
        disposed(false),
        last_required_frame_index(-1),
        last_frame_image(nullptr),
        last_frame_duration_ms(0) {
    if (generator) {
      frame_count = generator->GetFrameCount();
      unsigned int play_count = generator->GetPlayCount();
      // Match MultiFrameCodec behavior: -1 for infinite, otherwise play_count -
      // 1
      if (play_count == flutter::ImageGenerator::kInfinitePlayCount) {
        repetition_count = -1;
      } else {
        repetition_count = static_cast<int>(play_count) - 1;
      }
    }
  }

  // Constructor for SkCodec path
  CodecBridgeImpl(std::unique_ptr<SkCodec> codec)
      : source_type(CodecSourceType::kSkCodec),
        sk_codec(std::move(codec)),
        raw_row_bytes(0),
        frame_count(0),
        repetition_count(0),
        next_frame_index(0),
        disposed(false),
        last_required_frame_index(-1),
        last_frame_image(nullptr),
        last_frame_duration_ms(0) {
    if (sk_codec) {
      frame_count = sk_codec->getFrameCount();
      if (frame_count == 0) {
        // Non-animated images report 0 frames from getFrameCount but have 1
        // decodable frame
        frame_count = 1;
      }
      int rep = sk_codec->getRepetitionCount();
      // SkCodec: kRepetitionCountInfinite = -1, 0 = play once (no repeat)
      // Our API: -1 = infinite, 0 = play once
      repetition_count = rep;
    }
  }

  // Constructor for raw pixel path
  CodecBridgeImpl(sk_sp<SkData> data, SkImageInfo info, size_t row_bytes)
      : source_type(CodecSourceType::kRawPixels),
        raw_data(std::move(data)),
        raw_info(std::move(info)),
        raw_row_bytes(row_bytes),
        frame_count(1),
        repetition_count(0),
        next_frame_index(0),
        disposed(false),
        last_required_frame_index(-1),
        last_frame_image(nullptr),
        last_frame_duration_ms(0) {}

  ~CodecBridgeImpl() {
    // Release the last frame image if we own it
    if (last_frame_image) {
      last_frame_image->Release();
      last_frame_image = nullptr;
    }
  }
};

CodecBridge::CodecBridge(void* generator_ptr) {
  // The generator_ptr is a raw pointer to a heap-allocated
  // shared_ptr<ImageGenerator>. We take ownership and store it.
  auto* shared_gen =
      static_cast<std::shared_ptr<flutter::ImageGenerator>*>(generator_ptr);
  impl_ = new CodecBridgeImpl(std::move(*shared_gen));
  delete shared_gen;
}

CodecBridge* CodecBridge::CreateFromEncodedData(const void* sk_data_ptr) {
  if (!sk_data_ptr) {
    return nullptr;
  }
  sk_sp<SkData> data = *static_cast<const sk_sp<SkData>*>(sk_data_ptr);
  if (!data) {
    return nullptr;
  }

  // Use Skia's SkCodec directly to decode encoded image data.
  // This avoids depending on flutter::ImageGenerator (in //flutter/lib/ui).
  auto codec = SkCodec::MakeFromData(data);
  if (!codec) {
    return nullptr;
  }

  auto* bridge = new CodecBridge();
  bridge->impl_ = new CodecBridgeImpl(std::move(codec));
  return bridge;
}

CodecBridge* CodecBridge::CreateFromRawPixels(const void* sk_data_ptr,
                                              int width,
                                              int height,
                                              int row_bytes,
                                              int color_type,
                                              int alpha_type) {
  if (!sk_data_ptr) {
    return nullptr;
  }
  sk_sp<SkData> data = *static_cast<const sk_sp<SkData>*>(sk_data_ptr);
  if (!data) {
    return nullptr;
  }

  auto info = SkImageInfo::Make(width, height,
                                static_cast<SkColorType>(color_type),
                                static_cast<SkAlphaType>(alpha_type));

  auto* bridge = new CodecBridge();
  bridge->impl_ = new CodecBridgeImpl(std::move(data), info,
                                       static_cast<size_t>(row_bytes));
  return bridge;
}

CodecBridge::~CodecBridge() {
  delete impl_;
}

int CodecBridge::GetFrameCount() const {
  if (!impl_ || impl_->disposed) {
    return 0;
  }
  return impl_->frame_count;
}

int CodecBridge::GetRepetitionCount() const {
  if (!impl_ || impl_->disposed) {
    return 0;
  }
  return impl_->repetition_count;
}

// Helper to create an ImageBridge from an SkBitmap
static ImageBridge* CreateImageBridgeFromBitmap(SkBitmap& bitmap) {
  bitmap.setImmutable();
  sk_sp<SkImage> sk_image = SkImages::RasterFromBitmap(bitmap);
  if (!sk_image) {
    return nullptr;
  }

  sk_sp<DlImage> dl_image = DlImage::Make(std::move(sk_image));
  if (!dl_image) {
    return nullptr;
  }

  auto* image_bridge = new ImageBridge();
  auto* dl_image_heap = new sk_sp<DlImage>(std::move(dl_image));
  image_bridge->SetDlImageFromPtr(static_cast<void*>(dl_image_heap));
  return image_bridge;
}

// Decode using the ImageGenerator path (existing behavior)
static bool DecodeNextFrameGenerator(CodecBridgeImpl* impl) {
  SkBitmap bitmap;
  SkImageInfo info =
      impl->generator->GetInfo().makeColorType(kN32_SkColorType);
  if (info.alphaType() == kUnpremul_SkAlphaType) {
    info = info.makeAlphaType(kPremul_SkAlphaType);
  }

  if (!bitmap.tryAllocPixels(info)) {
    impl->last_error = "Failed to allocate memory for bitmap";
    return false;
  }

  int current_frame = impl->next_frame_index;

  if (impl->frame_count > 1) {
    flutter::ImageGenerator::FrameInfo frame_info =
        impl->generator->GetFrameInfo(current_frame);

    const int required_frame_index =
        frame_info.required_frame.value_or(-1);

    if (required_frame_index != -1) {
      if (impl->last_required_frame.has_value()) {
        bitmap.writePixels(impl->last_required_frame->pixmap());
        if (impl->restore_bg_color_rect.has_value()) {
          bitmap.erase(SK_ColorTRANSPARENT,
                       impl->restore_bg_color_rect.value());
        }
      }
    }

    if (!impl->generator->GetPixels(info, bitmap.getPixels(),
                                     bitmap.rowBytes(), current_frame,
                                     required_frame_index)) {
      impl->last_error = "Could not decode frame pixels";
      return false;
    }

    const bool keep_current =
        frame_info.disposal_method == SkCodecAnimation::DisposalMethod::kKeep;
    const bool restore_previous =
        frame_info.disposal_method ==
        SkCodecAnimation::DisposalMethod::kRestorePrevious;
    const bool previous_available = impl->last_required_frame.has_value();

    if (keep_current || (previous_available && !restore_previous)) {
      impl->last_required_frame = bitmap;
      impl->last_required_frame_index = current_frame;
    }

    if (frame_info.disposal_method ==
        SkCodecAnimation::DisposalMethod::kRestoreBGColor) {
      impl->restore_bg_color_rect = frame_info.disposal_rect;
    } else {
      impl->restore_bg_color_rect.reset();
    }

    impl->last_frame_duration_ms = frame_info.duration;
  } else {
    if (!impl->generator->GetPixels(info, bitmap.getPixels(),
                                     bitmap.rowBytes())) {
      impl->last_error = "Could not decode image pixels";
      return false;
    }
    impl->last_frame_duration_ms = 0;
  }

  impl->next_frame_index = (current_frame + 1) % impl->frame_count;

  auto* image_bridge = CreateImageBridgeFromBitmap(bitmap);
  if (!image_bridge) {
    impl->last_error = "Failed to create image from decoded bitmap";
    return false;
  }
  impl->last_frame_image = image_bridge;
  return true;
}

// Decode using the SkCodec path (new factory method)
static bool DecodeNextFrameSkCodec(CodecBridgeImpl* impl) {
  SkImageInfo info = impl->sk_codec->getInfo().makeColorType(kN32_SkColorType);
  if (info.alphaType() == kUnpremul_SkAlphaType) {
    info = info.makeAlphaType(kPremul_SkAlphaType);
  }

  SkBitmap bitmap;
  if (!bitmap.tryAllocPixels(info)) {
    impl->last_error = "Failed to allocate memory for bitmap";
    return false;
  }

  int current_frame = impl->next_frame_index;

  // Check if this is a multi-frame image (animated)
  int actual_frame_count = impl->sk_codec->getFrameCount();
  if (actual_frame_count > 1) {
    SkCodec::FrameInfo frame_info;
    if (!impl->sk_codec->getFrameInfo(current_frame, &frame_info)) {
      impl->last_error = "Could not get frame info";
      return false;
    }

    const int required_frame_index = frame_info.fRequiredFrame;

    if (required_frame_index != SkCodec::kNoFrame) {
      if (impl->last_required_frame.has_value()) {
        bitmap.writePixels(impl->last_required_frame->pixmap());
        if (impl->restore_bg_color_rect.has_value()) {
          bitmap.erase(SK_ColorTRANSPARENT,
                       impl->restore_bg_color_rect.value());
        }
      }
    }

    SkCodec::Options opts;
    opts.fFrameIndex = current_frame;
    opts.fPriorFrame = (required_frame_index != SkCodec::kNoFrame &&
                        impl->last_required_frame.has_value())
                           ? impl->last_required_frame_index
                           : SkCodec::kNoFrame;

    SkCodec::Result result =
        impl->sk_codec->getPixels(info, bitmap.getPixels(),
                                   bitmap.rowBytes(), &opts);
    if (result != SkCodec::kSuccess &&
        result != SkCodec::kIncompleteInput) {
      impl->last_error = "Could not decode frame pixels";
      return false;
    }

    // Update frame tracking state (mirrors MultiFrameCodec::State)
    const bool keep_current =
        frame_info.fDisposalMethod == SkCodecAnimation::DisposalMethod::kKeep;
    const bool restore_previous =
        frame_info.fDisposalMethod ==
        SkCodecAnimation::DisposalMethod::kRestorePrevious;
    const bool previous_available = impl->last_required_frame.has_value();

    if (keep_current || (previous_available && !restore_previous)) {
      impl->last_required_frame = bitmap;
      impl->last_required_frame_index = current_frame;
    }

    if (frame_info.fDisposalMethod ==
        SkCodecAnimation::DisposalMethod::kRestoreBGColor) {
      impl->restore_bg_color_rect = frame_info.fFrameRect;
    } else {
      impl->restore_bg_color_rect.reset();
    }

    impl->last_frame_duration_ms = frame_info.fDuration;
  } else {
    // Single-frame: just decode the first frame
    SkCodec::Result result =
        impl->sk_codec->getPixels(info, bitmap.getPixels(),
                                   bitmap.rowBytes());
    if (result != SkCodec::kSuccess &&
        result != SkCodec::kIncompleteInput) {
      impl->last_error = "Could not decode image pixels";
      return false;
    }
    impl->last_frame_duration_ms = 0;
  }

  impl->next_frame_index = (current_frame + 1) % impl->frame_count;

  auto* image_bridge = CreateImageBridgeFromBitmap(bitmap);
  if (!image_bridge) {
    impl->last_error = "Failed to create image from decoded bitmap";
    return false;
  }
  impl->last_frame_image = image_bridge;
  return true;
}

// Decode using the raw pixel path
static bool DecodeNextFrameRawPixels(CodecBridgeImpl* impl) {
  // Convert raw pixels to the target format
  SkImageInfo dst_info =
      impl->raw_info.makeColorType(kN32_SkColorType);
  if (dst_info.alphaType() == kUnpremul_SkAlphaType) {
    dst_info = dst_info.makeAlphaType(kPremul_SkAlphaType);
  }

  SkBitmap bitmap;
  if (!bitmap.tryAllocPixels(dst_info)) {
    impl->last_error = "Failed to allocate memory for bitmap";
    return false;
  }

  SkPixmap src(impl->raw_info, impl->raw_data->data(), impl->raw_row_bytes);
  SkPixmap dst(dst_info, bitmap.getPixels(), bitmap.rowBytes());
  if (!src.readPixels(dst)) {
    impl->last_error = "Could not decode raw pixel data";
    return false;
  }

  impl->last_frame_duration_ms = 0;
  impl->next_frame_index = 0;  // Always wraps back to 0 for single-frame

  auto* image_bridge = CreateImageBridgeFromBitmap(bitmap);
  if (!image_bridge) {
    impl->last_error = "Failed to create image from raw pixel data";
    return false;
  }
  impl->last_frame_image = image_bridge;
  return true;
}

bool CodecBridge::DecodeNextFrame() {
  if (!impl_ || impl_->disposed) {
    if (impl_) {
      impl_->last_error = "Codec has been disposed";
    }
    return false;
  }

  if (impl_->frame_count == 0) {
    impl_->last_error = "Could not provide any frame.";
    return false;
  }

  // Release previous frame image
  if (impl_->last_frame_image) {
    impl_->last_frame_image->Release();
    impl_->last_frame_image = nullptr;
  }
  impl_->last_error.clear();

  switch (impl_->source_type) {
    case CodecSourceType::kImageGenerator:
      if (!impl_->generator) {
        impl_->last_error = "Codec has no image generator";
        return false;
      }
      return DecodeNextFrameGenerator(impl_);

    case CodecSourceType::kSkCodec:
      if (!impl_->sk_codec) {
        impl_->last_error = "Codec has no SkCodec";
        return false;
      }
      return DecodeNextFrameSkCodec(impl_);

    case CodecSourceType::kRawPixels:
      if (!impl_->raw_data) {
        impl_->last_error = "Codec has no raw pixel data";
        return false;
      }
      return DecodeNextFrameRawPixels(impl_);
  }

  impl_->last_error = "Unknown codec source type";
  return false;
}

ImageBridge* CodecBridge::GetLastFrameImage() const {
  if (!impl_) {
    return nullptr;
  }
  if (impl_->last_frame_image) {
    // Retain for the caller - Swift's SWIFT_SHARED_REFERENCE will
    // release when done
    impl_->last_frame_image->Retain();
  }
  return impl_->last_frame_image;
}

int CodecBridge::GetLastFrameDurationMs() const {
  if (!impl_) {
    return 0;
  }
  return impl_->last_frame_duration_ms;
}

void CodecBridge::GetLastError(char* buffer, int buffer_size) const {
  if (!impl_ || buffer_size <= 0) {
    if (buffer_size > 0) {
      buffer[0] = '\0';
    }
    return;
  }
  std::snprintf(buffer, buffer_size, "%s", impl_->last_error.c_str());
}

void CodecBridge::Dispose() {
  if (impl_) {
    impl_->disposed = true;
    impl_->generator.reset();
    impl_->sk_codec.reset();
    impl_->raw_data.reset();
    impl_->last_required_frame.reset();
    impl_->restore_bg_color_rect.reset();
    if (impl_->last_frame_image) {
      impl_->last_frame_image->Release();
      impl_->last_frame_image = nullptr;
    }
  }
}

void CodecBridge::ToString(char* buffer, int buffer_size) const {
  if (!impl_ || buffer_size <= 0) {
    if (buffer_size > 0) {
      buffer[0] = '\0';
    }
    return;
  }

  if (impl_->frame_count > 0) {
    std::snprintf(buffer, buffer_size, "Codec(%d frames)", impl_->frame_count);
  } else {
    std::snprintf(buffer, buffer_size, "Codec()");
  }
}

}  // namespace flutter::swift_bridge
