#!/usr/bin/env npx ts-node
// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Plan Generator using Claude Agent SDK
 *
 * Generates migration plans for Dart library files.
 * Parses a Dart library file to extract exported files,
 * then calls Claude to create a migration plan for each file.
 *
 * Usage: npx ts-node scripts/plan_generator.ts packages/flutter/lib/physics.dart
 */

import { query } from "@anthropic-ai/claude-agent-sdk";
import * as fs from "fs";
import * as path from "path";

// Configuration
const CONFIG = {
  // Override with the WORKSPACE env var; defaults to the current directory.
  workspace: process.env.WORKSPACE ?? process.cwd(),
  maxTurnsPerPlan: 50,
  maxBudgetPerPlan: 5.0, // USD
  retryDelayMs: 5000,
  maxRetries: 3,
  plansDirectory: "plans",
  migrationGuide: "FLUTTER_FRAMEWORK_MIGRATION_GUIDE.md",
  samplePlan: "plans/diagnostics_migration_plan.md",
};

interface PlanResult {
  status: "success" | "error";
  dartFile: string;
  planFile?: string;
  message?: string;
  cost?: number;
  turns?: number;
}

interface FileDependencyInfo {
  dartFile: string;
  planFile: string;
  dependencies: string[];
  complexity: string;
}

/**
 * Parse a Dart library file and extract exported file paths
 */
function parseExports(dartFilePath: string): string[] {
  const absolutePath = dartFilePath.startsWith("/")
    ? dartFilePath
    : path.join(CONFIG.workspace, dartFilePath);

  if (!fs.existsSync(absolutePath)) {
    throw new Error(`Dart file not found: ${absolutePath}`);
  }

  const content = fs.readFileSync(absolutePath, "utf-8");
  const exports: string[] = [];

  // Match export statements like: export 'src/physics/clamped_simulation.dart';
  const exportRegex = /export\s+['"]([^'"]+)['"]\s*;/g;
  let match;

  while ((match = exportRegex.exec(content)) !== null) {
    exports.push(match[1]);
  }

  return exports;
}

/**
 * Extract the library/module name from the Dart file path
 */
function extractModuleName(dartFilePath: string): string {
  // packages/flutter/lib/physics.dart -> physics
  const fileName = path.basename(dartFilePath, ".dart");
  return fileName;
}

/**
 * Generate a plan file name for a given Dart source file
 */
function generatePlanFileName(dartFile: string): string {
  // src/physics/clamped_simulation.dart -> clamped_simulation_migration_plan.md
  const baseName = path.basename(dartFile, ".dart");
  return `${baseName}_migration_plan.md`;
}

/**
 * Generate a migration plan for a single Dart file
 */
