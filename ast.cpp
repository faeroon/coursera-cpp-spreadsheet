#include "ast.h"

namespace Ast {

char ToString(BinaryOperator op) {
    switch (op) {
        case BinaryOperator::ADD: return '+';
        case BinaryOperator::SUB: return '-';
        case BinaryOperator::MUL: return '*';
        default: return '/';
    }
}

char Ast::ToString(UnaryOperator op) {
    switch (op) {
        case UnaryOperator::PLUS:   return '+';
        case UnaryOperator::MINUS:  return '-';
    }
}

Parentheses& Node::AsMutableParentheses() {
    return *std::get<ParenthesesPtr>(*this);
}

const Parentheses& Node::AsParentheses() const {
    return *std::get<ParenthesesPtr>(*this);
}

UnaryOp& Node::AsMutableUnaryOp() {
    return *std::get<UnaryOpPtr>(*this);
}

const UnaryOp& Node::AsUnaryOp() const {
    return *std::get<UnaryOpPtr>(*this);
}

BinaryOp& Node::AsMutableBinaryOp() {
    return *std::get<BinaryOpPtr>(*this);
}

const BinaryOp& Node::AsBinaryOp() const {
    return *std::get<BinaryOpPtr>(*this);
}

Ast::Node Node::OfLiteral(std::string literal) {
    return Node(Ast::Literal{std::move(literal)});
}

Ast::Node Node::OfCellParamPtr(CellParamPtr id) {
    if(!(*id)->IsValid()) throw FormulaException("invalid position");
    return Node(id);
}

Ast::Node Node::OfParentheses(Node token) {
    return token.IsCell() || token.IsLiteral() || token.IsParentheses() ?
    std::move(token) : Node(std::make_unique<Ast::Parentheses>(std::move(token)));
}

Ast::Node Node::Unary(Ast::Node token, UnaryOperator op) {
    return Node(std::make_unique<UnaryOp>(SimplifyUnaryParentheses(std::move(token)), op));
}

Ast::Node Node::UnaryPlus(Node token) {
    return Node(std::make_unique<UnaryOp>(SimplifyUnaryParentheses(std::move(token)), UnaryOperator::PLUS));
}

Ast::Node Node::UnaryMinus(Node token) {
    return Node(std::make_unique<UnaryOp>(SimplifyUnaryParentheses(std::move(token)), UnaryOperator::MINUS));
}

Ast::Node Node::Binary(Ast::Node lhs, Ast::Node rhs, BinaryOperator op) {
    return Node(
        std::make_unique<BinaryOp>(
            SimplifyBinaryParentheses(op, std::move(lhs), true),
            SimplifyBinaryParentheses(op, std::move(rhs), false),
            op
        )
    );
}

Ast::Node Node::BinaryAdd(Node lhs, Node rhs) {
    return Node(
        std::make_unique<BinaryOp>(
            SimplifyBinaryParentheses(BinaryOperator::ADD, std::move(lhs), true),
            SimplifyBinaryParentheses(BinaryOperator::ADD, std::move(rhs), false),
            BinaryOperator::ADD
        )
    );
}

Ast::Node Node::BinarySub(Node lhs, Node rhs) {
    return Node(
        std::make_unique<BinaryOp>(
            SimplifyBinaryParentheses(BinaryOperator::SUB, std::move(lhs), true),
            SimplifyBinaryParentheses(BinaryOperator::SUB, std::move(rhs), false),
            BinaryOperator::SUB
        )
    );
}

Ast::Node Node::BinaryMul(Node lhs, Node rhs) {
    return Node(
        std::make_unique<BinaryOp>(
            SimplifyBinaryParentheses(BinaryOperator::MUL, std::move(lhs), true),
            SimplifyBinaryParentheses(BinaryOperator::MUL, std::move(rhs), false),
            BinaryOperator::MUL
        )
    );
}

Ast::Node Node::BinaryDiv(Node lhs, Node rhs) {
    return Node(
        std::make_unique<BinaryOp>(
            SimplifyBinaryParentheses(BinaryOperator::DIV, std::move(lhs), true),
            SimplifyBinaryParentheses(BinaryOperator::DIV, std::move(rhs), false),
            BinaryOperator::DIV
        )
    );

}

IFormula::Value Node::Evaluate(const ISheet& sheet) const {

    if (IsLiteral()) {
        return AsLiteral().AsDouble();
    } else if (IsCell()) {

        const CellParam& param = AsCell();

        if (param == std::nullopt) return FormulaError(FormulaError::Category::Ref);

        const ICell* cell = sheet.GetCell(*param);

        if (cell == nullptr) return 0.;

        ICell::Value cell_value = cell->GetValue();

        if (std::holds_alternative<std::string>(cell_value)) {

            const auto& cell_str_value = std::get<std::string>(cell_value);

            if (cell_str_value.empty()) return 0.;

            try {
                return std::stod(cell_str_value);
            } catch (std::invalid_argument& err) {
                return FormulaError(FormulaError::Category::Value);
            }
        } else if (std::holds_alternative<double>(cell_value)) {
            return std::get<double>(cell_value);
        } else {
            return std::get<FormulaError>(cell_value);
        }

    } else if (IsParentheses()) {
        return AsParentheses().GetContent().Evaluate(sheet);
    } else if (IsUnaryOp()) {

        const auto& unary_op = AsUnaryOp();
        IFormula::Value token = unary_op.GetToken().Evaluate(sheet);

        if (std::holds_alternative<double>(token)) {
            double value = std::get<double>(token);
            return unary_op.GetOp() == UnaryOperator::PLUS ? value : -value;
        }

        return token;

    } else if (IsBinaryOp()) {

        const auto& binary_op = AsBinaryOp();

        IFormula::Value lhs = binary_op.GetLhs().Evaluate(sheet);

        if (std::holds_alternative<FormulaError>(lhs)) {
            return lhs;
        }

        IFormula::Value rhs = binary_op.GetRhs().Evaluate(sheet);

        if (std::holds_alternative<FormulaError>(rhs)) {
            return rhs;
        }

        double lhs_value = std::get<double>(lhs);
        double rhs_value = std::get<double>(rhs);

        double result;

        switch (binary_op.GetOp()) {
            case BinaryOperator::ADD: result = lhs_value + rhs_value; break;
            case BinaryOperator::SUB: result = lhs_value - rhs_value; break;
            case BinaryOperator::MUL: result = lhs_value * rhs_value; break;
            case BinaryOperator::DIV: result = lhs_value / rhs_value; break;
        }

        if (std::isfinite(result)) {
            return result;
        } else {
            return FormulaError(FormulaError::Category::Div0);
        }
    }

    return 0.;
}

std::string Node::BuildExpression() const {
    if (IsLiteral()) {
        return AsLiteral().value;
    } else if (IsCell()) {
        const CellParam& param = AsCell();
        return param != std::nullopt ? param->ToString() : "#REF!";
    } else if (IsParentheses()) {
        return '(' + AsParentheses().GetContent().BuildExpression() + ')';
    } else if (IsUnaryOp()) {
        const auto& unary_op = AsUnaryOp();
        return ToString(unary_op.GetOp()) + unary_op.GetToken().BuildExpression();
    } else if (IsBinaryOp()) {
        const auto& binary_op = AsBinaryOp();
        return binary_op.GetLhs().BuildExpression() + ToString(binary_op.GetOp()) +
               binary_op.GetRhs().BuildExpression();
    }
}

Ast::Node Node::SimplifyUnaryParentheses(Ast::Node child) {

    if (!child.IsParentheses()) return child;

    auto& parentheses = child.AsMutableParentheses();

    if (parentheses.GetContent().IsBinaryOp()) {
        BinaryOperator child_op = parentheses.GetContent().AsBinaryOp().GetOp();
        ParenthesesRestrictions restrictions = UNARY_RESTRICTIONS[child_op];
        if (restrictions == ALL) return child;
    }

    return parentheses.Extract();
}

Ast::Node Node::SimplifyBinaryParentheses(BinaryOperator parent_op, Ast::Node child, bool left) {

    if (!child.IsParentheses()) return child;

    auto& parentheses = child.AsMutableParentheses();

    if (parentheses.GetContent().IsBinaryOp()) {
        BinaryOperator child_op = parentheses.GetContent().AsBinaryOp().GetOp();
        ParenthesesRestrictions restrictions = BINARY_RESTRICTIONS[parent_op][child_op];
        if (restrictions == ALL || (restrictions == LEFT && left) || (restrictions == RIGHT && !left)) return child;
    }

    return parentheses.Extract();
}

TreeBuilder& TreeBuilder::AddLiteral(std::string literal) {
    node_stack_.push(Ast::Node::OfLiteral(std::move(literal)));
    return *this;
}

TreeBuilder& TreeBuilder::AddCell(const std::string& cell_name) {
    const CellParamPtr& cell_param = cell_cache_.GetOrInsert(Position::FromString(cell_name));
    node_stack_.push(Ast::Node::OfCellParamPtr(cell_param));
    return *this;
}

TreeBuilder& TreeBuilder::AddParentheses() {

    Ast::Node content = std::move(node_stack_.top());
    node_stack_.pop();

    node_stack_.push(Ast::Node::OfParentheses(std::move(content)));

    return *this;
}

TreeBuilder& TreeBuilder::AddUnaryOp(UnaryOperator op) {

    Ast::Node content = std::move(node_stack_.top());
    node_stack_.pop();

    node_stack_.push(Ast::Node::Unary(std::move(content), op));

    return *this;
}

TreeBuilder& TreeBuilder::AddBinaryOp(BinaryOperator op) {

    Ast::Node rhs = std::move(node_stack_.top());
    node_stack_.pop();

    Ast::Node lhs = std::move(node_stack_.top());
    node_stack_.pop();

    node_stack_.push(Ast::Node::Binary(std::move(lhs), std::move(rhs), op));

    return *this;
}

Ast::Tree TreeBuilder::Build() {

    Ast::Node root = std::move(node_stack_.top());
    node_stack_.pop();

    return Ast::Tree(std::move(root), std::move(cell_cache_));
}

const CellParamPtr& CellParamCache::GetOrInsert(Position position) {

    std::map<int, CellParamPtr>& row = cell_params_[position.row];

    if (auto col_it = row.find(position.col); col_it != row.end()) {
        return col_it->second;
    } else {
        auto inserted = row.emplace(position.col, std::make_shared<std::optional<Position>>(position));
        return inserted.first->second;
    }
}

size_t CellParamCache::HandleInsertedRows(int before, int count) {

    if (cell_params_.empty()) return 0;

    std::vector<int> keys_to_update;

    for (auto it = cell_params_.rbegin(); it != cell_params_.rend() && it->first >= before; it++) {
        keys_to_update.push_back(it->first);
    }

    size_t updated_cell_params_count = 0;

    for (int key: keys_to_update) {

        int updated_row = key + count;

        for (auto&[_, ptr]: cell_params_.at(key)) {

            CellParam& cell_param = *ptr;

            if (cell_param != std::nullopt) {
                cell_param->row = updated_row;
                updated_cell_params_count++;
            }
        }

        auto entry = cell_params_.extract(key);
        entry.key() = updated_row;
        cell_params_.insert(std::move(entry));
    }

    return updated_cell_params_count;
}

size_t CellParamCache::HandleInsertedCols(int before, int count) {

    size_t updated_cell_params_count = 0;

    std::vector<int> keys_to_update;

    for (auto&[_, row]: cell_params_) {

        for (auto it = row.rbegin(); it != row.rend() && it->first >= before; it++) {
            keys_to_update.push_back(it->first);
        }

        for (int key: keys_to_update) {

            CellParam& param = *row.at(key);

            if (param != std::nullopt) {
                param->col = key + count;
                updated_cell_params_count++;
            }

            auto entry = row.extract(key);
            entry.key() = key + count;
            row.insert(std::move(entry));
        }

        keys_to_update.clear();
    }

    return updated_cell_params_count;
}

std::pair<size_t, size_t> CellParamCache::HandleDeletedRows(int start, int count) {

    size_t deleted_cell_params_count = 0;
    size_t updated_cell_params_count = 0;

    std::vector<int> keys_to_delete;
    std::vector<int> keys_to_update;

    auto it = cell_params_.rbegin();

    for (; it != cell_params_.rend() && it->first >= start + count; it++) {
        keys_to_update.push_back(it->first);
    }

    for (; it != cell_params_.rend() && it->first >= start; it++) {
        keys_to_delete.push_back(it->first);
    }

    for (int key: keys_to_delete) {
        for (auto& [_, ptr]: cell_params_.at(key)) {
            ptr->reset();
            deleted_cell_params_count++;
        }

        cell_params_.erase(key);
    }

    for (int key: keys_to_update) {

        int updated_row = key - count;

        for (auto&[_, ptr]: cell_params_.at(key)) {

            CellParam& cell_param = *ptr;

            if (cell_param != std::nullopt) {
                cell_param->row = updated_row;
                updated_cell_params_count++;
            }
        }

        auto entry = cell_params_.extract(key);
        entry.key() = updated_row;
        cell_params_.insert(std::move(entry));
    }

    return std::make_pair(deleted_cell_params_count, updated_cell_params_count);
}

std::pair<size_t, size_t> CellParamCache::HandleDeletedCols(int start, int count) {

    size_t deleted_cell_params_count = 0;
    size_t updated_cell_params_count = 0;

    std::vector<int> keys_to_delete;
    std::vector<int> keys_to_update;

    for (auto&[_, row]: cell_params_) {

        auto it = row.rbegin();

        for (; it != row.rend() && it->first >= start + count; it++) {
            keys_to_update.push_back(it->first);
        }

        for (; it != row.rend() && it->first >= start; it++) {
            keys_to_delete.push_back(it->first);
        }

        for (int key: keys_to_delete) {
            CellParam& param = *row.at(key);

            if (param != std::nullopt) {
                param.reset();
                deleted_cell_params_count++;
            }

            row.erase(key);
        }

        for (int key: keys_to_update) {

            CellParam& param = *row.at(key);

            if (param != std::nullopt) {
                param->col = key - count;
                updated_cell_params_count++;
            }

            auto entry = row.extract(key);
            entry.key() = key - count;
            row.insert(std::move(entry));
        }

        keys_to_delete.clear();
        keys_to_update.clear();
    }

    return std::make_pair(deleted_cell_params_count, updated_cell_params_count);
}

std::vector<Position> CellParamCache::GetReferencedCells() const {

    std::vector<Position> result;

    for (const auto&[row_index, row]: cell_params_) {
        for (const auto&[col_index, _]: row) {
            result.push_back(Position {row_index, col_index});
        }
    }

    return result;
}

}