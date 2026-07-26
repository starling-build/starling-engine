#!/usr/bin/env npx ts-node
// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Migration Runner using Claude Agent SDK
 *
 * Replaces the bash script with proper:
 * - Structured status monitoring
 * - Graceful timeout/interruption handling
 * - Session management for resume capability
 * - Error recovery and retry logic
 *
 * Usage: npx ts-node scripts/migrate_runner.ts plans/geometry_migration_plan.md
 */

import { query } from "@anthropic-ai/claude-agent-sdk";

// Configuration
const CONFIG = {
  // Override with the WORKSPACE env var; defaults to the current directory.
  workspace: process.env.WORKSPACE ?? process.cwd(),
  maxTurnsPerTask: 100,
  maxBudgetPerTask: 15.0,  // USD
  retryDelayMs: 5000,
  maxRetries: 3,
  stuckDetectionMs: 5 * 60 * 1000,  // 5 minutes no progress = stuck
};

interface TaskResult {
  status: "complete" | "blocked" | "all_complete" | "error" | "timeout";
  message?: string;
  sessionId?: string;
  cost?: number;
  turns?: number;
}

/**
 * Run a single migration task from the plan
 */
async function runMigrationTask(planFile: string): Promise<TaskResult> {
  const prompt = `You are migrating Flutter Dart code to Swift.

READ THE MIGRATION PLAN: ${planFile}

This plan contains the migration context and all the tasks that need to be done. Your job:

1. Read the plan file to find the NEXT INCOMPLETE task
2. Follow the 'Workflow Per Task' section in the plan exactly
3. Complete that ONE task (git branch, migrate code, build, commit, merge, push)
4. Update the plan file to mark the task as COMPLETE by adding '✅ COMPLETE' next to the task
5. Output 'TASK_COMPLETE: <task-id>' when done
6. If ALL tasks are complete, output 'ALL_TASKS_COMPLETE' and exit
7. If blocked on a task, output 'TASK_BLOCKED: <task-id> - <reason>'

IMPORTANT:
- Read FLUTTER_FRAMEWORK_MIGRATION_GUIDE.md before writing any code
- Only do ONE task per invocation
- Update the plan file progress after completing each task
- Follow git workflow: branch -> code -> build -> commit -> merge -> push`;

  let sessionId: string | undefined;
  let lastActivityTime = Date.now();
  let resultText = "";
  let totalCost = 0;
  let numTurns = 0;

  try {
    const response = query({
      prompt,
      options: {
        cwd: CONFIG.workspace,
        maxTurns: CONFIG.maxTurnsPerTask,
        maxBudgetUsd: CONFIG.maxBudgetPerTask,
        permissionMode: "bypassPermissions",
        allowedTools: [
          "Read", "Write", "Edit", "Glob", "Grep", "Bash",
          "Task", "TodoWrite", "WebFetch", "WebSearch"
        ],
        // Uses default model from Claude Code configuration
      }
    });

    // Set up stuck detection
    const stuckCheckInterval = setInterval(() => {
      const timeSinceActivity = Date.now() - lastActivityTime;
      if (timeSinceActivity > CONFIG.stuckDetectionMs) {
        console.log(`\n⚠️  No activity for ${Math.round(timeSinceActivity / 1000)}s - may be stuck`);
      }
    }, 30000);

    try {
      for await (const message of response) {
        lastActivityTime = Date.now();

        // Capture session ID
        if (message.type === "system" && "session_id" in message) {
          sessionId = message.session_id;
          console.log(`📍 Session: ${sessionId}`);
        }

        // Handle assistant messages (Claude's responses)
        if (message.type === "assistant" && message.message?.content) {
          for (const block of message.message.content) {
            if ("text" in block) {
              resultText += block.text + "\n";
              // Print progress
              const lines = block.text.split("\n");
              for (const line of lines) {
                if (line.trim()) {
                  console.log(`💭 ${line.substring(0, 120)}`);
                }
              }
            }
            if ("name" in block) {
              console.log(`🔧 Tool: ${block.name}`);
            }
          }
        }

        // Handle final result
        if (message.type === "result") {
          if ("total_cost_usd" in message) {
            totalCost = message.total_cost_usd;
          }
          if ("num_turns" in message) {
            numTurns = message.num_turns;
          }
          if ("result" in message) {
            resultText += message.result;
          }

          // Check for errors
          if (message.subtype !== "success") {
            console.log(`\n❌ Task ended with: ${message.subtype}`);
            if ("errors" in message) {
              console.log(`Errors: ${message.errors?.join(", ")}`);
            }

            // Check if it was a max turns issue (might be stuck)
            if (message.subtype === "error_max_turns") {
              return {
                status: "timeout",
                message: "Hit max turns limit",
                sessionId,
                cost: totalCost,
                turns: numTurns
              };
            }

            return {
              status: "error",
              message: message.subtype,
              sessionId,
              cost: totalCost,
              turns: numTurns
            };
          }
        }
      }
    } finally {
      clearInterval(stuckCheckInterval);
    }

    // Parse result to determine status
    if (resultText.includes("ALL_TASKS_COMPLETE")) {
      return {
        status: "all_complete",
        sessionId,
        cost: totalCost,
        turns: numTurns
      };
    }

    if (resultText.includes("TASK_BLOCKED")) {
      const match = resultText.match(/TASK_BLOCKED:\s*(.+)/);
      return {
        status: "blocked",
        message: match?.[1] || "Unknown block reason",
        sessionId,
        cost: totalCost,
        turns: numTurns
      };
    }

    if (resultText.includes("TASK_COMPLETE")) {
      return {
        status: "complete",
        sessionId,
        cost: totalCost,
        turns: numTurns
      };
    }

    // If we get here, task may not have finished properly
    return {
      status: "error",
      message: "Task did not produce expected completion marker",
      sessionId,
      cost: totalCost,
      turns: numTurns
    };

  } catch (error) {
    const errorMessage = error instanceof Error ? error.message : String(error);
    console.error(`\n💥 Exception: ${errorMessage}`);

    // Check for auth errors
    if (errorMessage.includes("401") || errorMessage.includes("authentication")) {
      console.log("\n⚠️  Authentication error - please run: claude /login");
      console.log("Waiting 3 minutes for re-authentication...");
      await sleep(180000);
      return {
        status: "error",
        message: "Authentication error - retrying",
        sessionId
      };
    }

    return {
      status: "error",
      message: errorMessage,
      sessionId
    };
  }
}

