// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Data models for Dart file metadata extraction.
///
/// These models represent the structure of a Dart source file and can be
/// serialized to JSON for AI consumption.

/// Represents a type reference with optional generics and nullability.
class TypeRef {
  TypeRef({
    required this.name,
    this.fullyQualifiedName,
    this.isNullable = false,
    this.typeArguments = const [],
  });

  final String name;
  final String? fullyQualifiedName;
  final bool isNullable;
  final List<TypeRef> typeArguments;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (fullyQualifiedName != null) 'fullyQualifiedName': fullyQualifiedName,
    'isNullable': isNullable,
    if (typeArguments.isNotEmpty)
      'typeArguments': typeArguments.map((t) => t.toJson()).toList(),
  };

  @override
  String toString() {
    final args = typeArguments.isNotEmpty
        ? '<${typeArguments.join(', ')}>'
        : '';
    final nullable = isNullable ? '?' : '';
    return '$name$args$nullable';
  }
}

/// Represents a generic type parameter with optional bound.
class TypeParameter {
  TypeParameter({
    required this.name,
    this.bound,
  });

  final String name;
  final TypeRef? bound;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (bound != null) 'bound': bound!.toJson(),
  };
}

/// Represents a function/method parameter.
class Parameter {
  Parameter({
    required this.name,
    required this.type,
    this.isRequired = true,
    this.isNamed = false,
    this.isPositional = true,
    this.defaultValue,
    this.annotations = const [],
  });

  final String name;
  final TypeRef type;
  final bool isRequired;
  final bool isNamed;
  final bool isPositional;
  final String? defaultValue;
  final List<String> annotations;

  Map<String, dynamic> toJson() => {
    'name': name,
    'type': type.toJson(),
    'isRequired': isRequired,
    'isNamed': isNamed,
    'isPositional': isPositional,
    if (defaultValue != null) 'defaultValue': defaultValue,
    if (annotations.isNotEmpty) 'annotations': annotations,
  };
}

/// Represents an annotation on a declaration.
class AnnotationInfo {
  AnnotationInfo({
    required this.name,
    this.arguments,
  });

  final String name;
  final String? arguments;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (arguments != null) 'arguments': arguments,
  };
}

/// Represents a source location with optional end line.
class SourceLocation {
  SourceLocation({
    required this.line,
    required this.column,
    required this.offset,
    this.endLine,
  });

  final int line;
  final int column;
  final int offset;
  final int? endLine;

  Map<String, dynamic> toJson() => {
    'line': line,
    'column': column,
    'offset': offset,
    if (endLine != null) 'endLine': endLine,
  };
}

/// Represents an import directive.
class ImportInfo {
  ImportInfo({
    required this.uri,
    this.prefix,
    this.showNames = const [],
    this.hideNames = const [],
    this.isDeferred = false,
  });

  final String uri;
  final String? prefix;
  final List<String> showNames;
  final List<String> hideNames;
  final bool isDeferred;

  Map<String, dynamic> toJson() => {
    'uri': uri,
    if (prefix != null) 'prefix': prefix,
    if (showNames.isNotEmpty) 'showNames': showNames,
    if (hideNames.isNotEmpty) 'hideNames': hideNames,
    if (isDeferred) 'isDeferred': isDeferred,
  };
}

/// Represents an export directive.
class ExportInfo {
  ExportInfo({
    required this.uri,
    this.showNames = const [],
    this.hideNames = const [],
  });

  final String uri;
  final List<String> showNames;
  final List<String> hideNames;

  Map<String, dynamic> toJson() => {
    'uri': uri,
    if (showNames.isNotEmpty) 'showNames': showNames,
    if (hideNames.isNotEmpty) 'hideNames': hideNames,
  };
}

/// Represents library information.
class LibraryInfo {
  LibraryInfo({
    this.name,
    this.uri,
  });

  String? name;
  String? uri;

  Map<String, dynamic> toJson() => {
    if (name != null) 'name': name,
    if (uri != null) 'uri': uri,
  };
}

/// References found within a function/method body.
class BodyReferences {
  BodyReferences();

  final List<ConstructorCallRef> constructorCalls = [];
  final List<MethodCallRef> methodCalls = [];
  final List<PropertyAccessRef> propertyAccesses = [];
  final List<FunctionCallRef> functionCalls = [];
  final List<String> typeCasts = [];
  final List<String> typeChecks = [];

  bool get isEmpty =>
      constructorCalls.isEmpty &&
      methodCalls.isEmpty &&
      propertyAccesses.isEmpty &&
      functionCalls.isEmpty &&
      typeCasts.isEmpty &&
      typeChecks.isEmpty;

  Map<String, dynamic> toJson() => {
    if (constructorCalls.isNotEmpty)
      'constructorCalls': constructorCalls.map((c) => c.toJson()).toList(),
    if (methodCalls.isNotEmpty)
      'methodCalls': methodCalls.map((m) => m.toJson()).toList(),
    if (propertyAccesses.isNotEmpty)
      'propertyAccesses': propertyAccesses.map((p) => p.toJson()).toList(),
    if (functionCalls.isNotEmpty)
      'functionCalls': functionCalls.map((f) => f.toJson()).toList(),
    if (typeCasts.isNotEmpty) 'typeCasts': typeCasts,
    if (typeChecks.isNotEmpty) 'typeChecks': typeChecks,
  };
}

/// A constructor call reference.
class ConstructorCallRef {
  ConstructorCallRef({
    required this.type,
    this.fullyQualifiedType,
    this.constructorName,
  });

  final String type;
  final String? fullyQualifiedType;
  final String? constructorName;

  Map<String, dynamic> toJson() => <String, dynamic>{
    'type': type,
    if (fullyQualifiedType != null) 'fullyQualifiedType': fullyQualifiedType,
    if (constructorName != null) 'constructorName': constructorName,
  };
}

