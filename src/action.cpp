/*
 * unplusplus
 * Copyright 2021 Eric Eaton
 */

#include "action.hpp"
#include "decls.hpp"

#include <clang/Index/IndexingAction.h>
#include <clang/Index/IndexingOptions.h>

using namespace unplusplus;
using namespace clang;

bool IndexDataConsumer::handleDeclOccurrence(const Decl *d, clang::index::SymbolRoleSet,
                                             llvm::ArrayRef<clang::index::SymbolRelation>,
                                             clang::SourceLocation sl,
                                             clang::index::IndexDataConsumer::ASTNodeInfo ani) {
  // send output to a temporary buffer in case we have to recurse to write a forward declaration.
  SourceManager &SM = d->getASTContext().getSourceManager();
  std::string location = sl.printToString(SM);
  if (const NamedDecl *nd = dynamic_cast<const NamedDecl *>(d)) {
    FileID FID = SM.getFileID(SM.getFileLoc(sl));
    bool Invalid = false;
    const SrcMgr::SLocEntry &SEntry = SM.getSLocEntry(FID, &Invalid);
    SrcMgr::CharacteristicKind ck = SEntry.getFile().getFileCharacteristic();
    // The declaration is from a C header that can just be included by the library
    if (ck == SrcMgr::CharacteristicKind::C_ExternCSystem) {
      _outs.addCHeader(SEntry.getFile().getName().str());
      return true;
    }
    SubOutputs temp(_outs);
    handle_decl(nd, temp);
  } else {
    std::cerr << "Warning: Ignoring unnamed Decl of kind " << d->getDeclKindName() << "\n";
  }
  return true;
}

std::unique_ptr<clang::FrontendAction> IndexActionFactory::create() {
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
