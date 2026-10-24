#include "parser.hpp"
#include <charconv>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace funny {
namespace {

    using namespace ast;

    std::string tokenText(const Token& t) {
        if (t.kind == TokenKind::END) return "end of input";
        if (t.lexeme.empty()) return std::string(tokenKindName(t.kind));
        return std::string(tokenKindName(t.kind)) + " (" + t.lexeme + ")";
    }

    // фабрики:
    ExprPtr makeInt(SourcePos p, long long value) {
        auto e = std::make_shared<Expr>(); e->pos = p; e->kind = Expr::Kind::Int; e->intValue = value; return e;
    }
    ExprPtr makeVar(SourcePos p, std::string name) {
        auto e = std::make_shared<Expr>(); e->pos = p; e->kind = Expr::Kind::Variable; e->name = std::move(name); return e;
    }
    ExprPtr makeArray(SourcePos p, std::string name, std::vector<ExprPtr> indices) {
        auto e = std::make_shared<Expr>(); e->pos = p; e->kind = Expr::Kind::ArrayAccess; e->name = std::move(name); e->indices = std::move(indices); return e;
    }
    ExprPtr makeUnary(SourcePos p, std::string op, ExprPtr operand) {
        auto e = std::make_shared<Expr>(); e->pos = p; e->kind = Expr::Kind::Unary; e->op = std::move(op); e->operand = std::move(operand); return e;
    }
    ExprPtr makeBinary(SourcePos p, std::string op, ExprPtr left, ExprPtr right) {
        auto e = std::make_shared<Expr>(); e->pos = p; e->kind = Expr::Kind::Binary; e->op = std::move(op); e->left = std::move(left); e->right = std::move(right); return e;
    }
    ExprPtr makeCall(SourcePos p, std::string name, std::vector<ExprPtr> args) {
        auto e = std::make_shared<Expr>(); e->pos = p; e->kind = Expr::Kind::Call; e->name = std::move(name); e->args = std::move(args); return e;
    }
    PredicatePtr makeBool(SourcePos p, bool value) {
        auto n = std::make_shared<Predicate>(); n->pos = p; n->kind = Predicate::Kind::Bool; n->boolValue = value; return n;
    }
    PredicatePtr makeComparison(SourcePos p, std::string op, ExprPtr left, ExprPtr right) {
        auto n = std::make_shared<Predicate>(); n->pos = p; n->kind = Predicate::Kind::Comparison; n->op = std::move(op); n->left = std::move(left); n->right = std::move(right); return n;
    }
    PredicatePtr makeUnaryPred(SourcePos p, std::string op, PredicatePtr operand) {
        auto n = std::make_shared<Predicate>(); n->pos = p; n->kind = Predicate::Kind::Unary; n->op = std::move(op); n->operand = std::move(operand); return n;
    }
    PredicatePtr makeBinaryPred(SourcePos p, std::string op, PredicatePtr left, PredicatePtr right) {
        auto n = std::make_shared<Predicate>(); n->pos = p; n->kind = Predicate::Kind::Binary; n->op = std::move(op); n->predLeft = std::move(left); n->predRight = std::move(right); return n;
    }

}

// таблица соответствия: "(" -> ")" для однозначного разбора выражений в скобках
Parser::Parser(const std::vector<Token>& tokens)
    : tokens_(tokens), matchingParen_(tokens.size(), std::numeric_limits<std::size_t>::max()) {
    std::vector<std::size_t> stack;
    stack.reserve(tokens_.size());
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
        if (tokens_[i].kind == TokenKind::LPAREN) {
            stack.push_back(i);
        } else if (tokens_[i].kind == TokenKind::RPAREN && !stack.empty()) {
            const std::size_t open = stack.back();
            stack.pop_back();
            matchingParen_[open] = i;
        }
    }
}

const Token& Parser::current() const { return tokens_.at(index_); }
const Token& Parser::lookahead(std::size_t n) const {
    const std::size_t i = index_ + n;
    return tokens_.at(i < tokens_.size() ? i : tokens_.size() - 1);
}
bool Parser::check(TokenKind kind) const { return current().kind == kind; }
bool Parser::match(TokenKind kind) {
    if (!check(kind)) return false;
    ++index_;
    return true;
}

