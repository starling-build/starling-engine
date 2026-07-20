package git

import (
	"fmt"
	"os/exec"
	"strings"
)

// Operations provides git operations
type Operations struct {
	workDir string
}

// NewOperations creates a new git operations instance
func NewOperations(workDir string) *Operations {
	return &Operations{workDir: workDir}
}

// run executes a git command
func (g *Operations) run(args ...string) (string, error) {
	cmd := exec.Command("git", args...)
	cmd.Dir = g.workDir

	output, err := cmd.CombinedOutput()
	if err != nil {
		return string(output), fmt.Errorf("git %s failed: %w\nOutput: %s",
			strings.Join(args, " "), err, string(output))
	}

	return strings.TrimSpace(string(output)), nil
}

// GetCurrentBranch returns the current branch name
func (g *Operations) GetCurrentBranch() (string, error) {
	return g.run("rev-parse", "--abbrev-ref", "HEAD")
}

// BranchExists checks if a branch exists
func (g *Operations) BranchExists(branch string) bool {
	_, err := g.run("rev-parse", "--verify", branch)
	return err == nil
}

// CreateBranch creates a new branch from base
func (g *Operations) CreateBranch(branchName, baseBranch string) error {
	// First ensure we're on the base branch
	if _, err := g.run("checkout", baseBranch); err != nil {
		return fmt.Errorf("failed to checkout base branch: %w", err)
	}

	// Pull latest
	g.run("pull", "origin", baseBranch) // Ignore error - might not have remote

	// Create and checkout new branch
	if _, err := g.run("checkout", "-b", branchName); err != nil {
		// Branch might already exist, try switching to it
		if _, err := g.run("checkout", branchName); err != nil {
			return fmt.Errorf("failed to create/checkout branch: %w", err)
		}
	}

	return nil
}

// Checkout switches to a branch
func (g *Operations) Checkout(branch string) error {
	_, err := g.run("checkout", branch)
	return err
}

// Add stages files for commit
func (g *Operations) Add(paths ...string) error {
	args := append([]string{"add"}, paths...)
	_, err := g.run(args...)
	return err
}

// AddAll stages all changes
func (g *Operations) AddAll() error {
	_, err := g.run("add", "-A")
	return err
}

// Commit creates a commit with the given message
func (g *Operations) Commit(message string) error {
	_, err := g.run("commit", "-m", message)
	return err
}

// GetLatestCommitHash returns the latest commit hash
func (g *Operations) GetLatestCommitHash() (string, error) {
	return g.run("log", "-1", "--format=%H")
}

// GetLatestCommitShortHash returns the short commit hash
func (g *Operations) GetLatestCommitShortHash() (string, error) {
	return g.run("log", "-1", "--format=%h")
}

// Merge merges source branch into target branch
func (g *Operations) Merge(sourceBranch, targetBranch string) error {
	// Checkout target
	if _, err := g.run("checkout", targetBranch); err != nil {
		return fmt.Errorf("failed to checkout target branch: %w", err)
	}

	// Merge
	message := fmt.Sprintf("Merge branch '%s' into %s", sourceBranch, targetBranch)
	_, err := g.run("merge", sourceBranch, "--no-ff", "-m", message)
	return err
}

// Push pushes the branch to remote
func (g *Operations) Push(branch string) error {
	_, err := g.run("push", "origin", branch)
	return err
}

// PushWithSetUpstream pushes and sets up tracking
func (g *Operations) PushWithSetUpstream(branch string) error {
	_, err := g.run("push", "-u", "origin", branch)
	return err
}

// Status returns the git status
func (g *Operations) Status() (string, error) {
	return g.run("status", "--short")
}

// HasChanges returns true if there are uncommitted changes
func (g *Operations) HasChanges() bool {
	output, err := g.Status()
	if err != nil {
		return false
	}
	return len(strings.TrimSpace(output)) > 0
}

// Stash stashes current changes
func (g *Operations) Stash() error {
	_, err := g.run("stash")
	return err
}

// StashPop pops the latest stash
func (g *Operations) StashPop() error {
	_, err := g.run("stash", "pop")
	return err
}

// Reset resets to HEAD
func (g *Operations) Reset() error {
	_, err := g.run("reset", "--hard", "HEAD")
	return err
}

// Clean removes untracked files
func (g *Operations) Clean() error {
	_, err := g.run("clean", "-fd")
	return err
}

// DeleteBranch deletes a branch
func (g *Operations) DeleteBranch(branch string) error {
	_, err := g.run("branch", "-D", branch)
	return err
}

// GetLog returns recent commit log
func (g *Operations) GetLog(count int) (string, error) {
	return g.run("log", fmt.Sprintf("-%d", count), "--oneline")
}

// GetDiff returns the diff of staged changes
func (g *Operations) GetDiff() (string, error) {
	return g.run("diff", "--cached")
}

// IsClean returns true if working directory is clean
func (g *Operations) IsClean() bool {
	output, err := g.run("status", "--porcelain")
	if err != nil {
		return false
	}
	return len(strings.TrimSpace(output)) == 0
}

// EnsureClean ensures the working directory is clean
func (g *Operations) EnsureClean() error {
	if !g.IsClean() {
		return fmt.Errorf("working directory is not clean")
	}
	return nil
}

// Helper functions

// CreateBranchAndCheckout creates a new branch from base and checks it out
func CreateBranchAndCheckout(workDir, branchName, baseBranch string) error {
	ops := NewOperations(workDir)
	return ops.CreateBranch(branchName, baseBranch)
}

// CommitChanges stages all changes and commits
func CommitChanges(workDir, message string) error {
	ops := NewOperations(workDir)
	if err := ops.AddAll(); err != nil {
		return err
	}
	return ops.Commit(message)
}

// MergeBranch merges source into target
func MergeBranch(workDir, sourceBranch, targetBranch string) error {
	ops := NewOperations(workDir)
	return ops.Merge(sourceBranch, targetBranch)
}

// PushBranch pushes a branch to origin
func PushBranch(workDir, branch string) error {
	ops := NewOperations(workDir)
	return ops.Push(branch)
}

// GetCommitHash returns the current commit hash
func GetCommitHash(workDir string) (string, error) {
	ops := NewOperations(workDir)
	return ops.GetLatestCommitHash()
}
