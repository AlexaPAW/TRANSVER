#include "lexer.hpp"
#include "dfa_table.hpp"
#include <algorithm>
#include <array>
#include <sstream>

namespace funny {
namespace {

    constexpr int kAlphabetSize = 128;

    const std::array<std::string_view, 48> kTokenNames = {{
        "BAD_INT", "WS", "COMMENT", "EQEQ", "NE", "LE", "GE", "ARROW", "FATARROW",
        "KW_function", "KW_returns", "KW_requires", "KW_ensures", "KW_uses", "KW_if",
        "KW_else", "KW_while", "KW_invariant", "KW_assert", "KW_assume", "KW_forall",
        "KW_exists", "KW_true", "KW_false", "KW_not", "KW_and", "KW_or", "KW_int",
        "KW_length", "INT", "IDENT", "LPAREN", "RPAREN", "LBRACKET", "RBRACKET",
        "LBRACE", "RBRACE", "COMMA", "SEMICOLON", "COLON", "PIPE", "PLUS", "MINUS",
        "STAR", "SLASH", "LT", "GT", "ASSIGN"
    }};

    // для диагноистики
    std::string quoteForMessage(std::string_view text) {
        std::string out;
        out.reserve(text.size() + 2);
        out.push_back('"');
        for (char ch : text) {
            if (ch == '\\' || ch == '"') out.push_back('\\');
            if (ch == '\n') out += "\\n";
            else if (ch == '\r') out += "\\r";
            else if (ch == '\t') out += "\\t";
            else out.push_back(ch);
        }
        out.push_back('"');
        return out;
    }

}

std::string_view tokenKindName(TokenKind kind) {
    int id = static_cast<int>(kind);
    if (id >= 0 && id < static_cast<int>(kTokenNames.size())) return kTokenNames[id];
    if (kind == TokenKind::END) return "<EOF>";
    return "<UNKNOWN>";
}

// построить индекс начала строк
Lexer::Lexer(std::string source) : source_(std::move(source)) {
    buildLineStarts(source_, lineStarts_);
}

// смещение в байтах
void Lexer::buildLineStarts(const std::string& source, std::vector<std::size_t>& starts) {
    starts.clear();
    starts.push_back(0);
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') {
            starts.push_back(i + 1);
        } else if (source[i] == '\r' && (i + 1 == source.size() || source[i + 1] != '\n')) {
            starts.push_back(i + 1);
        }
    }
}

// номера строки + столба
SourcePos Lexer::positionAt(std::size_t offset) const {
    auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    std::size_t lineIndex = static_cast<std::size_t>(std::distance(lineStarts_.begin(), it) - 1);
    return SourcePos{offset, lineIndex + 1, offset - lineStarts_[lineIndex] + 1};
}

LexResult Lexer::scan() const {
    LexResult result;
    std::size_t pos = 0;

    while (pos < source_.size()) {
        int state = funny_dfa::kStartState;
        int lastAccept = -1;
        std::size_t lastPos = pos;
        std::size_t cursor = pos;

        while (cursor < source_.size()) {
            unsigned char byte = static_cast<unsigned char>(source_[cursor]);
            if (byte >= kAlphabetSize) break;
            int next = funny_dfa::kTransition[state][byte];
            if (next == funny_dfa::kTrapState) break;
            state = next;
            ++cursor;
            if (funny_dfa::kAccept[state] >= 0) {
                lastAccept = funny_dfa::kAccept[state];
                lastPos = cursor;
            }
        }

        if (lastAccept < 0) {
            const auto p = positionAt(pos);
            std::string offending = source_.substr(pos, 1);
            result.diagnostics.push_back({
                "lexer", p,
                "unexpected byte " + quoteForMessage(offending) +
                "; no token matches at this position"
            });
            ++pos; // сканироуются все ошибки, а не только первая попавшаяся
            continue;
        }

        std::string lexeme = source_.substr(pos, lastPos - pos);
        const TokenKind kind = static_cast<TokenKind>(lastAccept);
        const auto p = positionAt(pos);

        if (kind == TokenKind::BAD_INT) {
            result.diagnostics.push_back({
                "lexer", p,
                "invalid integer literal " + quoteForMessage(lexeme)
            });
        } else if (kind != TokenKind::WS && kind != TokenKind::COMMENT) {
            result.tokens.push_back({kind, std::move(lexeme), p});
        }

        pos = lastPos;
    }

    result.tokens.push_back({TokenKind::END, {}, positionAt(source_.size())});
    return result;
}

}
