#include <avr/io.h>

int main()
{
    CLKPR = (1 << CLKPCE);
    CLKPR = (1 << CLKPS2);

    while (1) {
    }
}
