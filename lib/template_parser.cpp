#include "template_parser.h"

#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <cassert>

//////////////////////////////
// CUSTOMIZE THESE VARIABLES
//////////////////////////////

// if this is defined, only template info will be printed
//#define TEMPLATE_FILTER_STD
static int TemplatePrintThreshold = 10;
static int TEMPLATED_FUNCTION_PRINT_THRESHOLD = 100;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace clang::comments;
using namespace std;

int matched_classes_returned = 0;

namespace {

std::string get_canonical_name_for_decl(const TypeDecl *decl) {
  if (decl == nullptr) {
    llvm::report_fatal_error("null type decl to get_canonical_name_for_decl");
  }
  return decl->getTypeForDecl()->getCanonicalTypeInternal().getAsString();
}

int print_logging = 1;

struct ClassTemplate;
vector<std::unique_ptr<ClassTemplate>> class_templates;
struct ClassTemplate {
  std::string name;
  const ClassTemplateDecl *decl;
  int instantiations = 0;

  ClassTemplate(const ClassTemplateDecl *decl) : decl(decl) {
    name = decl->getQualifiedNameAsString();
  }

  void instantiated() { instantiations++; }

  static ClassTemplate &get_or_create(const ClassTemplateDecl *decl) {
    for (auto &tmpl : class_templates) {
      if (tmpl->decl == decl) {
        return *tmpl;
      }
    }
    class_templates.emplace_back(make_unique<ClassTemplate>(decl));
    return *class_templates.back();
  }
};

class ClassHandler : public MatchFinder::MatchCallback {
private:
  CompilerInstance &ci;
  SourceManager &source_manager;

public:
  ClassHandler(CompilerInstance &CI)
      : ci(CI), source_manager(CI.getSourceManager()) {}

  /**
         * This runs per-match from MyASTConsumer, but always on the same
   * ClassHandler object
         */
  virtual void run(const MatchFinder::MatchResult &Result) override {

    matched_classes_returned++;

    if (matched_classes_returned % 10000 == 0) {
      cerr << endl
           << "### MATCHER RESULT " << matched_classes_returned << " ###"
           << endl;
    }

    if (const ClassTemplateSpecializationDecl *klass =
            Result.Nodes.getNodeAs<clang::ClassTemplateSpecializationDecl>(
                "class")) {
      auto class_name = get_canonical_name_for_decl(klass);

      bool print_logging = false;

      if (std::regex_search(class_name,
                            std::regex("^(class|struct)\\s+v8toolkit"))) {
        //		if (std::regex_search(class_name,
        // std::regex("remove_reference"))) {
        print_logging = true;
        cerr << "Got class " << class_name << endl;
      }

#ifdef TEMPLATE_FILTER_STD
      if (std::regex_search(class_name, std::regex("^std::"))) {
        if (print_logging)
          cerr << "Filtering out because in std::" << endl;
        return;
      }
#endif

      auto tmpl = klass->getSpecializedTemplate();
      if (print_logging) {
        cerr << "got specialized template " << tmpl->getQualifiedNameAsString()
             << endl;
      }

#ifdef TEMPLATE_FILTER_STD
      if (std::regex_search(tmpl->getQualifiedNameAsString(),
                            std::regex("^std::"))) {
        return;
      }
#endif

      ClassTemplate::get_or_create(tmpl).instantiated();
    }
  }
};

// Implementation of the ASTConsumer interface for reading an AST produced
// by the Clang parser. It registers a couple of matchers and runs them on
// the AST.
class MyASTConsumer : public ASTConsumer {
public:
  MyASTConsumer(CompilerInstance &CI) : HandlerForClass(CI) {
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
  ClassHandler HandlerForClass;
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
    for (auto &class_template : class_templates) {
      insts.push_back({class_template->name, class_template->instantiations});
    }
    std::sort(insts.begin(), insts.end(),
              [](auto &a, auto &b) { return a.second < b.second; });
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
    skipped = 0;
    total = 0;
    insts.clear();
  }

protected:
  // The value returned here is used internally to run checks against
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return llvm::make_unique<MyASTConsumer>(CI);
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
};
}

static FrontendPluginRegistry::Add<PrintFunctionNamesAction>
    X("template-tools",
      "print the template instantiation count");
