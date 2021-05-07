/*
 * unplusplus
 * Copyright 2021 Eric Eaton
 */

#pragma once

#include <clang/AST/Decl.h>

#include "outputs.hpp"

namespace unplusplus {
class DeclWriterBase;
typedef std::unordered_map<const clang::Decl *, std::unique_ptr<DeclWriterBase>> DeclWriterMap;
class DeclHandler {
  Outputs &_out;
  DeclWriterMap _decls;
  std::stringstream _templates;
  IdentifierConfig _cfg;

 public:
  DeclHandler(Outputs &out, const clang::ASTContext &_astc) : _out(out), _cfg(_astc) {}
  void add(const clang::Decl *d);
  Outputs &out() { return _out; }
  void finish();
  std::string templates() const { return _templates.str(); }
  void addTemplate(const std::string &name) {
    _templates << "extern template class " << name << ";\n";
  }
  const IdentifierConfig &cfg() const { return _cfg; }
  // Ensure that a type is declared already
  void forward(const clang::QualType &t);
};

class DeclWriterBase {
 protected:
  DeclHandler &_dh;
  Outputs &_out;

 public:
  DeclWriterBase(DeclHandler &dh) : _dh(dh), _out(dh.out()) {}
  virtual ~DeclWriterBase() {}
  const IdentifierConfig &cfg() const { return _dh.cfg(); }
  const Outputs &out() const { return _out; }
};

template <class T>
class DeclWriter : public DeclWriterBase {
 protected:
  typedef T type;
  const T *_d;
  Identifier _i;

 public:
  DeclWriter(const T *d, DeclHandler &dh) : DeclWriterBase(dh), _d(d), _i(d, _dh.cfg()) {}

  void preamble(std::ostream &out) {
    std::string location = _d->getLocation().printToString(_d->getASTContext().getSourceManager());
    out << "// location: " << location << "\n";
    out << "// C++ name: " << _i.cpp << "\n";
  }
};
}  // namespace unplusplus
