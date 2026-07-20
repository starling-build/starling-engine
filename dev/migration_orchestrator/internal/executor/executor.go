package executor

import (
	"context"

	"github.com/anthropics/migration-orchestrator/internal/task"
)

// Executor is the interface for executing migration tasks
// Note: This interface is also defined in task package to avoid import cycles.
// Implementations in this package implement both.
type Executor interface {
	Execute(ctx context.Context, t *task.Task) (string, error)
}