const Token& Parser::expect(TokenKind kind, std::string_view what) {
    if (!check(kind)) {
        errorHere("expected " + std::string(what) + ", got " + tokenText(current()));
    }
    const Token& t = current();
    ++index_;
    return t;
}

[[noreturn]] void Parser::errorHere(const std::string& message) {
    diagnostics_.push_back({"parser", current().pos, message});
    throw Failure{message};
}

// каждое объявление изолировано точкой, поэтому одно некорректное объявление не мешает попробовать разобрать следующие.
ParseResult Parser::parse() {
    auto program = std::make_shared<Program>();
    while (!check(TokenKind::END)) {
        const std::size_t start = index_;
        try {
            program->declarations.push_back(parseDeclaration());
        } catch (const Failure&) {
            synchronizeTopLevel(start);
        }
    }
    if (program->declarations.empty() && diagnostics_.empty()) {
        diagnostics_.push_back({"parser", current().pos, "expected at least one top-level declaration, got end of input"});
    }
    return {program, diagnostics_};
}

// выбор главного узла
std::shared_ptr<Declaration> Parser::parseDeclaration() {
    if (check(TokenKind::KW_function)) {
        auto d = std::make_shared<Declaration>();
        d->kind = Declaration::Kind::Function;
        d->pos = current().pos;
        d->function = parseFunction();
        return d;
    }
    if (check(TokenKind::IDENT)) {
        auto d = std::make_shared<Declaration>();
        d->kind = Declaration::Kind::Formula;
        d->pos = current().pos;
        d->formula = parseFormula();
        return d;
    }
    errorHere("expected 'function' or a formula declaration, got " + tokenText(current()));
}

// функция
std::shared_ptr<FunctionDecl> Parser::parseFunction() {
    const Token& kw = expect(TokenKind::KW_function, "'function'");
    const Token& name = expect(TokenKind::IDENT, "function name");
    auto f = std::make_shared<FunctionDecl>(); f->pos = kw.pos; f->name = name.lexeme;

    expect(TokenKind::LPAREN, "'('");
    f->params = parseVariableDefs(TokenKind::RPAREN);
    expect(TokenKind::RPAREN, "')'");

    if (match(TokenKind::KW_requires)) f->requiresClause = parsePredicate();
    else f->requiresClause = makeBool(kw.pos, true);

    expect(TokenKind::KW_returns, "'returns'");
    f->returns = parseVariableDefs(TokenKind::END); // returns are mandatory and non-empty
    if (f->returns.empty()) errorHere("expected at least one return variable after 'returns'");

    if (match(TokenKind::KW_ensures)) f->ensures = parsePredicate();
    else f->ensures = makeBool(kw.pos, false);
    if (match(TokenKind::KW_uses)) {
        f->uses.push_back(parseLocalVarDef());
        while (match(TokenKind::COMMA)) f->uses.push_back(parseLocalVarDef());
    }
    f->body = parseStatement();
    return f;
}

// формула
std::shared_ptr<FormulaDecl> Parser::parseFormula() {
    const Token& name = expect(TokenKind::IDENT, "formula name");
    auto f = std::make_shared<FormulaDecl>(); f->pos = name.pos; f->name = name.lexeme;
    expect(TokenKind::LPAREN, "'('");
    f->params = parseVariableDefs(TokenKind::RPAREN);
    expect(TokenKind::RPAREN, "')'");
    expect(TokenKind::FATARROW, "'=>'");
    f->body = parsePredicate();
    return f;
}

// разбор списка конструкций вида x: int ; y: int[]
std::vector<VariableDef> Parser::parseVariableDefs(TokenKind closing) {
    std::vector<VariableDef> result;
    if (check(closing)) return result;
    result.push_back(parseVariableDef());
    while (match(TokenKind::COMMA)) result.push_back(parseVariableDef());
    return result;
}

