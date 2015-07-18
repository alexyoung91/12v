# 12v

12v client that runs on the Raspberry Pi.

## Dependencies

- [bcm2835](http://www.airspayce.com/mikem/bcm2835/)
- [libi2c-dev](https://packages.debian.org/jessie/libi2c-dev)
- libcurl
- [libncurses5-dev](https://packages.debian.org/jessie/libncurses5-dev)
- libpcre

bcm2835 has to be built and installed manually, libi2c-dev and libncurses5-dev can be installed with `sudo apt-get install libi2c-dev libncurses5-dev` on debian based distros.

## Building

```bash
make
```

## Running

The application needs to be run as root to access /dev/mem for bcm2835 functions

```bash
./main
```

## Notes

mcp3424 module is based on code found in [this](https://github.com/abelectronicsuk/ABElectronics_Python_Libraries/tree/master/ADCPi) repository. Two mcp3424 structures will be needed using ABElectronics' ADCPi.

## Todo

- When program exits, switch power to mains for safety
- Mutex on gusts_results when accessing from main thread
