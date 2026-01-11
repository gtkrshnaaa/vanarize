#include "Compiler/Parser.h"
#include "Compiler/Lexer.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

static Token currentToken;
static Token previousToken;
static Token nextToken; // Lookahead

static const char* currentNamespacePrefix = NULL;

// Forward declarations
static void scanNext();
static void advance();

typedef struct {
    Token current;
    Token previous;
    Token next;
    const char* namespacePrefix;
} ParserState;

static ParserState Parser_GetState() {
    ParserState state;
    state.current = currentToken;
    state.previous = previousToken;
    state.next = nextToken;
    state.namespacePrefix = currentNamespacePrefix;
    return state;
}

static void Parser_RestoreState(ParserState state) {
    currentToken = state.current;
    previousToken = state.previous;
    nextToken = state.next;
    currentNamespacePrefix = state.namespacePrefix;
}

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(1);
    }
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(1);
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(1);
    }
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

// Forward declare declaration
static AstNode* declaration();

static AstNode* compileFile(const char* path, const char* namespacePrefix) {
    // 1. Save State
    ParserState pState = Parser_GetState();
    LexerState lState = Lexer_GetState();
    
    // 2. Read File
    char* source = readFile(path);
    
    // 3. Init Compiler for new file
    Lexer_Init(source);
    currentNamespacePrefix = namespacePrefix;
    scanNext(); // Prime
    advance();  // Prime
    
    // 4. Parse File (Program Level)
    BlockStmt* block = malloc(sizeof(BlockStmt));
    block->main.type = NODE_BLOCK;
    block->statements = malloc(sizeof(AstNode*) * 1024);
    block->count = 0;
    
    while (currentToken.type != TOKEN_EOF) {
        block->statements[block->count++] = declaration();
    }
    
    // 5. Restore State
    Parser_RestoreState(pState);
    Lexer_RestoreState(lState);
    
    // Note: We leak 'source' intentionally because Tokens point to it.
    
    return (AstNode*)block;
}

static void scanNext() {
    for (;;) {
        nextToken = Lexer_NextToken();
        if (nextToken.type != TOKEN_ERROR) break;
        fprintf(stderr, "Lexer error: %.*s\n", nextToken.length, nextToken.start);
        exit(1);
    }
}

static void advance() {
    previousToken = currentToken;
    currentToken = nextToken;
    scanNext();
}

static void consume(TokenType type, const char* message) {
    if (currentToken.type == type) {
        advance();
        return;
    }
    fprintf(stderr, "[Parser] Error at line %d: %s\n", currentToken.line, message);
    fprintf(stderr, "Got token type %d\n", currentToken.type);
    exit(1);
}

void Parser_Init(const char* source) {
    Lexer_Init(source);
    scanNext(); // Fill nextToken with 1st token
    advance();  // Fill currentToken with 1st token, nextToken with 2nd
}

// Forward decls
static AstNode* expression();
static AstNode* assignment();
static AstNode* equality();
static AstNode* comparison();
static AstNode* term();
static AstNode* factor();
static AstNode* unary();
static AstNode* call();
static AstNode* primary();
static AstNode* statement();

static AstNode* expression() {
    return assignment();
}

static AstNode* assignment() {
    AstNode* expr = equality();
    
    if (currentToken.type == TOKEN_EQUAL) {
        advance();
        AstNode* value = assignment();
        
        if (expr->type == NODE_LITERAL_EXPR) {
            LiteralExpr* lit = (LiteralExpr*)expr;
            if (lit->token.type == TOKEN_IDENTIFIER) {
                AssignmentExpr* node = malloc(sizeof(AssignmentExpr));
                node->main.type = NODE_ASSIGNMENT_EXPR;
                node->name = lit->token;
                node->value = value;
                return (AstNode*)node;
            }
        }
        else if (expr->type == NODE_GET_EXPR) {
            GetExpr* get = (GetExpr*)expr;
            SetExpr* node = malloc(sizeof(SetExpr));
            node->main.type = NODE_SET_EXPR;
            node->object = get->object;
            node->name = get->name;
            node->value = value;
            return (AstNode*)node;
        }
        else if (expr->type == NODE_INDEX_EXPR) {
            IndexExpr* indexExpr = (IndexExpr*)expr;
            IndexSetExpr* node = malloc(sizeof(IndexSetExpr));
            node->main.type = NODE_INDEX_SET_EXPR;
            node->array = indexExpr->array;
            node->index = indexExpr->index;
            node->value = value;
             // Free the old IndexExpr shell if needed, but AST nodes are usually arena/pool allocated or leaked until end.
            return (AstNode*)node;
        }
        
        fprintf(stderr, "[Parser] Invalid assignment target.\n");
        exit(1);
    }
    
    return expr;
}

