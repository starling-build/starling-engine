// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_LIB_UI_DART_WRAPPER_H_
#define FLUTTER_LIB_UI_DART_WRAPPER_H_

#include "flutter/fml/memory/ref_counted.h"

#ifndef FLUTTER_NO_DART_VM
#include "third_party/tonic/dart_wrappable.h"
#endif

namespace flutter {

#ifdef FLUTTER_NO_DART_VM

// With no VM there is nothing to be wrappable BY, but the classes that derive
// from this are the shell's own plumbing as much as they are dart:ui peers --
// ImageDescriptor, SemanticsFlags, the codecs -- so the base keeps its name
// and its reference-counting half, and loses only the binding half.
// The destructor is virtual because the derived classes declare theirs
// `override` -- tonic::DartWrappable supplied that, and without it every one
// of them fails with "only virtual member functions can be marked 'override'"
// rather than with anything that mentions Dart.
template <typename T>
class RefCountedDartWrappable : public fml::RefCountedThreadSafe<T> {
 public:
  virtual ~RefCountedDartWrappable() = default;

  void RetainDartWrappableReference() const {
    fml::RefCountedThreadSafe<T>::AddRef();
  }

  void ReleaseDartWrappableReference() const {
    fml::RefCountedThreadSafe<T>::Release();
  }

  // dispose() calls this to drop the Dart peer. There is no peer.
  void ClearDartWrapper() {}
};

// tonic declares the per-class wrapper type info this expands to; with no
// bindings there is nothing to declare.
#define DEFINE_WRAPPERTYPEINFO()

#else

template <typename T>
class RefCountedDartWrappable : public fml::RefCountedThreadSafe<T>,
                                public tonic::DartWrappable {
 public:
  virtual void RetainDartWrappableReference() const override {
    fml::RefCountedThreadSafe<T>::AddRef();
  }

  virtual void ReleaseDartWrappableReference() const override {
    fml::RefCountedThreadSafe<T>::Release();
  }
};

#endif  // FLUTTER_NO_DART_VM

}  // namespace flutter

#endif  // FLUTTER_LIB_UI_DART_WRAPPER_H_
