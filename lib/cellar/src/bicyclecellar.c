#include "../include/bicyclecellar.h"
#include <stdio.h>
#include <string.h>

// Initialize the bicycle cellar
void initialize_bicycle_cellar(BicycleCellar *cellar) {
    cellar->count = 0;
}

// Add a new bicycle to the cellar
bool add_bicycle(BicycleCellar *cellar, int id, const char *owner) {
    if (cellar->count >= 100) {
        return false; // Cellar is full
    }
    for (int i = 0; i < cellar->count; i++) {
        if (cellar->bicycles[i].id == id) {
            return false; // Bicycle with the same ID already exists
        }
    }
    initialize_bicycle(&cellar->bicycles[cellar->count], id, owner);
    cellar->count++;
    return true;
}

// Remove a bicycle from the cellar by ID
bool remove_bicycle(BicycleCellar *cellar, int id) {
    for (int i = 0; i < cellar->count; i++) {
        if (cellar->bicycles[i].id == id) {
            // Shift bicycles to remove the found bicycle
            for (int j = i; j < cellar->count - 1; j++) {
                cellar->bicycles[j] = cellar->bicycles[j + 1];
            }
            cellar->count--;
            return true;
        }
    }
    return false; // Bicycle not found
}

// Mark a bicycle as in use or not
bool mark_bicycle_in_use(BicycleCellar *cellar, int id, bool in_use) {
    for (int i = 0; i < cellar->count; i++) {
        if (cellar->bicycles[i].id == id) {
            set_bicycle_in_use(&cellar->bicycles[i], in_use);
            return true;
        }
    }
    return false; // Bicycle not found
}

// Print the contents of the bicycle cellar
void print_bicycle_cellar(const BicycleCellar *cellar) {
    printf("Bicycle Cellar (%d bicycles):\n", cellar->count);
    for (int i = 0; i < cellar->count; i++) {
        print_bicycle(&cellar->bicycles[i]);
    }
}
