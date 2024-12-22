#include "../include/bicycle.h"
#include <stdio.h>
#include <string.h>

// Initialize a bicycle
void bicycle_initialize(Bicycle *bicycle, int id, const char *owner) {
    if (!bicycle) return;
    bicycle->id = id;
    strncpy(bicycle->owner, owner, sizeof(bicycle->owner) - 1);
    bicycle->owner[sizeof(bicycle->owner) - 1] = '\0';
    bicycle->is_in_use = false;
}

// Set the usage status of a bicycle
void bicycle_set_in_use(Bicycle *bicycle, bool in_use) {
    if (bicycle) {
        bicycle->is_in_use = in_use;
    }
}

// Print the details of a bicycle
void bicycle_print(const Bicycle *bicycle) {
    if (bicycle) {
        printf("Bicycle ID: %d, Owner: %s, In Use: %s\n",
               bicycle->id,
               bicycle->owner,
               bicycle->is_in_use ? "Yes" : "No");
    }
}
