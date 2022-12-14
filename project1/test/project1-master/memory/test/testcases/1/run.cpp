#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define PAGE_SIZE 1024 * 1024 * 40
#define TIME_SLEEP 1000

int main()
{
        void *ptr = NULL;
        void *nptr = NULL;
        size_t i = 1;
        while (1) {
                nptr = realloc(ptr, PAGE_SIZE * i);
                if (nptr) {
                        ptr = nptr;
                        ((char *)ptr)[PAGE_SIZE * (i-1)] = 'a';
                        i++;
                }
                else {
                        break;
                }
                usleep(TIME_SLEEP);
        }
        printf("REACH MAX\n");
        free(ptr);
        return 0;
}
