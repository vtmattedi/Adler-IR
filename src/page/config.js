document.addEventListener('DOMContentLoaded', function () {
    fetchConfigs();
    fetchConfigUpdates();
    setInterval(fetchConfigUpdates, 1000);
});

function fetchConfigUpdates() {
    fetch('/info?config=true;').then(response => response.text())
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
    else if (name === "ac") {
        const thisDiv = document.getElementById('ac-statusDiv');

        if (value.indexOf('=') < 1)
            thisDiv.textContent = "Status:" + value;
        else {
            var variables = value.split('+');
            var power = true;
            var temp = 0;
            var changing = 0;
            for (let index = 0; index < variables.length; index++) {
                if (index === 0)
                    temp = parseInt(variables[index].split('=')[1]);
                else if (index === 1)
                    power = variables[index].split('=')[1] === "1";
                else if (index === 2)
                    changing = parseInt(variables[index].split('=')[1]);
            }
            var text = `ac is ${power ? "on" : "off"} || ${temp}\u00B0C`
            if (changing > 0)
                text += ` (${power ? "sleeping" : "waking"} in ${formatTime(Date.now() - changing * 1000)})`;
            thisDiv.textContent = text;
        }
    }
}
function formatTime(seconds) {
    if (seconds < 90) {
        return `${seconds} s`;
    } else if (seconds < 90 * 60) {
        const minutes = Math.floor(seconds / 60);
        return `${minutes} m`;
    } else {
        const hours = Math.floor(seconds / 3600);
        const remainingMinutes = Math.floor((seconds % 3600) / 60);

        if (remainingMinutes === 0) {
            return `${hours} h`;
        } else {
            const formattedMinutes = remainingMinutes < 10 ? `0${remainingMinutes}` : remainingMinutes;
            return `${hours} h ${formattedMinutes} m`;
        }
    }
}

function test() {
    parseSensors(b);
    setConfigs(d);
}

function fetchConfigs() {
    fetch('/settings?sensorsList=1')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();
        })
        .then(data => {
            parseSensors(data);
        })
        .catch(error => {
            console.error(`Fetch error: ${error} on /settings?sensorsList=1`);
        });

    fetch('/settings?current-config=1')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();
        })
        .then(data => {
            setConfigs(data);
        })
        .catch(error => {
            console.error(`Fetch error: ${error} on /settings?current-config=1`);
        });
}


function getConfig(data) {
    var config = [];
    var pos = 0;
    while (pos < data.length) {
        var colonIndex = data.indexOf(':', pos);
        if (colonIndex == -1)
            break;

        var semicolonIndex = data.indexOf(';', colonIndex);
        if (semicolonIndex == -1)
            semicolonIndex = data.length();
        var thisSensor = {
            'key': data.substring(pos, colonIndex),
            'value': data.substring(colonIndex + 1, semicolonIndex)
        };
        config.push(thisSensor);
        pos = semicolonIndex + 1;
    }
    return config;
}

function setConfigs(data) {
    getConfig(data).forEach(pair => {
        if (pair.key === "MainSensor") {
            var found = false;
            var selectElement = document.getElementById("sensorTopic");
            for (var i = 0; i < selectElement.options.length; i++) {
                if (selectElement.options[i].value === pair.value) {
                    selectElement.options[i].selected = true;
                    found = true;
                    i = selectElement.options.length;
                }
            }
            if (!found) {
                const topicDiv = document.getElementById("sensorTopic");
                var opt = document.createElement('option');
                opt.value = pair.value;
                opt.innerHTML = setName(pair.value);
                opt.selected = true;
                topicDiv.appendChild(opt);
            }
        }
        else if (pair.key === "ShutdownTime") {
            document.getElementById("timeInput").value = pair.value;
        }
        else if (pair.key === "TemperatureThreshold") {
            document.getElementById("temperatureInput").value = pair.value;
        }
        else if (pair.key === "AcShutdown") {
            var value = parseInt(pair.value) == 1;
            document.getElementById("AcShutdown").checked = value;
            if (!value)
                setDisabled("timeInput", false);
        }
        else if (pair.key === "UseTemperatureThrashold") {
            var value = parseInt(pair.value) == 1;
            document.getElementById("temperatureThreshold").checked = value;
            if (!value)
                setDisabled("temperatureInput", false);
        }
        else if (pair.key === "DefaultBacktoBoard") {
            var value = parseInt(pair.value) == 1;
            document.getElementById("defaultBack").checked = value;
        }
        else if (pair.key === "DoubleEnforcement") {
            var value = parseInt(pair.value) == 1;
            document.getElementById("doubleEnforcement").checked = value;
        }
        else if (pair.key === "LedFeedback") {
            var value = parseInt(pair.value) == 1;
            document.getElementById("ledFeedback").checked = value;
        }

    });
}
function formatTime(seconds) {
    if (seconds < 90) {
        return `${seconds} s`;
    } else if (seconds < 90 * 60) {
        const minutes = Math.floor(seconds / 60);
        return `${minutes} m`;
    } else {
        const hours = Math.floor(seconds / 3600);
        const remainingMinutes = Math.floor((seconds % 3600) / 60);

        if (remainingMinutes === 0) {
            return `${hours} h`;
        } else {
            const formattedMinutes = remainingMinutes < 10 ? `0${remainingMinutes}` : remainingMinutes;
            return `${hours} h ${formattedMinutes} m`;
        }
    }
}

