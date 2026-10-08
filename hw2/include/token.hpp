#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace funny {

enum class TokenKind : int {
    BAD_INT = 0,
    WS = 1,
    COMMENT = 2,
    EQEQ = 3,
    NE = 4,
    LE = 5,
    GE = 6,
    ARROW = 7,
    FATARROW = 8,
    KW_function = 9,
    KW_returns = 10,
    KW_requires = 11,
    KW_ensures = 12,
    KW_uses = 13,
    KW_if = 14,
    KW_else = 15,
    KW_while = 16,
    KW_invariant = 17,
    KW_assert = 18,
    KW_assume = 19,
    KW_forall = 20,
    KW_exists = 21,
    KW_true = 22,
    KW_false = 23,
    KW_not = 24,
    KW_and = 25,
    KW_or = 26,
    KW_int = 27,
    KW_length = 28,
    INT = 29,
    IDENT = 30,
    LPAREN = 31,
    RPAREN = 32,
    LBRACKET = 33,
    RBRACKET = 34,
    LBRACE = 35,
    RBRACE = 36,
    COMMA = 37,
    SEMICOLON = 38,
    COLON = 39,
    PIPE = 40,
    PLUS = 41,
    MINUS = 42,
    STAR = 43,
    SLASH = 44,
    LT = 45,
    GT = 46,
    ASSIGN = 47,
    END = 1000
};

struct SourcePos {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

// для ошибок
struct Diagnostic {
    std::string phase;   // lexer | parser
    SourcePos pos;
    std::string message;
};

struct Token {
    TokenKind kind = TokenKind::END;
    std::string lexeme;
    SourcePos pos;
};

std::string_view tokenKindName(TokenKind kind);

} // namespace funny
