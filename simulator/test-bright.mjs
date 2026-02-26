import puppeteer from 'puppeteer';

const browser = await puppeteer.launch({ headless: true });
const page = await browser.newPage();
await page.setViewport({ width: 1400, height: 900 });

await page.goto('http://localhost:5176/', { waitUntil: 'networkidle2' });

await page.evaluate(() => {
  localStorage.setItem('sim_brokerUrl', 'ws://localhost:9001');
  localStorage.setItem('sim_pixieId', '1');
});

await page.reload({ waitUntil: 'networkidle2' });
await page.waitForSelector('button');

const connectBtn = await page.waitForSelector('button:not([disabled])');
await connectBtn.click();

try {
  await page.waitForFunction(() => {
    const s = document.querySelector('.status.online');
    return s !== null;
  }, { timeout: 15000 });
  console.log('Connected!');
} catch {
  console.log('Connection timeout');
}

// Wait for photo
await new Promise((r) => setTimeout(r, 5000));

// Increase brightness via the slider
await page.evaluate(() => {
  const slider = document.querySelector('input[type="range"]');
  if (slider) {
    const nativeInputValueSetter = Object.getOwnPropertyDescriptor(
      window.HTMLInputElement.prototype, 'value'
    ).set;
    nativeInputValueSetter.call(slider, 200);
    slider.dispatchEvent(new Event('input', { bubbles: true }));
    slider.dispatchEvent(new Event('change', { bubbles: true }));
  }
});

await new Promise((r) => setTimeout(r, 2000));

// Get logs
const logs = await page.evaluate(() => {
  return Array.from(document.querySelectorAll('.log-line')).map(l => l.textContent);
});
console.log('--- LOGS ---');
logs.forEach(l => console.log(l));

await page.screenshot({ path: '/tmp/simulator-bright.png' });
console.log('Screenshot: /tmp/simulator-bright.png');

await browser.close();
