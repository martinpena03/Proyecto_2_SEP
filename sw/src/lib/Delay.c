#include "sleep.h"
#include "Delay.h"

int xusleep(unsigned int useconds)
{
    usleep(useconds);
    return 0;
}

void millisleep(unsigned int milliseconds)
{
    unsigned int i;
    for (i = 0; i < milliseconds; i++) usleep(1000);
}

void del_sleep(unsigned int seconds)
{
    unsigned int i;
    for (i = 0; i < seconds; i++) usleep(1000000);
}

void delay_ms(unsigned int milliseconds)
{
    unsigned int i;
    for (i = 0; i < milliseconds; i++) usleep(1000);
}
