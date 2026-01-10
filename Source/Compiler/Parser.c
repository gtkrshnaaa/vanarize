#include "Compiler/Parser.h"
#include "Compiler/Lexer.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static Token currentToken;
static Token previousToken;
static Token nextToken; // Lookahead

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
    exit(1);
}

void Parser_Init(const char* source) {
    Lexer_Init(source);
    scanNext(); // Fill nextToken with 1st token
    advance();  // Fill currentToken with 1st token, nextToken with 2nd
}

// Forward decls
static AstNode* expression();
static AstNode* term();
static AstNode* factor();
static AstNode* primary();



// Correct implementation of assignment:
static AstNode* assignment();

static AstNode* expression() {
    return assignment();
}

static AstNode* assignment() {
    AstNode* expr = term();
    
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
        
        fprintf(stderr, "[Parser] Invalid assignment target.\n");
        exit(1);
    }
    
    return expr;
}

// term() parses binary + -
// factor() parses * /
// call() parses calls
// primary() parses literals and identifiers.


static AstNode* term() {
    AstNode* expr = factor();
    
    // Comparison operators: <, >, <=, >=, ==, !=
    while (currentToken.type == TOKEN_LESS || 
           currentToken.type == TOKEN_GREATER ||
           currentToken.type == TOKEN_LESS_EQUAL ||
           currentToken.type == TOKEN_GREATER_EQUAL ||
           currentToken.type == TOKEN_EQUAL_EQUAL ||
           currentToken.type == TOKEN_BANG_EQUAL) {
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
            node->callee = expr; // Store the expression (identifier or GET_EXPR for namespace.method)
            
            // We need to store arguments.
            node->args = malloc(sizeof(AstNode*) * 8); // Max 8 args for now
            node->argCount = 0;
            
            if (currentToken.type != TOKEN_RIGHT_PAREN) {
                do {
                    node->args[node->argCount++] = expression();
                } while (currentToken.type == TOKEN_COMMA && (advance(), 1)); // Cute comma skip
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
            expr = (AstNode*)node;
        } else {
            break;
        }
    }
    
    return expr;
}

static AstNode* factor() {
    AstNode* expr = call();
    // ... rest same
    
    while (currentToken.type == TOKEN_SLASH || currentToken.type == TOKEN_STAR) {
        Token op = currentToken;
        advance();
        AstNode* right = primary();
        
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
             if (t == TOKEN_TYPE_NUMBER || t == TOKEN_TYPE_TEXT || t == TOKEN_TYPE_BOOLEAN || t == TOKEN_IDENTIFIER) {
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
    bool isTyped = false;
    Token typeToken = {0};

    if (currentToken.type == TOKEN_VAR) {
        advance();
        return parseVarDecl(false, (Token){0});
    }
    
    if (currentToken.type == TOKEN_TYPE_NUMBER || 
        currentToken.type == TOKEN_TYPE_TEXT || 
        currentToken.type == TOKEN_TYPE_BOOLEAN) {
        typeToken = currentToken;
        advance();
        return parseVarDecl(true, typeToken);
    }
    
    // Identifier check (Struct Type?)
    // 'StructName varName' -> ID ID
    if (currentToken.type == TOKEN_IDENTIFIER && nextToken.type == TOKEN_IDENTIFIER) {
         typeToken = currentToken;
         advance();
         return parseVarDecl(true, typeToken);
    }

    if (currentToken.type == TOKEN_FUNCTION) {
        advance();
        consume(TOKEN_IDENTIFIER, "Expect function name.");
        Token name = previousToken;
        
        consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
        
        Token* params = malloc(sizeof(Token) * 8); 
        Token* paramTypes = malloc(sizeof(Token) * 8);
        int paramCount = 0;
        
        if (currentToken.type != TOKEN_RIGHT_PAREN) {
            do {
                // Check if typed: Type Name
                Token typeToken = {0};
                if (currentToken.type == TOKEN_TYPE_NUMBER || 
                    currentToken.type == TOKEN_TYPE_TEXT || 
                    currentToken.type == TOKEN_TYPE_BOOLEAN) {
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
            if (currentToken.type == TOKEN_TYPE_NUMBER || 
                currentToken.type == TOKEN_TYPE_TEXT || 
                currentToken.type == TOKEN_TYPE_BOOLEAN ||
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
    
    if (currentToken.type == TOKEN_WHILE) {
        advance();
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
        AstNode* condition = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
        AstNode* body = statement();
        
        WhileStmt* node = malloc(sizeof(WhileStmt));
        node->main.type = NODE_WHILE_STMT;
        node->condition = condition;
        node->body = body;
        return (AstNode*)node;
    }
    
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
    
    while (currentToken.type != TOKEN_EOF) {
        block->statements[block->count++] = declaration();
    }
    
    return (AstNode*)block;
}

// Alias for ParseExpression (both do program-level parsing)
AstNode* Parser_ParseProgram(void) {
    return Parser_ParseExpression();
}
