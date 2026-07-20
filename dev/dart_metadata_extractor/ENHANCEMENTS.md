# Dart Metadata Extractor - Missing Features & Enhancements

This document tracks known limitations and potential enhancements for the metadata extractor tool.

## Current Limitations

### 1. Prefix-Qualified Function Calls (HIGH PRIORITY)

**Status:** ✅ IMPLEMENTED

**Problem:** Calls like `math.cos()`, `math.sin()`, `math.sqrt()`, `math.atan2()`, `math.min()`, `math.max()` are not captured as external dependencies.

**Why:** The `_BodyReferenceVisitor` captures `MethodInvocation` nodes, but prefix-qualified top-level function calls (e.g., `math.cos(x)`) are different from method calls on objects (e.g., `obj.method()`).

**Example not captured:**
```dart
// geometry.dart line 126
return Offset(distance * math.cos(direction), distance * math.sin(direction));
```

**Solution:** Add a visitor for `PrefixedIdentifier` when the target is an import prefix, and handle function invocations where the target is a `PrefixedIdentifier`.

---

### 2. Unqualified Top-Level Function Calls (MEDIUM PRIORITY)

**Status:** ✅ IMPLEMENTED

**Problem:** Calls to `_lerpDouble()` and `clampDouble()` (internal dart:ui functions from `lerp.dart` and `math.dart`) are not captured.

**Why:** These are `SimpleIdentifier` function calls without a prefix or target type.

**Example not captured:**
```dart
// geometry.dart line 325
return Offset(_lerpDouble(a.dx, b.dx, t), _lerpDouble(a.dy, b.dy, t));
```

**Solution:** Add visitor for `FunctionExpressionInvocation` and `MethodInvocation` where target is null (indicating a top-level function call).

---

### 3. Constructor Body References (MEDIUM PRIORITY)

**Status:** ✅ IMPLEMENTED

**Problem:** Factory constructors and regular constructors don't have body reference extraction.

**Why:** `_extractConstructors()` doesn't call `_extractBodyReferences()` on constructor bodies.

**Example not captured:**
```dart
// Offset.fromDirection factory constructor at line 120
factory Offset.fromDirection(double direction, [double distance = 1.0]) {
  return Offset(distance * math.cos(direction), distance * math.sin(direction));
}
```

**Solution:** Add `bodyReferences` field to `ConstructorDecl` and extract from `member.body` in `_extractConstructors()`.

---

### 4. Getter/Accessor Body References (MEDIUM PRIORITY)

**Status:** ✅ IMPLEMENTED

**Problem:** Getters like `distance` and `direction` use `math.sqrt` and `math.atan2` but accessors don't have body reference extraction.

**Why:** `_extractAccessors()` doesn't call `_extractBodyReferences()`.

**Example not captured:**
```dart
// Offset.distance getter at line 143
double get distance => math.sqrt(dx * dx + dy * dy);

// Offset.direction getter at line 177
double get direction => math.atan2(dy, dx);
```

**Solution:** Add `bodyReferences` field to `AccessorDecl` and extract from `member.body` in `_extractAccessors()`.

---

### 5. Static Property Access (LOW PRIORITY)

**Status:** Partially captured

**Problem:** Access to static properties like `double.infinity`, `double.nan`, `double.maxFinite` may not be fully captured with type resolution.

**Example:**
```dart
bool get isInfinite => _dx >= double.infinity || _dy >= double.infinity;
```

**Current behavior:** These show up as property accesses on `double` type, which is captured.

---

### 6. Operator Overload Usage (LOW PRIORITY)

**Status:** Not captured as method calls

**Problem:** When operators like `+`, `-`, `*` are used, they're operator invocations but not captured as method calls.

**Example:**
```dart
Offset operator +(Offset other) => Offset(dx + other.dx, dy + other.dy);
```

**Note:** The operator definitions ARE captured (with `isOperator: true`), but usages of operators in bodies are not tracked.

---

## Enhancement Roadmap

### Phase 1: Critical for Migration Plans
- [x] Prefix-qualified function calls (`math.cos`, etc.) ✅ DONE
- [x] Constructor body references ✅ DONE

### Phase 2: Comprehensive Analysis
- [x] Getter/accessor body references ✅ DONE
- [x] Unqualified top-level function calls ✅ DONE

### Phase 3: Complete Coverage
- [ ] Operator usage tracking
- [ ] Closure/lambda body analysis (currently shallow)

---

## Implementation Notes

### For Prefix-Qualified Function Calls

The key is to handle `MethodInvocation` nodes where the target is a `PrefixedIdentifier` or `SimpleIdentifier` that refers to an import prefix.

```dart
@override
void visitMethodInvocation(MethodInvocation node) {
  // Check if this is a prefix-qualified call like math.cos()
  if (node.target is SimpleIdentifier) {
    final target = node.target as SimpleIdentifier;
    final element = target.staticElement;
    if (element is PrefixElement) {
      // This is a prefix-qualified function call
      final function = node.methodName.staticElement;
      if (function != null && function.library != null) {
        refs.functionCalls.add(FunctionCallRef(
          prefix: target.name,
          functionName: node.methodName.name,
          fullyQualifiedName: '${function.library!.source.uri}#${function.name}',
        ));
      }
    }
  }
  super.visitMethodInvocation(node);
}
```

### For Constructor Body References

```dart
// In _extractConstructors():
constructors.add(ConstructorDecl(
  // ... existing fields ...
  bodyReferences: analyzeBodies ? _extractBodyReferences(member.body) : null,
));
```

### For Accessor Body References

```dart
// In _extractAccessors():
accessors.add(AccessorDecl(
  // ... existing fields ...
  bodyReferences: analyzeBodies ? _extractBodyReferences(member.body) : null,
));
```

---

## Testing Checklist

When implementing enhancements, verify against `geometry.dart`:

- [x] `math.cos` (line 126) - captured as dart:math#cos ✅
- [x] `math.sin` (line 126) - captured as dart:math#sin ✅
- [x] `math.sqrt` (line 143) - captured as dart:math#sqrt ✅
- [x] `math.atan2` (line 177) - captured as dart:math#atan2 ✅
- [x] `math.min` (lines 492, 675-676, etc.) - captured as dart:math#min ✅
- [x] `math.max` (lines 493, 677-678, etc.) - captured as dart:math#max ✅
- [x] `_lerpDouble` (lines 325, 599, etc.) - captured as internal function ✅
- [x] `clampDouble` (lines 971-972, 983-984) - captured as internal function ✅
- [x] `Offset.fromDirection` body refs - math.cos, math.sin captured ✅
- [x] `Offset.distance` getter body refs - math.sqrt captured ✅
- [x] `Offset.direction` getter body refs - math.atan2 captured ✅
