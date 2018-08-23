#!/bin/bash

START_DBUS_HTTP=1
START_DBUS_HTTP_TEST=1
PORT=8080
DBUS_PATH="dbus/"
# DBUS_HTTP_ARGS="-v DEBUG"
# VALGRIND=valgrind

dbus_http_testd_pid=0
dbus_http_pid=0

exit_handler(){
    [ ${dbus_http_pid} -ne 0 ] && { echo "Stopping ${dbus_http_pid}"; kill ${dbus_http_pid} &> /dev/null; }
    [ ${dbus_http_testd_pid} -ne 0 ] && { echo "Stopping ${dbus_http_testd_pid}"; kill ${dbus_http_testd_pid} &> /dev/null; }
}
trap exit_handler EXIT


if [ $START_DBUS_HTTP_TEST -eq 1 ]; then
	./dbus-http-testd -s &
	dbus_http_testd_pid=$!
	echo "Started testd (${dbus_http_testd_pid})"
	sleep 1
fi


echo "Running Sanity checks for testd"
busctl --user introspect dbus.http.Calculator /dbus/http/Calculator > /dev/null \
  || { echo "testd introspection failed"; exit 1; }

busctl --user introspect dbus.http.Calculator /dbus/http/Calculator | grep -q Multiply \
  || { echo "testd Function Multiply not in introspection"; exit 1; }

busctl --user introspect dbus.http.Calculator /dbus/http/Calculator | grep -q Divide \
  || { echo "testd Function Multiply not in introspection"; exit 1; }

busctl --user call dbus.http.Calculator /dbus/http/Calculator org.freedesktop.DBus.Properties GetAll  "s" "" > /dev/null \
  || { echo "testd get Properties failed"; exit 1; }

busctl --user get-property dbus.http.Calculator /dbus/http/Calculator dbus.http.Calculator ZeroDivisionCounter | grep -q "u 0" \
  || { echo "testd zero division counter is not initialized 0 or get property failed."; exit 1; }

busctl --user call dbus.http.Calculator /dbus/http/Calculator dbus.http.Calculator Multiply "xx" 2 3 | grep -q "x 6" \
  || { echo "testd failed: Multiply 2*3=6"; exit 1; }

busctl --user call dbus.http.Calculator /dbus/http/Calculator dbus.http.Calculator Divide "xx" 4 2 | grep -q "x 2" \
  || { echo "testd failed: Division 4/2=2"; exit 1; }

busctl --user call dbus.http.Calculator /dbus/http/Calculator dbus.http.Calculator Divide "xx" 5 0 2>&1 | grep -q "Sorry" \
  || { echo "testd failed: Division by Zero"; exit 1; }

busctl --user get-property dbus.http.Calculator /dbus/http/Calculator dbus.http.Calculator ZeroDivisionCounter | grep -q "u 1" \
  || { echo "testd zero division counter is not 1 or get property failed."; exit 1; }

busctl --user call dbus.http.Calculator /dbus/http/Calculator dbus.http.Calculator GetArray | grep -q "ai 3 0 2147483647 -2147483648" \
  || { echo "testd method GetArray failed."; exit 1; }

busctl --user call dbus.http.Calculator /dbus/http/Calculator dbus.http.Calculator GetDict | grep -Fq 'a{sv} 2 "key1" i 17 "key2" s "test-string"' \
  || { echo "testd method GetDict failed."; exit 1; }

echo "Sanity checks for testd successfully passed"
echo ""


if [ $START_DBUS_HTTP -eq 1 ]; then
	${VALGRIND} ./dbus-http -s -p ${PORT} ${DBUS_HTTP_ARGS} &
	dbus_http_pid=$!
	echo "Started dbus-http (${dbus_http_pid})"
	sleep 1
fi

# Run tests
failed_tests=0

printf "\n--Get Properties GET\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator)
echo "$result"
[ "$result" == '{ "ZeroDivisionCounter": 1 }' ] || { ((failed_tests++)); echo "failed"; }

