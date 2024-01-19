
function setName(path) {
    if (path.indexOf("/") < 0)
        return path;
    return `${path.substring(0, path.indexOf("/"))} (${path.substring(path.lastIndexOf("/") + 1)})`;
}

document.addEventListener('DOMContentLoaded', function () {
    fetchInfoUpdates();
    setDisabled(false);
    setInterval(fetchInfoUpdates, 1000); // Update the codes every 5 seconds (adjust as needed)
    // const irInput = document.getElementById("ir-code");
    // irInput.addEventListener("keydown", function (event) {
    //     if (event.key === "Enter") {
    //         sendIRCode();
    //     }
    // });
});

function updateTemperatureDisplay(temperature) {
    const temperatureDiv = document.getElementById('temperatureDiv');
    temperatureDiv.textContent = `${temperature} \u00B0C`;//+'&deg; C';
}

function sendIRCode(value) {
    // const irInput = document.getElementById("ir-code");
    // if (typeof (value) !== "undefined") {
    //     irInput.value = value;
    // }
    fetch('/send-ir?code=' + value)
        .then(response => response.text())
        .then(data => {
            console.log(data);
            // Optionally, update UI with response data
        })
        .catch(error => {
            console.error('Error:', error);
            // Handle error if needed
        });
}
function getDefineName(value) {
    switch (value) {
        case -4:
            return "MQTT_CONNECTION_TIMEOUT";
        case -3:
            return "MQTT_CONNECTION_LOST";
        case -2:
            return "MQTT_CONNECT_FAILED";
        case -1:
            return "MQTT_DISCONNECTED";
        case 0:
            return "MQTT_CONNECTED";
        case 1:
            return "MQTT_CONNECT_BAD_PROTOCOL";
        case 2:
            return "MQTT_CONNECT_BAD_CLIENT_ID";
        case 3:
            return "MQTT_CONNECT_UNAVAILABLE";
        case 4:
            return "MQTT_CONNECT_BAD_CREDENTIALS";
        case 5:
            return "MQTT_CONNECT_UNAUTHORIZED";
        default:
            return "Unknown";
    }
}

function sendMqttReconnect() {
    fetch('/general?mqtt-force-recon=1').then(response => response.text())
        .then(data => {
            alert(data);
        })
}
const Skynet = {
    "Control": "Unknown",
    "Ac": "Unknown",
    "Temp": "Unknown",
    "Ssleep": "Unknown",
    "Hsleep": "Unknown",
    "Settemp": "Unknown",

    ParseNewData(data) {
        var pair = data.split("+");
        pair.forEach(value => {
            var KeyPair = value.split("=");
            if (KeyPair.length > 0)
                this[KeyPair[0]] = KeyPair[1];
        });

    },

    SetNewData() {

        var takeover = parseInt(this["Control"]);
        const setDiv = document.getElementById('set-statusDiv');
        const acDiv = document.getElementById('ac-statusDiv');



        //Control not finished
        if (takeover > 0) {
            acDiv.textContent = "Skynet is taking control in: " + formatTime(takeover) + ".";
            setDiv.textContent = "---";
            setDisabled(false);
            return;
        }
        const setTemp = parseInt(this.Settemp);
        if (setTemp > 0)
            setDiv.textContent = `Maintaining at ${setTemp}\xB0C.`;
        else
            setDiv.textContent = "Temperature Controller is off.";

        var text = `ac is ${this.Ac} || ${this.Temp}\u00B0C`
        const SWsleep = parseInt(this.Ssleep);
        const HWsleep = parseInt(this.Hsleep);
        if (SWsleep >= 0 || HWsleep >= 0) {
            if (HWsleep > SWsleep)
                text += ` (${this.Ac === "on" ? "sleeping" : "waking"} in ${formatTime(Date.now() - HWsleep * 1000)})`;
            else
                text += ` (sleeping in ${formatTime(Date.now() - SWsleep * 1000)})`;

        }
        acDiv.textContent = text;
        setDisabled(true);
    }

}


