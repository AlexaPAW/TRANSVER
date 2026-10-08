#include "ast.hpp"
#include <iomanip>
#include <sstream>

namespace funny::ast {
namespace {

    // кавычки и отступы
    class JsonWriter {
    public:
        explicit JsonWriter(bool pretty) : pretty_(pretty) {}

        std::string finish() const { return out_.str() + (pretty_ ? "\n" : ""); }

        void raw(const std::string& s) { out_ << s; }
        void string(const std::string& s) {
            out_ << '"';
            for (unsigned char c : s) {
                switch (c) {
                    case '"': out_ << "\\\""; break;
                    case '\\': out_ << "\\\\"; break;
                    case '\n': out_ << "\\n"; break;
                    case '\r': out_ << "\\r"; break;
                    case '\t': out_ << "\\t"; break;
                    default:
                        if (c < 0x20) {
                            out_ << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                                << std::dec << std::setfill(' ');
                        } else out_ << static_cast<char>(c);
                }
            }
            out_ << '"';
        }

        // отступ в 2 пробела
        void indent() {
            if (pretty_) out_ << std::string(indent_ * 2, ' ');
        }
        void newline() {
            if (pretty_) out_ << '\n';
        }
        // глубинна вложенности увеличивается / уменьшается
        void push() { ++indent_; }
        void pop() { --indent_; }

    private:
        bool pretty_;
        int indent_ = 0;
        std::ostringstream out_;
    };

    void fieldName(JsonWriter& w, const char* name, bool& first) {
        if (!first) w.raw(",");
        w.newline(); w.indent(); w.string(name); w.raw(": "); first = false;
    }

    void startObject(JsonWriter& w) { w.raw("{"); w.push(); }
    void endObject(JsonWriter& w, bool hadFields) {
        w.pop();
        if (hadFields) { w.newline(); w.indent(); }
        w.raw("}");
    }

    void writePos(JsonWriter& w, const SourcePos& p) {
        startObject(w); bool first = true;
        fieldName(w, "offset", first); w.raw(std::to_string(p.offset));
        fieldName(w, "line", first); w.raw(std::to_string(p.line));
        fieldName(w, "column", first); w.raw(std::to_string(p.column));
        endObject(w, true);
    }

    void writeString(JsonWriter& w, const std::string& s) { w.string(s); }

    void writeVariableDef(JsonWriter& w, const VariableDef& v) {
        startObject(w); bool first = true;
        fieldName(w, "kind", first); writeString(w, "VariableDef");
        fieldName(w, "pos", first); writePos(w, v.pos);
        fieldName(w, "name", first); writeString(w, v.name);
        fieldName(w, "type", first); writeString(w, v.type);
        endObject(w, true);
    }

    void writeLocalVarDef(JsonWriter& w, const LocalVarDef& v) {
        startObject(w); bool first = true;
        fieldName(w, "kind", first); writeString(w, "LocalVarDef");
        fieldName(w, "pos", first); writePos(w, v.pos);
        fieldName(w, "name", first); writeString(w, v.name);
        fieldName(w, "type", first);
        if (v.type) writeString(w, *v.type); else w.raw("null");
        endObject(w, true);
    }

    std::string exprKindName(Expr::Kind k) {
        switch (k) {
            case Expr::Kind::Int: return "IntLiteral";
            case Expr::Kind::Variable: return "Variable";
            case Expr::Kind::ArrayAccess: return "ArrayAccess";
            case Expr::Kind::Unary: return "UnaryExpr";
            case Expr::Kind::Binary: return "BinaryExpr";
            case Expr::Kind::Call: return "CallExpr";
        }
        return "Expr";
    }

    std::string predicateKindName(Predicate::Kind k) {
        switch (k) {
            case Predicate::Kind::Bool: return "BoolLiteral";
            case Predicate::Kind::Comparison: return "Comparison";
            case Predicate::Kind::Unary: return "UnaryPredicate";
            case Predicate::Kind::Binary: return "BinaryPredicate";
            case Predicate::Kind::FormulaRef: return "FormulaRef";
            case Predicate::Kind::Quantifier: return "Quantifier";
        }
        return "Predicate";
    }

    std::string statementKindName(Statement::Kind k) {
        switch (k) {
            case Statement::Kind::Block: return "Block";
            case Statement::Kind::Assignment: return "Assignment";
            case Statement::Kind::If: return "If";
            case Statement::Kind::While: return "While";
            case Statement::Kind::Assert: return "Assert";
            case Statement::Kind::Assume: return "Assume";
        }
        return "Statement";
    }

    void writeExpr(JsonWriter& w, const ExprPtr& e);
    void writePredicate(JsonWriter& w, const PredicatePtr& p);
    void writeStatement(JsonWriter& w, const StatementPtr& s);

