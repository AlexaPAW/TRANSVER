#pragma once
#include "token.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace funny::ast {

// каждый составной узел может содержать произвольное число вложенных узлов
using ExprPtr = std::shared_ptr<struct Expr>;
using PredicatePtr = std::shared_ptr<struct Predicate>;
using StatementPtr = std::shared_ptr<struct Statement>;

struct VariableDef {
    SourcePos pos;
    std::string name;
    std::string type;
};

// uses
struct LocalVarDef {
    SourcePos pos;
    std::string name;
    std::optional<std::string> type;
};

struct Expr {
    enum class Kind { Int, Variable, ArrayAccess, Unary, Binary, Call };
    SourcePos pos;
    Kind kind = Kind::Int;
    long long intValue = 0;
    std::string name;
    std::string op;
    std::vector<ExprPtr> indices;
    ExprPtr operand;
    ExprPtr left;
    ExprPtr right;
    std::vector<ExprPtr> args;
};

struct Predicate {
    enum class Kind { Bool, Comparison, Unary, Binary, FormulaRef, Quantifier };
    SourcePos pos;
    Kind kind = Kind::Bool;
    bool boolValue = false;
    std::string name;
    std::string op;
    ExprPtr left;
    ExprPtr right;
    PredicatePtr predLeft;
    PredicatePtr predRight;
    PredicatePtr operand;
    std::vector<ExprPtr> args;
    std::string quantifier;
    VariableDef binder;
    PredicatePtr body;
};

struct Statement {
    enum class Kind { Block, Assignment, If, While, Assert, Assume };
    SourcePos pos;
    Kind kind = Kind::Block;

    // Assignment
    std::string target;
    std::vector<ExprPtr> indices; // непустой список
    std::vector<std::string> targets; // список целей кортежного присваивания
    ExprPtr value;
    ExprPtr call;

    // If / While
    PredicatePtr condition;
    PredicatePtr invariant;
    StatementPtr thenBranch;
    StatementPtr elseBranch;
    StatementPtr body;

    // Block
    std::vector<StatementPtr> statements;

    // Assert / Assume
    PredicatePtr predicate;
};

struct FunctionDecl {
    SourcePos pos;
    std::string name;
    std::vector<VariableDef> params;
    PredicatePtr requiresClause;
    std::vector<VariableDef> returns;
    PredicatePtr ensures;
    std::vector<LocalVarDef> uses;
    StatementPtr body;
};

struct FormulaDecl {
    SourcePos pos;
    std::string name;
    std::vector<VariableDef> params;
    PredicatePtr body;
};

struct Declaration {
    enum class Kind { Function, Formula };
    SourcePos pos;
    Kind kind = Kind::Function;
    std::shared_ptr<FunctionDecl> function;
    std::shared_ptr<FormulaDecl> formula;
};

struct Program {
    std::vector<std::shared_ptr<Declaration>> declarations;
};

std::string toJson(const Program& program, bool pretty = true);

} // namespace funny::ast
