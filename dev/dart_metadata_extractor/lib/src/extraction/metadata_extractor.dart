// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:analyzer/dart/analysis/analysis_context.dart';
import 'package:analyzer/dart/analysis/analysis_context_collection.dart';
import 'package:analyzer/dart/analysis/results.dart';
import 'package:path/path.dart' as path;

import '../models/metadata_models.dart';
import '../visitors/file_metadata_visitor.dart';

/// Orchestrates the extraction of metadata from multiple Dart source files
/// with cross-file dependency analysis.
class MultiFileExtractor {
  MultiFileExtractor({
    required this.inputFiles,
    required this.outputPath,
    this.format = 'json',
    this.analyzeBodies = true,
    this.includePrivate = false,
    this.verbose = false,
    this.packageRoot,
    this.sdkPath,
  });

  /// List of input file paths to analyze.
  final List<String> inputFiles;

  /// Path to the output directory or file.
  final String outputPath;

  /// Data format: 'json' for full JSON, 'compact' for AI-readable text.
  final String format;

  /// Whether to analyze function bodies for references.
  final bool analyzeBodies;

  /// Whether to include private declarations (names starting with _).
  final bool includePrivate;

  /// Whether to print verbose output.
  final bool verbose;

  /// Optional package root for proper dependency resolution.
  final String? packageRoot;

  /// Optional SDK path for proper type resolution.
  final String? sdkPath;

  /// Runs the multi-file metadata extraction with dependency analysis.
  Future<MultiFileMetadata> run() async {
    if (inputFiles.isEmpty) {
      throw ArgumentError('No input files specified');
    }

    // Canonicalize all input paths
    final List<String> canonicalPaths = inputFiles
        .map((String p) => path.canonicalize(p))
        .where((String p) => p.endsWith('.dart'))
        .toList();

    if (canonicalPaths.isEmpty) {
      throw ArgumentError('No .dart files found in input');
    }

    // Find the common package root
    final String firstFile = canonicalPaths.first;
    final String analysisRoot = packageRoot != null
        ? path.canonicalize(packageRoot!)
        : _findPackageRoot(firstFile) ?? path.dirname(firstFile);

    if (verbose) {
      print('Analysis root: $analysisRoot');
      print('Processing ${canonicalPaths.length} files...');
    }

    // Create analysis context
    final AnalysisContextCollection collection = AnalysisContextCollection(
      includedPaths: <String>[analysisRoot],
      sdkPath: sdkPath,
    );

    final MultiFileMetadata result = MultiFileMetadata();

    // First pass: Extract metadata from all files
    final Map<String, FileMetadata> fileMetadataMap = <String, FileMetadata>{};

    for (final String filePath in canonicalPaths) {
      try {
        final AnalysisContext? context = _getContextForFile(collection, filePath);
        if (context == null) {
          if (verbose) {
            print('Warning: No context found for $filePath');
          }
          continue;
        }

        final SomeResolvedUnitResult unitResult =
            await context.currentSession.getResolvedUnit(filePath);

        if (unitResult is ResolvedUnitResult) {
          final FileMetadata metadata = _extractFileMetadata(unitResult);
          fileMetadataMap[filePath] = metadata;
          result.files.add(metadata);

          if (verbose) {
            print('Processed: ${path.basename(filePath)}');
          }
        } else {
          if (verbose) {
            print('Warning: Could not resolve $filePath');
          }
        }
      } catch (e) {
        if (verbose) {
          print('Error processing $filePath: $e');
        }
      }
    }

    // Second pass: Build cross-file dependency graph
    _buildGlobalDependencyGraph(result, fileMetadataMap);

    // Write output
    await _writeOutput(result);

    print('Successfully analyzed ${result.files.length} files');
    print('Migration order: ${result.globalDependencyGraph.migrationOrder.length} files');

    return result;
  }

  /// Gets the analysis context for a specific file.
  AnalysisContext? _getContextForFile(
    AnalysisContextCollection collection,
    String filePath,
  ) {
    for (final AnalysisContext context in collection.contexts) {
      if (context.contextRoot.isAnalyzed(filePath)) {
        return context;
      }
    }
    return null;
  }

  /// Extracts metadata from a resolved unit.
  FileMetadata _extractFileMetadata(ResolvedUnitResult unit) {
    final FileMetadataVisitor visitor = FileMetadataVisitor(
      analyzeBodies: analyzeBodies,
      includePrivate: includePrivate,
      lineInfo: unit.lineInfo,
    );

    unit.unit.accept(visitor);

    final FileMetadata metadata = visitor.metadata;
    metadata.filePath = unit.path;

    // Get library URI
    String? currentLibraryUri;
    try {
      currentLibraryUri = unit.libraryElement.source.uri.toString();
      metadata.library.uri = currentLibraryUri;
    } catch (_) {
      // Ignore
    }

    // Aggregate external dependencies
    _aggregateExternalDependencies(metadata, currentLibraryUri);

    // Build local dependency graph
    _buildDependencyGraph(metadata);

    return metadata;
  }

