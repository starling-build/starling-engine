// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package progress

import (
	"encoding/json"
	"fmt"
	"os"
	"sync"
	"time"
)

// Tracker manages migration progress persistence
type Tracker struct {
	filePath  string
	mu        sync.RWMutex
	StartedAt *time.Time              `json:"started_at,omitempty"`
	UpdatedAt *time.Time              `json:"updated_at,omitempty"`
	Total     int                     `json:"total_tasks"`
	Tasks     map[string]*TaskProgress `json:"tasks"`
}

// TaskProgress tracks the progress of a single task
type TaskProgress struct {
	Status     string     `json:"status"`
	BranchName string     `json:"branch_name,omitempty"`
	CommitHash string     `json:"commit_hash,omitempty"`
	StartTime  *time.Time `json:"start_time,omitempty"`
	EndTime    *time.Time `json:"end_time,omitempty"`
	Error      string     `json:"error,omitempty"`
}

// NewTracker creates a new progress tracker
func NewTracker(filePath string) *Tracker {
	return &Tracker{
		filePath: filePath,
		Tasks:    make(map[string]*TaskProgress),
	}
}

// Start initializes the tracker for a new migration run
func (t *Tracker) Start(totalTasks int) {
	t.mu.Lock()
	defer t.mu.Unlock()

	now := time.Now()
	if t.StartedAt == nil {
		t.StartedAt = &now
	}
	t.UpdatedAt = &now
	t.Total = totalTasks
}

// Load loads progress from file
func (t *Tracker) Load() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	data, err := os.ReadFile(t.filePath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil // No progress file is fine
		}
		return fmt.Errorf("failed to read progress file: %w", err)
	}

	if err := json.Unmarshal(data, t); err != nil {
		return fmt.Errorf("failed to parse progress file: %w", err)
	}

	return nil
}

// Save saves progress to file
func (t *Tracker) Save() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	now := time.Now()
	t.UpdatedAt = &now

	data, err := json.MarshalIndent(t, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshal progress: %w", err)
	}

	if err := os.WriteFile(t.filePath, data, 0644); err != nil {
		return fmt.Errorf("failed to write progress file: %w", err)
	}

	return nil
}

// UpdateTask updates the progress for a task
func (t *Tracker) UpdateTask(fileName string, progress *TaskProgress) {
	t.mu.Lock()
	defer t.mu.Unlock()

	t.Tasks[fileName] = progress
}

// GetTask returns the progress for a task
func (t *Tracker) GetTask(fileName string) *TaskProgress {
	t.mu.RLock()
	defer t.mu.RUnlock()

	return t.Tasks[fileName]
}

// Completed returns the count of completed tasks
func (t *Tracker) Completed() int {
	t.mu.RLock()
	defer t.mu.RUnlock()

	count := 0
	for _, p := range t.Tasks {
		if p.Status == "completed" {
			count++
		}
	}
	return count
}

// Failed returns the count of failed tasks
func (t *Tracker) Failed() int {
	t.mu.RLock()
	defer t.mu.RUnlock()

	count := 0
	for _, p := range t.Tasks {
		if p.Status == "failed" {
			count++
		}
	}
	return count
}

// Skipped returns the count of skipped tasks
func (t *Tracker) Skipped() int {
	t.mu.RLock()
	defer t.mu.RUnlock()

	count := 0
	for _, p := range t.Tasks {
		if p.Status == "skipped" {
			count++
		}
	}
	return count
}

// InProgress returns the count of in-progress tasks
func (t *Tracker) InProgress() int {
	t.mu.RLock()
	defer t.mu.RUnlock()

	count := 0
	for _, p := range t.Tasks {
		if p.Status == "in_progress" {
			count++
		}
	}
	return count
}

// GetFailedTasks returns information about failed tasks
func (t *Tracker) GetFailedTasks() []FailedTaskInfo {
	t.mu.RLock()
	defer t.mu.RUnlock()

	var failed []FailedTaskInfo
	for name, p := range t.Tasks {
		if p.Status == "failed" {
			failed = append(failed, FailedTaskInfo{
				TaskID: name,
				Error:  p.Error,
			})
		}
	}
	return failed
}

// FailedTaskInfo contains information about a failed task
type FailedTaskInfo struct {
	TaskID string
	Error  string
}