VariableDef Parser::parseVariableDef() {
    const Token& name = expect(TokenKind::IDENT, "identifier");
    expect(TokenKind::COLON, "':'");
    VariableDef v; v.pos = name.pos; v.name = name.lexeme; v.type = parseType(); return v;
}

LocalVarDef Parser::parseLocalVarDef() {
    const Token& name = expect(TokenKind::IDENT, "local variable name");
    LocalVarDef v; v.pos = name.pos; v.name = name.lexeme;
    if (match(TokenKind::COLON)) v.type = parseType();
    return v;
}

// объявление после "uses"
std::string Parser::parseType() {
    expect(TokenKind::KW_int, "'int'");
    if (match(TokenKind::LBRACKET)) {
        expect(TokenKind::RBRACKET, "']'");
        return "int[]";
    }
    return "int";
}

// выбор правила по однозначному первому токену
StatementPtr Parser::parseStatement() {
    switch (current().kind) {
        case TokenKind::LBRACE: return parseBlock();
        case TokenKind::KW_if: return parseIf();
        case TokenKind::KW_while: return parseWhile();
        case TokenKind::KW_assert: return parseAssert();
        case TokenKind::KW_assume: return parseAssume();
        case TokenKind::IDENT: return parseAssignment();
        default: errorHere("expected a statement, got " + tokenText(current()));
    }
}

// разбор "{...}"
StatementPtr Parser::parseBlock() {
    const Token& open = expect(TokenKind::LBRACE, "'{'");
    auto s = std::make_shared<Statement>(); s->kind = Statement::Kind::Block; s->pos = open.pos;
    while (!check(TokenKind::RBRACE) && !check(TokenKind::END)) {
        const std::size_t start = index_;
        try {
            s->statements.push_back(parseStatement());
        } catch (const Failure&) {
            synchronizeStatement(start);
        }
    }
    expect(TokenKind::RBRACE, "'}'");
    return s;
}

// присваивание: "[" и "," и "="
StatementPtr Parser::parseAssignment() {
    const Token& name = expect(TokenKind::IDENT, "assignment target");
    auto s = std::make_shared<Statement>(); s->kind = Statement::Kind::Assignment; s->pos = name.pos; s->target = name.lexeme;

    if (check(TokenKind::LBRACKET)) {
        s->indices = parseArraySuffixes();
        expect(TokenKind::ASSIGN, "'='");
        s->value = parseExpr();
        expect(TokenKind::SEMICOLON, "';'");
        return s;
    }

    if (match(TokenKind::COMMA)) {
        s->targets.push_back(name.lexeme);
        s->targets.push_back(expect(TokenKind::IDENT, "assignment target after ','").lexeme);
        while (match(TokenKind::COMMA)) s->targets.push_back(expect(TokenKind::IDENT, "assignment target after ','").lexeme);
        expect(TokenKind::ASSIGN, "'='");
        if (!check(TokenKind::IDENT)) errorHere("tuple assignment requires a function call on the right-hand side");
        const Token& callee = current();
        ++index_;
        expect(TokenKind::LPAREN, "'('");
        auto call = makeCall(callee.pos, callee.lexeme, parseArguments());
        s->call = std::move(call);
        expect(TokenKind::SEMICOLON, "';'");
        return s;
    }

    expect(TokenKind::ASSIGN, "'='");
    s->value = parseExpr();
    expect(TokenKind::SEMICOLON, "';'");
    return s;
}

// if / else
StatementPtr Parser::parseIf() {
    const Token& kw = expect(TokenKind::KW_if, "'if'");
    auto s = std::make_shared<Statement>(); s->kind = Statement::Kind::If; s->pos = kw.pos;
    expect(TokenKind::LPAREN, "'('");
    s->condition = parseCondition();
    expect(TokenKind::RPAREN, "')'");
    s->thenBranch = parseStatement();
    if (match(TokenKind::KW_else)) s->elseBranch = parseStatement();
    return s;
}

