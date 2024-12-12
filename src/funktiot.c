#include <stdio.h>
// #include "version.h"

// Function to add two numbers
int add(int a_number, int b_number) { return a_number + b_number; }

// Function to subtract two numbers
int subtract(int a_number, int b_number) { return a_number - b_number; }

// Function to multiply two numbers
int multiply(int a_number, int b_number) { return a_number * b_number; }

// Function to divide two numbers
float divide(float a_number, float b_number) {
  if (b_number == 0) {
    return 0;
  }
  return a_number / b_number;
}