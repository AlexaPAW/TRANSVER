#include "semantic.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace funny {
namespace {

using namespace ast;

struct Symbol {
    std::string name;
    std::string type = "unknown";
    SourcePos pos;
    bool writable = true;
    bool isParameter = false;
    bool isReturn = false;
    bool isLocal = false;
    std::optional<std::string>* localAnnotation = nullptr;
};

struct Callable {
    enum class Kind { Function, Formula } kind = Kind::Function;
    std::string name;
    SourcePos pos;
    std::vector<std::string> params;
    std::vector<std::string> returns;
};

bool isConcrete(const std::string& type) {
    return type == "int" || type == "int[]" || type == "bool" || type == "error" ||
           type.rfind("tuple<", 0) == 0;
}

std::string joinTypes(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        out << values[i];
    }
    return out.str();
}

class Analyzer {
public:
    SemanticResult run(Program& p) {
        program_ = &p;
        collectGlobalSignatures();
        for (const auto& decl : p.declarations) {
            if (!decl) continue;
            if (decl->kind == Declaration::Kind::Function && decl->function) checkFunction(*decl->function);
            else if (decl->kind == Declaration::Kind::Formula && decl->formula) checkFormula(*decl->formula);
        }
        return {diagnostics_};
    }

private:
    Program* program_ = nullptr;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::string, Callable> callables_;
    std::unordered_map<std::string, SourcePos> globalNames_;
    std::unordered_map<std::string, Symbol> scope_;
    std::vector<std::unordered_map<std::string, Symbol>> nestedScopes_;

    void error(SourcePos pos, const std::string& message) {
        diagnostics_.push_back({"semantic", pos, message});
    }

    static std::string canonical(const std::string& t) {
        if (t == "int" || t == "int[]") return t;
        return t;
    }

    void collectGlobalSignatures() {
        for (const auto& decl : program_->declarations) {
            if (!decl) continue;
            std::string name;
            SourcePos pos = decl->pos;
            Callable c;
            if (decl->kind == Declaration::Kind::Function && decl->function) {
                const auto& f = *decl->function;
                name = f.name; c.name = name; c.pos = f.pos; c.kind = Callable::Kind::Function;
                for (const auto& v : f.params) c.params.push_back(canonical(v.type));
                for (const auto& v : f.returns) c.returns.push_back(canonical(v.type));
            } else if (decl->kind == Declaration::Kind::Formula && decl->formula) {
                const auto& f = *decl->formula;
                name = f.name; c.name = name; c.pos = f.pos; c.kind = Callable::Kind::Formula;
                for (const auto& v : f.params) c.params.push_back(canonical(v.type));
                c.returns.push_back("bool");
            } else continue;

            auto found = globalNames_.find(name);
            if (found != globalNames_.end()) {
                error(pos, "повторное глобальное объявление '" + name + "' (первое объявление: " +
                      std::to_string(found->second.line) + ":" + std::to_string(found->second.column) + ")");
            } else {
                globalNames_[name] = pos;
                callables_[name] = c;
            }
        }
    }

    void addSymbol(const std::string& name, const std::string& type, SourcePos pos,
                   bool isParam = false, bool isReturn = false, bool isLocal = false,
                   std::optional<std::string>* annotation = nullptr) {
        auto it = scope_.find(name);
        if (it != scope_.end()) {
            error(pos, "повторное объявление переменной '" + name + "' (первое объявление: " +
                  std::to_string(it->second.pos.line) + ":" + std::to_string(it->second.pos.column) + ")");
            return;
        }
        Symbol s; s.name = name; s.type = type; s.pos = pos; s.isParameter = isParam;
        s.isReturn = isReturn; s.isLocal = isLocal; s.localAnnotation = annotation;
        scope_[name] = std::move(s);
    }

