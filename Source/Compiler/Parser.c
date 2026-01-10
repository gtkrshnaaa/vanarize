#include "Compiler/Parser.h"
#include "Compiler/Lexer.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static Token currentToken;
static Token previousToken;

static void advance() {
    previousToken = currentToken;
    for (;;) {
        currentToken = Lexer_NextToken();
        if (currentToken.type != TOKEN_ERROR) break;
        // errorAtCurrent(currentToken.start);
        fprintf(stderr, "Lexer error: %.*s\n", currentToken.length, currentToken.start);
        exit(1);
    }
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
    advance(); // Prime the pump
}

// Forward decls
static AstNode* expression();
static AstNode* term();
static AstNode* factor();
static AstNode* primary();

static AstNode* expression() {
    return term();
}

static AstNode* term() {
    AstNode* expr = factor();
    
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
            // node->callee = ... wait, expr must be identifier for now
            // Simplified: We assume current expr is an identifier.
            // But `expr` is an AstNode. We can't easily extract the token from generic node without casting.
            // Let's assume for this stage: calls only on identifiers.
            // Check if expr is a variable usage (which we haven't implemented yet, but primary parses identifiers?)
            // Actually primary() didn't parse identifiers in previous step.
            
            // We need to store arguments.
            node->args = malloc(sizeof(AstNode*) * 8); // Max 8 args for now
            node->argCount = 0;
            
            if (currentToken.type != TOKEN_RIGHT_PAREN) {
                do {
                    node->args[node->argCount++] = expression();
                } while (currentToken.type == TOKEN_COMMA && (advance(), 1)); // Cute comma skip
            }
            
            consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
            
            // Hack for callee name since we don't have Var access yet
            // If expr was NODE_LITERAL_EXPR (but type identifier?), wait primary handles literal.
            // We need primary to handle identifiers.
            node->callee = ((LiteralExpr*)expr)->token; // Unsafe cast, need primary update
            
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

static AstNode* declaration() {
    if (currentToken.type == TOKEN_VAR) {
        advance();
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        Token name = previousToken;
        
        AstNode* initializer = NULL;
        if (currentToken.type == TOKEN_EQUAL) {
            advance();
            initializer = expression();
        }
        
        consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
        
        VarDecl* node = malloc(sizeof(VarDecl));
        node->main.type = NODE_VAR_DECL;
        node->name = name;
        node->initializer = initializer;
        return (AstNode*)node;
    }
    
    // Fallback to statement -> expression statement
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
