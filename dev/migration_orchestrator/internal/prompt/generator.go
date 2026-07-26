// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package prompt

import (
	"bytes"
	"encoding/json"
	"fmt"
	"path/filepath"
	"strings"
	"text/template"

	"github.com/anthropics/migration-orchestrator/internal/config"
	"github.com/anthropics/migration-orchestrator/internal/task"
)

// Generator generates prompts for Claude
type Generator struct {
	cfg  *config.Config
	tmpl *template.Template
}

// NewGenerator creates a new prompt generator
func NewGenerator(cfg *config.Config) (*Generator, error) {
	g := &Generator{cfg: cfg}

	// Parse template
	tmpl, err := template.New("prompt").Parse(promptTemplate)
	if err != nil {
		return nil, fmt.Errorf("failed to parse template: %w", err)
	}
	g.tmpl = tmpl

	return g, nil
}

// Generate creates a prompt for the given task
func (g *Generator) Generate(t *task.Task) (string, error) {
	// Compute the correct source file path relative to workspace
	// The metadata may contain paths from a different base directory
	// We need to extract the relative path (e.g., "lib/ui/channel_buffers.dart")
	// and construct the full path based on the workspace
	sourceFilePath := g.normalizeSourcePath(t.FilePath)

	data := &promptData{
		TaskID:             t.ID,
		TaskType:           string(t.TaskType),
		Name:               t.Name,
		FileName:           t.FileName,
		FilePath:           sourceFilePath,
		BaseName:           t.GetBaseName(),
		SwiftFileName:      t.GetSwiftFileName(),
		BranchName:         t.BranchName,
		BaseBranch:         g.cfg.BaseBranch,
		BranchPrefix:       g.cfg.BranchPrefix,
		DependsOn:          t.DependsOn,
		DependedBy:         t.DependedBy,
		MigrationGuidePath: g.cfg.GetMigrationGuide(),
		BuildCpp:           g.cfg.BuildCpp,
		BuildSwift:         g.cfg.BuildSwift,
		AutoMerge:          g.cfg.AutoMerge,
		AutoPush:           g.cfg.AutoPush,
		WorkspacePath:      g.cfg.WorkspacePath,
		EnginePath:         g.cfg.GetEnginePath(),
		SwiftPkgPath:       g.cfg.GetSwiftPackagePath(),
	}

	// Get source location if available
	if loc := t.GetLocation(); loc != nil {
		if loc.EndLine > 0 && loc.EndLine != loc.Line {
			data.SourceLocation = fmt.Sprintf("%s:%d-%d", t.FileName, loc.Line, loc.EndLine)
		} else {
			data.SourceLocation = fmt.Sprintf("%s:%d", t.FileName, loc.Line)
		}
	}

	// Add declaration-specific metadata JSON
	var declData interface{}
	switch t.TaskType {
	// Top-level declarations
	case task.TaskTypeClass:
		declData = t.ClassDecl
	case task.TaskTypeMixin:
		declData = t.MixinDecl
	case task.TaskTypeEnum:
		declData = t.EnumDecl
	case task.TaskTypeExtension:
		declData = t.ExtensionDecl
	case task.TaskTypeTypedef:
		declData = t.TypedefDecl
	case task.TaskTypeFunction:
		declData = t.FunctionDecl
	case task.TaskTypeVariable:
		declData = t.VariableDecl
	// Member-level declarations
	case task.TaskTypeConstructor:
		declData = t.ConstructorDecl
	case task.TaskTypeMethod:
		declData = t.MethodDecl
	case task.TaskTypeAccessor:
		declData = t.AccessorDecl
	case task.TaskTypeField:
		declData = t.FieldDecl
	case task.TaskTypeEnumValue:
		declData = t.EnumValueDecl
	}

	if declData != nil {
		metaJSON, err := json.MarshalIndent(declData, "", "  ")
		if err == nil {
			data.MetadataJSON = string(metaJSON)
		}
	}

	// Format dependencies (task IDs)
	if len(t.DependsOn) > 0 {
		data.DependsOnStr = strings.Join(t.DependsOn, ", ")
	}
	if len(t.DependedBy) > 0 {
		data.DependedByStr = strings.Join(t.DependedBy, ", ")
	}

	var buf bytes.Buffer
	if err := g.tmpl.Execute(&buf, data); err != nil {
		return "", fmt.Errorf("failed to execute template: %w", err)
	}

	return buf.String(), nil
}