  /// Builds the global dependency graph across all files.
  void _buildGlobalDependencyGraph(
    MultiFileMetadata result,
    Map<String, FileMetadata> fileMetadataMap,
  ) {
    // Build a map of symbol -> source file path
    final Map<String, String> symbolToFile = <String, String>{};

    for (final MapEntry<String, FileMetadata> entry in fileMetadataMap.entries) {
      final String filePath = entry.key;
      final FileMetadata metadata = entry.value;

      // Collect all exported symbols from this file
      final List<String> exportedSymbols = <String>[];

      for (final ClassDecl cls in metadata.declarations.classes) {
        symbolToFile[cls.name] = filePath;
        exportedSymbols.add(cls.name);
      }
      for (final MixinDecl mixin in metadata.declarations.mixins) {
        symbolToFile[mixin.name] = filePath;
        exportedSymbols.add(mixin.name);
      }
      for (final EnumDecl enumDecl in metadata.declarations.enums) {
        symbolToFile[enumDecl.name] = filePath;
        exportedSymbols.add(enumDecl.name);
      }
      for (final ExtensionTypeDecl extType in metadata.declarations.extensionTypes) {
        symbolToFile[extType.name] = filePath;
        exportedSymbols.add(extType.name);
      }
      for (final TypedefDecl typedef in metadata.declarations.typedefs) {
        symbolToFile[typedef.name] = filePath;
        exportedSymbols.add(typedef.name);
      }
      for (final FunctionDecl func in metadata.declarations.functions) {
        symbolToFile[func.name] = filePath;
        exportedSymbols.add(func.name);
      }
      for (final VariableDecl variable in metadata.declarations.topLevelVariables) {
        symbolToFile[variable.name] = filePath;
        exportedSymbols.add(variable.name);
      }

      // Store exported symbols for later
      metadata.exportedSymbols = exportedSymbols;
    }

    // Build file dependency graph
    final Map<String, Set<String>> fileDependsOn = <String, Set<String>>{};
    final Map<String, Set<String>> fileDependedBy = <String, Set<String>>{};
    final Map<String, List<ImportedSymbol>> fileImportedSymbols =
        <String, List<ImportedSymbol>>{};

    for (final String filePath in fileMetadataMap.keys) {
      fileDependsOn[filePath] = <String>{};
      fileDependedBy[filePath] = <String>{};
      fileImportedSymbols[filePath] = <ImportedSymbol>[];
    }

    // Analyze dependencies
    for (final MapEntry<String, FileMetadata> entry in fileMetadataMap.entries) {
      final String filePath = entry.key;
      final FileMetadata metadata = entry.value;

      // Check external dependencies for cross-file references
      for (final ExternalDependency dep in metadata.externalDependencies) {
        // Check if this symbol comes from one of our analyzed files
        final String? sourceFile = symbolToFile[dep.symbol];
        if (sourceFile != null && sourceFile != filePath) {
          fileDependsOn[filePath]!.add(sourceFile);
          fileDependedBy[sourceFile]!.add(filePath);
          fileImportedSymbols[filePath]!.add(ImportedSymbol(
            symbol: dep.symbol,
            sourceFile: sourceFile,
            usageType: dep.usageType,
          ));
        }
      }

      // Also check imports for direct file references
      for (final ImportInfo import in metadata.imports) {
        // Try to resolve import URI to a file path
        for (final String otherPath in fileMetadataMap.keys) {
          if (otherPath != filePath && _importsFile(import.uri, otherPath)) {
            fileDependsOn[filePath]!.add(otherPath);
            fileDependedBy[otherPath]!.add(filePath);
          }
        }
      }
    }

    // Create FileDependencyInfo for each file
    for (final String filePath in fileMetadataMap.keys) {
      final FileMetadata metadata = fileMetadataMap[filePath]!;
      result.globalDependencyGraph.files.add(FileDependencyInfo(
        filePath: filePath,
        dependsOn: fileDependsOn[filePath]!.toList()..sort(),
        dependedBy: fileDependedBy[filePath]!.toList()..sort(),
        exportedSymbols: metadata.exportedSymbols ?? <String>[],
        importedSymbols: fileImportedSymbols[filePath]!,
      ));
    }

    // Topological sort for migration order
    final List<String> sortedFiles = _topologicalSortFiles(
      fileDependsOn,
      fileMetadataMap.keys.toSet(),
    );
    result.globalDependencyGraph.migrationOrder.addAll(sortedFiles);

    // Detect circular dependencies between files
    final List<List<String>> cycles = _detectFileCycles(fileDependsOn);
    result.globalDependencyGraph.circularDependencies.addAll(cycles);
  }

  /// Checks if an import URI refers to a specific file path.
  bool _importsFile(String importUri, String filePath) {
    // Handle relative imports
    if (importUri.endsWith('.dart')) {
      return filePath.endsWith(importUri) ||
          filePath.contains('/$importUri') ||
          path.basename(filePath) == path.basename(importUri);
    }
    // Handle package imports (simplified)
    if (importUri.startsWith('package:')) {
      final String packagePath = importUri.replaceFirst('package:', '');
      return filePath.contains(packagePath);
    }
    return false;
  }

