#include <stdio.h>
#include <kivakirjasto/cellar.h>
#include <kivakirjasto/version_kivakirjasto.h>
//#include <bicycle.h>
//#include <bicyclecellar.h>

int main(void) {
    BicycleCellar cellar = {0};
    cellar_initialize(&cellar);
    cellar_add_bicycle(&cellar, 1, "Alice");
    cellar_add_bicycle(&cellar, 2, "Bob");
    cellar_add_bicycle(&cellar, 3, "Charlie");
    printf("Hello from version %s\n",KIVAKIRJASTO_VERSION_HASH);
}
