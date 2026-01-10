#include "Compiler/Parser.h"
#include "Compiler/Lexer.h"
#include <stdlib.h>
#include <stdio.h>

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

static AstNode* factor() {
    AstNode* expr = primary();
    
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
    
    // Grouping, etc. omitted for briefness
    fprintf(stderr, "[Parser] Expect expression.\n");
    exit(1);
    return NULL;
}

AstNode* Parser_ParseExpression(void) {
    return expression();
}