  /// Topological sort for files using Kahn's algorithm.
  List<String> _topologicalSortFiles(
    Map<String, Set<String>> dependsOnMap,
    Set<String> allFiles,
  ) {
    final Map<String, int> inDegree = <String, int>{};
    for (final String file in allFiles) {
      inDegree[file] = dependsOnMap[file]?.length ?? 0;
    }

    final List<String> queue = <String>[];
    for (final String file in allFiles) {
      if (inDegree[file] == 0) {
        queue.add(file);
      }
    }
    queue.sort();

    final List<String> result = <String>[];

    while (queue.isNotEmpty) {
      final String file = queue.removeAt(0);
      result.add(file);

      for (final String other in allFiles) {
        if (dependsOnMap[other]?.contains(file) ?? false) {
          inDegree[other] = inDegree[other]! - 1;
          if (inDegree[other] == 0) {
            queue.add(other);
            queue.sort();
          }
        }
      }
    }

    // Add remaining files (in cycles)
    for (final String file in allFiles) {
      if (!result.contains(file)) {
        result.add(file);
      }
    }

    return result;
  }

  /// Detects cycles in the file dependency graph.
  List<List<String>> _detectFileCycles(Map<String, Set<String>> dependsOnMap) {
    final List<List<String>> cycles = <List<String>>[];
    final Set<String> visited = <String>{};
    final Set<String> inStack = <String>{};
    final List<String> stack = <String>[];

    void dfs(String node) {
      if (inStack.contains(node)) {
        final int cycleStart = stack.indexOf(node);
        if (cycleStart != -1) {
          final List<String> cycle = stack.sublist(cycleStart).toList()..add(node);
          // Normalize cycle
          final int minIndex = cycle.indexOf(
            cycle.reduce((String a, String b) => a.compareTo(b) < 0 ? a : b),
          );
          final List<String> normalized = <String>[
            ...cycle.sublist(minIndex, cycle.length - 1),
            ...cycle.sublist(0, minIndex),
          ];
          final bool alreadyFound = cycles.any((List<String> c) =>
              c.length == normalized.length &&
              c.asMap().entries.every((MapEntry<int, String> e) =>
                  e.value == normalized[e.key]));
          if (!alreadyFound) {
            cycles.add(normalized);
          }
        }
        return;
      }

      if (visited.contains(node)) {
        return;
      }

      visited.add(node);
      inStack.add(node);
      stack.add(node);

      for (final String dep in dependsOnMap[node] ?? <String>{}) {
        dfs(dep);
      }

      stack.removeLast();
      inStack.remove(node);
    }

    for (final String node in dependsOnMap.keys.toList()..sort()) {
      if (!visited.contains(node)) {
        dfs(node);
      }
    }

    return cycles;
  }

  /// Writes the output to file.
  Future<void> _writeOutput(MultiFileMetadata result) async {
    final String extension = format == 'compact' ? '.txt' : '.json';
    String outputFilePath = outputPath;

    // Determine the actual output file path
    if (outputPath.endsWith('/') || Directory(outputPath).existsSync()) {
      // Output is a directory - create a default filename
      outputFilePath = path.join(outputPath, 'multi_file_metadata$extension');
    } else if (!outputPath.endsWith('.json') && !outputPath.endsWith('.txt')) {
      // Output doesn't have a recognized extension - add one
      outputFilePath = '$outputPath$extension';
    }

    // Create parent directory if needed
    final String parentDir = path.dirname(outputFilePath);
    if (parentDir.isNotEmpty && parentDir != '.') {
      final Directory dir = Directory(parentDir);
      if (!dir.existsSync()) {
        dir.createSync(recursive: true);
      }
    }

    final File outputFile = File(outputFilePath);

    if (format == 'compact') {
      await outputFile.writeAsString(result.toCompactText());
    } else {
      const JsonEncoder encoder = JsonEncoder.withIndent('  ');
      await outputFile.writeAsString(encoder.convert(result.toJson()));
    }

    if (verbose) {
      print('Wrote: $outputFilePath');
    }
  }

  /// Finds the package root by looking for pubspec.yaml.
  String? _findPackageRoot(String startPath) {
    String current = startPath;

    if (FileSystemEntity.isFileSync(current)) {
      current = path.dirname(current);
    }

    while (current != path.dirname(current)) {
      final String pubspecPath = path.join(current, 'pubspec.yaml');
      if (File(pubspecPath).existsSync()) {
        return current;
      }
      current = path.dirname(current);
    }

    return null;
  }