    Symbol* lookup(const std::string& name) {
        for (auto it = nestedScopes_.rbegin(); it != nestedScopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        auto it = scope_.find(name);
        return it == scope_.end() ? nullptr : &it->second;
    }

    std::string lookupType(const std::string& name, SourcePos pos) {
        Symbol* s = lookup(name);
        if (!s) {
            error(pos, "неизвестное имя переменной '" + name + "'");
            return "error";
        }
        if (s->type == "unknown") {
            error(pos, "тип локальной переменной '" + name + "' ещё не выведен: сначала требуется присваивание");
            return "error";
        }
        return s->type;
    }

    bool unify(const std::string& actual, const std::string& expected, SourcePos pos,
               const std::string& context) {
        if (actual == "error" || expected == "error" || actual == "unknown" || expected == "unknown") return false;
        if (actual == expected) return true;
        error(pos, context + ": ожидался тип '" + expected + "', получен '" + actual + "'");
        return false;
    }

    bool checkArguments(const std::string& name, const std::vector<ExprPtr>& args,
                        const Callable& sig, SourcePos callPos) {
        bool valid = args.size() == sig.params.size();
        if (args.size() != sig.params.size()) {
            error(callPos, "вызов '" + name + "': ожидалось аргументов " +
                  std::to_string(sig.params.size()) + ", получено " + std::to_string(args.size()));
        }
        const std::size_t n = std::min(args.size(), sig.params.size());
        for (std::size_t i = 0; i < n; ++i) {
            std::string got = checkExpr(args[i]);
            if (!unify(got, sig.params[i], args[i] ? args[i]->pos : callPos,
                       "аргумент " + std::to_string(i + 1) + " вызова '" + name + "'")) valid = false;
        }
        for (std::size_t i = n; i < args.size(); ++i) checkExpr(args[i]);
        return valid;
    }

    std::string checkExpr(const ExprPtr& e) {
        if (!e) return "error";
        std::string type = "error";
        switch (e->kind) {
            case Expr::Kind::Int:
                type = "int";
                break;
            case Expr::Kind::Variable:
                type = lookupType(e->name, e->pos);
                break;
            case Expr::Kind::ArrayAccess: {
                std::string base = lookupType(e->name, e->pos);
                bool valid = base != "error";
                for (const auto& index : e->indices) {
                    std::string idxType = checkExpr(index);
                    if (!unify(idxType, "int", index ? index->pos : e->pos, "индекс массива")) valid = false;
                    if (base == "int[]") base = "int";
                    else if (base != "error") {
                        error(e->pos, "индексация требует массив int[], но '" + e->name + "' имеет тип '" + base + "'");
                        base = "error";
                        valid = false;
                    }
                }
                type = valid ? base : "error";
                break;
            }
            case Expr::Kind::Unary: {
                std::string operand = checkExpr(e->operand);
                const bool valid = unify(operand, "int", e->pos, "унарный оператор '" + e->op + "'");
                type = valid ? "int" : "error";
                break;
            }
            case Expr::Kind::Binary: {
                std::string left = checkExpr(e->left);
                std::string right = checkExpr(e->right);
                const bool leftOk = unify(left, "int", e->left ? e->left->pos : e->pos, "левый операнд '" + e->op + "'");
                const bool rightOk = unify(right, "int", e->right ? e->right->pos : e->pos, "правый операнд '" + e->op + "'");
                type = (leftOk && rightOk) ? "int" : "error";
                break;
            }
            case Expr::Kind::Call: {
                if (e->name == "length") {
                    bool valid = e->args.size() == 1;
                    if (!valid) {
                        error(e->pos, "вызов 'length': ожидался 1 аргумент, получено " + std::to_string(e->args.size()));
                    }
                    for (const auto& a : e->args) {
                        std::string at = checkExpr(a);
                        if (!unify(at, "int[]", a ? a->pos : e->pos, "аргумент length")) valid = false;
                    }
                    type = valid ? "int" : "error";
                    break;
                }
                auto it = callables_.find(e->name);
                if (it == callables_.end()) {
                    error(e->pos, "вызов неизвестной функции '" + e->name + "'");
                    for (const auto& a : e->args) checkExpr(a);
                    type = "error";
                } else if (it->second.kind != Callable::Kind::Function) {
                    error(e->pos, "формула '" + e->name + "' возвращает bool и может вызываться только как предикат");
                    (void)checkArguments(e->name, e->args, it->second, e->pos);
                    type = "error";
                } else {
                    const bool argsValid = checkArguments(e->name, e->args, it->second, e->pos);
                    if (!argsValid) type = "error";
                    else if (it->second.returns.size() == 1) type = it->second.returns.front();
                    else type = "tuple<" + joinTypes(it->second.returns) + ">";
                }
                break;
            }
        }
        e->inferredType = type;
        return type;
    }

    std::string checkPredicate(const PredicatePtr& p) {
        if (!p) return "bool";
        switch (p->kind) {
            case Predicate::Kind::Bool:
                break;
            case Predicate::Kind::Comparison: {
                std::string l = checkExpr(p->left);
                std::string r = checkExpr(p->right);
                if (p->op == "==" || p->op == "!=") {
                    if (l != "error" && r != "error" && l != r)
                        error(p->pos, "оператор '" + p->op + "' требует операнды одного типа, получены '" + l + "' и '" + r + "'");
                    if (l.rfind("tuple<", 0) == 0 || r.rfind("tuple<", 0) == 0)
                        error(p->pos, "сравнение кортежей результатов функций не поддерживается");
                } else {
                    unify(l, "int", p->left ? p->left->pos : p->pos, "левый операнд сравнения '" + p->op + "'");
                    unify(r, "int", p->right ? p->right->pos : p->pos, "правый операнд сравнения '" + p->op + "'");
                }
                break;
            }
            case Predicate::Kind::Unary:
                checkPredicate(p->operand);
                break;
            case Predicate::Kind::Binary:
                checkPredicate(p->predLeft);
                checkPredicate(p->predRight);
                if (p->op != "and" && p->op != "or" && p->op != "->")
                    error(p->pos, "неподдерживаемый логический оператор '" + p->op + "'");
                break;
            case Predicate::Kind::FormulaRef: {
                auto it = callables_.find(p->name);
                if (it == callables_.end()) {
                    error(p->pos, "неизвестная формула '" + p->name + "'");
                    for (const auto& a : p->args) checkExpr(a);
                } else if (it->second.kind != Callable::Kind::Formula) {
                    error(p->pos, "'" + p->name + "' — функция, а не формула; вызов в предикате недопустим");
                    checkArguments(p->name, p->args, it->second, p->pos);
                } else checkArguments(p->name, p->args, it->second, p->pos);
                break;
            }
            case Predicate::Kind::Quantifier: {
                nestedScopes_.emplace_back();
                auto& frame = nestedScopes_.back();
                // Переменная квантора живёт во вложенной области и может затенять внешнее имя.
                Symbol s; s.name = p->binder.name; s.type = canonical(p->binder.type); s.pos = p->binder.pos;
                frame[p->binder.name] = s;
                checkPredicate(p->body);
                nestedScopes_.pop_back();
                break;
            }
        }
        p->inferredType = "bool";
        return "bool";
    }

    void checkLValue(const std::string& name, const std::vector<ExprPtr>& indices,
                     SourcePos pos, const std::string& rhsType, bool inferAllowed) {
        Symbol* s = lookup(name);
        if (!s) {
            error(pos, "неизвестное имя переменной '" + name + "' слева от присваивания");
            for (const auto& idx : indices) unify(checkExpr(idx), "int", idx ? idx->pos : pos, "индекс массива");
            return;
        }
        if (indices.empty()) {
            if (s->type == "unknown" && inferAllowed && rhsType != "error" && rhsType != "unknown" && isConcrete(rhsType)) {
                if (rhsType.rfind("tuple<", 0) == 0) {
                    error(pos, "нельзя присвоить кортеж результатов функции одной переменной '" + name + "'");
                    return;
                }
                s->type = rhsType;
                if (s->localAnnotation) *s->localAnnotation = rhsType;
                return;
            }
            if (s->type == "unknown") {
                error(pos, "невозможно вывести тип локальной переменной '" + name + "' из этого присваивания");
                return;
            }
            unify(rhsType, s->type, pos, "присваивание переменной '" + name + "'");
            return;
        }

        bool indicesAreInt = true;
        for (const auto& idx : indices) {
            std::string idxType = checkExpr(idx);
            if (!unify(idxType, "int", idx ? idx->pos : pos, "индекс массива")) indicesAreInt = false;
        }
        if (s->type == "unknown" && inferAllowed && indices.size() == 1 && indicesAreInt && rhsType == "int") {
            s->type = "int[]";
            if (s->localAnnotation) *s->localAnnotation = "int[]";
        }
        if (s->type == "unknown") {
            error(pos, "невозможно вывести тип локальной переменной '" + name + "' из присваивания элементу массива");
            return;
        }
        std::string current = s->type;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            if (current == "int[]") current = "int";
            else if (current != "error") {
                error(pos, "индексация переменной '" + name + "' требует int[], получен '" + current + "'");
                current = "error";
            }
        }
        unify(rhsType, current, pos, "присваивание элементу массива '" + name + "'");
    }

