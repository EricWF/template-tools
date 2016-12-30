
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/AST/Comment.h"
#include "llvm/Support/raw_ostream.h"


using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::driver;
using namespace clang::tooling;
using namespace clang::comments;
using namespace std;

class MyClassHandler : public MatchFinder::MatchCallback {
private:
  CompilerInstance &ci;
  SourceManager &source_manager;


public:
  MyClassHandler(CompilerInstance &CI)
      : ci(CI), source_manager(CI.getSourceManager()) {}

  virtual void onEndOfTranslationUnit() override {}
  /**
         * This runs per-match from MyASTConsumer, but always on the same
   * MyClassHandler object
         */
  virtual void run(const MatchFinder::MatchResult &Result) override {
  }
};

// Implementation of the ASTConsumer interface for reading an AST produced
// by the Clang parser. It registers a couple of matchers and runs them on
// the AST.
class MyASTConsumer2 : public ASTConsumer {
public:
  MyASTConsumer2(CompilerInstance &CI) : HandlerForClass(CI) {
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
  MyClassHandler HandlerForClass;
  MatchFinder Matcher;
};

// This is the class that is registered with LLVM.  PluginASTAction is-a
// ASTFrontEndAction
class CountTemplatesAction : public PluginASTAction {
public:
  // open up output files
  CountTemplatesAction() {}

  // This is called when all parsing is done
  void EndSourceFileAction() {

  }

protected:
  // The value returned here is used internally to run checks against
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) {
    return llvm::make_unique<MyASTConsumer2>(CI);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) {
    for (unsigned i = 0, e = args.size(); i != e; ++i) {
      llvm::errs() << "CountTemplates arg = " << args[i] << "\n";

      // Example error handling.
      if (args[i] == "-an-error") {
        DiagnosticsEngine &D = CI.getDiagnostics();
        unsigned DiagID = D.getCustomDiagID(DiagnosticsEngine::Error,
                                            "invalid argument '%0'");
        D.Report(DiagID) << args[i];
        return false;
      }
    }
    if (args.size() && args[0] == "help")
      PrintHelp(llvm::errs());

    return true;
  }
  void PrintHelp(llvm::raw_ostream &ros) {
    ros << "Help for CountTemplates plugin goes here\n";
  }
};


static FrontendPluginRegistry::Add<CountTemplatesAction>
    X("my-template-tool-count-templates",
      "print the template instantiation count");