  /// Aggregates external dependencies from all body references.
  void _aggregateExternalDependencies(FileMetadata metadata, String? currentLibraryUri) {
    final String currentFileName = metadata.filePath.split('/').last;

    void addIfExternal(String? fullyQualifiedName, String usageType, {String? sourcePath}) {
      if (fullyQualifiedName == null) return;

      final int hashIndex = fullyQualifiedName.indexOf('#');
      if (hashIndex == -1) return;

      final String libraryUri = fullyQualifiedName.substring(0, hashIndex);
      final String symbol = fullyQualifiedName.substring(hashIndex + 1);

      String? sourceFile;
      if (sourcePath != null) {
        final String fileName = sourcePath.split('/').last;
        if (fileName.endsWith('.dart')) {
          sourceFile = fileName;
        }
      }

      bool isCrossFileDep = false;
      if (currentLibraryUri != null && libraryUri == currentLibraryUri) {
        if (sourceFile == null || sourceFile == currentFileName) {
          return;
        }
        isCrossFileDep = true;
      }

      metadata.externalDependencies.add(ExternalDependency(
        library: libraryUri,
        symbol: symbol,
        usageType: usageType,
        sourcePath: isCrossFileDep ? sourceFile : null,
      ));
    }

    for (final ClassDecl classDecl in metadata.declarations.classes) {
      if (classDecl.superclass?.fullyQualifiedName != null) {
        addIfExternal(classDecl.superclass!.fullyQualifiedName, 'superclass');
      }
      for (final TypeRef iface in classDecl.interfaces) {
        addIfExternal(iface.fullyQualifiedName, 'interface');
      }
      for (final TypeRef mixin in classDecl.mixins) {
        addIfExternal(mixin.fullyQualifiedName, 'mixin');
      }
      for (final ConstructorDecl constructor in classDecl.constructors) {
        _processBodyReferences(constructor.bodyReferences, addIfExternal);
      }
      for (final MethodDecl method in classDecl.methods) {
        _processBodyReferences(method.bodyReferences, addIfExternal);
      }
      for (final AccessorDecl accessor in classDecl.accessors) {
        _processBodyReferences(accessor.bodyReferences, addIfExternal);
      }
      for (final FieldDecl field in classDecl.fields) {
        addIfExternal(field.type.fullyQualifiedName, 'type');
      }
    }

    for (final FunctionDecl func in metadata.declarations.functions) {
      _processBodyReferences(func.bodyReferences, addIfExternal);
      addIfExternal(func.returnType.fullyQualifiedName, 'type');
    }
  }

  /// Processes body references to extract external dependencies.
  void _processBodyReferences(
    BodyReferences? refs,
    void Function(String?, String, {String? sourcePath}) addIfExternal,
  ) {
    if (refs == null) return;

    for (final ConstructorCallRef call in refs.constructorCalls) {
      addIfExternal(call.fullyQualifiedType, 'constructor');
    }
    for (final MethodCallRef call in refs.methodCalls) {
      addIfExternal(call.fullyQualifiedTargetType, 'method');
    }
    for (final PropertyAccessRef access in refs.propertyAccesses) {
      addIfExternal(access.fullyQualifiedTargetType, 'property');
    }
    for (final FunctionCallRef call in refs.functionCalls) {
      addIfExternal(call.fullyQualifiedName, 'function', sourcePath: call.sourcePath);
    }
  }

  /// Builds the dependency graph for migration ordering.
  void _buildDependencyGraph(FileMetadata metadata) {
    final Set<String> localTypes = <String>{};
    for (final ClassDecl cls in metadata.declarations.classes) {
      localTypes.add(cls.name);
    }
    for (final MixinDecl mixin in metadata.declarations.mixins) {
      localTypes.add(mixin.name);
    }
    for (final EnumDecl enumDecl in metadata.declarations.enums) {
      localTypes.add(enumDecl.name);
    }
    for (final ExtensionTypeDecl extType in metadata.declarations.extensionTypes) {
      localTypes.add(extType.name);
    }

    final Map<String, Set<String>> dependsOnMap = <String, Set<String>>{};
    final Map<String, Set<String>> dependedByMap = <String, Set<String>>{};

    for (final String name in localTypes) {
      dependsOnMap[name] = <String>{};
      dependedByMap[name] = <String>{};
    }

    for (final ClassDecl cls in metadata.declarations.classes) {
      final Set<String> deps = dependsOnMap[cls.name]!;

      if (cls.superclass != null && localTypes.contains(cls.superclass!.name)) {
        deps.add(cls.superclass!.name);
      }

      for (final TypeRef iface in cls.interfaces) {
        if (localTypes.contains(iface.name)) {
          deps.add(iface.name);
        }
      }

      for (final TypeRef mixin in cls.mixins) {
        if (localTypes.contains(mixin.name)) {
          deps.add(mixin.name);
        }
      }

      deps.remove(cls.name);
    }

    for (final MapEntry<String, Set<String>> entry in dependsOnMap.entries) {
      for (final String dep in entry.value) {
        dependedByMap[dep]!.add(entry.key);
      }
    }

    for (final String name in localTypes) {
      metadata.dependencyGraph.classes.add(DependencyInfo(
        name: name,
        dependsOn: dependsOnMap[name]!.toList()..sort(),
        dependedBy: dependedByMap[name]!.toList()..sort(),
      ));
    }
  }
}

/// Orchestrates the extraction of metadata from Dart source files.
class MetadataExtractor {
  MetadataExtractor({
    required this.inputPath,
    required this.outputPath,
    this.outputFormat = 'per-file',
    this.format = 'json',
    this.analyzeBodies = true,
    this.includePrivate = false,
    this.verbose = false,
    this.packageRoot,
    this.sdkPath,
  });

  /// Path to the input directory or file to analyze.
  final String inputPath;

  /// Path to the output directory for JSON files.
  final String outputPath;

  /// Output format: 'per-file' for one JSON per Dart file, 'single' for merged.
  final String outputFormat;