// while
StatementPtr Parser::parseWhile() {
    const Token& kw = expect(TokenKind::KW_while, "'while'");
    auto s = std::make_shared<Statement>(); s->kind = Statement::Kind::While; s->pos = kw.pos;
    expect(TokenKind::LPAREN, "'('");
    s->condition = parseCondition();
    expect(TokenKind::RPAREN, "')'");
    if (match(TokenKind::KW_invariant)) s->invariant = parsePredicate();
    else s->invariant = makeBool(kw.pos, true);
    s->body = parseStatement();
    return s;
}

// assert
StatementPtr Parser::parseAssert() {
    const Token& kw = expect(TokenKind::KW_assert, "'assert'");
    auto s = std::make_shared<Statement>(); s->kind = Statement::Kind::Assert; s->pos = kw.pos;
    s->predicate = parsePredicate();
    expect(TokenKind::SEMICOLON, "';'");
    return s;
}

// assume
StatementPtr Parser::parseAssume() {
    const Token& kw = expect(TokenKind::KW_assume, "'assume'");
    auto s = std::make_shared<Statement>(); s->kind = Statement::Kind::Assume; s->pos = kw.pos;
    s->predicate = parsePredicate();
    expect(TokenKind::SEMICOLON, "';'");
    return s;
}

ExprPtr Parser::parseExpr() { return parseAdd(); }

// + / -
ExprPtr Parser::parseAdd() {
    auto left = parseMul();
    while (check(TokenKind::PLUS) || check(TokenKind::MINUS)) {
        const Token op = current(); ++index_;
        auto right = parseMul();
        left = makeBinary(op.pos, op.kind == TokenKind::PLUS ? "+" : "-", std::move(left), std::move(right));
    }
    return left;
}

// * / деление
ExprPtr Parser::parseMul() {
    auto left = parseUnary();
    while (check(TokenKind::STAR) || check(TokenKind::SLASH)) {
        const Token op = current(); ++index_;
        auto right = parseUnary();
        left = makeBinary(op.pos, op.kind == TokenKind::STAR ? "*" : "/", std::move(left), std::move(right));
    }
    return left;
}

// унарный минус (перед выражением)
ExprPtr Parser::parseUnary() {
    if (check(TokenKind::MINUS)) {
        const Token op = current(); ++index_;
        return makeUnary(op.pos, "-", parseUnary());
    }
    return parsePrimary();
}

// целочисленная константа, переменная/вызов/доступ к массиву или выражение в скобках
ExprPtr Parser::parsePrimary() {
    if (check(TokenKind::INT)) {
        const Token t = current(); ++index_;
        long long value = 0;
        const auto* first = t.lexeme.data();
        std::from_chars(first, first + t.lexeme.size(), value);
        return makeInt(t.pos, value);
    }
    if (check(TokenKind::IDENT) || check(TokenKind::KW_length)) {
        const Token name = current(); ++index_;
        if (match(TokenKind::LPAREN)) {
            return makeCall(name.pos, name.lexeme, parseArguments());
        }
        auto indices = parseArraySuffixes();
        if (indices.empty()) return makeVar(name.pos, name.lexeme);
        return makeArray(name.pos, name.lexeme, std::move(indices));
    }
    if (match(TokenKind::LPAREN)) {
        auto e = parseExpr();
        expect(TokenKind::RPAREN, "')'");
        return e;
    }
    errorHere("expected an expression, got " + tokenText(current()));
}

// разбор списка внутри вызова функции
std::vector<ExprPtr> Parser::parseArguments() {
    std::vector<ExprPtr> args;
    if (match(TokenKind::RPAREN)) return args;
    args.push_back(parseExpr());
    while (match(TokenKind::COMMA)) args.push_back(parseExpr());
    expect(TokenKind::RPAREN, "')'");
    return args;
}

// разбор "[...]"
std::vector<ExprPtr> Parser::parseArraySuffixes() {
    std::vector<ExprPtr> result;
    while (match(TokenKind::LBRACKET)) {
        result.push_back(parseExpr());
        expect(TokenKind::RBRACKET, "']'");
    }
    return result;
}

PredicatePtr Parser::parseCondition() { return parseImplication(); }

