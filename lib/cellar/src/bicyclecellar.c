#include "../include/bicyclecellar.h"
#include <stdio.h>

void cellar_initialize(BicycleCellar *cellar) {
	cellar->count = 0;
}

bool cellar_add_bicycle(BicycleCellar *cellar, int id, const char *owner) {
	if (cellar->count >= MAX_AMOUNT_OF_BICYCLES) {
		return false;  // Cellar is full
	}
	for (int i = 0; i < cellar->count; i++) {
		if (cellar->bicycles[i].id == id) {
			return false;  // Bicycle with the same ID already exists
		}
	}
	bicycle_initialize(&cellar->bicycles[cellar->count], id, owner);
	cellar->count++;
	return true;
}

bool cellar_remove_bicycle(BicycleCellar *cellar, int id) {
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
	return false;  // Bicycle not found
}

bool cellar_mark_bicycle_in_use(BicycleCellar *cellar, int id, bool in_use) {
	for (int i = 0; i < cellar->count; i++) {
		if (cellar->bicycles[i].id == id) {
			bicycle_set_in_use(&cellar->bicycles[i], in_use);
			return true;
		}
	}
	return false;  // Bicycle not found
}

void cellar_print_bicycle_cellar(const BicycleCellar *cellar) {
	printf("Bicycle Cellar (%d bicycles):\n", cellar->count);
	for (int i = 0; i < cellar->count; i++) {
		bicycle_print(&cellar->bicycles[i]);
	}
}
