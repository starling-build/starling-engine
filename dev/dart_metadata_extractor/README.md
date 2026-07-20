# Dart Metadata Extractor

Extracts comprehensive metadata from Dart source files using the Dart analyzer. Outputs structured JSON for AI consumption to understand file structure, class hierarchies, function signatures, and references.

## Installation

```bash
cd dev/dart_metadata_extractor
dart pub get
```

## Usage

### Single Directory/File Mode

```bash
dart run bin/extract_metadata.dart --input <path> --output <path> [options]
```

### Multi-File Mode with Dependency Sorting

```bash
# Using --files option (can be repeated)
dart run bin/extract_metadata.dart --files <file1.dart> --files <file2.dart> --output <path>

# Using a file list
dart run bin/extract_metadata.dart --file-list <files.txt> --output <path>
```

### Options

| Option | Description |
|--------|-------------|
| `-i, --input` | Input directory or file to analyze (single mode) |
| `--files` | List of specific files to analyze with dependency sorting (multi-file mode, repeatable) |
| `--file-list` | Path to a file containing list of files to analyze (one per line) |
| `-o, --output` | Output directory or file path (required) |
| `-f, --format` | Output format: `json` (default) or `compact` (AI-readable text) |
| `--output-format` | `per-file` (default) or `single` merged JSON (single mode only) |
| `--analyze-bodies` | Analyze function bodies for references (default: true) |
| `--include-private` | Include private declarations (names starting with `_`) (default: false) |
| `-p, --package-root` | Package root for dependency resolution (auto-detected if not specified) |
| `-s, --sdk-path` | Dart SDK path for type resolution (uses default SDK if not specified) |
| `-v, --verbose` | Print verbose output |
| `-h, --help` | Show help message |

### Examples

```bash
# Extract metadata from a single file
dart run bin/extract_metadata.dart \
  -i ../packages/flutter/lib/src/widgets/framework.dart \
  -o /tmp/metadata

# Extract from a directory (one JSON per Dart file)
dart run bin/extract_metadata.dart \
  -i ../packages/flutter/lib/src/foundation \
  -o /tmp/metadata -v

# Extract to a single merged JSON file
dart run bin/extract_metadata.dart \
  -i ../packages/flutter/lib/src/widgets \
  -o /tmp/metadata \
  --output-format single

# Analyze specific files with dependency ordering
dart run bin/extract_metadata.dart \
  --files lib/a.dart --files lib/b.dart --files lib/c.dart \
  -o analysis.json

# Analyze files from a list with compact output
dart run bin/extract_metadata.dart \
  --file-list migration_files.txt \
  -o analysis.txt -f compact
```

## Multi-File Dependency Analysis

When using `--files` or `--file-list`, the tool performs cross-file dependency analysis:

1. **Extracts metadata** from each file individually
2. **Builds a global dependency graph** by tracking which symbols each file exports and imports
3. **Performs topological sorting** to determine the optimal migration order (dependencies first)
4. **Detects circular dependencies** that need special handling

### Multi-File Output Format

```json
{
  "version": "1.0.0",
  "fileCount": 3,
  "globalDependencyGraph": {
    "files": [
      {
        "filePath": "/path/to/base.dart",
        "dependsOn": [],
        "dependedBy": ["/path/to/child.dart"],
        "exportedSymbols": ["BaseClass", "helperFunction"],
        "importedSymbols": []
      },
      {
        "filePath": "/path/to/child.dart",
        "dependsOn": ["/path/to/base.dart"],
        "dependedBy": [],
        "exportedSymbols": ["ChildClass"],
        "importedSymbols": [
          { "symbol": "BaseClass", "sourceFile": "/path/to/base.dart", "usageType": "superclass" }
        ]
      }
    ],
    "migrationOrder": ["/path/to/base.dart", "/path/to/child.dart"],
    "circularDependencies": []
  },
  "files": [...]
}
```

### Compact Output Format

With `-f compact`, the output is optimized for AI consumption:

```
# Multi-File Dependency Analysis
Total files: 3

## Migration Order
1. base.dart
2. helper.dart
3. child.dart

## Circular Dependencies (need special handling)
- a.dart <-> b.dart

## File Dependencies
### base.dart
  exports: BaseClass, helperFunction
### child.dart
  depends on: base.dart
  exports: ChildClass
```

## Output Format

Each Dart file produces a JSON file with the following structure:

