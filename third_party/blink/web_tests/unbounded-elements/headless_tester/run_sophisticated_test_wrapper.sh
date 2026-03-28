#!/bin/bash
export DISPLAY=:92
Xvfb :92 -screen 0 1280x800x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2

fluxbox >/dev/null 2>&1 &
FLUX_PID=$!
sleep 2

export NODE_PATH=/usr/local/google/home/kerenzhu/code/chromium-unbounded-elements/src/third_party/devtools-frontend/src/node_modules
third_party/node/linux/node-linux-x64/bin/node run_sophisticated_test.cjs 

kill $FLUX_PID
kill $XVFB_PID
wait