/// A method call reference.
class MethodCallRef {
  MethodCallRef({
    this.targetType,
    this.fullyQualifiedTargetType,
    required this.methodName,
    this.isStatic = false,
  });

  final String? targetType;
  final String? fullyQualifiedTargetType;
  final String methodName;
  final bool isStatic;

  Map<String, dynamic> toJson() => <String, dynamic>{
    if (targetType != null) 'targetType': targetType,
    if (fullyQualifiedTargetType != null) 'fullyQualifiedTargetType': fullyQualifiedTargetType,
    'methodName': methodName,
    if (isStatic) 'isStatic': isStatic,
  };
}

/// A property access reference.
class PropertyAccessRef {
  PropertyAccessRef({
    this.targetType,
    this.fullyQualifiedTargetType,
    required this.propertyName,
    this.isStatic = false,
  });

  final String? targetType;
  final String? fullyQualifiedTargetType;
  final String propertyName;
  final bool isStatic;

  Map<String, dynamic> toJson() => <String, dynamic>{
    if (targetType != null) 'targetType': targetType,
    if (fullyQualifiedTargetType != null) 'fullyQualifiedTargetType': fullyQualifiedTargetType,
    'propertyName': propertyName,
    if (isStatic) 'isStatic': isStatic,
  };
}

/// A function call reference (top-level or prefix-qualified functions).
class FunctionCallRef {
  FunctionCallRef({
    this.prefix,
    required this.functionName,
    this.fullyQualifiedName,
    this.sourcePath,
  });

  /// Import prefix if any (e.g., 'math' in 'math.cos()').
  final String? prefix;

  /// The function name being called.
  final String functionName;

  /// Fully qualified name (e.g., 'dart:math#cos').
  final String? fullyQualifiedName;

  /// Source file path where this function is defined (for non-SDK deps).
  final String? sourcePath;

  Map<String, dynamic> toJson() => <String, dynamic>{
    if (prefix != null) 'prefix': prefix,
    'functionName': functionName,
    if (fullyQualifiedName != null) 'fullyQualifiedName': fullyQualifiedName,
    if (sourcePath != null) 'sourcePath': sourcePath,
  };
}

/// Represents a constructor declaration.
class ConstructorDecl {
  ConstructorDecl({
    this.name,
    this.isConst = false,
    this.isFactory = false,
    this.parameters = const [],
    this.annotations = const [],
    this.documentation,
    this.location,
    this.redirectsTo,
    this.superConstructorCall,
    this.bodyReferences,
  });

  final String? name;
  final bool isConst;
  final bool isFactory;
  final List<Parameter> parameters;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final String? redirectsTo;
  final String? superConstructorCall;
  final BodyReferences? bodyReferences;

  Map<String, dynamic> toJson() => {
    if (name != null) 'name': name,
    if (isConst) 'isConst': isConst,
    if (isFactory) 'isFactory': isFactory,
    if (parameters.isNotEmpty)
      'parameters': parameters.map((p) => p.toJson()).toList(),
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (redirectsTo != null) 'redirectsTo': redirectsTo,
    if (superConstructorCall != null) 'superConstructorCall': superConstructorCall,
    if (bodyReferences != null && !bodyReferences!.isEmpty)
      'bodyReferences': bodyReferences!.toJson(),
  };
}

/// Represents FFI @Native annotation info.
class NativeInfo {
  NativeInfo({
    this.symbol,
    this.isLeaf = false,
  });

  final String? symbol;
  final bool isLeaf;

  Map<String, dynamic> toJson() => {
    if (symbol != null) 'symbol': symbol,
    if (isLeaf) 'isLeaf': isLeaf,
  };
}

/// Represents a method declaration.
class MethodDecl {
  MethodDecl({
    required this.name,
    required this.returnType,
    this.typeParameters = const [],
    this.parameters = const [],
    this.isStatic = false,
    this.isAbstract = false,
    this.isOverride = false,
    this.isOperator = false,
    this.modifiers = const [],
    this.annotations = const [],
    this.documentation,
    this.location,
    this.bodyReferences,
    this.nativeInfo,
  });

  final String name;
  final TypeRef returnType;
  final List<TypeParameter> typeParameters;
  final List<Parameter> parameters;
  final bool isStatic;
  final bool isAbstract;
  final bool isOverride;
  final bool isOperator;
  final List<String> modifiers; // async, async*, sync*
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final BodyReferences? bodyReferences;
  final NativeInfo? nativeInfo;

  Map<String, dynamic> toJson() => {
    'name': name,
    'returnType': returnType.toJson(),
    if (typeParameters.isNotEmpty)
      'typeParameters': typeParameters.map((t) => t.toJson()).toList(),
    if (parameters.isNotEmpty)
      'parameters': parameters.map((p) => p.toJson()).toList(),
    if (isStatic) 'isStatic': isStatic,
    if (isAbstract) 'isAbstract': isAbstract,
    if (isOverride) 'isOverride': isOverride,
    if (isOperator) 'isOperator': isOperator,
    if (modifiers.isNotEmpty) 'modifiers': modifiers,
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (bodyReferences != null && !bodyReferences!.isEmpty)
      'bodyReferences': bodyReferences!.toJson(),
    if (nativeInfo != null) 'nativeInfo': nativeInfo!.toJson(),
  };
}

/// Represents a field declaration.
class FieldDecl {
  FieldDecl({
    required this.name,
    required this.type,
    this.isStatic = false,
    this.isFinal = false,
    this.isConst = false,
    this.isLate = false,
    this.isOverride = false,
    this.annotations = const [],
    this.documentation,
    this.location,
  });

  final String name;
  final TypeRef type;
  final bool isStatic;
  final bool isFinal;
  final bool isConst;
  final bool isLate;
  final bool isOverride;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;

