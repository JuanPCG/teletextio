#!/bin/bash
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig gcc capturar.c -o capturar $(pkg-config --cflags --libs zvbi-0.2)
