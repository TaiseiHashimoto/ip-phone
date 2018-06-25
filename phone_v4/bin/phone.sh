#!/bin/sh

bin/phone_v4 $1 | play --buffer 256 -t raw -b 16 -c 1 -e s -r 8000 -q -