// normalizeSourcePath converts the metadata path to the correct workspace path
// The metadata may contain paths like "/some/path/engine/src/flutter/lib/ui/file.dart"
// We need to extract the relative part (e.g., "lib/ui/file.dart") and construct
// the full path based on the configured workspace
func (g *Generator) normalizeSourcePath(metadataPath string) string {
	// Look for "engine/src/flutter/" in the path - this is the anchor point
	// After this marker, we have the relative path within the Flutter engine
	marker := "engine/src/flutter/"
	if idx := strings.Index(metadataPath, marker); idx != -1 {
		// Extract the relative path after "engine/src/flutter/"
		// e.g., "lib/ui/channel_buffers.dart"
		relativePath := metadataPath[idx+len(marker):]
		// Construct the full path using the workspace
		// workspace/engine/src/flutter/lib/ui/...
		return filepath.Join(g.cfg.WorkspacePath, "engine", "src", "flutter", relativePath)
	}

	// If no marker found, return the original path
	// It might already be correct or use a different structure
	return metadataPath
}

// promptData holds data for the prompt template
type promptData struct {
	TaskID             string
	TaskType           string
	Name               string
	FileName           string
	FilePath           string
	BaseName           string
	SwiftFileName      string
	SourceLocation     string
	BranchName         string
	BaseBranch         string
	BranchPrefix       string
	DependsOn          []string
	DependsOnStr       string
	DependedBy         []string
	DependedByStr      string
	MetadataJSON       string
	MigrationGuidePath string
	BuildCpp           bool
	BuildSwift         bool
	AutoMerge          bool
	AutoPush           bool
	WorkspacePath      string
	EnginePath         string
	SwiftPkgPath       string
}

