#include "Lexer.h"
#include <cctype>
#include <sstream>

Lexer::Lexer(const std::string& source) : source_(source) {}

Token Lexer::makeToken(TokenKind type, const std::string& text) {
    Token t;
    t.type = type;
    t.text = text;
    // Operators/delimiters are created with empty text; fill in the consumed
    // character so e.g. `$` shows as "$".  String literals may legitimately be
    // empty (`""`), so never substitute for them.
    if (t.text.empty() && type != TokenKind::String && pos_ > 0) {
        t.text = std::string(1, source_[pos_ - 1]);
    }
    t.line = line_;
    t.col = col_ - (int)t.text.size();
    return t;
}

void Lexer::skipWhitespaceAndComments() {
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (c == ' ' || c == '\t' || c == '\r') {
            pos_++; col_++;
        } else if (c == '\n') {
            pos_++; line_++; col_ = 1;
        } else if (c == '#') {
            while (pos_ < source_.size() && source_[pos_] != '\n') pos_++;
        } else {
            break;
        }
    }
}

Token Lexer::readNumber() {
    size_t start = pos_;
    col_++;
    pos_++;
    while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '.')) {
        if (source_[pos_] == '.') col_++;
        else col_++;
        pos_++;
    }
    std::string text = source_.substr(start, pos_ - start);
    Token t = makeToken(TokenKind::Number, text);
    t.num_val = std::stod(text);
    return t;
}

Token Lexer::readString() {
    pos_++; col_++; // skip opening quote
    size_t start = pos_;
    while (pos_ < source_.size() && source_[pos_] != '"') {
        if (source_[pos_] == '\\') {
            pos_++; col_++;
        }
        pos_++; col_++;
    }
    std::string text = source_.substr(start, pos_ - start);
    if (pos_ < source_.size()) {
        pos_++; col_++; // skip closing quote
    }
    Token t = makeToken(TokenKind::String, text);
    return t;
}

Token Lexer::readIdentifierOrKeyword() {
    size_t start = pos_;
    col_++;
    pos_++;
    while (pos_ < source_.size()) {
        unsigned char c = (unsigned char)source_[pos_];
        if (std::isalnum(c) || c == '_' || c >= 0x80) {
            // allow ASCII alnum/_ plus UTF-8 multibyte bytes (Chinese class names)
            col_++;
            pos_++;
        } else {
            break;
        }
    }
    std::string text = source_.substr(start, pos_ - start);

    TokenKind type = TokenKind::Identifier;
    if (text == "any") type = TokenKind::Any;
    else if (text == "all") type = TokenKind::All;
    else if (text == "macro") type = TokenKind::Macro;
    else if (text == "obj") type = TokenKind::Obj;
    else if (text == "del") type = TokenKind::Del;

    return makeToken(type, text);
}

Token Lexer::peek() {
    if (!has_peeked_) {
        peeked_ = consume();
        has_peeked_ = true;
    }
    return peeked_;
}

Token Lexer::consume() {
    if (has_peeked_) {
        has_peeked_ = false;
        return peeked_;
    }

    skipWhitespaceAndComments();

    if (pos_ >= source_.size()) {
        return makeToken(TokenKind::EndOfFile);
    }

    char c = source_[pos_];

    if (std::isdigit(c)) {
        return readNumber();
    }

    if (c == '"') {
        return readString();
    }

    if (std::isalpha(c) || c == '_' || ((unsigned char)c) >= 0x80) {
        return readIdentifierOrKeyword();
    }

    pos_++; col_++;

    switch (c) {
        case '$': return makeToken(TokenKind::Dollar);
        case '%': return makeToken(TokenKind::Percent);
        case '^': return makeToken(TokenKind::Caret);
        case '=':
            if (pos_ < source_.size() && source_[pos_] == '=') {
                pos_++; col_++;
                return makeToken(TokenKind::EQ);
            }
            return makeToken(TokenKind::Assign);
        case '+': return makeToken(TokenKind::Plus);
        case '-': return makeToken(TokenKind::Minus);
        case '*': return makeToken(TokenKind::Star);
        case '/': return makeToken(TokenKind::Slash);
        case '(': return makeToken(TokenKind::LParen);
        case ')': return makeToken(TokenKind::RParen);
        case ',': return makeToken(TokenKind::Comma);
        case '.': return makeToken(TokenKind::Dot);
        case ':': return makeToken(TokenKind::Colon);
        case ';': return makeToken(TokenKind::Semicolon);
        case '&':
            if (pos_ < source_.size() && source_[pos_] == '&') {
                pos_++; col_++;
                return makeToken(TokenKind::And);
            }
            return makeToken(TokenKind::Amp);
        case '!':
            if (pos_ < source_.size() && source_[pos_] == '=') {
                pos_++; col_++;
                return makeToken(TokenKind::NE);
            }
            return makeToken(TokenKind::Not);
        case '>':
            if (pos_ < source_.size() && source_[pos_] == '>') {
                pos_++; col_++;
                return makeToken(TokenKind::Expand);
            }
            if (pos_ < source_.size() && source_[pos_] == '=') {
                pos_++; col_++;
                return makeToken(TokenKind::GE);
            }
            return makeToken(TokenKind::GT);
        case '<':
            if (pos_ < source_.size() && source_[pos_] == '=') {
                pos_++; col_++;
                return makeToken(TokenKind::LE);
            }
            return makeToken(TokenKind::LT);
        case '|':
            if (pos_ < source_.size() && source_[pos_] == '|') {
                pos_++; col_++;
                return makeToken(TokenKind::Or);
            }
            return makeToken(TokenKind::Pipe);
        default: {
            Token t = makeToken(TokenKind::Error);
            error_msg_ = "Unexpected character: " + std::string(1, c);
            return t;
        }
    }
}

bool Lexer::isAtEnd() const {
    return pos_ >= source_.size() && !has_peeked_;
}