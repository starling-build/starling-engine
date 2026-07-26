// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cmd

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"

	"github.com/anthropics/migration-orchestrator/internal/config"
	"github.com/anthropics/migration-orchestrator/internal/executor"
	"github.com/anthropics/migration-orchestrator/internal/parser"
	"github.com/anthropics/migration-orchestrator/internal/progress"
	"github.com/anthropics/migration-orchestrator/internal/prompt"
	"github.com/anthropics/migration-orchestrator/internal/task"
	"github.com/spf13/cobra"
)

var (
	cfg *config.Config
)

var rootCmd = &cobra.Command{
	Use:   "migration-orchestrator",
	Short: "Orchestrate Dart to Swift migration using Claude",
	Long: `Migration Orchestrator automates the process of migrating Flutter's
dart:ui layer from Dart to Swift 6 using Claude CLI.

It reads metadata from extract_metadata tool output, creates migration tasks
respecting dependency order, launches Claude processes, and tracks progress.`,
	RunE: runMigration,
}

func Execute() error {
	return rootCmd.Execute()
}

func init() {
	// Input paths
	rootCmd.Flags().StringP("metadata", "m", "", "Path to result.json or result.txt from extract_metadata (required)")
	rootCmd.Flags().StringP("guide", "g", "", "Path to SWIFT_MIGRATION_GUIDE.md")
	rootCmd.Flags().StringP("workspace", "w", ".", "Flutter workspace path (defaults to the current directory)")

	// Concurrency
	rootCmd.Flags().IntP("concurrency", "c", 1, "Max concurrent Claude processes")

	// Git settings
	rootCmd.Flags().String("base-branch", "scheduler-test", "Branch to merge completed work to")
	rootCmd.Flags().String("branch-prefix", "migrate/", "Prefix for feature branches")
	rootCmd.Flags().Bool("auto-merge", true, "Automatically merge to base branch when task completes")
	rootCmd.Flags().Bool("auto-push", true, "Automatically push after merge")

	// Build settings
	rootCmd.Flags().Bool("build-cpp", true, "Run C++ engine build (et build)")
	rootCmd.Flags().Bool("build-swift", true, "Run Swift package build")

	// Progress
	rootCmd.Flags().StringP("progress", "p", "", "Progress file path (default: .migration/progress.json in workspace)")
	rootCmd.Flags().BoolP("resume", "r", false, "Resume from progress file")

	// Filtering
	rootCmd.Flags().StringSlice("skip", nil, "Files to skip (comma-separated)")
	rootCmd.Flags().StringSlice("only", nil, "Only process these files (comma-separated)")

	// Modes
	rootCmd.Flags().Bool("dry-run", false, "Show plan without executing")
	rootCmd.Flags().Bool("no-build", false, "Skip build verification")
	rootCmd.Flags().Bool("no-merge", false, "Don't merge to base branch")
	rootCmd.Flags().Bool("no-push", false, "Don't push after merge")

	// Output
	rootCmd.Flags().BoolP("verbose", "v", false, "Verbose output")

	rootCmd.MarkFlagRequired("metadata")
}

func runMigration(cmd *cobra.Command, args []string) error {
	// Build config from flags
	cfg = &config.Config{}

	cfg.MetadataPath, _ = cmd.Flags().GetString("metadata")
	cfg.MigrationGuide, _ = cmd.Flags().GetString("guide")
	cfg.WorkspacePath, _ = cmd.Flags().GetString("workspace")
	cfg.MaxConcurrency, _ = cmd.Flags().GetInt("concurrency")
	cfg.BaseBranch, _ = cmd.Flags().GetString("base-branch")
	cfg.BranchPrefix, _ = cmd.Flags().GetString("branch-prefix")
	cfg.AutoMerge, _ = cmd.Flags().GetBool("auto-merge")
	cfg.AutoPush, _ = cmd.Flags().GetBool("auto-push")
	cfg.BuildCpp, _ = cmd.Flags().GetBool("build-cpp")
	cfg.BuildSwift, _ = cmd.Flags().GetBool("build-swift")
	cfg.ProgressFile, _ = cmd.Flags().GetString("progress")
	cfg.Resume, _ = cmd.Flags().GetBool("resume")
	cfg.SkipFiles, _ = cmd.Flags().GetStringSlice("skip")
	cfg.OnlyFiles, _ = cmd.Flags().GetStringSlice("only")
	cfg.DryRun, _ = cmd.Flags().GetBool("dry-run")
	cfg.NoBuild, _ = cmd.Flags().GetBool("no-build")
	cfg.NoMerge, _ = cmd.Flags().GetBool("no-merge")
	cfg.NoPush, _ = cmd.Flags().GetBool("no-push")
	cfg.Verbose, _ = cmd.Flags().GetBool("verbose")

	// Override build flags if no-build is set
	if cfg.NoBuild {
		cfg.BuildCpp = false
		cfg.BuildSwift = false
	}

	// Override merge/push flags
	if cfg.NoMerge {
		cfg.AutoMerge = false
	}
	if cfg.NoPush {
		cfg.AutoPush = false
	}

	// Set default progress file path in workspace's .migration directory
	if cfg.ProgressFile == "" {
		cfg.ProgressFile = filepath.Join(cfg.WorkspacePath, ".migration", "progress.json")
	}

	// Validate config
	if err := cfg.Validate(); err != nil {
		return fmt.Errorf("invalid configuration: %w", err)
	}

	// Set up context with cancellation
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Handle signals for graceful shutdown
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-sigChan
		fmt.Println("\nReceived shutdown signal, finishing current tasks...")
		cancel()
	}()

	// Run the migration
	return run(ctx, cfg)
}