  Map<String, dynamic> toJson() => {
    'name': name,
    'type': type.toJson(),
    if (isStatic) 'isStatic': isStatic,
    if (isFinal) 'isFinal': isFinal,
    if (isConst) 'isConst': isConst,
    if (isLate) 'isLate': isLate,
    if (isOverride) 'isOverride': isOverride,
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
  };
}

/// Represents a getter or setter declaration.
class AccessorDecl {
  AccessorDecl({
    required this.name,
    required this.kind,
    required this.type,
    this.isStatic = false,
    this.isAbstract = false,
    this.annotations = const [],
    this.documentation,
    this.location,
    this.bodyReferences,
  });

  final String name;
  final String kind; // 'getter' or 'setter'
  final TypeRef type;
  final bool isStatic;
  final bool isAbstract;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final BodyReferences? bodyReferences;

  Map<String, dynamic> toJson() => {
    'name': name,
    'kind': kind,
    'type': type.toJson(),
    if (isStatic) 'isStatic': isStatic,
    if (isAbstract) 'isAbstract': isAbstract,
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (bodyReferences != null && !bodyReferences!.isEmpty)
      'bodyReferences': bodyReferences!.toJson(),
  };
}

/// Represents a class declaration.
class ClassDecl {
  ClassDecl({
    required this.name,
    required this.kind,
    this.typeParameters = const [],
    this.superclass,
    this.interfaces = const [],
    this.mixins = const [],
    this.annotations = const [],
    this.documentation,
    this.location,
    this.constructors = const [],
    this.methods = const [],
    this.fields = const [],
    this.accessors = const [],
  });

  final String name;
  final String kind; // 'class', 'abstract class', 'mixin class', etc.
  final List<TypeParameter> typeParameters;
  final TypeRef? superclass;
  final List<TypeRef> interfaces;
  final List<TypeRef> mixins;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final List<ConstructorDecl> constructors;
  final List<MethodDecl> methods;
  final List<FieldDecl> fields;
  final List<AccessorDecl> accessors;

  Map<String, dynamic> toJson() => {
    'name': name,
    'kind': kind,
    if (typeParameters.isNotEmpty)
      'typeParameters': typeParameters.map((t) => t.toJson()).toList(),
    if (superclass != null) 'superclass': superclass!.toJson(),
    if (interfaces.isNotEmpty)
      'interfaces': interfaces.map((i) => i.toJson()).toList(),
    if (mixins.isNotEmpty) 'mixins': mixins.map((m) => m.toJson()).toList(),
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (constructors.isNotEmpty)
      'constructors': constructors.map((c) => c.toJson()).toList(),
    if (methods.isNotEmpty) 'methods': methods.map((m) => m.toJson()).toList(),
    if (fields.isNotEmpty) 'fields': fields.map((f) => f.toJson()).toList(),
    if (accessors.isNotEmpty)
      'accessors': accessors.map((a) => a.toJson()).toList(),
  };
}

/// Represents a mixin declaration.
class MixinDecl {
  MixinDecl({
    required this.name,
    this.typeParameters = const [],
    this.onTypes = const [],
    this.interfaces = const [],
    this.annotations = const [],
    this.documentation,
    this.location,
    this.methods = const [],
    this.fields = const [],
    this.accessors = const [],
  });

  final String name;
  final List<TypeParameter> typeParameters;
  final List<TypeRef> onTypes;
  final List<TypeRef> interfaces;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final List<MethodDecl> methods;
  final List<FieldDecl> fields;
  final List<AccessorDecl> accessors;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (typeParameters.isNotEmpty)
      'typeParameters': typeParameters.map((t) => t.toJson()).toList(),
    if (onTypes.isNotEmpty) 'onTypes': onTypes.map((t) => t.toJson()).toList(),
    if (interfaces.isNotEmpty)
      'interfaces': interfaces.map((i) => i.toJson()).toList(),
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (methods.isNotEmpty) 'methods': methods.map((m) => m.toJson()).toList(),
    if (fields.isNotEmpty) 'fields': fields.map((f) => f.toJson()).toList(),
    if (accessors.isNotEmpty)
      'accessors': accessors.map((a) => a.toJson()).toList(),
  };
}

/// Represents an enum value.
class EnumValue {
  EnumValue({
    required this.name,
    this.arguments,
    this.documentation,
  });

  final String name;
  final String? arguments;
  final String? documentation;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (arguments != null) 'arguments': arguments,
    if (documentation != null) 'documentation': documentation,
  };
}

/// Represents an enum declaration.
class EnumDecl {
  EnumDecl({
    required this.name,
    this.typeParameters = const [],
    this.interfaces = const [],
    this.mixins = const [],
    this.annotations = const [],
    this.documentation,
    this.location,
    this.values = const [],
    this.constructors = const [],
    this.methods = const [],
    this.fields = const [],
  });

  final String name;
  final List<TypeParameter> typeParameters;
  final List<TypeRef> interfaces;
  final List<TypeRef> mixins;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final List<EnumValue> values;
  final List<ConstructorDecl> constructors;
  final List<MethodDecl> methods;
  final List<FieldDecl> fields;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (typeParameters.isNotEmpty)
      'typeParameters': typeParameters.map((t) => t.toJson()).toList(),
    if (interfaces.isNotEmpty)
      'interfaces': interfaces.map((i) => i.toJson()).toList(),
    if (mixins.isNotEmpty) 'mixins': mixins.map((m) => m.toJson()).toList(),
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (values.isNotEmpty) 'values': values.map((v) => v.toJson()).toList(),
    if (constructors.isNotEmpty)
      'constructors': constructors.map((c) => c.toJson()).toList(),
    if (methods.isNotEmpty) 'methods': methods.map((m) => m.toJson()).toList(),
    if (fields.isNotEmpty) 'fields': fields.map((f) => f.toJson()).toList(),
  };
}

