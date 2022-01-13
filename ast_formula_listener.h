#pragma once

#include "ast.h"
#include "FormulaListener.h"

#include <stack>

class AstFormulaListener : public FormulaListener {
private:
    Ast::TreeBuilder builder_;

public:

    ~AstFormulaListener() override = default;

    void exitUnaryOp(FormulaParser::UnaryOpContext *ctx) override {

        Ast::UnaryOperator op;

        if (ctx->SUB()) op = Ast::UnaryOperator::MINUS;
        else            op = Ast::UnaryOperator::PLUS;

        builder_.AddUnaryOp(op);
    }

    void exitParens(FormulaParser::ParensContext* ctx) override {
        builder_.AddParentheses();
    }

    void exitLiteral(FormulaParser::LiteralContext* ctx) override {
        builder_.AddLiteral(ctx->getText());
    }

    void exitCell(FormulaParser::CellContext* ctx) override {
        builder_.AddCell(ctx->getText());
    }

    void exitBinaryOp(FormulaParser::BinaryOpContext* ctx) override {

        Ast::BinaryOperator op;

        if (ctx->ADD())         op = Ast::BinaryOperator::ADD;
        else if (ctx->SUB())    op = Ast::BinaryOperator::SUB;
        else if (ctx->MUL())    op = Ast::BinaryOperator::MUL;
        else                    op = Ast::BinaryOperator::DIV;

        builder_.AddBinaryOp(op);
    }

    void visitTerminal(antlr4::tree::TerminalNode* node) override {}

    void visitErrorNode(antlr4::tree::ErrorNode* node) override {}

    void enterEveryRule(antlr4::ParserRuleContext* ctx) override {}

    void exitEveryRule(antlr4::ParserRuleContext* ctx) override {}

    void enterMain(FormulaParser::MainContext* ctx) override {}

    void exitMain(FormulaParser::MainContext* ctx) override {}

    void enterUnaryOp(FormulaParser::UnaryOpContext* ctx) override {}

    void enterParens(FormulaParser::ParensContext* ctx) override {}

    void enterLiteral(FormulaParser::LiteralContext* ctx) override {}

    void enterCell(FormulaParser::CellContext* ctx) override {}

    void enterBinaryOp(FormulaParser::BinaryOpContext* ctx) override {}

    Ast::Tree GetResult() {
        return builder_.Build();
    }

};