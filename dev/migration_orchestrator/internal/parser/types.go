package parser

// MetadataResult represents the parsed output from extract_metadata
type MetadataResult struct {
	Version              string              `json:"version"`
	FileCount            int                 `json:"fileCount"`
	MigrationOrder       []string            `json:"migrationOrder"`
	CircularDependencies [][]string          `json:"circularDependencies"`
	Files                []FileMetadata      `json:"files"`
	GlobalDependencyGraph *DependencyGraph   `json:"globalDependencyGraph,omitempty"`
}

// DependencyGraph represents the global dependency information
type DependencyGraph struct {
	Files          []FileDependency `json:"files"`
	MigrationOrder []string         `json:"migrationOrder"`
	CircularDeps   [][]string       `json:"circularDependencies"`
}

// FileDependency represents dependency info for a single file
type FileDependency struct {
	FilePath        string           `json:"filePath"`
	DependsOn       []string         `json:"dependsOn,omitempty"`
	DependedBy      []string         `json:"dependedBy,omitempty"`
	ExportedSymbols []string         `json:"exportedSymbols,omitempty"`
	ImportedSymbols []ImportedSymbol `json:"importedSymbols,omitempty"`
}

// ImportedSymbol represents an imported symbol from another file
type ImportedSymbol struct {
	Symbol     string `json:"symbol"`
	SourceFile string `json:"sourceFile"`
	UsageType  string `json:"usageType"`
}

// FileMetadata represents the full metadata for a single file
type FileMetadata struct {
	Version      string       `json:"version,omitempty"`
	FilePath     string       `json:"filePath"`
	Library      *Library     `json:"library,omitempty"`
	PartOf       string       `json:"partOf,omitempty"`
	Imports      []Import     `json:"imports,omitempty"`
	Exports      []Export     `json:"exports,omitempty"`
	Parts        []string     `json:"parts,omitempty"`
	Declarations *Declarations `json:"declarations,omitempty"`
	ExternalDeps []ExternalDep `json:"externalDependencies,omitempty"`
}

// Library represents a Dart library
type Library struct {
	Name string `json:"name"`
	URI  string `json:"uri"`
}

// Import represents a Dart import
type Import struct {
	URI        string   `json:"uri"`
	Prefix     string   `json:"prefix,omitempty"`
	ShowNames  []string `json:"showNames,omitempty"`
	HideNames  []string `json:"hideNames,omitempty"`
	IsDeferred bool     `json:"isDeferred,omitempty"`
}

// Export represents a Dart export
type Export struct {
	URI       string   `json:"uri"`
	ShowNames []string `json:"showNames,omitempty"`
	HideNames []string `json:"hideNames,omitempty"`
}

// ExternalDep represents an external dependency
type ExternalDep struct {
	Library   string `json:"library"`
	Symbol    string `json:"symbol"`
	UsageType string `json:"usageType"`
}

// Declarations holds all declarations in a file
type Declarations struct {
	Classes           []ClassDecl    `json:"classes,omitempty"`
	Mixins            []MixinDecl    `json:"mixins,omitempty"`
	Enums             []EnumDecl     `json:"enums,omitempty"`
	Extensions        []ExtensionDecl `json:"extensions,omitempty"`
	ExtensionTypes    []ExtTypeDecl  `json:"extensionTypes,omitempty"`
	Typedefs          []TypedefDecl  `json:"typedefs,omitempty"`
	Functions         []FunctionDecl `json:"functions,omitempty"`
	TopLevelVariables []VariableDecl `json:"topLevelVariables,omitempty"`
}

// ClassDecl represents a class declaration
type ClassDecl struct {
	Name           string         `json:"name"`
	Kind           string         `json:"kind"`
	TypeParameters []TypeParam    `json:"typeParameters,omitempty"`
	Superclass     *TypeRef       `json:"superclass,omitempty"`
	Interfaces     []TypeRef      `json:"interfaces,omitempty"`
	Mixins         []TypeRef      `json:"mixins,omitempty"`
	Annotations    []Annotation   `json:"annotations,omitempty"`
	Documentation  string         `json:"documentation,omitempty"`
	Location       Location       `json:"location"`
	Constructors   []Constructor  `json:"constructors,omitempty"`
	Methods        []MethodDecl   `json:"methods,omitempty"`
	Fields         []FieldDecl    `json:"fields,omitempty"`
	Accessors      []AccessorDecl `json:"accessors,omitempty"`
}