// term() parses binary + -
// factor() parses * /
// call() parses calls
// primary() parses literals and identifiers.


static AstNode* equality() {
    AstNode* expr = comparison();
    
    while (currentToken.type == TOKEN_EQUAL_EQUAL || currentToken.type == TOKEN_BANG_EQUAL) {
        Token op = currentToken;
        advance();
        AstNode* right = comparison();
        
        BinaryExpr* node = malloc(sizeof(BinaryExpr));
        node->main.type = NODE_BINARY_EXPR;
        node->left = expr;
        node->right = right;
        node->op = op;
        expr = (AstNode*)node;
    }
    return expr;
}

static AstNode* comparison() {
    AstNode* expr = term();
    
    while (currentToken.type == TOKEN_LESS || 
           currentToken.type == TOKEN_GREATER ||
           currentToken.type == TOKEN_LESS_EQUAL ||
           currentToken.type == TOKEN_GREATER_EQUAL) {
        Token op = currentToken;
        advance();
        AstNode* right = term();
        
        BinaryExpr* node = malloc(sizeof(BinaryExpr));
        node->main.type = NODE_BINARY_EXPR;
        node->left = expr;
        node->right = right;
        node->op = op;
        expr = (AstNode*)node;
    }
    return expr;
}

static AstNode* term() {
    AstNode* expr = factor();
    
    // Addition and subtraction
    while (currentToken.type == TOKEN_PLUS || currentToken.type == TOKEN_MINUS) {
        Token op = currentToken;
        advance();
        AstNode* right = factor();
        
        BinaryExpr* node = malloc(sizeof(BinaryExpr));
        node->main.type = NODE_BINARY_EXPR;
        node->left = expr;
        node->right = right;
        node->op = op;
        expr = (AstNode*)node;
    }
    
    return expr;
}

static AstNode* call() {
    AstNode* expr = primary();
    
    while (true) {
        if (currentToken.type == TOKEN_LEFT_PAREN) {
            advance();
            CallExpr* node = malloc(sizeof(CallExpr));
            node->main.type = NODE_CALL_EXPR;
            node->callee = expr;
            
            node->args = malloc(sizeof(AstNode*) * 8);
            node->argCount = 0;
            
            if (currentToken.type != TOKEN_RIGHT_PAREN) {
                do {
                    node->args[node->argCount++] = expression();
                } while (currentToken.type == TOKEN_COMMA && (advance(), 1));
            }
            
            consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
            expr = (AstNode*)node;
        } else if (currentToken.type == TOKEN_DOT) {
            advance();
            consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
            Token name = previousToken;
            
            GetExpr* node = malloc(sizeof(GetExpr));
            node->main.type = NODE_GET_EXPR;
            node->object = expr;
            node->name = name;
            node->name = name;
            expr = (AstNode*)node;
        } else if (currentToken.type == TOKEN_LEFT_BRACKET) {
             advance();
             AstNode* index = expression();
             consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
             
             IndexExpr* node = malloc(sizeof(IndexExpr));
             node->main.type = NODE_INDEX_EXPR;
             node->array = expr;
             node->index = index;
             expr = (AstNode*)node;
        } else {
            break;
        }
    }
    return expr;
}

static AstNode* unary() {
    // MASTERPLAN: await expression parsing
    if (currentToken.type == TOKEN_AWAIT) {
        advance();
        AstNode* expr = unary();  // Parse the awaited expression
        
        AwaitExpr* node = malloc(sizeof(AwaitExpr));
        node->main.type = NODE_AWAIT_EXPR;
        node->expression = expr;
        return (AstNode*)node;
    }
    
    if (currentToken.type == TOKEN_BANG || currentToken.type == TOKEN_MINUS) {
        Token op = currentToken;
        advance();
        AstNode* right = unary();
        
        UnaryExpr* node = malloc(sizeof(UnaryExpr));
        node->main.type = NODE_UNARY_EXPR;
        node->op = op;
        node->right = right;
        return (AstNode*)node;
    }
    return call();
}

