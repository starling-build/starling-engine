// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/dart/ast/visitor.dart';
import 'package:analyzer/dart/element/element.dart';
import 'package:analyzer/dart/element/type.dart';
import 'package:analyzer/source/line_info.dart';

import '../models/metadata_models.dart';
import 'type_extractor.dart';

/// Visitor that extracts comprehensive metadata from a Dart compilation unit.
///
/// This visitor extracts:
/// - Library directives
/// - Import/export directives
/// - Part directives
/// - All public declarations (classes, mixins, enums, extensions, typedefs, functions, variables)
class FileMetadataVisitor extends RecursiveAstVisitor<void> {
  FileMetadataVisitor({
    this.analyzeBodies = true,
    this.includePrivate = false,
    this.lineInfo,
  });

  /// Whether to analyze function/method bodies for references.
  final bool analyzeBodies;

  /// Whether to include private declarations (names starting with _).
  final bool includePrivate;

  /// Line info for computing source locations.
  final LineInfo? lineInfo;

  /// The extracted metadata.
  final FileMetadata metadata = FileMetadata();

  /// Helper to check if a name should be included based on visibility.
  bool _shouldInclude(String name) => includePrivate || !name.startsWith('_');

  /// Gets source location from an AST node, including end line.
  SourceLocation? _getLocation(AstNode node) {
    if (lineInfo == null) {
      return SourceLocation(line: 0, column: 0, offset: node.offset);
    }
    final CharacterLocation loc = lineInfo!.getLocation(node.offset);
    final CharacterLocation endLoc = lineInfo!.getLocation(node.end - 1);
    return SourceLocation(
      line: loc.lineNumber,
      column: loc.columnNumber,
      offset: node.offset,
      endLine: endLoc.lineNumber,
    );
  }

  /// Extracts documentation comment text.
  String? _getDocumentation(Comment? comment) {
    if (comment == null || comment.tokens.isEmpty) {
      return null;
    }
    return comment.tokens.map((t) => t.lexeme).join('\n');
  }

  /// Extracts annotations from metadata.
  List<AnnotationInfo> _getAnnotations(NodeList<Annotation> metadata) {
    return metadata.map((Annotation a) {
      return AnnotationInfo(
        name: a.name.name,
        arguments: a.arguments?.toSource(),
      );
    }).toList();
  }

  /// Checks if a declaration has @override annotation.
  bool _hasOverride(NodeList<Annotation> metadata) {
    return metadata.any((Annotation a) => a.name.name == 'override');
  }

  /// Extracts @Native annotation info if present.
  NativeInfo? _extractNativeInfo(NodeList<Annotation> metadata) {
    for (final Annotation a in metadata) {
      if (a.name.name == 'Native') {
        String? symbol;
        bool isLeaf = false;

        // Parse the annotation arguments
        final ArgumentList? args = a.arguments;
        if (args != null) {
          for (final Expression arg in args.arguments) {
            if (arg is NamedExpression) {
              final String name = arg.name.label.name;
              final Expression value = arg.expression;
              if (name == 'symbol' && value is SimpleStringLiteral) {
                symbol = value.value;
              } else if (name == 'isLeaf' && value is BooleanLiteral) {
                isLeaf = value.value;
              }
            }
          }
        }

        return NativeInfo(symbol: symbol, isLeaf: isLeaf);
      }
    }
    return null;
  }

  // ============================================================
  // File-level directives
  // ============================================================

  @override
  void visitLibraryDirective(LibraryDirective node) {
    metadata.library.name = node.name2?.name;
    super.visitLibraryDirective(node);
  }

  @override
  void visitImportDirective(ImportDirective node) {
    final List<String> showNames = <String>[];
    final List<String> hideNames = <String>[];

    for (final Combinator combinator in node.combinators) {
      if (combinator is ShowCombinator) {
        showNames.addAll(combinator.shownNames.map((n) => n.name));
      } else if (combinator is HideCombinator) {
        hideNames.addAll(combinator.hiddenNames.map((n) => n.name));
      }
    }

    metadata.imports.add(ImportInfo(
      uri: node.uri.stringValue ?? node.uri.toSource(),
      prefix: node.prefix?.name,
      showNames: showNames,
      hideNames: hideNames,
      isDeferred: node.deferredKeyword != null,
    ));
    super.visitImportDirective(node);
  }

