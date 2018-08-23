#!/bin/bash +x

START_DBUS_HTTP=1
START_DBUSSY_TEST=1
PORT=8080
DBUS_PATH="dbus/"
# DBUS_HTTP_ARGS="-v DEBUG"
# VALGRIND=valgrind

dbussy_test_pid=0
dbus_http_pid=0

exit_handler(){
    [ ${dbus_http_pid} -ne 0 ] && { echo "Stopping ${dbus_http_pid}"; kill ${dbus_http_pid} &> /dev/null; }
    [ ${dbussy_test_pid} -ne 0 ] && { echo "Stopping ${dbussy_test_pid}"; kill ${dbussy_test_pid} &> /dev/null; }
}
trap exit_handler EXIT


if [ $START_DBUSSY_TEST -eq 1 ]; then
	export PYTHONPATH=../test/dbussy
	../test/dbussy_test_server &
	dbussy_test_pid=$!
	echo "Started dbussy test server (${dbussy_test_pid})"
	sleep 1
fi


echo "Sanity checks for dbussy test server"
busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest get_string \
| grep 's "initial value with \\303\\244\\303\\266\\303\\274"' || { echo "test failed: get_string"; exit 1; }

busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest set_string s test1

busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest get_string \
| grep 's "test1"' || { echo "test failed: set_string"; exit 1; }


busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest get_array_struct_ss \
| grep 'a(ss) 2 "foo1" "bar1" "foo2" "bar2"' || { echo "test failed: get_array_struct_ss"; exit 1; }

busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest set_array_struct_ss "a(ss)" 3 bla bla ble ble blu blu

busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest get_array_struct_ss \
| grep 'a(ss) 3 "bla" "bla" "ble" "ble" "blu" "blu"' || { echo "test failed: set_array_struct_ss"; exit 1; }


busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest get_array_struct_svs \
| grep 'a(svs) 2 "foo1" u 7 "bar1" "foo2" s "another string" "bar2"' || { echo "test failed: get_array_struct_svs"; exit 1; }

busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest set_array_struct_svs "a(svs)" 3 "double" d 123.345 "updated bar1" "foo3" s "a new string" "bar3" "an additional array" as 2 "str1" "str2" "and the last of svs"

busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest get_array_struct_svs \
| grep 'a(svs) 3 "double" d 123.345 "updated bar1" "foo3" s "a new string" "bar3" "an additional array" as 2 "str1" "str2" "and the last of svs"' || { echo "test failed: set_array_struct_svs"; exit 1; }

busctl --user call dbus.http.dbussyTest /TestIf1 dbus.http.dbussyTest set_array_struct_svs "a(svs)" 1 "unsigned" u 123 "updated bar1"

echo "Sanity checks for dbussy_test_server successfully passed"
echo ""


if [ $START_DBUS_HTTP -eq 1 ]; then
	${VALGRIND} ./dbus-http -s -p ${PORT} ${DBUS_HTTP_ARGS} &
	dbus_http_pid=$!
	echo "Started dbus-http (${dbus_http_pid})"
	sleep 1
fi

# Run tests
failed_tests=0

printf "\n\n--get_string\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"get_string", "arguments":[]}')
echo "$result"
[ "$result" == "{ \"arg0\": \"test1\" }" ] || { ((failed_tests++)); echo "failed"; }

printf "\n\n--set_string\n"
curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"set_string", "arguments":["a new string with äöü"]}'

printf "\n\n--get_string\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"get_string", "arguments":[]}')
echo "$result"
[ "$result" == "{ \"arg0\": \"a new string with äöü\" }" ] || { ((failed_tests++)); echo "failed"; }


printf "\n\n--get_array_struct_ss\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"get_array_struct_ss", "arguments":[]}')
echo "$result"
reference="{ \"arg0\": [ [ \"bla\", \"bla\" ], [ \"ble\", \"ble\" ], [ \"blu\", \"blu\" ] ] }"
echo $reference
[ "$result" == "$reference" ] || { ((failed_tests++)); echo "failed"; }

printf "\n\n--set_array_struct_ss\n"
curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"set_array_struct_ss", "arguments":[[["foo", "bar"]]]}'

printf "\n\n--get_array_struct_ss\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"get_array_struct_ss", "arguments":[]}')
echo "$result"
reference="{ \"arg0\": [ [ \"foo\", \"bar\" ] ] }"
echo $reference
[ "$result" == "$reference" ] || { ((failed_tests++)); echo "failed"; }


printf "\n\n--get_array_struct_svs\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"get_array_struct_svs", "arguments":[]}')
echo "$result"
reference="{ \"arg0\": [ [ \"unsigned\", 123, \"updated bar1\" ] ] }"
echo $reference
[ "$result" == "$reference" ] || { ((failed_tests++)); echo "failed"; }

printf "\n\n--set_array_struct_svs\n"
curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"set_array_struct_svs", "arguments":[[["foo", { "dbus_variant_sign": "i", "data":-124 }, "bar"]]]}'

printf "\n\n--get_array_struct_svs\n"
result=$(curl -s http://localhost:${PORT}/${DBUS_PATH}dbus.http.dbussyTest/TestIf1 --data '{"interface":"dbus.http.dbussyTest", "method":"get_array_struct_svs", "arguments":[]}')
echo "$result"
reference="{ \"arg0\": [ [ \"foo\", -124, \"bar\" ] ] }"
echo $reference
[ "$result" == "$reference" ] || { ((failed_tests++)); echo "failed"; }


printf "\nEnd of test suite. $failed_tests tests failed.\n"