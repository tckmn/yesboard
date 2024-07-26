#!/usr/bin/env bash
exec ./yesboard "$(xinput list --id-only keyboard:"$(grep -o '[^"]*Keyboard[^"]*' /proc/bus/input/devices)")" $1
