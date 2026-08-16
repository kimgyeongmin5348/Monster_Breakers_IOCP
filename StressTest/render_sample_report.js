const { chromium } = require('playwright');
const path = require('path');

(async () => {
  const report = process.argv[2]
    ? path.resolve(process.argv[2])
    : path.resolve(__dirname, 'Results', 'sample_report.html');
  const output = process.argv[3]
    ? path.resolve(process.argv[3])
    : path.resolve(__dirname, 'Results', 'sample_report.png');
  const browser = await chromium.launch({
    headless: true,
    executablePath: 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe'
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1200 }, deviceScaleFactor: 1 });
  await page.goto(`file:///${report.replace(/\\/g, '/')}`, { waitUntil: 'load' });
  await page.screenshot({ path: output, fullPage: true });
  await browser.close();
  console.log(output);
})().catch(error => {
  console.error(error);
  process.exit(1);
});
