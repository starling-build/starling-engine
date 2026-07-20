package task

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/anthropics/migration-orchestrator/internal/config"
	"github.com/anthropics/migration-orchestrator/internal/progress"
)

// Executor is the interface for executing migration tasks
// Defined here to avoid import cycles
type Executor interface {
	Execute(ctx context.Context, t *Task) (string, error)
}

// Scheduler orchestrates task execution
type Scheduler struct {
	queue    *Queue
	executor Executor
	progress *progress.Tracker
	cfg      *config.Config
}

// NewScheduler creates a new scheduler
func NewScheduler(queue *Queue, exec Executor, tracker *progress.Tracker, cfg *config.Config) *Scheduler {
	return &Scheduler{
		queue:    queue,
		executor: exec,
		progress: tracker,
		cfg:      cfg,
	}
}

// Run executes all tasks respecting dependencies and concurrency
func (s *Scheduler) Run(ctx context.Context) error {
	// Initialize progress
	s.progress.Start(s.queue.Len())

	// Create cancellable context for stopping on failure
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	// Create worker pool
	var wg sync.WaitGroup
	taskChan := make(chan *Task, s.cfg.MaxConcurrency)
	resultChan := make(chan *TaskResult, s.cfg.MaxConcurrency)

	// Track if we had a failure
	var failedTask *TaskResult
	var failMu sync.Mutex

	// Track in-progress count to respect concurrency
	var inProgressCount int
	var inProgressMu sync.Mutex

	// Start workers
	for i := 0; i < s.cfg.MaxConcurrency; i++ {
		wg.Add(1)
		go s.worker(ctx, i, taskChan, resultChan, &wg)
	}

	// Result handler - stops on first failure
	resultDone := make(chan struct{})
	go func() {
		defer close(resultDone)
		for result := range resultChan {
			s.handleResult(result)

			// Decrement in-progress count
			inProgressMu.Lock()
			inProgressCount--
			inProgressMu.Unlock()

			if !result.Success {
				failMu.Lock()
				if failedTask == nil {
					failedTask = result
					cancel() // Cancel context to stop all workers
				}
				failMu.Unlock()
			}
		}
	}()

	// Main scheduling loop
	for {
		select {
		case <-ctx.Done():
			close(taskChan)
			wg.Wait()
			close(resultChan)
			<-resultDone

			failMu.Lock()
			ft := failedTask
			failMu.Unlock()

			if ft != nil {
				return fmt.Errorf("migration stopped: task %s failed: %s", ft.Task.ID, ft.Error)
			}
			return ctx.Err()
		default:
		}

		// Check if all done
		if s.queue.AllComplete() {
			break
		}

		// Check how many slots are available
		inProgressMu.Lock()
		availableSlots := s.cfg.MaxConcurrency - inProgressCount
		inProgressMu.Unlock()

		if availableSlots <= 0 {
			// Wait for tasks to complete
			time.Sleep(100 * time.Millisecond)
			continue
		}

		// Get next available tasks (up to available slots)
		tasks := s.queue.NextAvailable(availableSlots)
		if len(tasks) == 0 {
			// No tasks available - check if we're stuck
			if !s.queue.HasPendingWork() {
				break
			}
			// Wait for in-progress tasks to complete
			time.Sleep(100 * time.Millisecond)
			continue
		}

		// Send tasks to workers
		for _, t := range tasks {
			// Increment in-progress count before sending
			inProgressMu.Lock()
			inProgressCount++
			inProgressMu.Unlock()

			s.queue.MarkInProgress(t.ID)
			s.progress.UpdateTask(t.ID, &progress.TaskProgress{
				Status:     string(StatusInProgress),
				BranchName: t.BranchName,
				StartTime:  t.StartTime,
			})

			fmt.Printf("\n[%s] Starting migration: %s\n", time.Now().Format("15:04:05"), t.GetDescription())

			select {
			case taskChan <- t:
			case <-ctx.Done():
				close(taskChan)
				wg.Wait()
				close(resultChan)
				<-resultDone

				failMu.Lock()
				ft := failedTask
				failMu.Unlock()

				if ft != nil {
					return fmt.Errorf("migration stopped: task %s failed: %s", ft.Task.ID, ft.Error)
				}
				return ctx.Err()
			}
		}

		// Small delay to prevent tight loop
		time.Sleep(50 * time.Millisecond)
	}

	close(taskChan)
	wg.Wait()
	close(resultChan)
	<-resultDone

	// Save final progress
	if err := s.progress.Save(); err != nil {
		fmt.Printf("Warning: failed to save progress: %v\n", err)
	}

	// Check if there was a failure
	failMu.Lock()
	ft := failedTask
	failMu.Unlock()

	if ft != nil {
		return fmt.Errorf("migration stopped: task %s failed: %s", ft.Task.ID, ft.Error)
	}

	return nil
}

// TaskResult represents the result of a task execution
type TaskResult struct {
	Task       *Task
	Success    bool
	CommitHash string
	Error      string
}

// worker processes tasks from the channel
func (s *Scheduler) worker(ctx context.Context, id int, tasks <-chan *Task, results chan<- *TaskResult, wg *sync.WaitGroup) {
	defer wg.Done()

	for task := range tasks {
		select {
		case <-ctx.Done():
			return
		default:
		}

		result := &TaskResult{Task: task}

		// Execute the task
		commitHash, err := s.executor.Execute(ctx, task)
		if err != nil {
			result.Success = false
			result.Error = err.Error()
			if s.cfg.Verbose {
				fmt.Printf("[Worker %d] Task %s failed: %v\n", id, task.ID, err)
			}
		} else {
			result.Success = true
			result.CommitHash = commitHash
			if s.cfg.Verbose {
				fmt.Printf("[Worker %d] Task %s completed successfully\n", id, task.ID)
			}
		}

		results <- result
	}
}

// handleResult processes a task result
func (s *Scheduler) handleResult(result *TaskResult) {
	t := result.Task

	if result.Success {
		s.queue.MarkCompleted(t.ID, result.CommitHash)
		s.progress.UpdateTask(t.ID, &progress.TaskProgress{
			Status:     string(StatusCompleted),
			BranchName: t.BranchName,
			CommitHash: result.CommitHash,
			StartTime:  t.StartTime,
			EndTime:    t.EndTime,
		})
		fmt.Printf("[%s] Completed: %s (commit: %s)\n",
			time.Now().Format("15:04:05"), t.GetDescription(), result.CommitHash)
	} else {
		s.queue.MarkFailed(t.ID, result.Error)
		s.progress.UpdateTask(t.ID, &progress.TaskProgress{
			Status:     string(StatusFailed),
			BranchName: t.BranchName,
			StartTime:  t.StartTime,
			EndTime:    t.EndTime,
			Error:      result.Error,
		})
		fmt.Printf("[%s] Failed: %s - %s\n",
			time.Now().Format("15:04:05"), t.GetDescription(), result.Error)
	}

	// Save progress after each task
	if err := s.progress.Save(); err != nil {
		fmt.Printf("Warning: failed to save progress: %v\n", err)
	}
}
