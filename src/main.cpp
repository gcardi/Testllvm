#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

#ifndef BASIC_RUNTIME_SOURCE
#define BASIC_RUNTIME_SOURCE "runtime/basic_runtime.c"
#endif

std::string upper(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

bool isStringName(const std::string &name) {
  return !name.empty() && name.back() == '$';
}

enum class TokenKind {
  End,
  Ident,
  Number,
  String,
  Plus,
  Minus,
  Star,
  Slash,
  LParen,
  RParen,
  Equal,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  NotEqual,
  Colon,
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  int line = 0;
};

class Lexer {
public:
  Lexer(std::string line, int lineNo) : line_(std::move(line)), lineNo_(lineNo) {}

  std::vector<Token> lex() {
    std::vector<Token> tokens;
    while (true) {
      skipSpace();
      if (pos_ >= line_.size()) {
        tokens.push_back({TokenKind::End, "", lineNo_});
        return tokens;
      }

      char c = line_[pos_];
      if (c == '\'') {
        tokens.push_back({TokenKind::End, "", lineNo_});
        return tokens;
      }

      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        tokens.push_back(lexIdent());
        continue;
      }

      if (std::isdigit(static_cast<unsigned char>(c))) {
        tokens.push_back(lexNumber());
        continue;
      }

      if (c == '"') {
        tokens.push_back(lexString());
        continue;
      }

      ++pos_;
      switch (c) {
      case '+':
        tokens.push_back({TokenKind::Plus, "+", lineNo_});
        break;
      case '-':
        tokens.push_back({TokenKind::Minus, "-", lineNo_});
        break;
      case '*':
        tokens.push_back({TokenKind::Star, "*", lineNo_});
        break;
      case '/':
        tokens.push_back({TokenKind::Slash, "/", lineNo_});
        break;
      case '(':
        tokens.push_back({TokenKind::LParen, "(", lineNo_});
        break;
      case ')':
        tokens.push_back({TokenKind::RParen, ")", lineNo_});
        break;
      case ':':
        tokens.push_back({TokenKind::Colon, ":", lineNo_});
        break;
      case '=':
        tokens.push_back({TokenKind::Equal, "=", lineNo_});
        break;
      case '<':
        if (match('=')) {
          tokens.push_back({TokenKind::LessEqual, "<=", lineNo_});
        } else if (match('>')) {
          tokens.push_back({TokenKind::NotEqual, "<>", lineNo_});
        } else {
          tokens.push_back({TokenKind::Less, "<", lineNo_});
        }
        break;
      case '>':
        if (match('=')) {
          tokens.push_back({TokenKind::GreaterEqual, ">=", lineNo_});
        } else {
          tokens.push_back({TokenKind::Greater, ">", lineNo_});
        }
        break;
      default:
        throw std::runtime_error("line " + std::to_string(lineNo_) +
                                 ": unexpected character '" + std::string(1, c) + "'");
      }
    }
  }

private:
  void skipSpace() {
    while (pos_ < line_.size() &&
           std::isspace(static_cast<unsigned char>(line_[pos_]))) {
      ++pos_;
    }
  }

  bool match(char c) {
    if (pos_ < line_.size() && line_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  Token lexIdent() {
    size_t start = pos_;
    while (pos_ < line_.size()) {
      char c = line_[pos_];
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '$') {
        break;
      }
      ++pos_;
    }
    return {TokenKind::Ident, upper(line_.substr(start, pos_ - start)), lineNo_};
  }

  Token lexNumber() {
    size_t start = pos_;
    while (pos_ < line_.size() &&
           std::isdigit(static_cast<unsigned char>(line_[pos_]))) {
      ++pos_;
    }
    return {TokenKind::Number, line_.substr(start, pos_ - start), lineNo_};
  }

  Token lexString() {
    ++pos_;
    std::string out;
    while (pos_ < line_.size()) {
      char c = line_[pos_++];
      if (c == '"') {
        return {TokenKind::String, out, lineNo_};
      }
      if (c == '\\' && pos_ < line_.size()) {
        char escaped = line_[pos_++];
        if (escaped == 'n') {
          out.push_back('\n');
        } else if (escaped == 't') {
          out.push_back('\t');
        } else {
          out.push_back(escaped);
        }
      } else {
        out.push_back(c);
      }
    }
    throw std::runtime_error("line " + std::to_string(lineNo_) +
                             ": unterminated string literal");
  }

  std::string line_;
  int lineNo_ = 0;
  size_t pos_ = 0;
};

enum class ExprKind { IntegerLiteral, StringLiteral, Variable, Binary };

struct Expr {
  ExprKind kind;
  int line = 0;
  int intValue = 0;
  std::string text;
  char op = 0;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
};

enum class StmtKind { Let, Input, Print, IfGoto, Goto, End };

struct Stmt {
  StmtKind kind;
  int line = 0;
  std::vector<std::string> labels;
  std::string name;
  std::string target;
  TokenKind compare = TokenKind::End;
  std::unique_ptr<Expr> expr;
  std::unique_ptr<Expr> rhs;
};

class Parser {
public:
  explicit Parser(std::string source) : source_(std::move(source)) {}

  std::vector<Stmt> parse() {
    std::vector<Stmt> stmts;
    std::vector<std::string> pendingLabels;
    std::istringstream in(source_);
    std::string line;
    int lineNo = 1;

    while (std::getline(in, line)) {
      std::string trimmed = trim(line);
      if (trimmed.empty() || startsWithRem(trimmed)) {
        ++lineNo;
        continue;
      }

      Lexer lexer(trimmed, lineNo);
      tokens_ = lexer.lex();
      pos_ = 0;

      if (peek().kind == TokenKind::Ident && peek(1).kind == TokenKind::Colon &&
          peek(2).kind == TokenKind::End) {
        std::string label = consume(TokenKind::Ident, "label").text;
        consume(TokenKind::Colon, ":");
        pendingLabels.push_back(label);
        ++lineNo;
        continue;
      }

      Stmt stmt = parseStmt();
      stmt.labels = std::move(pendingLabels);
      pendingLabels.clear();
      stmts.push_back(std::move(stmt));
      ++lineNo;
    }

    if (!pendingLabels.empty()) {
      throw std::runtime_error("label '" + pendingLabels.back() +
                               "' is not followed by a statement");
    }
    if (stmts.empty()) {
      throw std::runtime_error("program is empty");
    }
    return stmts;
  }

private:
  static std::string trim(const std::string &s) {
    size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
      ++first;
    }
    size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) {
      --last;
    }
    return s.substr(first, last - first);
  }

  static bool startsWithRem(const std::string &s) {
    if (s.size() < 3) {
      return false;
    }
    std::string prefix = upper(s.substr(0, 3));
    return prefix == "REM" && (s.size() == 3 || std::isspace(static_cast<unsigned char>(s[3])));
  }

  const Token &peek(size_t offset = 0) const {
    size_t i = pos_ + offset;
    if (i >= tokens_.size()) {
      return tokens_.back();
    }
    return tokens_[i];
  }

  bool isKeyword(const std::string &kw, size_t offset = 0) const {
    return peek(offset).kind == TokenKind::Ident && peek(offset).text == kw;
  }

  Token consume(TokenKind kind, const std::string &what) {
    if (peek().kind != kind) {
      throw std::runtime_error("line " + std::to_string(peek().line) +
                               ": expected " + what);
    }
    return tokens_[pos_++];
  }

  void consumeKeyword(const std::string &kw) {
    if (!isKeyword(kw)) {
      throw std::runtime_error("line " + std::to_string(peek().line) +
                               ": expected " + kw);
    }
    ++pos_;
  }

  Stmt parseStmt() {
    int line = peek().line;
    if (isKeyword("LET")) {
      consumeKeyword("LET");
      Stmt stmt{StmtKind::Let, line};
      stmt.name = consume(TokenKind::Ident, "variable name").text;
      consume(TokenKind::Equal, "=");
      stmt.expr = parseExpression();
      consume(TokenKind::End, "end of line");
      return stmt;
    }

    if (isKeyword("INPUT")) {
      consumeKeyword("INPUT");
      Stmt stmt{StmtKind::Input, line};
      stmt.name = consume(TokenKind::Ident, "variable name").text;
      consume(TokenKind::End, "end of line");
      return stmt;
    }

    if (isKeyword("PRINT")) {
      consumeKeyword("PRINT");
      Stmt stmt{StmtKind::Print, line};
      stmt.expr = parseExpression();
      consume(TokenKind::End, "end of line");
      return stmt;
    }

    if (isKeyword("IF")) {
      consumeKeyword("IF");
      Stmt stmt{StmtKind::IfGoto, line};
      stmt.expr = parseExpression();
      stmt.compare = consumeComparison();
      stmt.rhs = parseExpression();
      consumeKeyword("GOTO");
      stmt.target = consume(TokenKind::Ident, "label").text;
      consume(TokenKind::End, "end of line");
      return stmt;
    }

    if (isKeyword("GOTO")) {
      consumeKeyword("GOTO");
      Stmt stmt{StmtKind::Goto, line};
      stmt.target = consume(TokenKind::Ident, "label").text;
      consume(TokenKind::End, "end of line");
      return stmt;
    }

    if (isKeyword("END")) {
      consumeKeyword("END");
      Stmt stmt{StmtKind::End, line};
      consume(TokenKind::End, "end of line");
      return stmt;
    }

    throw std::runtime_error("line " + std::to_string(line) + ": unknown statement");
  }

  TokenKind consumeComparison() {
    TokenKind kind = peek().kind;
    switch (kind) {
    case TokenKind::Equal:
    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
    case TokenKind::NotEqual:
      ++pos_;
      return kind;
    default:
      throw std::runtime_error("line " + std::to_string(peek().line) +
                               ": expected comparison operator");
    }
  }

  std::unique_ptr<Expr> parseExpression() { return parseAdditive(); }

  std::unique_ptr<Expr> parseAdditive() {
    auto lhs = parseMultiplicative();
    while (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus) {
      Token op = tokens_[pos_++];
      auto rhs = parseMultiplicative();
      auto expr = std::make_unique<Expr>();
      expr->kind = ExprKind::Binary;
      expr->line = op.line;
      expr->op = op.kind == TokenKind::Plus ? '+' : '-';
      expr->lhs = std::move(lhs);
      expr->rhs = std::move(rhs);
      lhs = std::move(expr);
    }
    return lhs;
  }

  std::unique_ptr<Expr> parseMultiplicative() {
    auto lhs = parsePrimary();
    while (peek().kind == TokenKind::Star || peek().kind == TokenKind::Slash) {
      Token op = tokens_[pos_++];
      auto rhs = parsePrimary();
      auto expr = std::make_unique<Expr>();
      expr->kind = ExprKind::Binary;
      expr->line = op.line;
      expr->op = op.kind == TokenKind::Star ? '*' : '/';
      expr->lhs = std::move(lhs);
      expr->rhs = std::move(rhs);
      lhs = std::move(expr);
    }
    return lhs;
  }

  std::unique_ptr<Expr> parsePrimary() {
    Token tok = peek();
    if (tok.kind == TokenKind::Number) {
      ++pos_;
      auto expr = std::make_unique<Expr>();
      expr->kind = ExprKind::IntegerLiteral;
      expr->line = tok.line;
      expr->intValue = std::stoi(tok.text);
      return expr;
    }

    if (tok.kind == TokenKind::String) {
      ++pos_;
      auto expr = std::make_unique<Expr>();
      expr->kind = ExprKind::StringLiteral;
      expr->line = tok.line;
      expr->text = tok.text;
      return expr;
    }

    if (tok.kind == TokenKind::Ident) {
      ++pos_;
      auto expr = std::make_unique<Expr>();
      expr->kind = ExprKind::Variable;
      expr->line = tok.line;
      expr->text = tok.text;
      return expr;
    }

    if (tok.kind == TokenKind::LParen) {
      consume(TokenKind::LParen, "(");
      auto expr = parseExpression();
      consume(TokenKind::RParen, ")");
      return expr;
    }

    throw std::runtime_error("line " + std::to_string(tok.line) +
                             ": expected expression");
  }

  std::string source_;
  std::vector<Token> tokens_;
  size_t pos_ = 0;
};

