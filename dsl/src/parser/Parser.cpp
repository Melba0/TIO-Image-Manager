#include "Parser.h"
#include <stdexcept>
#include <iostream>

Parser::Parser(Lexer& lexer) : lexer_(lexer) {}

Token Parser::peek() { return lexer_.peek(); }
Token Parser::consume() { return lexer_.consume(); }

Token Parser::consume(TokenKind expected) {
    Token t = lexer_.consume();
    if (t.type != expected) {
        error("Expected " + std::to_string((int)expected) + " but got " + t.text);
    }
    return t;
}

bool Parser::match(TokenKind type) {
    if (peek().type == type) {
        consume();
        return true;
    }
    return false;
}

void Parser::error(const std::string& msg) {
    had_error_ = true;
    throw std::runtime_error("Parse error: " + msg);
}

std::unique_ptr<Program> Parser::parseProgram() {
    auto program = std::make_unique<Program>();
    while (peek().type != TokenKind::EndOfFile) {
        try {
            program->statements.push_back(parseStatement());
            // allow `;` as a statement separator (e.g. `macro m(x)=...; out = ...`)
            while (peek().type == TokenKind::Semicolon) {
                consume();
            }
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            // Skip to end of the current statement
            while (peek().type != TokenKind::EndOfFile && peek().type != TokenKind::Semicolon) {
                consume();
            }
        }
    }
    return program;
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    // macro definition
    if (peek().type == TokenKind::Macro) {
        return parseMacroDef();
    }

    // `del <target>` : delete images (top-level only).
    if (peek().type == TokenKind::Del) {
        consume();
        auto target = parseExpression();
        return std::make_unique<DelStmt>(std::move(target));
    }

    // Check if it's an assignment: identifier followed by =
    if (peek().type == TokenKind::Identifier) {
        // Look ahead to see if next token is =
        auto saved = lexer_;
        Token id = consume();
        if (peek().type == TokenKind::Assign) {
            consume(); // consume =
            auto value = parseExpression();
            return std::make_unique<AssignStmt>(id.text, std::move(value));
        }
        // Not an assignment, backtrack
        lexer_ = saved;
    }

    auto expr = parseExpression();
    return std::make_unique<ExprStmt>(std::move(expr));
}

std::unique_ptr<ASTNode> Parser::parseMacroDef() {
    consume(TokenKind::Macro);
    Token name = consume(TokenKind::Identifier);
    consume(TokenKind::LParen);

    std::vector<std::string> params;
    if (peek().type != TokenKind::RParen) {
        params.push_back(consume(TokenKind::Identifier).text);
        while (peek().type == TokenKind::Comma) {
            consume();
            params.push_back(consume(TokenKind::Identifier).text);
        }
    }
    consume(TokenKind::RParen);
    consume(TokenKind::Assign);

    auto body = parseExpression();
    auto stmt = std::make_unique<MacroDefStmt>();
    stmt->name = name.text;
    stmt->params = std::move(params);
    stmt->body = std::move(body);
    return stmt;
}

std::unique_ptr<Expr> Parser::parseExpression() {
    return parseExpand();
}

