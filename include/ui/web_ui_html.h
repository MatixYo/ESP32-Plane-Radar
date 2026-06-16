#include <Arduino.h>

const char kWebUiHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Plane Radar Dashboard</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" integrity="sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY=" crossorigin=""/>
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js" integrity="sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo=" crossorigin=""></script>
    <style>
        :root {
            --bg-dark: #0f172a;
            --panel-bg: rgba(30, 41, 59, 0.7);
            --border-color: rgba(255, 255, 255, 0.1);
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
            --accent: #38bdf8;
        }
        body {
            margin: 0;
            padding: 20px;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg-dark);
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .container {
            width: 100%;
            max-width: 800px;
        }
        .glass-panel {
            background: var(--panel-bg);
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
            border: 1px solid var(--border-color);
            border-radius: 16px;
            padding: 24px;
            margin-bottom: 24px;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06);
        }
        #map {
            height: 400px;
            width: 100%;
            border-radius: 8px;
            margin-bottom: 20px;
            background-color: rgba(15, 23, 42, 0.5);
            border: 1px solid var(--border-color);
            z-index: 1;
        }
        .leaflet-container {
            font-family: inherit;
        }
        .plane-icon {
            filter: drop-shadow(0 2px 4px rgba(0,0,0,0.5));
        }
        h1, h2 {
            margin-top: 0;
            font-weight: 600;
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
        }
        .settings-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
        }
        .form-group {
            display: flex;
            flex-direction: column;
            gap: 8px;
        }
        label {
            font-size: 0.9rem;
            color: var(--text-muted);
            font-weight: 500;
        }
        select, input[type="text"] {
            background-color: rgba(15, 23, 42, 0.8);
            color: var(--text-main);
            border: 1px solid var(--border-color);
            padding: 10px 12px;
            border-radius: 8px;
            font-size: 1rem;
            outline: none;
            transition: border-color 0.2s;
            appearance: none;
            width: 100%;
            box-sizing: border-box;
        }
        select:focus, input[type="text"]:focus {
            border-color: var(--accent);
        }
        .btn {
            background-color: var(--accent);
            color: #0f172a;
            border: none;
            padding: 10px 16px;
            border-radius: 8px;
            font-weight: 600;
            font-size: 0.95rem;
            cursor: pointer;
            transition: background-color 0.2s;
            height: 41px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .btn:hover {
            background-color: #7dd3fc;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 10px;
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid var(--border-color);
        }
        th {
            color: var(--text-muted);
            font-weight: 500;
            font-size: 0.9rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        tr:last-child td {
            border-bottom: none;
        }
        tr:hover td {
            background-color: rgba(255, 255, 255, 0.02);
        }
        .badge {
            background-color: rgba(56, 189, 248, 0.1);
            color: var(--accent);
            padding: 4px 8px;
            border-radius: 4px;
            font-size: 0.85rem;
            font-weight: 500;
        }
        .empty-state {
            text-align: center;
            padding: 40px 20px;
            color: var(--text-muted);
        }
        .pulse {
            display: inline-block;
            width: 8px;
            height: 8px;
            background-color: #10b981;
            border-radius: 50%;
            box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7);
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7); }
            70% { transform: scale(1); box-shadow: 0 0 0 10px rgba(16, 185, 129, 0); }
            100% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(16, 185, 129, 0); }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="glass-panel">
            <div class="header">
                <h2>Radar Settings</h2>
            </div>
            <div class="settings-grid">
                <div class="form-group">
                    <label for="rangeSelect">Radar Range</label>
                    <select id="rangeSelect" onchange="updateConfig()">
                        <option value="0">5 km (Local)</option>
                        <option value="1">10 km (Default)</option>
                        <option value="2">15 km (Wide)</option>
                        <option value="3">25 km (Regional)</option>
                    </select>
                </div>
                <div class="form-group">
                    <label for="airportModeSelect">Airport Data Mode</label>
                    <select id="airportModeSelect" onchange="updateConfig()">
                        <option value="0">None (Hide Route)</option>
                        <option value="1">IATA Codes (e.g. DEN-LGA)</option>
                        <option value="2">City Names (e.g. Denver - New York)</option>
                    </select>
                </div>
                <div class="form-group">
                    <label for="unitModeSelect">Unit System</label>
                    <select id="unitModeSelect" onchange="unitModeChanged()">
                        <option value="false">Metric (km/h, m)</option>
                        <option value="true">Imperial (kts, ft)</option>
                    </select>
                </div>
                <div class="form-group">
                    <label for="latInput">Home Latitude</label>
                    <input type="text" id="latInput" onchange="updateConfig()">
                </div>
                <div class="form-group">
                    <label for="lonInput">Home Longitude</label>
                    <input type="text" id="lonInput" onchange="updateConfig()">
                </div>
                <div class="form-group">
                    <label>&nbsp;</label>
                    <button class="btn" onclick="getLocation()">📍 Use Browser GPS</button>
                </div>
            </div>
        </div>

        <div class="glass-panel">
            <div class="header">
                <h2>Live Aircraft <span class="pulse" style="margin-left: 8px;"></span></h2>
            </div>
            <div id="map"></div>
            <div id="tableContainer">
                <div class="empty-state">Loading aircraft data...</div>
            </div>
        </div>
    </div>

    <script>
        let map = null;
        let planeMarkers = {};
        let centerLat = 0, centerLon = 0;
        let useMiles = false;

        function initMap(lat, lon) {
            if (map !== null) return;
            map = L.map('map').setView([lat, lon], 11);
            L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
                attribution: '&copy; OpenStreetMap & CARTO',
                subdomains: 'abcd',
                maxZoom: 19
            }).addTo(map);
            L.circleMarker([lat, lon], {
                radius: 6, fillColor: "#ff0000", color: "#fff", weight: 2, opacity: 1, fillOpacity: 0.8
            }).addTo(map).bindPopup("Home");
        }

        function getBearing(lat1, lon1, lat2, lon2) {
            const rad = Math.PI / 180;
            const dLon = (lon2 - lon1) * rad;
            const y = Math.sin(dLon) * Math.cos(lat2 * rad);
            const x = Math.cos(lat1 * rad) * Math.sin(lat2 * rad) - Math.sin(lat1 * rad) * Math.cos(lat2 * rad) * Math.cos(dLon);
            return (Math.atan2(y, x) / rad + 360) % 360;
        }

        function bearingToCardinal(deg) {
            const dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"];
            return dirs[Math.round(deg / 45) % 8];
        }

        function getPlaneIcon(track) {
            const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" style="transform: rotate(${track}deg)"><path d="M21 16v-2l-8-5V3.5c0-.83-.67-1.5-1.5-1.5S10 2.67 10 3.5V9l-8 5v2l8-2.5V19l-2 1.5V22l3.5-1 3.5 1v-1.5L13 19v-5.5l8 2.5z" fill="#38bdf8"/></svg>`;
            return L.divIcon({ html: svg, className: 'plane-icon', iconSize: [24, 24], iconAnchor: [12, 12] });
        }

        function getLocation() {
            if (navigator.geolocation) {
                navigator.geolocation.getCurrentPosition((pos) => {
                    document.getElementById('latInput').value = pos.coords.latitude.toFixed(6);
                    document.getElementById('lonInput').value = pos.coords.longitude.toFixed(6);
                    updateConfig();
                }, (err) => {
                    alert("Could not get location: " + err.message);
                });
            } else {
                alert("Geolocation is not supported by this browser.");
            }
        }

        async function fetchConfig() {
            try {
                const res = await fetch('/api/config');
                const data = await res.json();
                document.getElementById('rangeSelect').value = data.rangeIndex;
                document.getElementById('airportModeSelect').value = data.airportDataMode;
                document.getElementById('unitModeSelect').value = data.useMiles ? "true" : "false";
                document.getElementById('latInput').value = data.centerLat;
                document.getElementById('lonInput').value = data.centerLon;
                centerLat = data.centerLat;
                centerLon = data.centerLon;
                useMiles = data.useMiles;
                initMap(centerLat, centerLon);
                updateRangeLabels();
            } catch (e) {
                console.error("Failed to load config", e);
            }
        }

        function unitModeChanged() {
            useMiles = document.getElementById('unitModeSelect').value === "true";
            updateRangeLabels();
            updateConfig();
            fetchPlanes(); // Refresh table distances instantly
        }

        function updateRangeLabels() {
            const useMiles = document.getElementById('unitModeSelect').value === "true";
            const select = document.getElementById('rangeSelect');
            if (useMiles) {
                select.options[0].text = "3 mi (Local)";
                select.options[1].text = "6 mi (Default)";
                select.options[2].text = "9 mi (Wide)";
                select.options[3].text = "16 mi (Regional)";
            } else {
                select.options[0].text = "5 km (Local)";
                select.options[1].text = "10 km (Default)";
                select.options[2].text = "15 km (Wide)";
                select.options[3].text = "25 km (Regional)";
            }
        }

        async function updateConfig() {
            const rangeIndex = parseInt(document.getElementById('rangeSelect').value);
            const airportDataMode = parseInt(document.getElementById('airportModeSelect').value);
            const centerLat = parseFloat(document.getElementById('latInput').value) || 0;
            const centerLon = parseFloat(document.getElementById('lonInput').value) || 0;
            
            try {
                await fetch('/api/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ rangeIndex, airportDataMode, useMiles, centerLat, centerLon })
                });
            } catch (e) {
                console.error("Failed to update config", e);
            }
        }

        async function fetchPlanes() {
            try {
                const res = await fetch('/api/planes');
                const planes = await res.json();
                
                const container = document.getElementById('tableContainer');
                
                if (planes.length === 0) {
                    container.innerHTML = '<div class="empty-state">No aircraft currently in range</div>';
                    return;
                }

                let html = `
                    <table>
                        <thead>
                            <tr>
                                <th>Callsign</th>
                                <th>Type</th>
                                <th>Altitude</th>
                                <th>Speed</th>
                                <th>Distance</th>
                                <th>Route</th>
                            </tr>
                        </thead>
                        <tbody>
                `;

                let currentHexes = new Set();

                planes.forEach(p => {
                    const id = p.callsign || Math.random().toString();
                    currentHexes.add(id);

                    if (planeMarkers[id]) {
                        planeMarkers[id].setLatLng([p.lat, p.lon]);
                        planeMarkers[id].setIcon(getPlaneIcon(p.track));
                    } else if (map && p.lat && p.lon) {
                        planeMarkers[id] = L.marker([p.lat, p.lon], {icon: getPlaneIcon(p.track)}).addTo(map);
                    }
                    if (planeMarkers[id]) {
                        planeMarkers[id].bindPopup(`<b>${p.callsign || 'Unknown'}</b><br>${p.type || ''}<br>${p.alt || ''}<br>${p.speed || ''}`);
                    }

                    let distStr = "";
                    if (centerLat !== 0 && map && p.lat && p.lon) {
                        const distMeters = map.distance([centerLat, centerLon], [p.lat, p.lon]);
                        const bearing = getBearing(centerLat, centerLon, p.lat, p.lon);
                        const card = bearingToCardinal(bearing);
                        
                        if (useMiles) {
                            distStr = (distMeters / 1609.34).toFixed(1) + " mi " + card;
                        } else {
                            distStr = (distMeters / 1000).toFixed(1) + " km " + card;
                        }
                    }

                    html += `
                        <tr>
                            <td><strong>${p.callsign || 'N/A'}</strong></td>
                            <td><span class="badge">${p.type || '?'}</span></td>
                            <td>${p.alt || ''}</td>
                            <td>${p.speed || ''}</td>
                            <td>${distStr}</td>
                            <td>${p.route || ''}</td>
                        </tr>
                    `;
                });

                Object.keys(planeMarkers).forEach(id => {
                    if (!currentHexes.has(id)) {
                        map.removeLayer(planeMarkers[id]);
                        delete planeMarkers[id];
                    }
                });

                html += '</tbody></table>';
                container.innerHTML = html;
            } catch (e) {
                console.error("Failed to fetch planes", e);
            }
        }

        // Initialize
        fetchConfig();
        fetchPlanes();
        
        // Auto refresh planes
        setInterval(fetchPlanes, 2000);
    </script>
</body>
</html>
)rawliteral";
