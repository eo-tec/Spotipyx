import puppeteer from 'puppeteer';

const browser = await puppeteer.launch({ headless: true });
const page = await browser.newPage();
await page.setViewport({ width: 1400, height: 900 });

await page.goto('http://localhost:5176/', { waitUntil: 'networkidle2' });

// Set correct localStorage values
await page.evaluate(() => {
  localStorage.setItem('sim_brokerUrl', 'ws://localhost:9001');
  localStorage.setItem('sim_pixieId', '1');
});

// Reload
await page.reload({ waitUntil: 'networkidle2' });
await page.waitForSelector('button');

// Click Connect button
const connectBtn = await page.waitForSelector('button:not([disabled])');
await connectBtn.click();

console.log('Clicked Connect, waiting...');

// Wait for connected status
try {
  await page.waitForFunction(() => {
    const s = document.querySelector('.status.online');
    return s !== null;
  }, { timeout: 15000 });
  console.log('Connected!');
} catch {
  console.log('Connection timeout, taking screenshot anyway');
}

// Wait for photo to load
await new Promise((r) => setTimeout(r, 12000));

// Get logs
const logs = await page.evaluate(() => {
  return Array.from(document.querySelectorAll('.log-line')).map(
    (l) => l.textContent
  );
});
console.log('--- LOGS ---');
logs.forEach((l) => console.log(l));

// Check console errors
const consoleMessages = [];
page.on('console', (msg) => consoleMessages.push(msg.text()));

// Screenshot
await page.screenshot({ path: '/tmp/simulator-connected.png' });
console.log('Screenshot: /tmp/simulator-connected.png');

await browser.close();
