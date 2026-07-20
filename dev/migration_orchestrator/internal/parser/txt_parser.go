package parser

import (
	"bufio"
	"fmt"
	"os"
	"regexp"
	"strings"
)

// TxtParser parses the compact TXT format from extract_metadata
type TxtParser struct {
	path string
}

// NewTxtParser creates a new TXT parser
func NewTxtParser(path string) *TxtParser {
	return &TxtParser{path: path}
}

// Parse parses the compact text metadata file
func (p *TxtParser) Parse() (*MetadataResult, error) {
	file, err := os.Open(p.path)
	if err != nil {
		return nil, fmt.Errorf("failed to open file: %w", err)
	}
	defer file.Close()

	result := &MetadataResult{
		Version:              "1.0.0",
		Files:                []FileMetadata{},
		MigrationOrder:       []string{},
		CircularDependencies: [][]string{},
		GlobalDependencyGraph: &DependencyGraph{
			Files: []FileDependency{},
		},
	}

	scanner := bufio.NewScanner(file)

	// Patterns for parsing
	totalFilesPattern := regexp.MustCompile(`^Total files:\s*(\d+)`)
	migrationOrderPattern := regexp.MustCompile(`^\d+\.\s+(.+\.dart)`)
	pathPattern := regexp.MustCompile(`^path:\s+(.+)`)
	exportsPattern := regexp.MustCompile(`^\s+exports:\s+(.+)`)
	dependsOnPattern := regexp.MustCompile(`^\s+depends on:\s+(.+)`)
	requiredByPattern := regexp.MustCompile(`^\s+required by:\s+(.+)`)
	circularPattern := regexp.MustCompile(`^-\s+(.+)\s+<->\s+(.+)`)

	var currentSection string
	var currentFile *FileDependency
	var currentMetadata *FileMetadata
	inMigrationOrder := false
	inCircularDeps := false
	inFileDeps := false

	for scanner.Scan() {
		line := scanner.Text()

		// Check for section headers
		if strings.HasPrefix(line, "## Migration Order") {
			inMigrationOrder = true
			inCircularDeps = false
			inFileDeps = false
			continue
		}
		if strings.HasPrefix(line, "## Circular Dependencies") {
			inMigrationOrder = false
			inCircularDeps = true
			inFileDeps = false
			continue
		}
		if strings.HasPrefix(line, "## File Dependencies") {
			inMigrationOrder = false
			inCircularDeps = false
			inFileDeps = true
			continue
		}
		if strings.HasPrefix(line, "---") {
			// Section separator - reset file context
			if currentFile != nil {
				result.GlobalDependencyGraph.Files = append(result.GlobalDependencyGraph.Files, *currentFile)
			}
			if currentMetadata != nil {
				result.Files = append(result.Files, *currentMetadata)
			}
			currentFile = nil
			currentMetadata = nil
			inMigrationOrder = false
			inCircularDeps = false
			inFileDeps = false
			continue
		}

		// Parse total files
		if matches := totalFilesPattern.FindStringSubmatch(line); matches != nil {
			fmt.Sscanf(matches[1], "%d", &result.FileCount)
			continue
		}

		// Parse migration order
		if inMigrationOrder {
			if matches := migrationOrderPattern.FindStringSubmatch(line); matches != nil {
				result.MigrationOrder = append(result.MigrationOrder, matches[1])
			}
			continue
		}

		// Parse circular dependencies
		if inCircularDeps {
			if matches := circularPattern.FindStringSubmatch(line); matches != nil {
				result.CircularDependencies = append(result.CircularDependencies,
					[]string{strings.TrimSpace(matches[1]), strings.TrimSpace(matches[2])})
			}
			continue
		}

		// Parse file dependencies section
		if inFileDeps {
			if strings.HasPrefix(line, "### ") {
				// Save previous file
				if currentFile != nil {
					result.GlobalDependencyGraph.Files = append(result.GlobalDependencyGraph.Files, *currentFile)
				}
				fileName := strings.TrimPrefix(line, "### ")
				currentFile = &FileDependency{
					FilePath: fileName,
				}
				continue
			}
			if currentFile != nil {
				if matches := exportsPattern.FindStringSubmatch(line); matches != nil {
					symbols := strings.Split(matches[1], ", ")
					currentFile.ExportedSymbols = symbols
				}
				if matches := dependsOnPattern.FindStringSubmatch(line); matches != nil {
					deps := strings.Split(matches[1], ", ")
					currentFile.DependsOn = deps
				}
				if matches := requiredByPattern.FindStringSubmatch(line); matches != nil {
					deps := strings.Split(matches[1], ", ")
					currentFile.DependedBy = deps
				}
			}
			continue
		}

		// Parse individual file sections (after ---)
		if strings.HasPrefix(line, "# ") && strings.HasSuffix(line, ".dart") {
			currentSection = "file"
			if currentMetadata != nil {
				result.Files = append(result.Files, *currentMetadata)
			}
			currentMetadata = &FileMetadata{}
			continue
		}

		if currentSection == "file" && currentMetadata != nil {
			if matches := pathPattern.FindStringSubmatch(line); matches != nil {
				currentMetadata.FilePath = matches[1]
			}
		}
	}

	// Save any remaining file
	if currentFile != nil {
		result.GlobalDependencyGraph.Files = append(result.GlobalDependencyGraph.Files, *currentFile)
	}
	if currentMetadata != nil {
		result.Files = append(result.Files, *currentMetadata)
	}

	// Copy migration order to dependency graph
	result.GlobalDependencyGraph.MigrationOrder = result.MigrationOrder
	result.GlobalDependencyGraph.CircularDeps = result.CircularDependencies

	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("error reading file: %w", err)
	}

	return result, nil
}
