# Migration Orchestrator Tool - Implementation Plan

## Overview

A Go-based orchestrator tool that reads metadata output from `extract_metadata`, creates migration tasks, schedules Claude CLI processes to execute Dart-to-Swift migrations, and tracks progress.

## Key Inputs

1. **Metadata Files**: `result.json` or `result.txt` from `extract_metadata` tool
2. **Migration Guide**: `SWIFT_MIGRATION_GUIDE.md` - rules for migrating Dart to Swift
3. **Workspace**: `<workspace>/` - the Flutter engine repository

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Migration Orchestrator                          │
├─────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                 │
│  │   Config    │  │   Parser    │  │   Task      │                 │
│  │   Loader    │  │ (JSON/TXT)  │  │   Queue     │                 │
│  └─────────────┘  └─────────────┘  └─────────────┘                 │
│         │               │                │                          │
│         ▼               ▼                ▼                          │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    Task Scheduler                            │   │
│  │  - Respects dependency order (migrationOrder from JSON)      │   │
│  │  - Configurable concurrency (default: 1, expandable)         │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                      │
│         ┌────────────────────┼────────────────────┐                │
│         ▼                    ▼                    ▼                │
│  ┌─────────────┐      ┌─────────────┐      ┌─────────────┐        │
│  │   Claude    │      │   Claude    │      │   Claude    │        │
│  │  Process 1  │      │  Process 2  │      │  Process N  │        │
│  └─────────────┘      └─────────────┘      └─────────────┘        │
│         │                    │                    │                 │
│         └────────────────────┼────────────────────┘                │
│                              ▼                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                  Progress Tracker                            │   │
│  │  - Task status (pending/in_progress/completed/failed)        │   │
│  │  - Persistence to JSON file                                  │   │
│  │  - Resume capability                                         │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

## Directory Structure

```
dev/migration_orchestrator/
├── main.go                    # Entry point, CLI parsing
├── config/
│   └── config.go              # Configuration management
├── parser/
│   ├── parser.go              # Interface for parsing
│   ├── json_parser.go         # Parse result.json format
│   └── txt_parser.go          # Parse result.txt (compact) format
├── task/
│   ├── task.go                # Task data structures
│   ├── queue.go               # Task queue management
│   └── scheduler.go           # Task scheduling logic
├── executor/
│   ├── executor.go            # Interface for execution
│   └── claude_executor.go     # Claude CLI process management
├── progress/
│   └── tracker.go             # Progress tracking and persistence
├── prompt/
│   └── generator.go           # Generate prompts for Claude
├── git/
│   └── operations.go          # Git branch/merge operations
├── build/
│   └── runner.go              # Build verification (C++ and Swift)
└── go.mod                     # Go module file
```

## Data Structures

### Task

```go
type Task struct {
    ID           string            `json:"id"`
    FilePath     string            `json:"file_path"`
    FileName     string            `json:"file_name"`
    Status       TaskStatus        `json:"status"`
    Priority     int               `json:"priority"`       // Based on migrationOrder
    DependsOn    []string          `json:"depends_on"`     // File paths
    DependedBy   []string          `json:"depended_by"`    // File paths
    Exports      []string          `json:"exports"`        // Exported symbols
    Imports      []ImportedSymbol  `json:"imports"`        // Imported symbols
    Declarations DeclSummary       `json:"declarations"`   // Summary of classes, functions, etc.
    BranchName   string            `json:"branch_name"`
    StartTime    *time.Time        `json:"start_time,omitempty"`
    EndTime      *time.Time        `json:"end_time,omitempty"`
    Error        string            `json:"error,omitempty"`
    CommitHash   string            `json:"commit_hash,omitempty"`
}

type TaskStatus string
const (
    StatusPending    TaskStatus = "pending"
    StatusInProgress TaskStatus = "in_progress"
    StatusCompleted  TaskStatus = "completed"
    StatusFailed     TaskStatus = "failed"
    StatusSkipped    TaskStatus = "skipped"
)
```

### Config

