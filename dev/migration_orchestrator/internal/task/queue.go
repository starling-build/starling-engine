// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package task

import (
	"path/filepath"
	"sync"

	"github.com/anthropics/migration-orchestrator/internal/config"
	"github.com/anthropics/migration-orchestrator/internal/parser"
	"github.com/anthropics/migration-orchestrator/internal/progress"
)

// Queue manages the task queue with dependency ordering
type Queue struct {
	tasks map[string]*Task // keyed by task ID
	order []string         // task IDs in migration order
	mu    sync.RWMutex
	cfg   *config.Config
}

// NewQueue creates a new task queue from parsed metadata
// Creates individual tasks for each class, function, enum, etc.
func NewQueue(metadata *parser.MetadataResult, cfg *config.Config) *Queue {
	q := &Queue{
		tasks: make(map[string]*Task),
		order: make([]string, 0),
		cfg:   cfg,
	}

	priority := 0

	// Process files in migration order
	for _, filePath := range metadata.MigrationOrder {
		fileName := filepath.Base(filePath)

		// Check if should skip this file
		if cfg.ShouldSkip(fileName) {
			continue
		}

		// Find file metadata
		var fileMeta *parser.FileMetadata
		for i := range metadata.Files {
			if metadata.Files[i].FilePath == filePath ||
				filepath.Base(metadata.Files[i].FilePath) == fileName {
				fileMeta = &metadata.Files[i]
				break
			}
		}

		if fileMeta == nil || fileMeta.Declarations == nil {
			continue
		}

		// Create tasks for each declaration type
		decls := fileMeta.Declarations

		// Top-level functions first (often utilities like lerpDouble, clampDouble)
		for i := range decls.Functions {
			fn := &decls.Functions[i]
			task := NewFunctionTask(filePath, priority, fn, fileMeta)
			q.tasks[task.ID] = task
			q.order = append(q.order, task.ID)
			priority++
		}

		// Top-level variables (constants)
		for i := range decls.TopLevelVariables {
			v := &decls.TopLevelVariables[i]
			task := NewVariableTask(filePath, priority, v, fileMeta)
			q.tasks[task.ID] = task
			q.order = append(q.order, task.ID)
			priority++
		}

		// Typedefs
		for i := range decls.Typedefs {
			td := &decls.Typedefs[i]
			task := NewTypedefTask(filePath, priority, td, fileMeta)
			q.tasks[task.ID] = task
			q.order = append(q.order, task.ID)
			priority++
		}

		// Enums - create one task per enum (including all values)
		for i := range decls.Enums {
			enum := &decls.Enums[i]
			task := NewEnumTask(filePath, priority, enum, fileMeta)
			q.tasks[task.ID] = task
			q.order = append(q.order, task.ID)
			priority++
		}

		// Mixins - create tasks for each member
		for i := range decls.Mixins {
			mixin := &decls.Mixins[i]

			// Fields first
			for j := range mixin.Fields {
				field := &mixin.Fields[j]
				task := NewFieldTask(filePath, priority, mixin.Name, "mixin", field, fileMeta)
				q.tasks[task.ID] = task
				q.order = append(q.order, task.ID)
				priority++
			}

			// Methods
			for j := range mixin.Methods {
				method := &mixin.Methods[j]
				task := NewMethodTask(filePath, priority, mixin.Name, "mixin", method, fileMeta)
				q.tasks[task.ID] = task
				q.order = append(q.order, task.ID)
				priority++
			}
		}

		// Classes - create tasks for each member
		for i := range decls.Classes {
			class := &decls.Classes[i]

			// Fields first
			for j := range class.Fields {
				field := &class.Fields[j]
				task := NewFieldTask(filePath, priority, class.Name, "class", field, fileMeta)
				q.tasks[task.ID] = task
				q.order = append(q.order, task.ID)
				priority++
			}

			// Constructors
			for j := range class.Constructors {
				ctor := &class.Constructors[j]
				task := NewConstructorTask(filePath, priority, class.Name, "class", ctor, fileMeta)
				q.tasks[task.ID] = task
				q.order = append(q.order, task.ID)
				priority++
			}

			// Methods
			for j := range class.Methods {
				method := &class.Methods[j]
				task := NewMethodTask(filePath, priority, class.Name, "class", method, fileMeta)
				q.tasks[task.ID] = task
				q.order = append(q.order, task.ID)
				priority++
			}

			// Accessors (getters/setters)
			for j := range class.Accessors {
				accessor := &class.Accessors[j]
				task := NewAccessorTask(filePath, priority, class.Name, "class", accessor, fileMeta)
				q.tasks[task.ID] = task
				q.order = append(q.order, task.ID)
				priority++
			}
		}

		// Extensions - create tasks for each member
		for i := range decls.Extensions {
			ext := &decls.Extensions[i]

			extName := ext.Name
			if extName == "" {
				extName = "ExtensionOn" + ext.ExtendedType.Name
			}

			// Fields
			for j := range ext.Fields {
				field := &ext.Fields[j]
				task := NewFieldTask(filePath, priority, extName, "extension", field, fileMeta)
				q.tasks[task.ID] = task
				q.order = append(q.order, task.ID)
				priority++
			}

			// Methods
			for j := range ext.Methods {
				method := &ext.Methods[j]
				task := NewMethodTask(filePath, priority, extName, "extension", method, fileMeta)
				q.tasks[task.ID] = task
				q.order = append(q.order, task.ID)
				priority++
			}
		}
	}

	// Build reverse dependency graph (dependedBy)
	for _, task := range q.tasks {
		for _, depID := range task.DependsOn {
			if dep, ok := q.tasks[depID]; ok {
				dep.DependedBy = append(dep.DependedBy, task.ID)
			}
		}
	}

	return q
}

