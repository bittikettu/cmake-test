#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define the Bicycle structure
typedef struct {
    char model[50];
    float price;
    int gears;
    char color[20];
} Bicycle;

// Define the BicycleShop structure
typedef struct {
    Bicycle *inventory;
    int inventory_size;
    int max_inventory;
    int sales_count;
    float total_sales;
} BicycleShop;

// Function prototypes
void initBicycle(Bicycle *bike, const char *model, float price, int gears, const char *color);
void initBicycleShop(BicycleShop *shop, int max_inventory);
void addBikeToInventory(BicycleShop *shop, const char *model, float price, int gears, const char *color);
void sellBike(BicycleShop *shop, const char *model);
void printInventory(BicycleShop *shop);
void printSalesReport(BicycleShop *shop);

int main() {
    BicycleShop my_shop;
    initBicycleShop(&my_shop, 10); // Initialize shop with max inventory of 10 bikes

    // Add some bikes to the inventory
    addBikeToInventory(&my_shop, "Mountain Bike", 500.0, 21, "Red");
    addBikeToInventory(&my_shop, "Road Bike", 800.0, 18, "Blue");
    addBikeToInventory(&my_shop, "Hybrid Bike", 600.0, 24, "Green");

    // Print the inventory
    printf("Initial Inventory:\n");
    printInventory(&my_shop);

    // Sell a bike
    sellBike(&my_shop, "Mountain Bike");

    // Print updated inventory and sales report
    printf("\nUpdated Inventory:\n");
    printInventory(&my_shop);
    printf("\nSales Report:\n");
    printSalesReport(&my_shop);

    // Clean up memory
    free(my_shop.inventory);

    return 0;
}

// Initialize a Bicycle structure
void initBicycle(Bicycle *bike, const char *model, float price, int gears, const char *color) {
    strncpy(bike->model, model, sizeof(bike->model) - 1);
    bike->price = price;
    bike->gears = gears;
    strncpy(bike->color, color, sizeof(bike->color) - 1);
}

// Initialize a BicycleShop structure
void initBicycleShop(BicycleShop *shop, int max_inventory) {
    shop->inventory_size = 0;
    shop->max_inventory = max_inventory;
    shop->sales_count = 0;
    shop->total_sales = 0.0;
    shop->inventory = (Bicycle *)malloc(max_inventory * sizeof(Bicycle));
}

// Add a bike to the inventory
void addBikeToInventory(BicycleShop *shop, const char *model, float price, int gears, const char *color) {
    if (shop->inventory_size < shop->max_inventory) {
        initBicycle(&shop->inventory[shop->inventory_size], model, price, gears, color);
        shop->inventory_size++;
    } else {
        printf("Inventory is full!\n");
    }
}

// Sell a bike
void sellBike(BicycleShop *shop, const char *model) {
    for (int i = 0; i < shop->inventory_size; i++) {
        if (strcmp(shop->inventory[i].model, model) == 0) {
            shop->total_sales += shop->inventory[i].price;
            shop->sales_count++;

            // Remove bike from inventory
            for (int j = i; j < shop->inventory_size - 1; j++) {
                shop->inventory[j] = shop->inventory[j + 1];
            }
            shop->inventory_size--;
            printf("Sold %s\n", model);
            return;
        }
    }
    printf("Bike not found in inventory!\n");
}

// Print the inventory
void printInventory(BicycleShop *shop) {
    for (int i = 0; i < shop->inventory_size; i++) {
        printf("%s - Price: $%.2f, Gears: %d, Color: %s\n",
               shop->inventory[i].model,
               shop->inventory[i].price,
               shop->inventory[i].gears,
               shop->inventory[i].color);
    }
}

// Print the sales report
void printSalesReport(BicycleShop *shop) {
    printf("Total Sales: $%.2f\n", shop->total_sales);
    printf("Number of Bikes Sold: %d\n", shop->sales_count);
}