```go
type Config struct {
    // Input paths
    MetadataPath     string `json:"metadata_path"`      // result.json or result.txt
    MigrationGuide   string `json:"migration_guide"`    // SWIFT_MIGRATION_GUIDE.md
    WorkspacePath    string `json:"workspace_path"`     // <workspace>/

    // Concurrency
    MaxConcurrency   int    `json:"max_concurrency"`    // Default: 1

    // Git settings
    BaseBranch       string `json:"base_branch"`        // swift3
    BranchPrefix     string `json:"branch_prefix"`      // migrate/
    AutoMerge        bool   `json:"auto_merge"`         // Merge to swift3 when done
    AutoPush         bool   `json:"auto_push"`          // Push after merge

    // Build settings
    BuildCpp         bool   `json:"build_cpp"`          // Run et build
    BuildSwift       bool   `json:"build_swift"`        // Run swift build

    // Progress
    ProgressFile     string `json:"progress_file"`      // migration_progress.json
    Resume           bool   `json:"resume"`             // Resume from progress file
}
```

## Core Components

### 1. Parser Module (`parser/`)

**Responsibilities:**
- Parse JSON format (`result.json`) with full metadata
- Parse compact TXT format (`result.txt`) for simpler task creation
- Extract migration order from `globalDependencyGraph.migrationOrder`
- Build dependency graph for proper scheduling

**Key Functions:**
```go
type Parser interface {
    Parse(path string) (*MetadataResult, error)
}

type MetadataResult struct {
    Version              string
    FileCount            int
    MigrationOrder       []string
    CircularDependencies [][]string
    Files                []FileMetadata
}
```

### 2. Task Queue (`task/queue.go`)

**Responsibilities:**
- Create tasks from parsed metadata
- Maintain task ordering based on dependencies
- Provide next available task(s) respecting concurrency limits
- Handle task state transitions

**Key Functions:**
```go
type TaskQueue struct {
    tasks       map[string]*Task
    order       []string          // Migration order
    inProgress  map[string]bool
    completed   map[string]bool
    mu          sync.RWMutex
}

func (q *TaskQueue) NextAvailable(n int) []*Task    // Get up to n tasks ready to run
func (q *TaskQueue) MarkInProgress(taskID string)   // Start a task
func (q *TaskQueue) MarkCompleted(taskID string)    // Complete a task
func (q *TaskQueue) MarkFailed(taskID, error string)// Fail a task
func (q *TaskQueue) CanRun(task *Task) bool         // Check if dependencies are met
```

### 3. Scheduler (`task/scheduler.go`)

**Responsibilities:**
- Orchestrate task execution
- Manage concurrency (worker pool pattern)
- Handle failures and retries
- Coordinate with progress tracker

**Key Functions:**
```go
type Scheduler struct {
    queue     *TaskQueue
    executor  Executor
    progress  *ProgressTracker
    config    *Config
    workers   int
}

func (s *Scheduler) Run(ctx context.Context) error
func (s *Scheduler) runWorker(ctx context.Context, id int)
```

### 4. Claude Executor (`executor/claude_executor.go`)

**Responsibilities:**
- Generate migration prompts for each task
- Launch Claude CLI process with proper arguments
- Capture stdout/stderr
- Detect success/failure from Claude output
- Handle timeouts

**Key Functions:**
```go
type ClaudeExecutor struct {
    config      *Config
    promptGen   *PromptGenerator
}

func (e *ClaudeExecutor) Execute(ctx context.Context, task *Task) error
func (e *ClaudeExecutor) buildCommand(task *Task) *exec.Cmd
```

**Claude CLI Invocation:**
```bash
claude --print --dangerously-skip-permissions \
    --prompt "$(cat migration_prompt.txt)" \
    --cwd <workspace>/
```

### 5. Prompt Generator (`prompt/generator.go`)

**Responsibilities:**
- Read SWIFT_MIGRATION_GUIDE.md
- Generate task-specific migration prompts
- Include file metadata, dependencies, exports
- Embed git workflow instructions

