TS=$(date +%s)

SIGN=$(echo -n "GET
/api/v4/spot/currencies

$TS" \
| openssl dgst -sha512 \
-hmac "640251b512c50d68e168982119321fc3ac00e43ce5a89befd90ad3c1e68926a5" \
| awk '{print $2}')

curl -v "https://api-testnet.gateapi.io/api/v4/spot/currencies" \
  -H "KEY: f9c578b83395df8498d6906ef4eb814b" \
  -H "Timestamp: $TS" \
  -H "SIGN: $SIGN"