// findTaskIDByName finds a task ID by declaration name
func findTaskIDByName(tasks map[string]*Task, name string) string {
	for id, task := range tasks {
		if task.Name == name {
			return id
		}
	}
	return ""
}

// Len returns the total number of tasks
func (q *Queue) Len() int {
	q.mu.RLock()
	defer q.mu.RUnlock()
	return len(q.tasks)
}

// Pending returns the number of pending tasks
func (q *Queue) Pending() int {
	q.mu.RLock()
	defer q.mu.RUnlock()
	count := 0
	for _, t := range q.tasks {
		if t.Status == StatusPending {
			count++
		}
	}
	return count
}

// GetTask returns a task by ID
func (q *Queue) GetTask(id string) *Task {
	q.mu.RLock()
	defer q.mu.RUnlock()
	return q.tasks[id]
}

// GetAllTasks returns all tasks in order
func (q *Queue) GetAllTasks() []*Task {
	q.mu.RLock()
	defer q.mu.RUnlock()
	tasks := make([]*Task, 0, len(q.order))
	for _, id := range q.order {
		if t, ok := q.tasks[id]; ok {
			tasks = append(tasks, t)
		}
	}
	return tasks
}

// GetPendingTasks returns all pending tasks in order
func (q *Queue) GetPendingTasks() []*Task {
	q.mu.RLock()
	defer q.mu.RUnlock()
	tasks := make([]*Task, 0)
	for _, id := range q.order {
		if t, ok := q.tasks[id]; ok && t.Status == StatusPending {
			tasks = append(tasks, t)
		}
	}
	return tasks
}

// NextAvailable returns up to n tasks that are ready to run
// A task is ready if all its dependencies are completed
func (q *Queue) NextAvailable(n int) []*Task {
	q.mu.Lock()
	defer q.mu.Unlock()

	available := make([]*Task, 0, n)

	for _, id := range q.order {
		if len(available) >= n {
			break
		}

		t, ok := q.tasks[id]
		if !ok || t.Status != StatusPending {
			continue
		}

		if q.canRunLocked(t) {
			available = append(available, t)
		}
	}

	return available
}

