#ifndef TEMPLATE_PARSER_H
#define TEMPLATE_PARSER_H

/**
 * Use these to expose classes/class functions/class data members via javascript
 */
#define V8TOOLKIT_NONE_STRING "v8toolkit_generate_bindings_none"
#define V8TOOLKIT_ALL_STRING "v8toolkit_generate_bindings_all"
#define V8TOOLKIT_READONLY_STRING "v8toolkit_generate_bindings_readonly"
#define V8TOOLKIT_EXTEND_WRAPPER_STRING "v8toolkit_extend_wrapper"

/**
 * For setting a name alias to be used for javascript to refer to the type as
 * -- basically sets a different constructor name when you don't have control
 *    over the class definition
 * Usage: using MyTypeInt V8TOOLKIT_NAME_ALIAS = MyType<int>;
 *        using MyTypeChar V8TOOLKIT_NAME_ALIAS = MyType<char>;
 * Otherwise both of those would get the same constructor name (MyType) and code generation would fail
 */
#define V8TOOLKIT_NAME_ALIAS_STRING "v8toolkit_name_alias"
#define V8TOOLKIT_NAME_ALIAS                                                   \
  __attribute__((annotate(V8TOOLKIT_NAME_ALIAS_STRING)))

/**
 * Use this to create a JavaScript constructor function with the specified name
 */
#define V8TOOLKIT_CONSTRUCTOR_PREFIX "v8toolkit_bidirectional_constructor_"
#define V8TOOLKIT_CONSTRUCTOR(name)                                            \
  __attribute__((annotate(V8TOOLKIT_CONSTRUCTOR_PREFIX #name)))

/**
 * Use this to create a JavaScript constructor function with the specified name
 */
#define V8TOOLKIT_EXPOSE_STATIC_METHODS_AS_PREFIX                              \
  "v8toolkit_expose_static_methods_as_"
#define V8TOOLKIT_EXPOSE_STATIC_METHODS_AS(name)                               \
  __attribute__((annotate(V8TOOLKIT_EXPOSE_STATIC_METHODS_AS_PREFIX #name)))

/**
 * For classes with multiple inheritance, allows you to specify type(s) not to use.
 * Templates should be specified with only the base template name, not with template parameters
 *   e.g. MyTemplatedType not MyTemplatedType<int, char*> - does not support
 *        MI to select one where type inherits from two different versions of same template
 */
#define V8TOOLKIT_IGNORE_BASE_TYPE_PREFIX "v8toolkit_ignore_base_type_"
#define V8TOOLKIT_IGNORE_BASE_TYPE(name)                                       \
  __attribute__((annotate(V8TOOLKIT_IGNORE_BASE_TYPE_PREFIX #name)))

/**
 * For classes with multiple inheritance, allows you to specify which one to use
 * Templates should be specified with only the base template name, not with template parameters
 *   e.g. MyTemplatedType not MyTemplatedType<int, char*> - does not support
 *        MI to select one where type inherits from two different specializations of same template
 */
#define V8TOOLKIT_USE_BASE_TYPE_PREFIX "v8toolkit_use_base_type_"
#define V8TOOLKIT_USE_BASE_TYPE(name)                                          \
  __attribute__((annotate(V8TOOLKIT_USE_BASE_TYPE_PREFIX #name)))

/**
 * This can be specified in a forward declaration of a type to eliminate all constructors from being wrapped
 */
#define V8TOOLKIT_DO_NOT_WRAP_CONSTRUCTORS_STRING                              \
  "v8toolkit_do_not_wrap_constructors"
#define V8TOOLKIT_DO_NOT_WRAP_CONSTRUCTORS                                     \
  __attribute__((annotate(V8TOOLKIT_DO_NOT_WRAP_CONSTRUCTORS_STRING)))

#define V8TOOLKIT_BIDIRECTIONAL_CLASS_STRING "v8toolkit_generate_bidirectional"
#define V8TOOLKIT_BIDIRECTIONAL_CONSTRUCTOR_STRING                             \
  "v8toolkit_generate_bidirectional_constructor"
#define V8TOOLKIT_BIDIRECTIONAL_INTERNAL_PARAMETER_STRING                      \
  "V8toolkit_generate_bidirectional_internal_parameter"

#endif // TEMPLATE_PARSER_H
