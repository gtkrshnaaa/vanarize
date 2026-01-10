#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "Compiler/Lexer.h"
#include "Compiler/Parser.h"

void TestLexer() {
    printf("Testing Lexer...\n");
    
    const char* source = "number x = 10 + 20";
    Lexer_Init(source);
    
    Token t1 = Lexer_NextToken();
    assert(t1.type == TOKEN_TYPE_NUMBER);
    
    Token t2 = Lexer_NextToken();
    assert(t2.type == TOKEN_IDENTIFIER);
    assert(strncmp(t2.start, "x", 1) == 0);
    
    Token t3 = Lexer_NextToken();
    assert(t3.type == TOKEN_EQUAL);
    
    Token t4 = Lexer_NextToken();
    assert(t4.type == TOKEN_NUMBER);
    
    printf("Lexer OK.\n");
}

void TestParser() {
    printf("Testing Parser...\n");
    
    const char* source = "10 + 20 * 30";
    Parser_Init(source);
    AstNode* root = Parser_ParseExpression();
    
    // Structure should be Binary(+): Left=Lit(10), Right=Binary(*): Left=Lit(20), Right=Lit(30)
    assert(root->type == NODE_BINARY_EXPR);
    BinaryExpr* add = (BinaryExpr*)root;
    assert(add->op.type == TOKEN_PLUS);
    
    assert(add->left->type == NODE_LITERAL_EXPR); // 10
    
    assert(add->right->type == NODE_BINARY_EXPR); // 20 * 30
    BinaryExpr* mul = (BinaryExpr*)add->right;
    assert(mul->op.type == TOKEN_STAR);
    
    printf("Parser OK.\n");
}

int main() {
    TestLexer();
    TestParser();
    return 0;
}