async function generatePlan(
  dartFile: string,
  libraryPath: string,
  moduleName: string
): Promise<PlanResult> {
  const planFileName = generatePlanFileName(dartFile);
  const planFilePath = path.join(CONFIG.plansDirectory, moduleName, planFileName);
  const absolutePlanPath = path.join(CONFIG.workspace, planFilePath);

  // Create the module subdirectory if it doesn't exist
  const planDir = path.dirname(absolutePlanPath);
  if (!fs.existsSync(planDir)) {
    fs.mkdirSync(planDir, { recursive: true });
  }

  // Check if plan already exists
  if (fs.existsSync(absolutePlanPath)) {
    console.log(`   ⏭️  Plan already exists: ${planFilePath}`);
    return {
      status: "success",
      dartFile,
      planFile: planFilePath,
      message: "Plan already exists",
    };
  }

  // Get the full path to the Dart source file
  const libraryDir = path.dirname(libraryPath);
  const fullDartPath = path.join(libraryDir, dartFile);

  const prompt = `You are creating a migration plan for migrating a Flutter Dart file to Swift.

## Your Task

Create a comprehensive migration plan for the file: ${fullDartPath}

## References

1. **Read the Migration Guide first**: Read the file \`${CONFIG.migrationGuide}\` to understand the migration patterns and principles.

2. **Study the Sample Plan**: Read the file \`${CONFIG.samplePlan}\` to understand the structure and format of migration plans.

3. **Read the Dart Source File**: Read the file \`${fullDartPath}\` to understand what needs to be migrated.

## Plan Requirements

Create a migration plan markdown file with the following sections:

1. **Overview**
   - Source file path
   - Target directory (flutter_swift/Sources/Flutter/${moduleName}/)
   - Complexity rating (🟢 Low, 🟡 Medium, 🔴 High) based on lines and types
   - Base branch: final-branch
   - Branch prefix: swift-migrate/

2. **Dart Source Analysis**
   - List ALL types to migrate (classes, enums, mixins, typedefs, top-level functions)
   - Include line numbers for each type
   - Note what Swift type each should become
   - Identify deprecated code to skip (with @Deprecated or @deprecated)

3. **Dependencies**
   - List imports from other Dart files
   - Note which dependencies need to be migrated first or stubbed

4. **Swift Design Decisions**
   - Key architectural choices for this migration
   - Pattern mappings (mixin → protocol, abstract class → protocol/class, etc.)

5. **Type Mapping Reference**
   - Specific type conversions for this file

6. **Subtask Breakdown**
   - Break down into logical subtasks (aim for 3-10 subtasks)
   - Each subtask should be completable in one focused session
   - Include checklists with [ ] for tracking

7. **Test Migration**
   - Note the corresponding test file if it exists
   - Key test categories

8. **Verification Checklist**
   - Standard verification items

## Workflow Per Task

For each subtask, the implementer should follow this workflow:
1. Create feature branch: \`git checkout -b swift-migrate/<subtask-name>\`
2. Implement the Swift code with Dart source references
3. Run \`swift build\` to verify compilation
4. Commit changes
5. Merge to final-branch: \`git checkout final-branch && git merge --no-ff <feature-branch>\`
6. Push changes: \`git push origin final-branch\`
7. Mark subtask as complete with ✅ COMPLETE

## Output

Write the complete migration plan to: ${absolutePlanPath}

IMPORTANT:
- Follow the exact format and structure of the sample plan
- Be thorough in analyzing the Dart source file
- Include line numbers for all type definitions
- Group related types into logical subtasks`;

  let totalCost = 0;
  let numTurns = 0;

  try {
    const response = query({
      prompt,
      options: {
        cwd: CONFIG.workspace,
        maxTurns: CONFIG.maxTurnsPerPlan,
        maxBudgetUsd: CONFIG.maxBudgetPerPlan,
        permissionMode: "bypassPermissions",
        allowedTools: ["Read", "Write", "Edit", "Glob", "Grep", "Bash"],
      },
    });

    for await (const message of response) {
      if (message.type === "assistant" && message.message?.content) {
        for (const block of message.message.content) {
          if ("text" in block) {
            const lines = block.text.split("\n");
            for (const line of lines) {
              if (line.trim()) {
                console.log(`   💭 ${line.substring(0, 100)}`);
              }
            }
          }
          if ("name" in block) {
            console.log(`   🔧 Tool: ${block.name}`);
          }
        }
      }

      if (message.type === "result") {
        if ("total_cost_usd" in message) {
          totalCost = message.total_cost_usd;
        }
        if ("num_turns" in message) {
          numTurns = message.num_turns;
        }

        if (message.subtype !== "success") {
          return {
            status: "error",
            dartFile,
            message: `Task ended with: ${message.subtype}`,
            cost: totalCost,
            turns: numTurns,
          };
        }
      }
    }

    // Verify the plan was created
    if (fs.existsSync(absolutePlanPath)) {
      return {
        status: "success",
        dartFile,
        planFile: planFilePath,
        cost: totalCost,
        turns: numTurns,
      };
    } else {
      return {
        status: "error",
        dartFile,
        message: "Plan file was not created",
        cost: totalCost,
        turns: numTurns,
      };
    }
  } catch (error) {
    const errorMessage = error instanceof Error ? error.message : String(error);
    console.error(`   💥 Exception: ${errorMessage}`);
    return {
      status: "error",
      dartFile,
      message: errorMessage,
    };
  }
}

/**
 * Main entry point
 */