**Prompt Template Structure:**
```
# Migration Task: {file_name}

## Context
You are migrating Flutter's dart:ui layer from Dart to Swift 6.

## Source File
Path: {file_path}
Exports: {exports}
Dependencies: {dependencies}

## File Metadata
{json_metadata_for_file}

## Migration Guide
{SWIFT_MIGRATION_GUIDE.md contents}

## Your Task

1. **Git Operations**
   - Checkout a new branch: `{branch_prefix}{file_name_no_ext}`
   - Base branch: `{base_branch}`

2. **Migration Steps**
   - Create C++ bridge header in `engine/src/flutter/lib/ui/swift/include/`
   - Create C++ bridge implementation in `engine/src/flutter/lib/ui/swift/src/`
   - Create Swift wrapper in `flutter_swift/Sources/FlutterSwiftBridge/`
   - Update module.modulemap
   - Update BUILD.gn

3. **Build Verification**
   - Run: `cd engine/src && et build --config ci/host_debug_unopt_arm64`
   - Run: `cd flutter_swift && swift build`

4. **Commit and Merge**
   - Commit with message: "Migrate {file_name} to Swift"
   - Merge to {base_branch}
   - Push to remote

## Important Rules
- Follow ALL patterns in the Migration Guide
- Include Dart source references in ALL Swift code
- Document ANY differences from Dart implementation
- Do NOT skip build verification
```

### 6. Git Operations (`git/operations.go`)

**Responsibilities:**
- Create feature branches
- Commit changes
- Merge to target branch
- Push to remote

**Key Functions:**
```go
func CreateBranch(workDir, branchName, baseBranch string) error
func Commit(workDir, message string) error
func Merge(workDir, sourceBranch, targetBranch string) error
func Push(workDir, branch string) error
func GetCurrentBranch(workDir string) (string, error)
```

### 7. Build Runner (`build/runner.go`)

**Responsibilities:**
- Run C++ engine build (`et build`)
- Run Swift package build (`swift build`)
- Run Swift tests (`swift test`)
- Parse build output for errors

**Key Functions:**
```go
func BuildCpp(workDir string) error
func BuildSwift(workDir string) error
func RunSwiftTests(workDir string) error
```

### 8. Progress Tracker (`progress/tracker.go`)

**Responsibilities:**
- Persist progress to JSON file
- Support resume from saved state
- Log task completions/failures
- Generate summary reports

**Progress File Format:**
```json
{
    "started_at": "2026-01-19T10:00:00Z",
    "updated_at": "2026-01-19T12:30:00Z",
    "total_tasks": 13,
    "completed": 5,
    "failed": 1,
    "in_progress": 1,
    "pending": 6,
    "tasks": {
        "math.dart": {
            "status": "completed",
            "branch_name": "migrate/math",
            "commit_hash": "abc123",
            "start_time": "...",
            "end_time": "..."
        },
        "geometry.dart": {
            "status": "in_progress",
            "branch_name": "migrate/geometry",
            "start_time": "..."
        }
    }
}
```

## CLI Interface

```bash
# Basic usage - start new migration
migration-orchestrator \
    --metadata result.json \
    --guide SWIFT_MIGRATION_GUIDE.md \
    --workspace <workspace>/ \
    --base-branch swift3

# With concurrency (run 2 Claude processes)
migration-orchestrator \
    --metadata result.json \
    --concurrency 2

# Resume interrupted migration
migration-orchestrator \
    --resume \
    --progress migration_progress.json

# Skip certain files
migration-orchestrator \
    --metadata result.json \
    --skip hooks.dart,annotations.dart

# Dry run - show what would be done
migration-orchestrator \
    --metadata result.json \
    --dry-run

# Process specific file(s) only
migration-orchestrator \
    --metadata result.json \
    --only math.dart,lerp.dart
```

**CLI Flags:**
| Flag | Description | Default |
|------|-------------|---------|
| `--metadata`, `-m` | Path to result.json or result.txt | Required |
| `--guide`, `-g` | Path to SWIFT_MIGRATION_GUIDE.md | `../../SWIFT_MIGRATION_GUIDE.md` |
| `--workspace`, `-w` | Flutter workspace path | `<workspace>/` |
| `--concurrency`, `-c` | Max concurrent Claude processes | `1` |
| `--base-branch` | Branch to merge completed work to | `swift3` |
| `--branch-prefix` | Prefix for feature branches | `migrate/` |
| `--progress`, `-p` | Progress file path | `migration_progress.json` |
| `--resume`, `-r` | Resume from progress file | `false` |
| `--skip` | Comma-separated files to skip | `""` |
| `--only` | Only process these files | `""` |
| `--dry-run` | Show plan without executing | `false` |
| `--no-build` | Skip build verification | `false` |
| `--no-merge` | Don't merge to base branch | `false` |
| `--no-push` | Don't push after merge | `false` |
| `--verbose`, `-v` | Verbose output | `false` |

