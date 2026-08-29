#pragma once
#include <string>
#include <vector>
#include <variant>

enum class TokenKind {
    // Special
    EndOfFile, Error,

    // Literals
    Number, String, Identifier,

    // Keywords
    Any, All, Macro, Obj, Del,

    // Operators
    Dollar, Percent, Caret, Assign,
    Plus, Minus, Star, Slash,
    GT, LT, GE, LE, EQ, NE,
    And, Or, Not,
    Pipe, Amp, Expand, Dot, Colon, // set union & intersection & `>>` & `.` & `:`
    Semicolon,

    // Delimiters
    LParen, RParen, Comma, Newline
};

struct Token {
    TokenKind type;
    std::string text;
    double num_val = 0;
    int line = 0;
    int col = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& source);

    Token peek();
    Token consume();
    bool isAtEnd() const;
    std::string getErrorMessage() const { return error_msg_; }

private:
    void skipWhitespaceAndComments();
    Token readNumber();
    Token readString();
    Token readIdentifierOrKeyword();
    Token makeToken(TokenKind type, const std::string& text = "");

    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    Token peeked_;
    bool has_peeked_ = false;
    std::string error_msg_;
};