async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0) {
    console.log("Usage: npx ts-node scripts/plan_generator.ts <dart_library_file>");
    console.log("Example: npx ts-node scripts/plan_generator.ts packages/flutter/lib/physics.dart");
    console.log("");
    console.log("This tool:");
    console.log("1. Parses the Dart library file to extract exported files");
    console.log("2. For each exported file, generates a migration plan");
    console.log("3. Plans are saved to plans/<module_name>/<file>_migration_plan.md");
    process.exit(1);
  }

  const dartLibraryFile = args[0];
  const absolutePath = dartLibraryFile.startsWith("/")
    ? dartLibraryFile
    : path.join(CONFIG.workspace, dartLibraryFile);

  if (!fs.existsSync(absolutePath)) {
    console.error(`Error: Dart library file not found: ${absolutePath}`);
    process.exit(1);
  }

  const moduleName = extractModuleName(dartLibraryFile);
  console.log("==========================================");
  console.log("Plan Generator (TypeScript SDK)");
  console.log(`Library: ${dartLibraryFile}`);
  console.log(`Module: ${moduleName}`);
  console.log("==========================================\n");

  // Parse exports
  console.log("📖 Parsing exports...\n");
  const exports = parseExports(absolutePath);

  if (exports.length === 0) {
    console.log("No exports found in the library file.");
    process.exit(0);
  }

  console.log(`Found ${exports.length} exported files:`);
  for (const exp of exports) {
    console.log(`  - ${exp}`);
  }
  console.log("");

  // Generate plans for each export
  const results: PlanResult[] = [];
  let totalCost = 0;

  for (let i = 0; i < exports.length; i++) {
    const dartFile = exports[i];
    console.log(`\n📋 [${i + 1}/${exports.length}] Generating plan for: ${dartFile}`);
    console.log("-".repeat(50));

    const result = await generatePlan(dartFile, dartLibraryFile, moduleName);
    results.push(result);

    if (result.cost) {
      totalCost += result.cost;
    }

    if (result.status === "success") {
      console.log(`   ✅ Plan created: ${result.planFile}`);
    } else {
      console.log(`   ❌ Failed: ${result.message}`);
    }

    // Small delay between plans
    if (i < exports.length - 1) {
      await sleep(2000);
    }
  }

  // Summary
  console.log("\n==========================================");
  console.log("Plan Generation Summary");
  console.log("==========================================");

  const successful = results.filter((r) => r.status === "success");
  const failed = results.filter((r) => r.status === "error");

  console.log(`Total: ${results.length} files`);
  console.log(`✅ Successful: ${successful.length}`);
  console.log(`❌ Failed: ${failed.length}`);
  console.log(`💰 Total cost: $${totalCost.toFixed(4)}`);

  if (successful.length > 0) {
    console.log("\nGenerated plans:");
    for (const result of successful) {
      console.log(`  ✅ ${result.planFile}`);
    }
  }

  if (failed.length > 0) {
    console.log("\nFailed:");
    for (const result of failed) {
      console.log(`  ❌ ${result.dartFile}: ${result.message}`);
    }
  }

  // Generate the summary file with porting order
  if (successful.length > 0) {
    generateSummaryFile(moduleName, results, dartLibraryFile);
  }

  console.log("\n==========================================");

  // Exit with error code if any failed
  if (failed.length > 0) {
    process.exit(1);
  }
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/**
 * Parse a migration plan file to extract dependency and complexity info
 */