    // рекурсивная сериализация выражений
    void writeExpr(JsonWriter& w, const ExprPtr& e) {
        if (!e) { w.raw("null"); return; }
        startObject(w); bool first = true;
        fieldName(w, "kind", first); writeString(w, exprKindName(e->kind));
        fieldName(w, "pos", first); writePos(w, e->pos);
        switch (e->kind) {
            case Expr::Kind::Int:
                fieldName(w, "value", first); w.raw(std::to_string(e->intValue));
                break;
            case Expr::Kind::Variable:
                fieldName(w, "name", first); writeString(w, e->name);
                break;
            case Expr::Kind::ArrayAccess:
                fieldName(w, "name", first); writeString(w, e->name);
                fieldName(w, "indices", first); w.raw("["); w.push();
                for (std::size_t i = 0; i < e->indices.size(); ++i) {
                    if (i) w.raw(",");
                    w.newline();
                    w.indent();
                    writeExpr(w, e->indices[i]);
                }
                w.pop(); if (!e->indices.empty()) { w.newline(); w.indent(); } w.raw("]");
                break;
            case Expr::Kind::Unary:
                fieldName(w, "op", first); writeString(w, e->op);
                fieldName(w, "operand", first); writeExpr(w, e->operand);
                break;
            case Expr::Kind::Binary:
                fieldName(w, "op", first); writeString(w, e->op);
                fieldName(w, "left", first); writeExpr(w, e->left);
                fieldName(w, "right", first); writeExpr(w, e->right);
                break;
            case Expr::Kind::Call:
                fieldName(w, "name", first); writeString(w, e->name);
                fieldName(w, "args", first); w.raw("["); w.push();
                for (std::size_t i = 0; i < e->args.size(); ++i) {
                    if (i) w.raw(",");
                    w.newline();
                    w.indent();
                    writeExpr(w, e->args[i]);
                }
                w.pop(); if (!e->args.empty()) { w.newline(); w.indent(); } w.raw("]");
                break;
        }
        endObject(w, true);
    }

    // рекурсивная сериализация предикатов
    void writePredicate(JsonWriter& w, const PredicatePtr& p) {
        if (!p) { w.raw("null"); return; }
        startObject(w); bool first = true;
        fieldName(w, "kind", first); writeString(w, predicateKindName(p->kind));
        fieldName(w, "pos", first); writePos(w, p->pos);
        switch (p->kind) {
            case Predicate::Kind::Bool:
                fieldName(w, "value", first); w.raw(p->boolValue ? "true" : "false");
                break;
            case Predicate::Kind::Comparison:
                fieldName(w, "op", first); writeString(w, p->op);
                fieldName(w, "left", first); writeExpr(w, p->left);
                fieldName(w, "right", first); writeExpr(w, p->right);
                break;
            case Predicate::Kind::Unary:
                fieldName(w, "op", first); writeString(w, p->op);
                fieldName(w, "operand", first); writePredicate(w, p->operand);
                break;
            case Predicate::Kind::Binary:
                fieldName(w, "op", first); writeString(w, p->op);
                fieldName(w, "left", first); writePredicate(w, p->predLeft);
                fieldName(w, "right", first); writePredicate(w, p->predRight);
                break;
            case Predicate::Kind::FormulaRef:
                fieldName(w, "name", first); writeString(w, p->name);
                fieldName(w, "args", first); w.raw("["); w.push();
                for (std::size_t i = 0; i < p->args.size(); ++i) {
                    if (i) w.raw(",");
                    w.newline();
                    w.indent();
                    writeExpr(w, p->args[i]);
                }
                w.pop(); if (!p->args.empty()) { w.newline(); w.indent(); } w.raw("]");
                break;
            case Predicate::Kind::Quantifier:
                fieldName(w, "quantifier", first); writeString(w, p->quantifier);
                fieldName(w, "binder", first); writeVariableDef(w, p->binder);
                fieldName(w, "body", first); writePredicate(w, p->body);
                break;
        }
        endObject(w, true);
    }

