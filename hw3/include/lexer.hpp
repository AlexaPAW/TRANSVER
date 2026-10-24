#pragma once
#include "token.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace funny {

// результат разбора 1 строки
struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

class Lexer {
public:
    explicit Lexer(std::string source);
    LexResult scan() const;

private:
    std::string source_;
    std::vector<std::size_t> lineStarts_;

    // считает смещения каждой строки
    SourcePos positionAt(std::size_t offset) const;
    static void buildLineStarts(const std::string& source, std::vector<std::size_t>& starts);
};

} // namespace funny
