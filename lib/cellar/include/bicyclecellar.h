#ifndef BICYCLE_CELLAR_H
#define BICYCLE_CELLAR_H

#include "../include/bicycle.h"

#define MAX_AMOUNT_OF_BICYCLES 100

// Define the BicycleCellar structure
typedef struct tBicycleCellar {
	Bicycle bicycles[MAX_AMOUNT_OF_BICYCLES];  // Maximum capacity of 100 bicycles
	int count;								   // Number of bicycles currently stored
} BicycleCellar;

/**
 * @brief Initialize new Bicycle cellar
 *
 * @param cellar Pointer to empty #BicycleCellar
 */
void cellar_initialize(BicycleCellar *cellar);

/**
 * @brief
 *
 * @param cellar
 * @param id
 * @param owner
 * @return true
 * @return false
 */
bool cellar_add_bicycle(BicycleCellar *cellar, int id, const char *owner);

/**
 * @brief
 *
 * @param cellar
 * @param id
 * @return true
 * @return false
 */
bool cellar_remove_bicycle(BicycleCellar *cellar, int id);

/**
 * @brief
 *
 * @param cellar
 * @param id
 * @param in_use
 * @return true
 * @return false
 */
bool cellar_mark_bicycle_in_use(BicycleCellar *cellar, int id, bool in_use);

/**
 * @brief
 *
 * @param cellar
 */
void cellar_print_bicycle_cellar(const BicycleCellar *cellar);

#endif	// BICYCLE_CELLAR_H
