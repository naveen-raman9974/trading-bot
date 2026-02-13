#!/bin/bash

API_KEY="f9c578b83395df8498d6906ef4eb814b"
API_SECRET="640251b512c50d68e168982119321fc3ac00e43ce5a89befd90ad3c1e68926a5"
ORDER_ID="307136082" # The ID from your previous response
TS=$(date +%s)

METHOD="DELETE"
# For DELETE, the currency_pair must be in the URL as a query string
ENDPOINT="/api/v4/spot/orders/$ORDER_ID"
QUERY_STRING="currency_pair=BTC_USDT"

# 1. Body is empty for DELETE, but we still need the hash of an empty string
BODY=""
BODY_HASH=$(echo -n "$BODY" | openssl dgst -sha512 | awk '{print $2}')

# 2. Construct the Signature String
# Format: METHOD + \n + ENDPOINT + \n + QUERY_STRING + \n + BODY_HASH + \n + TIMESTAMP
SIGN_STR=$(printf "$METHOD\n$ENDPOINT\n$QUERY_STRING\n$BODY_HASH\n$TS")

# 3. Sign the string
SIGN=$(echo -n "$SIGN_STR" | openssl dgst -sha512 -hmac "$API_SECRET" | awk '{print $2}')

# 4. Execute the request
# Note that the URL in the curl command must include the query string
curl -X DELETE "https://api-testnet.gateapi.io$ENDPOINT?$QUERY_STRING" \
  -H "KEY: $API_KEY" \
  -H "Timestamp: $TS" \
  -H "SIGN: $SIGN" \
  -H "Content-Type: application/json"