  @override
  void visitExportDirective(ExportDirective node) {
    final List<String> showNames = <String>[];
    final List<String> hideNames = <String>[];

    for (final Combinator combinator in node.combinators) {
      if (combinator is ShowCombinator) {
        showNames.addAll(combinator.shownNames.map((n) => n.name));
      } else if (combinator is HideCombinator) {
        hideNames.addAll(combinator.hiddenNames.map((n) => n.name));
      }
    }

    metadata.exports.add(ExportInfo(
      uri: node.uri.stringValue ?? node.uri.toSource(),
      showNames: showNames,
      hideNames: hideNames,
    ));
    super.visitExportDirective(node);
  }

  @override
  void visitPartDirective(PartDirective node) {
    metadata.parts.add(node.uri.stringValue ?? node.uri.toSource());
    super.visitPartDirective(node);
  }

  @override
  void visitPartOfDirective(PartOfDirective node) {
    // Can be either 'part of "uri";' or 'part of library.name;'
    if (node.uri != null) {
      metadata.partOf = node.uri!.stringValue ?? node.uri!.toSource();
    } else if (node.libraryName != null) {
      metadata.partOf = node.libraryName!.name;
    }
    super.visitPartOfDirective(node);
  }

  // ============================================================
  // Class declarations
  // ============================================================

  @override
  void visitClassDeclaration(ClassDeclaration node) {
    if (!_shouldInclude(node.name.lexeme)) {
      return; // Skip private classes
    }

    final ClassDecl classDecl = ClassDecl(
      name: node.name.lexeme,
      kind: _getClassKind(node),
      typeParameters: TypeExtractor.extractTypeParameters(node.typeParameters),
      superclass: TypeExtractor.extractSuperclass(node.extendsClause),
      interfaces: TypeExtractor.extractInterfaces(node.implementsClause),
      mixins: TypeExtractor.extractMixins(node.withClause),
      annotations: _getAnnotations(node.metadata),
      documentation: _getDocumentation(node.documentationComment),
      location: _getLocation(node),
      constructors: _extractConstructors(node.members),
      methods: _extractMethods(node.members),
      fields: _extractFields(node.members),
      accessors: _extractAccessors(node.members),
    );

    metadata.declarations.classes.add(classDecl);
    // Don't call super - we've already processed members
  }

  String _getClassKind(ClassDeclaration node) {
    final List<String> modifiers = <String>[];
    if (node.abstractKeyword != null) modifiers.add('abstract');
    if (node.sealedKeyword != null) modifiers.add('sealed');
    if (node.finalKeyword != null) modifiers.add('final');
    if (node.baseKeyword != null) modifiers.add('base');
    if (node.interfaceKeyword != null) modifiers.add('interface');
    if (node.mixinKeyword != null) modifiers.add('mixin');
    modifiers.add('class');
    return modifiers.join(' ');
  }

  // ============================================================
  // Mixin declarations
  // ============================================================

  @override
  void visitMixinDeclaration(MixinDeclaration node) {
    if (!_shouldInclude(node.name.lexeme)) {
      return;
    }

    final MixinDecl mixinDecl = MixinDecl(
      name: node.name.lexeme,
      typeParameters: TypeExtractor.extractTypeParameters(node.typeParameters),
      onTypes: TypeExtractor.extractOnTypes(node.onClause),
      interfaces: TypeExtractor.extractInterfaces(node.implementsClause),
      annotations: _getAnnotations(node.metadata),
      documentation: _getDocumentation(node.documentationComment),
      location: _getLocation(node),
      methods: _extractMethods(node.members),
      fields: _extractFields(node.members),
      accessors: _extractAccessors(node.members),
    );

    metadata.declarations.mixins.add(mixinDecl);
  }

  // ============================================================
  // Enum declarations
  // ============================================================

