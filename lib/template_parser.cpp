/**

FOR COMMENT PARSING, LOOK AT ASTContext::getCommentForDecl

http://clang.llvm.org/doxygen/classclang_1_1ASTContext.html#aa3ec454ca3698f73c421b08f6edcea92

 */

/**
 * This clang plugin looks for classes annotated with V8TOOLKIT_WRAPPED_CLASS
 * and/or V8TOOLKIT_BIDIRECTIONAL_CLASS
 * and automatically generates source files for class wrappings and/or JSWrapper
 * objects for those classes.
 *
 * Each JSWrapper object type will go into its own .h file called
 * v8toolkit_generated_bidirectional_<ClassName>.h
 *   These files should be included from within the header file defining the
 * class.
 *
 * MISSING DOCS FOR CLASS WRAPPER CODE GENERATION
 */

// This program will only work with clang but the output should be useable on
// any platform.

/**
 * KNOWN BUGS:
 * Doesn't properly understand virtual methods and will duplicate them across
 * the inheritence hierarchy
 * Doesn't include root file of compilation - if this is pretty safe in a unity
 * build, as the root file is a file that
 *   just includes all the other files
 */

/**
How to run over complete code base using cmake + cotiregg1

add_library(api-gen-template OBJECT ${YOUR_SOURCE_FILES})
target_compile_options(api-gen-template
        PRIVATE -Xclang -ast-dump -fdiagnostics-color=never <== this isn't right
yet, this just dumps the ast
        )
set_target_properties(api-gen-template PROPERTIES COTIRE_UNITY_TARGET_NAME
"api-gen")
cotire(api-gen-template)
 */

#include <vector>
#include <string>
#include <map>
#include <cassert>
using namespace std;

//////////////////////////////
// CUSTOMIZE THESE VARIABLES
//////////////////////////////

// if this is defined, only template info will be printed
//#define TEMPLATE_FILTER_STD

#define TEMPLATED_CLASS_PRINT_THRESHOLD 10
#define TEMPLATED_FUNCTION_PRINT_THRESHOLD 100

// Generate an additional file with sfinae for each wrapped class type
bool generate_v8classwrapper_sfinae = true;

// Having this too high can lead to VERY memory-intensive compilation units
// Single classes (+base classes) with more than this number of declarations
// will still be in one file.
// TODO: This should be a command line parameter to the plugin
#define MAX_DECLARATIONS_PER_FILE 40

// Any base types you want to always ignore -- v8toolkit::WrappedClassBase must
// remain, others may be added/changed
vector<string> base_types_to_ignore = {"class v8toolkit::WrappedClassBase",
                                       "class Subscriber"};

// Top level types that will be immediately discarded
vector<string> types_to_ignore_regex = {
    "^struct has_custom_process[<].*[>]::mixin$"};

vector<string> includes_for_every_class_wrapper_file = {
    "<stdbool.h>", "\"js_casts.h\"", "<v8toolkit/v8_class_wrapper_impl.h>"};

// error if bidirectional types don't make it in due to include file ordering
// disable "fast_compile" so the V8ClassWrapper code can be generated
string header_for_every_class_wrapper_file =
    "#define NEED_BIDIRECTIONAL_TYPES\n#undef V8TOOLKIT_WRAPPER_FAST_COMPILE\n";

// sometimes files sneak in that just shouldn't be
vector<string> never_include_for_any_file = {"\"v8helpers.h\""};

map<string, string> static_method_renames = {{"name", "get_name"}};

// http://usejsdoc.org/tags-type.html
map<string, string> cpp_to_js_type_conversions = {
    {"^(?:std::)?vector[<]\\s*([^>]+?)\\s*[>]\\s*$", "Array.{$1}"},
    {"^(?:std::)?map[<]\\s*([^>]+?)\\s*,\\s*([^>]+?)\\s*[>]\\s*$",
     "Object.{$1, $2}"},
    {"^([^<]+)\\s*[<]\\s*(.+?)\\s*[>]\\s*([^>]*)$", "$1<$2>$3"},
    {"^(?:const)?\\s*(?:unsigned)?\\s*(?:char|short|int|long|long "
     "long|float|double|long double)\\s*(?:const)?\\s*[*]?\\s*[&]?$",
     "Number"},
    {"^(?:const)?\\s*_?[Bb]ool\\s*(?:const)?\\s*[*]?\\s*[&]?$", "Boolean"},
    {"^(?:const)?\\s*(?:char\\s*[*]|(?:std::)?string)\\s*(?:const)?\\s*\\s*[&]?"
     "$",
     "String"},
    {"^void$", "Undefined"}};

// regex for @callback instead of @param:
// ^(const)?\s*(std::)?function[<][^>]*[>]\s*(const)?\s*\s*[&]?$

std::string js_api_header = R"JS_API_HEADER(
/**
 * Prints a string and appends a newline
 * @param {string} s the string to be printed
 */
function println(s){}

/**
 * Prints a string without adding a newline to the end
 * @param {string} s the string to be printed
 */
function print(s){}

/**
 * Dumps the contents of the given variable - only 'own' properties
 * @param o the variable to be dumped
 */
function printobj(o)

/**
 * Dumps the contents of the given variable - all properties including those of prototype chain
 * @param o the variable to be dumped
 */
function printobjall(o)

/**
 * Attempts to load the given module and returns the exported data.  Requiring the same module
 *   more than once will return the cached result, not re-execute the source.
 * @param {string} module_name name of the module to require
 */
function require(module_name) {}


)JS_API_HEADER";

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <regex>

#include <fmt/ostream.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"

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

#pragma clang diagnostic pop

#include "template_parser.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::driver;
using namespace clang::tooling;
using namespace clang::comments;
using namespace std;

#define PRINT_SKIPPED_EXPORT_REASONS true
//#define PRINT_SKIPPED_EXPORT_REASONS false

int classes_wrapped = 0;
int methods_wrapped = 0;
int matched_classes_returned = 0;