enum class ValueKind { Int, String };

enum class TargetKind { Linux64, Win64 };

std::string targetTriple(TargetKind target) {
  if (target == TargetKind::Win64) {
    return "x86_64-w64-windows-gnu";
  }
  return llvm::sys::getDefaultTargetTriple();
}

struct TypedValue {
  llvm::Value *value = nullptr;
  ValueKind kind = ValueKind::Int;
};

struct Variable {
  llvm::AllocaInst *alloca = nullptr;
  ValueKind kind = ValueKind::Int;
};

class IRGenerator {
public:
  explicit IRGenerator(std::vector<Stmt> stmts, std::string triple)
      : stmts_(std::move(stmts)), module_(std::make_unique<llvm::Module>("basic", context_)),
        builder_(context_) {
    module_->setTargetTriple(std::move(triple));
  }

  std::unique_ptr<llvm::Module> generate() {
    declareRuntime();

    auto *mainTy = llvm::FunctionType::get(i32Ty(), false);
    main_ = llvm::Function::Create(mainTy, llvm::Function::ExternalLinkage, "main",
                                   module_.get());

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", main_);
    std::vector<llvm::BasicBlock *> blocks;
    blocks.reserve(stmts_.size());
    for (size_t i = 0; i < stmts_.size(); ++i) {
      blocks.push_back(llvm::BasicBlock::Create(context_, blockName(i), main_));
    }
    exitBlock_ = llvm::BasicBlock::Create(context_, "exit", main_);

    buildLabelMap(blocks);

    builder_.SetInsertPoint(entry);
    prepareVariables();
    builder_.CreateBr(blocks.empty() ? exitBlock_ : blocks[0]);

    for (size_t i = 0; i < stmts_.size(); ++i) {
      builder_.SetInsertPoint(blocks[i]);
      emitStmt(stmts_[i], nextBlock(blocks, i));
      if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateBr(nextBlock(blocks, i));
      }
    }

