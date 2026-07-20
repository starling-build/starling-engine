// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/dart/element/element.dart';
import 'package:analyzer/dart/element/nullability_suffix.dart';
import 'package:analyzer/dart/element/type.dart';

import '../models/metadata_models.dart' as models;

/// Utility class for extracting type information from AST nodes.
class TypeExtractor {
  /// Extracts a [models.TypeRef] from a [TypeAnnotation].
  static models.TypeRef fromTypeAnnotation(TypeAnnotation? annotation) {
    if (annotation == null) {
      return models.TypeRef(name: 'dynamic', isNullable: true);
    }

    if (annotation is NamedType) {
      return _fromNamedType(annotation);
    }

    if (annotation is GenericFunctionType) {
      return _fromGenericFunctionType(annotation);
    }

    if (annotation is RecordTypeAnnotation) {
      return _fromRecordType(annotation);
    }

    // Fallback for unknown types
    return models.TypeRef(name: annotation.toSource());
  }

  static models.TypeRef _fromNamedType(NamedType node) {
    final String name = node.name2.lexeme;
    final bool isNullable = node.question != null;

    // Extract type arguments
    final List<models.TypeRef> typeArgs = <models.TypeRef>[];
    if (node.typeArguments != null) {
      for (final TypeAnnotation arg in node.typeArguments!.arguments) {
        typeArgs.add(fromTypeAnnotation(arg));
      }
    }

    // Try to get fully qualified name from element
    String? fullyQualifiedName;
    final Element? element = node.element;
    if (element != null) {
      fullyQualifiedName = _getFullyQualifiedName(element);
    }

    return models.TypeRef(
      name: name,
      fullyQualifiedName: fullyQualifiedName,
      isNullable: isNullable,
      typeArguments: typeArgs,
    );
  }

  static models.TypeRef _fromGenericFunctionType(GenericFunctionType node) {
    // For function types, we represent them as a string
    return models.TypeRef(
      name: node.toSource(),
      isNullable: node.question != null,
    );
  }

  static models.TypeRef _fromRecordType(RecordTypeAnnotation node) {
    // For record types, we represent them as a string
    return models.TypeRef(
      name: node.toSource(),
      isNullable: node.question != null,
    );
  }

  /// Extracts a [models.TypeRef] from a [DartType].
  static models.TypeRef fromDartType(DartType? type) {
    if (type == null || type is DynamicType) {
      return models.TypeRef(name: 'dynamic', isNullable: true);
    }

    if (type is VoidType) {
      return models.TypeRef(name: 'void', isNullable: false);
    }

    if (type is NeverType) {
      return models.TypeRef(name: 'Never', isNullable: false);
    }

    if (type is InterfaceType) {
      return _fromInterfaceType(type);
    }

    if (type is FunctionType) {
      return models.TypeRef(
        name: type.getDisplayString(),
        isNullable: type.nullabilitySuffix == NullabilitySuffix.question,
      );
    }

    if (type is TypeParameterType) {
      return models.TypeRef(
        name: type.element.name,
        isNullable: type.nullabilitySuffix == NullabilitySuffix.question,
      );
    }

    if (type is RecordType) {
      return models.TypeRef(
        name: type.getDisplayString(),
        isNullable: type.nullabilitySuffix == NullabilitySuffix.question,
      );
    }

    // Fallback
    return models.TypeRef(
      name: type.getDisplayString(),
      isNullable: type.nullabilitySuffix == NullabilitySuffix.question,
    );
  }

  static models.TypeRef _fromInterfaceType(InterfaceType type) {
    final String name = type.element.name;
    final bool isNullable = type.nullabilitySuffix == NullabilitySuffix.question;

    // Extract type arguments
    final List<models.TypeRef> typeArgs = <models.TypeRef>[];
    for (final DartType arg in type.typeArguments) {
      typeArgs.add(fromDartType(arg));
    }

    // Get fully qualified name
    final String? fullyQualifiedName = _getFullyQualifiedName(type.element);

    return models.TypeRef(
      name: name,
      fullyQualifiedName: fullyQualifiedName,
      isNullable: isNullable,
      typeArguments: typeArgs,
    );
  }

  /// Gets the fully qualified name of an element.
  static String? _getFullyQualifiedName(Element element) {
    final LibraryElement? library = element.library;
    if (library == null) {
      return null;
    }

    final String? elementName = element.name;
    if (elementName == null) {
      return null;
    }

    // Get the library URI
    final String libraryUri = library.source.uri.toString();

    return '$libraryUri#$elementName';
  }

  /// Extracts type parameters from a [TypeParameterList].
  static List<models.TypeParameter> extractTypeParameters(TypeParameterList? params) {
    if (params == null) {
      return <models.TypeParameter>[];
    }

    return params.typeParameters.map((TypeParameter p) {
      return models.TypeParameter(
        name: p.name.lexeme,
        bound: p.bound != null ? fromTypeAnnotation(p.bound) : null,
      );
    }).toList();
  }

  /// Extracts the superclass type from an extends clause.
  static models.TypeRef? extractSuperclass(ExtendsClause? clause) {
    if (clause == null) {
      return null;
    }
    return fromTypeAnnotation(clause.superclass);
  }

  /// Extracts interface types from an implements clause.
  static List<models.TypeRef> extractInterfaces(ImplementsClause? clause) {
    if (clause == null) {
      return <models.TypeRef>[];
    }
    return clause.interfaces.map((NamedType t) => fromTypeAnnotation(t)).toList();
  }

  /// Extracts mixin types from a with clause.
  static List<models.TypeRef> extractMixins(WithClause? clause) {
    if (clause == null) {
      return <models.TypeRef>[];
    }
    return clause.mixinTypes.map((NamedType t) => fromTypeAnnotation(t)).toList();
  }

  /// Extracts "on" types from a mixin's on clause.
  static List<models.TypeRef> extractOnTypes(MixinOnClause? clause) {
    if (clause == null) {
      return <models.TypeRef>[];
    }
    return clause.superclassConstraints.map((NamedType t) => fromTypeAnnotation(t)).toList();
  }
}
