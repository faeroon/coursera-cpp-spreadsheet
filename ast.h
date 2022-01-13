#pragma once

#include "formula.h"

#include <cmath>
#include <string>
#include <variant>
#include <memory>
#include <unordered_map>
#include <map>
#include <stack>
#include <set>

namespace Ast {

enum UnaryOperator {
    PLUS,
    MINUS
};

char ToString(UnaryOperator op);

enum BinaryOperator {
    ADD,
    SUB,
    MUL,
    DIV
};

using CellParam = std::optional<Position>;
using CellParamPtr = std::shared_ptr<CellParam>;

char ToString(BinaryOperator op);

struct Literal {
    std::string value;

    double AsDouble() const {
        return std::stod(value);
    }
};

class Parentheses;
using ParenthesesPtr = std::unique_ptr<Parentheses>;

class UnaryOp;
using UnaryOpPtr = std::unique_ptr<UnaryOp>;

class BinaryOp;
using BinaryOpPtr = std::unique_ptr<BinaryOp>;

class Node;

class Node : std::variant<Literal, CellParamPtr, ParenthesesPtr, UnaryOpPtr, BinaryOpPtr> {

private:

    enum ParenthesesRestrictions {
        ALL,
        LEFT,
        RIGHT,
        NONE
    };

    static constexpr ParenthesesRestrictions UNARY_RESTRICTIONS[4] { ALL, ALL, NONE, NONE };

    static Ast::Node SimplifyUnaryParentheses(Ast::Node child);

    static constexpr ParenthesesRestrictions BINARY_RESTRICTIONS[4][4] {
        {NONE, NONE, NONE, NONE}, // ADD
        {RIGHT, RIGHT, NONE, NONE}, // SUB
        {ALL, ALL, NONE, NONE}, // MUL
        {ALL, ALL, RIGHT, RIGHT} // DIV
    };

    static Ast::Node SimplifyBinaryParentheses(BinaryOperator parent_op, Ast::Node child, bool left);

public:

    using variant::variant;

    //region node type checkers

    bool IsLiteral() const {
        return std::holds_alternative<Ast::Literal>(*this);
    }

    bool IsCell() const {
        return std::holds_alternative<Ast::CellParamPtr>(*this);
    }

    bool IsParentheses() const {
        return std::holds_alternative<ParenthesesPtr>(*this);
    }

    bool IsUnaryOp() const {
        return std::holds_alternative<UnaryOpPtr>(*this);
    }

    bool IsBinaryOp() const {
        return std::holds_alternative<BinaryOpPtr>(*this);
    }

    //endregion

    //region node type converters

    const Literal& AsLiteral() const {
        return std::get<Ast::Literal>(*this);
    }

    const CellParam&  AsCell() const {
        return *std::get<Ast::CellParamPtr>(*this);
    }

    Parentheses& AsMutableParentheses();

    const Parentheses& AsParentheses() const;

    UnaryOp& AsMutableUnaryOp();

    const UnaryOp& AsUnaryOp() const;

    BinaryOp& AsMutableBinaryOp();

    const BinaryOp& AsBinaryOp() const;

    //endregion

    //region static initializers

    static Ast::Node OfLiteral(std::string literal);

    static Ast::Node OfCellParamPtr(CellParamPtr id);

    static Ast::Node OfParentheses(Ast::Node token);

    static Ast::Node Unary(Ast::Node token, UnaryOperator op);

    static Ast::Node UnaryMinus(Ast::Node token);

    static Ast::Node UnaryPlus(Ast::Node token);

    static Ast::Node Binary(Ast::Node lhs, Ast::Node rhs, BinaryOperator op);

    static Ast::Node BinaryAdd(Ast::Node lhs, Ast::Node rhs);

    static Ast::Node BinarySub(Ast::Node lhs, Ast::Node rhs);

    static Ast::Node BinaryMul(Ast::Node lhs, Ast::Node rhs);

    static Ast::Node BinaryDiv(Ast::Node lhs, Ast::Node rhs);

    //endregion

    IFormula::Value Evaluate(const ISheet& sheet) const;

    std::string BuildExpression() const;

};

class Parentheses {
private:
    Ast::Node content_;

public:

    explicit Parentheses(Ast::Node&& content): content_(std::move(content)) {}

    Ast::Node Extract() {
        return std::move(content_);
    }

    const Ast::Node& GetContent() const {
        return content_;
    }
};

class UnaryOp {
private:
    const Ast::Node token_;
    const UnaryOperator op_;

public:

    UnaryOp(Ast::Node&& token, UnaryOperator op): token_(std::move(token)), op_(op) {}

    const Ast::Node& GetToken() const {
        return token_;
    }

    UnaryOperator GetOp() const {
        return op_;
    }
};

class BinaryOp {
private:
    Ast::Node lhs_;
    Ast::Node rhs_;
    BinaryOperator op_;

public:

    BinaryOp(
        Ast::Node&& lhs,
        Ast::Node&& rhs,
        BinaryOperator op
    ) : lhs_(std::move(lhs)), rhs_(std::move(rhs)), op_(op) {}

    const Ast::Node& GetLhs() const {
        return lhs_;
    }

    const Ast::Node& GetRhs() const {
        return rhs_;
    }

    BinaryOperator GetOp() const {
        return op_;
    }
};

class CellParamCache {
private:
    std::map<int, std::map<int, CellParamPtr>> cell_params_;

public:

    const CellParamPtr& GetOrInsert(Position position);

    size_t HandleInsertedRows(int before, int count);

    size_t HandleInsertedCols(int before, int count);

    std::pair<size_t, size_t> HandleDeletedRows(int start, int count);

    std::pair<size_t, size_t> HandleDeletedCols(int start, int count);

    std::vector<Position> GetReferencedCells() const;
};

class Tree {
private:
    Ast::Node root_;
    CellParamCache cell_cache_;

public:

    Tree(
        Ast::Node root,
        CellParamCache cell_cache
    ) : root_(std::move(root)), cell_cache_(std::move(cell_cache)) {}

    IFormula::Value Evaluate(const ISheet& sheet) const {
        return root_.Evaluate(sheet);
    }

    std::string BuildExpression() const {
        return root_.BuildExpression();
    }

    std::vector<Position> GetReferencedCells() const {
        return cell_cache_.GetReferencedCells();
    }

    size_t HandleInsertedRows(int before, int count) {
        return cell_cache_.HandleInsertedRows(before, count);
    }

    size_t HandleInsertedCols(int before, int count) {
        return cell_cache_.HandleInsertedCols(before, count);
    }

    std::pair<size_t, size_t> HandleDeletedRows(int first, int count) {
        return cell_cache_.HandleDeletedRows(first, count);
    }

    std::pair<size_t, size_t> HandleDeletedCols(int first, int count) {
        return cell_cache_.HandleDeletedCols(first, count);
    }

};

class TreeBuilder {
private:
    std::stack<Ast::Node> node_stack_;
    CellParamCache cell_cache_;

public:

    TreeBuilder& AddLiteral(std::string literal);

    TreeBuilder& AddCell(const std::string& cell_name);

    TreeBuilder& AddParentheses();

    TreeBuilder& AddUnaryOp(UnaryOperator op);

    TreeBuilder& AddBinaryOp(BinaryOperator op);

    Ast::Tree Build();
};

}