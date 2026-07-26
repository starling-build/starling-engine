// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package executor

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/anthropics/migration-orchestrator/internal/config"
	"github.com/anthropics/migration-orchestrator/internal/task"
)

// PromptGenerator interface for generating prompts
type PromptGenerator interface {
	Generate(t *task.Task) (string, error)
}

// ClaudeExecutor executes migration tasks using Claude CLI
type ClaudeExecutor struct {
	cfg       *config.Config
	promptGen PromptGenerator
}

// NewClaudeExecutor creates a new Claude executor
func NewClaudeExecutor(cfg *config.Config, promptGen PromptGenerator) *ClaudeExecutor {
	return &ClaudeExecutor{
		cfg:       cfg,
		promptGen: promptGen,
	}
}

// Execute runs a migration task using Claude CLI
func (e *ClaudeExecutor) Execute(ctx context.Context, t *task.Task) (string, error) {
	// Generate prompt
	promptText, err := e.promptGen.Generate(t)
	if err != nil {
		return "", fmt.Errorf("failed to generate prompt: %w", err)
	}

	// Save prompt to file for reference/debugging
	if _, err := e.savePromptToFile(t, promptText); err != nil {
		return "", fmt.Errorf("failed to save prompt: %w", err)
	}
	// Keep prompt file for debugging, don't remove

	// Build claude command
	cmd := e.buildCommand(ctx, t, promptText)

	// Create log file for output
	logFile, err := e.createLogFile(t)
	if err != nil {
		return "", fmt.Errorf("failed to create log file: %w", err)
	}
	defer logFile.Close()

	// Redirect stdout and stderr to log file with immediate flush
	syncWriter := &syncWriter{f: logFile}
	cmd.Stdout = syncWriter
	cmd.Stderr = syncWriter

	// Start the command
	if e.cfg.Verbose {
		fmt.Printf("  Starting Claude CLI for %s\n", t.FileName)
	}

	if err := cmd.Start(); err != nil {
		return "", fmt.Errorf("failed to start claude: %w", err)
	}

	// Wait for completion or context cancellation
	done := make(chan error, 1)
	go func() {
		done <- cmd.Wait()
	}()

	select {
	case <-ctx.Done():
		cmd.Process.Kill()
		return "", ctx.Err()
	case err := <-done:
		if err != nil {
			// Read log file for error output
			output := e.readLogFile(logFile.Name())
			return "", fmt.Errorf("claude failed: %w\nOutput: %s", err, truncateOutput(output, 500))
		}
	}

	// Read log file to check for success/blocked markers
	output := e.readLogFile(logFile.Name())

	// Check for blocked marker first (Claude couldn't figure out how to migrate)
	if strings.Contains(output, "MIGRATION_BLOCKED:") {
		// Extract the reason if possible
		reason := extractBlockedReason(output)
		return "", fmt.Errorf("migration blocked: %s", reason)
	}

	// Check for success marker
	if !strings.Contains(output, "MIGRATION_COMPLETE:") {
		return "", fmt.Errorf("migration did not complete successfully")
	}

	// Extract commit hash from git log
	commitHash, err := e.getLatestCommitHash(t)
	if err != nil {
		// Not fatal - migration might have succeeded but commit format differs
		if e.cfg.Verbose {
			fmt.Printf("  Warning: could not get commit hash: %v\n", err)
		}
		commitHash = "unknown"
	}

	return commitHash, nil
}

// buildCommand creates the claude CLI command
func (e *ClaudeExecutor) buildCommand(ctx context.Context, _ *task.Task, promptText string) *exec.Cmd {
	args := []string{
		"--print",                        // Print output and exit
		"--verbose",                      // Show detailed output for debugging
		"--output-format", "stream-json", // Stream output in real-time
		"--dangerously-skip-permissions", // Skip permission prompts
		"-p", promptText,                 // Pass prompt directly
	}

	cmd := exec.CommandContext(ctx, "claude", args...)
	cmd.Dir = e.cfg.WorkspacePath

	// Set environment
	cmd.Env = append(os.Environ(),
		"CLAUDE_CODE_ENTRYPOINT=migration-orchestrator",
	)

	return cmd
}

// savePromptToFile saves the prompt to a temporary file
func (e *ClaudeExecutor) savePromptToFile(t *task.Task, promptText string) (string, error) {
	// Create prompts directory
	promptDir := filepath.Join(e.cfg.WorkspacePath, ".migration", "prompts")
	if err := os.MkdirAll(promptDir, 0755); err != nil {
		return "", err
	}

	// Write prompt file
	fileName := fmt.Sprintf("%s_%d.txt", t.GetBaseName(), time.Now().Unix())
	filePath := filepath.Join(promptDir, fileName)

	if err := os.WriteFile(filePath, []byte(promptText), 0644); err != nil {
		return "", err
	}

	return filePath, nil
}

