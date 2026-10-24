#pragma once
#include "ast.hpp"
#include "token.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace funny {

// полный результат работы парсера
struct ParseResult {
    std::shared_ptr<ast::Program> program;
    std::vector<Diagnostic> diagnostics;
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    ParseResult parse();

private:
    const std::vector<Token>& tokens_;
    std::vector<std::size_t> matchingParen_;
    std::size_t index_ = 0;
    std::vector<Diagnostic> diagnostics_;

    struct Failure {
        std::string message;
    };

    const Token& current() const;
    const Token& lookahead(std::size_t n) const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token& expect(TokenKind kind, std::string_view what);
    [[noreturn]] void errorHere(const std::string& message);

    std::shared_ptr<ast::Declaration> parseDeclaration();
    std::shared_ptr<ast::FunctionDecl> parseFunction();
    std::shared_ptr<ast::FormulaDecl> parseFormula();

    std::vector<ast::VariableDef> parseVariableDefs(TokenKind closing);
    ast::VariableDef parseVariableDef();
    ast::LocalVarDef parseLocalVarDef();
    std::string parseType();

    ast::StatementPtr parseStatement();
    ast::StatementPtr parseBlock();
    ast::StatementPtr parseAssignment();
    ast::StatementPtr parseIf();
    ast::StatementPtr parseWhile();
    ast::StatementPtr parseAssert();
    ast::StatementPtr parseAssume();

    ast::ExprPtr parseExpr();
    ast::ExprPtr parseAdd();
    ast::ExprPtr parseMul();
    ast::ExprPtr parseUnary();
    ast::ExprPtr parsePrimary();
    std::vector<ast::ExprPtr> parseArguments();
    std::vector<ast::ExprPtr> parseArraySuffixes();

    ast::PredicatePtr parseCondition();
    ast::PredicatePtr parseImplication();
    ast::PredicatePtr parseOr();
    ast::PredicatePtr parseAnd();
    ast::PredicatePtr parseConditionUnary();
    ast::PredicatePtr parseConditionAtom();
    ast::PredicatePtr parseComparison();
    ast::PredicatePtr parseComparisonFromLeft(ast::ExprPtr left);

    ast::PredicatePtr parsePredicate();
    ast::PredicatePtr parsePredicateImplies();
    ast::PredicatePtr parsePredicateOr();
    ast::PredicatePtr parsePredicateAnd();
    ast::PredicatePtr parsePredicateUnary();
    ast::PredicatePtr parsePredicateAtom();
    ast::PredicatePtr parseQuantifier();
    ast::PredicatePtr parseFormulaRefOrComparison();

    bool isComparisonOperator(TokenKind kind) const;
    bool parenthesizedValueIsComparisonLeftOperand() const;

    void synchronizeTopLevel(std::size_t startIndex);
    void synchronizeStatement(std::size_t startIndex);
};

} // namespace funny