// MixinDecl represents a mixin declaration
type MixinDecl struct {
	Name           string       `json:"name"`
	TypeParameters []TypeParam  `json:"typeParameters,omitempty"`
	OnTypes        []TypeRef    `json:"onTypes,omitempty"`
	Interfaces     []TypeRef    `json:"interfaces,omitempty"`
	Documentation  string       `json:"documentation,omitempty"`
	Location       Location     `json:"location"`
	Methods        []MethodDecl `json:"methods,omitempty"`
	Fields         []FieldDecl  `json:"fields,omitempty"`
}

// EnumDecl represents an enum declaration
type EnumDecl struct {
	Name          string        `json:"name"`
	Values        []EnumValue   `json:"values,omitempty"`
	Documentation string        `json:"documentation,omitempty"`
	Location      Location      `json:"location"`
	Constructors  []Constructor `json:"constructors,omitempty"`
	Methods       []MethodDecl  `json:"methods,omitempty"`
	Fields        []FieldDecl   `json:"fields,omitempty"`
}

// EnumValue represents an enum value
type EnumValue struct {
	Name          string      `json:"name"`
	Arguments     interface{} `json:"arguments,omitempty"` // Can be string or []string
	Documentation string      `json:"documentation,omitempty"`
}

// ExtensionDecl represents an extension declaration
type ExtensionDecl struct {
	Name          string       `json:"name,omitempty"`
	ExtendedType  TypeRef      `json:"extendedType"`
	Documentation string       `json:"documentation,omitempty"`
	Location      Location     `json:"location"`
	Methods       []MethodDecl `json:"methods,omitempty"`
	Fields        []FieldDecl  `json:"fields,omitempty"`
}

// ExtTypeDecl represents an extension type declaration
type ExtTypeDecl struct {
	Name               string    `json:"name"`
	RepresentationType TypeRef   `json:"representationType"`
	Interfaces         []TypeRef `json:"interfaces,omitempty"`
	Documentation      string    `json:"documentation,omitempty"`
	Location           Location  `json:"location"`
}

// TypedefDecl represents a typedef declaration
type TypedefDecl struct {
	Name           string      `json:"name"`
	TypeParameters []TypeParam `json:"typeParameters,omitempty"`
	AliasedType    TypeRef     `json:"aliasedType"`
	Documentation  string      `json:"documentation,omitempty"`
	Location       Location    `json:"location"`
}

// FunctionDecl represents a top-level function
type FunctionDecl struct {
	Name           string        `json:"name"`
	ReturnType     TypeRef       `json:"returnType"`
	TypeParameters []TypeParam   `json:"typeParameters,omitempty"`
	Parameters     []Parameter   `json:"parameters,omitempty"`
	IsAsync        bool          `json:"isAsync,omitempty"`
	IsGenerator    bool          `json:"isGenerator,omitempty"`
	Annotations    []Annotation  `json:"annotations,omitempty"`
	Documentation  string        `json:"documentation,omitempty"`
	Location       Location      `json:"location"`
	BodyReferences *BodyRefs     `json:"bodyReferences,omitempty"`
	NativeInfo     *NativeInfo   `json:"nativeInfo,omitempty"`
}

// VariableDecl represents a top-level variable
type VariableDecl struct {
	Name          string     `json:"name"`
	Type          TypeRef    `json:"type"`
	IsFinal       bool       `json:"isFinal,omitempty"`
	IsConst       bool       `json:"isConst,omitempty"`
	IsLate        bool       `json:"isLate,omitempty"`
	Documentation string     `json:"documentation,omitempty"`
	Location      Location   `json:"location"`
}

// TypeParam represents a type parameter
type TypeParam struct {
	Name  string   `json:"name"`
	Bound *TypeRef `json:"bound,omitempty"`
}

// TypeRef represents a type reference
type TypeRef struct {
	Name              string    `json:"name"`
	FullyQualifiedName string   `json:"fullyQualifiedName,omitempty"`
	IsNullable        bool      `json:"isNullable,omitempty"`
	TypeArguments     []TypeRef `json:"typeArguments,omitempty"`
}