    // рекурсивная сериализация операторов
    void writeStatement(JsonWriter& w, const StatementPtr& s) {
        if (!s) { w.raw("null"); return; }
        startObject(w); bool first = true;
        fieldName(w, "kind", first); writeString(w, statementKindName(s->kind));
        fieldName(w, "pos", first); writePos(w, s->pos);
        switch (s->kind) {
            case Statement::Kind::Block:
                fieldName(w, "statements", first); w.raw("["); w.push();
                for (std::size_t i = 0; i < s->statements.size(); ++i) {
                    if (i) w.raw(",");
                    w.newline();
                    w.indent();
                    writeStatement(w, s->statements[i]);
                }
                w.pop(); if (!s->statements.empty()) { w.newline(); w.indent(); } w.raw("]");
                break;
            case Statement::Kind::Assignment:
                if (!s->targets.empty()) {
                    fieldName(w, "targets", first); w.raw("[");
                    for (std::size_t i = 0; i < s->targets.size(); ++i) {
                        if (i) w.raw(", ");
                        writeString(w, s->targets[i]);
                    }
                    w.raw("]");
                    fieldName(w, "call", first); writeExpr(w, s->call);
                } else {
                    fieldName(w, "target", first); writeString(w, s->target);
                    fieldName(w, "indices", first); w.raw("["); w.push();
                    for (std::size_t i = 0; i < s->indices.size(); ++i) {
                        if (i) w.raw(",");
                        w.newline();
                        w.indent();
                        writeExpr(w, s->indices[i]);
                    }
                    w.pop(); if (!s->indices.empty()) { w.newline(); w.indent(); } w.raw("]");
                    fieldName(w, "value", first); writeExpr(w, s->value);
                }
                break;
            case Statement::Kind::If:
                fieldName(w, "condition", first); writePredicate(w, s->condition);
                fieldName(w, "then", first); writeStatement(w, s->thenBranch);
                fieldName(w, "else", first); writeStatement(w, s->elseBranch);
                break;
            case Statement::Kind::While:
                fieldName(w, "condition", first); writePredicate(w, s->condition);
                fieldName(w, "invariant", first); writePredicate(w, s->invariant);
                fieldName(w, "body", first); writeStatement(w, s->body);
                break;
            case Statement::Kind::Assert:
            case Statement::Kind::Assume:
                fieldName(w, "predicate", first); writePredicate(w, s->predicate);
                break;
        }
        endObject(w, true);
    }

    // сериализация функции
    void writeFunction(JsonWriter& w, const FunctionDecl& f) {
        startObject(w); bool first = true;
        fieldName(w, "kind", first); writeString(w, "FunctionDecl");
        fieldName(w, "pos", first); writePos(w, f.pos);
        fieldName(w, "name", first); writeString(w, f.name);
        fieldName(w, "params", first); w.raw("["); w.push();
        for (std::size_t i = 0; i < f.params.size(); ++i) { if (i) w.raw(","); w.newline(); w.indent(); writeVariableDef(w, f.params[i]); }
        w.pop(); if (!f.params.empty()) { w.newline(); w.indent(); } w.raw("]");
        fieldName(w, "requires", first); writePredicate(w, f.requiresClause);
        fieldName(w, "returns", first); w.raw("["); w.push();
        for (std::size_t i = 0; i < f.returns.size(); ++i) { if (i) w.raw(","); w.newline(); w.indent(); writeVariableDef(w, f.returns[i]); }
        w.pop(); if (!f.returns.empty()) { w.newline(); w.indent(); } w.raw("]");
        fieldName(w, "ensures", first); writePredicate(w, f.ensures);
        fieldName(w, "uses", first); w.raw("["); w.push();
        for (std::size_t i = 0; i < f.uses.size(); ++i) { if (i) w.raw(","); w.newline(); w.indent(); writeLocalVarDef(w, f.uses[i]); }
        w.pop(); if (!f.uses.empty()) { w.newline(); w.indent(); } w.raw("]");
        fieldName(w, "body", first); writeStatement(w, f.body);
        endObject(w, true);
    }

    // сериализация формулы
    void writeFormula(JsonWriter& w, const FormulaDecl& f) {
        startObject(w); bool first = true;
        fieldName(w, "kind", first); writeString(w, "FormulaDecl");
        fieldName(w, "pos", first); writePos(w, f.pos);
        fieldName(w, "name", first); writeString(w, f.name);
        fieldName(w, "params", first); w.raw("["); w.push();
        for (std::size_t i = 0; i < f.params.size(); ++i) { if (i) w.raw(","); w.newline(); w.indent(); writeVariableDef(w, f.params[i]); }
        w.pop(); if (!f.params.empty()) { w.newline(); w.indent(); } w.raw("]");
        fieldName(w, "body", first); writePredicate(w, f.body);
        endObject(w, true);
    }

    // передавание объявления сериализатору функции или формулы
    void writeDeclaration(JsonWriter& w, const std::shared_ptr<Declaration>& d) {
        if (!d) { w.raw("null"); return; }
        if (d->kind == Declaration::Kind::Function) writeFunction(w, *d->function);
        else writeFormula(w, *d->formula);
    }

} // namespace

std::string toJson(const Program& program, bool pretty) {
    JsonWriter w(pretty);
    startObject(w); bool first = true;
    fieldName(w, "kind", first); writeString(w, "Program");
    fieldName(w, "declarations", first); w.raw("["); w.push();
    for (std::size_t i = 0; i < program.declarations.size(); ++i) {
        if (i) w.raw(",");
        w.newline();
        w.indent();
        writeDeclaration(w, program.declarations[i]);
    }
    w.pop(); if (!program.declarations.empty()) { w.newline(); w.indent(); } w.raw("]");
    endObject(w, true);
    return w.finish();
}

} // namespace funny::ast
