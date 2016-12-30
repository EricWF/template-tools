
// RUN: %cxx -std=c++14 %plugin_flags %s 2>&1 | FileCheck %s

// CHECK: Class templates with 10 or more instantiations
// CHECK-NEXT: Test: 50
template <int> struct Test {};
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
template class Test<__COUNTER__>;