function parsePlanFile(planFilePath: string): FileDependencyInfo | null {
  const absolutePath = planFilePath.startsWith("/")
    ? planFilePath
    : path.join(CONFIG.workspace, planFilePath);

  if (!fs.existsSync(absolutePath)) {
    return null;
  }

  const content = fs.readFileSync(absolutePath, "utf-8");
  const dartFile = path.basename(planFilePath, "_migration_plan.md") + ".dart";

  // Extract complexity (look for 🟢, 🟡, or 🔴)
  let complexity = "Unknown";
  if (content.includes("🟢")) {
    complexity = "Low";
  } else if (content.includes("🟡")) {
    complexity = "Medium";
  } else if (content.includes("🔴")) {
    complexity = "High";
  }

  // Extract dependencies from the Dependencies section
  const dependencies: string[] = [];
  const depsMatch = content.match(/##\s*Dependencies[\s\S]*?(?=##|$)/i);
  if (depsMatch) {
    const depsSection = depsMatch[0];
    // Match import statements or file references
    const importRegex = /['"`]([^'"`]*\.dart)['"`]/g;
    let match: RegExpExecArray | null;
    while ((match = importRegex.exec(depsSection)) !== null) {
      const dep = path.basename(match[1], ".dart");
      if (!dependencies.includes(dep)) {
        dependencies.push(dep);
      }
    }
  }

  return {
    dartFile,
    planFile: planFilePath,
    dependencies,
    complexity,
  };
}

/**
 * Topological sort to determine porting order based on dependencies
 */
function determinePortingOrder(files: FileDependencyInfo[]): FileDependencyInfo[] {
  const fileMap = new Map<string, FileDependencyInfo>();
  const fileNames = new Set<string>();

  // Build a map of file base names to their info
  for (const file of files) {
    const baseName = path.basename(file.dartFile, ".dart");
    fileMap.set(baseName, file);
    fileNames.add(baseName);
  }

  // Filter dependencies to only include files in this module
  for (const file of files) {
    file.dependencies = file.dependencies.filter((dep) => fileNames.has(dep));
  }

  // Topological sort using Kahn's algorithm
  const inDegree = new Map<string, number>();
  const graph = new Map<string, string[]>();

  for (const file of files) {
    const baseName = path.basename(file.dartFile, ".dart");
    inDegree.set(baseName, 0);
    graph.set(baseName, []);
  }

  // Build the graph (dependency -> dependent)
  for (const file of files) {
    const baseName = path.basename(file.dartFile, ".dart");
    for (const dep of file.dependencies) {
      if (graph.has(dep)) {
        graph.get(dep)!.push(baseName);
        inDegree.set(baseName, (inDegree.get(baseName) || 0) + 1);
      }
    }
  }

  // Find all nodes with no dependencies
  const queue: string[] = [];
  for (const [name, degree] of inDegree) {
    if (degree === 0) {
      queue.push(name);
    }
  }

  // Sort queue by complexity (Low first, then Medium, then High)
  const complexityOrder = { Low: 0, Medium: 1, High: 2, Unknown: 3 };
  queue.sort((a, b) => {
    const aComplexity = fileMap.get(a)?.complexity || "Unknown";
    const bComplexity = fileMap.get(b)?.complexity || "Unknown";
    return (
      complexityOrder[aComplexity as keyof typeof complexityOrder] -
      complexityOrder[bComplexity as keyof typeof complexityOrder]
    );
  });

  const sorted: string[] = [];
  while (queue.length > 0) {
    const current = queue.shift()!;
    sorted.push(current);

    const dependents = graph.get(current) || [];
    for (const dep of dependents) {
      inDegree.set(dep, (inDegree.get(dep) || 0) - 1);
      if (inDegree.get(dep) === 0) {
        queue.push(dep);
        // Re-sort to maintain complexity ordering
        queue.sort((a, b) => {
          const aComplexity = fileMap.get(a)?.complexity || "Unknown";
          const bComplexity = fileMap.get(b)?.complexity || "Unknown";
          return (
            complexityOrder[aComplexity as keyof typeof complexityOrder] -
            complexityOrder[bComplexity as keyof typeof complexityOrder]
          );
        });
      }
    }
  }

  // Handle any remaining files (cycles or disconnected)
  for (const file of files) {
    const baseName = path.basename(file.dartFile, ".dart");
    if (!sorted.includes(baseName)) {
      sorted.push(baseName);
    }
  }

  return sorted.map((name) => fileMap.get(name)!).filter(Boolean);
}

/**
 * Generate a summary file with the recommended porting order
 */
function generateSummaryFile(
  moduleName: string,
  results: PlanResult[],
  libraryPath: string
): void {
  const summaryPath = path.join(
    CONFIG.workspace,
    CONFIG.plansDirectory,
    moduleName,
    "PORTING_ORDER.md"
  );

  // Parse all successful plan files
  const fileInfos: FileDependencyInfo[] = [];
  for (const result of results) {
    if (result.status === "success" && result.planFile) {
      const info = parsePlanFile(result.planFile);
      if (info) {
        fileInfos.push(info);
      }
    }
  }

  // Determine the porting order
  const orderedFiles = determinePortingOrder(fileInfos);

  // Generate summary content
  const lines: string[] = [
    `# ${moduleName.charAt(0).toUpperCase() + moduleName.slice(1)} Module - Porting Order`,
    "",
    `> Auto-generated summary for migrating \`${libraryPath}\` to Swift`,
    "",
    "## Overview",
    "",
    `- **Total files**: ${orderedFiles.length}`,
    `- **Low complexity**: ${orderedFiles.filter((f) => f.complexity === "Low").length}`,
    `- **Medium complexity**: ${orderedFiles.filter((f) => f.complexity === "Medium").length}`,
    `- **High complexity**: ${orderedFiles.filter((f) => f.complexity === "High").length}`,
    "",
    "## Recommended Porting Order",
    "",
    "Files are ordered by dependencies (files with no dependencies first) and complexity (simpler files first).",
    "",
    "| # | File | Complexity | Dependencies | Plan |",
    "|---|------|------------|--------------|------|",
  ];

  for (let i = 0; i < orderedFiles.length; i++) {
    const file = orderedFiles[i];
    const deps = file.dependencies.length > 0 ? file.dependencies.join(", ") : "None";
    const complexityEmoji =
      file.complexity === "Low" ? "🟢" : file.complexity === "Medium" ? "🟡" : "🔴";
    const planLink = `[Plan](${path.basename(file.planFile)})`;
    lines.push(
      `| ${i + 1} | \`${file.dartFile}\` | ${complexityEmoji} ${file.complexity} | ${deps} | ${planLink} |`
    );
  }

  lines.push("");
  lines.push("## Porting Checklist");
  lines.push("");

  for (let i = 0; i < orderedFiles.length; i++) {
    const file = orderedFiles[i];
    const complexityEmoji =
      file.complexity === "Low" ? "🟢" : file.complexity === "Medium" ? "🟡" : "🔴";
    lines.push(`- [ ] ${i + 1}. \`${file.dartFile}\` ${complexityEmoji}`);
  }

  lines.push("");
  lines.push("---");
  lines.push(`*Generated on ${new Date().toISOString().split("T")[0]}*`);
  lines.push("");

  fs.writeFileSync(summaryPath, lines.join("\n"));
  console.log(`\n📑 Summary file created: ${path.relative(CONFIG.workspace, summaryPath)}`);
}

main().catch((error) => {
  console.error("Fatal error:", error);
  process.exit(1);
});
