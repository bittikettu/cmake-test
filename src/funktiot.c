#include <stdio.h>
#include "version.h"

// Function to add two numbers
int add(int a, int b) { return a + b; }

// Function to subtract two numbers
int subtract(int a, int b) { return a - b; }

// Function to multiply two numbers
int multiply(int a, int b) { return a * b; }

// Function to divide two numbers
float divide(float a, float b) {
  if (b == 0) {
    return 0;
  }
  return a / b;
}