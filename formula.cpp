#include "formula.h"
#include "ast_formula_listener.h"
#include "FormulaLexer.h"

#include <set>

class Formula : public IFormula {

private:
    Ast::Tree tree_;

public:

    explicit Formula(Ast::Tree tree): tree_(std::move(tree)) {}

    Value Evaluate(const ISheet& sheet) const override {
        return tree_.Evaluate(sheet);
    }

    std::string GetExpression() const override {
        return tree_.BuildExpression();
    }

    std::vector<Position> GetReferencedCells() const override {
        return tree_.GetReferencedCells();
    }

    HandlingResult HandleInsertedRows(int before, int count) override {
        size_t updated_cell_params = tree_.HandleInsertedRows(before, count);
        return updated_cell_params > 0 ? HandlingResult::ReferencesRenamedOnly : HandlingResult::NothingChanged;
    }

    HandlingResult HandleInsertedCols(int before, int count) override {
        size_t updated_cell_params = tree_.HandleInsertedCols(before, count);
        return updated_cell_params > 0 ? HandlingResult::ReferencesRenamedOnly : HandlingResult::NothingChanged;
    }

    HandlingResult HandleDeletedRows(int first, int count) override {

        auto result = tree_.HandleDeletedRows(first, count);

        if (result.first > 0) return HandlingResult::ReferencesChanged;
        return result.second > 0 ? HandlingResult::ReferencesRenamedOnly : HandlingResult::NothingChanged;
    }

    HandlingResult HandleDeletedCols(int first, int count) override {

        auto result = tree_.HandleDeletedCols(first, count);

        if (result.first > 0) return HandlingResult::ReferencesChanged;
        return result.second > 0 ? HandlingResult::ReferencesRenamedOnly : HandlingResult::NothingChanged;
    }

};

class BailErrorListener : public antlr4::BaseErrorListener {
public:
    void syntaxError(
        antlr4::Recognizer* /* recognizer */,
        antlr4::Token* /* offendingSymbol */,
        size_t /* line */,
        size_t /* charPositionInLine */,
        const std::string& msg,
        std::exception_ptr /* e */
    ) override {
        throw std::runtime_error("Error when lexing: " + msg);
    }
};

std::unique_ptr<IFormula> ParseFormula(std::string expression) {

    try {

        antlr4::ANTLRInputStream input(expression);

        FormulaLexer lexer(&input);
        BailErrorListener error_listener;
        lexer.removeErrorListeners();
        lexer.addErrorListener(&error_listener);

        antlr4::CommonTokenStream tokens(&lexer);
        FormulaParser parser(&tokens);
        auto error_handler = std::make_shared<antlr4::BailErrorStrategy>();
        parser.setErrorHandler(error_handler);
        parser.removeErrorListeners();

        antlr4::tree::ParseTree* tree = parser.main();
        AstFormulaListener listener;
        antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);

        return std::make_unique<Formula>(listener.GetResult());
    } catch (const std::exception& e) {
        throw FormulaException(e.what());
    }
}

