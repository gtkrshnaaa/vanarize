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
            expr = (AstNode*)node;
        } else {
            break;
        }
    }
    return expr;
}

static AstNode* unary() {
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
        
        if (increment != NULL) {
            // Create block for body + increment
            BlockStmt* block = malloc(sizeof(BlockStmt));
            block->main.type = NODE_BLOCK;
            block->statements = malloc(sizeof(AstNode*) * 2);
            block->statements[0] = body;
            // Increment is an expression, wrap in plain evaluation??
            // JIT emitNode handles expression by evaluating. 
            // In Block we expect Statements. ExpressionStmt handles expression + pop?
            // JIT for Expression just evaluates.
            // Let's assume Expression is a valid Statement in our specific JIT/AST flavor 
            // OR wrap it in ExpressionStmt if we had one explicitly distinct?
            // Ast just says Node. JIT emits it.
            // If increment returns value, it stays on stack?
            // JIT emitNode for Block: loops through statements.
            // If statement leaves value, stack grows. 
            // We need to POP if it's an expression.
            // But Parser desugaring usually makes it an ExpressionStmt?
            // My Parser doesn't seem to have explicit ExpressionStmt Node wrapping Expression.
            // statement() -> expression()-> consumes semicolon -> returns Expression Node?
            // Check return of statement() for ExpressionStmt. 
            // See Line 468 in view: statement() returns expression() result directly?
            // I need to verify if statement() wraps expression.
            // Wait, statement() calls expressionStatement()?
            
            block->statements[1] = increment;
            block->count = 2;
            body = (AstNode*)block;
        }
        
        if (condition == NULL) {
             // while(true)
             LiteralExpr* trueLit = malloc(sizeof(LiteralExpr));
             trueLit->main.type = NODE_LITERAL_EXPR; 
             // We need a TOKEN_TRUE
             Token t; t.type = TOKEN_TRUE; t.start="true"; t.length=4; t.line=0;
             trueLit->token = t;
             condition = (AstNode*)trueLit;
        }
        
        WhileStmt* whileStmt = malloc(sizeof(WhileStmt));
        whileStmt->main.type = NODE_WHILE_STMT;
        whileStmt->condition = condition;
        whileStmt->body = body;
        
        if (initializer != NULL) {
             BlockStmt* outer = malloc(sizeof(BlockStmt));
             outer->main.type = NODE_BLOCK;
             outer->statements = malloc(sizeof(AstNode*) * 2);
             outer->statements[0] = initializer;
             outer->statements[1] = (AstNode*)whileStmt;
             outer->count = 2;
             return (AstNode*)outer;
        }
        
        return (AstNode*)whileStmt;
    }

    if (currentToken.type == TOKEN_WHILE) {
        // User Ban: While loops should be removed from Examples, but Parser MUST support them 
        // because we desugar 'for' into 'while'. 
        // So JIT/Parser needs 'while' support.
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