/// Represents an extension declaration.
class ExtensionDecl {
  ExtensionDecl({
    this.name,
    this.typeParameters = const [],
    required this.extendedType,
    this.annotations = const [],
    this.documentation,
    this.location,
    this.methods = const [],
    this.fields = const [],
    this.accessors = const [],
  });

  final String? name;
  final List<TypeParameter> typeParameters;
  final TypeRef extendedType;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final List<MethodDecl> methods;
  final List<FieldDecl> fields;
  final List<AccessorDecl> accessors;

  Map<String, dynamic> toJson() => {
    if (name != null) 'name': name,
    if (typeParameters.isNotEmpty)
      'typeParameters': typeParameters.map((t) => t.toJson()).toList(),
    'extendedType': extendedType.toJson(),
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (methods.isNotEmpty) 'methods': methods.map((m) => m.toJson()).toList(),
    if (fields.isNotEmpty) 'fields': fields.map((f) => f.toJson()).toList(),
    if (accessors.isNotEmpty)
      'accessors': accessors.map((a) => a.toJson()).toList(),
  };
}

/// Represents an extension type declaration.
class ExtensionTypeDecl {
  ExtensionTypeDecl({
    required this.name,
    this.typeParameters = const [],
    required this.representationType,
    this.interfaces = const [],
    this.annotations = const [],
    this.documentation,
    this.location,
    this.constructors = const [],
    this.methods = const [],
    this.fields = const [],
    this.accessors = const [],
  });

  final String name;
  final List<TypeParameter> typeParameters;
  final TypeRef representationType;
  final List<TypeRef> interfaces;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final List<ConstructorDecl> constructors;
  final List<MethodDecl> methods;
  final List<FieldDecl> fields;
  final List<AccessorDecl> accessors;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (typeParameters.isNotEmpty)
      'typeParameters': typeParameters.map((t) => t.toJson()).toList(),
    'representationType': representationType.toJson(),
    if (interfaces.isNotEmpty)
      'interfaces': interfaces.map((i) => i.toJson()).toList(),
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (constructors.isNotEmpty)
      'constructors': constructors.map((c) => c.toJson()).toList(),
    if (methods.isNotEmpty) 'methods': methods.map((m) => m.toJson()).toList(),
    if (fields.isNotEmpty) 'fields': fields.map((f) => f.toJson()).toList(),
    if (accessors.isNotEmpty)
      'accessors': accessors.map((a) => a.toJson()).toList(),
  };
}

/// Represents a typedef declaration.
class TypedefDecl {
  TypedefDecl({
    required this.name,
    this.typeParameters = const [],
    required this.aliasedType,
    this.annotations = const [],
    this.documentation,
    this.location,
  });

  final String name;
  final List<TypeParameter> typeParameters;
  final TypeRef aliasedType;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (typeParameters.isNotEmpty)
      'typeParameters': typeParameters.map((t) => t.toJson()).toList(),
    'aliasedType': aliasedType.toJson(),
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
  };
}

/// Represents a top-level function declaration.
class FunctionDecl {
  FunctionDecl({
    required this.name,
    required this.returnType,
    this.typeParameters = const [],
    this.parameters = const [],
    this.modifiers = const [],
    this.annotations = const [],
    this.documentation,
    this.location,
    this.bodyReferences,
  });

  final String name;
  final TypeRef returnType;
  final List<TypeParameter> typeParameters;
  final List<Parameter> parameters;
  final List<String> modifiers;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;
  final BodyReferences? bodyReferences;

  Map<String, dynamic> toJson() => {
    'name': name,
    'returnType': returnType.toJson(),
    if (typeParameters.isNotEmpty)
      'typeParameters': typeParameters.map((t) => t.toJson()).toList(),
    if (parameters.isNotEmpty)
      'parameters': parameters.map((p) => p.toJson()).toList(),
    if (modifiers.isNotEmpty) 'modifiers': modifiers,
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (bodyReferences != null && !bodyReferences!.isEmpty)
      'bodyReferences': bodyReferences!.toJson(),
  };
}

/// Represents a top-level variable declaration.
class VariableDecl {
  VariableDecl({
    required this.name,
    required this.type,
    this.isFinal = false,
    this.isConst = false,
    this.isLate = false,
    this.annotations = const [],
    this.documentation,
    this.location,
    this.dependencies = const [],
  });

  final String name;
  final TypeRef type;
  final bool isFinal;
  final bool isConst;
  final bool isLate;
  final List<AnnotationInfo> annotations;
  final String? documentation;
  final SourceLocation? location;

  /// Types that this variable depends on (e.g., the type used in the initializer).
  /// For example, `final ChannelBuffers channelBuffers = ChannelBuffers();`
  /// would have a dependency on `ChannelBuffers`.
  final List<TypeRef> dependencies;

  Map<String, dynamic> toJson() => {
    'name': name,
    'type': type.toJson(),
    if (isFinal) 'isFinal': isFinal,
    if (isConst) 'isConst': isConst,
    if (isLate) 'isLate': isLate,
    if (annotations.isNotEmpty)
      'annotations': annotations.map((a) => a.toJson()).toList(),
    if (documentation != null) 'documentation': documentation,
    if (location != null) 'location': location!.toJson(),
    if (dependencies.isNotEmpty)
      'dependencies': dependencies.map((d) => d.toJson()).toList(),
  };
}

/// Container for all declarations in a file.
class Declarations {
  Declarations();

  final List<ClassDecl> classes = [];
  final List<MixinDecl> mixins = [];
  final List<EnumDecl> enums = [];
  final List<ExtensionDecl> extensions = [];
  final List<ExtensionTypeDecl> extensionTypes = [];
  final List<TypedefDecl> typedefs = [];
  final List<FunctionDecl> functions = [];
  final List<VariableDecl> topLevelVariables = [];