namespace {

std::string get_canonical_name_for_decl(const TypeDecl *decl) {
  if (decl == nullptr) {
    llvm::report_fatal_error("null type decl to get_canonical_name_for_decl");
  }
  return decl->getTypeForDecl()->getCanonicalTypeInternal().getAsString();
}

int print_logging = 1;

// how was a wrapped class determined to be a wrapped class?
enum FOUND_METHOD {
  FOUND_UNSPECIFIED = 0,
  FOUND_ANNOTATION,
  FOUND_INHERITANCE,
  FOUND_GENERATED,
  FOUND_BASE_CLASS, // if the class is a base class of a wrapped type, the class
                    // must be wrapped
  FOUND_NEVER_WRAP
};

enum EXPORT_TYPE {
  EXPORT_UNSPECIFIED = 0,
  EXPORT_NONE,   // export nothing
  EXPORT_SOME,   // only exports specifically marked entities
  EXPORT_EXCEPT, // exports everything except specifically marked entities
  EXPORT_ALL
}; // exports everything

EXPORT_TYPE get_export_type(const NamedDecl *decl,
                            EXPORT_TYPE previous = EXPORT_UNSPECIFIED);

// list of data (source code being processed) errors occuring during the run.
// Clang API errors are still immediate fails with
//   llvm::report_fatal_error
vector<string> errors;
void data_error(const string &error) {
  cerr << "DATA ERROR: " << error << endl;
  errors.push_back(error);
}

vector<string> warnings;
void data_warning(const string &warning) {
  cerr << "DATA WARNING: " << warning << endl;
  warnings.push_back(warning);
}

QualType get_plain_type(QualType qual_type) {
  auto type = qual_type.getNonReferenceType().getUnqualifiedType();
  while (!type->getPointeeType().isNull()) {
    type = type->getPointeeType().getUnqualifiedType();
  }
  return type;
}

class PrintLoggingGuard {
  bool logging = false;

public:
  PrintLoggingGuard() = default;
  ~PrintLoggingGuard() {
    if (logging) {
      print_logging--;
    }
  }
  void log() {
    print_logging++;
    logging = true;
  }
};


void print_vector(const vector<string> &vec, const string &header = "",
                  const string &indentation = "", bool ignore_empty = true) {

  if (ignore_empty && vec.empty()) {
    return;
  }

  if (header != "") {
    cerr << indentation << header << endl;
  }
  for (auto &str : vec) {
    cerr << indentation << " - " << str << endl;
  }
  if (vec.empty()) {
    cerr << indentation << " * (EMPTY VECTOR)" << endl;
  }
}

// joins a range of strings with commas (or whatever specified)
template <class T>
std::string join(const T &source, const std::string &between = ", ",
                 bool leading_between = false) {
  if (source.empty()) {
    return "";
  }
  stringstream result;
  if (leading_between) {
    result << between;
  }
  bool first = true;
  for (auto &str : source) {
    if (str == "") {
      // printf("Skipping blank entry in join()\n");
      continue;
    }
    if (!first) {
      result << between;
    }
    first = false;
    result << str;
  }
  return result.str();
}

/* For some classes (ones with dependent base types among them), there will be
 * two copies, one will be
     * unusable for an unknown reason.  This tests if it is that
     */
bool is_good_record_decl(const CXXRecordDecl *decl) {
  if (decl == nullptr) {
    return false;
  }

  if (!decl->isThisDeclarationADefinition()) {
    return true;
  }

  for (auto base : decl->bases()) {
    if (base.getType().getTypePtrOrNull() == nullptr) {
      llvm::report_fatal_error(
          fmt::format("base type ptr was null for {}", decl->getNameAsString())
              .c_str());
    }
    if (!is_good_record_decl(base.getType()->getAsCXXRecordDecl())) {
      return false;
    }
  }
  return true;
}

// Finds where file_id is included, how it was included, and returns the string
// to duplicate
//   that inclusion
std::string get_include_string_for_fileid(CompilerInstance &compiler_instance,
                                          FileID &file_id) {
  auto include_source_location =
      compiler_instance.getPreprocessor().getSourceManager().getIncludeLoc(
          file_id);

  // If it's in the "root" file (file id 1), then there's no include for it
  if (include_source_location.isValid()) {
    auto header_file = include_source_location.printToString(
        compiler_instance.getPreprocessor().getSourceManager());
    //            if (print_logging) cerr << "include source location: " <<
    //            header_file << endl;
    //            wrapped_class.include_files.insert(header_file);
  } else {
    //            if (print_logging) cerr << "No valid source location" << endl;
    return "";
  }

  bool invalid;
  // This gets EVERYTHING after the start of the filename in the include.
  // "asdf.h"..... or <asdf.h>.....
  const char *text =
      compiler_instance.getPreprocessor().getSourceManager().getCharacterData(
          include_source_location, &invalid);
  const char *text_end = text + 1;
  while (*text_end != '>' && *text_end != '"') {
    text_end++;
  }

  return string(text, (text_end - text) + 1);
}

std::string
get_include_for_source_location(CompilerInstance &compiler_instance,
                                const SourceLocation &source_location) {
  auto full_source_loc = FullSourceLoc(
      source_location, compiler_instance.getPreprocessor().getSourceManager());

  auto file_id = full_source_loc.getFileID();
  return get_include_string_for_fileid(compiler_instance, file_id);
}

std::string get_include_for_type_decl(CompilerInstance &compiler_instance,
                                      const TypeDecl *type_decl) {
  if (type_decl == nullptr) {
    return "";
  }
  return get_include_for_source_location(compiler_instance,
                                         type_decl->getLocStart());
}

//
//    std::string decl2str(const clang::Decl *d, SourceManager &sm) {
//        // (T, U) => "T,,"
//        std::string text =
//        Lexer::getSourceText(CharSourceRange::getTokenRange(d->getSourceRange()),
//        sm, LangOptions(), 0);
//        if (text.at(text.size()-1) == ',')
//            return
//            Lexer::getSourceText(CharSourceRange::getCharRange(d->getSourceRange()),
//            sm, LangOptions(), 0);
//        return text;
//    }

std::string get_source_for_source_range(SourceManager &sm,
                                        SourceRange source_range) {
  std::string text = Lexer::getSourceText(
      CharSourceRange::getTokenRange(source_range), sm, LangOptions(), 0);
  if (!text.empty() && text.at(text.size() - 1) == ',')
    return Lexer::getSourceText(CharSourceRange::getCharRange(source_range), sm,
                                LangOptions(), 0);
  return text;
}
#if 0

    vector<string> count_top_level_template_parameters(const std::string & source) {
        int open_angle_count = 0;
        vector<string> parameter_strings;
        std::string * current;
        for (char c : source) {
            if (isspace(c)) {
                continue;
            }
            if (c == '<') {
                open_angle_count++;
                if (open_angle_count > 1) {
                    *current += c;
                }
            } else if (c == '>') {
                open_angle_count--;
                if (open_angle_count > 0) {
                    *current += c;
                }
            } else {
                if (open_angle_count == 1) {
                    if (parameter_strings.size() == 0) {
                        parameter_strings.push_back("");
                        current = &parameter_strings.back();
                    }
                    if (c == ',') {
                        parameter_strings.push_back("");
                        current = &parameter_strings.back();
                        if (open_angle_count > 1) {
                            *current += c;
                        }
                    } else {
                        *current += c;
                    }
                } else if (open_angle_count > 1) {
                    *current += c;
                }
            }
        }
        if (print_logging) if (print_logging) cerr << "^^^^^^^^^^^^^^^ Counted " << parameter_strings.size() << " for " << source << endl;
        for (auto & str : parameter_strings) {
            if (print_logging) cerr <<  "^^^^^^^^^^^^^^^" << str << endl;
        }
        return parameter_strings;
    }
#endif

class Annotations {
  set<string> annotations;

  void get_annotations_for_decl(const Decl *decl) {
    if (!decl) {
      return;
    }
    for (auto attr : decl->getAttrs()) {
      AnnotateAttr *annotation = dyn_cast<AnnotateAttr>(attr);
      if (annotation) {
        auto attribute_attr = dyn_cast<AnnotateAttr>(attr);
        auto annotation_string = attribute_attr->getAnnotation().str();
        // if (print_logging) cerr << "Got annotation " << annotation_string <<
        // endl;
        annotations.emplace(annotation->getAnnotation().str());
      }
    }
  }

public:
  Annotations(const Decl *decl) { get_annotations_for_decl(decl); }

  Annotations(const CXXRecordDecl *decl);
  Annotations(const CXXMethodDecl *decl) { get_annotations_for_decl(decl); }

  Annotations() = default;

  const vector<string> get() const {
    std::vector<string> results;

    for (auto &annotation : annotations) {
      results.push_back(annotation);
    }
    return results;
  }

  std::vector<string> get_regex(const string &regex_string) const {
    auto regex = std::regex(regex_string);
    std::vector<string> results;

    for (auto &annotation : annotations) {
      std::smatch matches;
      if (std::regex_match(annotation, matches, regex)) {
        // printf("GOT %d MATCHES\n", (int)matches.size());
        if (matches.size() > 1) {
          results.emplace_back(matches[1]);
        }
      }
    }
    return results;
  }

  bool has(const std::string &target) const {
    return std::find(annotations.begin(), annotations.end(), target) !=
           annotations.end();
  }

  void merge(const Annotations &other) {
    cerr << fmt::format("Merging in {} annotations onto {} existing ones",
                        other.get().size(), this->get().size())
         << endl;
    annotations.insert(other.annotations.begin(), other.annotations.end());
  }
};

map<const ClassTemplateDecl *, Annotations> annotations_for_class_templates;

// any annotations on 'using' statements should be applied to the actual
// CXXRecordDecl being aliased (the right side)
map<const CXXRecordDecl *, Annotations> annotations_for_record_decls;

// if a template instantiation is named with a 'using' statement, use that alias
// for the type isntead of the template/class name itself
//   this stops them all from being named the same thing - aka CppFactory,
//   CppFactory, ...  instead of MyThingFactory, MyOtherThingFactory, ...
map<const CXXRecordDecl *, string> names_for_record_decls;

Annotations::Annotations(const CXXRecordDecl *decl) {
  auto name = get_canonical_name_for_decl(decl);
  get_annotations_for_decl(decl);
  cerr << "Making annotations object for " << name << endl;
  if (auto spec_decl = dyn_cast<ClassTemplateSpecializationDecl>(decl)) {
    cerr << fmt::format(
                "{} is a template, getting any tmeplate annotations available",
                name)
         << endl;
    cerr << annotations_for_class_templates[spec_decl->getSpecializedTemplate()]
                .get()
                .size()
         << " annotations available" << endl;
    merge(annotations_for_class_templates[spec_decl->getSpecializedTemplate()]);
  } else {
    cerr << "Not a template" << endl;
  }
}

struct ClassTemplate;
vector<std::unique_ptr<ClassTemplate>> class_templates;
struct ClassTemplate {
  std::string name;
  const ClassTemplateDecl *decl;
  int instantiations = 0;

