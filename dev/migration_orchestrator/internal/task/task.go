package task

import (
	"fmt"
	"path/filepath"
	"strings"
	"time"

	"github.com/anthropics/migration-orchestrator/internal/parser"
)

// Status represents the status of a task
type Status string

const (
	StatusPending    Status = "pending"
	StatusInProgress Status = "in_progress"
	StatusCompleted  Status = "completed"
	StatusFailed     Status = "failed"
	StatusSkipped    Status = "skipped"
)

// TaskType represents the type of declaration being migrated
type TaskType string

const (
	// Top-level declarations
	TaskTypeClass     TaskType = "class"
	TaskTypeMixin     TaskType = "mixin"
	TaskTypeEnum      TaskType = "enum"
	TaskTypeExtension TaskType = "extension"
	TaskTypeTypedef   TaskType = "typedef"
	TaskTypeFunction  TaskType = "function"
	TaskTypeVariable  TaskType = "variable"

	// Class/Mixin members
	TaskTypeConstructor TaskType = "constructor"
	TaskTypeMethod      TaskType = "method"
	TaskTypeAccessor    TaskType = "accessor"
	TaskTypeField       TaskType = "field"

	// Enum members
	TaskTypeEnumValue TaskType = "enum_value"
)

// Task represents a single migration task for a class, function, or other declaration
type Task struct {
	ID           string     `json:"id"`            // Unique identifier: "filename:DeclarationName"
	TaskType     TaskType   `json:"task_type"`     // Type of declaration
	Name         string     `json:"name"`          // Declaration name (class name, function name, etc.)
	FilePath     string     `json:"file_path"`     // Source Dart file path
	FileName     string     `json:"file_name"`     // Source Dart file name
	Status       Status     `json:"status"`
	Priority     int        `json:"priority"`      // Order in migration sequence
	DependsOn    []string   `json:"depends_on,omitempty"`    // Task IDs this depends on
	DependedBy   []string   `json:"depended_by,omitempty"`   // Task IDs that depend on this
	BranchName   string     `json:"branch_name"`
	StartTime    *time.Time `json:"start_time,omitempty"`
	EndTime      *time.Time `json:"end_time,omitempty"`
	Error        string     `json:"error,omitempty"`
	CommitHash   string     `json:"commit_hash,omitempty"`

	// Parent context (for member tasks)
	ParentName string `json:"parent_name,omitempty"` // Class/Mixin/Enum name for member tasks
	ParentType string `json:"parent_type,omitempty"` // "class", "mixin", "enum", "extension"

	// Declaration-specific metadata (top-level)
	ClassDecl     *parser.ClassDecl     `json:"class_decl,omitempty"`
	MixinDecl     *parser.MixinDecl     `json:"mixin_decl,omitempty"`
	EnumDecl      *parser.EnumDecl      `json:"enum_decl,omitempty"`
	ExtensionDecl *parser.ExtensionDecl `json:"extension_decl,omitempty"`
	TypedefDecl   *parser.TypedefDecl   `json:"typedef_decl,omitempty"`
	FunctionDecl  *parser.FunctionDecl  `json:"function_decl,omitempty"`
	VariableDecl  *parser.VariableDecl  `json:"variable_decl,omitempty"`

	// Member-level metadata
	ConstructorDecl *parser.Constructor  `json:"constructor_decl,omitempty"`
	MethodDecl      *parser.MethodDecl   `json:"method_decl,omitempty"`
	AccessorDecl    *parser.AccessorDecl `json:"accessor_decl,omitempty"`
	FieldDecl       *parser.FieldDecl    `json:"field_decl,omitempty"`
	EnumValueDecl   *parser.EnumValue    `json:"enum_value_decl,omitempty"`

	// File-level context (for reference)
	FileMetadata *parser.FileMetadata `json:"file_metadata,omitempty"`
}

// NewClassTask creates a task for migrating a class
func NewClassTask(filePath string, priority int, class *parser.ClassDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, class.Name),
		TaskType:     TaskTypeClass,
		Name:         class.Name,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(class.Name)),
		ClassDecl:    class,
		FileMetadata: fileMeta,
	}
}

// NewMixinTask creates a task for migrating a mixin
func NewMixinTask(filePath string, priority int, mixin *parser.MixinDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, mixin.Name),
		TaskType:     TaskTypeMixin,
		Name:         mixin.Name,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(mixin.Name)),
		MixinDecl:    mixin,
		FileMetadata: fileMeta,
	}
}

// NewEnumTask creates a task for migrating an enum
func NewEnumTask(filePath string, priority int, enum *parser.EnumDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, enum.Name),
		TaskType:     TaskTypeEnum,
		Name:         enum.Name,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(enum.Name)),
		EnumDecl:     enum,
		FileMetadata: fileMeta,
	}
}

