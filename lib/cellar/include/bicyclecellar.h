#ifndef BICYCLE_CELLAR_H
#define BICYCLE_CELLAR_H

#include "../include/bicycle.h"

// Define the BicycleCellar structure
typedef struct tBicycleCellar {
    Bicycle bicycles[100]; // Maximum capacity of 100 bicycles
    int count;             // Number of bicycles currently stored
} BicycleCellar;

// Function prototypes
void initialize_bicycle_cellar(BicycleCellar *cellar);
bool add_bicycle(BicycleCellar *cellar, int id, const char *owner);
bool remove_bicycle(BicycleCellar *cellar, int id);
bool mark_bicycle_in_use(BicycleCellar *cellar, int id, bool in_use);
void print_bicycle_cellar(const BicycleCellar *cellar);

#endif // BICYCLE_CELLAR_H
