#include <stdio.h>
#include <cellar.h>
//#include <bicycle.h>
//#include <bicyclecellar.h>

int main(void) {
    BicycleCellar cellar = {0};
    cellar_initialize(&cellar);
    cellar_add_bicycle(&cellar, 1, "Alice");
    cellar_add_bicycle(&cellar, 2, "Bob");
    cellar_add_bicycle(&cellar, 3, "Charlie");
    printf("Hello\n");
}
