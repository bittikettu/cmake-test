#include "../unity/src/unity.h"
#include "funktiot.h"
#include <stdint.h>
// #include <stdio.h>

// Setup and Teardown functions
static uint8_t global_number;

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
  TEST_ASSERT_EQUAL_INT(
      15, add(global_number, 5)); // Assert that result should be 15
}

// A simple test case: testing subtraction
void test_Subtraction(void) {
  TEST_ASSERT_EQUAL_INT(
      5, subtract(global_number, 5)); // Assert that result should be 5
}

// A test with failure
void test_FailingTest(void) {
  uint16_t result = global_number * 2;
  TEST_ASSERT_EQUAL_INT(100, result); // This will fail because result is 20
}

void test_multiply(void) {
  TEST_ASSERT_EQUAL_INT(50, multiply(global_number, 5));
}

void test_divide(void) {
  TEST_ASSERT_EQUAL_INT(5, divide(global_number, 2));
  TEST_ASSERT_EQUAL_INT(0, divide(global_number, 0));
}
// Main function that runs the tests
int main(void) {
  // Initialize Unity test framework
  UNITY_BEGIN();

  // Run individual test functions
  RUN_TEST(test_Addition);
  RUN_TEST(test_Subtraction);
  RUN_TEST(test_multiply);
  RUN_TEST(test_divide);
  // RUN_TEST(test_FailingTest);

  // End the test and print results
  return UNITY_END();
}