  /// Data format: 'json' for full JSON, 'compact' for AI-readable text.
  final String format;

  /// Whether to analyze function bodies for references.
  final bool analyzeBodies;

  /// Whether to include private declarations (names starting with _).
  final bool includePrivate;

  /// Whether to print verbose output.
  final bool verbose;

  /// Optional package root for proper dependency resolution.
  /// If not provided, will try to find it automatically.
  final String? packageRoot;

  /// Optional SDK path for proper type resolution.
  /// If not provided, the analyzer will use the default SDK.
  final String? sdkPath;

  /// Runs the metadata extraction.
  Future<void> run() async {
    final String canonicalInput = path.canonicalize(inputPath);

    // Validate input path
    final FileSystemEntityType inputType = FileSystemEntity.typeSync(canonicalInput);
    if (inputType == FileSystemEntityType.notFound) {
      throw ArgumentError('Input path does not exist: $inputPath');
    }

    // Create output directory
    final Directory outputDir = Directory(outputPath);
    if (!outputDir.existsSync()) {
      outputDir.createSync(recursive: true);
    }

    // Find the package root for proper dependency resolution
    final String analysisRoot = packageRoot != null
        ? path.canonicalize(packageRoot!)
        : _findPackageRoot(canonicalInput) ?? canonicalInput;

    if (verbose) {
      print('Analysis root: $analysisRoot');
    }

    // Create analysis context rooted at the package directory
    final AnalysisContextCollection collection = AnalysisContextCollection(
      includedPaths: <String>[analysisRoot],
      sdkPath: sdkPath,
    );

    final List<FileMetadata> allMetadata = <FileMetadata>[];
    int processed = 0;
    int skipped = 0;

    // Process each file
    for (final AnalysisContext context in collection.contexts) {
      final Iterable<String> analyzedFiles = context.contextRoot.analyzedFiles();

      for (final String filePath in analyzedFiles) {
        if (!filePath.endsWith('.dart')) {
          continue;
        }

        // Only process files under the input path
        if (!_isUnderInputPath(filePath, canonicalInput)) {
          continue;
        }

        // Skip generated files
        if (filePath.endsWith('.g.dart') || filePath.endsWith('.freezed.dart')) {
          skipped++;
          continue;
        }

        try {
          final SomeResolvedUnitResult result =
              await context.currentSession.getResolvedUnit(filePath);

          if (result is ResolvedUnitResult) {
            final FileMetadata fileMetadata = _extractFileMetadata(result);
            allMetadata.add(fileMetadata);

            if (outputFormat == 'per-file') {
              await _writePerFileOutput(fileMetadata, canonicalInput);
            }

            processed++;
            if (verbose && processed % 50 == 0) {
              print('Processed $processed files...');
            }
          } else {
            if (verbose) {
              print('Warning: Could not resolve $filePath');
            }
            skipped++;
          }
        } catch (e) {
          if (verbose) {
            print('Error processing $filePath: $e');
          }
          skipped++;
        }
      }
    }

    // Write single merged output if requested
    if (outputFormat == 'single') {
      await _writeSingleOutput(allMetadata);
    }

    print('Successfully extracted metadata from $processed files');
    if (skipped > 0) {
      print('Skipped $skipped files');
    }
  }

  /// Extracts metadata from a resolved unit.
  FileMetadata _extractFileMetadata(ResolvedUnitResult unit) {
    final FileMetadataVisitor visitor = FileMetadataVisitor(
      analyzeBodies: analyzeBodies,
      includePrivate: includePrivate,
      lineInfo: unit.lineInfo,
    );

    unit.unit.accept(visitor);

    final FileMetadata metadata = visitor.metadata;
    metadata.filePath = unit.path;

    // Try to get the library URI
    String? currentLibraryUri;
    try {
      currentLibraryUri = unit.libraryElement.source.uri.toString();
      metadata.library.uri = currentLibraryUri;
    } catch (_) {
      // Ignore if we can't get the library URI
    }

    // Aggregate external dependencies from body references
    _aggregateExternalDependencies(metadata, currentLibraryUri);

    // Build dependency graph for migration ordering
    _buildDependencyGraph(metadata);

    return metadata;
  }