  @override
  void visitEnumDeclaration(EnumDeclaration node) {
    if (!_shouldInclude(node.name.lexeme)) {
      return;
    }

    final List<EnumValue> values = node.constants
        .where((EnumConstantDeclaration c) => _shouldInclude(c.name.lexeme))
        .map((EnumConstantDeclaration c) {
      return EnumValue(
        name: c.name.lexeme,
        arguments: c.arguments?.toSource(),
        documentation: _getDocumentation(c.documentationComment),
      );
    }).toList();

    final EnumDecl enumDecl = EnumDecl(
      name: node.name.lexeme,
      typeParameters: TypeExtractor.extractTypeParameters(node.typeParameters),
      interfaces: TypeExtractor.extractInterfaces(node.implementsClause),
      mixins: TypeExtractor.extractMixins(node.withClause),
      annotations: _getAnnotations(node.metadata),
      documentation: _getDocumentation(node.documentationComment),
      location: _getLocation(node),
      values: values,
      constructors: _extractConstructors(node.members),
      methods: _extractMethods(node.members),
      fields: _extractFields(node.members),
    );

    metadata.declarations.enums.add(enumDecl);
  }

  // ============================================================
  // Extension declarations
  // ============================================================

  @override
  void visitExtensionDeclaration(ExtensionDeclaration node) {
    // Extensions can be unnamed
    if (node.name != null && !_shouldInclude(node.name!.lexeme)) {
      return;
    }

    // Get the extended type from the onClause
    TypeRef extendedType;
    if (node.onClause != null) {
      extendedType = TypeExtractor.fromTypeAnnotation(node.onClause!.extendedType);
    } else {
      extendedType = TypeRef(name: 'dynamic');
    }

    final ExtensionDecl extensionDecl = ExtensionDecl(
      name: node.name?.lexeme,
      typeParameters: TypeExtractor.extractTypeParameters(node.typeParameters),
      extendedType: extendedType,
      annotations: _getAnnotations(node.metadata),
      documentation: _getDocumentation(node.documentationComment),
      location: _getLocation(node),
      methods: _extractMethods(node.members),
      fields: _extractFields(node.members),
      accessors: _extractAccessors(node.members),
    );

    metadata.declarations.extensions.add(extensionDecl);
  }

  // ============================================================
  // Extension type declarations
  // ============================================================

  @override
  void visitExtensionTypeDeclaration(ExtensionTypeDeclaration node) {
    if (!_shouldInclude(node.name.lexeme)) {
      return;
    }

    final ExtensionTypeDecl extensionTypeDecl = ExtensionTypeDecl(
      name: node.name.lexeme,
      typeParameters: TypeExtractor.extractTypeParameters(node.typeParameters),
      representationType: TypeExtractor.fromTypeAnnotation(
        node.representation.fieldType,
      ),
      interfaces: TypeExtractor.extractInterfaces(node.implementsClause),
      annotations: _getAnnotations(node.metadata),
      documentation: _getDocumentation(node.documentationComment),
      location: _getLocation(node),
      constructors: _extractConstructors(node.members),
      methods: _extractMethods(node.members),
      fields: _extractFields(node.members),
      accessors: _extractAccessors(node.members),
    );

    metadata.declarations.extensionTypes.add(extensionTypeDecl);
  }

  // ============================================================
  // Typedef declarations
  // ============================================================

  @override
  void visitGenericTypeAlias(GenericTypeAlias node) {
    if (!_shouldInclude(node.name.lexeme)) {
      return;
    }

    final TypedefDecl typedefDecl = TypedefDecl(
      name: node.name.lexeme,
      typeParameters: TypeExtractor.extractTypeParameters(node.typeParameters),
      aliasedType: TypeExtractor.fromTypeAnnotation(node.type),
      annotations: _getAnnotations(node.metadata),
      documentation: _getDocumentation(node.documentationComment),
      location: _getLocation(node),
    );

    metadata.declarations.typedefs.add(typedefDecl);
  }

