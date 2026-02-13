#!/bin/bash

API_KEY="f9c578b83395df8498d6906ef4eb814b"
API_SECRET="640251b512c50d68e168982119321fc3ac00e43ce5a89befd90ad3c1e68926a5"
TS=$(date +%s)

# 1. Define the exact body
BODY='{"currency_pair":"BTC_USDT","type":"limit","side":"buy","price":"30000","amount":"0.0001"}'

# 2. HASH THE BODY (Crucial Step)
# This creates a hex-encoded SHA512 hash of the JSON string
BODY_HASH=$(echo -n "$BODY" | openssl dgst -sha512 | awk '{print $2}')

# 3. CONSTRUCT THE SIGNATURE STRING
# Format: METHOD + \n + URL + \n + QUERY_STRING + \n + BODY_HASH + \n + TIMESTAMP
# Note the empty line between URL and BODY_HASH is for the (empty) Query String
SIGN_STR=$(printf "POST\n/api/v4/spot/orders\n\n$BODY_HASH\n$TS")

# 4. SIGN THE STRING
SIGN=$(echo -n "$SIGN_STR" | openssl dgst -sha512 -hmac "$API_SECRET" | awk '{print $2}')

# 5. EXECUTE
curl -v "https://api-testnet.gateapi.io/api/v4/spot/orders" \
  -H "KEY: $API_KEY" \
  -H "Timestamp: $TS" \
  -H "SIGN: $SIGN" \
  -H "Content-Type: application/json" \
  --data-raw "$BODY"