  /// Aggregates external dependencies from all body references.
  void _aggregateExternalDependencies(FileMetadata metadata, String? currentLibraryUri) {
    // Get current file name for comparison
    final String currentFileName = metadata.filePath.split('/').last;

    // Helper to add dependency if it's from an external library or different file
    void addIfExternal(String? fullyQualifiedName, String usageType, {String? sourcePath}) {
      if (fullyQualifiedName == null) return;

      // Parse "library_uri#symbol" format
      final int hashIndex = fullyQualifiedName.indexOf('#');
      if (hashIndex == -1) return;

      final String libraryUri = fullyQualifiedName.substring(0, hashIndex);
      final String symbol = fullyQualifiedName.substring(hashIndex + 1);

      // Extract just the filename from the source path
      // Only keep source file if it's a real .dart file (not SDK library names like "math")
      String? sourceFile;
      if (sourcePath != null) {
        final String fileName = sourcePath.split('/').last;
        if (fileName.endsWith('.dart')) {
          sourceFile = fileName;
        }
      }

      // Skip if it's from the same library AND the same file (or no source path info)
      // But include if it's from the same library but a DIFFERENT file (cross-file dep)
      bool isCrossFileDep = false;
      if (currentLibraryUri != null && libraryUri == currentLibraryUri) {
        // Same library - only include if we have a different source file
        if (sourceFile == null || sourceFile == currentFileName) {
          return; // Skip - same file or unknown source
        }
        // Different file in same library - this is a cross-file dependency we want to track
        isCrossFileDep = true;
      }

      metadata.externalDependencies.add(ExternalDependency(
        library: libraryUri,
        symbol: symbol,
        usageType: usageType,
        // Only include sourcePath for cross-file deps (same library, different file)
        sourcePath: isCrossFileDep ? sourceFile : null,
      ));
    }

    // Process all classes
    for (final ClassDecl classDecl in metadata.declarations.classes) {
      // Process superclass
      if (classDecl.superclass?.fullyQualifiedName != null) {
        addIfExternal(classDecl.superclass!.fullyQualifiedName, 'superclass');
      }
      // Process interfaces
      for (final TypeRef iface in classDecl.interfaces) {
        addIfExternal(iface.fullyQualifiedName, 'interface');
      }
      // Process mixins
      for (final TypeRef mixin in classDecl.mixins) {
        addIfExternal(mixin.fullyQualifiedName, 'mixin');
      }
      // Process constructor body references
      for (final ConstructorDecl constructor in classDecl.constructors) {
        _processBodyReferences(constructor.bodyReferences, addIfExternal);
      }
      // Process method body references
      for (final MethodDecl method in classDecl.methods) {
        _processBodyReferences(method.bodyReferences, addIfExternal);
      }
      // Process accessor body references
      for (final AccessorDecl accessor in classDecl.accessors) {
        _processBodyReferences(accessor.bodyReferences, addIfExternal);
      }
      // Process field types
      for (final FieldDecl field in classDecl.fields) {
        addIfExternal(field.type.fullyQualifiedName, 'type');
      }
    }

    // Process top-level functions
    for (final FunctionDecl func in metadata.declarations.functions) {
      _processBodyReferences(func.bodyReferences, addIfExternal);
      addIfExternal(func.returnType.fullyQualifiedName, 'type');
    }
  }

  /// Processes body references to extract external dependencies.
  void _processBodyReferences(
    BodyReferences? refs,
    void Function(String?, String, {String? sourcePath}) addIfExternal,
  ) {
    if (refs == null) return;

    for (final ConstructorCallRef call in refs.constructorCalls) {
      addIfExternal(call.fullyQualifiedType, 'constructor');
    }
    for (final MethodCallRef call in refs.methodCalls) {
      addIfExternal(call.fullyQualifiedTargetType, 'method');
    }
    for (final PropertyAccessRef access in refs.propertyAccesses) {
      addIfExternal(access.fullyQualifiedTargetType, 'property');
    }
    for (final FunctionCallRef call in refs.functionCalls) {
      addIfExternal(call.fullyQualifiedName, 'function', sourcePath: call.sourcePath);
    }
  }

  /// Builds the dependency graph for migration ordering.
  void _buildDependencyGraph(FileMetadata metadata) {
    // Collect all class/type names in this file
    final Set<String> localTypes = <String>{};
    for (final ClassDecl cls in metadata.declarations.classes) {
      localTypes.add(cls.name);
    }
    for (final MixinDecl mixin in metadata.declarations.mixins) {
      localTypes.add(mixin.name);
    }
    for (final EnumDecl enumDecl in metadata.declarations.enums) {
      localTypes.add(enumDecl.name);
    }
    for (final ExtensionTypeDecl extType in metadata.declarations.extensionTypes) {
      localTypes.add(extType.name);
    }

    // Build dependency map: className -> Set<dependsOnClassName>
    final Map<String, Set<String>> dependsOnMap = <String, Set<String>>{};
    final Map<String, Set<String>> dependedByMap = <String, Set<String>>{};

    // Initialize maps
    for (final String name in localTypes) {
      dependsOnMap[name] = <String>{};
      dependedByMap[name] = <String>{};
    }

    // Process each class to find dependencies
    for (final ClassDecl cls in metadata.declarations.classes) {
      final Set<String> deps = dependsOnMap[cls.name]!;

      // Superclass dependency
      if (cls.superclass != null && localTypes.contains(cls.superclass!.name)) {
        deps.add(cls.superclass!.name);
      }

      // Interface dependencies
      for (final TypeRef iface in cls.interfaces) {
        if (localTypes.contains(iface.name)) {
          deps.add(iface.name);
        }
      }

      // Mixin dependencies
      for (final TypeRef mixin in cls.mixins) {
        if (localTypes.contains(mixin.name)) {
          deps.add(mixin.name);
        }
      }

      // Dependencies from body references (constructors, methods, accessors, fields)
      _collectBodyDependencies(cls, localTypes, deps);

      // Remove self-reference (a class returning its own type isn't a dependency)
      deps.remove(cls.name);
    }

    // Build reverse map (dependedBy)
    for (final MapEntry<String, Set<String>> entry in dependsOnMap.entries) {
      for (final String dep in entry.value) {
        dependedByMap[dep]!.add(entry.key);
      }
    }

    // Create DependencyInfo objects for classes
    for (final String name in localTypes) {
      metadata.dependencyGraph.classes.add(DependencyInfo(
        name: name,
        dependsOn: dependsOnMap[name]!.toList()..sort(),
        dependedBy: dependedByMap[name]!.toList()..sort(),
      ));
    }

    // Process top-level variables for their dependencies
    for (final VariableDecl variable in metadata.declarations.topLevelVariables) {
      final List<String> varDeps = <String>[];
      for (final TypeRef dep in variable.dependencies) {
        if (localTypes.contains(dep.name)) {
          varDeps.add(dep.name);
          // Also update dependedBy for the type
          dependedByMap[dep.name]?.add(variable.name);
        }
      }
      // Also check the declared type
      if (localTypes.contains(variable.type.name)) {
        if (!varDeps.contains(variable.type.name)) {
          varDeps.add(variable.type.name);
          dependedByMap[variable.type.name]?.add(variable.name);
        }
      }
      if (varDeps.isNotEmpty) {
        metadata.dependencyGraph.variables.add(DependencyInfo(
          name: variable.name,
          dependsOn: varDeps..sort(),
          dependedBy: <String>[],
        ));
      }
    }

    // Perform topological sort for migration order
    final List<String> sorted = _topologicalSort(dependsOnMap, localTypes);
    metadata.dependencyGraph.migrationOrder.addAll(sorted);

    // Detect circular dependencies
    final List<List<String>> cycles = _detectCycles(dependsOnMap);
    metadata.dependencyGraph.circularDependencies.addAll(cycles);
  }