  @override
  void visitFunctionTypeAlias(FunctionTypeAlias node) {
    if (!_shouldInclude(node.name.lexeme)) {
      return;
    }

    // Build the function type representation
    final String returnTypeStr = node.returnType?.toSource() ?? 'dynamic';
    final String paramsStr = node.parameters.toSource();
    final String typeParamsStr = node.typeParameters?.toSource() ?? '';
    final String aliasedTypeStr = '$returnTypeStr Function$typeParamsStr$paramsStr';

    final TypedefDecl typedefDecl = TypedefDecl(
      name: node.name.lexeme,
      typeParameters: TypeExtractor.extractTypeParameters(node.typeParameters),
      aliasedType: TypeRef(name: aliasedTypeStr),
      annotations: _getAnnotations(node.metadata),
      documentation: _getDocumentation(node.documentationComment),
      location: _getLocation(node),
    );

    metadata.declarations.typedefs.add(typedefDecl);
  }

  // ============================================================
  // Top-level function declarations
  // ============================================================

  @override
  void visitFunctionDeclaration(FunctionDeclaration node) {
    if (!_shouldInclude(node.name.lexeme)) {
      return;
    }

    // Skip getters/setters at top level - they're handled as accessors
    if (node.isGetter || node.isSetter) {
      // For top-level, treat as variable-like
      return;
    }

    final FunctionExpression func = node.functionExpression;
    final List<String> modifiers = <String>[];
    if (func.body.isAsynchronous) {
      if (func.body.isGenerator) {
        modifiers.add('async*');
      } else {
        modifiers.add('async');
      }
    } else if (func.body.isGenerator) {
      modifiers.add('sync*');
    }

    final FunctionDecl functionDecl = FunctionDecl(
      name: node.name.lexeme,
      returnType: TypeExtractor.fromTypeAnnotation(node.returnType),
      typeParameters: TypeExtractor.extractTypeParameters(func.typeParameters),
      parameters: _extractParameters(func.parameters),
      modifiers: modifiers,
      annotations: _getAnnotations(node.metadata),
      documentation: _getDocumentation(node.documentationComment),
      location: _getLocation(node),
      bodyReferences: analyzeBodies ? _extractBodyReferences(func.body) : null,
    );

    metadata.declarations.functions.add(functionDecl);
  }

  // ============================================================
  // Top-level variable declarations
  // ============================================================

  @override
  void visitTopLevelVariableDeclaration(TopLevelVariableDeclaration node) {
    for (final VariableDeclaration variable in node.variables.variables) {
      if (!_shouldInclude(variable.name.lexeme)) {
        continue;
      }

      // Extract dependencies from the initializer expression
      final List<TypeRef> dependencies = <TypeRef>[];
      if (variable.initializer != null) {
        final _InitializerDependencyVisitor depVisitor = _InitializerDependencyVisitor();
        variable.initializer!.accept(depVisitor);
        dependencies.addAll(depVisitor.dependencies);
      }

      final VariableDecl variableDecl = VariableDecl(
        name: variable.name.lexeme,
        type: TypeExtractor.fromTypeAnnotation(node.variables.type),
        isFinal: node.variables.isFinal,
        isConst: node.variables.isConst,
        isLate: node.variables.isLate,
        annotations: _getAnnotations(node.metadata),
        documentation: _getDocumentation(node.documentationComment),
        location: _getLocation(node),
        dependencies: dependencies,
      );

      metadata.declarations.topLevelVariables.add(variableDecl);
    }
    super.visitTopLevelVariableDeclaration(node);
  }

  // ============================================================
  // Member extraction helpers
  // ============================================================

  List<ConstructorDecl> _extractConstructors(NodeList<ClassMember> members) {
    final List<ConstructorDecl> constructors = <ConstructorDecl>[];

    for (final ClassMember member in members) {
      if (member is ConstructorDeclaration) {
        // Skip private constructors
        if (member.name != null && !_shouldInclude(member.name!.lexeme)) {
          continue;
        }

        String? redirectsTo;
        String? superCall;

        // Check for redirecting constructor
        if (member.redirectedConstructor != null) {
          redirectsTo = member.redirectedConstructor!.toSource();
        }

        // Check for super constructor call
        for (final ConstructorInitializer init in member.initializers) {
          if (init is SuperConstructorInvocation) {
            superCall = init.toSource();
            break;
          }
        }

        constructors.add(ConstructorDecl(
          name: member.name?.lexeme,
          isConst: member.constKeyword != null,
          isFactory: member.factoryKeyword != null,
          parameters: _extractParameters(member.parameters),
          annotations: _getAnnotations(member.metadata),
          documentation: _getDocumentation(member.documentationComment),
          location: _getLocation(member),
          redirectsTo: redirectsTo,
          superConstructorCall: superCall,
          bodyReferences: analyzeBodies ? _extractBodyReferences(member.body) : null,
        ));
      }
    }

    return constructors;
  }