func run(ctx context.Context, cfg *config.Config) error {
	fmt.Printf("Migration Orchestrator\n")
	fmt.Printf("======================\n\n")

	// Step 1: Parse metadata
	fmt.Printf("Parsing metadata from: %s\n", cfg.MetadataPath)
	p, err := parser.New(cfg.MetadataPath)
	if err != nil {
		return fmt.Errorf("failed to create parser: %w", err)
	}

	metadata, err := p.Parse()
	if err != nil {
		return fmt.Errorf("failed to parse metadata: %w", err)
	}

	fmt.Printf("Found %d files to migrate\n", metadata.FileCount)
	fmt.Printf("Migration order: %v\n\n", metadata.MigrationOrder)

	if len(metadata.CircularDependencies) > 0 {
		fmt.Printf("WARNING: Circular dependencies detected:\n")
		for _, cycle := range metadata.CircularDependencies {
			fmt.Printf("  - %v\n", cycle)
		}
		fmt.Println()
	}

	// Step 2: Create task queue
	fmt.Println("Creating task queue...")
	queue := task.NewQueue(metadata, cfg)
	fmt.Printf("Created %d tasks\n\n", queue.Len())

	// Step 3: Load progress if resuming
	tracker := progress.NewTracker(cfg.ProgressFile)
	if cfg.Resume {
		fmt.Println("Resuming from progress file...")
		if err := tracker.Load(); err != nil {
			fmt.Printf("Warning: could not load progress file: %v\n", err)
		} else {
			queue.ApplyProgress(tracker)
			fmt.Printf("Resumed: %d completed, %d failed, %d pending\n\n",
				tracker.Completed(), tracker.Failed(), queue.Pending())
		}
	}

	// Step 4: Show plan in dry-run mode
	if cfg.DryRun {
		fmt.Println("DRY RUN - Tasks to be executed:")
		fmt.Println("================================")

		// Show summary by type
		summary := queue.Summary()
		fmt.Printf("\nTask Summary:\n")
		for taskType, count := range summary {
			fmt.Printf("  - %s: %d\n", taskType, count)
		}
		fmt.Println()

		// Show all pending tasks
		for i, t := range queue.GetPendingTasks() {
			fmt.Printf("%d. [%s] %s (from %s)\n", i+1, t.TaskType, t.Name, t.FileName)
			fmt.Printf("   ID: %s\n", t.ID)
			if loc := t.GetLocation(); loc != nil {
				if loc.EndLine > 0 && loc.EndLine != loc.Line {
					fmt.Printf("   Lines: %d-%d\n", loc.Line, loc.EndLine)
				} else {
					fmt.Printf("   Line: %d\n", loc.Line)
				}
			}
			fmt.Printf("   Branch: %s\n", t.BranchName)
			fmt.Printf("   Swift file: %s\n", t.GetSwiftFileName())
			if len(t.DependsOn) > 0 {
				fmt.Printf("   Depends on: %v\n", t.DependsOn)
			}
			if len(t.DependedBy) > 0 {
				fmt.Printf("   Required by: %v\n", t.DependedBy)
			}
		}
		return nil
	}

	// Step 5: Create prompt generator
	promptGen, err := prompt.NewGenerator(cfg)
	if err != nil {
		return fmt.Errorf("failed to create prompt generator: %w", err)
	}

	// Step 6: Create executor
	exec := executor.NewClaudeExecutor(cfg, promptGen)

	// Step 7: Create and run scheduler
	scheduler := task.NewScheduler(queue, exec, tracker, cfg)

	fmt.Println("Starting migration...")
	fmt.Println("=====================")

	if err := scheduler.Run(ctx); err != nil {
		return fmt.Errorf("migration failed: %w", err)
	}

	// Step 8: Print summary
	fmt.Println("\nMigration Summary")
	fmt.Println("=================")
	fmt.Printf("Completed: %d\n", tracker.Completed())
	fmt.Printf("Failed: %d\n", tracker.Failed())
	fmt.Printf("Skipped: %d\n", tracker.Skipped())

	if tracker.Failed() > 0 {
		fmt.Println("\nFailed tasks:")
		for _, t := range tracker.GetFailedTasks() {
			fmt.Printf("  - %s: %s\n", t.TaskID, t.Error)
		}
	}

	return nil
}