static AstNode* factor() {
    AstNode* expr = unary();
    
    while (currentToken.type == TOKEN_SLASH || currentToken.type == TOKEN_STAR) {
        Token op = currentToken;
        advance();
        AstNode* right = unary();
        
        BinaryExpr* node = malloc(sizeof(BinaryExpr));
        node->main.type = NODE_BINARY_EXPR;
        node->left = expr;
        node->right = right;
        node->op = op;
        expr = (AstNode*)node;
    }
    
    return expr;
}


static AstNode* primary() {
    if (currentToken.type == TOKEN_NUMBER) {
        LiteralExpr* node = malloc(sizeof(LiteralExpr));
        node->main.type = NODE_LITERAL_EXPR;
        node->token = currentToken;
        advance();
        return (AstNode*)node;
    }
    
    if (currentToken.type == TOKEN_STRING) {
        StringExpr* node = malloc(sizeof(StringExpr));
        node->main.type = NODE_STRING_LITERAL;
        node->token = currentToken;
        advance();
        return (AstNode*)node;
    }

    if (currentToken.type == TOKEN_IDENTIFIER) {
        // Treat as literal/variable access
        LiteralExpr* node = malloc(sizeof(LiteralExpr));
        node->main.type = NODE_LITERAL_EXPR; // Reusing Literal for Var for now
        node->token = currentToken;
        advance();
        return (AstNode*)node;
    }

    if (currentToken.type == TOKEN_PRINT) {
        // Treat 'print' as an identifier-like literal so it can be called
        LiteralExpr* node = malloc(sizeof(LiteralExpr));
        node->main.type = NODE_LITERAL_EXPR;
        node->token = currentToken;
        advance();
        return (AstNode*)node;
    }
    
    if (currentToken.type == TOKEN_TRUE || currentToken.type == TOKEN_FALSE || currentToken.type == TOKEN_NIL) {
        LiteralExpr* node = malloc(sizeof(LiteralExpr));
        node->main.type = NODE_LITERAL_EXPR;
        node->token = currentToken;
        advance();
        return (AstNode*)node;
    }
    
    if (currentToken.type == TOKEN_LEFT_PAREN) {
        advance();
        AstNode* expr = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
        return expr;
    }
    
    if (currentToken.type == TOKEN_LEFT_BRACKET) {
        advance();
        AstNode** elements = malloc(sizeof(AstNode*) * 16); // Initial cap 16
        int count = 0;
        int capacity = 16;
        
        if (currentToken.type != TOKEN_RIGHT_BRACKET) {
            do {
                if (count >= capacity) {
                    capacity *= 2;
                    elements = realloc(elements, sizeof(AstNode*) * capacity);
                }
                elements[count++] = expression();
            } while (currentToken.type == TOKEN_COMMA && (advance(), 1));
        }
        consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
        
        ArrayLiteral* node = malloc(sizeof(ArrayLiteral));
        node->main.type = NODE_ARRAY_LITERAL;
        node->elements = elements;
        node->count = count;
        return (AstNode*)node;
    }
    
    // Grouping, etc. omitted for briefness
    fprintf(stderr, "[Parser] Expect expression. Got token type %d\n", currentToken.type);
    exit(1);
    return NULL;
}

static AstNode* statement();



static bool check(TokenType type) {
    return currentToken.type == type;
}

static AstNode* parseStructInit(Token typeName) {
    consume(TOKEN_LEFT_BRACE, "Expect '{' for struct initialization.");
    
    Token* fieldNames = malloc(sizeof(Token) * 16);
    AstNode** values = malloc(sizeof(AstNode*) * 16);
    int count = 0;
    
    if (currentToken.type != TOKEN_RIGHT_BRACE) {
        do {
            // key: value
            consume(TOKEN_IDENTIFIER, "Expect field name.");
            fieldNames[count] = previousToken;
            
            consume(TOKEN_COLON, "Expect ':' after field name.");
            
            values[count] = expression();
            count++;
        } while (currentToken.type == TOKEN_COMMA && (advance(), 1));
    }
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after struct init.");
    
    StructInit* node = malloc(sizeof(StructInit));
    node->main.type = NODE_STRUCT_INIT;
    node->structName = typeName;
    node->fieldNames = fieldNames;
    node->values = values;
    node->fieldCount = count;
    return (AstNode*)node;
}

