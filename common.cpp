#include "common.h"

#include "formula.h"

#include <stack>
#include <vector>
#include <unordered_set>
#include <memory>
#include <stack>
#include <regex>
#include <optional>

static const uint8_t ALPHABET_POWER = 26;
static const uint8_t ALPHABET_OFFSET = 0x41;
static const std::regex CELL_REGEX("([A-Z]{1,3})([1-9]\\d{0,4})");

static const std::string REF_ERROR_STR = "#REF!";
static const std::string VALUE_ERROR_STR = "#VALUE!";
static const std::string DIV_ERROR_STR = "#DIV/0!";

class Cell : public ICell {

private:
    const ISheet& sheet_;
    std::string text_;
    std::unique_ptr<IFormula> formula_;
    mutable std::optional<Value> cache_;

    std::unordered_set<Cell*> in_cells_;
    std::unordered_set<Cell*> out_cells_;

    void ClearData() {

        text_.clear();
        formula_.reset();
        cache_.reset();

        //TODO remove from incoming cells

        for (Cell* out_cell: out_cells_) {
            out_cell->RemoveFromIncoming(this);
        }

        out_cells_.clear();
    }

    void CalculateCache() const {
        if (formula_) {

            const auto& result = formula_->Evaluate(sheet_);

            if (std::holds_alternative<double>(result)) {
                cache_.emplace(std::get<double>(result));
            } else {
                cache_.emplace(std::get<FormulaError>(result));
            }
        } else if (!text_.empty() && text_.front() == kEscapeSign) {
            cache_.emplace(text_.substr(1));
        } else {
            cache_.emplace(text_);
        }
    }

public:

    explicit Cell(const ISheet& sheet)
        : sheet_(sheet),
          text_(),
          formula_(),
          cache_(std::nullopt),
          in_cells_(),
          out_cells_() {}

    ~Cell() override = default;

    void SetFormula(std::string text, std::unique_ptr<IFormula> formula, std::unordered_set<Cell*> out_cells) {

        ClearData();

        text_ = std::move(text);
        formula_ = std::move(formula);
        out_cells_ = std::move(out_cells);

        for (Cell* out_cell: out_cells_) {
            out_cell->AddIncomingCell(this);
        }
    }

    void SetPlainText(std::string text) {
        ClearData();
        text_ = std::move(text);
    }

    Value GetValue() const override {

        if (cache_ == std::nullopt) {
            CalculateCache();
        }

        return *cache_;
    }

    bool HasCache() const {
        return cache_ != std::nullopt;
    }

    void InvalidateCache() {
        cache_.reset();
    }

    std::string GetText() const override {
        return text_;
    }

    std::vector<Position> GetReferencedCells() const override {
        return formula_ ? formula_->GetReferencedCells() : std::vector<Position>();
    }

    void AddIncomingCell(Cell* cell) {
        in_cells_.insert(cell);
    }

    void RemoveFromIncoming(Cell* cell) {
        in_cells_.erase(cell);
    }

    const std::unordered_set<Cell*>& GetInCells() const {
        return in_cells_;
    }

    const std::unordered_set<Cell*>& GetOutCells() const {
        return out_cells_;
    }

};


class Sheet : public ISheet {

private:
    std::vector<std::vector<std::unique_ptr<Cell>>> cells_;

    size_t Rows() const {
        return cells_.size();
    }

    size_t Cols() const {
        return !cells_.empty() ? cells_.front().size() : 0;
    }

    bool OutOfRange(Position pos) const {
        return pos.row >= Rows() || pos.col >= Cols();
    }

    void ResizeRows(size_t row_index) {
        if (Rows() <= row_index) {

            size_t old_rows = Rows();

            cells_.resize(row_index + 1);

            for (size_t i = old_rows; i < cells_.size(); ++i) {
                cells_[i].resize(Cols());
            }
        }
    }

    void ResizeCols(size_t col_index) {
        if (Cols() <= col_index) {
            for (auto& row: cells_) {
                row.resize(col_index + 1);
            }
        }
    }

    void Resize(Position pos) {
        ResizeRows(pos.row);
        ResizeCols(pos.col);
    }

    static std::unordered_set<const Cell*> TransitiveReferencedCells(const std::unordered_set<Cell*>& formula_ref_cells) {

        std::stack<const Cell*> stack;

        for (Cell* ref_cell: formula_ref_cells) {
            stack.push(ref_cell);
        }

        std::unordered_set<const Cell*> result;

        while (!stack.empty()) {

            const Cell* current_cell = stack.top();
            stack.pop();

            if (result.count(current_cell) == 0) {

                result.insert(current_cell);

                for (const Cell* out_cell: current_cell->GetOutCells()) {
                    if (result.count(out_cell) == 0) stack.push(out_cell);
                }
            }
        }

        return result;
    }

    void InvalidateCache(Cell& cell) {

        std::unordered_set<Cell*> visited;

        std::stack<Cell*> stack;

        stack.push(&cell);

        while (!stack.empty()) {

            Cell* current_cell = stack.top();
            stack.pop();

            if (visited.count(current_cell) > 0) continue;

            visited.insert(current_cell);

            if (current_cell->HasCache()) {

                current_cell->InvalidateCache();

                for (Cell* in_cell: current_cell->GetInCells()) {
                    if (visited.count(in_cell) == 0) stack.push(in_cell);
                }
            }
        }
    }

public:

    ~Sheet() override = default;

    void SetCell(Position pos, std::string text) override {

        Cell& cell = GetOrCreateCell(pos);

        if (!text.empty() && text.front() == kFormulaSign) {

            std::unique_ptr<IFormula> formula = ParseFormula(text.substr(1));

            //update dependency graph
            std::unordered_set<Cell*> out_cells;

            for (Position ref_pos: formula->GetReferencedCells()) {
                out_cells.insert(&GetOrCreateCell(ref_pos));
            }

            std::unordered_set<const Cell*> transitive_ref_cells = TransitiveReferencedCells(out_cells);

            if (transitive_ref_cells.count(&cell) > 0) {
                throw CircularDependencyException("circular dependency exception");
            }

            InvalidateCache(cell);

            cell.SetFormula(std::move(text), std::move(formula), out_cells);
        } else {
            InvalidateCache(cell);
            cell.SetPlainText(std::move(text));
        }
    }

    Cell& GetOrCreateCell(Position pos) {

        if (!pos.IsValid()) throw InvalidPositionException("invalid position: " + pos.ToString());

        if (OutOfRange(pos)) {
            Resize(pos);
        }

        std::unique_ptr<Cell>& cell = cells_[pos.row][pos.col];

        if (cell == nullptr) {
            cell = std::make_unique<Cell>(*this);
        }

        return *cell;
    }

    const ICell* GetCell(Position pos) const override {

        if (!pos.IsValid()) throw InvalidPositionException("invalid position: " + pos.ToString());

        if (OutOfRange(pos)) return nullptr;

        const auto& cell = cells_[pos.row][pos.col];
        return cell ? cell.get() : nullptr;
    }

    ICell* GetCell(Position pos) override {

        if (!pos.IsValid()) throw InvalidPositionException("invalid position: " + pos.ToString());

        if (OutOfRange(pos)) return nullptr;

        auto& cell = cells_[pos.row][pos.col];
        return cell ? cell.get() : nullptr;
    }

    void ClearCell(Position pos) override {
        if (OutOfRange(pos)) return;
        cells_[pos.row][pos.col].reset();
    }

    void InsertRows(int before, int count) override {
        if (Rows() + count > Position::kMaxRows) throw TableTooBigException("table too big");
    }

    void InsertCols(int before, int count) override {
        if (Cols() + count > Position::kMaxCols) throw TableTooBigException("table too big");
    }

    void DeleteRows(int first, int count) override {

    }

    void DeleteCols(int first, int count) override {

    }

    Size GetPrintableSize() const override {
        return Size {static_cast<int>(Rows()), static_cast<int>(Cols())};
    }

    void PrintValues(std::ostream& output) const override {

    }

    void PrintTexts(std::ostream& output) const override {

    }


};


bool Position::operator==(const Position& rhs) const {
    return row == rhs.row && col == rhs.col;
}


bool Position::operator<(const Position& rhs) const {
    return std::tie(row, col) < std::tie(rhs.row, rhs.col);
}

bool Position::IsValid() const {
    return 0 <= row && row < kMaxRows && 0 <= col && col < kMaxCols;
}

std::string Position::ToString() const {

    if (row < 0 || col < 0) return "";

    std::stack<char> chars;

    int current_col = col;

    while (current_col >= ALPHABET_POWER) {
        chars.push(ALPHABET_OFFSET + current_col % ALPHABET_POWER);
        current_col = current_col / ALPHABET_POWER - 1;
    }

    chars.push(current_col + ALPHABET_OFFSET);

    std::string string_position;

    while (!chars.empty()) {
        string_position += chars.top();
        chars.pop();
    }

    string_position += std::to_string(row + 1);

    return string_position;
}

Position Position::FromString(std::string_view str) {

    std::match_results<std::string_view::const_iterator> match;

    if (std::regex_match(str.begin(), str.end(), match, CELL_REGEX)) {
        if (match.size() == 3) {

            std::string col_part = match[1].str();
            std::string row_part = match[2].str();


            int col = 0;

            auto it = col_part.rbegin();
            col += *it - ALPHABET_OFFSET;
            it++;

            size_t multiplier = ALPHABET_POWER;

            for (; it != col_part.rend(); ++it) {
                col += (*it - ALPHABET_OFFSET + 1) * multiplier;
                multiplier *= ALPHABET_POWER;
            }

            int row = std::stoi(row_part) - 1;

            if (row < Position::kMaxRows && col < Position::kMaxCols) {
                return Position {row, col};
            }
        }
    }

    return Position {-1, -1};
}

bool Size::operator==(const Size& rhs) const {
    return cols == rhs.cols && rows == rhs.rows;
}

std::ostream& operator<<(std::ostream& output, FormulaError fe) {
    return output << fe.ToString();
}

FormulaError::FormulaError(FormulaError::Category category) : category_(category) {}

FormulaError::Category FormulaError::GetCategory() const {
    return category_;
}

bool FormulaError::operator==(FormulaError rhs) const {
    return category_ == rhs.category_;
}

std::string_view FormulaError::ToString() const {
    switch (category_) {
        case Category::Value:
            return VALUE_ERROR_STR;
        case Category::Ref:
            return REF_ERROR_STR;
        case Category::Div0:
            return DIV_ERROR_STR;
    }
}

std::unique_ptr<ISheet> CreateSheet() {
    return std::make_unique<Sheet>();
}