  /// Collects dependencies from a class's body references.
  void _collectBodyDependencies(
    ClassDecl cls,
    Set<String> localTypes,
    Set<String> deps,
  ) {
    // Helper to extract type name from body references
    void addFromBodyRefs(BodyReferences? refs) {
      if (refs == null) return;

      for (final ConstructorCallRef call in refs.constructorCalls) {
        // Extract type name from the constructor call
        final String typeName = call.type.split('<').first.split('.').last;
        if (localTypes.contains(typeName)) {
          deps.add(typeName);
        }
      }

      for (final MethodCallRef call in refs.methodCalls) {
        if (call.targetType != null) {
          final String typeName = call.targetType!.split('<').first.split('?').first;
          if (localTypes.contains(typeName)) {
            deps.add(typeName);
          }
        }
      }

      for (final PropertyAccessRef access in refs.propertyAccesses) {
        if (access.targetType != null) {
          final String typeName = access.targetType!.split('<').first.split('?').first;
          if (localTypes.contains(typeName)) {
            deps.add(typeName);
          }
        }
      }
    }

    // Process constructors
    for (final ConstructorDecl ctor in cls.constructors) {
      addFromBodyRefs(ctor.bodyReferences);
      // Also check parameter types
      for (final Parameter param in ctor.parameters) {
        final String typeName = param.type.name.split('<').first.split('?').first;
        if (localTypes.contains(typeName)) {
          deps.add(typeName);
        }
      }
    }

    // Process methods
    for (final MethodDecl method in cls.methods) {
      addFromBodyRefs(method.bodyReferences);
      // Check return type
      final String returnType = method.returnType.name.split('<').first.split('?').first;
      if (localTypes.contains(returnType)) {
        deps.add(returnType);
      }
      // Check parameter types
      for (final Parameter param in method.parameters) {
        final String typeName = param.type.name.split('<').first.split('?').first;
        if (localTypes.contains(typeName)) {
          deps.add(typeName);
        }
      }
    }

    // Process accessors
    for (final AccessorDecl accessor in cls.accessors) {
      addFromBodyRefs(accessor.bodyReferences);
      // Check accessor type
      final String typeName = accessor.type.name.split('<').first.split('?').first;
      if (localTypes.contains(typeName)) {
        deps.add(typeName);
      }
    }

    // Process field types
    for (final FieldDecl field in cls.fields) {
      final String typeName = field.type.name.split('<').first.split('?').first;
      if (localTypes.contains(typeName)) {
        deps.add(typeName);
      }
    }
  }

  /// Performs topological sort using Kahn's algorithm.
  /// Returns classes in order: dependencies first, dependents last.
  List<String> _topologicalSort(
    Map<String, Set<String>> dependsOnMap,
    Set<String> allNodes,
  ) {
    // Calculate in-degree (number of dependencies) for each node
    final Map<String, int> inDegree = <String, int>{};
    for (final String node in allNodes) {
      inDegree[node] = dependsOnMap[node]!.length;
    }

    // Queue of nodes with no dependencies
    final List<String> queue = <String>[];
    for (final String node in allNodes) {
      if (inDegree[node] == 0) {
        queue.add(node);
      }
    }
    queue.sort(); // Alphabetical for deterministic output

    final List<String> result = <String>[];

    while (queue.isNotEmpty) {
      final String node = queue.removeAt(0);
      result.add(node);

      // For each node that depends on this one, reduce its in-degree
      for (final String other in allNodes) {
        if (dependsOnMap[other]!.contains(node)) {
          inDegree[other] = inDegree[other]! - 1;
          if (inDegree[other] == 0) {
            queue.add(other);
            queue.sort(); // Keep sorted for deterministic output
          }
        }
      }
    }

    // If result doesn't contain all nodes, there's a cycle
    // Add remaining nodes at the end
    for (final String node in allNodes) {
      if (!result.contains(node)) {
        result.add(node);
      }
    }

    return result;
  }