// canRunLocked checks if a task can run (dependencies satisfied)
// Must be called with lock held
func (q *Queue) canRunLocked(t *Task) bool {
	for _, depID := range t.DependsOn {
		if dep, ok := q.tasks[depID]; ok {
			if dep.Status != StatusCompleted && dep.Status != StatusSkipped {
				return false
			}
		}
	}
	return true
}

// CanRun checks if a task can run (thread-safe)
func (q *Queue) CanRun(t *Task) bool {
	q.mu.RLock()
	defer q.mu.RUnlock()
	return q.canRunLocked(t)
}

// MarkInProgress marks a task as in progress
func (q *Queue) MarkInProgress(id string) {
	q.mu.Lock()
	defer q.mu.Unlock()
	if t, ok := q.tasks[id]; ok {
		t.MarkInProgress()
	}
}

// MarkCompleted marks a task as completed
func (q *Queue) MarkCompleted(id string, commitHash string) {
	q.mu.Lock()
	defer q.mu.Unlock()
	if t, ok := q.tasks[id]; ok {
		t.MarkCompleted(commitHash)
	}
}

// MarkFailed marks a task as failed
func (q *Queue) MarkFailed(id string, err string) {
	q.mu.Lock()
	defer q.mu.Unlock()
	if t, ok := q.tasks[id]; ok {
		t.MarkFailed(err)
	}
}

// MarkSkipped marks a task as skipped
func (q *Queue) MarkSkipped(id string, reason string) {
	q.mu.Lock()
	defer q.mu.Unlock()
	if t, ok := q.tasks[id]; ok {
		t.MarkSkipped(reason)
	}
}

// ApplyProgress applies saved progress to the queue
func (q *Queue) ApplyProgress(tracker *progress.Tracker) {
	q.mu.Lock()
	defer q.mu.Unlock()

	for id, taskProgress := range tracker.Tasks {
		if t, ok := q.tasks[id]; ok {
			t.Status = Status(taskProgress.Status)
			t.BranchName = taskProgress.BranchName
			t.CommitHash = taskProgress.CommitHash
			t.Error = taskProgress.Error
			if taskProgress.StartTime != nil {
				t.StartTime = taskProgress.StartTime
			}
			if taskProgress.EndTime != nil {
				t.EndTime = taskProgress.EndTime
			}
		}
	}
}

// HasPendingWork returns true if there are tasks that can still run
func (q *Queue) HasPendingWork() bool {
	q.mu.RLock()
	defer q.mu.RUnlock()

	for _, id := range q.order {
		t, ok := q.tasks[id]
		if !ok {
			continue
		}
		if t.Status == StatusPending || t.Status == StatusInProgress {
			return true
		}
	}
	return false
}

// AllComplete returns true if all tasks are in terminal state
func (q *Queue) AllComplete() bool {
	q.mu.RLock()
	defer q.mu.RUnlock()

	for _, t := range q.tasks {
		if !t.IsTerminal() {
			return false
		}
	}
	return true
}

// GetTasksByFile returns all tasks for a given file
func (q *Queue) GetTasksByFile(fileName string) []*Task {
	q.mu.RLock()
	defer q.mu.RUnlock()

	tasks := make([]*Task, 0)
	for _, t := range q.tasks {
		if t.FileName == fileName {
			tasks = append(tasks, t)
		}
	}
	return tasks
}

// GetTasksByType returns all tasks of a given type
func (q *Queue) GetTasksByType(taskType TaskType) []*Task {
	q.mu.RLock()
	defer q.mu.RUnlock()

	tasks := make([]*Task, 0)
	for _, id := range q.order {
		if t, ok := q.tasks[id]; ok && t.TaskType == taskType {
			tasks = append(tasks, t)
		}
	}
	return tasks
}

// Summary returns a summary of tasks by type
func (q *Queue) Summary() map[TaskType]int {
	q.mu.RLock()
	defer q.mu.RUnlock()

	summary := make(map[TaskType]int)
	for _, t := range q.tasks {
		summary[t.TaskType]++
	}
	return summary
}