// ->
PredicatePtr Parser::parseImplication() {
    auto left = parseOr();
    if (match(TokenKind::ARROW)) {
        const Token op = tokens_[index_ - 1];
        auto right = parseImplication();
        return makeBinaryPred(op.pos, "->", std::move(left), std::move(right));
    }
    return left;
}

//  or
PredicatePtr Parser::parseOr() {
    auto left = parseAnd();
    while (match(TokenKind::KW_or)) {
        const Token op = tokens_[index_ - 1];
        auto right = parseAnd();
        left = makeBinaryPred(op.pos, "or", std::move(left), std::move(right));
    }
    return left;
}

// and
PredicatePtr Parser::parseAnd() {
    auto left = parseConditionUnary();
    while (match(TokenKind::KW_and)) {
        const Token op = tokens_[index_ - 1];
        auto right = parseConditionUnary();
        left = makeBinaryPred(op.pos, "and", std::move(left), std::move(right));
    }
    return left;
}

// not
PredicatePtr Parser::parseConditionUnary() {
    if (check(TokenKind::KW_not)) {
        const Token op = current(); ++index_;
        return makeUnaryPred(op.pos, "not", parseConditionUnary());
    }
    return parseConditionAtom();
}

PredicatePtr Parser::parseConditionAtom() {
    if (check(TokenKind::KW_true)) { const Token t = current(); ++index_; return makeBool(t.pos, true); }
    if (check(TokenKind::KW_false)) { const Token t = current(); ++index_; return makeBool(t.pos, false); }
    if (check(TokenKind::LPAREN)) {
        // явное определение: выражение в скобках участвует в сравнении?
        if (parenthesizedValueIsComparisonLeftOperand()) {
            ++index_;
            auto left = parseExpr();
            expect(TokenKind::RPAREN, "')'");
            return parseComparisonFromLeft(std::move(left));
        }
        ++index_;
        auto p = parseCondition();
        expect(TokenKind::RPAREN, "')'");
        return p;
    }
    return parseComparison();
}

PredicatePtr Parser::parseComparison() {
    auto left = parseExpr();
    return parseComparisonFromLeft(std::move(left));
}

PredicatePtr Parser::parseComparisonFromLeft(ExprPtr left) {
    if (!isComparisonOperator(current().kind)) {
        errorHere("expected comparison operator after expression, got " + tokenText(current()));
    }
    const Token op = current(); ++index_;
    const std::string opText = [&] {
        switch (op.kind) {
            case TokenKind::EQEQ: return std::string("==");
            case TokenKind::NE: return std::string("!=");
            case TokenKind::LE: return std::string("<=");
            case TokenKind::GE: return std::string(">=");
            case TokenKind::LT: return std::string("<");
            case TokenKind::GT: return std::string(">");
            default: return std::string("?");
        }
    }();
    auto right = parseExpr();
    return makeComparison(op.pos, opText, std::move(left), std::move(right));
}

PredicatePtr Parser::parsePredicate() { return parsePredicateOr(); }

// -> на самом деле нет; or
PredicatePtr Parser::parsePredicateImplies() {
    return parsePredicateOr();
}

PredicatePtr Parser::parsePredicateOr() {
    auto left = parsePredicateAnd();
    while (match(TokenKind::KW_or)) {
        const Token op = tokens_[index_ - 1];
        auto right = parsePredicateAnd();
        left = makeBinaryPred(op.pos, "or", std::move(left), std::move(right));
    }
    return left;
}

PredicatePtr Parser::parsePredicateAnd() {
    auto left = parsePredicateUnary();
    while (match(TokenKind::KW_and)) {
        const Token op = tokens_[index_ - 1];
        auto right = parsePredicateUnary();
        left = makeBinaryPred(op.pos, "and", std::move(left), std::move(right));
    }
    return left;
}

PredicatePtr Parser::parsePredicateUnary() {
    if (check(TokenKind::KW_not)) {
        const Token op = current(); ++index_;
        return makeUnaryPred(op.pos, "not", parsePredicateUnary());
    }
    return parsePredicateAtom();
}