  Map<String, dynamic> toJson() => {
    if (classes.isNotEmpty) 'classes': classes.map((c) => c.toJson()).toList(),
    if (mixins.isNotEmpty) 'mixins': mixins.map((m) => m.toJson()).toList(),
    if (enums.isNotEmpty) 'enums': enums.map((e) => e.toJson()).toList(),
    if (extensions.isNotEmpty)
      'extensions': extensions.map((e) => e.toJson()).toList(),
    if (extensionTypes.isNotEmpty)
      'extensionTypes': extensionTypes.map((e) => e.toJson()).toList(),
    if (typedefs.isNotEmpty)
      'typedefs': typedefs.map((t) => t.toJson()).toList(),
    if (functions.isNotEmpty)
      'functions': functions.map((f) => f.toJson()).toList(),
    if (topLevelVariables.isNotEmpty)
      'topLevelVariables': topLevelVariables.map((v) => v.toJson()).toList(),
  };
}

/// Represents a dependency on an external symbol.
class ExternalDependency {
  ExternalDependency({
    required this.library,
    required this.symbol,
    this.usageType = 'reference',
    this.sourcePath,
  });

  final String library; // e.g., "dart:math", "dart:typed_data", "dart.ui"
  final String symbol;  // e.g., "cos", "Float32List", "_lerpDouble"
  final String usageType; // "type", "constructor", "method", "property"
  final String? sourcePath; // For non-SDK deps, the file path (e.g., "lerp.dart")

  /// Returns true if this is a Dart SDK dependency (dart:core, dart:math, etc.)
  bool get isDartSdk => library.startsWith('dart:');

  Map<String, dynamic> toJson() => {
    'library': library,
    'symbol': symbol,
    'usageType': usageType,
    if (sourcePath != null) 'sourcePath': sourcePath,
  };

  @override
  bool operator ==(Object other) =>
      other is ExternalDependency &&
      library == other.library &&
      symbol == other.symbol &&
      usageType == other.usageType &&
      sourcePath == other.sourcePath;

  @override
  int get hashCode => Object.hash(library, symbol, usageType, sourcePath);
}

/// Represents the dependency relationship for a declaration.
class DependencyInfo {
  DependencyInfo({
    required this.name,
    this.dependsOn = const [],
    this.dependedBy = const [],
  });

  /// Name of this declaration.
  final String name;

  /// Names of declarations this one depends on (must be ported first).
  final List<String> dependsOn;

  /// Names of declarations that depend on this one.
  final List<String> dependedBy;

  Map<String, dynamic> toJson() => {
    'name': name,
    if (dependsOn.isNotEmpty) 'dependsOn': dependsOn,
    if (dependedBy.isNotEmpty) 'dependedBy': dependedBy,
  };
}

/// Dependency graph for ordering migration tasks.
class DependencyGraph {
  DependencyGraph();

  /// Dependency info for each class/type in the file.
  final List<DependencyInfo> classes = [];

  /// Dependency info for top-level variables.
  final List<DependencyInfo> variables = [];

  /// Topologically sorted order for migration (dependencies first).
  final List<String> migrationOrder = [];

  /// Classes with circular dependencies (need special handling).
  final List<List<String>> circularDependencies = [];

  Map<String, dynamic> toJson() => {
    if (classes.isNotEmpty) 'classes': classes.map((c) => c.toJson()).toList(),
    if (variables.isNotEmpty) 'variables': variables.map((v) => v.toJson()).toList(),
    if (migrationOrder.isNotEmpty) 'migrationOrder': migrationOrder,
    if (circularDependencies.isNotEmpty) 'circularDependencies': circularDependencies,
  };
}

/// Represents file-level dependency information for cross-file analysis.
class FileDependencyInfo {
  FileDependencyInfo({
    required this.filePath,
    this.dependsOn = const [],
    this.dependedBy = const [],
    this.exportedSymbols = const [],
    this.importedSymbols = const [],
  });

  /// Path to this file.
  final String filePath;

  /// File paths this file depends on (must be migrated first).
  final List<String> dependsOn;

  /// File paths that depend on this file.
  final List<String> dependedBy;

  /// Symbols exported by this file (classes, functions, etc.).
  final List<String> exportedSymbols;

  /// Symbols imported from other files (with source file info).
  final List<ImportedSymbol> importedSymbols;

  Map<String, dynamic> toJson() => {
    'filePath': filePath,
    if (dependsOn.isNotEmpty) 'dependsOn': dependsOn,
    if (dependedBy.isNotEmpty) 'dependedBy': dependedBy,
    if (exportedSymbols.isNotEmpty) 'exportedSymbols': exportedSymbols,
    if (importedSymbols.isNotEmpty)
      'importedSymbols': importedSymbols.map((s) => s.toJson()).toList(),
  };
}

/// Represents a symbol imported from another file.
class ImportedSymbol {
  ImportedSymbol({
    required this.symbol,
    required this.sourceFile,
    this.usageType = 'reference',
  });

  final String symbol;
  final String sourceFile;
  final String usageType;

  Map<String, dynamic> toJson() => {
    'symbol': symbol,
    'sourceFile': sourceFile,
    'usageType': usageType,
  };

  @override
  bool operator ==(Object other) =>
      other is ImportedSymbol &&
      symbol == other.symbol &&
      sourceFile == other.sourceFile;

  @override
  int get hashCode => Object.hash(symbol, sourceFile);
}

/// Global dependency graph across multiple files.
class GlobalDependencyGraph {
  GlobalDependencyGraph();

  /// Dependency info for each file.
  final List<FileDependencyInfo> files = [];

  /// Files in topological order for migration (dependencies first).
  final List<String> migrationOrder = [];