    builder_.SetInsertPoint(exitBlock_);
    builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));

    if (llvm::verifyFunction(*main_, &llvm::errs())) {
      throw std::runtime_error("generated invalid LLVM function");
    }
    if (llvm::verifyModule(*module_, &llvm::errs())) {
      throw std::runtime_error("generated invalid LLVM module");
    }
    return std::move(module_);
  }

private:
  llvm::Type *i32Ty() { return llvm::Type::getInt32Ty(context_); }
  llvm::Type *voidTy() { return llvm::Type::getVoidTy(context_); }
  llvm::PointerType *ptrTy() { return llvm::PointerType::get(context_, 0); }

  std::string blockName(size_t index) const {
    if (!stmts_[index].labels.empty()) {
      return "label." + stmts_[index].labels.front();
    }
    return "stmt." + std::to_string(index);
  }

  llvm::BasicBlock *nextBlock(const std::vector<llvm::BasicBlock *> &blocks,
                              size_t index) {
    if (index + 1 < blocks.size()) {
      return blocks[index + 1];
    }
    return exitBlock_;
  }

  void declareRuntime() {
    inputI32_ = declare("basic_input_i32", i32Ty(), {});
    inputString_ = declare("basic_input_string", ptrTy(), {});
    concat_ = declare("basic_concat", ptrTy(), {ptrTy(), ptrTy()});
    printI32_ = declare("basic_print_i32", voidTy(), {i32Ty()});
    printString_ = declare("basic_print_string", voidTy(), {ptrTy()});
  }

  llvm::Function *declare(const std::string &name, llvm::Type *ret,
                          std::vector<llvm::Type *> args) {
    auto *type = llvm::FunctionType::get(ret, args, false);
    return llvm::Function::Create(type, llvm::Function::ExternalLinkage, name,
                                  module_.get());
  }

  void buildLabelMap(const std::vector<llvm::BasicBlock *> &blocks) {
    for (size_t i = 0; i < stmts_.size(); ++i) {
      for (const auto &label : stmts_[i].labels) {
        if (!labels_.emplace(label, blocks[i]).second) {
          throw std::runtime_error("duplicate label '" + label + "'");
        }
      }
    }

    for (const auto &stmt : stmts_) {
      if ((stmt.kind == StmtKind::Goto || stmt.kind == StmtKind::IfGoto) &&
          labels_.find(stmt.target) == labels_.end()) {
        throw std::runtime_error("line " + std::to_string(stmt.line) +
                                 ": unknown label '" + stmt.target + "'");
      }
    }
  }

  Variable &getVariable(const std::string &name) {
    auto it = variables_.find(name);
    if (it == variables_.end()) {
      throw std::runtime_error("internal error: undeclared variable '" + name + "'");
    }
    return it->second;
  }

  void declareVariable(const std::string &name) {
    if (variables_.find(name) != variables_.end()) {
      return;
    }
    ValueKind kind = isStringName(name) ? ValueKind::String : ValueKind::Int;
    llvm::IRBuilder<> entryBuilder(&main_->getEntryBlock(),
                                   main_->getEntryBlock().begin());
    llvm::AllocaInst *alloca =
        entryBuilder.CreateAlloca(kind == ValueKind::Int ? i32Ty() : ptrTy(),
                                  nullptr, name);

    if (kind == ValueKind::Int) {
      builder_.CreateStore(llvm::ConstantInt::get(i32Ty(), 0), alloca);
    } else {
      builder_.CreateStore(builder_.CreateGlobalString("", "empty"), alloca);
    }

    variables_.emplace(name, Variable{alloca, kind});
  }

  void collectVariables(const Expr &expr, std::vector<std::string> &names) {
    switch (expr.kind) {
    case ExprKind::Variable:
      names.push_back(expr.text);
      return;
    case ExprKind::Binary:
      collectVariables(*expr.lhs, names);
      collectVariables(*expr.rhs, names);
      return;
    case ExprKind::IntegerLiteral:
    case ExprKind::StringLiteral:
      return;
    }
  }

  void prepareVariables() {
    std::vector<std::string> names;
    for (const Stmt &stmt : stmts_) {
      if (!stmt.name.empty()) {
        names.push_back(stmt.name);
      }
      if (stmt.expr) {
        collectVariables(*stmt.expr, names);
      }
      if (stmt.rhs) {
        collectVariables(*stmt.rhs, names);
      }
    }

    for (const auto &name : names) {
      declareVariable(name);
    }
  }

  TypedValue emitExpr(const Expr &expr) {
    switch (expr.kind) {
    case ExprKind::IntegerLiteral:
      return {llvm::ConstantInt::get(i32Ty(), expr.intValue), ValueKind::Int};
    case ExprKind::StringLiteral:
      return {builder_.CreateGlobalString(expr.text), ValueKind::String};
    case ExprKind::Variable: {
      Variable &var = getVariable(expr.text);
      return {builder_.CreateLoad(var.kind == ValueKind::Int ? i32Ty() : ptrTy(),
                                  var.alloca, expr.text + ".load"),
              var.kind};
    }
    case ExprKind::Binary:
      return emitBinary(expr);
    }
    throw std::runtime_error("unreachable expression kind");
  }

  TypedValue emitBinary(const Expr &expr) {
    TypedValue lhs = emitExpr(*expr.lhs);
    TypedValue rhs = emitExpr(*expr.rhs);

    if (lhs.kind == ValueKind::String || rhs.kind == ValueKind::String) {
      if (expr.op != '+' || lhs.kind != ValueKind::String ||
          rhs.kind != ValueKind::String) {
        throw std::runtime_error("line " + std::to_string(expr.line) +
                                 ": strings only support string + string");
      }
      return {builder_.CreateCall(concat_, {lhs.value, rhs.value}, "strcat"),
              ValueKind::String};
    }

    switch (expr.op) {
    case '+':
      return {builder_.CreateAdd(lhs.value, rhs.value, "addtmp"), ValueKind::Int};
    case '-':
      return {builder_.CreateSub(lhs.value, rhs.value, "subtmp"), ValueKind::Int};
    case '*':
      return {builder_.CreateMul(lhs.value, rhs.value, "multmp"), ValueKind::Int};
    case '/':
      return {builder_.CreateSDiv(lhs.value, rhs.value, "divtmp"), ValueKind::Int};
    default:
      throw std::runtime_error("line " + std::to_string(expr.line) +
                               ": unsupported binary operator");
    }
  }

  llvm::Value *emitCompare(const Stmt &stmt) {
    TypedValue lhs = emitExpr(*stmt.expr);
    TypedValue rhs = emitExpr(*stmt.rhs);
    if (lhs.kind != ValueKind::Int || rhs.kind != ValueKind::Int) {
      throw std::runtime_error("line " + std::to_string(stmt.line) +
                               ": IF comparisons support integers only");
    }

    switch (stmt.compare) {
    case TokenKind::Equal:
      return builder_.CreateICmpEQ(lhs.value, rhs.value, "cmptmp");
    case TokenKind::Less:
      return builder_.CreateICmpSLT(lhs.value, rhs.value, "cmptmp");
    case TokenKind::Greater:
      return builder_.CreateICmpSGT(lhs.value, rhs.value, "cmptmp");
    case TokenKind::LessEqual:
      return builder_.CreateICmpSLE(lhs.value, rhs.value, "cmptmp");
    case TokenKind::GreaterEqual:
      return builder_.CreateICmpSGE(lhs.value, rhs.value, "cmptmp");
    case TokenKind::NotEqual:
      return builder_.CreateICmpNE(lhs.value, rhs.value, "cmptmp");
    default:
      throw std::runtime_error("line " + std::to_string(stmt.line) +
                               ": invalid comparison operator");
    }
  }

  void emitStmt(const Stmt &stmt, llvm::BasicBlock *fallthrough) {
    switch (stmt.kind) {
    case StmtKind::Let: {
      TypedValue value = emitExpr(*stmt.expr);
      Variable &var = getVariable(stmt.name);
      if (var.kind != value.kind) {
        throw std::runtime_error("line " + std::to_string(stmt.line) +
                                 ": type mismatch assigning to '" + stmt.name + "'");
      }
      builder_.CreateStore(value.value, var.alloca);
      return;
    }
    case StmtKind::Input: {
      Variable &var = getVariable(stmt.name);
      llvm::Value *value =
          var.kind == ValueKind::Int ? builder_.CreateCall(inputI32_)
                                     : builder_.CreateCall(inputString_);
      builder_.CreateStore(value, var.alloca);
      return;
    }
    case StmtKind::Print: {
      TypedValue value = emitExpr(*stmt.expr);
      if (value.kind == ValueKind::Int) {
        builder_.CreateCall(printI32_, {value.value});
      } else {
        builder_.CreateCall(printString_, {value.value});
      }
      return;
    }
    case StmtKind::IfGoto:
      builder_.CreateCondBr(emitCompare(stmt), labels_.at(stmt.target), fallthrough);
      return;
    case StmtKind::Goto:
      builder_.CreateBr(labels_.at(stmt.target));
      return;
    case StmtKind::End:
      builder_.CreateBr(exitBlock_);
      return;
    }
  }

  std::vector<Stmt> stmts_;
  llvm::LLVMContext context_;
  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;
  llvm::Function *main_ = nullptr;
  llvm::BasicBlock *exitBlock_ = nullptr;
  llvm::Function *inputI32_ = nullptr;
  llvm::Function *inputString_ = nullptr;
  llvm::Function *concat_ = nullptr;
  llvm::Function *printI32_ = nullptr;
  llvm::Function *printString_ = nullptr;
  std::map<std::string, Variable> variables_;
  std::map<std::string, llvm::BasicBlock *> labels_;
};

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open input file: " + path);
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

