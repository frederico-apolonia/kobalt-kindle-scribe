#!/bin/sh
# Name: Kobalt
# Author: Kobalt
# DontUseFBInk

# Library entry. Books are read from /mnt/us/books, a folder KOReader can open too. Copy to /mnt/us/documents next to the Kobalt folder (/mnt/us/kobalt).
# Detach at once so the Library's script runner is not left waiting on the reader.
KOBALT_LIBRARY=/mnt/us/books setsid sh /mnt/us/kobalt/kobalt.sh >/dev/null 2>&1 </dev/null &
