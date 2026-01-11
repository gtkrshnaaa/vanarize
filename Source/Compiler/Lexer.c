#include "Compiler/Lexer.h"
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>

typedef struct {
    const char* start;
    const char* current;
    int line;
} LexerState;

static LexerState lexer;

void Lexer_Init(const char* source) {
    lexer.start = source;
    lexer.current = source;
    lexer.line = 1;
}

static bool isAtEnd() {
    return *lexer.current == '\0';
}

static char advance() {
    lexer.current++;
    return lexer.current[-1];
}

static char peek() {
    return *lexer.current;
}

static char peekNext() {
    if (isAtEnd()) return '\0';
    return lexer.current[1];
}

static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*lexer.current != expected) return false;
    lexer.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer.start;
    token.length = (int)(lexer.current - lexer.start);
    token.line = lexer.line;
    return token;
}

static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = lexer.line;
    return token;
}

static void skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                lexer.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    if (lexer.current - lexer.start == start + length &&
        memcmp(lexer.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    switch (lexer.start[0]) {
        case 'a': 
            if (lexer.current - lexer.start > 1) {
                switch(lexer.start[1]) {
                    case 'n': return checkKeyword(2, 1, "d", TOKEN_AND);
                    case 's': return checkKeyword(2, 3, "ync", TOKEN_ASYNC);
                    case 'w': return checkKeyword(2, 3, "ait", TOKEN_AWAIT);
                }
            }
            break;
        case 'b': 
            if (lexer.current - lexer.start > 1) {
                switch(lexer.start[1]) {
                    case 'o': return checkKeyword(2, 5, "olean", TOKEN_TYPE_BOOLEAN);
                    case 'y': return checkKeyword(2, 2, "te", TOKEN_TYPE_BYTE);
                }
            }
            break;
        case 'c': 
            if (lexer.current - lexer.start > 1) {
                switch(lexer.start[1]) {
                    case 'h': return checkKeyword(2, 2, "ar", TOKEN_TYPE_CHAR);
                    case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                }
            }
            break;
        case 'd': return checkKeyword(1, 5, "ouble", TOKEN_TYPE_DOUBLE);
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 6, "nction", TOKEN_FUNCTION);
                    case 'l': return checkKeyword(2, 3, "oat", TOKEN_TYPE_FLOAT);
                }
            }
            break;
        case 'i': 
            if (lexer.current - lexer.start > 1) {
                switch(lexer.start[1]) {
                    case 'f': return TOKEN_IF;
                    case 'm': return checkKeyword(2, 4, "port", TOKEN_IMPORT);
                    case 'n': return checkKeyword(2, 1, "t", TOKEN_TYPE_INT);
                }
            }
            break;
        case 'l': return checkKeyword(1, 3, "ong", TOKEN_TYPE_LONG);
        case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's':
            if (lexer.current - lexer.start > 1) {
                switch(lexer.start[1]) {
                    case 'u': return checkKeyword(2, 3, "per", TOKEN_SUPER);
                    case 't': 
                        if (lexer.current - lexer.start > 3) {
                            if (lexer.start[3] == 'i') return checkKeyword(4, 2, "ng", TOKEN_TYPE_STRING);
                            if (lexer.start[3] == 'u') return checkKeyword(4, 2, "ct", TOKEN_STRUCT);
                        }
                        break;
                    case 'h': return checkKeyword(2, 3, "ort", TOKEN_TYPE_SHORT);
                }
            }
            break;
        case 't':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR); // Legacy/Alternative
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    while (isalnum(peek())) advance();
    return makeToken(identifierType());
}

static Token number() {
    while (isdigit(peek())) advance();

    if (peek() == '.' && isdigit(peekNext())) {
        advance();
        while (isdigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}

static Token string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') lexer.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");
    advance();
    return makeToken(TOKEN_STRING);
}

Token Lexer_NextToken(void) {
    skipWhitespace();
    lexer.start = lexer.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();

    if (isalpha(c)) return identifier();
    if (isdigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);
        case ':': 
            return match(':') ? makeToken(TOKEN_DOUBLE_COLON) : makeToken(TOKEN_COLON);
        case '!':
            return match('=') ? makeToken(TOKEN_BANG_EQUAL) : makeToken(TOKEN_BANG);
        case '=':
            return match('=') ? makeToken(TOKEN_EQUAL_EQUAL) : makeToken(TOKEN_EQUAL);
        case '<':
            return match('=') ? makeToken(TOKEN_LESS_EQUAL) : makeToken(TOKEN_LESS);
        case '>':
            return match('=') ? makeToken(TOKEN_GREATER_EQUAL) : makeToken(TOKEN_GREATER);
        case '"': return string();
    }

    return errorToken("Unexpected character.");
}