  ClassTemplate(const ClassTemplateDecl *decl) : decl(decl) {
    name = decl->getQualifiedNameAsString();
    // cerr << fmt::format("Created class template for {}", name) << endl;
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

struct FunctionTemplate;
vector<unique_ptr<FunctionTemplate>> function_templates;
struct FunctionTemplate {
  std::string name;
  // const FunctionTemplateDecl * decl;

  // not all functions instantiated because of a template are templated
  // themselves
  const FunctionDecl *decl;
  int instantiations = 0;

  FunctionTemplate(const FunctionDecl *decl) : decl(decl) {
    name = decl->getQualifiedNameAsString();
    //	    cerr << fmt::format("Created function template for {}", name) <<
    //endl;
  }

  void instantiated() { instantiations++; }

  static FunctionTemplate &get_or_create(const FunctionDecl *decl) {

    for (auto &tmpl : function_templates) {
      if (tmpl->decl == decl) {
        return *tmpl;
      }
    }
    function_templates.emplace_back(make_unique<FunctionTemplate>(decl));
    return *function_templates.back();
  }
};

// store all the constructor names used, since they all go into the same global
// object template used as for building contexts
std::vector<std::string> used_constructor_names;

struct WrappedClass {
  CXXRecordDecl const *decl = nullptr;

  // if this wrapped class is a template instantiation, what was it patterned
  // from -- else nullptr
  CXXRecordDecl const *instantiation_pattern = nullptr;
  string class_name;
  string name_alias; // if no alias, is equal to class_name
  set<string> include_files;
  int declaration_count = 0;
  set<string> methods;

  set<CXXMethodDecl const *> wrapped_methods_decls;
  set<string> members;
  set<string> constructors;
  set<string> names;
  set<WrappedClass *> derived_types;
  set<WrappedClass *> base_types;
  set<FieldDecl *> fields;
  set<string> wrapper_extension_methods;
  CompilerInstance &compiler_instance;
  string my_include; // the include for getting my type
  bool done = false;
  bool valid = false; // guilty until proven innocent - don't delete !valid
                      // classes because they may be base classes for valid
                      // types
  Annotations annotations;
  bool dumped = false;              // this class has been dumped to file
  set<WrappedClass *> used_classes; // classes this class uses in its wrapped
                                    // functions/members/etc
  bool has_static_method = false;
  FOUND_METHOD found_method;
  bool force_no_constructors = false;

  std::string get_short_name() const {
    if (decl == nullptr) {
      llvm::report_fatal_error(
          fmt::format("Tried to get_short_name on 'fake' WrappedClass {}",
                      class_name)
              .c_str());
    }
    return decl->getNameAsString();
  }

  bool is_template_specialization() {
    return dyn_cast<ClassTemplateSpecializationDecl>(decl);
  }

  // all the correct annotations and name overrides may not be available when
  // the WrappedObject is initially created
  void update_data() {
    cerr << "Updating wrapped class data for " << class_name << endl;
    string new_name = names_for_record_decls[decl];
    if (!new_name.empty()) {
      cerr << "Got type alias: " << new_name << endl;
      name_alias = new_name;
    } else {
      cerr << "no type alias" << endl;
    }
    cerr << "Went from " << this->annotations.get().size()
         << " annotations to ";
    this->annotations = Annotations(this->decl);
    cerr << this->annotations.get().size() << endl;
  }

  std::string make_sfinae_to_match_wrapped_class() const {

    // if it was found by annotation, there's no way for V8ClassWrapper to know
    // about it other than
    //   explicit sfinae for the specific class.  Inheritance can be handled via
    //   a single std::is_base_of
    if (found_method == FOUND_ANNOTATION) {
      return fmt::format("std::is_same<T, {}>::value", class_name);
    } else {
      return "";
    }
  }

  bool should_be_wrapped() const {
    auto a = class_name;
    auto b = found_method;
    auto c = join(annotations.get());
    cerr << fmt::format("In should be wrapped with class {}, found_method: {}, "
                        "annotations: {}",
                        a, b, c)
         << endl;

    if (annotations.has(V8TOOLKIT_NONE_STRING) &&
        annotations.has(V8TOOLKIT_ALL_STRING)) {
      data_error(fmt::format(
          "type has both NONE_STRING and ALL_STRING - this makes no sense",
          class_name));
    }

    if (found_method == FOUND_BASE_CLASS) {
      return true;
    }
    if (found_method == FOUND_GENERATED) {
      return true;
    }

    if (found_method == FOUND_INHERITANCE) {
      if (annotations.has(V8TOOLKIT_NONE_STRING)) {
        cerr << "Found NONE_STRING" << endl;
        return false;
      }
    } else if (found_method == FOUND_ANNOTATION) {
      if (annotations.has(V8TOOLKIT_NONE_STRING)) {
        cerr << "Found NONE_STRING" << endl;
        return false;
      }
      if (!annotations.has(V8TOOLKIT_ALL_STRING)) {
        llvm::report_fatal_error(
            fmt::format("Type was supposedly found by annotation, but "
                        "annotation not found: {}",
                        class_name));
      }
    } else if (found_method == FOUND_UNSPECIFIED) {
      if (annotations.has(V8TOOLKIT_NONE_STRING)) {
        cerr << "Found NONE_STRING on UNSPECIFIED" << endl;
        return false;
      }
      if (!annotations.has(V8TOOLKIT_ALL_STRING)) {
        cerr << "didn't find all string on UNSPECIFIED" << endl;
        return false;
      }
    }

    if (base_types.size() > 1) {
      data_error(fmt::format("trying to see if type should be wrapped but it "
                             "has more than one base type -- unsupported",
                             class_name));
    }

    /*
              // *** IF A TYPE SHOULD BE WRAPPED THAT FORCES ITS PARENT TYPE TO
       BE WRAPPED ***


            if (base_types.empty()) {
            cerr << "no base typ so SHOULD BE WRAPPED" << endl;
            return true;
            } else {
            cerr << "Checking should_be_wrapped for base type" << endl;
            auto & base_type_wrapped_class = **base_types.begin();
            cerr << "base type is '" << base_type_wrapped_class.class_name <<
       "'" << endl;
            return base_type_wrapped_class.should_be_wrapped();
            }
            */

    return true;
  }

  bool ready_for_wrapping(set<WrappedClass *> dumped_classes) const {

    if (!valid) {
      cerr << "'invalid' class" << endl;
      return false;
    }

    // don't double wrap yourself
    if (find(dumped_classes.begin(), dumped_classes.end(), this) !=
        dumped_classes.end()) {
      printf("Already wrapped %s\n", class_name.c_str());
      return false;
    }

    if (!should_be_wrapped()) {
      cerr << "should be wrapped returned false" << endl;
      return false;
    }

    /*
                // if all this class's directly derived types have been wrapped,
       then we're good since their
                //   dependencies would have to be met for them to be wrapped
                for (auto derived_type : derived_types) {
                    if (find(dumped_classes.begin(), dumped_classes.end(),
       derived_type) == dumped_classes.end()) {
                        printf("Couldn't find %s\n",
       derived_type->class_name.c_str());
                        return false;
                    }
                }
            */
    for (auto base_type : base_types) {
      if (find(dumped_classes.begin(), dumped_classes.end(), base_type) ==
          dumped_classes.end()) {
        printf("base type %s not already wrapped - return false\n",
               base_type->class_name.c_str());
        return false;
      }
    }

    printf("Ready to wrap %s\n", class_name.c_str());

    return true;
  }

  // return all the header files for all the types used by all the base types of
  // the specified type
  std::set<string> get_base_type_includes() {
    set<string> results{this->my_include};
    results.insert(this->include_files.begin(), this->include_files.end());
    std::cerr << fmt::format("adding base type include for {}",
                             this->class_name)
              << std::endl;

    for (WrappedClass *base_class : this->base_types) {
      auto base_results = base_class->get_base_type_includes();
      results.insert(base_results.begin(), base_results.end());
    }

    return results;
  }

  std::set<string> get_derived_type_includes() {
    cerr << fmt::format("Getting derived type includes for {}", name_alias)
         << endl;
    set<string> results;
    results.insert(my_include);
    for (auto derived_type : derived_types) {

      results.insert(derived_type->include_files.begin(),
                     derived_type->include_files.end());

      auto derived_includes = derived_type->get_derived_type_includes();
      results.insert(derived_includes.begin(), derived_includes.end());

      cerr << fmt::format("{}: Derived type includes for subclass {} and all "
                          "its derived classes: {}",
                          name_alias, derived_type->class_name,
                          join(derived_includes))
           << endl;
    }
    return results;
  }

  WrappedClass(const WrappedClass &) = delete;
  WrappedClass &operator=(const WrappedClass &) = delete;

  // for newly created classes --- used for bidirectional classes that don't
  // actually exist in the AST
  WrappedClass(const std::string class_name,
               CompilerInstance &compiler_instance)
      : decl(nullptr), class_name(class_name), name_alias(class_name),
        compiler_instance(compiler_instance),
        valid(true), // explicitly generated, so must be valid
        found_method(FOUND_GENERATED) {}

  std::string generate_js_stub() {
    struct MethodParam {
      string type = "";
      string name = "";
      string description = "no description available";

      void convert_type(std::string const &indentation = "") {
        std::smatch matches;
        std::cerr << fmt::format("{} converting {}...", indentation, this->type)
                  << std::endl;
        this->type = regex_replace(type, std::regex("^(struct|class) "), "");
        for (auto &pair : cpp_to_js_type_conversions) {
          if (regex_match(this->type, matches, std::regex(pair.first))) {
            std::cerr << fmt::format("{} matched {}, converting to {}",
                                     indentation, pair.first, pair.second)
                      << std::endl;
            auto new_type = pair.second; // need a temp because the regex
                                         // matches point into the current
                                         // this->type

            // look for $1, $2, etc in resplacement and substitute in the
            // matching position
            for (size_t i = 1; i < matches.size(); i++) {
              // recursively convert the matched type
              MethodParam mp;
              mp.type = matches[i].str();
              mp.convert_type(indentation + "   ");
              new_type = std::regex_replace(
                  new_type, std::regex(fmt::format("[$]{}", i)), mp.type);
            }
            this->type = new_type;
            std::cerr << fmt::format("{}... final conversion to: {}",
                                     indentation, this->type)
                      << std::endl;
          }
        }
      }
    }; //  MethodParam

    stringstream result;
    string indentation = "    ";

    result << "/**\n";
    result << fmt::format(" * @class {}\n", this->name_alias);

    for (auto field : this->fields) {
      MethodParam field_type;
      field_type.name = field->getNameAsString();
      field_type.type = field->getType().getAsString();
      field_type.convert_type();
      result << fmt::format(" * @property {{{}}} {} \n", field_type.type,
                            field_type.name);
    }
    result << fmt::format(" **/\n", indentation);

    result << fmt::format("class {} {{\n", this->name_alias);

    for (auto method_decl : this->wrapped_methods_decls) {

      vector<MethodParam> parameters;
      MethodParam return_value_info;
      string method_description;

      auto parameter_count = method_decl->getNumParams();
      for (unsigned int i = 0; i < parameter_count; i++) {
        MethodParam this_param;
        auto param_decl = method_decl->getParamDecl(i);
        auto parameter_name = param_decl->getNameAsString();
        if (parameter_name == "") {
          data_warning(fmt::format(
              "class {} method {} parameter index {} has no variable name",
              this->name_alias, method_decl->getNameAsString(), i));
          parameter_name =
              fmt::format("unspecified_position_{}", parameters.size());
        }
        this_param.name = parameter_name;
        auto type = get_plain_type(param_decl->getType());
        this_param.type = type.getAsString();
        parameters.push_back(this_param);
      }

      return_value_info.type =
          get_plain_type(method_decl->getReturnType()).getAsString();

      FullComment *comment =
          this->compiler_instance.getASTContext().getCommentForDecl(method_decl,
                                                                    nullptr);
      if (comment != nullptr) {
        cerr << "**1" << endl;
        auto comment_text = get_source_for_source_range(
            this->compiler_instance.getPreprocessor().getSourceManager(),
            comment->getSourceRange());

        cerr << "FullComment: " << comment_text << endl;
        for (auto i = comment->child_begin(); i != comment->child_end(); i++) {
          cerr << "**2.1" << endl;

          auto child_comment_source_range = (*i)->getSourceRange();
          if (child_comment_source_range.isValid()) {
            cerr << "**2.2" << endl;

            auto child_comment_text = get_source_for_source_range(
                this->compiler_instance.getPreprocessor().getSourceManager(),
                child_comment_source_range);

            if (auto param_command = dyn_cast<ParamCommandComment>(*i)) {
              cerr << "Is ParamCommandComment" << endl;
              auto command_param_name =
                  param_command->getParamName(comment).str();

              auto matching_param_iterator =
                  std::find_if(parameters.begin(), parameters.end(),
                               [&command_param_name](auto &param) {
                                 return command_param_name == param.name;
                               });

              if (param_command->hasParamName() &&
                  matching_param_iterator != parameters.end()) {

                auto &param_info = *matching_param_iterator;
                if (param_command->getParagraph() != nullptr) {
                  cerr << "**3" << endl;
                  param_info.description = get_source_for_source_range(
                      this->compiler_instance.getPreprocessor()
                          .getSourceManager(),
                      param_command->getParagraph()->getSourceRange());
                }
              } else {
                data_warning(fmt::format("method parameter comment name "
                                         "doesn't match any parameter {}",
                                         command_param_name));
              }
            } else {
              cerr << "is not param command comment" << endl;
            }
            cerr << "Child comment " << (*i)->getCommentKind() << ": "
                 << child_comment_text << endl;
          }
        }
      } else {
        cerr << "No comment on " << method_decl->getNameAsString() << endl;
      }

      result << fmt::format("{}/**\n", indentation);
      for (auto &param : parameters) {
        param.convert_type(); // change to JS types
        result << fmt::format("{} * @param {} {{{}}} {}\n", indentation,
                              param.name, param.type, param.description);
      }
      return_value_info.convert_type();
      result << fmt::format("{} * @return {{{}}} {}\n", indentation,
                            return_value_info.type,
                            return_value_info.description);
      result << fmt::format("{} */\n", indentation);
      if (method_decl->isStatic()) {
        result << fmt::format("{}static ", indentation);
      }

      result << fmt::format("{}{}(", indentation,
                            method_decl->getNameAsString());
      bool first_parameter = true;
      for (auto &param : parameters) {
        if (!first_parameter) {
          result << ", ";
        }
        first_parameter = false;
        result << fmt::format("{}", param.name);
      }
      result << fmt::format("){{}}\n\n");
    }

    result << fmt::format("}}\n\n\n");
    fprintf(stderr, "%s", result.str().c_str());
    return result.str();
  }

  // for classes actually in the code being parsed
  WrappedClass(const CXXRecordDecl *decl, CompilerInstance &compiler_instance,
               FOUND_METHOD found_method)
      : decl(decl), class_name(get_canonical_name_for_decl(decl)),
        name_alias(decl->getNameAsString()),
        compiler_instance(compiler_instance),

        my_include(get_include_for_type_decl(compiler_instance, decl)),
        annotations(decl), found_method(found_method) {
    fprintf(stderr, "Creating WrappedClass for record decl ptr: %p\n",
            (void *)decl);
    string using_name = names_for_record_decls[decl];
    if (!using_name.empty()) {
      cerr << fmt::format("Setting name alias for {} to {} because of a "
                          "'using' statement",
                          class_name, using_name)
           << endl;
      name_alias = using_name;
    }

    cerr << "Top of WrappedClass constructor body" << endl;
    if (class_name == "") {
      fprintf(stderr, "%p\n", (void *)decl);
      llvm::report_fatal_error("Empty name string for decl");
    }

    auto pattern = this->decl->getTemplateInstantiationPattern();
    if (pattern && pattern != this->decl) {
      if (!pattern->isDependentType()) {
        llvm::report_fatal_error(fmt::format(
            "template instantiation class's pattern isn't dependent - this is "
            "not expected from my understanding: {}",
            class_name));
      }
    }

    //	    instantiation_pattern = pattern;
    // annotations.merge(Annotations(instantiation_pattern));

    if (auto specialization =
            dyn_cast<ClassTemplateSpecializationDecl>(this->decl)) {
      annotations.merge(Annotations(specialization->getSpecializedTemplate()));
    }

    cerr << "Final wrapped class annotations: " << endl;
    print_vector(annotations.get());
  }

  std::string get_derived_classes_string(int level = 0,
                                         const std::string indent = "") {
    vector<string> results;
    //            printf("%s In (%d) %s looking at %d derived classes\n",
    //            indent.c_str(), level, class_name.c_str(),
    //            (int)derived_types.size());
    for (WrappedClass *derived_class : derived_types) {
      results.push_back(derived_class->class_name);
      // only use directly derived types now
      // results.push_back(derived_class->get_derived_classes_string(level + 1,
      // indent + "  "));
    }
    //            printf("%s Returning %s\n", indent.c_str(),
    //            join(results).c_str());
    return join(results);
  }

  std::string get_base_class_string() {
    auto i = base_types.begin();
    while (i != base_types.end()) {
      if (std::find(base_types_to_ignore.begin(), base_types_to_ignore.end(),
                    (*i)->class_name) != base_types_to_ignore.end()) {
        base_types.erase(i++); // move iterator before erasing
      } else {
        i++;
      }
    };
    if (base_types.size() > 1) {
      data_error(
          fmt::format("Type {} has more than one base class - this isn't "
                      "supported because javascript doesn't support MI\n",
                      class_name));
    }
    return base_types.size() ? (*base_types.begin())->class_name : "";
  }

  std::string get_wrapper_string() {
    stringstream result;
    string indentation = "  ";

    result << indentation << "{\n";
    result << fmt::format("{}  // {}", indentation, class_name) << "\n";
    result << fmt::format("{}  v8toolkit::V8ClassWrapper<{}> & class_wrapper = "
                          "isolate.wrap_class<{}>();\n",
                          indentation, class_name, class_name);
    result << fmt::format("{}  class_wrapper.set_class_name(\"{}\");\n",
                          indentation, name_alias);

    for (auto &method : methods) {
      result << method;
    }
    for (auto &member : members) {
      result << member;
    }
    for (auto &wrapper_extension_method : wrapper_extension_methods) {
      result << fmt::format("{}  {}\n", indentation, wrapper_extension_method);
    }
    if (!derived_types.empty()) {
      result << fmt::format("{}  class_wrapper.set_compatible_types<{}>();\n",
                            indentation, get_derived_classes_string());
    }
    if (get_base_class_string() != "") {
      result << fmt::format("{}  class_wrapper.set_parent_type<{}>();\n",
                            indentation, get_base_class_string());
    }
    result << fmt::format("{}  class_wrapper.finalize(true);\n", indentation);

    for (auto &constructor : constructors) {
      result << constructor;
    }

    result << indentation << "}\n\n";
    return result.str();
  }
};

std::vector<WrappedClass *> definitions_to_process;

void add_definition(WrappedClass &wrapped_class) {
  if (!wrapped_class.decl->isThisDeclarationADefinition()) {
    llvm::report_fatal_error(
        fmt::format("tried to add non-definition to definition list for post "
                    "processing: {}",
                    wrapped_class.class_name)
            .c_str());
  }
  if (std::find(definitions_to_process.begin(), definitions_to_process.end(),
                &wrapped_class) == definitions_to_process.end()) {
    definitions_to_process.push_back(&wrapped_class);
  }
}

std::vector<unique_ptr<WrappedClass>> wrapped_classes;

/*
    vector<WrappedClass *> get_wrapped_class_regex(const string & regex_string)
   {
        cerr << "Searching with regex: " << regex_string << endl;
        vector<WrappedClass *> results;
        for (auto & wrapped_class : wrapped_classes) {
            cerr << " -- " << wrapped_class->class_name << endl;
            if (regex_search(wrapped_class->class_name, regex(regex_string))) {
                cerr << " -- ** MATCH ** " << endl;
                results.push_back(wrapped_class.get());
            }
        }
        cerr << fmt::format("Returning {} results", results.size()) << endl;
        return results;
    }
    */

bool has_wrapped_class(const CXXRecordDecl *decl) {
  auto class_name = get_canonical_name_for_decl(decl);

  for (auto &wrapped_class : wrapped_classes) {

    if (wrapped_class->class_name == class_name) {
      return true;
    }
  }
  return false;
}

WrappedClass &get_or_insert_wrapped_class(const CXXRecordDecl *decl,
                                          CompilerInstance &compiler_instance,
                                          FOUND_METHOD found_method) {

  if (decl->isDependentType()) {
    throw exception();
  }

  auto class_name = get_canonical_name_for_decl(decl);

  if (!decl->isThisDeclarationADefinition()) {

    cerr << class_name << " is not a definition - getting definition..."
         << endl;
    if (!decl->hasDefinition()) {

      llvm::report_fatal_error(
          fmt::format("{} doesn't have a definition", class_name).c_str());
    }

    decl = decl->getDefinition();
  }

  // fprintf(stderr, "get or insert wrapped class %p\n", (void*)decl);
  // fprintf(stderr, " -- class name %s\n", class_name.c_str());
  for (auto &wrapped_class : wrapped_classes) {

    if (wrapped_class->class_name == class_name) {

      // promote found_method if FOUND_BASE_CLASS is specified - the type must
      // ALWAYS be wrapped
      //   if it is the base of a wrapped type
      if (found_method == FOUND_BASE_CLASS) {

        // if the class wouldn't otherwise be wrapped, need to make sure no
        // constructors are created
        if (!wrapped_class->should_be_wrapped()) {
          wrapped_class->force_no_constructors = true;
        }
        wrapped_class->found_method = FOUND_BASE_CLASS;
      }
      // fprintf(stderr, "returning existing object: %p\n", (void
      // *)wrapped_class.get());
      return *wrapped_class;
    }
  }

  auto up =
      std::make_unique<WrappedClass>(decl, compiler_instance, found_method);

  wrapped_classes.emplace_back(std::move(up));

  // fprintf(stderr, "get or insert wrapped class returning new object: %p\n",
  // (void*)wrapped_classes.back().get());
  return *wrapped_classes.back();
}

string get_sfinae_matching_wrapped_classes(
    const vector<unique_ptr<WrappedClass>> &wrapped_classes) {
  vector<string> sfinaes;
  string forward_declarations =
      "#define V8TOOLKIT_V8CLASSWRAPPER_FORWARD_DECLARATIONS ";
  for (auto &wrapped_class : wrapped_classes) {
    if (wrapped_class->found_method == FOUND_INHERITANCE) {
      continue;
    }
    if (!wrapped_class->should_be_wrapped()) {
      continue;
    }
    sfinaes.emplace_back(wrapped_class->make_sfinae_to_match_wrapped_class());
    forward_declarations += wrapped_class->class_name + "; ";
  }

  for (int i = sfinaes.size() - 1; i >= 0; i--) {
    if (sfinaes[i] == "") {
      sfinaes.erase(sfinaes.begin() + i);
    }
  }

  // too many forward declarations do bad things to compile time / ram usage, so
  // try to catch any silly mistakes
  if (sfinaes.size() > 40 /* 40 is arbitrary */) {
    cerr << join(sfinaes, " || ") << endl;
    llvm::report_fatal_error("more 'sfinae's than arbitrary number used to "
                             "catch likely errors - can be increased if "
                             "needed");
  }

  std::string sfinae = "";
  if (!sfinaes.empty()) {
    sfinae = string("#define V8TOOLKIT_V8CLASSWRAPPER_FULL_TEMPLATE_SFINAE ") +
             join(sfinaes, " || ") + "\n";
  } // else if it's empty, leave it undefined

  forward_declarations += "\n";
  return sfinae + "\n" + forward_declarations;
}

EXPORT_TYPE get_export_type(const NamedDecl *decl, EXPORT_TYPE previous) {
  auto &attrs = decl->getAttrs();
  EXPORT_TYPE export_type = previous;

  auto name = decl->getNameAsString();

  bool found_export_specifier = false;

  for (auto attr : attrs) {
    if (dyn_cast<AnnotateAttr>(attr)) {
      auto attribute_attr = dyn_cast<AnnotateAttr>(attr);
      auto annotation_string = attribute_attr->getAnnotation().str();

      if (annotation_string == V8TOOLKIT_ALL_STRING) {
        if (found_export_specifier) {
          data_error(
              fmt::format("Found more than one export specifier on {}", name));
        }

        export_type = EXPORT_ALL;
        found_export_specifier = true;
      } else if (annotation_string == "v8toolkit_generate_bindings_some") {
        if (found_export_specifier) {
          data_error(
              fmt::format("Found more than one export specifier on {}", name));
        }
        export_type = EXPORT_SOME;
        found_export_specifier = true;
      } else if (annotation_string == "v8toolkit_generate_bindings_except") {
        if (found_export_specifier) {
          data_error(
              fmt::format("Found more than one export specifier on {}", name)
                  .c_str());
        }
        export_type = EXPORT_EXCEPT;
        found_export_specifier = true;
      } else if (annotation_string == V8TOOLKIT_NONE_STRING) {
        if (found_export_specifier) {
          data_error(
              fmt::format("Found more than one export specifier on {}", name)
                  .c_str());
        }
        export_type = EXPORT_NONE; // just for completeness
        found_export_specifier = true;
      }
    }
  }

  // go through bases looking for specific ones
  if (const CXXRecordDecl *record_decl = dyn_cast<CXXRecordDecl>(decl)) {
    for (auto &base : record_decl->bases()) {
      auto type = base.getType();
      auto base_decl = type->getAsCXXRecordDecl();
      auto base_name = get_canonical_name_for_decl(base_decl);
      //                cerr << "%^%^%^%^%^%^%^% " <<
      //                get_canonical_name_for_decl(base_decl) << endl;
      if (base_name == "class v8toolkit::WrappedClassBase") {
        cerr << "FOUND WRAPPED CLASS BASE -- EXPORT_ALL" << endl;
        if (found_export_specifier) {
          data_error(
              fmt::format("Found more than one export specifier on {}", name)
                  .c_str());
        }
        export_type = EXPORT_ALL;
        found_export_specifier = true;
      }
    }
  }

  //        printf("Returning export type: %d for %s\n", export_type,
  //        name.c_str());
  return export_type;
}

//    std::string strip_path_from_filename(const std::string & filename) {
//
//        // naive regex to grab everything after the last slash or backslash
//        auto regex = std::regex("([^/\\\\]*)$");
//
//        std::smatch matches;
//        if (std::regex_search(filename, matches, regex)) {
//            return matches[1];
//        }
//        if (print_logging) cerr << fmt::format("Unrecognizable filename {}",
//        filename);
//        throw std::exception();
//    }

#if 0


    // Returns true if qual_type is a 'trivial' std:: type like
    //   std::string
    bool is_trivial_std_type(QualType & qual_type, std::string & output) {
        std::string name = qual_type.getAsString();
        std::string canonical_name = qual_type.getCanonicalType().getAsString();

        // if it's a std:: type and not explicitly user-specialized, pass it through
        if (std::regex_match(name, regex("^(const\\s+|volatile\\s+)*(class\\s+|struct\\s+)?std::[^<]*$"))) {
            output = handle_std(name);a
            return true;
        }
        // or if the canonical type has std:: in it and it's not user-customized
        else if (has_std(canonical_name) &&
                 std::regex_match(name, regex("^[^<]*$"))) {
            output = handle_std(name);
            return true;
        }
        return false;
    }

    // Returns true if qual_type is a 'non-trivial' std:: type (containing user-specified template types like
    //   std::map<MyType1, MyType2>
    bool is_nontrivial_std_type(QualType & qual_type, std::string & output) {

        std::string name = qual_type.getAsString();
        std::string canonical_name = qual_type.getCanonicalType().getAsString();
        if (print_logging) cerr << "Checking nontrivial std type on " << name << " : " << canonical_name << endl;
        smatch matches;


        // if it's in standard (according to its canonical type) and has user-specified types
        if (has_std(canonical_name) &&
                 std::regex_match(name, matches, regex("^([^<]*<).*$"))) {
            output = handle_std(matches[1].str());
            if (print_logging) cerr << "Yes" << endl;
            return true;
        }
        if (print_logging) cerr << "No" << endl;
        return false;
    }

#endif

std::string get_type_string(QualType qual_type,
                            const std::string &indentation = "") {

  auto original_qualifiers = qual_type.getLocalFastQualifiers();
  // chase any typedefs to get the "real" type
  while (auto typedef_type = dyn_cast<TypedefType>(qual_type)) {
    qual_type = typedef_type->getDecl()->getUnderlyingType();
  }

  // re-apply qualifiers to potentially new qualtype
  qual_type.setLocalFastQualifiers(original_qualifiers);

  auto canonical_qual_type = qual_type.getCanonicalType();
  //        cerr << "canonical qual type typeclass: " <<
  //        canonical_qual_type->getTypeClass() << endl;
  auto canonical = canonical_qual_type.getAsString();
  return regex_replace(canonical, regex("std::__1::"), "std::");

#if 0
        std::string source = input_source;

        bool turn_logging_off = false;
	/*
        if (regex_match(qual_type.getAsString(), regex("^.*glm.*$") )) {
            print_logging++;
            turn_logging_off = true;
        }
	*/
        
        if (print_logging) cerr << indentation << "Started at " << qual_type.getAsString() << endl;
        if (print_logging) cerr << indentation << "  And canonical name: " << qual_type.getCanonicalType().getAsString() << endl;
        if (print_logging) cerr << indentation << "  And source " << source << endl;

        std::string std_result;
        if (is_trivial_std_type(qual_type, std_result)) {
            if (print_logging) cerr << indentation << "Returning trivial std:: type: " << std_result << endl << endl;
            if (turn_logging_off) print_logging--;
            return std_result;
        }

        bool is_reference = qual_type->isReferenceType();
        string reference_suffix = is_reference ? "&" : "";
        qual_type = qual_type.getNonReferenceType();

        stringstream pointer_suffix;
        bool changed = true;
        while(changed) {
            changed = false;
            if (!qual_type->getPointeeType().isNull()) {
                changed = true;
                pointer_suffix << "*";
                qual_type = qual_type->getPointeeType();
                if (print_logging) cerr << indentation << "stripped pointer, went to: " << qual_type.getAsString() << endl;
                continue; // check for more pointers first
            }

            // This code traverses all the typdefs and pointers to get to the actual base type
            if (dyn_cast<TypedefType>(qual_type) != nullptr) {
                changed = true;
                if (print_logging) cerr << indentation << "stripped typedef, went to: " << qual_type.getAsString() << endl;
                qual_type = dyn_cast<TypedefType>(qual_type)->getDecl()->getUnderlyingType();
                source = ""; // source is invalidated if it's a typedef
            }
        }

        if (print_logging) cerr << indentation << "CHECKING TO SEE IF " << qual_type.getUnqualifiedType().getAsString() << " is a template specialization"<< endl;
        auto base_type_record_decl = qual_type.getUnqualifiedType()->getAsCXXRecordDecl();
        if (dyn_cast<ClassTemplateSpecializationDecl>(base_type_record_decl)) {



            if (print_logging) cerr << indentation << "!!!!! Started with template specialization: " << qual_type.getAsString() << endl;



            std::smatch matches;
            string qual_type_string = qual_type.getAsString();

            std::string std_type_output;
            bool nontrivial_std_type = false;
            if (is_nontrivial_std_type(qual_type, std_type_output)) {
                if (print_logging) cerr << indentation << "is nontrivial std type and got result: " << std_type_output << endl;
                nontrivial_std_type = true;
                result << std_type_output;
            }
            // Get everything EXCEPT the template parameters into matches[1] and [2]
            else if (!regex_match(qual_type_string, matches, regex("^([^<]+<).*(>[^>]*)$"))) {
                if (print_logging) cerr << indentation << "short circuiting on " << original_qual_type.getAsString() << endl;
                if (turn_logging_off) print_logging--;
                return original_qual_type.getAsString();
            } else {
                result << matches[1];
                if (print_logging) cerr << indentation << "is NOT nontrivial std type" << endl;
            }
            auto template_specialization_decl = dyn_cast<ClassTemplateSpecializationDecl>(base_type_record_decl);

            auto user_specified_template_parameters = count_top_level_template_parameters(source);


            auto & template_arg_list = template_specialization_decl->getTemplateArgs();
            if (user_specified_template_parameters.size() > template_arg_list.size()) {
		llvm::report_fatal_error(fmt::format("ERROR: detected template parameters > actual list size for {}", qual_type_string).c_str());
            }

            auto template_types_to_handle = user_specified_template_parameters.size();
            if (source == "") {
                // if a typedef was followed, then user sources is meaningless
                template_types_to_handle = template_arg_list.size();
            }

//            for (decltype(template_arg_list.size()) i = 0; i < template_arg_list.size(); i++) {
            for (decltype(template_arg_list.size()) i = 0; i < template_types_to_handle; i++) {
                if (i > 0) {
                    result << ", ";
                }
                if (print_logging) cerr << indentation << "Working on template parameter " << i << endl;
                auto & arg = template_arg_list[i];

                switch(arg.getKind()) {
                    case clang::TemplateArgument::Type: {
                        if (print_logging) cerr << indentation << "processing as type argument" << endl;
                        auto template_arg_qual_type = arg.getAsType();
                        auto template_type_string = get_type_string(template_arg_qual_type,
                                                  indentation + "  ");
                        if (print_logging) cerr << indentation << "About to append " << template_type_string << " template type string onto existing: " << result.str() << endl;
                        result << template_type_string;
                        break; }
                    case clang::TemplateArgument::Integral: {
                        if (print_logging) cerr << indentation << "processing as integral argument" << endl;
                        auto integral_value = arg.getAsIntegral();
                        if (print_logging) cerr << indentation << "integral value radix10: " << integral_value.toString(10) << endl;
                        result << integral_value.toString(10);
                        break;}
                    default:
                        if (print_logging) cerr << indentation << "Oops, unhandled argument type" << endl;
                }
            }
            result << ">" << pointer_suffix.str() << reference_suffix;
            if (print_logging) cerr << indentation << "!!!!!Finished stringifying templated type to: " << result.str() << endl << endl;
            if (turn_logging_off) print_logging--;
            return result.str();

//        } else if (std::regex_match(qual_type.getAsString(), regex("^(class |struct )?std::.*$"))) {
//
//
//            if (print_logging) cerr << indentation << "checking " << qual_type.getAsString();
//            if (dyn_cast<TypedefType>(qual_type)) {
//                if (print_logging) cerr << indentation << " and returning " << dyn_cast<TypedefType>(qual_type)->getDecl()->getQualifiedNameAsString() <<
//                endl << endl;
//                return dyn_cast<TypedefType>(qual_type)->getDecl()->getQualifiedNameAsString() +
//                       (is_reference ? " &" : "");
//            } else {
//                if (print_logging) cerr << indentation << " and returning (no typedef) " << qual_type.getAsString() << endl << endl;
//                return qual_type.getAsString() + pointer_suffix.str() + reference_suffix;
//            }

        } else {

            // THIS APPROACH DOES NOT GENERATE PORTABLE STL NAMES LIKE THE LINE BELOW IS libc++ only not libstdc++
            // std::__1::basic_string<char, struct std::__1::char_traits<char>, class std::__1::allocator<char> >

            // this isn't great because it loses the typedef'd names of things, but it ALWAYS works
            // There is no confusion with reference types or typedefs or const/volatile
            // EXCEPT: it generates a elaborated type specifier which can't be used in certain places
            // http://en.cppreference.com/w/cpp/language/elaborated_type_specifier
            auto canonical_qual_type = original_qual_type.getCanonicalType();

            //printf("Canonical qualtype typedeftype cast: %p\n",(void*) dyn_cast<TypedefType>(canonical_qual_type));

            if (print_logging) cerr << indentation << "returning canonical: " << canonical_qual_type.getAsString() << endl << endl;
            if (turn_logging_off) print_logging--;

            return canonical_qual_type.getAsString();
        }
#endif
}

// Gets the "most basic" type in a type.   Strips off ref, pointer, CV
//   then calls out to get how to include that most basic type definition
//   and puts it in wrapped_class.include_files
void update_wrapped_class_for_type(
    CompilerInstance &compiler_instance, WrappedClass &wrapped_class,
    // don't capture qualtype by ref since it is changed in this function
    QualType qual_type) {

  bool print_logging = true;

  cerr << fmt::format("In update_wrapped_class_for_type {} in wrapped class {}",
                      qual_type.getAsString(), wrapped_class.class_name)
       << endl;

  if (print_logging)
    cerr << "Went from " << qual_type.getAsString();
  qual_type = qual_type.getLocalUnqualifiedType();

  // remove pointers
  while (!qual_type->getPointeeType().isNull()) {
    qual_type = qual_type->getPointeeType();
  }
  qual_type = qual_type.getLocalUnqualifiedType();

  if (print_logging)
    cerr << " to " << qual_type.getAsString() << endl;
  auto base_type_record_decl = qual_type->getAsCXXRecordDecl();

  if (auto function_type = dyn_cast<FunctionType>(&*qual_type)) {
    cerr << "IS A FUNCTION TYPE!!!!" << endl;

    // it feels strange, but the type int(bool) from std::function<int(bool)> is
    // a FunctionProtoType
    if (auto function_prototype = dyn_cast<FunctionProtoType>(function_type)) {
      cerr << "IS A FUNCTION PROTOTYPE" << endl;

      cerr << "Recursing on return type" << endl;
      update_wrapped_class_for_type(compiler_instance, wrapped_class,
                                    function_prototype->getReturnType());

      for (auto param : function_prototype->param_types()) {

        cerr << "Recursing on param type" << endl;
        update_wrapped_class_for_type(compiler_instance, wrapped_class, param);
      }
    } else {
      cerr << "IS NOT A FUNCTION PROTOTYPE" << endl;
    }

  } else {
    cerr << "is not a FUNCTION TYPE" << endl;
  }

  // primitive types don't have record decls
  if (base_type_record_decl == nullptr) {
    return;
  }

  auto actual_include_string =
      get_include_for_type_decl(compiler_instance, base_type_record_decl);

  if (print_logging)
    cerr << &wrapped_class << "Got include string for "
         << qual_type.getAsString() << ": " << actual_include_string << endl;

  // if there's no wrapped type, it may be something like a std::function or STL
  // container -- those are ok to not be wrapped
  if (has_wrapped_class(base_type_record_decl)) {
    auto &used_wrapped_class = get_or_insert_wrapped_class(
        base_type_record_decl, compiler_instance, FOUND_UNSPECIFIED);
    wrapped_class.used_classes.insert(&used_wrapped_class);
  }

  wrapped_class.include_files.insert(actual_include_string);

  if (dyn_cast<ClassTemplateSpecializationDecl>(base_type_record_decl)) {
    if (print_logging)
      cerr << "##!#!#!#!# Oh shit, it's a template type "
           << qual_type.getAsString() << endl;

    auto template_specialization_decl =
        dyn_cast<ClassTemplateSpecializationDecl>(base_type_record_decl);

    // go through the template args
    auto &template_arg_list = template_specialization_decl->getTemplateArgs();
    for (decltype(template_arg_list.size()) i = 0; i < template_arg_list.size();
         i++) {
      auto &arg = template_arg_list[i];

      // this code only cares about types, so skip non-type template arguments
      if (arg.getKind() != clang::TemplateArgument::Type) {
        continue;
      }
      auto template_arg_qual_type = arg.getAsType();
      if (template_arg_qual_type.isNull()) {
        if (print_logging)
          cerr << "qual type is null" << endl;
        continue;
      }
      if (print_logging)
        cerr << "Recursing on templated type "
             << template_arg_qual_type.getAsString() << endl;
      update_wrapped_class_for_type(compiler_instance, wrapped_class,
                                    template_arg_qual_type);
    }
  } else {
    if (print_logging)
      cerr << "Not a template specializaiton type " << qual_type.getAsString()
           << endl;
  }
}

vector<QualType> get_method_param_qual_types(const CXXMethodDecl *method,
                                             const string &annotation = "") {
  vector<QualType> results;
  auto parameter_count = method->getNumParams();
  for (unsigned int i = 0; i < parameter_count; i++) {
    auto param_decl = method->getParamDecl(i);
    Annotations annotations(param_decl);
    if (annotation != "" && !annotations.has(annotation)) {
      if (print_logging)
        cerr << "Skipping method parameter because it didn't have requested "
                "annotation: "
             << annotation << endl;
      continue;
    }
    auto param_qual_type = param_decl->getType();
    results.push_back(param_qual_type);
  }
  return results;
}

vector<string> generate_variable_names(vector<QualType> qual_types,
                                       bool with_std_move = false) {
  vector<string> results;
  for (std::size_t i = 0; i < qual_types.size(); i++) {
    if (with_std_move && qual_types[i]->isRValueReferenceType()) {
      results.push_back(fmt::format("std::move(var{})", i + 1));
    } else {
      results.push_back(fmt::format("var{}", i + 1));
    }
  }
  return results;
}

std::string get_method_parameters(CompilerInstance &compiler_instance,
                                  WrappedClass &wrapped_class,
                                  const CXXMethodDecl *method,
                                  bool add_leading_comma = false,
                                  bool insert_variable_names = false,
                                  const string &annotation = "") {
  std::stringstream result;
  bool first_param = true;
  auto type_list = get_method_param_qual_types(method, annotation);

  if (!type_list.empty() && add_leading_comma) {
    result << ", ";
  }
  int count = 0;
  auto var_names = generate_variable_names(type_list, false);
  for (auto &param_qual_type : type_list) {

    if (!first_param) {
      result << ", ";
    }
    first_param = false;

    auto type_string = get_type_string(param_qual_type);
    result << type_string;

    if (insert_variable_names) {
      result << " " << var_names[count++];
    }
    update_wrapped_class_for_type(compiler_instance, wrapped_class,
                                  param_qual_type);
  }
  return result.str();
}

std::string get_return_type(CompilerInstance &compiler_instance,
                            WrappedClass &wrapped_class,
                            const CXXMethodDecl *method) {
  auto qual_type = method->getReturnType();
  auto result = get_type_string(qual_type);
  //        auto return_type_decl = qual_type->getAsCXXRecordDecl();
  //        auto full_source_loc =
  //        FullSourceLoc(return_type_decl->getLocStart(), compiler_instance);
  //        auto header_file =
  //        strip_path_from_filename(compiler_instance.getFilename(full_source_loc).str());
  //        if (print_logging) cerr << fmt::format("{} needs {}",
  //        wrapped_class.class_name, header_file) << endl;
  //        wrapped_class.include_files.insert(header_file);
  //

  update_wrapped_class_for_type(compiler_instance, wrapped_class, qual_type);

  return result;
}

std::string get_method_return_type_class_and_parameters(
    CompilerInstance &compiler_instance, WrappedClass &wrapped_class,
    const CXXRecordDecl *klass, const CXXMethodDecl *method) {
  std::stringstream results;
  results << get_return_type(compiler_instance, wrapped_class, method);
  results << ", " << get_canonical_name_for_decl(klass);
  results << get_method_parameters(compiler_instance, wrapped_class, method,
                                   true);
  return results.str();
}

std::string get_method_return_type_and_parameters(
    CompilerInstance &compiler_instance, WrappedClass &wrapped_class,
    const CXXRecordDecl *klass, const CXXMethodDecl *method) {
  std::stringstream results;
  results << get_return_type(compiler_instance, wrapped_class, method);
  results << get_method_parameters(compiler_instance, wrapped_class, method,
                                   true);
  return results.str();
}

void print_specialization_info(const CXXRecordDecl *decl) {
  auto name = get_canonical_name_for_decl(decl);
  cerr << "*****" << endl;
  if (decl->isDependentType()) {
    cerr << fmt::format("{} is a dependent type", name) << endl;
  }
  if (auto spec = dyn_cast<ClassTemplateSpecializationDecl>(decl)) {
    fprintf(stderr, "decl is a ClassTemplateSpecializationDecl: %p\n",
            (void *)decl);
    cerr << name << endl;

    if (auto spec_tmpl = spec->getSpecializedTemplate()) {
      fprintf(stderr, "Specialized template: %p, %s\n", (void *)spec_tmpl,
              spec_tmpl->getQualifiedNameAsString().c_str());
      print_vector(Annotations(spec_tmpl).get(),
                   "specialized template annotations", "", false);
    } else {
      cerr << "no spec tmpl" << endl;
    }

    if (dyn_cast<ClassTemplatePartialSpecializationDecl>(decl)) {
      cerr << "It is also a partial specialization decl" << endl;
    } else {
      cerr << "It is NOT a PARTIAL specialization decl" << endl;
    }

  } else {
    cerr << name << " is not a class template specialization decl" << endl;
  }
  cerr << "*****END" << endl;
}

map<string, int> template_instantiations;

class ClassHandler : public MatchFinder::MatchCallback {
private:
  CompilerInstance &ci;
  SourceManager &source_manager;

  WrappedClass *top_level_class; // the class currently being wrapped
  std::set<std::string> names_used;

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
        //std::regex("remove_reference"))) {
        print_logging = true;
        cerr << fmt::format("Got class {}", class_name) << endl;
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

    if (const CXXMethodDecl *method =
            Result.Nodes.getNodeAs<clang::CXXMethodDecl>("method")) {
      auto method_name = method->getQualifiedNameAsString();
      const FunctionDecl *pattern = nullptr;

      if (!method->isTemplateInstantiation()) {
        return;
      }
#ifdef TEMPLATE_FILTER_STD
      if (std::regex_search(method_name, std::regex("^std::"))) {
        return;
      }
#endif

      pattern = method->getTemplateInstantiationPattern();
      if (!pattern) {
        pattern = method;
      }

      if (!pattern) {
        llvm::report_fatal_error(
            "method is template insantiation but pattern still nullptr");
      }

      FunctionTemplate::get_or_create(pattern).instantiated();
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
  void EndSourceFileAction() {
    cerr << fmt::format("Class template instantiations") << endl;
    vector<pair<string, int>> insts;
    for (auto &class_template : class_templates) {
      insts.push_back({class_template->name, class_template->instantiations});
    }
    std::sort(insts.begin(), insts.end(),
              [](auto &a, auto &b) { return a.second < b.second; });
    int skipped = 0;
    int total = 0;
    cerr << endl
         << fmt::format(
                "Class templates with more than {} or more instantiations:",
                TEMPLATED_CLASS_PRINT_THRESHOLD)
         << endl;
    for (auto &pair : insts) {
      total += pair.second;
      if (pair.second < TEMPLATED_CLASS_PRINT_THRESHOLD) {
        skipped++;
        continue;
      }
      cerr << pair.first << ": " << pair.second << endl;
      ;
    }
    cerr << endl;
    cerr << "Skipped " << skipped << " entries because they had fewer than "
         << TEMPLATED_CLASS_PRINT_THRESHOLD << " instantiations" << endl;
    cerr << "Total of " << total << " instantiations" << endl;
    skipped = 0;
    total = 0;
    insts.clear();
    for (auto &function_template : function_templates) {
      insts.push_back(
          {function_template->name, function_template->instantiations});
    }
    std::sort(insts.begin(), insts.end(),
              [](auto &a, auto &b) { return a.second < b.second; });
    cerr << endl
         << fmt::format(
                "Function templates with more than {} or more instantiations:",
                TEMPLATED_FUNCTION_PRINT_THRESHOLD)
         << endl;
    for (auto &pair : insts) {
      total += pair.second;
      if (pair.second < TEMPLATED_FUNCTION_PRINT_THRESHOLD) {
        skipped++;
        continue;
      }
      cerr << pair.first << ": " << pair.second << endl;
      ;
    }

    cerr << endl;
    cerr << "Skipped " << skipped << " entries because they had fewer than "
         << TEMPLATED_FUNCTION_PRINT_THRESHOLD << " instantiations" << endl;
    cerr << "Total of " << total << " instantiations" << endl;

    for (auto &warning : warnings) {
      cerr << warning << endl;
    }

    if (!errors.empty()) {
      cerr << "Errors detected:" << endl;
      for (auto &error : errors) {
        cerr << error << endl;
      }
      llvm::report_fatal_error("Errors detected in source data");
      exit(1);
    }
  }

protected:
  // The value returned here is used internally to run checks against
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) {
    return llvm::make_unique<MyASTConsumer>(CI);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) {
    for (unsigned i = 0, e = args.size(); i != e; ++i) {
      llvm::errs() << "PrintFunctionNames arg = " << args[i] << "\n";

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
    ros << "Help for PrintFunctionNames plugin goes here\n";
  }
};
}

static FrontendPluginRegistry::Add<PrintFunctionNamesAction>
    X("template-tool-count-templates",
      "print the template instantiation count");
