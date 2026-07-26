#!/bin/bash
# Copyright the Starling authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generic migration script for Dart to Swift migrations
# Uses Claude CLI with dangerously-skip-permissions in a loop
#
# Usage: ./migrate_channel_buffers.sh <plan_file> [plan_file2] [plan_file3] ...
# Example: ./migrate_channel_buffers.sh plans/geometry_migration_plan.md plans/math_migration_plan.md

set -e

# Default to the repo this script lives in; override with WORKSPACE=... 
WORKSPACE="${WORKSPACE:-$(cd "$(dirname "$0")/.." && pwd)}"
TIMEOUT_MINUTES=30
TIMEOUT_SECONDS=$((TIMEOUT_MINUTES * 60))

# Check for required argument
if [ -z "$1" ]; then
    echo "Usage: $0 <plan_file> [plan_file2] [plan_file3] ..."
    echo "Example: $0 plans/geometry_migration_plan.md plans/math_migration_plan.md"
    exit 1
fi

# Store all plan files
PLAN_FILES=()
for arg in "$@"; do
    # Handle both absolute and relative paths
    if [[ "$arg" = /* ]]; then
        plan="$arg"
    else
        plan="$WORKSPACE/$arg"
    fi

    # Verify plan file exists
    if [ ! -f "$plan" ]; then
        echo "Error: Plan file not found: $plan"
        exit 1
    fi

    PLAN_FILES+=("$plan")
done

cd "$WORKSPACE"

echo "=========================================="
echo "Migration Script"
echo "Plans to process: ${#PLAN_FILES[@]}"
for plan in "${PLAN_FILES[@]}"; do
    echo "  - $(basename "$plan")"
done
echo "=========================================="
echo ""

# Track overall status
COMPLETED_PLANS=()
BLOCKED_PLAN=""

# Process each plan in order
for PLAN_FILE in "${PLAN_FILES[@]}"; do
    # Extract plan name for display (filename without path and extension)
    PLAN_NAME=$(basename "$PLAN_FILE" .md | sed 's/_migration_plan//' | tr '_' ' ')

    echo ""
    echo "=========================================="
    echo "Starting plan: $PLAN_NAME"
    echo "Plan file: $PLAN_FILE"
    echo "=========================================="
    echo ""

    # Loop until all tasks in this plan are complete
    while true; do
        echo ""
        echo "Launching Claude CLI for next task..."
        echo ""

        PROMPT="You are migrating Flutter Dart code to Swift.

READ THE MIGRATION PLAN: $PLAN_FILE

This plan contains the migration context and all the tasks that need to be done. Your job:

1. Read the plan file to find the NEXT INCOMPLETE task
2. Follow the 'Workflow Per Task' section in the plan exactly
3. Complete that ONE task (git branch, migrate code, build, commit, merge, push)
4. Update the plan file to mark the task as COMPLETE by adding '✅ COMPLETE' next to the task
5. Output 'TASK_COMPLETE: <task-id>' when done
6. If ALL tasks are complete, output 'ALL_TASKS_COMPLETE' and exit
7. If blocked on a task, output 'TASK_BLOCKED: <task-id> - <reason>'

IMPORTANT:
- Read SWIFT_MIGRATION_GUIDE.md before writing any code
- Only do ONE task per invocation
- Update the plan file progress after completing each task
- Follow git workflow: branch -> code -> build -> commit -> merge -> push"

        # Run Claude CLI with timeout (works on macOS without coreutils)
        # Start claude in background and monitor with timeout
        TIMED_OUT=false
        OUTPUT_FILE=$(mktemp)

        claude --dangerously-skip-permissions -p "$PROMPT" > "$OUTPUT_FILE" 2>&1 &
        CLAUDE_PID=$!

        # Wait for process with timeout
        SECONDS_WAITED=0
        while kill -0 $CLAUDE_PID 2>/dev/null; do
            if [ $SECONDS_WAITED -ge $TIMEOUT_SECONDS ]; then
                echo ""
                echo "=========================================="
                echo "⏰ TIMEOUT: Claude CLI exceeded ${TIMEOUT_MINUTES} minutes"
                echo "=========================================="
                kill -9 $CLAUDE_PID 2>/dev/null || true
                wait $CLAUDE_PID 2>/dev/null || true
                TIMED_OUT=true
                break
            fi
            sleep 5
            SECONDS_WAITED=$((SECONDS_WAITED + 5))
        done

        OUTPUT=$(cat "$OUTPUT_FILE")
        rm -f "$OUTPUT_FILE"

        echo "$OUTPUT"

        # Check if command timed out
        if [ "$TIMED_OUT" = true ]; then
            echo "Continuing to next iteration..."
            echo ""
            sleep 2
            continue
        fi

        # Check for authentication errors (401 - expired/invalid token)
        if echo "$OUTPUT" | grep -q "authentication_error\|OAuth token has expired\|API Error: 401"; then
            echo ""
            echo "=========================================="
            echo "⚠️  AUTHENTICATION ERROR DETECTED"
            echo "=========================================="
            echo "The API token has expired or is invalid."
            echo ""
            echo "Please run: /login"
            echo ""
            echo "Waiting 3 minutes for you to re-authenticate..."
            echo "Press Ctrl+C to cancel if needed."
            echo "=========================================="
            echo ""

            # Wait 3 minutes (180 seconds) to give user time to login
            sleep 180

            echo "Retrying after authentication wait..."
            continue
        fi

        # Check if all tasks are complete for this plan
        if echo "$OUTPUT" | grep -q "ALL_TASKS_COMPLETE"; then
            echo ""
            echo "=========================================="
            echo "Plan completed: $PLAN_NAME"
            echo "=========================================="
            COMPLETED_PLANS+=("$PLAN_NAME")
            break
        fi

        # Check if blocked
        if echo "$OUTPUT" | grep -q "TASK_BLOCKED"; then
            echo ""
            echo "=========================================="
            echo "Migration blocked on plan: $PLAN_NAME"
            echo "Check the output above."
            echo "=========================================="
            BLOCKED_PLAN="$PLAN_NAME"
            break
        fi

        # Small delay between tasks
        sleep 2
    done

    # If blocked, stop processing further plans
    if [ -n "$BLOCKED_PLAN" ]; then
        break
    fi
done

# Final summary
echo ""
echo "=========================================="
echo "Migration Summary"
echo "=========================================="
echo "Completed plans: ${#COMPLETED_PLANS[@]}/${#PLAN_FILES[@]}"
for plan in "${COMPLETED_PLANS[@]}"; do
    echo "  ✅ $plan"
done
if [ -n "$BLOCKED_PLAN" ]; then
    echo "  ❌ $BLOCKED_PLAN (blocked)"
fi
echo "=========================================="
