# The iPad / web remote page in a real (headless) browser against a running
# LiveRack test build: live socket, meters, bypass, plugin view, edit mode.
# Uses LIVERACK_TEST_CHROME if set, otherwise Playwright's own Chromium.
import asyncio, os
from playwright.async_api import async_playwright
async def main():
    async with async_playwright() as p:
        chrome = os.environ.get("LIVERACK_TEST_CHROME")
        b = await p.chromium.launch(executable_path=chrome or None, args=["--no-sandbox"])
        pg = await b.new_page(viewport={"width": 1180, "height": 820})
        errors = []
        pg.on("pageerror", lambda e: errors.append(str(e)))
        await pg.goto("http://localhost:8090/")
        await pg.wait_for_timeout(1500)
        st = lambda: pg.evaluate("fetch('/state').then(r=>r.json())")
        print("WEB live socket open:", await pg.evaluate("live && live.readyState === 1"), "strips:", await pg.locator(".strip").count())
        w1 = await pg.evaluate("document.querySelector('[data-m=\"4\"]').style.width")
        await pg.wait_for_timeout(700)
        w2 = await pg.evaluate("document.querySelector('[data-m=\"4\"]').style.width")
        print("WEB meter widths", w1, w2, "light ok:", await pg.evaluate("document.getElementById('light').className"))
        await pg.locator("[data-bypass='1']").click(); await pg.wait_for_timeout(500)
        s = await st(); print("WEB bypass via live:", s["racks"][1]["bypass"], "button:", await pg.locator("[data-bypass='1']").inner_text())
        await pg.locator("[data-bypass='1']").click(); await pg.wait_for_timeout(500)
        # open a plugin's sliders (view query over live)
        await pg.locator("[data-open='0:1']").click(); await pg.wait_for_timeout(1200)
        print("WEB view over live:", await pg.evaluate("state.view && state.view.name"), "params", await pg.evaluate("state.view && state.view.params.length"),
              "plugin window shown:", await pg.evaluate("document.getElementById('gui').className"))
        # edit mode still lists shows/drivers over live
        await pg.goto("http://localhost:8090/"); await pg.wait_for_timeout(1200)
        await pg.click("#editBtn"); await pg.wait_for_timeout(800)
        print("WEB edit: add-plugin buttons", await pg.locator("[data-addp]").count(), "drivers in state", await pg.evaluate("state.drivers && state.drivers.length"))
        out = os.environ.get("LIVERACK_TEST_OUT")
        if out:
            await pg.screenshot(path=os.path.join(out, "ipad-page.png"))
        print("WEB errors:", errors)
        await b.close()
asyncio.run(main())