  List<MethodDecl> _extractMethods(NodeList<ClassMember> members) {
    final List<MethodDecl> methods = <MethodDecl>[];

    for (final ClassMember member in members) {
      if (member is MethodDeclaration) {
        if (!_shouldInclude(member.name.lexeme)) {
          continue;
        }

        // Skip getters and setters
        if (member.isGetter || member.isSetter) {
          continue;
        }

        final List<String> modifiers = <String>[];
        if (member.body.isAsynchronous) {
          if (member.body.isGenerator) {
            modifiers.add('async*');
          } else {
            modifiers.add('async');
          }
        } else if (member.body.isGenerator) {
          modifiers.add('sync*');
        }

        methods.add(MethodDecl(
          name: member.name.lexeme,
          returnType: TypeExtractor.fromTypeAnnotation(member.returnType),
          typeParameters: TypeExtractor.extractTypeParameters(member.typeParameters),
          parameters: _extractParameters(member.parameters),
          isStatic: member.isStatic,
          isAbstract: member.isAbstract,
          isOverride: _hasOverride(member.metadata),
          isOperator: member.isOperator,
          modifiers: modifiers,
          annotations: _getAnnotations(member.metadata),
          documentation: _getDocumentation(member.documentationComment),
          location: _getLocation(member),
          bodyReferences: analyzeBodies ? _extractBodyReferences(member.body) : null,
          nativeInfo: _extractNativeInfo(member.metadata),
        ));
      }
    }

    return methods;
  }

  List<FieldDecl> _extractFields(NodeList<ClassMember> members) {
    final List<FieldDecl> fields = <FieldDecl>[];

    for (final ClassMember member in members) {
      if (member is FieldDeclaration) {
        for (final VariableDeclaration variable in member.fields.variables) {
          if (!_shouldInclude(variable.name.lexeme)) {
            continue;
          }

          fields.add(FieldDecl(
            name: variable.name.lexeme,
            type: TypeExtractor.fromTypeAnnotation(member.fields.type),
            isStatic: member.isStatic,
            isFinal: member.fields.isFinal,
            isConst: member.fields.isConst,
            isLate: member.fields.isLate,
            isOverride: _hasOverride(member.metadata),
            annotations: _getAnnotations(member.metadata),
            documentation: _getDocumentation(member.documentationComment),
            location: _getLocation(member),
          ));
        }
      }
    }

    return fields;
  }

  List<AccessorDecl> _extractAccessors(NodeList<ClassMember> members) {
    final List<AccessorDecl> accessors = <AccessorDecl>[];

    for (final ClassMember member in members) {
      if (member is MethodDeclaration && (member.isGetter || member.isSetter)) {
        if (!_shouldInclude(member.name.lexeme)) {
          continue;
        }

        accessors.add(AccessorDecl(
          name: member.name.lexeme,
          kind: member.isGetter ? 'getter' : 'setter',
          type: TypeExtractor.fromTypeAnnotation(member.returnType),
          isStatic: member.isStatic,
          isAbstract: member.isAbstract,
          annotations: _getAnnotations(member.metadata),
          documentation: _getDocumentation(member.documentationComment),
          location: _getLocation(member),
          bodyReferences: analyzeBodies ? _extractBodyReferences(member.body) : null,
        ));
      }
    }

    return accessors;
  }

  // ============================================================
  // Parameter extraction
  // ============================================================

  List<Parameter> _extractParameters(FormalParameterList? params) {
    if (params == null) {
      return <Parameter>[];
    }

    return params.parameters.map((FormalParameter p) {
      return _extractParameter(p);
    }).toList();
  }