// Helper forward decl or static definition
static AstNode* parseVarDecl(bool typed, Token typeToken) {
    consume(TOKEN_IDENTIFIER, "Expect variable name.");
    Token name = previousToken;
    AstNode* initializer = NULL;
    if (currentToken.type == TOKEN_EQUAL) {
        advance();
        if (currentToken.type == TOKEN_LEFT_BRACE) {
            // Struct Init syntax: { key: val, ... }
            if (!typed) {
                 fprintf(stderr, "[Parser] Cannot infer type for anonymous struct literal.\n");
                 exit(1);
            }
            initializer = parseStructInit(typeToken);
        } else {
            initializer = expression();
        }
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    
    VarDecl* node = malloc(sizeof(VarDecl));
    node->main.type = NODE_VAR_DECL;
    node->name = name;
    node->typeName = typeToken; // Set if typed
    node->initializer = initializer;
    return (AstNode*)node;
}

static AstNode* declaration() {
    // 0. Import Statement
    if (currentToken.type == TOKEN_IMPORT) {
        advance();
        consume(TOKEN_STRING, "Expect string after 'import'.");
        
        // LiteralExpr* pathExpr = ...
        // We need the string content. 
        // Token string has quotes.
        int len = previousToken.length - 2;
        char* path = malloc(len + 1);
        memcpy(path, previousToken.start + 1, len);
        path[len] = '\0';
        
        // Extract Namespace (Basename)
        // e.g. "Libs/Math.vana" -> "Math"
        char* base = strrchr(path, '/');
        char* filename = base ? base + 1 : path;
        
        // Find extension dot
        char* dot = strrchr(filename, '.');
        int nameLen = dot ? (int)(dot - filename) : (int)strlen(filename);
        
        char* nsPrefix = malloc(nameLen + 2); // "Math_"
        memcpy(nsPrefix, filename, nameLen);
        nsPrefix[nameLen] = '_';
        nsPrefix[nameLen + 1] = '\0';
        
        AstNode* moduleBlock = compileFile(path, nsPrefix);
        free(path); // Path is temporary, source read is persistent
        // nsPrefix is persistent? Used in compileFile for CURRENT prefix.
        // But tokens created will point to strings? 
        // Fn name creation needs to allocate new Token string. 
        // So nsPrefix can be freed if we use it to CREATE tokens properly.
        // Wait, compileFile sets global `currentNamespacePrefix`.
        // declaration() -> FunctionDecl uses it.
        // If FunctionDecl keeps it, we can't free it yet?
        // Actually, FunctionDecl parsing will use it to generated prefixed names.
        // If we generate new strings, we are good.
        // So we can free nsPrefix after compileFile returns?
        // NO, if we implement recursive imports, we might need it? 
        // compileFile uses it synchronously. So yes, free after return is safe.
        
        // BUT wait, for the Function Parsing, we need the prefix string to exist during parsing.
        
        consume(TOKEN_SEMICOLON, "Expect ';' after import.");
        
        // We return the Block containing library functions.
        // The CodeGen will emit them.
        return moduleBlock;
    }

    // 1. Struct Declaration
    if (currentToken.type == TOKEN_STRUCT) {
        advance();
        consume(TOKEN_IDENTIFIER, "Expect struct name.");
        Token structName = previousToken;
        consume(TOKEN_LEFT_BRACE, "Expect '{' before struct body.");
        
        Token* fields = malloc(sizeof(Token) * 16);
        Token* fieldTypes = malloc(sizeof(Token) * 16);
        int fieldCount = 0;
        
        while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
             TokenType t = currentToken.type;
             if (t == TOKEN_TYPE_BYTE || t == TOKEN_TYPE_SHORT || t == TOKEN_TYPE_INT || t == TOKEN_TYPE_LONG ||
                 t == TOKEN_TYPE_FLOAT || t == TOKEN_TYPE_DOUBLE || t == TOKEN_TYPE_CHAR || t == TOKEN_TYPE_BOOLEAN ||
                 t == TOKEN_TYPE_STRING || t == TOKEN_IDENTIFIER) {
                 fieldTypes[fieldCount] = currentToken;
                 advance();
             } else {
                 fprintf(stderr, "[Parser] Expect field type in struct. Got %d\n", t);
                 exit(1);
             }
             
             consume(TOKEN_IDENTIFIER, "Expect field name.");
             fields[fieldCount] = previousToken;
             fieldCount++;
        }
        consume(TOKEN_RIGHT_BRACE, "Expect '}' after struct body.");
        
        StructDecl* node = malloc(sizeof(StructDecl));
        node->main.type = NODE_STRUCT_DECL;
        node->name = structName;
        node->fields = fields;
        node->fieldTypes = fieldTypes;
        node->fieldCount = fieldCount;
        return (AstNode*)node;
    }

    // 2. Typed Declaration or 'var'
    Token typeToken = {0};


    
    if (currentToken.type == TOKEN_TYPE_BYTE || currentToken.type == TOKEN_TYPE_SHORT ||
        currentToken.type == TOKEN_TYPE_INT || currentToken.type == TOKEN_TYPE_LONG ||
        currentToken.type == TOKEN_TYPE_FLOAT || currentToken.type == TOKEN_TYPE_DOUBLE ||
        currentToken.type == TOKEN_TYPE_CHAR || currentToken.type == TOKEN_TYPE_BOOLEAN ||
        currentToken.type == TOKEN_TYPE_STRING) {
        typeToken = currentToken;
        advance();
        
        // Array Type Check: int[]
        if (currentToken.type == TOKEN_LEFT_BRACKET) {
            advance(); // Eat [
            consume(TOKEN_RIGHT_BRACKET, "Expect ']' after '[' for array type."); // Eat ]
            
            // Extend typeToken to cover "int[]"
            const char* end = previousToken.start + previousToken.length;
            typeToken.length = (int)(end - typeToken.start);
        }
        
        return parseVarDecl(true, typeToken);
    }
    
    // Identifier check (Struct Type?)
    // 'StructName varName' -> ID ID
    if (currentToken.type == TOKEN_IDENTIFIER && nextToken.type == TOKEN_IDENTIFIER) {
         typeToken = currentToken;
         advance();
         return parseVarDecl(true, typeToken);
    }

   // Struct Array Type: MyStruct[] varName
    if (currentToken.type == TOKEN_IDENTIFIER && nextToken.type == TOKEN_LEFT_BRACKET) {
        // Peek ahead to ensure it's not array access? No, declaration context.
        // It could be 'MyStruct[] name'.
        // Or 'var[idx] = ...' (Statement).
        // statement() handles assignment. declaration() handles declarations.
        // If we are here, we expect a declaration.
        // But 'MyStruct[0] = 1' is a statement.
        // Ambiguity?
        // declaration() logic handles: import, struct, var, functions, statements.
        // Wait, standard C: 'ID ID' is declaration. 'ID[...]' is statement start.
        // Unnarize: 'Variable must be declared'.
        // If we see 'Type[] ID', it's a declaration.
        // If we see 'ID[...]', it's a statement.
        // We need 3-token lookahead? 
        // Current: current=Type, next=[.
        // If next-next is ']', it's array type.
        // Lexer provides 'nextToken'. We don't have nextNextToken.
        // We can peek more? Parser has 'scanNext'.
        // But `nextToken` is 1 lookahead.
        // We can hack: check if nextToken is [ and assume array type IF...
        // Wait. `Parser.c` parses statements at the end of declaration()?
        // No, `block->statements` contains declarations.
        // In `declaration()`: if not specific keyword, checks Types.
        // Fallback: `return statement();`
        
        // If we implement Struct Arrays:
        // We need to distinguish `Struct[] x` from `x[0] = 1`.
        // `Struct` is TOKEN_IDENTIFIER. `x` is TOKEN_IDENTIFIER.
        // `x` is TOKEN_IDENTIFIER. `[` is TOKEN_LEFT_BRACKET.
        
        // If identifiers are types...
        // Masterplan says: PascalCase for Types. camelCase for vars.
        // We *could* enforce this?
        // But simpler: 'Type[]' requires ']' immediately.
        // 'Var[' requires expression.
        
        // Implementation Limitation: For now, only Primitive Arrays supported easily.
        // Or we assume that if we see "ID [ ] ID", it is declaration.
         
         // Let's stick to primitive arrays for this Step implementation if tricky.
         // Masterplan enforces explicit typing.
         // I'll leave Struct Array support for now or implement simplistic check:
         // If `ID [ ]` -> it is array type.
         // If `ID [ expr ]` -> statement.
         // We can't see past `[`.
         // Unless we speculatively parse brackets?
         // Leave it for now. Primitives are priority.
    }

    // MASTERPLAN: async function support
    int isAsync = 0;
    if (currentToken.type == TOKEN_ASYNC) {
        isAsync = 1;
        advance();
    }

    if (currentToken.type == TOKEN_FUNCTION) {
        advance();
        consume(TOKEN_IDENTIFIER, "Expect function name.");
        Token name = previousToken;
        
        // NAMESPACE PREFIXING
        if (currentNamespacePrefix != NULL) {
            // Create new name: Prefix + Name
            int prefixLen = strlen(currentNamespacePrefix);
            int totalLen = prefixLen + name.length;
            char* newName = malloc(totalLen + 1);
            memcpy(newName, currentNamespacePrefix, prefixLen);
            memcpy(newName + prefixLen, name.start, name.length);
            newName[totalLen] = '\0';
            
            // Hack Token to point to new string (Leaked intentionally for AST lifetime)
            name.start = newName;
            name.length = totalLen;
            // Type stays IDENTIFIER
        }
        
        consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
        
        Token* params = malloc(sizeof(Token) * 8); 
        Token* paramTypes = malloc(sizeof(Token) * 8);
        int paramCount = 0;
        
        if (currentToken.type != TOKEN_RIGHT_PAREN) {
            do {
                // Check if typed: Type Name
                Token typeToken = {0};
                if (currentToken.type == TOKEN_TYPE_BYTE || currentToken.type == TOKEN_TYPE_SHORT ||
                    currentToken.type == TOKEN_TYPE_INT || currentToken.type == TOKEN_TYPE_LONG ||
                    currentToken.type == TOKEN_TYPE_FLOAT || currentToken.type == TOKEN_TYPE_DOUBLE ||
                    currentToken.type == TOKEN_TYPE_CHAR || currentToken.type == TOKEN_TYPE_BOOLEAN ||
                    currentToken.type == TOKEN_TYPE_STRING) {
                    typeToken = currentToken;
                    advance();
                } else if (currentToken.type == TOKEN_IDENTIFIER && nextToken.type == TOKEN_IDENTIFIER) {
                    // Struct Type
                    typeToken = currentToken;
                    advance();
                }
                
                consume(TOKEN_IDENTIFIER, "Expect parameter name.");
                params[paramCount] = previousToken;
                paramTypes[paramCount] = typeToken;
                paramCount++;
            } while (currentToken.type == TOKEN_COMMA && (advance(), 1));
        }
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
        
        // Return Type :: Type
        Token returnType = {0};
        if (currentToken.type == TOKEN_DOUBLE_COLON) {
            advance();
            if (currentToken.type == TOKEN_TYPE_BYTE || currentToken.type == TOKEN_TYPE_SHORT ||
                currentToken.type == TOKEN_TYPE_INT || currentToken.type == TOKEN_TYPE_LONG ||
                currentToken.type == TOKEN_TYPE_FLOAT || currentToken.type == TOKEN_TYPE_DOUBLE ||
                currentToken.type == TOKEN_TYPE_CHAR || currentToken.type == TOKEN_TYPE_BOOLEAN ||
                currentToken.type == TOKEN_TYPE_STRING || currentToken.type == TOKEN_TYPE_VOID || 
                currentToken.type == TOKEN_IDENTIFIER) {
                returnType = currentToken;
                advance();
            } else {
                 fprintf(stderr, "[Parser] Expect type after '::'\n");
                 exit(1);
            }
        }
        
        consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
        BlockStmt* body = malloc(sizeof(BlockStmt));
        body->main.type = NODE_BLOCK;
        body->statements = malloc(sizeof(AstNode*) * 64);
        body->count = 0;
        
        while (currentToken.type != TOKEN_RIGHT_BRACE && currentToken.type != TOKEN_EOF) {
            body->statements[body->count++] = declaration();
        }
        consume(TOKEN_RIGHT_BRACE, "Expect '}' after function body.");
        
        FunctionDecl* node = malloc(sizeof(FunctionDecl));
        node->main.type = NODE_FUNCTION_DECL;
        node->name = name;
        node->params = params;
        node->paramTypes = paramTypes;
        node->paramCount = paramCount;
        node->returnType = returnType;
        node->body = (AstNode*)body;
        node->isAsync = isAsync;  // MASTERPLAN: async flag
        return (AstNode*)node;
    }

    return statement();
}

