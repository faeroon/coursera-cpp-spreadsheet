#include "common.h"

#include "formula.h"

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
    mutable std::optional<Value> value_;

    void UpdateValue() const {
        if (formula_) {

            const auto& result = formula_->Evaluate(sheet_);

            if (std::holds_alternative<double>(result)) {
                value_.emplace(std::get<double>(result));
            } else {
                value_.emplace(std::get<FormulaError>(result));
            }
        } else if (!text_.empty() && text_.front() == '\'') {
            value_.emplace(text_.substr(1));
        } else {
            value_.emplace(text_);
        }
    }

public:

    Cell(const ISheet& sheet, std::string text): sheet_(sheet), text_(std::move(text)), value_(std::nullopt) {
        if (!text_.empty() && text_.front() == '=') {
            formula_ = ParseFormula(text_.substr(1));
        }
    }

    ~Cell() override = default;

    Value GetValue() const override {

        if (value_ == std::nullopt) {
            UpdateValue();
        }

        return *value_;
    }

    std::string GetText() const override {
        return text_;
    }

    void SetText(std::string text) {
        if (text_ != text) {

            value_.reset();
            formula_.reset();

            text_ = std::move(text);

            if (!text_.empty() && text_.front() == '=') {
                formula_ = ParseFormula(text_.substr(1));
            }

//            UpdateValue();
        }
    }

    std::vector<Position> GetReferencedCells() const override {
        return formula_ ? formula_->GetReferencedCells() : std::vector<Position>();
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

public:

    void SetCell(Position pos, std::string text) override {

        if (!pos.IsValid()) throw InvalidPositionException("invalid position: " + pos.ToString());

        Resize(pos);

        std::unique_ptr<Cell>& cell = cells_[pos.row][pos.col];

        if (cell == nullptr) {
            cell = std::make_unique<Cell>(*this, std::move(text));
        } else {
            cell->SetText(std::move(text));
        }
    }

    const ICell* GetCell(Position pos) const override {
        if (!pos.IsValid() || OutOfRange(pos)) return nullptr;
        const auto& cell = cells_[pos.row][pos.col];
        return cell ? cell.get() : nullptr;
    }

    ICell* GetCell(Position pos) override {
        if (!pos.IsValid() || OutOfRange(pos)) return nullptr;
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

    return Position{-1, -1};
}

bool Size::operator==(const Size& rhs) const {
    return cols == rhs.cols && rows == rhs.rows;
}

std::ostream& operator<<(std::ostream& output, FormulaError fe) {
    return output << fe.ToString();
}

FormulaError::FormulaError(FormulaError::Category category): category_(category) {}

FormulaError::Category FormulaError::GetCategory() const {
    return category_;
}

bool FormulaError::operator==(FormulaError rhs) const {
    return category_ == rhs.category_;
}

std::string_view FormulaError::ToString() const {
    switch (category_) {
        case Category::Value: return VALUE_ERROR_STR;
        case Category::Ref: return REF_ERROR_STR;
        case Category::Div0: return DIV_ERROR_STR;
    }
}

std::unique_ptr<ISheet> CreateSheet() {
    return std::make_unique<Sheet>();
}
