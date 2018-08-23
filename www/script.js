function multiply() {
	var num1 = document.getElementById("number1").value;
	var num2 = document.getElementById("number2").value;
	
	var url = "dbus/dbus.http.Calculator/dbus/http/Calculator";
	var json_body = {
		"interface": "dbus.http.Calculator",
		"method": "Multiply",
		"arguments": [parseInt(num1), parseInt(num2)]
	};
	
	var xmlhttp = new XMLHttpRequest();
	xmlhttp.onreadystatechange = function() {
		if (this.readyState == 4 && this.status == 200) {
			parse_result(JSON.parse(this.responseText));
		} else if(this.status == 401) {
			window.location.replace("/index.html");
		}
	};

	xmlhttp.open("POST", url);
	xmlhttp.setRequestHeader("Content-Type", "application/json");
	xmlhttp.send(JSON.stringify(json_body));
}

function parse_result(res) {
	document.getElementById("result").value = res["arg0"];
}
