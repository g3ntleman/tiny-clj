#include <stdio.h>
int main() { 
#ifdef DEBUG
    printf("DEBUG=1\n");
#else
    printf("DEBUG=0\n");
#endif
    return 0; 
}