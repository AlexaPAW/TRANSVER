#pragma once
#include "ast.hpp"
#include "token.hpp"
#include <string>
#include <vector>

namespace funny {

struct SemanticResult {
    std::vector<Diagnostic> diagnostics;
};

// Выполняет проверку имён и типов и аннотирует Expr/Predicate узлы в AST.
class SemanticAnalyzer {
public:
    SemanticResult analyze(ast::Program& program);
};

} // namespace funny
