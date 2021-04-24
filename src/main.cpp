/*
 * unplusplus
 * Copyright 2021 Eric Eaton
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <filesystem>

#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

#include "outputs.hpp"
#include "identifier.hpp"
#include "action.hpp"

using namespace clang;
using namespace llvm;
using namespace unplusplus;
using std::filesystem::path;

static cl::OptionCategory UppCategory("unplusplus options");
static cl::opt<std::string> OutStem("o", cl::desc("Output files base name"), cl::Optional, cl::cat(UppCategory), cl::sub(*cl::AllSubCommands));

int main(int argc, const char **argv) {
  std::vector<const char *> args(argv, argv + argc);
  std::cout << "clang resource dir: " << CLANG_RESOURCE_DIRECTORY << std::endl;
  args.push_back("--extra-arg-before=-resource-dir=" CLANG_RESOURCE_DIRECTORY);
  args.push_back("--");
  args.push_back("clang++");
  args.push_back("-c");
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