// GetCompletedTasks returns the names of completed tasks
func (t *Tracker) GetCompletedTasks() []string {
	t.mu.RLock()
	defer t.mu.RUnlock()

	var completed []string
	for name, p := range t.Tasks {
		if p.Status == "completed" {
			completed = append(completed, name)
		}
	}
	return completed
}

// Summary returns a summary of the migration progress
type Summary struct {
	StartedAt   *time.Time
	UpdatedAt   *time.Time
	Total       int
	Completed   int
	Failed      int
	Skipped     int
	InProgress  int
	Pending     int
	Duration    time.Duration
	FailedTasks []FailedTaskInfo
}

// GetSummary returns a summary of progress
func (t *Tracker) GetSummary() Summary {
	t.mu.RLock()
	defer t.mu.RUnlock()

	s := Summary{
		StartedAt: t.StartedAt,
		UpdatedAt: t.UpdatedAt,
		Total:     t.Total,
	}

	for _, p := range t.Tasks {
		switch p.Status {
		case "completed":
			s.Completed++
		case "failed":
			s.Failed++
		case "skipped":
			s.Skipped++
		case "in_progress":
			s.InProgress++
		default:
			s.Pending++
		}
	}

	s.Pending = t.Total - s.Completed - s.Failed - s.Skipped - s.InProgress

	if t.StartedAt != nil && t.UpdatedAt != nil {
		s.Duration = t.UpdatedAt.Sub(*t.StartedAt)
	}

	// Get failed task info
	for name, p := range t.Tasks {
		if p.Status == "failed" {
			s.FailedTasks = append(s.FailedTasks, FailedTaskInfo{
				TaskID: name,
				Error:  p.Error,
			})
		}
	}

	return s
}

// PrintSummary prints a formatted summary
func (t *Tracker) PrintSummary() {
	s := t.GetSummary()

	fmt.Println("\n=== Migration Progress Summary ===")
	if s.StartedAt != nil {
		fmt.Printf("Started:   %s\n", s.StartedAt.Format(time.RFC3339))
	}
	if s.UpdatedAt != nil {
		fmt.Printf("Updated:   %s\n", s.UpdatedAt.Format(time.RFC3339))
	}
	fmt.Printf("Duration:  %v\n", s.Duration.Round(time.Second))
	fmt.Println()
	fmt.Printf("Total:     %d\n", s.Total)
	fmt.Printf("Completed: %d\n", s.Completed)
	fmt.Printf("Failed:    %d\n", s.Failed)
	fmt.Printf("Skipped:   %d\n", s.Skipped)
	fmt.Printf("Pending:   %d\n", s.Pending)

	if len(s.FailedTasks) > 0 {
		fmt.Println("\nFailed Tasks:")
		for _, ft := range s.FailedTasks {
			fmt.Printf("  - %s: %s\n", ft.TaskID, ft.Error)
		}
	}
	fmt.Println("==================================")
}

// Reset clears all progress
func (t *Tracker) Reset() {
	t.mu.Lock()
	defer t.mu.Unlock()

	t.StartedAt = nil
	t.UpdatedAt = nil
	t.Total = 0
	t.Tasks = make(map[string]*TaskProgress)
}

// MarkTaskInProgress marks a task as in progress
func (t *Tracker) MarkTaskInProgress(fileName, branchName string) {
	now := time.Now()
	t.UpdateTask(fileName, &TaskProgress{
		Status:     "in_progress",
		BranchName: branchName,
		StartTime:  &now,
	})
}

// MarkTaskCompleted marks a task as completed
func (t *Tracker) MarkTaskCompleted(fileName, branchName, commitHash string) {
	now := time.Now()
	existing := t.GetTask(fileName)

	progress := &TaskProgress{
		Status:     "completed",
		BranchName: branchName,
		CommitHash: commitHash,
		EndTime:    &now,
	}

	if existing != nil && existing.StartTime != nil {
		progress.StartTime = existing.StartTime
	}

	t.UpdateTask(fileName, progress)
}

// MarkTaskFailed marks a task as failed
func (t *Tracker) MarkTaskFailed(fileName, branchName, errorMsg string) {
	now := time.Now()
	existing := t.GetTask(fileName)

	progress := &TaskProgress{
		Status:     "failed",
		BranchName: branchName,
		Error:      errorMsg,
		EndTime:    &now,
	}

	if existing != nil && existing.StartTime != nil {
		progress.StartTime = existing.StartTime
	}

	t.UpdateTask(fileName, progress)
}