// createLogFile creates a log file for the task output
func (e *ClaudeExecutor) createLogFile(t *task.Task) (*os.File, error) {
	// Create logs directory
	logDir := filepath.Join(e.cfg.WorkspacePath, ".migration", "logs")
	if err := os.MkdirAll(logDir, 0755); err != nil {
		return nil, err
	}

	// Create log file
	fileName := fmt.Sprintf("%s_%s.log", t.GetBaseName(), time.Now().Format("20060102_150405"))
	filePath := filepath.Join(logDir, fileName)

	return os.Create(filePath)
}

// syncWriter wraps an os.File and syncs after each write
type syncWriter struct {
	f *os.File
}

func (w *syncWriter) Write(p []byte) (n int, err error) {
	n, err = w.f.Write(p)
	if err == nil {
		w.f.Sync()
	}
	return
}

// readLogFile reads the contents of a log file
func (e *ClaudeExecutor) readLogFile(path string) string {
	data, err := os.ReadFile(path)
	if err != nil {
		return ""
	}
	return string(data)
}

// getLatestCommitHash gets the latest commit hash from the workspace
func (e *ClaudeExecutor) getLatestCommitHash(t *task.Task) (string, error) {
	cmd := exec.Command("git", "log", "-1", "--format=%H")
	cmd.Dir = e.cfg.WorkspacePath

	output, err := cmd.Output()
	if err != nil {
		return "", err
	}

	return strings.TrimSpace(string(output)), nil
}

// truncateOutput truncates output to maxLen characters
func truncateOutput(s string, maxLen int) string {
	if len(s) <= maxLen {
		return s
	}
	return s[:maxLen] + "..."
}

// MockExecutor is a mock executor for testing
type MockExecutor struct {
	Results map[string]MockResult
}

// MockResult represents a mock execution result
type MockResult struct {
	CommitHash string
	Error      error
}

// NewMockExecutor creates a new mock executor
func NewMockExecutor() *MockExecutor {
	return &MockExecutor{
		Results: make(map[string]MockResult),
	}
}

// Execute returns a mock result
func (e *MockExecutor) Execute(ctx context.Context, t *task.Task) (string, error) {
	if result, ok := e.Results[t.FileName]; ok {
		return result.CommitHash, result.Error
	}
	// Default success
	return fmt.Sprintf("mock_%s", t.GetBaseName()), nil
}

// SetResult sets a mock result for a task
func (e *MockExecutor) SetResult(fileName string, commitHash string, err error) {
	e.Results[fileName] = MockResult{
		CommitHash: commitHash,
		Error:      err,
	}
}

// DryRunExecutor simulates execution without actually running Claude
type DryRunExecutor struct {
	cfg       *config.Config
	promptGen PromptGenerator
}

// NewDryRunExecutor creates a new dry run executor
func NewDryRunExecutor(cfg *config.Config, promptGen PromptGenerator) *DryRunExecutor {
	return &DryRunExecutor{
		cfg:       cfg,
		promptGen: promptGen,
	}
}

// Execute prints what would be done without executing
func (e *DryRunExecutor) Execute(ctx context.Context, t *task.Task) (string, error) {
	promptText, err := e.promptGen.Generate(t)
	if err != nil {
		return "", err
	}

	fmt.Printf("\n=== DRY RUN: %s ===\n", t.FileName)
	fmt.Printf("Would create branch: %s\n", t.BranchName)
	fmt.Printf("Would create Swift file: %s\n", t.GetSwiftFileName())
	fmt.Printf("Prompt preview (first 500 chars):\n%s\n", truncateOutput(promptText, 500))
	fmt.Printf("=== END DRY RUN ===\n")

	// Simulate commit hash
	return fmt.Sprintf("dryrun_%s", t.GetBaseName()), nil
}

// extractBlockedReason extracts the reason from a MIGRATION_BLOCKED message
func extractBlockedReason(output string) string {
	// Look for "MIGRATION_BLOCKED: task_id - reason"
	re := regexp.MustCompile(`MIGRATION_BLOCKED:\s*\S+\s*-\s*(.+)`)
	if matches := re.FindStringSubmatch(output); len(matches) > 1 {
		return strings.TrimSpace(matches[1])
	}
	return "unknown reason"
}

// ExtractCommitHashFromOutput attempts to extract a commit hash from Claude output
func ExtractCommitHashFromOutput(output string) string {
	// Look for patterns like "commit abc1234" or "[abc1234]"
	patterns := []string{
		`commit\s+([a-f0-9]{7,40})`,
		`\[([a-f0-9]{7,40})\]`,
		`Committed:\s*([a-f0-9]{7,40})`,
	}

	for _, pattern := range patterns {
		re := regexp.MustCompile(pattern)
		if matches := re.FindStringSubmatch(output); len(matches) > 1 {
			return matches[1]
		}
	}

	return ""
}
