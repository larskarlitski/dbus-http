#!/bin/bash

PORT=8080
DBUS_PATH="dbus/"

dbus_http_pid=0


RANDOM_FILE=$(mktemp test-upload.XXXXXXXXXX) || { echo "Failed to create temp file"; exit 1; }
head -c 1024 </dev/urandom >> $RANDOM_FILE

exit_handler(){
    [ ${dbus_http_pid} -ne 0 ] && { echo "Stopping ${dbus_http_pid}"; kill ${dbus_http_pid} &> /dev/null; }
    rm -f $RANDOM_FILE
    rm -f /tmp/$RANDOM_FILE
}
trap exit_handler EXIT



# Start dbus-http
# DBUS_HTTP_ARGS="-v DEBUG"
./dbus-http -s -p ${PORT} ${DBUS_HTTP_ARGS} &
dbus_http_pid=$!
echo "Started dbus-http (${dbus_http_pid})"
sleep 1

# Run tests
curl -F profile=@$RANDOM_FILE http://localhost:${PORT}/filepost

diff /tmp/$RANDOM_FILE $RANDOM_FILE
if [ $? -eq 0 ]; then
    echo "File uploaded and compared successfully"
else 
    echo "File upload failed"
fi