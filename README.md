# Configuration
config.json format:
{
"symbol": "BTC_USDT",
"quantity": 0.0001,
"order_mode": "WS",
"api_key": "your_api_key",
"api_secret": "your_api_secret",
"settle": "usdt"
}

# Building and running the application(present in startup.sh)

- Also load environment variables from .env file

#!/bin/bash

if [ -f .env ]; then
    set -a
    source .env
    set +a
fi

cd build
rm -rf *
cmake ..
make
./bot ../config.json