function fetchInfoUpdates() {
    fetch('/info').then(response => response.text())
        .then(data => {
            parseInfoData(data);
        })
}
function parseInfoData(responseText) {
    const infoPairs = responseText.split(';');
    infoPairs.forEach(pair => {
        const [name, value] = pair.split(':');
        updateInfoData(name, value);
    });
}
function updateInfoData(name, value) {
    if (typeof (name) === undefined)
        return;
    if (name === "temp") {
        const temperatureDiv = document.getElementById('temperatureDiv');
        temperatureDiv.textContent = `${value} \u00B0C`;
    }
    else if (name === "mqtt") {
        var intvalue = parseInt(value);
        const mqttDiv = document.getElementById('mqttDiv');
        mqttDiv.textContent = getDefineName(intvalue);
        if (intvalue === 0) {
            document.getElementById("mqqt-reconnect-button").style.display = "none";
            mqttDiv.classList.add("mqtt-good");
            mqttDiv.classList.remove("mqtt-bad");
        }
        else {
            document.getElementById("mqqt-reconnect-button").style.display = "block";
            mqttDiv.classList.remove("mqtt-good");
            mqttDiv.classList.add("mqtt-bad");
        }
    }
    else if (name === "time_synced") {
        var intvalue = parseInt(value);
        const thisDiv = document.getElementById('synced-timeDiv');
        thisDiv.textContent = intvalue === 0 ? "Nope" : "Yup";
        if (intvalue === 1) {
            thisDiv.classList.add("mqtt-good");
            thisDiv.classList.remove("mqtt-bad");
        }
        else {
            thisDiv.classList.remove("mqtt-good");
            thisDiv.classList.add("mqtt-bad");
        }
    }
    else if (name === "life") {
        var intvalue = parseInt(value);
        const thisDiv = document.getElementById('life-timeDiv');
        thisDiv.textContent = formatTime(intvalue);

    }
    else if (name === "boot_time") {
        var date = new Date((parseInt(value) + 3 * 3600) * 1000);
        const thisDiv = document.getElementById('boot-timeDiv');
        const options = { year: '2-digit', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' };
        thisDiv.textContent = date.toLocaleString('pt-US', options);

    }
    else if (name === "mainSensor") {
        const thisDiv = document.getElementById('sensor-topicDiv');
        thisDiv.textContent = "Sensor: " + setName(value);
    }

    else if (name === "Skynet") {
        Skynet.ParseNewData(value);
        Skynet.SetNewData();
    }

}
function formatTime(seconds) {
    if (seconds < 90) {
        return `${seconds} s`;
    } else if (seconds < 90 * 60) {
        const minutes = Math.floor(seconds / 60);
        return `${minutes} m`;
    } else if (seconds < 26 * 60 * 60) {
        const hours = Math.floor(seconds / 3600);
        const remainingMinutes = Math.floor((seconds % 3600) / 60);

        if (remainingMinutes === 0) {
            return `${hours} h`;
        } else {
            const formattedMinutes = remainingMinutes < 10 ? `0${remainingMinutes}` : remainingMinutes;
            return `${hours} h ${formattedMinutes} m`;
        }
    }
    else {
        const days = Math.floor(seconds / (3600 * 24));
        const remainingHours = Math.floor((seconds % (3600 * 24)) / 60);

        if (remainingHours === 0) {
            return `${days} d`;
        } else {
            const formattedHours = remainingHours < 10 ? `0${remainingHours}` : remainingHours;
            return `${days} d ${formattedHours} h`;
        }
    }
}

function updateTemperatureDislay(temperature) {
    const temperatureDiv = document.getElementById('temperatureDiv');
    temperatureDiv.textContent = `${temperature} \u00B0C`;//+'&deg; C';
}

function setTemperature(temperature) {
    var _temp = 0;
    if (typeof (temperature) !== "undefined")
        _temp = temperature;
    else
        _temp = document.getElementById("set-tempInput").value;
    fetch("/general?set-temperature=" + _temp)
        .then(response => response.text())
        .then(data => {
            console.log(data);
            // Optionally, update UI with response data
        })
        .catch(error => {
            console.error('Error:', error);
            // Handle error if needed
        });
}

function updatesetTemp(increase) {
    if (document.getElementById('set-tempInput').hasAttributes("disabled"))
        return;
    if (increase === true && document.getElementById('set-tempInput').value < 30) {
        document.getElementById('set-tempInput').value++;
    }
    else if (increase === false && document.getElementById('set-tempInput').value > 18) {
        document.getElementById('set-tempInput').value--;
    }
}


function updateShutdown(increase) {
    if (document.getElementById('set-shutdownInput').hasAttributes("disabled"))
        return;
    if (increase === true && document.getElementById('set-shutdownInput').value < 12) {
        document.getElementById('set-shutdownInput').value++;
    }
    else if (increase === false && document.getElementById('set-shutdownInput').value > 1) {
        document.getElementById('set-shutdownInput').value--;
    }
}

function AcShutdown() {
    var request = String(document.getElementById('set-shutdownInput').value);
    if (!document.getElementById("useHWInput").checked)
        request += "&software=1";
    fetch(`/general?acshutdown=${request}`)
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();
        })
        .then(data => {
            console.log(data)
        })
        .catch(error => {
            console.error(`Fetch error: ${error} on /general?acshutdown=`);
        });
}

function setDisabled(value) {
    if (!value) {
        document.getElementById("setButton").setAttribute("disabled", "");
        document.getElementById("offButton").setAttribute("disabled", "");
        document.getElementById("goButton").setAttribute("disabled", "");
        document.getElementById("set-tempInput").setAttribute("disabled", "");
        document.getElementById("set-shutdownInput").setAttribute("disabled", "");
        document.getElementById("useHWInput").setAttribute("disabled", "");

    }
    else {
        document.getElementById("setButton").removeAttribute("disabled");
        document.getElementById("offButton").removeAttribute("disabled");
        document.getElementById("goButton").removeAttribute("disabled");
        document.getElementById("set-tempInput").removeAttribute("disabled");
        document.getElementById("set-shutdownInput").removeAttribute("disabled");
        document.getElementById("useHWInput").removeAttribute("disabled");
    }
}