/**
 * Process a single plan file until all tasks complete or blocked
 */
async function processPlan(planFile: string): Promise<"complete" | "blocked"> {
  console.log(`\n${"=".repeat(50)}`);
  console.log(`📋 Starting plan: ${planFile}`);
  console.log(`${"=".repeat(50)}\n`);

  let totalCost = 0;
  let totalTurns = 0;
  let taskCount = 0;
  let retryCount = 0;

  while (true) {
    taskCount++;
    console.log(`\n--- Task iteration #${taskCount} ---\n`);

    const result = await runMigrationTask(planFile);

    if (result.cost) totalCost += result.cost;
    if (result.turns) totalTurns += result.turns;

    console.log(`\n📊 Task result: ${result.status}`);
    if (result.cost) console.log(`   Cost: $${result.cost.toFixed(4)}`);
    if (result.turns) console.log(`   Turns: ${result.turns}`);

    switch (result.status) {
      case "all_complete":
        console.log(`\n✅ All tasks in plan complete!`);
        console.log(`   Total cost: $${totalCost.toFixed(4)}`);
        console.log(`   Total turns: ${totalTurns}`);
        return "complete";

      case "complete":
        // One task done, continue to next
        retryCount = 0;
        console.log(`   Continuing to next task...`);
        await sleep(2000);
        break;

      case "blocked":
        console.log(`\n❌ Plan blocked: ${result.message}`);
        return "blocked";

      case "timeout":
      case "error":
        retryCount++;
        if (retryCount >= CONFIG.maxRetries) {
          console.log(`\n❌ Max retries (${CONFIG.maxRetries}) exceeded`);
          console.log(`   Last error: ${result.message}`);
          return "blocked";
        }
        console.log(`   Retry ${retryCount}/${CONFIG.maxRetries} after delay...`);
        await sleep(CONFIG.retryDelayMs);
        break;
    }
  }
}

/**
 * Main entry point
 */
async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0) {
    console.log("Usage: npx ts-node scripts/migrate_runner.ts <plan_file> [plan_file2] ...");
    console.log("Example: npx ts-node scripts/migrate_runner.ts plans/geometry_migration_plan.md");
    process.exit(1);
  }

  // Resolve plan file paths
  const planFiles = args.map(arg => {
    if (arg.startsWith("/")) return arg;
    return `${CONFIG.workspace}/${arg}`;
  });

  // Verify all plan files exist
  const fs = await import("fs");
  for (const plan of planFiles) {
    if (!fs.existsSync(plan)) {
      console.error(`Error: Plan file not found: ${plan}`);
      process.exit(1);
    }
  }

  console.log("==========================================");
  console.log("Migration Runner (TypeScript SDK)");
  console.log(`Plans to process: ${planFiles.length}`);
  for (const plan of planFiles) {
    console.log(`  - ${plan.split("/").pop()}`);
  }
  console.log("==========================================\n");

  const completedPlans: string[] = [];
  let blockedPlan: string | undefined;

  for (const planFile of planFiles) {
    const planName = planFile.split("/").pop()?.replace("_migration_plan.md", "") || planFile;

    const result = await processPlan(planFile);

    if (result === "complete") {
      completedPlans.push(planName);
    } else {
      blockedPlan = planName;
      break;  // Stop processing further plans
    }
  }

  // Final summary
  console.log("\n==========================================");
  console.log("Migration Summary");
  console.log("==========================================");
  console.log(`Completed plans: ${completedPlans.length}/${planFiles.length}`);
  for (const plan of completedPlans) {
    console.log(`  ✅ ${plan}`);
  }
  if (blockedPlan) {
    console.log(`  ❌ ${blockedPlan} (blocked)`);
  }
  console.log("==========================================");
}

function sleep(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

main().catch(error => {
  console.error("Fatal error:", error);
  process.exit(1);
});