  Parameter _extractParameter(FormalParameter param) {
    String name = '';
    TypeRef type = TypeRef(name: 'dynamic');
    String? defaultValue;
    List<String> annotations = <String>[];

    // Handle default parameters (which wrap other parameter types)
    FormalParameter actualParam = param;
    if (param is DefaultFormalParameter) {
      defaultValue = param.defaultValue?.toSource();
      actualParam = param.parameter;
    }

    // Extract annotations
    annotations = actualParam.metadata.map((a) => a.name.name).toList();

    // Try to get the resolved type from the parameter element first
    // This handles cases like `this.field` where type is inferred
    final ParameterElement? paramElement = actualParam.declaredElement;
    if (paramElement != null) {
      final DartType dartType = paramElement.type;
      // Skip InvalidType - fall back to annotation-based extraction
      if (dartType.toString() != 'dynamic' && dartType.toString() != 'InvalidType') {
        type = TypeExtractor.fromDartType(dartType);
      }
    }

    // Handle different parameter types
    if (actualParam is SimpleFormalParameter) {
      name = actualParam.name?.lexeme ?? '';
      // Only override if we didn't get a good type from element
      if (type.name == 'dynamic' && actualParam.type != null) {
        type = TypeExtractor.fromTypeAnnotation(actualParam.type);
      }
    } else if (actualParam is FunctionTypedFormalParameter) {
      name = actualParam.name.lexeme;
      // For function-typed params, build the type string if not resolved
      if (type.name == 'dynamic') {
        final String returnTypeStr = actualParam.returnType?.toSource() ?? 'dynamic';
        final String paramsStr = actualParam.parameters.toSource();
        type = TypeRef(name: '$returnTypeStr Function$paramsStr');
      }
    } else if (actualParam is FieldFormalParameter) {
      name = actualParam.name.lexeme;
      // Type should be resolved from element above
      if (type.name == 'dynamic' && actualParam.type != null) {
        type = TypeExtractor.fromTypeAnnotation(actualParam.type);
      }
    } else if (actualParam is SuperFormalParameter) {
      name = actualParam.name.lexeme;
      // Type should be resolved from element above
      if (type.name == 'dynamic' && actualParam.type != null) {
        type = TypeExtractor.fromTypeAnnotation(actualParam.type);
      }
    }

    return Parameter(
      name: name,
      type: type,
      isRequired: param.isRequired,
      isNamed: param.isNamed,
      isPositional: param.isPositional,
      defaultValue: defaultValue,
      annotations: annotations,
    );
  }

  // ============================================================
  // Body reference extraction (shallow analysis)
  // ============================================================

  BodyReferences? _extractBodyReferences(FunctionBody body) {
    if (body is EmptyFunctionBody) {
      return null;
    }

    final BodyReferences refs = BodyReferences();
    final _BodyReferenceVisitor visitor = _BodyReferenceVisitor(refs);
    body.accept(visitor);

    return refs.isEmpty ? null : refs;
  }
}

/// Internal visitor for extracting references from function bodies.
class _BodyReferenceVisitor extends RecursiveAstVisitor<void> {
  _BodyReferenceVisitor(this.refs);

  final BodyReferences refs;

  @override
  void visitInstanceCreationExpression(InstanceCreationExpression node) {
    final String typeName = node.constructorName.type.toSource();
    final String? constructorName = node.constructorName.name?.name;

    // Try to get the fully qualified type name
    String? fullyQualifiedType;
    final DartType? staticType = node.staticType;
    if (staticType != null && staticType.toString() != 'InvalidType') {
      final Element? element = staticType.element;
      if (element != null && element.library != null) {
        final String libraryUri = element.library!.source.uri.toString();
        final String? elementName = element.name;
        if (elementName != null) {
          fullyQualifiedType = '$libraryUri#$elementName';
        }
      }
    }

    refs.constructorCalls.add(ConstructorCallRef(
      type: typeName,
      fullyQualifiedType: fullyQualifiedType,
      constructorName: constructorName,
    ));

    super.visitInstanceCreationExpression(node);
  }