// function fetchInfoUpdates() {
//     fetch('/info').then(response => response.text())
//         .then(data => {
//             parseInfoData(data);
//         })
// }
// function parseInfoData(responseText) {
//     const infoPairs = responseText.split(';');
//     infoPairs.forEach(pair => {
//         const [name, value] = pair.split(':');
//         updateInfoData(name, value);
//     });
// }
// function updateInfoData(name, value) {
//     if (typeof (name) === "undefined")
//         return;
//     if (name === "status") {
//         const thisDiv = document.getElementById('current-statusDiv');
//         thisDiv.textContent = value;
//         thisDiv.className = value;

//     }
//     else if (name === "ac") {
//         const thisDiv = document.getElementById('ac-statusDiv');
//         const powerbutton = document.getElementById('power-button');
//         if (value === "0") {
//             thisDiv.textContent = "Unknown";
//             thisDiv.className = "";
//             powerbutton.textContent = "Power";
//         }
//         else if (value === "1") {
//             thisDiv.textContent = "ON";
//             thisDiv.className = "Cooling";
//             powerbutton.textContent = "Turn Off";
//         }
//         else if (value === "2") {
//             thisDiv.textContent = "OFF";
//             powerbutton.textContent = "Turn On";
//             thisDiv.className = "Warming";
//         }
//     }
// }
// function formatTime(seconds) {
//     if (seconds < 90) {
//         return `${seconds} s`;
//     } else if (seconds < 90 * 60) {
//         const minutes = Math.floor(seconds / 60);
//         return `${minutes} m`;
//     } else {
//         const hours = Math.floor(seconds / 3600);
//         const remainingMinutes = Math.floor((seconds % 3600) / 60);

//         if (remainingMinutes === 0) {
//             return `${hours} h`;
//         } else {
//             const formattedMinutes = remainingMinutes < 10 ? `0${remainingMinutes}` : remainingMinutes;
//             return `${hours} h ${formattedMinutes} m`;
//         }
//     }
// }

function setName(path) {
    if (path.indexOf("/") < 0)
        return path;
    return `${path.substring(0, path.indexOf("/"))} (${path.substring(path.lastIndexOf("/") + 1)})`;
}


function setSensors(sensorlist) {

    const topicDiv = document.getElementById("sensorTopic");
    topicDiv.innerHTML = "";
    sensorlist.forEach(sensor => {
        if (sensor.path != "") {
            var opt = document.createElement('option');
            opt.value = sensor.path;
            opt.innerHTML = sensor.name;
            topicDiv.appendChild(opt);
        }
    });


}

function parseSensors(sensorlist) {
    const sensors = sensorlist.split('+');
    var options = [];
    sensors.forEach(sensor => {
        var thisSensor = {
            'name': setName(sensor),
            'path': sensor
        };
        options.push(thisSensor);
        // console.log(thisSensor)
    });
    setSensors(options);
}

function updateTemperatureDisplay(temperature) {
    const temperatureDiv = document.getElementById('temperatureDiv');
    temperatureDiv.textContent = `${temperature} \u00B0C`;

}

function setDisabled(id, value) {
    if (typeof (id) === "undefined" || typeof (value) === "undefined")
        return;
    //console.log(id, value);
    if (!value)
        document.getElementById(id).setAttribute("disabled", "");
    else
        document.getElementById(id).removeAttribute("disabled");

}

function factoryReset() {
    fetch('/settings?factory-reset=1')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();

        })
        .then(data => {
            alert(data);
            fetchConfigs();

        })
        .catch(error => {
            console.error(`Fetch error: ${error} on /settings?current-config=1`);
        });
}
function espRestart() {
    fetch('/settings?restart=1')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();

        })
        .then(data => {
            alert(data);
            window.location.href = '/';
        })
        .catch(error => {
            console.error(`Fetch error: ${error} on /settings?current-config=1`);
        });
}

function saveConfigs() {
    var result = "";
    result += "MainSensor=" + document.getElementById("sensorTopic").value + "&";
    result += "LedFeedback=" + (document.getElementById("ledFeedback").checked ? "1" : "0") + "&";
    result += "TemperatureThreshold=" + document.getElementById("temperatureInput").value + "&";
    result += "ShutdownTime=" + document.getElementById("timeInput").value + "&";
    // fetch('/settings?new-config=1&' + result)
    // .then(response => {
    //     if (!response.ok) {
    //         throw new Error('Network response was not ok');
    //     }
    //     return response.text();
    // })
    // .then(data => {
    //     alert(data);
    // })
    // .catch(error => {
    //     console.error(`Fetch error: ${error} on /settings?new-config`);
    // });
    // result = "";
    result += "AcShutdown=" + (document.getElementById("AcShutdown").checked ? "1" : "0") + "&";
    result += "UseTemperatureThrashold=" + (document.getElementById("temperatureThreshold").checked ? "1" : "0") + "&";
    result += "DefaultBacktoBoard=" + (document.getElementById("defaultBack").checked ? "1" : "0") + "&";
    result += "DoubleEnforcement=" + (document.getElementById("doubleEnforcement").checked ? "1" : "0");

    fetch('/settings?new-config=1&' + result)
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();
        })
        .then(data => {
            alert(data);
            window.location.href = "/";
        })
        .catch(error => {
            console.error(`Fetch error: ${error} on /settings?new-config`);
        });
}

function ManualSync() {
    var result = document.getElementById("ManualTempState").value;
    if (!document.getElementById("ManualACState").checked)
        result = "-" + result;
    fetch('/general?manual-sync=' + result)
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();
        })
        .then(data => {
            alert(data);
            window.location.href = "/";
        })
        .catch(error => {
            console.error(`Fetch error: ${error} on /general?manual-sync`);
        });
}

