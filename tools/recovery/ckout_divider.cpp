// Clock-donor instrumentation (env:donor_ckout_slowclock -- see platformio.ini for the full
// story): puts ~1MHz on CKOUT/PB0 by dividing the donor's 16MHz system clock by 16, for
// injecting a recovery clock into a panel whose fuses expect an external clock it no longer
// gets. The CKOUT fuse must be programmed on the donor for PB0 to carry the clock at all.
#include <avr/io.h>

int main()
{
    CLKPR = (1 << CLKPCE);
    CLKPR = (1 << CLKPS2);

    while (1) {
    }
}
