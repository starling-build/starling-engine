package config

import (
	"fmt"
	"os"
	"path/filepath"
)

// Config holds all configuration for the migration orchestrator
type Config struct {
	// Input paths
	MetadataPath   string `json:"metadata_path"`   // Path to result.json or result.txt
	MigrationGuide string `json:"migration_guide"` // Path to SWIFT_MIGRATION_GUIDE.md
	WorkspacePath  string `json:"workspace_path"`  // Flutter workspace path

	// Concurrency
	MaxConcurrency int `json:"max_concurrency"` // Max concurrent Claude processes

	// Git settings
	BaseBranch   string `json:"base_branch"`   // Branch to merge completed work to
	BranchPrefix string `json:"branch_prefix"` // Prefix for feature branches
	AutoMerge    bool   `json:"auto_merge"`    // Merge to base branch when done
	AutoPush     bool   `json:"auto_push"`     // Push after merge

	// Build settings
	BuildCpp   bool `json:"build_cpp"`   // Run et build
	BuildSwift bool `json:"build_swift"` // Run swift build

	// Progress
	ProgressFile string `json:"progress_file"` // migration_progress.json
	Resume       bool   `json:"resume"`        // Resume from progress file

	// Filtering
	SkipFiles []string `json:"skip_files"` // Files to skip
	OnlyFiles []string `json:"only_files"` // Only process these files

	// Modes
	DryRun  bool `json:"dry_run"`  // Show plan without executing
	NoBuild bool `json:"no_build"` // Skip build verification
	NoMerge bool `json:"no_merge"` // Don't merge to base branch
	NoPush  bool `json:"no_push"`  // Don't push after merge

	// Output
	Verbose bool `json:"verbose"` // Verbose output
}

// Validate checks that the configuration is valid
func (c *Config) Validate() error {
	// Check metadata path exists
	if c.MetadataPath == "" {
		return fmt.Errorf("metadata path is required")
	}
	if _, err := os.Stat(c.MetadataPath); os.IsNotExist(err) {
		return fmt.Errorf("metadata file does not exist: %s", c.MetadataPath)
	}

	// Check workspace exists
	if c.WorkspacePath == "" {
		return fmt.Errorf("workspace path is required")
	}
	if _, err := os.Stat(c.WorkspacePath); os.IsNotExist(err) {
		return fmt.Errorf("workspace does not exist: %s", c.WorkspacePath)
	}

	// Check migration guide if specified
	if c.MigrationGuide != "" {
		if _, err := os.Stat(c.MigrationGuide); os.IsNotExist(err) {
			return fmt.Errorf("migration guide does not exist: %s", c.MigrationGuide)
		}
	}

	// Validate concurrency
	if c.MaxConcurrency < 1 {
		c.MaxConcurrency = 1
	}

	return nil
}

// GetMigrationGuide returns the path to the migration guide, using default if not specified
func (c *Config) GetMigrationGuide() string {
	if c.MigrationGuide != "" {
		return c.MigrationGuide
	}
	// Default: look relative to metadata path
	dir := filepath.Dir(c.MetadataPath)
	return filepath.Join(dir, "..", "..", "SWIFT_MIGRATION_GUIDE.md")
}

// ShouldSkip returns true if the given file should be skipped
func (c *Config) ShouldSkip(fileName string) bool {
	// If OnlyFiles is specified, skip anything not in the list
	if len(c.OnlyFiles) > 0 {
		for _, f := range c.OnlyFiles {
			if f == fileName {
				return false
			}
		}
		return true
	}

	// Check if in skip list
	for _, f := range c.SkipFiles {
		if f == fileName {
			return true
		}
	}

	return false
}

// GetEnginePath returns the path to the engine source
func (c *Config) GetEnginePath() string {
	return filepath.Join(c.WorkspacePath, "engine", "src")
}

// GetSwiftPackagePath returns the path to the Swift package
func (c *Config) GetSwiftPackagePath() string {
	return filepath.Join(c.WorkspacePath, "flutter_swift")
}

// GetBridgeHeaderPath returns the path for C++ bridge headers
func (c *Config) GetBridgeHeaderPath() string {
	return filepath.Join(c.GetEnginePath(), "flutter", "lib", "ui", "swift", "include")
}

// GetBridgeSourcePath returns the path for C++ bridge implementations
func (c *Config) GetBridgeSourcePath() string {
	return filepath.Join(c.GetEnginePath(), "flutter", "lib", "ui", "swift", "src")
}
