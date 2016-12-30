
// RUN: %cxx -std=c++14 %plugin_flags %s 2>&1 | FileCheck %s

// CHECK: Class templates with 10 or more instantiations
// CHECK-DAG: Test: 50
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

// Test that implicit instantiations are also found
template <int> struct Test2 {};
void instantiate_test2() {
  // CHECK-DAG: Test2: 10
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
  { Test2<__COUNTER__> t; }
}
