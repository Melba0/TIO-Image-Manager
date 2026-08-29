#pragma once
#include <memory>
#include "AST.h"
#include "Lexer.h"

class Parser {
public:
    explicit Parser(Lexer& lexer);

    std::unique_ptr<Program> parseProgram();

private:
    // Statement parsing
    std::unique_ptr<ASTNode> parseStatement();
    std::unique_ptr<ASTNode> parseMacroDef();

    // Expression parsing (recursive descent)
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseExpand();
    std::unique_ptr<Expr> parseQuantifier();
    std::unique_ptr<Expr> parseSetExpr();
    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePrimary();

    // Helper
    Token peek();
    Token consume();
    Token consume(TokenKind expected);
    bool match(TokenKind type);
    void error(const std::string& msg);

    Lexer& lexer_;
    bool had_error_ = false;
};