#include "template_parser.h"

#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <cassert>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace clang::comments;
using namespace std;

static int TemplatePrintThreshold = 10;

using TemplateUnion =
    llvm::PointerUnion<ClassTemplateDecl *,
                       ClassTemplatePartialSpecializationDecl *>;

namespace std {
template <class T, class U>
struct hash<::llvm::PointerUnion<T, U>> {
  using value_type = ::llvm::PointerUnion<T, U>;
  size_t operator()(value_type const &VT) const {
    return llvm::DenseMapInfo<value_type>::getHashValue(VT);
  }
};
}

namespace {

struct InstantiationInfo {
  InstantiationInfo() : name(), instantiations(0) {}
  std::string name;
  int instantiations = 0;
};

using TemplateInfoMap = std::unordered_map<TemplateUnion, InstantiationInfo>;

class TemplateMatchCallback : public MatchFinder::MatchCallback {
private:
  CompilerInstance &ci;
  SourceManager &source_manager;
  TemplateInfoMap &foundTemplates;

public:
  TemplateMatchCallback(CompilerInstance &CI, TemplateInfoMap &M)
      : ci(CI), source_manager(CI.getSourceManager()), foundTemplates(M) {}

  virtual void run(const MatchFinder::MatchResult &Result) override {
    if (const ClassTemplateSpecializationDecl *klass =
            Result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("class")) {
      auto it_pair = foundTemplates.emplace(klass->getInstantiatedFrom(),
                                            InstantiationInfo{});
      auto it = it_pair.first;
      if (it_pair.second)
        it->second.name =
            klass->getSpecializedTemplate()->getQualifiedNameAsString();
      it->second.instantiations++;
    }
  }
};

// Implementation of the ASTConsumer interface for reading an AST produced
// by the Clang parser. It registers a couple of matchers and runs them on
// the AST.
class MyASTConsumer : public ASTConsumer {
public:
  MyASTConsumer(CompilerInstance &CI, TemplateInfoMap &M)
      : HandlerForClass(CI, M) {
    Matcher.addMatcher(
        decl(anyOf(classTemplateSpecializationDecl().bind("class"),
                   cxxMethodDecl().bind("method"))),
        &HandlerForClass);
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    // Run the matchers when we have the whole TU parsed.
    Matcher.matchAST(Context);
  }

private:
  TemplateMatchCallback HandlerForClass;
  MatchFinder Matcher;
};

// This is the class that is registered with LLVM.  PluginASTAction is-a
// ASTFrontEndAction
class PrintFunctionNamesAction : public PluginASTAction {
public:
  // open up output files
  PrintFunctionNamesAction() {}

  // This is called when all parsing is done
  void EndSourceFileAction() override {
    cerr << "Class template instantiations" << endl;
    vector<pair<string, int>> insts;
    for (auto &KV : foundTemplates)
      insts.push_back({KV.second.name, KV.second.instantiations});
    std::sort(insts.begin(), insts.end(),
              [](auto &a, auto &b) { return a.second < b.second; });

    // assert(insts == insts_other);
    int skipped = 0;
    int total = 0;
    cerr << endl
         << "Class templates with " << TemplatePrintThreshold
         << " or more instantiations:" << endl;
    for (auto &pair : insts) {
      total += pair.second;
      if (pair.second < TemplatePrintThreshold) {
        skipped++;
        continue;
      }
      cerr << pair.first << ": " << pair.second << endl;
      ;
    }
    cerr << endl;
    cerr << "Skipped " << skipped << " entries because they had fewer than "
         << TemplatePrintThreshold << " instantiations" << endl;
    cerr << "Total of " << total << " instantiations" << endl;
    insts.clear();
  }

protected:
  // The value returned here is used internally to run checks against
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return llvm::make_unique<MyASTConsumer>(CI, foundTemplates);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    for (StringRef Arg : args) {
      if (Arg.startswith("-min_threshold=")) {
        StringRef Val = Arg.substr(strlen("-min_threshold="));
        if (Val.getAsInteger(0, TemplatePrintThreshold)) {
          llvm::errs() << "Failed to parse option: " << Arg;
          return false;
        }
      }
      if (Arg == "help")
        PrintHelp(llvm::errs());
    }
    return true;
  }

  void PrintHelp(llvm::raw_ostream &ros) {
    ros << "Help for PrintFunctionNames plugin goes here\n";
  }

private:
  TemplateInfoMap foundTemplates;
};

} // end namespace

static FrontendPluginRegistry::Add<PrintFunctionNamesAction>
    X("template-tools", "print the template instantiation count");