```json
{
  "version": "1.0.0",
  "filePath": "/path/to/file.dart",
  "library": {
    "name": "library_name",
    "uri": "package:flutter/src/widgets/framework.dart"
  },
  "partOf": "dart.ui",
  "imports": [
    {
      "uri": "dart:ui",
      "prefix": "ui",
      "showNames": ["Color", "Offset"],
      "hideNames": [],
      "isDeferred": false
    }
  ],
  "exports": [...],
  "parts": [...],
  "declarations": {
    "classes": [...],
    "mixins": [...],
    "enums": [...],
    "extensions": [...],
    "extensionTypes": [...],
    "typedefs": [...],
    "functions": [...],
    "topLevelVariables": [...]
  },
  "externalDependencies": [
    {
      "library": "dart:typed_data",
      "symbol": "Float32List",
      "usageType": "constructor"
    }
  ]
}
```

### Class Declaration

```json
{
  "name": "State",
  "kind": "abstract class",
  "typeParameters": [
    {
      "name": "T",
      "bound": {
        "name": "StatefulWidget",
        "fullyQualifiedName": "package:flutter/src/widgets/framework.dart#StatefulWidget"
      }
    }
  ],
  "superclass": { "name": "Object" },
  "interfaces": [],
  "mixins": [{ "name": "Diagnosticable" }],
  "annotations": [{ "name": "immutable" }],
  "documentation": "/// The logic and internal state for a [StatefulWidget]...",
  "location": { "line": 825, "column": 1, "offset": 34522 },
  "constructors": [...],
  "methods": [...],
  "fields": [...],
  "accessors": [...]
}
```

### Method Declaration

```json
{
  "name": "build",
  "returnType": {
    "name": "Widget",
    "fullyQualifiedName": "package:flutter/src/widgets/framework.dart#Widget",
    "isNullable": false
  },
  "typeParameters": [],
  "parameters": [
    {
      "name": "context",
      "type": { "name": "BuildContext" },
      "isRequired": true,
      "isNamed": false,
      "isPositional": true
    }
  ],
  "isStatic": false,
  "isAbstract": false,
  "isOverride": true,
  "modifiers": [],
  "annotations": [{ "name": "protected" }],
  "documentation": "/// Describes the part of the user interface...",
  "location": { "line": 1024, "column": 3, "offset": 42156 },
  "bodyReferences": {
    "constructorCalls": [
      { "type": "Container" },
      { "type": "Text" }
    ],
    "methodCalls": [
      { "targetType": "Theme", "methodName": "of", "isStatic": true }
    ],
    "propertyAccesses": [
      { "targetType": "BuildContext", "propertyName": "size" }
    ],
    "typeCasts": ["Widget"],
    "typeChecks": ["StatelessWidget"]
  },
  "nativeInfo": {
    "symbol": "RSuperellipse::contains",
    "isLeaf": true
  }
}
```

## Extracted Metadata

### File-Level
- Library directive (name, uri)
- Part-of directive (for files that are `part of` a library)
- Imports (uri, prefix, show/hide combinators, deferred)
- Exports (uri, show/hide combinators)
- Part directives
- External dependencies (aggregated from all body references)

### Declarations
- **Classes**: name, kind (abstract/sealed/final/mixin class), type parameters with bounds, superclass, interfaces, mixins, annotations, documentation
- **Mixins**: name, type parameters, on-types, interfaces
- **Enums**: name, values (with arguments), constructors, methods, fields
- **Extensions**: name, extended type, methods, fields
- **Extension Types**: name, representation type, interfaces
- **Typedefs**: name, type parameters, aliased type
- **Functions**: name, return type, parameters, async modifiers
- **Variables**: name, type, final/const/late modifiers

### Members
- **Constructors**: name, const/factory, parameters, redirects, super calls
- **Methods**: signature, static/abstract/override, async modifiers, body references, @Native FFI info
- **Fields**: type, static/final/const/late modifiers
- **Accessors**: getters/setters with types

### Body References (Shallow Analysis)
- Constructor calls (type instantiations)
- Method invocations (with target type when resolved)
- Property accesses
- Type casts (`as` expressions)
- Type checks (`is` expressions)

### FFI Support
- **@Native annotation**: Extracts symbol name and `isLeaf` flag for FFI bridge methods

## Design Decisions

- **Public by default**: Private declarations (names starting with `_`) are skipped unless `--include-private` is specified
- **Shallow body analysis**: Only direct references in function bodies, not nested closures
- **Fully qualified names**: When type resolution succeeds, includes full package URI

## Project Structure

```
dev/dart_metadata_extractor/
├── bin/
│   └── extract_metadata.dart       # CLI entry point
├── lib/
│   ├── dart_metadata_extractor.dart  # Library exports
│   └── src/
│       ├── extraction/
│       │   └── metadata_extractor.dart  # Orchestrator
│       ├── models/
│       │   └── metadata_models.dart     # Data classes
│       └── visitors/
│           ├── file_metadata_visitor.dart  # Main AST visitor
│           └── type_extractor.dart         # Type utilities
├── pubspec.yaml
└── README.md
```

## Dependencies

- `analyzer`: Dart static analysis
- `args`: Command-line argument parsing
- `path`: Path manipulation utilities