// NewExtensionTask creates a task for migrating an extension
func NewExtensionTask(filePath string, priority int, ext *parser.ExtensionDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	name := ext.Name
	if name == "" {
		name = fmt.Sprintf("ExtensionOn%s", ext.ExtendedType.Name)
	}

	return &Task{
		ID:            fmt.Sprintf("%s:%s", fileName, name),
		TaskType:      TaskTypeExtension,
		Name:          name,
		FilePath:      filePath,
		FileName:      fileName,
		Status:        StatusPending,
		Priority:      priority,
		BranchName:    fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(name)),
		ExtensionDecl: ext,
		FileMetadata:  fileMeta,
	}
}

// NewTypedefTask creates a task for migrating a typedef
func NewTypedefTask(filePath string, priority int, typedef *parser.TypedefDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, typedef.Name),
		TaskType:     TaskTypeTypedef,
		Name:         typedef.Name,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(typedef.Name)),
		TypedefDecl:  typedef,
		FileMetadata: fileMeta,
	}
}

// NewFunctionTask creates a task for migrating a top-level function
func NewFunctionTask(filePath string, priority int, fn *parser.FunctionDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, fn.Name),
		TaskType:     TaskTypeFunction,
		Name:         fn.Name,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(fn.Name)),
		FunctionDecl: fn,
		FileMetadata: fileMeta,
	}
}

// NewVariableTask creates a task for migrating a top-level variable
func NewVariableTask(filePath string, priority int, v *parser.VariableDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, v.Name),
		TaskType:     TaskTypeVariable,
		Name:         v.Name,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(v.Name)),
		VariableDecl: v,
		FileMetadata: fileMeta,
	}
}

// NewConstructorTask creates a task for migrating a constructor within a class/mixin
func NewConstructorTask(filePath string, priority int, parentName, parentType string, ctor *parser.Constructor, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	// Constructor name: ClassName or ClassName.namedCtor
	ctorName := parentName
	if ctor.Name != "" {
		ctorName = fmt.Sprintf("%s.%s", parentName, ctor.Name)
	}

	return &Task{
		ID:              fmt.Sprintf("%s:%s", fileName, ctorName),
		TaskType:        TaskTypeConstructor,
		Name:            ctorName,
		ParentName:      parentName,
		ParentType:      parentType,
		FilePath:        filePath,
		FileName:        fileName,
		Status:          StatusPending,
		Priority:        priority,
		BranchName:      fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(ctorName)),
		ConstructorDecl: ctor,
		FileMetadata:    fileMeta,
	}
}

// NewMethodTask creates a task for migrating a method within a class/mixin/extension
func NewMethodTask(filePath string, priority int, parentName, parentType string, method *parser.MethodDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	methodName := fmt.Sprintf("%s.%s", parentName, method.Name)

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, methodName),
		TaskType:     TaskTypeMethod,
		Name:         methodName,
		ParentName:   parentName,
		ParentType:   parentType,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(methodName)),
		MethodDecl:   method,
		FileMetadata: fileMeta,
	}
}

// NewAccessorTask creates a task for migrating a getter/setter within a class/mixin/extension
func NewAccessorTask(filePath string, priority int, parentName, parentType string, accessor *parser.AccessorDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	// Accessor name: ClassName.propertyName (get/set)
	accessorName := fmt.Sprintf("%s.%s", parentName, accessor.Name)

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, accessorName),
		TaskType:     TaskTypeAccessor,
		Name:         accessorName,
		ParentName:   parentName,
		ParentType:   parentType,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(accessorName)),
		AccessorDecl: accessor,
		FileMetadata: fileMeta,
	}
}

// NewFieldTask creates a task for migrating a field within a class/mixin
func NewFieldTask(filePath string, priority int, parentName, parentType string, field *parser.FieldDecl, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	fieldName := fmt.Sprintf("%s.%s", parentName, field.Name)

	return &Task{
		ID:           fmt.Sprintf("%s:%s", fileName, fieldName),
		TaskType:     TaskTypeField,
		Name:         fieldName,
		ParentName:   parentName,
		ParentType:   parentType,
		FilePath:     filePath,
		FileName:     fileName,
		Status:       StatusPending,
		Priority:     priority,
		BranchName:   fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(fieldName)),
		FieldDecl:    field,
		FileMetadata: fileMeta,
	}
}

// NewEnumValueTask creates a task for migrating an enum value
func NewEnumValueTask(filePath string, priority int, enumName string, value *parser.EnumValue, fileMeta *parser.FileMetadata) *Task {
	fileName := filepath.Base(filePath)
	fileBaseName := strings.TrimSuffix(fileName, filepath.Ext(fileName))

	valueName := fmt.Sprintf("%s.%s", enumName, value.Name)

	return &Task{
		ID:            fmt.Sprintf("%s:%s", fileName, valueName),
		TaskType:      TaskTypeEnumValue,
		Name:          valueName,
		ParentName:    enumName,
		ParentType:    "enum",
		FilePath:      filePath,
		FileName:      fileName,
		Status:        StatusPending,
		Priority:      priority,
		BranchName:    fmt.Sprintf("migrate/%s-%s", fileBaseName, toSnakeCase(valueName)),
		EnumValueDecl: value,
		FileMetadata:  fileMeta,
	}
}

