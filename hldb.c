/*** filetypes ***/

char * C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char * C_HL_keywords[] = {
	"break", "case", "class", "continue", "do", "default", "else", "enum", "extern", "for", "if", "goto", "register", "return", "static", "sizeof", "struct", "switch", "typedef", "union", "while", 
	"auto|", "char|", "const|", "double|", "float|", "int|", "long|", "signed|", "short|", "void|", "volatile|", "unsigned|"
};

char * PY_HL_extensions[] = { ".py", NULL };
char * PY_HL_keywords[] = {
	"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
	"str|", "int|", "float|", "complex|", "list|", "tuple|", "range|", "dict|", "set|", "frozenset|", "bool|", "bytes|", "bytearray|", "memoryview|"	
};