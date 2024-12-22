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
void bicycle_initialize(Bicycle *bicycle, int id, const char *owner);
void bicycle_set_in_use(Bicycle *bicycle, bool in_use);
void bicycle_print(const Bicycle *bicycle);

#endif	// BICYCLE_H
