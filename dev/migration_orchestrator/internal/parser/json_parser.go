package parser

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// JSONParser parses the JSON format from extract_metadata
type JSONParser struct {
	path string
}

// NewJSONParser creates a new JSON parser
func NewJSONParser(path string) *JSONParser {
	return &JSONParser{path: path}
}

// Parse parses the JSON metadata file
func (p *JSONParser) Parse() (*MetadataResult, error) {
	data, err := os.ReadFile(p.path)
	if err != nil {
		return nil, fmt.Errorf("failed to read file: %w", err)
	}

	var result MetadataResult
	if err := json.Unmarshal(data, &result); err != nil {
		return nil, fmt.Errorf("failed to parse JSON: %w", err)
	}

	// Extract migration order from global dependency graph if present
	if result.GlobalDependencyGraph != nil {
		result.MigrationOrder = result.GlobalDependencyGraph.MigrationOrder
		result.CircularDependencies = result.GlobalDependencyGraph.CircularDeps

		// Build file metadata from dependency graph if Files is empty
		if len(result.Files) == 0 {
			result.Files = p.buildFilesFromGraph(result.GlobalDependencyGraph)
		}
	}

	// If migration order is still empty, create from files
	if len(result.MigrationOrder) == 0 && len(result.Files) > 0 {
		for _, f := range result.Files {
			result.MigrationOrder = append(result.MigrationOrder, f.FilePath)
		}
	}

	// Update file count
	if result.FileCount == 0 {
		result.FileCount = len(result.Files)
	}

	return &result, nil
}

// buildFilesFromGraph creates FileMetadata from DependencyGraph
func (p *JSONParser) buildFilesFromGraph(graph *DependencyGraph) []FileMetadata {
	files := make([]FileMetadata, 0, len(graph.Files))

	for _, dep := range graph.Files {
		fm := FileMetadata{
			FilePath: dep.FilePath,
		}
		files = append(files, fm)
	}

	return files
}

// GetFileMetadataByPath finds file metadata by path
func GetFileMetadataByPath(result *MetadataResult, path string) *FileMetadata {
	for i := range result.Files {
		if result.Files[i].FilePath == path {
			return &result.Files[i]
		}
	}
	return nil
}

// GetDependencyByPath finds file dependency info by path
func GetDependencyByPath(result *MetadataResult, path string) *FileDependency {
	if result.GlobalDependencyGraph == nil {
		return nil
	}
	for i := range result.GlobalDependencyGraph.Files {
		if result.GlobalDependencyGraph.Files[i].FilePath == path {
			return &result.GlobalDependencyGraph.Files[i]
		}
	}
	return nil
}

// GetFileName extracts the file name from a path
func GetFileName(path string) string {
	return filepath.Base(path)
}
