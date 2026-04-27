# ece453-student-cyble-416045-02-full-custom

## Getting started
This project demonstrates a fully custom hardware design based on the Infineon CYBLE-416045-02 PSoC6 module. Unlike typical examples that rely on the CY8CPROTO-BLE-063 development kit, this implementation is optimized for space-constrained applications where a full dev kit is impractical.

The design provides:
 * A minimal footprint for embedded BLE applications.
 * Full flexibility for custom PCB layouts.
 * A programming header for the KitProg4
 * A USB-to-UART Bridge

The design does NOT provide:
 * The 3.3V power rail needed for the CYBLE-416045-02 has not been specified.  You will need to determine how your design will supply 3.3V.

## Initialize your repository
```
git clone --recurse-submodules https://git.doit.wisc.edu/engr/ece/cmpe_courses/public/ece453/altium/ece453-student-cyble-416045-02-full-custom.git
```

## Authors and acknowledgment
This project was developed by Joe Krachey (jkrachey@wisc.edu) to support students at the University of Wisconsin–Madison in ECE453.
