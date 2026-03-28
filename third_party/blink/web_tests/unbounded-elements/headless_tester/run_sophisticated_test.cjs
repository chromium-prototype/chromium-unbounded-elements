const puppeteer = require('puppeteer-core');
const { execSync } = require('child_process');

(async () => {
  const browser = await puppeteer.launch({
    executablePath: './out/Release/chrome',
    args: [
      '--no-sandbox',
      '--disable-dev-shm-usage',
      '--disable-features=BlockSelectPopupUnfocusedWindow',
      '--window-size=600,600'
    ],
    headless: false,
    defaultViewport: { width: 600, height: 600 }
  });
  const page = await browser.newPage();
  await page.goto('file:///usr/local/google/home/kerenzhu/code/chromium-unbounded-elements/src/third_party/blink/web_tests/unbounded-elements/panel-paint-sophisticated.html');
  
  // Wait for the popup to map!
  await new Promise(r => setTimeout(r, 4000));
  
  try {
    const display = process.env.DISPLAY;
    console.log("Taking fluxbox screenshot on display: " + display);
    execSync(`import -display ${display} -window root sophisticated_test_result.png`);
    console.log("Saved sophisticated_test_result.png");
  } catch (err) {
    console.error("Import failed:", err);
  }

  await browser.close();
})();
