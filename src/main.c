#include <stdint.h>
#include <unity.h>
#include <funktiot.h>
#include <version.h>

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

void test_divide_positive_numbers(void) {
  TEST_ASSERT_EQUAL_FLOAT(2.0f, divide(4.0f, 2.0f));
}

void test_divide_negative_numbers(void) {
  TEST_ASSERT_EQUAL_FLOAT(-2.0f, divide(-4.0f, 2.0f));
}

void test_divide_by_zero(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, divide(4.0f, 0.0f));
}

void test_divide_zero_by_number(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, divide(0.0f, 2.0f));
}

void test_divide_float_numbers(void) {
  TEST_ASSERT_EQUAL_FLOAT(1.5f, divide(3.0f, 2.0f));
}

// Write test case that would test the multiplication
void test_divide_with_zero_denominator(void) {
  double numerator = 10;
  double denominator = 0;
  double result = divide(numerator, denominator);

  TEST_ASSERT_EQUAL_FLOAT(result, INFINITY);
}

void test_divide_with_positive_numbers(void) {
  double numerator = 10;
  double denominator = 2;
  double result = divide(numerator, denominator);

  TEST_ASSERT_EQUAL_FLOAT(result, 5.0);
}

void test_divide_with_negative_numbers(void) {
  double numerator = -10;
  double denominator = 2;
  double result = divide(numerator, denominator);

  TEST_ASSERT_EQUAL_FLOAT(result, -5.0);
}

// Main function that runs the tests
int main(void) {
  // Initialize Unity test framework
  UNITY_BEGIN();
  printf("Ver: %d.%d.%d\n", PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR,
         PROJECT_VERSION_PATCH);
  // Run individual test functions
  RUN_TEST(test_Addition);
  RUN_TEST(test_Subtraction);
  RUN_TEST(test_multiply);
  RUN_TEST(test_divide);
  RUN_TEST(test_divide_positive_numbers);
  RUN_TEST(test_divide_negative_numbers);
  RUN_TEST(test_divide_by_zero);
  RUN_TEST(test_divide_zero_by_number);
  RUN_TEST(test_divide_float_numbers);

  // RUN_TEST(test_divide_with_zero_denominator);
  // RUN_TEST(test_divide_with_positive_numbers);
  // RUN_TEST(test_divide_with_negative_numbers);
  //  RUN_TEST(test_FailingTest);

  // End the test and print results
  return UNITY_END();
}
