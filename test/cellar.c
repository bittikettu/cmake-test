#include <stdint.h>
#include <unity.h>
#include <kivakirjasto/cellar.h>

BicycleCellar cellar = {0};

void setUp(void) {
  cellar_initialize(&cellar);
  cellar_add_bicycle(&cellar, 1, "Alice");
  cellar_add_bicycle(&cellar, 2, "Bob");
  cellar_add_bicycle(&cellar, 3, "Charlie");
}

void tearDown(void) {}

void cellar_should_contain_3_bikes(void) {
  TEST_ASSERT_EQUAL_INT(3, cellar.count);
}

void cellar_should_contain_2_after_remove_bikes(void) {
  cellar_remove_bicycle(&cellar, 1);
  TEST_ASSERT_EQUAL_INT(2, cellar.count);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(cellar_should_contain_3_bikes);
  RUN_TEST(cellar_should_contain_2_after_remove_bikes);

  return UNITY_END();
}