  /// Groups of files with circular dependencies (need special handling).
  final List<List<String>> circularDependencies = [];

  Map<String, dynamic> toJson() => {
    'files': files.map((f) => f.toJson()).toList(),
    'migrationOrder': migrationOrder,
    if (circularDependencies.isNotEmpty)
      'circularDependencies': circularDependencies,
  };

  /// Generates a compact text representation for AI consumption.
  String toCompactText() {
    final StringBuffer buf = StringBuffer();

    buf.writeln('# Multi-File Dependency Analysis');
    buf.writeln('Total files: ${files.length}');
    buf.writeln();

    // Migration order
    buf.writeln('## Migration Order');
    for (int i = 0; i < migrationOrder.length; i++) {
      final String filePath = migrationOrder[i];
      final String fileName = filePath.split('/').last;
      buf.writeln('${i + 1}. $fileName');
    }
    buf.writeln();

    // Circular dependencies warning
    if (circularDependencies.isNotEmpty) {
      buf.writeln('## Circular Dependencies (need special handling)');
      for (final List<String> cycle in circularDependencies) {
        final List<String> fileNames = cycle.map((p) => p.split('/').last).toList();
        buf.writeln('- ${fileNames.join(' <-> ')}');
      }
      buf.writeln();
    }

    // File details
    buf.writeln('## File Dependencies');
    for (final FileDependencyInfo file in files) {
      final String fileName = file.filePath.split('/').last;
      buf.writeln('### $fileName');
      if (file.dependsOn.isNotEmpty) {
        buf.writeln('  depends on: ${file.dependsOn.map((p) => p.split('/').last).join(', ')}');
      }
      if (file.dependedBy.isNotEmpty) {
        buf.writeln('  required by: ${file.dependedBy.map((p) => p.split('/').last).join(', ')}');
      }
      if (file.exportedSymbols.isNotEmpty) {
        buf.writeln('  exports: ${file.exportedSymbols.join(', ')}');
      }
    }

    return buf.toString();
  }
}

/// Complete metadata for multiple Dart files with cross-file dependency analysis.
class MultiFileMetadata {
  MultiFileMetadata();

  static const String schemaVersion = '1.0.0';

  /// All individual file metadata.
  final List<FileMetadata> files = [];

  /// Global dependency graph across all files.
  final GlobalDependencyGraph globalDependencyGraph = GlobalDependencyGraph();

  Map<String, dynamic> toJson() => {
    'version': schemaVersion,
    'fileCount': files.length,
    'globalDependencyGraph': globalDependencyGraph.toJson(),
    'files': files.map((f) => f.toJson()).toList(),
  };

  /// Generates a compact text representation for AI consumption.
  String toCompactText() {
    final StringBuffer buf = StringBuffer();

    // Global dependency info first
    buf.write(globalDependencyGraph.toCompactText());
    buf.writeln();
    buf.writeln('---');
    buf.writeln();

    // Then individual file metadata
    for (final FileMetadata file in files) {
      buf.write(file.toCompactText());
      buf.writeln('---');
      buf.writeln();
    }

    return buf.toString();
  }
}

/// Complete metadata for a Dart file.
class FileMetadata {
  FileMetadata();

  static const String schemaVersion = '1.1.0';

  String filePath = '';
  final LibraryInfo library = LibraryInfo();
  String? partOf; // If this file is a 'part of' another library
  final List<ImportInfo> imports = [];
  final List<ExportInfo> exports = [];
  final List<String> parts = [];
  final Declarations declarations = Declarations();
  final Set<ExternalDependency> externalDependencies = {};
  final DependencyGraph dependencyGraph = DependencyGraph();

  /// Symbols exported by this file (populated during multi-file analysis).
  List<String>? exportedSymbols;

  Map<String, dynamic> toJson() => {
    'version': schemaVersion,
    'filePath': filePath,
    'library': library.toJson(),
    if (partOf != null) 'partOf': partOf,
    if (imports.isNotEmpty) 'imports': imports.map((i) => i.toJson()).toList(),
    if (exports.isNotEmpty) 'exports': exports.map((e) => e.toJson()).toList(),
    if (parts.isNotEmpty) 'parts': parts,
    'declarations': declarations.toJson(),
    if (externalDependencies.isNotEmpty)
      'externalDependencies': externalDependencies.map((d) => d.toJson()).toList(),
    'dependencyGraph': dependencyGraph.toJson(),
  };

