#include <stdint.h>
#include <stdio.h>
#include <version.h>
#include <calc/calc.h>
//#include <git_hashes.h>
#include <jansson.h>

int janssoni() {
    // JSON string
    const char *json_string = "{\"name\": \"Alice\", \"age\": 30, \"is_student\": false, \"skills\": [\"C\", \"C++\", \"Python\"]}";

    // Parse the JSON string
    json_error_t error;
    json_t *root = json_loads(json_string, 0, &error);
    if (!root) {
        fprintf(stderr, "Error parsing JSON: %s\n", error.text);
        return 1;
    }

    // Access and print individual elements
    const char *name = json_string_value(json_object_get(root, "name"));
    if (name) printf("Name: %s\n", name);

    json_t *age = json_object_get(root, "age");
    if (json_is_integer(age)) printf("Age: %lld\n", json_integer_value(age));

    json_t *is_student = json_object_get(root, "is_student");
    if (json_is_boolean(is_student)) printf("Is student: %s\n", json_is_true(is_student) ? "true" : "false");

    // Access and print array elements
    json_t *skills = json_object_get(root, "skills");
    if (json_is_array(skills)) {
        printf("Skills:\n");
        size_t index;
        json_t *value;
        json_array_foreach(skills, index, value) {
            printf("  - %s\n", json_string_value(value));
        }
    }

    // Cleanup
    json_decref(root);

    return 0;
}


int main(void) {
  printf("Ver: %d.%d.%d\n", PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH);
  janssoni();
  int sum = calc_add(2, 34);
  printf("sum %d\n", sum);
}