static AstNode* statement() {
    if (currentToken.type == TOKEN_RETURN) {
        advance();
        AstNode* value = NULL;
        if (currentToken.type != TOKEN_SEMICOLON) {
            value = expression();
        }
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        
        ReturnStmt* node = malloc(sizeof(ReturnStmt));
        node->main.type = NODE_RETURN_STMT;
        node->returnValue = value;
        return (AstNode*)node;
    }

    if (currentToken.type == TOKEN_IF) {
        advance();
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
        AstNode* condition = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
        
        AstNode* thenBranch = statement();
        AstNode* elseBranch = NULL;
        
        if (currentToken.type == TOKEN_ELSE) {
            advance();
            elseBranch = statement();
        }
        
        IfStmt* node = malloc(sizeof(IfStmt));
        node->main.type = NODE_IF_STMT;
        node->condition = condition;
        node->thenBranch = thenBranch;
        node->elseBranch = elseBranch;
        return (AstNode*)node;
    }
    
    if (currentToken.type == TOKEN_FOR) {
        advance();
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
        
        // 1. Initializer
        AstNode* initializer;
        if (currentToken.type == TOKEN_SEMICOLON) {
            consume(TOKEN_SEMICOLON, "Expect ';'.");
            initializer = NULL;
        } else {
            // declaration() parses VarDecl or ExpressionStmt and consumes ';'
            initializer = declaration();
        }
        
        // 2. Condition
        AstNode* condition = NULL;
        if (currentToken.type != TOKEN_SEMICOLON) {
            condition = expression();
        }
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
        
        // 3. Increment
        AstNode* increment = NULL;
        if (currentToken.type != TOKEN_RIGHT_PAREN) {
            increment = expression();
        }
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        
        // 4. Body
        AstNode* body = statement();
        
        // Desugar: { init; while(cond) { body; inc; } }
        // MASTERPLAN: while is REMOVED. Use ForStmt directly!
        
        ForStmt* forStmt = malloc(sizeof(ForStmt));
        forStmt->main.type = NODE_FOR_STMT;
        forStmt->initializer = initializer;
        forStmt->condition = condition;
        forStmt->increment = increment;
        forStmt->body = body;
        
        return (AstNode*)forStmt;
    }
    
    // NOTE: TOKEN_WHILE removed per MASTERPLAN Section 4.2.C
    // 'while' loops are explicitly NOT supported in Vanarize
    
    if (currentToken.type == TOKEN_LEFT_BRACE) {
        advance();
        BlockStmt* node = malloc(sizeof(BlockStmt));
        node->main.type = NODE_BLOCK;
        node->statements = malloc(sizeof(AstNode*) * 64);
        node->count = 0;
        
        while (currentToken.type != TOKEN_RIGHT_BRACE && currentToken.type != TOKEN_EOF) {
            node->statements[node->count++] = declaration();
        }
        consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
        return (AstNode*)node;
    }
    
    AstNode* expr = expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    return expr;
}

AstNode* Parser_ParseExpression(void) {
    // HACK: For now, we return a Block containing declarations
    // We need to parse multiple statements.
    
    // We'll create a BlockStmt node to hold everything
    BlockStmt* block = malloc(sizeof(BlockStmt));
    block->main.type = NODE_BLOCK;
    block->statements = malloc(sizeof(AstNode*) * 64); // Max 64 stmts
    block->count = 0;
    
    block->count = 0;
    
    while (currentToken.type != TOKEN_EOF) {
        AstNode* decl = declaration();
        block->statements[block->count++] = decl;
    }
    
    return (AstNode*)block;
}

// Alias for ParseExpression (both do program-level parsing)
AstNode* Parser_ParseProgram(void) {
    return Parser_ParseExpression();
}