  /// Generates a compact, AI-readable text format for migration planning.
  ///
  /// This format is ~10-20x smaller than JSON and optimized for AI consumption.
  /// Format:
  /// - One line per member
  /// - Compact notation for types, modifiers, and dependencies
  /// - No redundant information
  String toCompactText() {
    final StringBuffer buf = StringBuffer();

    // Header
    buf.writeln('# ${filePath.split('/').last}');
    buf.writeln('path: $filePath');
    if (partOf != null) buf.writeln('partOf: $partOf');
    buf.writeln();

    // External dependencies (grouped by library)
    if (externalDependencies.isNotEmpty) {
      buf.writeln('## External Dependencies');

      // Group SDK deps by library (dart:core, dart:math, etc.)
      final Map<String, Set<String>> sdkDeps = <String, Set<String>>{};
      // Group cross-file deps by source file (same library, different file)
      final Map<String, Set<String>> crossFileDeps = <String, Set<String>>{};
      // Group non-SDK deps by source file or library
      final Map<String, Set<String>> projectDeps = <String, Set<String>>{};

      for (final ExternalDependency dep in externalDependencies) {
        // Check if this is a cross-file dependency (has sourcePath)
        if (dep.sourcePath != null) {
          // Cross-file dep - group by source file
          crossFileDeps.putIfAbsent(dep.sourcePath!, () => <String>{}).add(dep.symbol);
        } else if (dep.isDartSdk) {
          // SDK dep without source path
          sdkDeps.putIfAbsent(dep.library, () => <String>{}).add(dep.symbol);
        } else {
          // Non-SDK without source path - use library name
          projectDeps.putIfAbsent(dep.library, () => <String>{}).add(dep.symbol);
        }
      }

      // Output SDK deps first
      for (final String lib in sdkDeps.keys.toList()..sort()) {
        buf.writeln('$lib: ${(sdkDeps[lib]!.toList()..sort()).join(', ')}');
      }

      // Output cross-file deps (from same library but different files)
      if (crossFileDeps.isNotEmpty) {
        for (final String sourcePath in crossFileDeps.keys.toList()..sort()) {
          buf.writeln('$sourcePath: ${(crossFileDeps[sourcePath]!.toList()..sort()).join(', ')}');
        }
      }

      // Output project deps with file paths
      if (projectDeps.isNotEmpty) {
        for (final String sourcePath in projectDeps.keys.toList()..sort()) {
          buf.writeln('$sourcePath: ${(projectDeps[sourcePath]!.toList()..sort()).join(', ')}');
        }
      }
      buf.writeln();
    }

    // Migration order
    if (dependencyGraph.migrationOrder.isNotEmpty) {
      buf.writeln('## Migration Order');
      buf.writeln(dependencyGraph.migrationOrder.join(' -> '));
      buf.writeln();
    }

    // Circular dependencies warning
    if (dependencyGraph.circularDependencies.isNotEmpty) {
      buf.writeln('## Circular Dependencies (need special handling)');
      for (final cycle in dependencyGraph.circularDependencies) {
        buf.writeln('- ${cycle.join(' <-> ')}');
      }
      buf.writeln();
    }

    // Classes
    for (final cls in declarations.classes) {
      buf.writeln(_formatClass(cls));
    }

    // Mixins
    for (final mixin in declarations.mixins) {
      buf.writeln(_formatMixin(mixin));
    }

    // Enums
    for (final enumDecl in declarations.enums) {
      buf.writeln(_formatEnum(enumDecl));
    }

    // Extension types
    for (final extType in declarations.extensionTypes) {
      buf.writeln(_formatExtensionType(extType));
    }

    // Top-level functions
    if (declarations.functions.isNotEmpty) {
      buf.writeln('## Functions');
      for (final func in declarations.functions) {
        buf.writeln(_formatFunction(func));
      }
      buf.writeln();
    }

    // Top-level variables
    if (declarations.topLevelVariables.isNotEmpty) {
      buf.writeln('## Variables');
      for (final v in declarations.topLevelVariables) {
        final mods = [
          if (v.isConst) 'const',
          if (v.isFinal) 'final',
          if (v.isLate) 'late',
        ];
        final String deps = v.dependencies.isNotEmpty
            ? ' | depends on: ${v.dependencies.map((d) => d.name).join(', ')}'
            : '';
        buf.writeln('L${v.location?.line ?? 0} ${mods.join(' ')} ${v.type} ${v.name}$deps');
      }
      buf.writeln();
    }

    return buf.toString();
  }

  String _formatClass(ClassDecl cls) {
    final StringBuffer buf = StringBuffer();
    final int memberCount = cls.constructors.length +
        cls.fields.length +
        cls.accessors.length +
        cls.methods.length;

    // Class header
    buf.write('## ${cls.name}');
    if (cls.kind != 'class') buf.write(' (${cls.kind})');
    buf.write(' ${_formatLineRange(cls.location)}');
    buf.write(' [$memberCount members]');
    buf.writeln();

    // Inheritance
    if (cls.superclass != null) buf.writeln('  extends: ${cls.superclass}');
    if (cls.interfaces.isNotEmpty) buf.writeln('  implements: ${cls.interfaces.join(', ')}');
    if (cls.mixins.isNotEmpty) buf.writeln('  with: ${cls.mixins.join(', ')}');
    if (cls.typeParameters.isNotEmpty) {
      buf.writeln('  typeParams: ${cls.typeParameters.map((t) => t.bound != null ? '${t.name} extends ${t.bound}' : t.name).join(', ')}');
    }

    // Dependencies on other local types
    final deps = dependencyGraph.classes.firstWhere(
      (d) => d.name == cls.name,
      orElse: () => DependencyInfo(name: cls.name),
    );
    if (deps.dependsOn.isNotEmpty) {
      buf.writeln('  uses: ${deps.dependsOn.join(', ')}');
    }

    buf.writeln();

    // Constructors
    for (final ctor in cls.constructors) {
      buf.writeln(_formatConstructor(cls.name, ctor));
    }

    // Fields
    for (final field in cls.fields) {
      buf.writeln(_formatField(field));
    }

    // Accessors (getters/setters)
    for (final acc in cls.accessors) {
      buf.writeln(_formatAccessor(acc));
    }

    // Methods
    for (final method in cls.methods) {
      buf.writeln(_formatMethod(method));
    }

    buf.writeln();
    return buf.toString();
  }

  String _formatMixin(MixinDecl mixin) {
    final StringBuffer buf = StringBuffer();
    final int memberCount = mixin.fields.length +
        mixin.accessors.length +
        mixin.methods.length;

    buf.write('## ${mixin.name} (mixin)');
    buf.write(' ${_formatLineRange(mixin.location)}');
    buf.write(' [$memberCount members]');
    buf.writeln();

    if (mixin.onTypes.isNotEmpty) buf.writeln('  on: ${mixin.onTypes.join(', ')}');
    if (mixin.interfaces.isNotEmpty) buf.writeln('  implements: ${mixin.interfaces.join(', ')}');

    for (final field in mixin.fields) {
      buf.writeln(_formatField(field));
    }
    for (final acc in mixin.accessors) {
      buf.writeln(_formatAccessor(acc));
    }
    for (final method in mixin.methods) {
      buf.writeln(_formatMethod(method));
    }

    buf.writeln();
    return buf.toString();
  }