// MarkInProgress marks the task as in progress
func (t *Task) MarkInProgress() {
	t.Status = StatusInProgress
	now := time.Now()
	t.StartTime = &now
}

// MarkCompleted marks the task as completed
func (t *Task) MarkCompleted(commitHash string) {
	t.Status = StatusCompleted
	now := time.Now()
	t.EndTime = &now
	t.CommitHash = commitHash
}

// MarkFailed marks the task as failed
func (t *Task) MarkFailed(err string) {
	t.Status = StatusFailed
	now := time.Now()
	t.EndTime = &now
	t.Error = err
}

// MarkSkipped marks the task as skipped
func (t *Task) MarkSkipped(reason string) {
	t.Status = StatusSkipped
	t.Error = reason
}

// Duration returns the task duration if available
func (t *Task) Duration() time.Duration {
	if t.StartTime == nil {
		return 0
	}
	end := time.Now()
	if t.EndTime != nil {
		end = *t.EndTime
	}
	return end.Sub(*t.StartTime)
}

// IsTerminal returns true if the task is in a terminal state
func (t *Task) IsTerminal() bool {
	return t.Status == StatusCompleted || t.Status == StatusFailed || t.Status == StatusSkipped
}

// GetFileBaseName returns the file name without extension
func (t *Task) GetFileBaseName() string {
	return strings.TrimSuffix(t.FileName, filepath.Ext(t.FileName))
}

// GetBaseName returns a base name for the task (used for file naming)
func (t *Task) GetBaseName() string {
	return toSnakeCase(t.Name)
}

// GetSwiftFileName returns the expected Swift file name for this declaration
func (t *Task) GetSwiftFileName() string {
	// Use the same base name as the original Dart file
	// e.g., channel_buffers.dart -> channel_buffers.swift
	baseName := strings.TrimSuffix(t.FileName, ".dart")
	return baseName + ".swift"
}

// GetDescription returns a human-readable description of the task
func (t *Task) GetDescription() string {
	return fmt.Sprintf("%s %s from %s", t.TaskType, t.Name, t.FileName)
}

// GetLocation returns the source location if available
func (t *Task) GetLocation() *parser.Location {
	switch t.TaskType {
	// Top-level declarations
	case TaskTypeClass:
		if t.ClassDecl != nil {
			return &t.ClassDecl.Location
		}
	case TaskTypeMixin:
		if t.MixinDecl != nil {
			return &t.MixinDecl.Location
		}
	case TaskTypeEnum:
		if t.EnumDecl != nil {
			return &t.EnumDecl.Location
		}
	case TaskTypeExtension:
		if t.ExtensionDecl != nil {
			return &t.ExtensionDecl.Location
		}
	case TaskTypeTypedef:
		if t.TypedefDecl != nil {
			return &t.TypedefDecl.Location
		}
	case TaskTypeFunction:
		if t.FunctionDecl != nil {
			return &t.FunctionDecl.Location
		}
	case TaskTypeVariable:
		if t.VariableDecl != nil {
			return &t.VariableDecl.Location
		}
	// Member-level declarations
	case TaskTypeConstructor:
		if t.ConstructorDecl != nil {
			return &t.ConstructorDecl.Location
		}
	case TaskTypeMethod:
		if t.MethodDecl != nil {
			return &t.MethodDecl.Location
		}
	case TaskTypeAccessor:
		if t.AccessorDecl != nil {
			return &t.AccessorDecl.Location
		}
	case TaskTypeField:
		if t.FieldDecl != nil {
			return &t.FieldDecl.Location
		}
	case TaskTypeEnumValue:
		// EnumValue doesn't have its own location - location info not available
		return nil
	}
	return nil
}

// Helper functions

// toSnakeCase converts PascalCase or camelCase to snake_case
func toSnakeCase(s string) string {
	var result strings.Builder
	for i, r := range s {
		if i > 0 && r >= 'A' && r <= 'Z' {
			result.WriteRune('_')
		}
		result.WriteRune(r)
	}
	return strings.ToLower(result.String())
}

// toPascalCase converts snake_case to PascalCase
func toPascalCase(s string) string {
	parts := strings.Split(s, "_")
	for i, p := range parts {
		if len(p) > 0 {
			parts[i] = strings.ToUpper(p[:1]) + p[1:]
		}
	}
	return strings.Join(parts, "")
}