// `left >> ext_name` (lowest precedence, applied to a whole expression)
std::unique_ptr<Expr> Parser::parseExpand() {
    auto left = parseQuantifier();
    while (peek().type == TokenKind::Expand) {
        consume();
        Token t = peek();
        std::string ext;
        if (t.type == TokenKind::Identifier || t.type == TokenKind::String) {
            consume();
            ext = t.text;
        } else {
            error("Expected extension name after '>>'");
        }
        left = std::make_unique<ExpandExpr>(std::move(left), ext);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseQuantifier() {
    auto left = parseSetExpr();
    while (peek().type == TokenKind::Any || peek().type == TokenKind::All) {
        Token t = consume();
        Quantifier q = (t.type == TokenKind::Any) ? Quantifier::Any : Quantifier::All;

        std::unique_ptr<Expr> condition;
        if (peek().type == TokenKind::LParen) {
            consume(TokenKind::LParen);
            condition = parseExpression();
            consume(TokenKind::RParen);
        } else {
            // shorthand: `any flower`  ==  `any (class == "flower")`
            Token ct = peek();
            if (ct.type == TokenKind::Identifier || ct.type == TokenKind::String) {
                consume();
                condition = std::make_unique<BinaryExpr>(
                    std::make_unique<IdentExpr>("class"), BinOp::EQ,
                    std::make_unique<StringExpr>(ct.text));
            } else {
                error("Expected '(' or a class name after quantifier");
            }
        }
        left = std::make_unique<QuantifierExpr>(std::move(left), q, std::move(condition));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseSetExpr() {
    auto left = parseLogicalOr();
    while (true) {
        // `imgs : (condition)` : filter an image set by an image-level condition.
        if (peek().type == TokenKind::Colon) {
            consume();
            auto cond = parseExpression();
            left = std::make_unique<FilterExpr>(std::move(left), std::move(cond));
            continue;
        }
        if (peek().type != TokenKind::Pipe && peek().type != TokenKind::Amp &&
            peek().type != TokenKind::Minus) {
            break;
        }
        Token t = consume();
        auto right = parseLogicalOr();
        BinOp op;
        switch (t.type) {
            case TokenKind::Pipe:  op = BinOp::SetUnion; break;
            case TokenKind::Amp:   op = BinOp::SetIntersect; break;
            case TokenKind::Minus: op = BinOp::SetDiff; break;
            default: error("Invalid set operator");
        }
        left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto left = parseLogicalAnd();
    while (peek().type == TokenKind::Or) {
        consume();
        auto right = parseLogicalAnd();
        left = std::make_unique<BinaryExpr>(std::move(left), BinOp::Or, std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto left = parseComparison();
    while (peek().type == TokenKind::And) {
        consume();
        auto right = parseComparison();
        left = std::make_unique<BinaryExpr>(std::move(left), BinOp::And, std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseComparison() {
    auto left = parseAdditive();
    while (peek().type == TokenKind::GT || peek().type == TokenKind::LT ||
           peek().type == TokenKind::GE || peek().type == TokenKind::LE ||
           peek().type == TokenKind::EQ || peek().type == TokenKind::NE) {
        Token t = consume();
        auto right = parseAdditive();
        BinOp op;
        switch (t.type) {
            case TokenKind::GT: op = BinOp::GT; break;
            case TokenKind::LT: op = BinOp::LT; break;
            case TokenKind::GE: op = BinOp::GE; break;
            case TokenKind::LE: op = BinOp::LE; break;
            case TokenKind::EQ: op = BinOp::EQ; break;
            case TokenKind::NE: op = BinOp::NE; break;
            default: error("Invalid comparison operator");
        }
        left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAdditive() {
    auto left = parseMultiplicative();
    while (peek().type == TokenKind::Plus || peek().type == TokenKind::Minus) {
        Token t = consume();
        auto right = parseMultiplicative();
        BinOp op = (t.type == TokenKind::Plus) ? BinOp::Add : BinOp::Sub;
        left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseMultiplicative() {
    auto left = parseUnary();
    while (peek().type == TokenKind::Star || peek().type == TokenKind::Slash) {
        Token t = consume();
        auto right = parseUnary();
        BinOp op = (t.type == TokenKind::Star) ? BinOp::Mul : BinOp::Div;
        left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (peek().type == TokenKind::Not) {
        consume();
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::Not, std::move(operand));
    }
    if (peek().type == TokenKind::Minus) {
        consume();
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::Negate, std::move(operand));
    }
    if (peek().type == TokenKind::Percent) {
        consume();
        // % takes a quantifier expression as operand (allows any/all inside)
        auto operand = parseQuantifier();
        return std::make_unique<UnaryExpr>(UnaryOp::Percent, std::move(operand));
    }
    if (peek().type == TokenKind::Caret) {
        consume();
        auto operand = parseQuantifier();
        return std::make_unique<CaretExpr>(std::move(operand));
    }
    return parsePostfix();
}

// Postfix property access: `expr.property` (chained: `obj.attr.h`).
std::unique_ptr<Expr> Parser::parsePostfix() {
    auto left = parsePrimary();
    while (peek().type == TokenKind::Dot) {
        consume();
        Token p = consume(TokenKind::Identifier);
        left = std::make_unique<PropertyAccessExpr>(std::move(left), p.text);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    Token t = peek();

    if (t.type == TokenKind::Dollar) {
        consume();
        // `$ (condition)` == `$ any (condition)` (implicit any)
        if (peek().type == TokenKind::LParen) {
            consume(TokenKind::LParen);
            auto condition = parseExpression();
            consume(TokenKind::RParen);
            return std::make_unique<QuantifierExpr>(
                std::make_unique<DollarExpr>(), Quantifier::Any, std::move(condition));
        }
        return std::make_unique<DollarExpr>();
    }

    if (t.type == TokenKind::Obj) {
        consume();
        if (peek().type == TokenKind::Any || peek().type == TokenKind::All) {
            return std::make_unique<CurrentImageObjectsExpr>();
        }
        return std::make_unique<CurrentObjectExpr>();
    }

    // `any(condition)` / `all(condition)`: object-level predicates used inside
    // a filter condition (e.g. `$ : (any(class == "cat"))`).
    if (t.type == TokenKind::Any || t.type == TokenKind::All) {
        consume();
        Quantifier q = (t.type == TokenKind::Any) ? Quantifier::Any : Quantifier::All;
        consume(TokenKind::LParen);
        auto cond = parseExpression();
        consume(TokenKind::RParen);
        return std::make_unique<AnyAllExpr>(q, std::move(cond));
    }

    if (t.type == TokenKind::Number) {
        consume();
        return std::make_unique<NumberExpr>(t.num_val);
    }

    if (t.type == TokenKind::String) {
        consume();
        return std::make_unique<StringExpr>(t.text);
    }

    if (t.type == TokenKind::Identifier) {
        consume();
        // cnt(class) counting function
        if (t.text == "cnt" && peek().type == TokenKind::LParen) {
            consume(TokenKind::LParen);
            Token ct = peek();
            if (ct.type == TokenKind::Identifier || ct.type == TokenKind::String) {
                consume();
                consume(TokenKind::RParen);
                return std::make_unique<CntExpr>(ct.text);
            }
            error("Expected a class name inside cnt(...)");
        }
        // macro call: name(arg1, arg2, ...)
        if (peek().type == TokenKind::LParen) {
            consume(TokenKind::LParen);
            std::vector<std::unique_ptr<Expr>> args;
            if (peek().type != TokenKind::RParen) {
                args.push_back(parseExpression());
                while (peek().type == TokenKind::Comma) {
                    consume();
                    args.push_back(parseExpression());
                }
            }
            consume(TokenKind::RParen);
            return std::make_unique<MacroCallExpr>(t.text, std::move(args));
        }
        return std::make_unique<IdentExpr>(t.text);
    }

    if (t.type == TokenKind::LParen) {
        consume();
        auto expr = parseExpression();
        consume(TokenKind::RParen);
        return expr;
    }

    error("Unexpected token: " + t.text + " (type: " + std::to_string((int)t.type) + ")");
    return nullptr;
}