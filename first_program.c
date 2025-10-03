#include <stdio.h>

int main() {
    // Variables declaration
    char name[50];
    int age;
    
    // Print welcome message
    printf("Hello, World!\n");
    
    // Get user input
    printf("Please enter your name: ");
    scanf("%s", name);
    
    printf("Please enter your age: ");
    scanf("%d", &age);
    
    // Output personalized message
    printf("\nWelcome, %s!\n", name);
    printf("You are %d years old.\n", age);
    
    return 0;
}