void writeModule(llvm::Module &module, const std::string &path) {
  std::error_code ec;
  llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_Text);
  if (ec) {
    throw std::runtime_error("cannot open output file: " + ec.message());
  }
  module.print(out, nullptr);
}

void optimizeModule(llvm::Module &module, int level) {
  if (level == 0) {
    return;
  }

  llvm::LoopAnalysisManager loopAM;
  llvm::FunctionAnalysisManager functionAM;
  llvm::CGSCCAnalysisManager cgsccAM;
  llvm::ModuleAnalysisManager moduleAM;
  llvm::PassBuilder passBuilder;

  passBuilder.registerModuleAnalyses(moduleAM);
  passBuilder.registerCGSCCAnalyses(cgsccAM);
  passBuilder.registerFunctionAnalyses(functionAM);
  passBuilder.registerLoopAnalyses(loopAM);
  passBuilder.crossRegisterProxies(loopAM, functionAM, cgsccAM, moduleAM);

  llvm::OptimizationLevel opt = llvm::OptimizationLevel::O2;
  if (level == 1) {
    opt = llvm::OptimizationLevel::O1;
  } else if (level == 3) {
    opt = llvm::OptimizationLevel::O3;
  }

  llvm::ModulePassManager modulePM =
      passBuilder.buildPerModuleDefaultPipeline(opt);
  modulePM.run(module, moduleAM);
}

