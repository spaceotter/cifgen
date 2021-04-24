/*
 * unplusplus
 * Copyright 2021 Eric Eaton
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <filesystem>

#include "clang/Index/IndexDataConsumer.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Index/IndexingOptions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include "outputs.hpp"
#include "identifier.hpp"

using namespace clang;
using namespace llvm;
using namespace unplusplus;
using std::filesystem::path;

class IndexDataConsumer : public index::IndexDataConsumer {
  Outputs &_outs;
 public:
  IndexDataConsumer(Outputs &outs) : _outs(outs) {}

  // Print a message to stdout if any kind of declaration is found
  bool handleDeclOccurrence(const Decl *d, clang::index::SymbolRoleSet,
                            llvm::ArrayRef<clang::index::SymbolRelation>, clang::SourceLocation sl,
                            clang::index::IndexDataConsumer::ASTNodeInfo ani) override {
    // send output to a temporary buffer in case we have to recurse to write a forward declaration.
    SubOutputs temp(_outs);
    if (const NamedDecl *nd = dynamic_cast<const NamedDecl *>(d)) {
      std::cout << "Decl name:" << nd->getDeclName().getAsString() << std::endl;
      temp.hf() << "// Decl name:" << nd->getDeclName().getAsString() << "\n";
    } else {
      std::cout << "Unnamed Decl" << std::endl;
    }
    SourceManager &SM = d->getASTContext().getSourceManager();
    std::cout << "Source: " << sl.printToString(SM) << " which is: ";
    FileID FID = SM.getFileID(SM.getFileLoc(sl));
    bool Invalid = false;
    const SrcMgr::SLocEntry &SEntry = SM.getSLocEntry(FID, &Invalid);
    switch(SEntry.getFile().getFileCharacteristic()) {
      case SrcMgr::CharacteristicKind::C_ExternCSystem:
        std::cout << "ExternCSystem";
        break;
      case SrcMgr::CharacteristicKind::C_User:
        std::cout << "User";
        break;
      case SrcMgr::CharacteristicKind::C_System:
        std::cout << "System";
        break;
      case SrcMgr::CharacteristicKind::C_User_ModuleMap:
        std::cout << "User ModuleMap";
        break;
      case SrcMgr::CharacteristicKind::C_System_ModuleMap:
        std::cout << "System ModuleMap";
        break;
    }
    std::cout << std::endl;
    if (ani.Parent != nullptr) {
      if (const NamedDecl *nd = dynamic_cast<const NamedDecl *>(ani.Parent)) {
        std::cout << "Parent Decl name:" << nd->getDeclName().getAsString() << std::endl;
      } else {
        std::cout << "Unnamed Decl" << std::endl;
      }
    }
    return true;
  }
};

class IndexActionFactory : public clang::tooling::FrontendActionFactory {
  Outputs &_outs;
 public:
  IndexActionFactory(Outputs &outs) : _outs(outs) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    clang::index::IndexingOptions opts;
    opts.IndexFunctionLocals = false;
    opts.IndexImplicitInstantiation = false;
    opts.IndexMacrosInPreprocessor = true;
    opts.IndexParametersInDeclarations = true;
    opts.IndexTemplateParameters = false;
    opts.SystemSymbolFilter = clang::index::IndexingOptions::SystemSymbolFilterKind::All;
    IndexDataConsumer idx(_outs);
    return createIndexingAction(std::make_shared<IndexDataConsumer>(idx), opts);
  }
};

static cl::OptionCategory UppCategory("unplusplus options");
static cl::opt<std::string> OutStem("o", cl::desc("Output files base name"), cl::Optional, cl::cat(UppCategory), cl::sub(*cl::AllSubCommands));

int main(int argc, const char **argv) {
  std::vector<const char *> args(argv, argv + argc);
  args.push_back("--");
  args.push_back("clang++");
  args.push_back("-c");
  args.push_back("-resource-dir");
  args.push_back(CLANG_RESOURCE_DIRECTORY);
  int size = args.size();
  tooling::CommonOptionsParser OptionsParser(size, args.data(), UppCategory);
  std::vector<std::string> sources = OptionsParser.getSourcePathList();
  tooling::ClangTool Tool(OptionsParser.getCompilations(), sources);
  path stem;
  if (OutStem.empty()) {
    stem = path(sources[0]).stem();
  } else {
    stem = path(OutStem.getValue());
  }
  std::cout << "Writing library to: " << stem.string() << ".*" << std::endl;
  IdentifierConfig icfg;
  FileOutputs fout(stem, sources, icfg);
  IndexActionFactory Factory(fout);
  return Tool.run(&Factory);
}