const promptTemplate = `# Migration Task: {{.TaskType}} {{.Name}}

## Context
You are migrating Flutter's dart:ui layer from Dart to Swift 6. This creates a pure Swift glue layer between the Flutter framework and the C++ engine.

**IMPORTANT: This task is for ONE SPECIFIC {{.TaskType}} only, NOT the entire file.**

## Task Details
- **Task ID:** {{.TaskID}}
- **Type:** {{.TaskType}}
- **Name:** {{.Name}}
- **Source File:** {{.FilePath}}
{{if .SourceLocation}}- **Source Location (ONLY migrate code at these lines):** {{.SourceLocation}}{{end}}
- **Swift Target:** {{.SwiftFileName}}
{{if .DependsOnStr}}- **Depends On:** {{.DependsOnStr}}{{end}}
{{if .DependedByStr}}- **Required By:** {{.DependedByStr}}{{end}}

{{if .MetadataJSON}}
## Declaration Metadata
` + "```json" + `
{{.MetadataJSON}}
` + "```" + `
{{end}}

## CRITICAL: Scope of This Task

**You must ONLY migrate the specific {{.TaskType}} named "{{.Name}}" located at {{.SourceLocation}} in the source file.**

- Do NOT migrate the entire file
- Do NOT migrate unrelated classes, methods, or declarations in the same file
- ONLY port the code between the specified line numbers
- **EXCEPTION**: If the code at the specified lines depends on other code defined elsewhere in the SAME file (e.g., a method that uses a private field, a helper function, or a constant), you MUST also port those dependencies
- If the Swift file already exists, ADD your code to it (do not overwrite existing content)
- If the Swift file does not exist, create it with ONLY the code for this specific {{.TaskType}} and its required dependencies

## Your Task

### Step 1: Git Operations
First, create a feature branch for this migration:
` + "```bash" + `
cd {{.WorkspacePath}}
git checkout {{.BaseBranch}}
git pull myflutter {{.BaseBranch}}
git checkout -b {{.BranchName}}
` + "```" + `

### Step 2: Read Migration Guide
**IMPORTANT:** Before writing any code, you MUST read the entire migration guide file:
` + "```" + `
{{.MigrationGuidePath}}
` + "```" + `

Use the Read tool to read this file NOW. The guide contains critical rules including:
- File locations and directory structure
- C++ bridge patterns (pimpl, SWIFT_SHARED_REFERENCE, etc.)
- BUILD.gn dependency rules (NEVER depend on //flutter/lib/ui/...)
- Swift coding patterns

Do NOT proceed without reading the migration guide first.

### Step 3: Migration
Migrate ONLY the {{.TaskType}} **{{.Name}}** (lines {{.SourceLocation}}) from {{.FileName}} to Swift.

1. Read the source Dart file and locate the specific {{.TaskType}} at the specified lines
2. Check existing Swift files in ` + "`{{.SwiftPkgPath}}/Sources/FlutterSwiftBridge/`" + ` for patterns
3. Implement ONLY this {{.TaskType}} following the migration guide's patterns
4. Add C++ bridge if needed (see migration guide for when/how)
5. If the Swift file already exists, append your code appropriately (do not duplicate existing code)

{{if .BuildCpp}}
### Step 4: Build C++ Engine
` + "```bash" + `
cd {{.EnginePath}}
./flutter/bin/et build --config ci/host_debug_unopt_arm64
` + "```" + `
If build fails, fix the errors before proceeding.
{{end}}

{{if .BuildSwift}}
### Step 5: Build Swift Package
` + "```bash" + `
cd {{.SwiftPkgPath}}
swift build
` + "```" + `
If build fails, fix the errors before proceeding.
{{end}}

### Step 6: Commit Changes
` + "```bash" + `
cd {{.WorkspacePath}}
git add -A
git commit -m "Migrate {{.TaskType}} {{.Name}} to Swift

- Source: {{.FileName}}
- Created Swift implementation: {{.SwiftFileName}}
- Added C++ bridge (if applicable)
- Follows SWIFT_MIGRATION_GUIDE.md patterns

Co-Authored-By: Claude <noreply@anthropic.com>"
` + "```" + `

{{if .AutoMerge}}
### Step 7: Merge to {{.BaseBranch}}
` + "```bash" + `
git checkout {{.BaseBranch}}
git merge {{.BranchName}} --no-ff -m "Merge branch '{{.BranchName}}' into {{.BaseBranch}}"
` + "```" + `
{{end}}

{{if .AutoPush}}
### Step 8: Push Changes
` + "```bash" + `
git push myflutter {{.BaseBranch}}
` + "```" + `
{{end}}

## Important Rules

1. **ONLY migrate the specific {{.TaskType}} at the specified lines** - Do NOT port the entire file or unrelated declarations
2. **Include dependencies from the same file** - If the code depends on fields, helpers, or constants defined elsewhere in the same Dart file, port those too
3. **Follow the migration guide exactly** - it has all the patterns and rules
4. **ALWAYS include Dart source references** in Swift code
5. **Document ANY differences** from Dart implementation with comments
6. **Build MUST succeed** before committing
7. **NEVER add any ` + "`//flutter/lib/ui/...`" + ` dependency in BUILD.gn** - anything under ` + "`//flutter/lib/ui/`" + ` is the Dart UI layer we are replacing (e.g., ` + "`//flutter/lib/ui/window:window`" + `, ` + "`//flutter/lib/ui:ui`" + `, etc.). The C++ bridge should ONLY depend on underlying engine libraries like ` + "`//flutter/display_list`" + `, ` + "`//flutter/impeller`" + `, ` + "`//flutter/fml`" + `, ` + "`//third_party/skia`" + `, etc.
8. **If Swift file already exists**, add your code to it without removing existing code
9. **If uncertain, EXIT early** - If you don't know how to properly migrate this code (e.g., unclear patterns, missing context, complex dependencies you can't resolve), output "MIGRATION_BLOCKED: {{.TaskID}} - <reason>" and exit. A wrong implementation is worse than no implementation.

When complete, output: "MIGRATION_COMPLETE: {{.TaskID}}"
If blocked, output: "MIGRATION_BLOCKED: {{.TaskID}} - <reason>"
`