    void checkStatement(const StatementPtr& st) {
        if (!st) return;
        switch (st->kind) {
            case Statement::Kind::Block:
                // В грамматике нет объявлений внутри block, однако область блока сохраняется для расширения.
                for (const auto& child : st->statements) checkStatement(child);
                break;
            case Statement::Kind::Assignment:
                if (!st->targets.empty()) {
                    std::vector<std::string> resultTypes;
                    auto it = st->call ? callables_.find(st->call->name) : callables_.end();
                    std::string callType = checkExpr(st->call);
                    if (it != callables_.end() && it->second.kind == Callable::Kind::Function) resultTypes = it->second.returns;
                    if (resultTypes.size() != st->targets.size()) {
                        error(st->pos, "кортежное присваивание: слева " + std::to_string(st->targets.size()) +
                              " целей, функция возвращает " + std::to_string(resultTypes.size()) + " значений");
                    }
                    for (std::size_t i = 0; i < st->targets.size(); ++i) {
                        std::string rhs = i < resultTypes.size() ? resultTypes[i] : "error";
                        checkLValue(st->targets[i], {}, st->pos, rhs, callType != "error");
                    }
                    (void)callType;
                } else {
                    std::string rhs = checkExpr(st->value);
                    checkLValue(st->target, st->indices, st->pos, rhs, true);
                }
                break;
            case Statement::Kind::If:
                checkPredicate(st->condition);
                checkStatement(st->thenBranch);
                checkStatement(st->elseBranch);
                break;
            case Statement::Kind::While:
                checkPredicate(st->condition);
                checkPredicate(st->invariant);
                checkStatement(st->body);
                break;
            case Statement::Kind::Assert:
            case Statement::Kind::Assume:
                checkPredicate(st->predicate);
                break;
        }
    }