## Workflow

### Normal Execution Flow

1. **Initialize**
   - Parse CLI arguments
   - Load config
   - Parse metadata file
   - Create task queue with dependency ordering

2. **Resume Check**
   - If `--resume`, load progress file
   - Mark completed tasks, restore in-progress state

3. **Main Loop**
   - Get next available task(s) respecting concurrency
   - For each task:
     a. Update status to `in_progress`
     b. Generate migration prompt
     c. Launch Claude CLI process
     d. Wait for completion
     e. Verify build success
     f. Update status to `completed` or `failed`
     g. If successful and `--auto-merge`, merge to base branch
     h. If `--auto-push`, push changes
     i. Save progress

4. **Completion**
   - Generate summary report
   - List any failed tasks
   - Clean up

### Error Handling

1. **Claude Process Failure**
   - Mark task as `failed`
   - Log error details
   - Continue with other tasks (dependencies permitting)
   - Allow retry via `--resume`

2. **Build Failure**
   - Mark task as `failed`
   - Keep branch for manual inspection
   - Don't merge to base branch
   - Continue with independent tasks

3. **Merge Conflict**
   - Mark task as `failed`
   - Keep feature branch
   - Alert user for manual resolution

4. **Circular Dependencies**
   - Warn user at startup
   - Process in specified order
   - May require manual intervention

## Implementation Order

### Phase 1: Core Infrastructure
1. `main.go` - CLI parsing with `cobra` or `flag`
2. `config/config.go` - Configuration management
3. `parser/json_parser.go` - Parse result.json
4. `task/task.go` - Task data structures

### Phase 2: Task Management
5. `task/queue.go` - Task queue with dependency ordering
6. `progress/tracker.go` - Progress persistence
7. `task/scheduler.go` - Basic sequential scheduler

### Phase 3: Execution
8. `prompt/generator.go` - Prompt generation
9. `executor/claude_executor.go` - Claude CLI execution
10. `git/operations.go` - Git operations

### Phase 4: Build & Merge
11. `build/runner.go` - Build verification
12. Integration of merge/push workflow

### Phase 5: Concurrency
13. Enhance scheduler for concurrent execution
14. Add worker pool management

### Phase 6: Polish
15. Add `--dry-run` mode
16. Improve logging and error messages
17. Add summary report generation

## Testing Strategy

1. **Unit Tests**
   - Parser tests with sample JSON/TXT
   - Task queue ordering tests
   - Prompt generation tests

2. **Integration Tests**
   - Mock Claude executor
   - Test full workflow with mock

3. **Manual Testing**
   - Run against real metadata files
   - Verify Claude prompts are correct
   - Test resume functionality

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Claude fails to complete migration correctly | Progress tracking allows resume; failed tasks can be retried |
| Build breaks during migration | Each task on separate branch; don't merge broken code |
| Merge conflicts | Feature branches; manual resolution; track in progress file |
| Long running tasks | Timeouts; progress updates; ability to cancel gracefully |
| Rate limiting on Claude | Configurable concurrency; exponential backoff |

## Success Criteria

1. ✅ Parse both JSON and TXT metadata formats
2. ✅ Create tasks respecting dependency order
3. ✅ Generate comprehensive migration prompts
4. ✅ Launch and monitor Claude processes
5. ✅ Track progress with resume capability
6. ✅ Support configurable concurrency
7. ✅ Handle git branch/merge/push workflow
8. ✅ Verify builds before merging
9. ✅ Provide clear logging and status updates
10. ✅ Generate completion reports

---

**Document Version:** 1.0
**Created:** 2026-01-19
