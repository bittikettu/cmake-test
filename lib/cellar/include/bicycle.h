#ifndef BICYCLE_H
#define BICYCLE_H

#include <stdbool.h>

// Define the Bicycle structure
typedef struct tBicycle {
    int id;
    char owner[50];
    bool is_in_use;
} Bicycle;

// Function prototypes
void initialize_bicycle(Bicycle *bicycle, int id, const char *owner);
void set_bicycle_in_use(Bicycle *bicycle, bool in_use);
void print_bicycle(const Bicycle *bicycle);

#endif // BICYCLE_H