PredicatePtr Parser::parsePredicateAtom() {
    if (check(TokenKind::KW_true)) { const Token t = current(); ++index_; return makeBool(t.pos, true); }
    if (check(TokenKind::KW_false)) { const Token t = current(); ++index_; return makeBool(t.pos, false); }
    if (check(TokenKind::KW_forall) || check(TokenKind::KW_exists)) return parseQuantifier();
    if (check(TokenKind::LPAREN)) {
        // явное определение: выражение в скобках участвует в сравнении?
        if (parenthesizedValueIsComparisonLeftOperand()) {
            ++index_;
            auto left = parseExpr();
            expect(TokenKind::RPAREN, "')'");
            return parseComparisonFromLeft(std::move(left));
        }
        ++index_;
        auto p = parsePredicate();
        expect(TokenKind::RPAREN, "')'");
        return p;
    }
    return parseFormulaRefOrComparison();
}

// разобрать `forall|exists ( variableDef | predicate )`
PredicatePtr Parser::parseQuantifier() {
    const Token q = current(); ++index_;
    expect(TokenKind::LPAREN, "'('");
    auto binder = parseVariableDef();
    expect(TokenKind::PIPE, "'|'");
    auto body = parsePredicate();
    expect(TokenKind::RPAREN, "')'");
    auto p = std::make_shared<Predicate>(); p->kind = Predicate::Kind::Quantifier; p->pos = q.pos;
    p->quantifier = q.lexeme; p->binder = std::move(binder); p->body = std::move(body); return p;
}

PredicatePtr Parser::parseFormulaRefOrComparison() {
    auto left = parseExpr();
    if (isComparisonOperator(current().kind)) {
        const Token op = current(); ++index_;
        const std::string opText = [&] {
            switch (op.kind) {
                case TokenKind::EQEQ: return std::string("==");
                case TokenKind::NE: return std::string("!=");
                case TokenKind::LE: return std::string("<=");
                case TokenKind::GE: return std::string(">=");
                case TokenKind::LT: return std::string("<");
                case TokenKind::GT: return std::string(">");
                default: return std::string("?");
            }
        }();
        auto right = parseExpr();
        return makeComparison(op.pos, opText, std::move(left), std::move(right));
    }
    if (left && left->kind == Expr::Kind::Call) {
        auto p = std::make_shared<Predicate>(); p->kind = Predicate::Kind::FormulaRef; p->pos = left->pos;
        p->name = left->name; p->args = std::move(left->args); return p;
    }
    errorHere("predicate atom must be a comparison, formula reference, boolean literal, or quantifier");
}

// оператор сравнения?
bool Parser::isComparisonOperator(TokenKind kind) const {
    return kind == TokenKind::EQEQ || kind == TokenKind::NE || kind == TokenKind::LE ||
           kind == TokenKind::GE || kind == TokenKind::LT || kind == TokenKind::GT;
}

// после "(...)" есть оператор сравнения?
bool Parser::parenthesizedValueIsComparisonLeftOperand() const {
    if (!check(TokenKind::LPAREN) || index_ >= matchingParen_.size()) return false;
    constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();
    const std::size_t close = matchingParen_[index_];
    if (close == kNoIndex || close + 1 >= tokens_.size()) return false;
    return isComparisonOperator(tokens_[close + 1].kind);
}

// если блок с ошибкой - пропуск и идем дальше
void Parser::synchronizeTopLevel(std::size_t startIndex) {
    const std::size_t before = index_;
    while (!check(TokenKind::END) && !check(TokenKind::KW_function) && !check(TokenKind::IDENT)) ++index_;
    if (index_ == before && index_ == startIndex && !check(TokenKind::END)) ++index_;
}
// то же самое, но внутри {}
void Parser::synchronizeStatement(std::size_t startIndex) {
    const std::size_t before = index_;
    while (!check(TokenKind::END) && !check(TokenKind::RBRACE) && !check(TokenKind::SEMICOLON)) ++index_;
    if (check(TokenKind::SEMICOLON)) ++index_;
    if (index_ == before && index_ == startIndex && !check(TokenKind::END) && !check(TokenKind::RBRACE)) ++index_;
}

} // namespace funny
