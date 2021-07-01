/*** filetypes ***/

/* C */
char * C_HL_extensions[] = { ".c", NULL };
char * C_HL_keywords[] = {
	"break", "case", "continue", "do", "default", "else", "enum", "extern", "for", "if", "goto", "NULL", "register", "return", "static", "sizeof",
	"struct", "switch", "typedef", "union", "while", 
	"auto|", "char|", "const|", "double|", "float|", "int|", "long|", "signed|", "short|", "void|", "volatile|", "unsigned|"
};

char * CPP_HL_extensions[] = { ".h", ".cpp", NULL };
char * CPP_HL_keywords[] = {
	"asm", "break", "case", "catch", "class", "continue", "const_cast", "do", "default", "delete", "dynamic_cast", "else", "enum", "extern",
	"explicit", "false", "for", "friend", "if", "goto", "inline", "mutable", "namespace", "new", "NULL", "operator", "private", "protected",
	"public",  "register", "reinterpret_cast", "return", "static", "static_cast", "sizeof",	"struct", "switch", "template", "this", "throw",
	"true", "try", "typedef", "typeid", "typename", "union", "using","virtual",  "while", 
	"auto|", "bool|", "char|", "const|", "double|", "float|", "int|", "long|", "signed|", "short|", "void|", "volatile|", "unsigned|", "wchar_t|"
};

/* Python */
char * PY_HL_extensions[] = { ".py", NULL };
char * PY_HL_keywords[] = {
	"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else", "except",
	"finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",	"return", "try", "while",
	"with", "yield",
	"str|", "int|", "float|", "complex|", "list|", "tuple|", "range|", "dict|", "set|", "frozenset|", "bool|", "bytes|", "bytearray|", "memoryview|"
};

/* Javascript */
char * JS_HL_extensions[] = { ".js", NULL };
char * JS_HL_keywords[] = {
	"abstract", "arguments", "await", "break", "case", "catch", "class", "continue", "debugger", "default", "delete", "do", "else", "enum", "eval",
	"export", "extends", "false", "final", "finally", "for", "function", "goto", "if", "implements", "import", "in", "instanceof", "interface",
	"let", "native", "new", "null", "package", "private", "protected", "public", "return", "static", "super", "switch", "synchronized", "this",
	"throw", "throws", "transient", "true", "try", "typeof", "var", "while", "with", "yield", 
	"boolean|", "byte|", "char|", "const|", "double|", "float|", "int|", "long|", "short|", "void|", "volatile|", "var|"
};