  String _formatEnum(EnumDecl enumDecl) {
    final StringBuffer buf = StringBuffer();
    buf.write('## ${enumDecl.name} (enum)');
    buf.write(' ${_formatLineRange(enumDecl.location)}');
    buf.writeln();

    buf.writeln('  values: ${enumDecl.values.map((v) => v.name).join(', ')}');

    for (final ctor in enumDecl.constructors) {
      buf.writeln(_formatConstructor(enumDecl.name, ctor));
    }
    for (final field in enumDecl.fields) {
      buf.writeln(_formatField(field));
    }
    for (final method in enumDecl.methods) {
      buf.writeln(_formatMethod(method));
    }

    buf.writeln();
    return buf.toString();
  }

  String _formatExtensionType(ExtensionTypeDecl extType) {
    final StringBuffer buf = StringBuffer();
    buf.write('## ${extType.name} (extension type)');
    buf.write(' ${_formatLineRange(extType.location)}');
    buf.writeln();

    buf.writeln('  represents: ${extType.representationType}');
    if (extType.interfaces.isNotEmpty) {
      buf.writeln('  implements: ${extType.interfaces.join(', ')}');
    }

    for (final ctor in extType.constructors) {
      buf.writeln(_formatConstructor(extType.name, ctor));
    }
    for (final field in extType.fields) {
      buf.writeln(_formatField(field));
    }
    for (final acc in extType.accessors) {
      buf.writeln(_formatAccessor(acc));
    }
    for (final method in extType.methods) {
      buf.writeln(_formatMethod(method));
    }

    buf.writeln();
    return buf.toString();
  }

  /// Formats a line range as "L10-15" or just "L10" if single line.
  String _formatLineRange(SourceLocation? loc) {
    if (loc == null) return 'L0';
    final int start = loc.line;
    final int? end = loc.endLine;
    if (end == null || end == start) {
      return 'L$start';
    }
    return 'L$start-$end';
  }

  String _formatConstructor(String className, ConstructorDecl ctor) {
    final List<String> mods = [
      if (ctor.isConst) 'const',
      if (ctor.isFactory) 'factory',
    ];
    final String name = ctor.name != null ? '$className.${ctor.name}' : className;
    final String params = _formatParams(ctor.parameters);
    final String uses = _formatBodyRefs(ctor.bodyReferences);

    return '  ${_formatLineRange(ctor.location)} ${mods.join(' ')} ctor $name($params)${uses.isNotEmpty ? ' | $uses' : ''}';
  }

  String _formatField(FieldDecl field) {
    final List<String> mods = [
      if (field.isStatic) 'static',
      if (field.isConst) 'const',
      if (field.isFinal) 'final',
      if (field.isLate) 'late',
    ];
    final String native = field.annotations.any((a) => a.name == 'Native') ? '@Native ' : '';

    return '  ${_formatLineRange(field.location)} $native${mods.join(' ')} field ${field.type} ${field.name}';
  }

  String _formatAccessor(AccessorDecl acc) {
    final List<String> mods = [
      if (acc.isStatic) 'static',
      if (acc.isAbstract) 'abstract',
    ];
    final String uses = _formatBodyRefs(acc.bodyReferences);

    return '  ${_formatLineRange(acc.location)} ${mods.join(' ')} ${acc.kind} ${acc.type} ${acc.name}${uses.isNotEmpty ? ' | $uses' : ''}';
  }

  String _formatMethod(MethodDecl method) {
    final List<String> mods = [
      if (method.isStatic) 'static',
      if (method.isAbstract) 'abstract',
      if (method.isOverride) 'override',
    ];
    final String native = method.nativeInfo != null ? '@Native ' : '';
    final String opPrefix = method.isOperator ? 'operator ' : '';
    final String params = _formatParams(method.parameters);
    final String uses = _formatBodyRefs(method.bodyReferences);

    return '  ${_formatLineRange(method.location)} $native${mods.join(' ')} method ${method.returnType} $opPrefix${method.name}($params)${uses.isNotEmpty ? ' | $uses' : ''}';
  }

  String _formatFunction(FunctionDecl func) {
    final String params = _formatParams(func.parameters);
    final String uses = _formatBodyRefs(func.bodyReferences);

    return '${_formatLineRange(func.location)} func ${func.returnType} ${func.name}($params)${uses.isNotEmpty ? ' | $uses' : ''}';
  }

  String _formatParams(List<Parameter> params) {
    if (params.isEmpty) return '';
    return params.map((p) {
      final String req = p.isRequired ? '' : '?';
      final String named = p.isNamed ? '${p.name}: ' : '';
      return '$named${p.type}$req';
    }).join(', ');
  }

  String _formatBodyRefs(BodyReferences? refs) {
    if (refs == null || refs.isEmpty) return '';

    final Set<String> uses = {};

    for (final call in refs.constructorCalls) {
      final String ctor = call.constructorName != null
          ? '${call.type}.${call.constructorName}()'
          : '${call.type}()';
      uses.add(ctor);
    }

    for (final call in refs.methodCalls) {
      if (call.targetType != null) {
        uses.add('${call.targetType}.${call.methodName}()');
      } else {
        uses.add('${call.methodName}()');
      }
    }

    for (final access in refs.propertyAccesses) {
      if (access.targetType != null) {
        uses.add('${access.targetType}.${access.propertyName}');
      } else {
        uses.add(access.propertyName);
      }
    }

    for (final call in refs.functionCalls) {
      if (call.prefix != null) {
        uses.add('${call.prefix}.${call.functionName}()');
      } else {
        uses.add('${call.functionName}()');
      }
    }

    return 'uses: ${uses.join(', ')}';
  }
}
