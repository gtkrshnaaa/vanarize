#ifndef VANARIZE_COMPILER_AST_H
#define VANARIZE_COMPILER_AST_H

#include <stdint.h>
#include "Compiler/Token.h"

typedef enum {
    NODE_BINARY_EXPR,
    NODE_LITERAL_EXPR,
    NODE_STRING_LITERAL,
    NODE_CALL_EXPR,
    NODE_VAR_DECL,
    NODE_ASSIGNMENT_EXPR,
    NODE_FUNCTION_DECL,
    NODE_BLOCK,
    NODE_RETURN_STMT,
    NODE_IF_STMT,

    NODE_WHILE_STMT,
    NODE_STRUCT_DECL,
    NODE_STRUCT_INIT,
    NODE_GET_EXPR, // obj.field
    NODE_UNARY_EXPR
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

typedef struct {
    AstNode main;
    Token op;
    AstNode* right;
} UnaryExpr;

typedef struct {
    AstNode main;
    Token token;
    // We will evaluate the string value at parse time or compile time?
    // Lexer gives a pointer to source.
    // For now, AST holds the Token. Compiler will convert to ObjString.
} StringExpr;

typedef struct {
    AstNode main;
    Token name;
    Token typeName; // e.g. "number", "SensorData" or empty (nested check)
    AstNode* initializer;
} VarDecl;

typedef struct {
    AstNode main;
    Token name;
    AstNode* value;
} AssignmentExpr;

typedef struct {
    AstNode main;
    AstNode* callee; // Changed from Token to support namespace.method
    AstNode** args;
    int argCount;
} CallExpr;

// Statements
typedef struct {
    AstNode main;
    AstNode** statements;
    int count;
} BlockStmt;



typedef struct {
    AstNode main;
    AstNode* condition;
    AstNode* thenBranch;
    AstNode* elseBranch;
} IfStmt;

typedef struct {
    AstNode main;
    AstNode* condition;
    AstNode* body;
} WhileStmt;

typedef struct {
    AstNode main;
    Token name;
    Token* params;
    Token* paramTypes; // Types for params
    int paramCount;
    Token returnType; // :: Type
    AstNode* body;
} FunctionDecl;

// Structs
typedef struct {
    AstNode main;
    Token name;
    Token* fields;     // Array of field names
    Token* fieldTypes; // Array of field types (Tokens)
    int fieldCount;
} StructDecl;

typedef struct {
    AstNode main;
    Token structName;
    Token* fieldNames;
    AstNode** values; // Expressions
    int fieldCount;
} StructInit;

typedef struct {
    AstNode main;
    AstNode* object;
    Token name; // Property name
} GetExpr;

typedef struct {
    AstNode main;
    AstNode* returnValue;
} ReturnStmt;

#endif // VANARIZE_COMPILER_AST_H