  /// Extracts target type info from a DartType, returns (displayName, fullyQualifiedName)
  (String?, String?) _extractTargetTypeInfo(DartType? dartType) {
    if (dartType == null) {
      return (null, null);
    }
    final String typeStr = dartType.getDisplayString();
    if (typeStr == 'InvalidType') {
      return (null, null);
    }

    String? fullyQualified;
    final Element? element = dartType.element;
    if (element != null && element.library != null) {
      final String libraryUri = element.library!.source.uri.toString();
      final String? elementName = element.name;
      if (elementName != null) {
        fullyQualified = '$libraryUri#$elementName';
      }
    }

    return (typeStr, fullyQualified);
  }

  @override
  void visitMethodInvocation(MethodInvocation node) {
    // Check if this is a prefix-qualified top-level function call (e.g., math.cos())
    if (node.target is SimpleIdentifier) {
      final SimpleIdentifier target = node.target! as SimpleIdentifier;
      final Element? targetElement = target.staticElement;

      if (targetElement is PrefixElement) {
        // This is a prefix-qualified function call like math.cos()
        final Element? functionElement = node.methodName.staticElement;
        String? fullyQualifiedName;
        String? sourcePath;

        if (functionElement != null && functionElement.library != null) {
          final String libraryUri = functionElement.library!.source.uri.toString();
          final String? funcName = functionElement.name;
          if (funcName != null) {
            fullyQualifiedName = '$libraryUri#$funcName';
          }
          // Always capture source path - needed for cross-file deps in same library
          final Uri sourceUri = functionElement.source!.uri;
          sourcePath = sourceUri.path;
        }

        refs.functionCalls.add(FunctionCallRef(
          prefix: target.name,
          functionName: node.methodName.name,
          fullyQualifiedName: fullyQualifiedName,
          sourcePath: sourcePath,
        ));

        super.visitMethodInvocation(node);
        return;
      }
    }

    // Check if this is an unqualified top-level function call (no target)
    if (node.target == null) {
      final Element? functionElement = node.methodName.staticElement;

      // Check if this is a top-level function (not a method on 'this')
      if (functionElement is FunctionElement) {
        String? fullyQualifiedName;
        final String libraryUri = functionElement.library.source.uri.toString();
        fullyQualifiedName = '$libraryUri#${functionElement.name}';

        // Always capture source path - needed for cross-file deps in same library
        final Uri sourceUri = functionElement.source.uri;
        final String sourcePath = sourceUri.path;

        refs.functionCalls.add(FunctionCallRef(
          functionName: node.methodName.name,
          fullyQualifiedName: fullyQualifiedName,
          sourcePath: sourcePath,
        ));

        super.visitMethodInvocation(node);
        return;
      }
    }

    // Regular method call on an object
    String? targetType;
    String? fullyQualifiedTargetType;

    // Try to get target type from static type if available
    if (node.realTarget != null) {
      final Expression target = node.realTarget!;
      final (String? typeName, String? fqName) = _extractTargetTypeInfo(target.staticType);
      targetType = typeName;
      fullyQualifiedTargetType = fqName;

      // Fallback to identifier name if type not resolved
      if (targetType == null && target is SimpleIdentifier) {
        targetType = target.name;
      }
    }

    // Check if it's a static call (target is a type name)
    final bool isStatic = node.target is SimpleIdentifier &&
        (node.target as SimpleIdentifier).name[0].toUpperCase() ==
            (node.target as SimpleIdentifier).name[0];

    refs.methodCalls.add(MethodCallRef(
      targetType: targetType,
      fullyQualifiedTargetType: fullyQualifiedTargetType,
      methodName: node.methodName.name,
      isStatic: isStatic,
    ));

    super.visitMethodInvocation(node);
  }

  @override
  void visitPrefixedIdentifier(PrefixedIdentifier node) {
    // Property access via prefix.identifier
    final (String? targetType, String? fullyQualifiedTargetType) =
        _extractTargetTypeInfo(node.prefix.staticType);

    refs.propertyAccesses.add(PropertyAccessRef(
      targetType: targetType,
      fullyQualifiedTargetType: fullyQualifiedTargetType,
      propertyName: node.identifier.name,
    ));

    super.visitPrefixedIdentifier(node);
  }