  /// Detects cycles in the dependency graph using DFS.
  List<List<String>> _detectCycles(Map<String, Set<String>> dependsOnMap) {
    final List<List<String>> cycles = <List<String>>[];
    final Set<String> visited = <String>{};
    final Set<String> inStack = <String>{};
    final List<String> stack = <String>[];

    void dfs(String node) {
      if (inStack.contains(node)) {
        // Found a cycle - extract it from the stack
        final int cycleStart = stack.indexOf(node);
        if (cycleStart != -1) {
          final List<String> cycle = stack.sublist(cycleStart).toList()..add(node);
          // Normalize cycle (start from smallest element)
          final int minIndex = cycle.indexOf(cycle.reduce((a, b) => a.compareTo(b) < 0 ? a : b));
          final List<String> normalized = <String>[
            ...cycle.sublist(minIndex, cycle.length - 1),
            ...cycle.sublist(0, minIndex),
          ];
          // Check if we already have this cycle
          final bool alreadyFound = cycles.any((List<String> c) =>
              c.length == normalized.length &&
              c.asMap().entries.every((e) => e.value == normalized[e.key]));
          if (!alreadyFound) {
            cycles.add(normalized);
          }
        }
        return;
      }

      if (visited.contains(node)) {
        return;
      }

      visited.add(node);
      inStack.add(node);
      stack.add(node);

      for (final String dep in dependsOnMap[node] ?? <String>{}) {
        dfs(dep);
      }

      stack.removeLast();
      inStack.remove(node);
    }

    for (final String node in dependsOnMap.keys.toList()..sort()) {
      if (!visited.contains(node)) {
        dfs(node);
      }
    }

    return cycles;
  }

  /// Writes metadata for a single file to its own output file.
  Future<void> _writePerFileOutput(FileMetadata metadata, String inputBase) async {
    String relativePath;

    // Check if input is a file or directory
    if (FileSystemEntity.isFileSync(inputBase)) {
      // If input is a single file, just use its basename
      relativePath = path.basename(metadata.filePath);
    } else {
      // Calculate relative path from input directory
      relativePath = path.relative(metadata.filePath, from: inputBase);
    }

    // Replace .dart with appropriate extension
    final String extension = format == 'compact' ? '.txt' : '.json';
    relativePath = relativePath.replaceAll('.dart', extension);

    final String outputFilePath = path.join(outputPath, relativePath);

    // Create parent directories
    final Directory parentDir = Directory(path.dirname(outputFilePath));
    if (!parentDir.existsSync()) {
      parentDir.createSync(recursive: true);
    }

    // Write output in the appropriate format
    final File outputFile = File(outputFilePath);
    if (format == 'compact') {
      await outputFile.writeAsString(metadata.toCompactText());
    } else {
      const JsonEncoder encoder = JsonEncoder.withIndent('  ');
      await outputFile.writeAsString(encoder.convert(metadata.toJson()));
    }

    if (verbose) {
      print('Wrote: $outputFilePath');
    }
  }

  /// Writes all metadata to a single JSON file.
  Future<void> _writeSingleOutput(List<FileMetadata> allMetadata) async {
    final String outputFilePath = path.join(outputPath, 'metadata.json');
    final File outputFile = File(outputFilePath);

    const JsonEncoder encoder = JsonEncoder.withIndent('  ');
    final Map<String, dynamic> output = <String, dynamic>{
      'version': FileMetadata.schemaVersion,
      'fileCount': allMetadata.length,
      'files': allMetadata.map((FileMetadata m) => m.toJson()).toList(),
    };

    await outputFile.writeAsString(encoder.convert(output));
    print('Wrote: $outputFilePath');
  }

  /// Finds the package root by looking for pubspec.yaml.
  String? _findPackageRoot(String startPath) {
    String current = startPath;

    // If it's a file, start from its directory
    if (FileSystemEntity.isFileSync(current)) {
      current = path.dirname(current);
    }

    // Walk up the directory tree looking for pubspec.yaml
    while (current != path.dirname(current)) {
      final String pubspecPath = path.join(current, 'pubspec.yaml');
      if (File(pubspecPath).existsSync()) {
        return current;
      }
      current = path.dirname(current);
    }

    return null;
  }

  /// Checks if a file path is under the input path.
  bool _isUnderInputPath(String filePath, String inputPath) {
    final String normalizedFile = path.normalize(filePath);
    final String normalizedInput = path.normalize(inputPath);

    // If input is a file, check exact match
    if (FileSystemEntity.isFileSync(normalizedInput)) {
      return normalizedFile == normalizedInput;
    }

    // If input is a directory, check if file is under it
    return normalizedFile.startsWith(normalizedInput + path.separator) ||
        normalizedFile == normalizedInput;
  }
}