    void clearFunctionScope() {
        scope_.clear();
        nestedScopes_.clear();
    }

    void checkFunction(FunctionDecl& f) {
        clearFunctionScope();
        for (const auto& p : f.params) addSymbol(p.name, canonical(p.type), p.pos, true, false, false);
        for (const auto& r : f.returns) addSymbol(r.name, canonical(r.type), r.pos, false, true, false);
        for (auto& u : f.uses) {
            const std::string t = u.type ? canonical(*u.type) : "unknown";
            addSymbol(u.name, t, u.pos, false, false, true, &u.type);
        }

        // Сначала тело: оно выводит типы локальных uses-переменных.
        checkStatement(f.body);
        // Контракты проверяются после вывода типов, но в той же области параметров/возвратов/uses.
        checkPredicate(f.requiresClause);
        checkPredicate(f.ensures);
        clearFunctionScope();
    }

    void checkFormula(FormulaDecl& f) {
        clearFunctionScope();
        for (const auto& p : f.params) addSymbol(p.name, canonical(p.type), p.pos, true, false, false);
        checkPredicate(f.body);
        clearFunctionScope();
    }
};

} // namespace

SemanticResult SemanticAnalyzer::analyze(ast::Program& program) {
    Analyzer analyzer;
    return analyzer.run(program);
}

} // namespace funny