void writeTempModule(llvm::Module &module, std::string &path) {
  llvm::SmallString<128> tempPath;
  int fd = -1;
  std::error_code ec = llvm::sys::fs::createTemporaryFile("basicc", "ll", fd, tempPath);
  if (ec) {
    throw std::runtime_error("cannot create temporary IR file: " + ec.message());
  }

  llvm::raw_fd_ostream out(fd, true);
  module.print(out, nullptr);
  out.close();
  path = std::string(tempPath.str());
}

void writeExecutable(llvm::Module &module, const std::string &path, int optLevel,
                     TargetKind target) {
  std::string tempIR;
  writeTempModule(module, tempIR);

  auto clang = llvm::sys::findProgramByName("clang-20");
  if (!clang) {
    llvm::sys::fs::remove(tempIR);
    throw std::runtime_error("cannot find clang-20 on PATH");
  }

  std::string clangPath = *clang;
  std::string runtime = BASIC_RUNTIME_SOURCE;
  std::string outputFlag = "-o";
  std::string optFlag = "-O" + std::to_string(optLevel);
  std::string targetFlag =
      target == TargetKind::Win64 ? "--target=x86_64-w64-windows-gnu"
                                  : "--target=" + llvm::sys::getDefaultTargetTriple();
  std::vector<llvm::StringRef> args = {clangPath, targetFlag, tempIR, runtime,
                                       outputFlag, path, optFlag};

  int result = llvm::sys::ExecuteAndWait(clangPath, args);
  llvm::sys::fs::remove(tempIR);
  if (result != 0) {
    throw std::runtime_error("clang-20 failed while linking executable");
  }
}

