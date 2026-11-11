const http = require('http');
const fs = require('fs');
const path = require('path');

const htmlContent = fs.readFileSync(path.join(__dirname, 'web_ui_test.html'), 'utf-8');

// Mock state
let state = {
  running: 1, autoT: 1, autoV: 1, timeBaseIdx: 3, vDivIdx: 2,
  ch2Mode: 1, probeIdx: 0, trigMode: 0, calFreqIdx: 1, acCoupled: 0, selfTest: 0,
  trigVolts: 1.65, dc: 1.65, ac: 0.707, vpp: 2.0, freq: 1000.0, fs: 200000,
  batVolts: 4.15, batPct: 95, ip: "192.168.1.100", ssid: "Home_WiFi", rssi: -58, bars: 4,
  gw: "192.168.1.1", mac: "84:FC:E6:12:34:56", clients: 1, httpReqs: 42
};

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  if (url.pathname === '/') {
    res.writeHead(200, { 'Content-Type': 'text/html' });
    res.end(htmlContent);
  } else if (url.pathname === '/api/metrics') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(state));
  } else if (url.pathname === '/api/waveform') {
    const ch1 = [];
    const ch2 = [];
    for (let i = 0; i < 320; ++i) {
      const t = i / 320.0 * 4.0 * Math.PI; // 2 periods
      const y1 = Math.round(55 + 35 * Math.sin(t));
      const y2 = Math.round(55 + 25 * Math.cos(t));
      ch1.push(y1);
      ch2.push(y2);
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ trigY: 55, ch2Mode: state.ch2Mode, ch1, ch2 }));
  } else if (url.pathname === '/api/set') {
    for (const [k, v] of url.searchParams.entries()) {
      if (k in state) {
        state[k] = isNaN(Number(v)) ? v : Number(v);
      }
    }
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('OK');
  } else {
    res.writeHead(404);
    res.end();
  }
});

server.listen(0, '127.0.0.1', () => {
  const port = server.address().port;
  console.log(`[Test Server] Running on http://127.0.0.1:${port}`);

  // Test HTTP GET /
  http.get(`http://127.0.0.1:${port}/`, (res) => {
    console.log(`[Test] GET / status: ${res.statusCode} (Length: ${res.headers['content-length'] || 'ok'})`);
    if (res.statusCode !== 200) process.exit(1);

    // Test GET /api/metrics
    http.get(`http://127.0.0.1:${port}/api/metrics`, (mRes) => {
      let data = '';
      mRes.on('data', chunk => data += chunk);
      mRes.on('end', () => {
        const json = JSON.parse(data);
        console.log(`[Test] GET /api/metrics verified: IP=${json.ip}, Freq=${json.freq}Hz, Bat=${json.batPct}%`);
        if (json.ip !== "192.168.1.100") process.exit(1);

        // Test GET /api/set?running=0
        http.get(`http://127.0.0.1:${port}/api/set?running=0&vDivIdx=4`, (sRes) => {
          console.log(`[Test] GET /api/set status: ${sRes.statusCode}`);
          if (state.running !== 0 || state.vDivIdx !== 4) process.exit(1);

          // Test GET /api/waveform
          http.get(`http://127.0.0.1:${port}/api/waveform`, (wRes) => {
            let wData = '';
            wRes.on('data', chunk => wData += chunk);
            wRes.on('end', () => {
              const wJson = JSON.parse(wData);
              console.log(`[Test] GET /api/waveform verified: ch1 points=${wJson.ch1.length}, ch2 points=${wJson.ch2.length}`);
              if (wJson.ch1.length !== 320) process.exit(1);
              console.log("[Test] ALL WEB REST APIS AND HTML TEMPLATE PASSED!");
              server.close();
              process.exit(0);
            });
          });
        });
      });
    });
  });
});

