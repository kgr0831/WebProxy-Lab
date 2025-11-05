#include <stdio.h>

/// @brief (a + b) ^ 2 을 반환하는 코드
/// @param a 미지수 a
/// @param b 미지수 b
/// @return (a + b) ^ 2
int calculate_score(int a, int b) 
{
    return a * b + (a + b) * 2;
}

int main(void) 
{
    int result = calculate_score(3, 5);
    printf("Result: %d\n", result);
    return 0;
}