  @override
  void visitPropertyAccess(PropertyAccess node) {
    final (String? targetType, String? fullyQualifiedTargetType) =
        _extractTargetTypeInfo(node.realTarget.staticType);

    refs.propertyAccesses.add(PropertyAccessRef(
      targetType: targetType,
      fullyQualifiedTargetType: fullyQualifiedTargetType,
      propertyName: node.propertyName.name,
    ));

    super.visitPropertyAccess(node);
  }

  @override
  void visitIsExpression(IsExpression node) {
    refs.typeChecks.add(node.type.toSource());
    super.visitIsExpression(node);
  }

  @override
  void visitAsExpression(AsExpression node) {
    refs.typeCasts.add(node.type.toSource());
    super.visitAsExpression(node);
  }
}

/// Internal visitor for extracting type dependencies from initializer expressions.
///
/// This is used for top-level variable declarations to track which types
/// they depend on. For example:
///   `final ChannelBuffers channelBuffers = ChannelBuffers();`
/// would have a dependency on `ChannelBuffers`.
class _InitializerDependencyVisitor extends RecursiveAstVisitor<void> {
  _InitializerDependencyVisitor();

  /// The extracted type dependencies.
  final List<TypeRef> dependencies = <TypeRef>[];

  /// Track types we've already added to avoid duplicates.
  final Set<String> _seenTypes = <String>{};

  void _addDependency(TypeRef typeRef) {
    // Skip dynamic and basic types
    if (typeRef.name == 'dynamic' || typeRef.name == 'void') {
      return;
    }
    // Avoid duplicates
    final String key = typeRef.fullyQualifiedName ?? typeRef.name;
    if (_seenTypes.contains(key)) {
      return;
    }
    _seenTypes.add(key);
    dependencies.add(typeRef);
  }

  @override
  void visitInstanceCreationExpression(InstanceCreationExpression node) {
    // Extract the type being constructed
    final ConstructorName constructorName = node.constructorName;
    final NamedType type = constructorName.type;

    // Try to get the fully qualified type name
    String? fullyQualifiedType;
    final DartType? staticType = node.staticType;
    if (staticType != null && staticType.toString() != 'InvalidType') {
      final Element? element = staticType.element;
      if (element != null && element.library != null) {
        final String libraryUri = element.library!.source.uri.toString();
        final String? elementName = element.name;
        if (elementName != null) {
          fullyQualifiedType = '$libraryUri#$elementName';
        }
      }
    }

    _addDependency(TypeRef(
      name: type.name2.lexeme,
      fullyQualifiedName: fullyQualifiedType,
      isNullable: type.question != null,
    ));

    super.visitInstanceCreationExpression(node);
  }

  @override
  void visitMethodInvocation(MethodInvocation node) {
    // Check for static method calls like `SomeClass.create()`
    if (node.target is SimpleIdentifier) {
      final SimpleIdentifier target = node.target! as SimpleIdentifier;
      final Element? targetElement = target.staticElement;

      // If the target is a class (static method call)
      if (targetElement is InterfaceElement) {
        String? fullyQualifiedType;
        final LibraryElement? library = targetElement.library;
        if (library != null) {
          fullyQualifiedType = '${library.source.uri}#${targetElement.name}';
        }

        _addDependency(TypeRef(
          name: target.name,
          fullyQualifiedName: fullyQualifiedType,
        ));
      }
    }

    super.visitMethodInvocation(node);
  }

  @override
  void visitSimpleIdentifier(SimpleIdentifier node) {
    // Check if this identifier refers to a top-level variable or class
    final Element? element = node.staticElement;

    if (element is TopLevelVariableElement) {
      // This references another top-level variable - extract its type as dependency
      final DartType type = element.type;
      final TypeRef typeRef = TypeExtractor.fromDartType(type);
      _addDependency(typeRef);
    }

    super.visitSimpleIdentifier(node);
  }
}