printf "\n\n--Get Properties POST\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"org.freedesktop.DBus.Properties", "method":"GetAll", "arguments":[""]}')
echo "$result"
[ "$result" == '{ "properties": { "ZeroDivisionCounter": 1 } }' ] || { ((failed_tests++)); echo "failed"; }

printf "\n\n--Multiply\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"Multiply", "arguments":[3,4]}')
echo "$result"
[ "$result" == "{ \"arg0\": 12 }" ] || { ((failed_tests++)); echo "failed"; }

printf "\n\n--Divide\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"Divide", "arguments":[12,3]}')
echo "$result"
[ "$result" == "{ \"arg0\": 4 }" ] ||  { ((failed_tests++)); echo "failed"; }

printf "\n\n--Divide by Zero\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"Divide", "arguments":[1,0]}')
echo "$result"
[ "$result" == "{ \"error\": \"dbus.http.DivisionByZero\", \"message\": \"Sorry, can't allow division by zero.\" }" ] ||  { ((failed_tests++)); echo "failed"; }

printf "\n\n--Get Properties GET\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator)
echo "$result"
[ "$result" == '{ "ZeroDivisionCounter": 2 }' ] || { ((failed_tests++)); echo "failed"; }

printf "\n\n--GetArray\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"GetArray", "arguments":[]}')
echo "$result"
[ "$result" == '{ "arg0": [ 0, 2147483647, -2147483648 ] }' ] ||  { ((failed_tests++)); echo "failed"; }

printf "\n\n--SetArray\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"SetArray", "arguments":[[7,8,9]]}')
echo "$result"

printf "\n\n--GetDict\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"GetDict", "arguments":[]}')
echo "$result"
[ "$result" == '{ "arg0": { "key1": 17, "key2": "test-string" } }' ] ||  { ((failed_tests++)); echo "failed"; }

printf "\n\n--SetDict\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"SetDict", "arguments":[{"key1": { "dbus_variant_sign": "i", "data":18 }, "key2": { "dbus_variant_sign": "s", "data":"test-string-new" } }]}')
echo "$result"

printf "\n\n--GetDict\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"GetDict", "arguments":[]}')
echo "$result"
[ "$result" == '{ "arg0": { "key1": 18, "key2": "test-string-new" } }' ] ||  { ((failed_tests++)); echo "failed"; }



printf "\n\n--GetStruct\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"GetStruct", "arguments":[]}')
echo "$result"
[ "$result" == '{ "arg0": [ 123, "foo bar" ] }' ] ||  { ((failed_tests++)); echo "failed"; }

printf "\n\n--SetStruct\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"SetStruct", "arguments":[ [124, "only foo"] ]}')
echo "$result"

printf "\n\n--GetStruct\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"GetStruct", "arguments":[]}')
echo "$result"
[ "$result" == '{ "arg0": [ 124, "only foo" ] }' ] ||  { ((failed_tests++)); echo "failed"; }



printf "\n\n--GetNested1\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"GetNested1", "arguments":[]}')
echo "$result"
[ "$result" == '{ "arg0": [ [ 1212, "bar1" ], [ 1313, "bar2" ] ], "arg1": 123, "arg2": [ 1, 2, 3 ] }' ] ||  { ((failed_tests++)); echo "failed"; }

printf "\n\n--SetNested1\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"SetNested1", "arguments":[ [ [ 1414, "bar2" ], [ 1515, "bar3" ] ], { "dbus_variant_sign": "u", "data":124 }, [ 1, 2, 4 ] ]}')
echo "$result"

printf "\n\n--GetNested1\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.Calculator/dbus/http/Calculator --data '{"interface":"dbus.http.Calculator", "method":"GetNested1", "arguments":[]}')
echo "$result"
[ "$result" == '{ "arg0": [ [ 1414, "bar2" ], [ 1515, "bar3" ] ], "arg1": 124, "arg2": [ 1, 2, 4 ] }' ] ||  { ((failed_tests++)); echo "failed"; }


printf "\nEnd of test suite. $failed_tests tests failed.\n"
