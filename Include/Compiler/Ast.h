#ifndef VANARIZE_COMPILER_AST_H
#define VANARIZE_COMPILER_AST_H

#include <stdint.h>
#include "Compiler/Token.h"

typedef enum {
    NODE_BINARY_EXPR,
    NODE_LITERAL_EXPR,
    NODE_VAR_DECL,
    NODE_FUNCTION_DECL,
    NODE_BLOCK,
    NODE_RETURN_STMT
} NodeType;

typedef struct AstNode AstNode;

struct AstNode {
    NodeType type;
};

// Expressions
typedef struct {
    AstNode main;
    AstNode* left;
    AstNode* right;
    Token op;
} BinaryExpr;

typedef struct {
    AstNode main;
    Token token; // For Number or String
} LiteralExpr;

// Statements
typedef struct {
    AstNode main;
    AstNode** statements;
    int count;
} BlockStmt;

typedef struct {
    AstNode main;
    Token name;
    Token typeName; // e.g. "number"
    AstNode* initializer;
} VarDecl;

typedef struct {
    AstNode main;
    AstNode* returnValue;
} ReturnStmt;

#endif // VANARIZE_COMPILER_AST_H
