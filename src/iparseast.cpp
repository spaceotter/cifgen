/*  -*- c++ -*-

    This file is part of c2ffi.

    c2ffi is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    c2ffi is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with c2ffi.  If not, see <http://www.gnu.org/licenses/>.

    This code mostly originates from clang, with some small modifications.
    https://clang.llvm.org/
*/
#include "iparseast.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ExternalASTSource.h>
#include <clang/AST/Stmt.h>
#include <clang/Parse/ParseAST.h>
#include <clang/Parse/ParseDiagnostic.h>
#include <clang/Parse/Parser.h>
#include <clang/Sema/CodeCompleteConsumer.h>
#include <clang/Sema/SemaConsumer.h>
#include <clang/Sema/TemplateInstCallback.h>
#include <llvm/Support/CrashRecoveryContext.h>
#include <llvm/Support/TimeProfiler.h>

#include <cstdio>
#include <memory>

using namespace clang;

namespace {

/// Resets LLVM's pretty stack state so that stack traces are printed correctly
/// when there are nested CrashRecoveryContexts and the inner one recovers from
/// a crash.
class ResetStackCleanup
    : public llvm::CrashRecoveryContextCleanupBase<ResetStackCleanup, const void> {
 public:
  ResetStackCleanup(llvm::CrashRecoveryContext *Context, const void *Top)
      : llvm::CrashRecoveryContextCleanupBase<ResetStackCleanup, const void>(Context, Top) {}
  void recoverResources() override { llvm::RestorePrettyStackState(resource); }
};

/// If a crash happens while the parser is active, an entry is printed for it.
class PrettyStackTraceParserEntry : public llvm::PrettyStackTraceEntry {
  const Parser &P;

 public:
  PrettyStackTraceParserEntry(const Parser &p) : P(p) {}
  void print(raw_ostream &OS) const override;
};

/// If a crash happens while the parser is active, print out a line indicating
/// what the current token is.
void PrettyStackTraceParserEntry::print(raw_ostream &OS) const {
  const Token &Tok = P.getCurToken();
  if (Tok.is(tok::eof)) {
    OS << "<eof> parser at end of file\n";
    return;
  }

  if (Tok.getLocation().isInvalid()) {
    OS << "<unknown> parser at unknown location\n";
    return;
  }

  const Preprocessor &PP = P.getPreprocessor();
  Tok.getLocation().print(OS, PP.getSourceManager());
  if (Tok.isAnnotation()) {
    OS << ": at annotation token\n";
  } else {
    // Do the equivalent of PP.getSpelling(Tok) except for the parts that would
    // allocate memory.
    bool Invalid = false;
    const SourceManager &SM = P.getPreprocessor().getSourceManager();
    unsigned Length = Tok.getLength();
    const char *Spelling = SM.getCharacterData(Tok.getLocation(), &Invalid);
    if (Invalid) {
      OS << ": unknown current parser token\n";
      return;
    }
    OS << ": current parser token '" << StringRef(Spelling, Length) << "'\n";
  }
}

}  // namespace

//===----------------------------------------------------------------------===//
// Public interface to the file
//===----------------------------------------------------------------------===//

/// IncrementalParseAST - based on the upstream ParseAST, but sneak the extra
/// file in after having already parsed other code
void unplusplus::IncrementalParseAST(Sema &S, const std::string &buf, const std::string &name,
                                     bool PrintStats, bool SkipFunctionBodies) {
  auto &SM = S.getSourceManager();
  clang::FileID fid = SM.createFileID(
      std::unique_ptr<llvm::MemoryBuffer>(llvm::MemoryBuffer::getMemBuffer(buf, name)));
  // Collect global stats on Decls/Stmts (until we have a module streamer).
  if (PrintStats) {
    Decl::EnableStatistics();
    Stmt::EnableStatistics();
  }

  // Also turn on collection of stats inside of the Sema object.
  bool OldCollectStats = PrintStats;
  std::swap(OldCollectStats, S.CollectStats);

  // Initialize the template instantiation observer chain.
  // FIXME: See note on "finalize" below.
  initialize(S.TemplateInstCallbacks, S);

  ASTConsumer *Consumer = &S.getASTConsumer();

  std::unique_ptr<Parser> ParseOP(new Parser(S.getPreprocessor(), S, SkipFunctionBodies));
  Parser &P = *ParseOP.get();

  llvm::CrashRecoveryContextCleanupRegistrar<const void, ResetStackCleanup> CleanupPrettyStack(
      llvm::SavePrettyStackState());
  PrettyStackTraceParserEntry CrashInfo(P);

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<Parser> CleanupParser(ParseOP.get());

  // ParseAST was copied wholesale to modify a couple lines here. This switches
  // to the new file ID rather than entering the main file again, which
  // re-initializes some things that shouldn't be.
  S.getPreprocessor().EnterSourceFile(fid, nullptr, SM.getLocForStartOfFile(fid));
  // Clear the old translation unit context ... or something
  S.CurContext = nullptr;
  ExternalASTSource *External = S.getASTContext().getExternalSource();
  if (External) External->StartTranslationUnit(Consumer);

  // If a PCH through header is specified that does not have an include in
  // the source, or a PCH is being created with #pragma hdrstop with nothing
  // after the pragma, there won't be any tokens or a Lexer.
  bool HaveLexer = S.getPreprocessor().getCurrentLexer();

  if (HaveLexer) {
    llvm::TimeTraceScope TimeScope("Frontend");
    P.Initialize();
    Parser::DeclGroupPtrTy ADecl;
    for (bool AtEOF = P.ParseFirstTopLevelDecl(ADecl); !AtEOF; AtEOF = P.ParseTopLevelDecl(ADecl)) {
      // If we got a null return and something *was* parsed, ignore it.  This
      // is due to a top-level semicolon, an action override, or a parse error
      // skipping something.
      if (ADecl && !Consumer->HandleTopLevelDecl(ADecl.get())) return;
    }
  }

  // Process any TopLevelDecls generated by #pragma weak.
  for (Decl *D : S.WeakTopLevelDecls()) Consumer->HandleTopLevelDecl(DeclGroupRef(D));

  Consumer->HandleTranslationUnit(S.getASTContext());

  // Finalize the template instantiation observer chain.
  // FIXME: This (and init.) should be done in the Sema class, but because
  // Sema does not have a reliable "Finalize" function (it has a
  // destructor, but it is not guaranteed to be called ("-disable-free")).
  // So, do the initialization above and do the finalization here:
  finalize(S.TemplateInstCallbacks, S);

  std::swap(OldCollectStats, S.CollectStats);
  if (PrintStats) {
    llvm::errs() << "\nSTATISTICS:\n";
    if (HaveLexer) P.getActions().PrintStats();
    S.getASTContext().PrintStats();
    Decl::PrintStats();
    Stmt::PrintStats();
    Consumer->PrintStats();
  }
}
