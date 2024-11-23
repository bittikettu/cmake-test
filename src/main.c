#include "../unity/src/unity.h"
#include <stdio.h>

// Setup and Teardown functions
static int global_number;

// This function is called before each test is run
void setUp(void) {
  // Initialize variables or perform setup here
  global_number = 10;
}

// This function is called after each test has completed
void tearDown(void) {
  // Perform cleanup here
}

// A simple test case: testing an addition function
void test_Addition(void) {
  int result = global_number + 5;
  TEST_ASSERT_EQUAL_INT(15, result); // Assert that result should be 15
}

// A simple test case: testing subtraction
void test_Subtraction(void) {
  int result = global_number - 5;
  TEST_ASSERT_EQUAL_INT(5, result); // Assert that result should be 5
}

// A test with failure
void test_FailingTest(void) {
  int result = global_number * 2;
  TEST_ASSERT_EQUAL_INT(100, result); // This will fail because result is 20
}

// Main function that runs the tests
int main(void) {
  // Initialize Unity test framework
  UNITY_BEGIN();

  // Run individual test functions
  RUN_TEST(test_Addition);
  RUN_TEST(test_Subtraction);
  // RUN_TEST(test_FailingTest);

  // End the test and print results
  return UNITY_END();
}