struct Options {
  std::string input;
  std::string output;
  bool emitExecutable = false;
  int optLevel = 0;
  TargetKind target = TargetKind::Linux64;
};

void usage(const char *argv0) {
  std::cerr << "usage: " << argv0
            << " input.bas -o output [--emit=llvm|exe] [--target=linux64|win64]"
               " [-O0|-O1|-O2|-O3]\n";
}

Options parseOptions(int argc, char **argv) {
  Options options;
  if (argc < 4) {
    usage(argv[0]);
    std::exit(2);
  }

  std::string driver = argv[0];
  if (driver.find("basiccw") != std::string::npos) {
    options.target = TargetKind::Win64;
    options.emitExecutable = true;
  }

  options.input = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-o") {
      if (i + 1 >= argc) {
        throw std::runtime_error("-o requires an output path");
      }
      options.output = argv[++i];
    } else if (arg == "--emit=llvm") {
      options.emitExecutable = false;
    } else if (arg == "--emit=exe") {
      options.emitExecutable = true;
    } else if (arg == "--target=linux64") {
      options.target = TargetKind::Linux64;
    } else if (arg == "--target=win64") {
      options.target = TargetKind::Win64;
    } else if (arg == "-O0") {
      options.optLevel = 0;
    } else if (arg == "-O1") {
      options.optLevel = 1;
    } else if (arg == "-O2") {
      options.optLevel = 2;
    } else if (arg == "-O3") {
      options.optLevel = 3;
    } else {
      throw std::runtime_error("unknown option: " + arg);
    }
  }

  if (options.input.empty() || options.output.empty()) {
    usage(argv[0]);
    std::exit(2);
  }
  return options;
}

} // namespace

int main(int argc, char **argv) {
  try {
    Options options = parseOptions(argc, argv);
    Parser parser(readFile(options.input));
    IRGenerator generator(parser.parse(), targetTriple(options.target));
    std::unique_ptr<llvm::Module> module = generator.generate();
    optimizeModule(*module, options.optLevel);
    if (options.emitExecutable) {
      writeExecutable(*module, options.output, options.optLevel, options.target);
    } else {
      writeModule(*module, options.output);
    }
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }
}
