
// RUN: %cxx -std=c++14 %plugin_flags %s 2>&1 | \
// RUN: FileCheck -check-prefix=CHECK-DEFAULT %s


// RUN: %cxx -std=c++14 %plugin_flags  %plugin_arg=-min_threshold=2 %s 2>&1 | \
// RUN: FileCheck -check-prefix=CHECK-MIN-FLAG %s

template <int> struct Test {};
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;

// CHECK-DEFAULT: Class templates with 10 or more instantiations:
// CHECK-DEFAULT: Skipped 1 entries because they had fewer than 10 instantiations

// CHECK-MIN-FLAG: Class templates with 2 or more instantiations:
// CHECK-MIN-FLAG: Skipped 0 entries because they had fewer than 2 instantiations