// Location represents source location
type Location struct {
	Line    int `json:"line"`
	Column  int `json:"column"`
	Offset  int `json:"offset"`
	EndLine int `json:"endLine,omitempty"`
}

// Annotation represents an annotation
type Annotation struct {
	Name      string      `json:"name"`
	Arguments interface{} `json:"arguments,omitempty"` // Can be string or []string
}

// Constructor represents a constructor
type Constructor struct {
	Name          string      `json:"name"`
	IsConst       bool        `json:"isConst,omitempty"`
	IsFactory     bool        `json:"isFactory,omitempty"`
	Parameters    []Parameter `json:"parameters,omitempty"`
	Documentation string      `json:"documentation,omitempty"`
	Location      Location    `json:"location"`
	Redirects     string      `json:"redirects,omitempty"`
	SuperCall     string      `json:"superCall,omitempty"`
}

// MethodDecl represents a method
type MethodDecl struct {
	Name           string       `json:"name"`
	ReturnType     TypeRef      `json:"returnType"`
	TypeParameters []TypeParam  `json:"typeParameters,omitempty"`
	Parameters     []Parameter  `json:"parameters,omitempty"`
	IsStatic       bool         `json:"isStatic,omitempty"`
	IsAbstract     bool         `json:"isAbstract,omitempty"`
	IsOverride     bool         `json:"isOverride,omitempty"`
	Modifiers      []string     `json:"modifiers,omitempty"`
	Annotations    []Annotation `json:"annotations,omitempty"`
	Documentation  string       `json:"documentation,omitempty"`
	Location       Location     `json:"location"`
	BodyReferences *BodyRefs    `json:"bodyReferences,omitempty"`
	NativeInfo     *NativeInfo  `json:"nativeInfo,omitempty"`
}

// FieldDecl represents a field
type FieldDecl struct {
	Name          string   `json:"name"`
	Type          TypeRef  `json:"type"`
	IsStatic      bool     `json:"isStatic,omitempty"`
	IsFinal       bool     `json:"isFinal,omitempty"`
	IsConst       bool     `json:"isConst,omitempty"`
	IsLate        bool     `json:"isLate,omitempty"`
	Documentation string   `json:"documentation,omitempty"`
	Location      Location `json:"location"`
}

// AccessorDecl represents a getter or setter
type AccessorDecl struct {
	Name          string      `json:"name"`
	Type          TypeRef     `json:"type"`
	IsGetter      bool        `json:"isGetter"`
	IsSetter      bool        `json:"isSetter"`
	IsStatic      bool        `json:"isStatic,omitempty"`
	Documentation string      `json:"documentation,omitempty"`
	Location      Location    `json:"location"`
	BodyReferences *BodyRefs  `json:"bodyReferences,omitempty"`
}

// Parameter represents a function/method parameter
type Parameter struct {
	Name         string   `json:"name"`
	Type         TypeRef  `json:"type"`
	IsRequired   bool     `json:"isRequired,omitempty"`
	IsNamed      bool     `json:"isNamed,omitempty"`
	IsPositional bool     `json:"isPositional,omitempty"`
	DefaultValue string   `json:"defaultValue,omitempty"`
}

// BodyRefs represents references found in function bodies
type BodyRefs struct {
	ConstructorCalls []ConstructorCall `json:"constructorCalls,omitempty"`
	MethodCalls      []MethodCall      `json:"methodCalls,omitempty"`
	PropertyAccesses []PropertyAccess  `json:"propertyAccesses,omitempty"`
	TypeCasts        []string          `json:"typeCasts,omitempty"`
	TypeChecks       []string          `json:"typeChecks,omitempty"`
}

// ConstructorCall represents a constructor call in body
type ConstructorCall struct {
	Type string `json:"type"`
}

// MethodCall represents a method call in body
type MethodCall struct {
	TargetType string `json:"targetType,omitempty"`
	MethodName string `json:"methodName"`
	IsStatic   bool   `json:"isStatic,omitempty"`
}

// PropertyAccess represents a property access in body
type PropertyAccess struct {
	TargetType   string `json:"targetType,omitempty"`
	PropertyName string `json:"propertyName"`
}

// NativeInfo represents @Native FFI annotation info
type NativeInfo struct {
	Symbol string `json:"symbol"`
	IsLeaf bool   `json:"isLeaf,omitempty"`
}
