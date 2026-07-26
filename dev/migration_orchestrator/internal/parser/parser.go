// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fmt"
	"path/filepath"
	"strings"
)

// Parser is the interface for parsing metadata files
type Parser interface {
	Parse() (*MetadataResult, error)
}

// New creates a new parser based on file extension
func New(path string) (Parser, error) {
	ext := strings.ToLower(filepath.Ext(path))
	switch ext {
	case ".json":
		return NewJSONParser(path), nil
	case ".txt":
		return NewTxtParser(path), nil
	default:
		return nil, fmt.Errorf("unsupported file format: %s", ext)
	}
}
